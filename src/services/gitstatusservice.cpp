#include "services/gitstatusservice.h"
#include "services/cloudmounts.h"
#include "services/filesystemtasks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

bool gitStatusRunningInFlatpak()
{
    static const bool inSandbox = QFile::exists(QStringLiteral("/.flatpak-info"));
    return inSandbox;
}

bool gitStatusHostToolAvailable(const QString &program)
{
    if (!gitStatusRunningInFlatpak())
        return !QStandardPaths::findExecutable(program).isEmpty();

    if (QStandardPaths::findExecutable(QStringLiteral("flatpak-spawn")).isEmpty())
        return false;

    QProcess proc;
    proc.start(QStringLiteral("flatpak-spawn"),
               {QStringLiteral("--host"), program, QStringLiteral("--version")});
    return proc.waitForFinished(2000) && proc.exitCode() == 0;
}

void startGitTool(QProcess *process, const QStringList &arguments)
{
    QStringList safeArguments {
        // Never take .git/index.lock: we get SIGKILLed on app exit and a
        // half-written refresh would leave the lock behind for the user.
        QStringLiteral("--no-optional-locks"),
        QStringLiteral("-c"), QStringLiteral("core.fsmonitor=false"),
        QStringLiteral("-c"), QStringLiteral("core.hooksPath=/dev/null")
    };
    safeArguments.append(arguments);

    if (gitStatusRunningInFlatpak()) {
        QStringList hostArgs;
        hostArgs << QStringLiteral("--host") << QStringLiteral("git") << safeArguments;
        process->start(QStringLiteral("flatpak-spawn"), hostArgs);
        return;
    }

    process->start(QStringLiteral("git"), safeArguments);
}

} // namespace

GitStatusService::GitStatusService(QObject *parent)
    : QObject(parent)
{
    m_gitAvailable = gitStatusHostToolAvailable(QStringLiteral("git"));

    m_indexWatcher = new QFileSystemWatcher(this);
    connect(m_indexWatcher, &QFileSystemWatcher::fileChanged,
            this, &GitStatusService::onGitIndexChanged);

    m_statusDebounce.setSingleShot(true);
    m_statusDebounce.setInterval(80);
    connect(&m_statusDebounce, &QTimer::timeout, this, &GitStatusService::queryGitStatus);
}

GitStatusService::~GitStatusService()
{
    stopProcesses();
}

void GitStatusService::setRootPath(const QString &path)
{
    if (path == m_rootPath)
        return;

    m_rootPath = path;
    m_statusCache.clear();
    m_dirtyDirs.clear();
    m_statusDebounce.stop();
    stopProcesses();

    if (!m_repoRoot.isEmpty()) {
        const QString oldIndex = m_repoRoot + QStringLiteral("/.git/index");
        if (m_indexWatcher->files().contains(oldIndex))
            m_indexWatcher->removePath(oldIndex);
        m_repoRoot.clear();
        emit statusChanged();
    }

    if (!m_gitAvailable || isRemotePath(path)) {
        emit statusChanged();
        return;
    }

    startFindRepoRoot(path);
}

void GitStatusService::scheduleGitStatus()
{
    m_statusDebounce.start();
}

QString GitStatusService::statusForPath(const QString &path) const
{
    if (path.isEmpty() || m_repoRoot.isEmpty())
        return {};

    auto it = m_statusCache.constFind(path);
    if (it != m_statusCache.constEnd())
        return it.value();

    if (m_dirtyDirs.contains(path))
        return QStringLiteral("dirty");

    return {};
}

bool GitStatusService::isGitRepo() const
{
    return !m_repoRoot.isEmpty();
}

void GitStatusService::stopProcesses()
{
    stopFilesystemProcess(m_repoProcess);
    stopFilesystemProcess(m_gitProcess);
}

void GitStatusService::startFindRepoRoot(const QString &path)
{
    auto *process = new QProcess(this);
    m_repoProcess = process;

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus) {
        if (process != m_repoProcess) {
            process->deleteLater();
            return;
        }

        const QString repoRoot = exitCode == 0
            ? QString::fromUtf8(process->readAllStandardOutput()).trimmed()
            : QString();
        m_repoProcess = nullptr;
        process->deleteLater();

        if (repoRoot.isEmpty()) {
            emit statusChanged();
            return;
        }

        m_repoRoot = repoRoot;
        const QString newIndex = m_repoRoot + QStringLiteral("/.git/index");
        if (QFileInfo::exists(newIndex) && !m_indexWatcher->files().contains(newIndex))
            m_indexWatcher->addPath(newIndex);

        scheduleGitStatus();
    });

    QTimer::singleShot(2000, this, [this, process = QPointer(process)]() {
        if (process && process == m_repoProcess && process->state() != QProcess::NotRunning)
            process->kill();
    });

    startGitTool(process, {QStringLiteral("-C"), path, QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
}

void GitStatusService::queryGitStatus()
{
    if (m_repoRoot.isEmpty())
        return;

    stopFilesystemProcess(m_gitProcess);
    m_gitProcess = new QProcess(this);

    connect(m_gitProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &GitStatusService::onGitStatusFinished);

    startGitTool(m_gitProcess,
                 {QStringLiteral("-C"), m_repoRoot, QStringLiteral("status"),
                  QStringLiteral("--porcelain=v1"),
                  QStringLiteral("-z"),
                  QStringLiteral("--ignored=matching")});
}

void GitStatusService::onGitStatusFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)

    m_statusCache.clear();
    m_dirtyDirs.clear();

    if (exitCode != 0 || !m_gitProcess) {
        if (m_gitProcess) {
            m_gitProcess->deleteLater();
            m_gitProcess = nullptr;
        }
        emit statusChanged();
        return;
    }

    const QByteArray output = m_gitProcess->readAllStandardOutput();
    m_gitProcess->deleteLater();
    m_gitProcess = nullptr;

    if (output.isEmpty()) {
        emit statusChanged();
        return;
    }

    // Parse NUL-separated entries
    const QList<QByteArray> entries = output.split('\0');

    for (int i = 0; i < entries.size(); ++i) {
        const QByteArray &entry = entries[i];
        if (entry.size() < 4)
            continue;

        const char x = entry[0];
        const char y = entry[1];
        // entry[2] is space
        QString relativePath = QString::fromUtf8(entry.mid(3));
        // Strip trailing slash from directory entries (e.g., ignored dirs)
        if (relativePath.endsWith(QLatin1Char('/')))
            relativePath.chop(1);
        const QString absolutePath = m_repoRoot + QLatin1Char('/') + relativePath;

        QString status;

        // Conflicted: any U, or AA, DD
        if (x == 'U' || y == 'U' ||
            (x == 'A' && y == 'A') ||
            (x == 'D' && y == 'D')) {
            status = QStringLiteral("conflicted");
        }
        // Renamed
        else if (x == 'R') {
            status = QStringLiteral("renamed");
            // Skip the next NUL entry (rename destination)
            ++i;
        }
        // Worktree modified
        else if (y == 'M') {
            status = QStringLiteral("modified");
        }
        // Worktree deleted
        else if (y == 'D') {
            status = QStringLiteral("deleted");
        }
        // Index-only staged (M, A in index, clean worktree)
        else if ((x == 'M' || x == 'A') && y == ' ') {
            status = QStringLiteral("staged");
        }
        // Index deleted
        else if (x == 'D' && y == ' ') {
            status = QStringLiteral("deleted");
        }
        // Untracked
        else if (x == '?' && y == '?') {
            status = QStringLiteral("untracked");
        }
        // Ignored
        else if (x == '!' && y == '!') {
            status = QStringLiteral("ignored");
        }

        if (!status.isEmpty()) {
            m_statusCache.insert(absolutePath, status);
            if (status != QStringLiteral("ignored"))
                markParentsDirty(absolutePath);
        }
    }

    emit statusChanged();
}

void GitStatusService::onGitIndexChanged(const QString &path)
{
    // QFileSystemWatcher may auto-remove the path after change; re-add it
    if (!m_indexWatcher->files().contains(path) && QFileInfo::exists(path))
        m_indexWatcher->addPath(path);

    scheduleGitStatus();
}

void GitStatusService::markParentsDirty(const QString &filePath)
{
    QDir dir(filePath);
    // Move to parent of the file
    if (!dir.cdUp())
        return;

    while (dir.absolutePath() != m_repoRoot) {
        const QString dirPath = dir.absolutePath();
        if (m_dirtyDirs.contains(dirPath))
            break; // Already marked, parents are too
        m_dirtyDirs.insert(dirPath, true);
        if (!dir.cdUp())
            break;
    }
}

bool GitStatusService::isRemotePath(const QString &path) const
{
    return path.startsWith(QStringLiteral("smb://")) ||
           path.startsWith(QStringLiteral("sftp://")) ||
           path.startsWith(QStringLiteral("ftp://")) ||
           path.startsWith(QStringLiteral("trash://")) ||
           isCloudMountPath(path);
}

#include "services/gitstatusservice.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

GitStatusService::GitStatusService(QObject *parent)
    : QObject(parent)
{
    m_gitAvailable = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();

    m_indexWatcher = new QFileSystemWatcher(this);
    connect(m_indexWatcher, &QFileSystemWatcher::fileChanged,
            this, &GitStatusService::onGitIndexChanged);
}

void GitStatusService::setRootPath(const QString &path)
{
    if (path == m_rootPath)
        return;

    m_rootPath = path;
    m_statusCache.clear();
    m_dirtyDirs.clear();

    if (!m_gitAvailable || isRemotePath(path)) {
        if (!m_repoRoot.isEmpty()) {
            m_repoRoot.clear();
            emit statusChanged();
        }
        return;
    }

    const QString oldRoot = m_repoRoot;
    findRepoRoot(path);

    if (m_repoRoot != oldRoot) {
        // Stop watching old index
        if (!oldRoot.isEmpty()) {
            const QString oldIndex = oldRoot + QStringLiteral("/.git/index");
            if (m_indexWatcher->files().contains(oldIndex))
                m_indexWatcher->removePath(oldIndex);
        }

        // Watch new index
        if (!m_repoRoot.isEmpty()) {
            const QString newIndex = m_repoRoot + QStringLiteral("/.git/index");
            if (QFileInfo::exists(newIndex))
                m_indexWatcher->addPath(newIndex);
        }
    }

    if (!m_repoRoot.isEmpty())
        queryGitStatus();
    else
        emit statusChanged();
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

void GitStatusService::findRepoRoot(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        m_repoRoot.clear();
        emit statusChanged();
        return;
    }

    QProcess revParse;
    revParse.setWorkingDirectory(path);
    revParse.setProgram(QStringLiteral("git"));
    revParse.setArguments({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    revParse.start();
    revParse.waitForFinished(2000);
    if (revParse.exitCode() != 0) {
        m_repoRoot.clear();
        emit statusChanged();
        return;
    }
    m_repoRoot = QString::fromUtf8(revParse.readAllStandardOutput()).trimmed();
}

void GitStatusService::queryGitStatus()
{
    if (m_repoRoot.isEmpty())
        return;

    // Kill any running process
    if (m_gitProcess) {
        m_gitProcess->disconnect();
        if (m_gitProcess->state() != QProcess::NotRunning) {
            m_gitProcess->kill();
            m_gitProcess->waitForFinished(500);
        }
        m_gitProcess->deleteLater();
    }

    m_gitProcess = new QProcess(this);
    m_gitProcess->setWorkingDirectory(m_repoRoot);

    connect(m_gitProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &GitStatusService::onGitStatusFinished);

    m_gitProcess->start(QStringLiteral("git"),
                        {QStringLiteral("status"),
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
        const QString relativePath = QString::fromUtf8(entry.mid(3));
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

    queryGitStatus();
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
           path.startsWith(QStringLiteral("trash://"));
}

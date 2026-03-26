#include "services/fileoperations.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

FileOperations::FileOperations(QObject *parent)
    : QObject(parent)
{
}

bool FileOperations::busy() const { return m_busy; }
double FileOperations::progress() const { return m_progress; }
QString FileOperations::statusText() const { return m_statusText; }

void FileOperations::copyFiles(const QStringList &sources, const QString &destination)
{
    QStringList args = {"--progress", "-r"};
    for (const auto &src : sources)
        args.append(src);
    args.append(destination + "/");

    m_statusText = QString("Copying %1 item(s)...").arg(sources.size());
    emit statusTextChanged();
    runProcess("rsync", args);
}

void FileOperations::moveFiles(const QStringList &sources, const QString &destination)
{
    // Use rsync to merge, then remove empty source dirs with a shell command
    QStringList args = {"-c", ""};
    // Build: rsync -a --remove-source-files src1 src2 dest/ && find src1 src2 -type d -empty -delete
    QString rsyncCmd = "rsync -a --progress --remove-source-files";
    QString findCmd = "find";
    for (const auto &src : sources) {
        rsyncCmd += " " + QString("'%1'").arg(src);
        findCmd += " " + QString("'%1'").arg(src);
    }
    rsyncCmd += " " + QString("'%1/'").arg(destination);
    findCmd += " -type d -empty -delete 2>/dev/null";
    args[1] = rsyncCmd + " && " + findCmd;

    m_statusText = QString("Moving %1 item(s)...").arg(sources.size());
    emit statusTextChanged();
    runProcess("sh", args);
}

void FileOperations::trashFiles(const QStringList &paths)
{
    QStringList args = {"trash"};
    args.append(paths);
    m_statusText = QString("Trashing %1 item(s)...").arg(paths.size());
    emit statusTextChanged();
    runProcess("gio", args);
}

void FileOperations::deleteFiles(const QStringList &paths)
{
    for (const auto &path : paths) {
        QFileInfo info(path);
        if (info.isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
    }
    emit operationFinished(true, QString());
}

bool FileOperations::rename(const QString &path, const QString &newName)
{
    QFileInfo info(path);
    QString newPath = info.dir().filePath(newName);
    return QFile::rename(path, newPath);
}

void FileOperations::createFolder(const QString &parentPath, const QString &name)
{
    QDir(parentPath).mkdir(name);
}

void FileOperations::createFile(const QString &parentPath, const QString &name)
{
    QFile f(QDir(parentPath).filePath(name));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.close();
}

void FileOperations::openFile(const QString &path)
{
    auto *proc = new QProcess(this);
    proc->start("xdg-open", {path});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

void FileOperations::copyPathToClipboard(const QString &path)
{
    auto *proc = new QProcess(this);
    proc->start("wl-copy", {path});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

void FileOperations::openInTerminal(const QString &dirPath)
{
    QString terminal = qEnvironmentVariable("TERMINAL", "kitty");
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(dirPath);
    proc->start(terminal, {});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

void FileOperations::runProcess(const QString &program, const QStringList &args)
{
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_busy = true;
    m_progress = 0.0;
    emit busyChanged();

    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        parseRsyncProgress(m_process->readAllStandardOutput());
    });

    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        m_busy = false;
        m_progress = 1.0;
        m_statusText.clear();
        emit busyChanged();
        emit statusTextChanged();
        emit operationFinished(exitCode == 0,
            exitCode != 0 ? QString::fromUtf8(m_process->readAllStandardError()) : QString());
        m_process->deleteLater();
        m_process = nullptr;
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        Q_UNUSED(error)
        if (!m_process)
            return;
        m_busy = false;
        m_statusText.clear();
        emit busyChanged();
        emit statusTextChanged();
        emit operationFinished(false, m_process->errorString());
        m_process->deleteLater();
        m_process = nullptr;
    });

    m_process->start(program, args);
}

void FileOperations::parseRsyncProgress(const QByteArray &data)
{
    static QRegularExpression re(R"((\d+)%\s+(\S+/s))");
    QString text = QString::fromUtf8(data);
    auto match = re.match(text);
    if (match.hasMatch()) {
        m_progress = match.captured(1).toDouble() / 100.0;
        QString speed = match.captured(2);
        emit progressChanged(m_progress, speed, QString());
    }
}

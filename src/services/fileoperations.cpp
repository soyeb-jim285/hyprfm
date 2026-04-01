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

void FileOperations::openFileWith(const QString &path, const QString &desktopFile)
{
    auto *proc = new QProcess(this);
    proc->start("gtk-launch", {desktopFile, path});
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

void FileOperations::compressFiles(const QStringList &paths, const QString &format)
{
    if (paths.isEmpty()) return;

    // Determine output name from first file's parent + name
    QFileInfo first(paths.first());
    QString baseName = (paths.size() == 1) ? first.completeBaseName() : "archive";
    QString parentDir = first.absolutePath();

    QString cmd;
    if (format == "zip") {
        QString outPath = parentDir + "/" + baseName + ".zip";
        cmd = "zip -r " + QString("'%1'").arg(outPath);
        for (const auto &p : paths) {
            QFileInfo fi(p);
            cmd += " -j " + QString("'%1'").arg(p);
        }
        // Use cd + relative paths for proper zip structure
        cmd = "cd " + QString("'%1'").arg(parentDir) + " && zip -r " +
              QString("'%1'").arg(outPath);
        for (const auto &p : paths)
            cmd += " " + QString("'%1'").arg(QFileInfo(p).fileName());
    } else if (format == "tar.gz") {
        QString outPath = parentDir + "/" + baseName + ".tar.gz";
        cmd = "tar -czf " + QString("'%1'").arg(outPath) +
              " -C " + QString("'%1'").arg(parentDir);
        for (const auto &p : paths)
            cmd += " " + QString("'%1'").arg(QFileInfo(p).fileName());
    } else if (format == "tar.xz") {
        QString outPath = parentDir + "/" + baseName + ".tar.xz";
        cmd = "tar -cJf " + QString("'%1'").arg(outPath) +
              " -C " + QString("'%1'").arg(parentDir);
        for (const auto &p : paths)
            cmd += " " + QString("'%1'").arg(QFileInfo(p).fileName());
    } else if (format == "tar.bz2") {
        QString outPath = parentDir + "/" + baseName + ".tar.bz2";
        cmd = "tar -cjf " + QString("'%1'").arg(outPath) +
              " -C " + QString("'%1'").arg(parentDir);
        for (const auto &p : paths)
            cmd += " " + QString("'%1'").arg(QFileInfo(p).fileName());
    } else if (format == "tar") {
        QString outPath = parentDir + "/" + baseName + ".tar";
        cmd = "tar -cf " + QString("'%1'").arg(outPath) +
              " -C " + QString("'%1'").arg(parentDir);
        for (const auto &p : paths)
            cmd += " " + QString("'%1'").arg(QFileInfo(p).fileName());
    } else {
        return;
    }

    m_statusText = QString("Compressing %1 item(s)...").arg(paths.size());
    emit statusTextChanged();
    runProcess("sh", {"-c", cmd});
}

void FileOperations::extractArchive(const QString &archivePath, const QString &destination)
{
    QFileInfo fi(archivePath);
    QString ext = fi.suffix().toLower();
    QString fullExt = fi.fileName().toLower();

    // Extract directly to destination — archives with a root folder stay clean,
    // loose files land in the current directory (same as Nautilus "Extract Here")
    QString cmd;
    if (ext == "zip")
        cmd = "unzip -o " + QString("'%1'").arg(archivePath) + " -d " + QString("'%1'").arg(destination);
    else if (fullExt.endsWith(".tar.gz") || fullExt.endsWith(".tgz"))
        cmd = "tar -xzf " + QString("'%1'").arg(archivePath) + " -C " + QString("'%1'").arg(destination);
    else if (fullExt.endsWith(".tar.xz") || fullExt.endsWith(".txz"))
        cmd = "tar -xJf " + QString("'%1'").arg(archivePath) + " -C " + QString("'%1'").arg(destination);
    else if (fullExt.endsWith(".tar.bz2") || fullExt.endsWith(".tbz2"))
        cmd = "tar -xjf " + QString("'%1'").arg(archivePath) + " -C " + QString("'%1'").arg(destination);
    else if (ext == "tar")
        cmd = "tar -xf " + QString("'%1'").arg(archivePath) + " -C " + QString("'%1'").arg(destination);
    else if (ext == "gz")
        cmd = "gunzip -k " + QString("'%1'").arg(archivePath);
    else if (ext == "xz")
        cmd = "unxz -k " + QString("'%1'").arg(archivePath);
    else if (ext == "bz2")
        cmd = "bunzip2 -k " + QString("'%1'").arg(archivePath);
    else
        return;

    m_statusText = "Extracting...";
    emit statusTextChanged();
    runProcess("sh", {"-c", cmd});
}

QString FileOperations::archiveRootFolder(const QString &archivePath)
{
    QFileInfo fi(archivePath);
    QString ext = fi.suffix().toLower();
    QString fullExt = fi.fileName().toLower();

    // List archive contents and check if all entries share a single root folder
    QString cmd;
    if (ext == "zip")
        cmd = "unzip -Z1 " + QString("'%1'").arg(archivePath);
    else if (fullExt.endsWith(".tar.gz") || fullExt.endsWith(".tgz"))
        cmd = "tar -tzf " + QString("'%1'").arg(archivePath);
    else if (fullExt.endsWith(".tar.xz") || fullExt.endsWith(".txz"))
        cmd = "tar -tJf " + QString("'%1'").arg(archivePath);
    else if (fullExt.endsWith(".tar.bz2") || fullExt.endsWith(".tbz2"))
        cmd = "tar -tjf " + QString("'%1'").arg(archivePath);
    else if (ext == "tar")
        cmd = "tar -tf " + QString("'%1'").arg(archivePath);
    else
        return {};

    QProcess proc;
    proc.start("sh", {"-c", cmd});
    if (!proc.waitForFinished(5000))
        return {};

    QStringList entries = QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    if (entries.isEmpty())
        return {};

    // Find common root: first path component of every entry
    QString root;
    for (const auto &entry : entries) {
        QString top = entry.section('/', 0, 0);
        if (root.isEmpty())
            root = top;
        else if (top != root)
            return {}; // multiple top-level entries, no single root
    }
    return root;
}

bool FileOperations::isArchive(const QString &path)
{
    QString lower = path.toLower();
    return lower.endsWith(".zip") || lower.endsWith(".tar") ||
           lower.endsWith(".tar.gz") || lower.endsWith(".tgz") ||
           lower.endsWith(".tar.xz") || lower.endsWith(".txz") ||
           lower.endsWith(".tar.bz2") || lower.endsWith(".tbz2") ||
           lower.endsWith(".gz") || lower.endsWith(".xz") || lower.endsWith(".bz2");
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

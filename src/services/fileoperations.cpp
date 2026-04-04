#include "services/fileoperations.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>
#include <QUrl>
#include <unistd.h>

namespace {

bool isTrashUriPath(const QString &path)
{
    return QUrl(path).scheme() == "trash";
}

QString currentUidString()
{
    return QString::number(geteuid());
}

QString trashUriRootPath()
{
    return QStringLiteral("trash:///");
}

QString homeTrashRootPath()
{
    return QDir::cleanPath(QDir::homePath() + "/.local/share/Trash");
}

QString homeTrashFilesPath()
{
    return homeTrashRootPath() + "/files";
}

QString existingLookupPathFor(const QString &path)
{
    QString candidate = QDir::cleanPath(path);
    if (candidate.isEmpty())
        return QDir::homePath();

    const QFileInfo info(candidate);
    candidate = info.isDir() ? info.absoluteFilePath() : info.absolutePath();

    while (!candidate.isEmpty() && !QFileInfo::exists(candidate)) {
        const QString parent = QFileInfo(candidate).absolutePath();
        if (parent == candidate)
            break;
        candidate = parent;
    }

    return QFileInfo::exists(candidate) ? candidate : QDir::homePath();
}

QStringList trashRootCandidatesForPath(const QString &path)
{
    QStringList roots;

    const QString cleanPath = QDir::cleanPath(path);
    if (!cleanPath.isEmpty()) {
        const QString lookupPath = existingLookupPathFor(cleanPath);
        const QString storageRoot = QDir::cleanPath(QStorageInfo(lookupPath).rootPath());
        if (!storageRoot.isEmpty() && storageRoot != "/") {
            const QString uid = currentUidString();
            roots.append(QDir(storageRoot).filePath(".Trash-" + uid));
            roots.append(QDir(storageRoot).filePath(".Trash/" + uid));
        }
    }

    roots.append(homeTrashRootPath());
    roots.removeDuplicates();
    return roots;
}

QString matchingTrashFilesRoot(const QString &path)
{
    if (isTrashUriPath(path))
        return trashUriRootPath();

    const QString cleanPath = QDir::cleanPath(path);
    if (cleanPath.isEmpty())
        return {};

    for (const QString &trashRoot : trashRootCandidatesForPath(cleanPath)) {
        const QString filesRoot = QDir(trashRoot).filePath("files");
        if (cleanPath == filesRoot || cleanPath.startsWith(filesRoot + "/"))
            return filesRoot;
    }

    return {};
}

QString trashUriForPath(const QString &path)
{
    const QUrl url(path);
    if (url.scheme() == "trash")
        return url.toString(QUrl::FullyEncoded);

    const QString filesRoot = matchingTrashFilesRoot(path);
    if (filesRoot.isEmpty())
        return {};

    const QString relativePath = QDir::cleanPath(QDir(filesRoot).relativeFilePath(QDir::cleanPath(path)));
    if (relativePath.isEmpty() || relativePath == "." || relativePath.startsWith("../"))
        return {};

    QUrl trashUrl;
    trashUrl.setScheme("trash");
    trashUrl.setPath("/" + QDir::fromNativeSeparators(relativePath));
    return trashUrl.toString(QUrl::FullyEncoded);
}

QString shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace("'", "'\"'\"'");
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QStringList nameParts(const QString &name)
{
    const int dotIndex = name.lastIndexOf('.');
    if (dotIndex > 0)
        return {name.left(dotIndex), name.mid(dotIndex)};
    return {name, QString()};
}

struct RenameOperation {
    QString sourcePath;
    QString targetPath;
    QString tempPath;
};

QVariantMap renameResult(bool success, const QString &error = {}, const QStringList &changedPaths = {})
{
    QVariantMap result;
    result["success"] = success;
    result["error"] = error;
    result["changedPaths"] = changedPaths;
    return result;
}

QString temporaryRenamePathFor(const QString &sourcePath)
{
    const QDir dir = QFileInfo(sourcePath).dir();

    QString tempPath;
    do {
        tempPath = dir.filePath(QStringLiteral(".hyprfm-rename-%1.tmp")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    } while (QFileInfo::exists(tempPath));

    return tempPath;
}

QString renameTargetError(const QString &targetPath)
{
    const QString fileName = QFileInfo(targetPath).fileName();
    if (fileName.isEmpty() || fileName == "." || fileName == "..")
        return QStringLiteral("Enter a valid target name");

    const QString parentDir = QFileInfo(targetPath).absolutePath();
    if (parentDir.isEmpty() || !QDir(parentDir).exists())
        return QStringLiteral("Target folder does not exist");

    return {};
}

}

FileOperations::FileOperations(QObject *parent)
    : QObject(parent)
{
}

bool FileOperations::busy() const { return m_busy; }
double FileOperations::progress() const { return m_progress; }
QString FileOperations::statusText() const { return m_statusText; }

void FileOperations::copyFiles(const QStringList &sources, const QString &destination)
{
    transferResolvedItems(transferPlan(sources, destination), false);
}

void FileOperations::copyResolvedItems(const QVariantList &operations)
{
    transferResolvedItems(operations, false);
}

void FileOperations::moveFiles(const QStringList &sources, const QString &destination)
{
    transferResolvedItems(transferPlan(sources, destination), true);
}

void FileOperations::moveResolvedItems(const QVariantList &operations)
{
    transferResolvedItems(operations, true);
}

void FileOperations::trashFiles(const QStringList &paths)
{
    QStringList args = {"trash"};
    args.append(paths);
    m_statusText = QString("Trashing %1 item(s)...").arg(paths.size());
    emit statusTextChanged();
    runProcess("gio", args);
}

void FileOperations::restoreFromTrash(const QStringList &paths)
{
    QStringList trashUris;
    for (const QString &path : paths) {
        const QString uri = trashUriForPath(path);
        if (!uri.isEmpty() && !trashUris.contains(uri))
            trashUris.append(uri);
    }

    if (trashUris.isEmpty()) {
        emit operationFinished(false, "No trashed items could be restored");
        return;
    }

    QStringList args = {"trash", "--restore", "--force"};
    args.append(trashUris);

    m_statusText = QString("Restoring %1 item(s)...").arg(trashUris.size());
    emit statusTextChanged();
    runProcess("gio", args);
}

bool FileOperations::isTrashPath(const QString &path) const
{
    return isTrashUriPath(path) || !matchingTrashFilesRoot(path).isEmpty();
}

QString FileOperations::trashFilesPathFor(const QString &path) const
{
    if (isTrashUriPath(path))
        return trashUriRootPath();

    const QString matchedRoot = matchingTrashFilesRoot(path);
    if (!matchedRoot.isEmpty())
        return matchedRoot;

    const QStringList roots = trashRootCandidatesForPath(path);
    if (!roots.isEmpty())
        return QDir(roots.first()).filePath("files");

    return homeTrashFilesPath();
}

QVariantList FileOperations::transferPlan(const QStringList &sources, const QString &destination) const
{
    QVariantList plan;
    const QDir destinationDir(destination);

    for (const QString &sourcePath : sources) {
        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists())
            continue;

        const QString sourceName = sourceInfo.fileName();
        const QString targetPath = destinationDir.filePath(sourceName);

        QVariantMap item;
        item["sourcePath"] = sourceInfo.absoluteFilePath();
        item["sourceName"] = sourceName;
        item["targetPath"] = QDir::cleanPath(targetPath);
        item["targetName"] = sourceName;
        item["targetExists"] = QFileInfo::exists(targetPath);
        item["samePath"] = (QDir::cleanPath(sourceInfo.absoluteFilePath()) == QDir::cleanPath(targetPath));
        item["isDir"] = sourceInfo.isDir();
        plan.append(item);
    }

    return plan;
}

QString FileOperations::uniqueNameForDestination(const QString &destinationDir, const QString &desiredName,
                                                const QStringList &blockedNames) const
{
    if (desiredName.isEmpty())
        return {};

    const auto parts = nameParts(desiredName);
    const QString stem = parts.at(0);
    const QString suffix = parts.at(1);
    const QDir dir(destinationDir);

    auto isBlocked = [&](const QString &candidate) {
        return blockedNames.contains(candidate) || QFileInfo::exists(dir.filePath(candidate));
    };

    if (!isBlocked(desiredName))
        return desiredName;

    const QString copyStem = stem + " (copy)";
    const QString firstCandidate = copyStem + suffix;
    if (!isBlocked(firstCandidate))
        return firstCandidate;

    for (int i = 2; i < 10000; ++i) {
        const QString candidate = QString("%1 (copy %2)%3").arg(stem).arg(i).arg(suffix);
        if (!isBlocked(candidate))
            return candidate;
    }

    return {};
}

void FileOperations::deleteFiles(const QStringList &paths)
{
    QStringList trashUris;
    bool localSuccess = true;

    for (const auto &path : paths) {
        if (isTrashUriPath(path)) {
            trashUris.append(QUrl(path).toString(QUrl::FullyEncoded));
            continue;
        }

        QFileInfo info(path);
        if (info.isDir())
            localSuccess = QDir(path).removeRecursively() && localSuccess;
        else
            localSuccess = QFile::remove(path) && localSuccess;
    }

    if (!trashUris.isEmpty()) {
        m_statusText = QString("Deleting %1 item(s)...").arg(trashUris.size());
        emit statusTextChanged();
        QStringList args = {"remove", "-f"};
        args.append(trashUris);
        runProcess("gio", args);
        return;
    }

    emit operationFinished(localSuccess, localSuccess ? QString() : QStringLiteral("Failed to delete one or more items"));
}

void FileOperations::transferResolvedItems(const QVariantList &operations, bool moveOperation)
{
    if (operations.isEmpty()) {
        emit operationFinished(true, QString());
        return;
    }

    QStringList commands;
    int itemCount = 0;

    for (const QVariant &variant : operations) {
        const QVariantMap item = variant.toMap();
        const QString sourcePath = QDir::cleanPath(item.value("sourcePath").toString());
        const QString targetPath = QDir::cleanPath(item.value("targetPath").toString());
        const QString backupPath = item.value("backupPath").toString();
        const bool overwrite = item.value("overwrite").toBool();

        if (sourcePath.isEmpty() || targetPath.isEmpty()) {
            emit operationFinished(false, "Transfer operation is missing a source or destination path");
            return;
        }

        if (sourcePath == targetPath) {
            emit operationFinished(false, QString("Source and destination are the same for %1").arg(QFileInfo(sourcePath).fileName()));
            return;
        }

        const QString targetDirPath = QFileInfo(targetPath).absolutePath();
        commands.append(QString("mkdir -p -- %1").arg(shellQuote(targetDirPath)));

        if (overwrite && !backupPath.isEmpty()) {
            const QString backupDirPath = QFileInfo(backupPath).absolutePath();
            commands.append(QString("mkdir -p -- %1").arg(shellQuote(backupDirPath)));
            commands.append(QString("if [ -e %1 ] || [ -L %1 ]; then rm -rf -- %2 && mv -- %1 %2; fi")
                                .arg(shellQuote(targetPath), shellQuote(backupPath)));
        }

        if (moveOperation)
            commands.append(QString("mv -- %1 %2").arg(shellQuote(sourcePath), shellQuote(targetPath)));
        else
            commands.append(QString("cp -a -- %1 %2").arg(shellQuote(sourcePath), shellQuote(targetPath)));

        ++itemCount;
    }

    m_statusText = QString(moveOperation ? "Moving %1 item(s)..." : "Copying %1 item(s)...").arg(itemCount);
    emit statusTextChanged();
    runProcess("sh", {"-c", commands.join(" && ")});
}

bool FileOperations::rename(const QString &path, const QString &newName)
{
    const QFileInfo info(path);
    const QVariantMap result = renameResolvedItems({QVariantMap {
        {"sourcePath", path},
        {"targetPath", info.dir().filePath(newName)}
    }});
    return result.value("success").toBool();
}

QVariantMap FileOperations::renameResolvedItems(const QVariantList &operations)
{
    QList<RenameOperation> renameOperations;
    QSet<QString> sourcePaths;
    QSet<QString> changedSourcePaths;
    QSet<QString> finalTargetPaths;

    for (const QVariant &variant : operations) {
        const QVariantMap item = variant.toMap();
        const QString sourcePath = QDir::cleanPath(item.value("sourcePath").toString());
        const QString targetPath = QDir::cleanPath(item.value("targetPath").toString());

        if (sourcePath.isEmpty() || targetPath.isEmpty())
            return renameResult(false, QStringLiteral("Rename operation is missing a path"));

        if (sourcePaths.contains(sourcePath))
            return renameResult(false, QStringLiteral("Cannot rename the same item twice in one batch"));

        sourcePaths.insert(sourcePath);

        const QString targetError = renameTargetError(targetPath);
        if (!targetError.isEmpty())
            return renameResult(false, targetError);

        if (finalTargetPaths.contains(targetPath))
            return renameResult(false, QStringLiteral("Two items cannot end with the same name"));

        finalTargetPaths.insert(targetPath);

        if (!QFileInfo::exists(sourcePath)) {
            return renameResult(false, QStringLiteral("%1 no longer exists")
                .arg(QFileInfo(sourcePath).fileName()));
        }

        if (sourcePath == targetPath)
            continue;

        changedSourcePaths.insert(sourcePath);
        renameOperations.append({sourcePath, targetPath, temporaryRenamePathFor(sourcePath)});
    }

    for (const RenameOperation &op : renameOperations) {
        if (QFileInfo::exists(op.targetPath) && !changedSourcePaths.contains(op.targetPath)) {
            return renameResult(false, QStringLiteral("%1 already exists")
                .arg(QFileInfo(op.targetPath).fileName()));
        }
    }

    if (renameOperations.isEmpty())
        return renameResult(true, {}, {});

    QList<int> stagedIndices;
    QList<int> finalizedIndices;
    auto rollback = [&renameOperations, &stagedIndices, &finalizedIndices]() {
        for (int i = finalizedIndices.size() - 1; i >= 0; --i) {
            const RenameOperation &op = renameOperations.at(finalizedIndices.at(i));
            if (QFileInfo::exists(op.targetPath))
                QFile::rename(op.targetPath, op.sourcePath);
        }

        for (int i = stagedIndices.size() - 1; i >= 0; --i) {
            const RenameOperation &op = renameOperations.at(stagedIndices.at(i));
            if (QFileInfo::exists(op.tempPath))
                QFile::rename(op.tempPath, op.sourcePath);
        }
    };

    for (int i = 0; i < renameOperations.size(); ++i) {
        const RenameOperation &op = renameOperations.at(i);
        if (!QFile::rename(op.sourcePath, op.tempPath)) {
            rollback();
            return renameResult(false, QStringLiteral("Could not prepare %1 for renaming")
                .arg(QFileInfo(op.sourcePath).fileName()));
        }

        stagedIndices.append(i);
    }

    for (int i = 0; i < renameOperations.size(); ++i) {
        const RenameOperation &op = renameOperations.at(i);
        if (!QFile::rename(op.tempPath, op.targetPath)) {
            rollback();
            return renameResult(false, QStringLiteral("Could not rename %1")
                .arg(QFileInfo(op.sourcePath).fileName()));
        }

        finalizedIndices.append(i);
    }

    QStringList changedPaths;
    for (const RenameOperation &op : renameOperations)
        changedPaths.append(op.targetPath);

    return renameResult(true, {}, changedPaths);
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
    if (isTrashUriPath(path))
        proc->start("gio", {"open", QUrl(path).toString(QUrl::FullyEncoded)});
    else
        proc->start("xdg-open", {path});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

bool FileOperations::pathExists(const QString &path) const
{
    if (isTrashUriPath(path)) {
        QProcess proc;
        proc.start("gio", {"info", QUrl(path).toString(QUrl::FullyEncoded)});
        return proc.waitForFinished(2000) && proc.exitCode() == 0;
    }

    return QFileInfo::exists(path);
}

void FileOperations::emptyTrash()
{
    m_statusText = "Emptying trash...";
    emit statusTextChanged();
    runProcess("gio", {"trash", "--empty"});
}

void FileOperations::openFileWith(const QString &path, const QString &desktopFile)
{
    auto *proc = new QProcess(this);
    proc->start("gtk-launch", {desktopFile, path});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

bool FileOperations::hasClipboardImage() const
{
    if (!clipboardImageData().isEmpty())
        return true;

    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;

    const QMimeData *mime = clipboard->mimeData();
    return mime && mime->hasImage();
}

QString FileOperations::pasteClipboardImage(const QString &destinationDir)
{
    const QString outputPath = uniqueImagePastePath(destinationDir);
    if (outputPath.isEmpty()) {
        emit operationFinished(false, "Destination folder does not exist");
        return {};
    }

    // Prefer writing raw wl-paste bytes directly to preserve the original image exactly
    const QByteArray rawImage = clipboardImageData();
    if (!rawImage.isEmpty()) {
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly)) {
            emit operationFinished(false, "Failed to write clipboard image");
            return {};
        }
        file.write(rawImage);
        file.close();
        emit operationFinished(true, QString());
        return outputPath;
    }

    // Fallback: Qt clipboard (e.g. X11 or non-Wayland)
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || !clipboard->mimeData() || !clipboard->mimeData()->hasImage()) {
        emit operationFinished(false, "Clipboard does not contain an image");
        return {};
    }

    QImage image = clipboard->image();
    if (image.isNull()) {
        const QVariant imageData = clipboard->mimeData()->imageData();
        if (imageData.canConvert<QImage>())
            image = qvariant_cast<QImage>(imageData);
        else if (imageData.canConvert<QPixmap>())
            image = qvariant_cast<QPixmap>(imageData).toImage();
    }

    if (image.isNull()) {
        emit operationFinished(false, "Clipboard image could not be read");
        return {};
    }

    if (!image.save(outputPath, "PNG")) {
        emit operationFinished(false, "Failed to save clipboard image");
        return {};
    }

    emit operationFinished(true, QString());
    return outputPath;
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

QString FileOperations::uniqueImagePastePath(const QString &destinationDir) const
{
    QDir dir(destinationDir);
    if (!dir.exists())
        return {};

    const QString baseName = "Pasted image";
    const QString extension = ".png";
    QString candidate = dir.filePath(baseName + extension);
    if (!QFileInfo::exists(candidate))
        return candidate;

    for (int i = 2; i < 10000; ++i) {
        candidate = dir.filePath(QString("%1 %2%3").arg(baseName).arg(i).arg(extension));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }

    return {};
}

QString FileOperations::conflictBackupPath(const QString &targetPath) const
{
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty())
        cacheRoot = QDir::homePath() + "/.cache/hyprfm";

    QDir backupDir(cacheRoot + "/conflict-backups");
    backupDir.mkpath(".");

    const QString baseName = QFileInfo(targetPath).fileName();
    const QString uniqueName = QUuid::createUuid().toString(QUuid::WithoutBraces) + "-" + baseName;
    return backupDir.filePath(uniqueName);
}

QByteArray FileOperations::clipboardImageData() const
{
    const QString wlPastePath = QStandardPaths::findExecutable("wl-paste");
    if (wlPastePath.isEmpty())
        return {};

    QProcess listProcess;
    listProcess.start(wlPastePath, {"--list-types"});
    if (!listProcess.waitForFinished(1000) || listProcess.exitCode() != 0)
        return {};

    const QStringList types = QString::fromUtf8(listProcess.readAllStandardOutput())
                                  .split('\n', Qt::SkipEmptyParts);
    QString imageType;
    if (types.contains("image/png"))
        imageType = "image/png";
    else {
        for (const QString &type : types) {
            if (type.startsWith("image/")) {
                imageType = type;
                break;
            }
        }
    }

    if (imageType.isEmpty())
        return {};

    QProcess imageProcess;
    imageProcess.start(wlPastePath, {"--no-newline", "--type", imageType});
    if (!imageProcess.waitForFinished(3000) || imageProcess.exitCode() != 0)
        return {};

    return imageProcess.readAllStandardOutput();
}

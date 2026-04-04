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
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <unistd.h>

namespace {

bool isTrashUriPath(const QString &path)
{
    return QUrl(path).scheme() == "trash";
}

bool isUriPath(const QString &path)
{
    const QUrl url(path);
    return url.isValid() && !url.scheme().isEmpty();
}

bool isRemoteUriPath(const QString &path)
{
    const QUrl url(path);
    return url.isValid() && !url.scheme().isEmpty()
        && url.scheme() != QStringLiteral("file")
        && url.scheme() != QStringLiteral("trash");
}

QString normalizeLocation(const QString &path)
{
    if (path.isEmpty())
        return {};

    const QUrl url(path);
    if (url.isValid() && url.scheme() == QStringLiteral("file"))
        return QDir::cleanPath(url.toLocalFile());

    if (url.isValid() && !url.scheme().isEmpty()) {
        QUrl normalized = url.adjusted(QUrl::NormalizePathSegments);
        QString urlPath = normalized.path();
        if (urlPath.isEmpty())
            urlPath = QStringLiteral("/");
        if (urlPath.size() > 1 && urlPath.endsWith('/'))
            urlPath.chop(1);
        normalized.setPath(urlPath);
        return normalized.toString(QUrl::FullyEncoded);
    }

    return QDir::cleanPath(path);
}

QString gioLocationArg(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    if (isUriPath(normalized))
        return QUrl(normalized).toString(QUrl::FullyEncoded);
    return normalized;
}

QString locationFileName(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    if (isUriPath(normalized)) {
        const QUrl url(normalized);
        QString fileName = QUrl::fromPercentEncoding(url.fileName().toUtf8());
        if (!fileName.isEmpty())
            return fileName;
        if (!url.host().isEmpty())
            return url.host();
        return url.scheme().toUpper();
    }

    if (normalized == QStringLiteral("/"))
        return normalized;

    const QFileInfo info(normalized);
    return info.fileName().isEmpty() ? normalized : info.fileName();
}

QString parentLocation(const QString &path)
{
    const QString normalized = normalizeLocation(path);

    if (isTrashUriPath(normalized)) {
        QString current = normalized;
        if (current.size() > 9 && current.endsWith('/'))
            current.chop(1);
        if (current == QStringLiteral("trash://"))
            current = QStringLiteral("trash:///");
        if (current == QStringLiteral("trash:///") || current == QStringLiteral("trash://"))
            return QStringLiteral("trash:///");

        const int slashIndex = current.lastIndexOf('/');
        return slashIndex <= 8 ? QStringLiteral("trash:///") : current.left(slashIndex);
    }

    if (isRemoteUriPath(normalized)) {
        QUrl url(normalized);
        QString urlPath = url.path();
        if (urlPath.isEmpty() || urlPath == QStringLiteral("/"))
            return normalizeLocation(url.toString(QUrl::FullyEncoded));

        if (urlPath.endsWith('/'))
            urlPath.chop(1);

        const int slashIndex = urlPath.lastIndexOf('/');
        url.setPath(slashIndex <= 0 ? QStringLiteral("/") : urlPath.left(slashIndex));
        return normalizeLocation(url.toString(QUrl::FullyEncoded));
    }

    const QFileInfo info(normalized);
    return info.absolutePath();
}

QString joinLocation(const QString &parentPath, const QString &name)
{
    const QString normalizedParent = normalizeLocation(parentPath);
    if (isUriPath(normalizedParent)) {
        QUrl url(normalizedParent);
        QString urlPath = url.path();
        if (!urlPath.endsWith('/'))
            urlPath += '/';
        url.setPath(urlPath + name);
        return normalizeLocation(url.toString(QUrl::FullyEncoded));
    }

    return QDir(normalizedParent).filePath(name);
}

bool gioPathExists(const QString &path)
{
    QProcess proc;
    proc.start(QStringLiteral("gio"), {QStringLiteral("info"), gioLocationArg(path)});
    return proc.waitForFinished(3000) && proc.exitCode() == 0;
}

QVariantMap remotePathInfo(const QString &path)
{
    QVariantMap result;
    const QString normalized = normalizeLocation(path);
    const QStringList args = {
        QStringLiteral("info"),
        QStringLiteral("-a"),
        QStringLiteral("standard::type,standard::size,standard::is-symlink"),
        gioLocationArg(normalized)
    };

    QProcess proc;
    proc.start(QStringLiteral("gio"), args);
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return result;

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    result[QStringLiteral("exists")] = true;
    result[QStringLiteral("fileName")] = locationFileName(normalized);
    result[QStringLiteral("path")] = normalized;

    static const QRegularExpression typeRe(R"(^type:\s*(.+)$)", QRegularExpression::MultilineOption);
    static const QRegularExpression sizeRe(R"(^\s*standard::size:\s*(\d+).*$)", QRegularExpression::MultilineOption);
    static const QRegularExpression symlinkRe(R"(^\s*standard::is-symlink:\s*(TRUE|FALSE).*$)", QRegularExpression::MultilineOption);

    const QString typeText = typeRe.match(output).captured(1).trimmed().toLower();
    result[QStringLiteral("isDir")] = typeText.contains(QStringLiteral("directory"));
    result[QStringLiteral("size")] = sizeRe.match(output).captured(1).toLongLong();
    result[QStringLiteral("isSymlink")] = symlinkRe.match(output).captured(1) == QStringLiteral("TRUE");
    return result;
}

QVariantMap sourceInfoForPath(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    if (isRemoteUriPath(normalized))
        return remotePathInfo(normalized);

    QVariantMap result;
    QFileInfo info(normalized);
    result[QStringLiteral("exists")] = info.exists();
    result[QStringLiteral("fileName")] = info.fileName();
    result[QStringLiteral("path")] = info.absoluteFilePath();
    result[QStringLiteral("isDir")] = info.isDir();
    result[QStringLiteral("size")] = info.size();
    result[QStringLiteral("isSymlink")] = info.isSymLink();
    return result;
}

bool moveLocationSync(const QString &sourcePath, const QString &targetPath, QString *error = nullptr)
{
    const QString normalizedSource = normalizeLocation(sourcePath);
    const QString normalizedTarget = normalizeLocation(targetPath);

    if (!isUriPath(normalizedSource) && !isUriPath(normalizedTarget)) {
        const bool ok = QFile::rename(normalizedSource, normalizedTarget);
        if (!ok && error)
            *error = QStringLiteral("Could not rename %1").arg(locationFileName(normalizedSource));
        return ok;
    }

    QProcess proc;
    proc.start(QStringLiteral("gio"), {QStringLiteral("move"), QStringLiteral("-T"),
                                        gioLocationArg(normalizedSource), gioLocationArg(normalizedTarget)});
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }

    return true;
}

bool makeDirectorySync(const QString &path, QString *error = nullptr)
{
    const QString normalized = normalizeLocation(path);
    if (!isUriPath(normalized)) {
        const bool ok = QDir().mkpath(normalized);
        if (!ok && error)
            *error = QStringLiteral("Could not create folder");
        return ok;
    }

    QProcess proc;
    proc.start(QStringLiteral("gio"), {QStringLiteral("mkdir"), QStringLiteral("-p"), gioLocationArg(normalized)});
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }

    return true;
}

bool createEmptyFileSync(const QString &path, QString *error = nullptr)
{
    const QString normalized = normalizeLocation(path);
    if (!isUriPath(normalized)) {
        QFile file(normalized);
        const bool ok = file.open(QIODevice::WriteOnly);
        file.close();
        if (!ok && error)
            *error = QStringLiteral("Could not create file");
        return ok;
    }

    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        if (error)
            *error = QStringLiteral("Could not create temporary file");
        return false;
    }
    tempFile.close();

    QProcess proc;
    proc.start(QStringLiteral("gio"), {QStringLiteral("copy"), QStringLiteral("-T"),
                                        gioLocationArg(tempFile.fileName()), gioLocationArg(normalized)});
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return false;
    }

    return true;
}

bool pathExistsSync(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    if (isUriPath(normalized))
        return gioPathExists(normalized);
    return QFileInfo::exists(normalized);
}

QVariantList buildBreadcrumbs(const QString &path)
{
    QVariantList segments;
    const QString normalized = normalizeLocation(path);

    if (normalized.isEmpty())
        return segments;

    if (isTrashUriPath(normalized)) {
        if (normalized == QStringLiteral("trash:///") || normalized == QStringLiteral("trash://")) {
            segments.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("Trash")},
                                        {QStringLiteral("fullPath"), QStringLiteral("trash:///")}});
            return segments;
        }

        QString current = normalized;
        if (current.endsWith('/'))
            current.chop(1);
        QString remainder = current.mid(QStringLiteral("trash:///").size());
        segments.append(QVariantMap{{QStringLiteral("label"), QStringLiteral("Trash")},
                                    {QStringLiteral("fullPath"), QStringLiteral("trash:///")}});

        QString accumulated = QStringLiteral("trash:///");
        const QStringList parts = remainder.split('/', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            accumulated = joinLocation(accumulated, QUrl::fromPercentEncoding(part.toUtf8()));
            segments.append(QVariantMap{{QStringLiteral("label"), QUrl::fromPercentEncoding(part.toUtf8())},
                                        {QStringLiteral("fullPath"), accumulated}});
        }
        return segments;
    }

    if (isRemoteUriPath(normalized)) {
        const QUrl url(normalized);
        const QString authority = !url.userName().isEmpty()
            ? QStringLiteral("%1@%2").arg(url.userName(), url.host())
            : url.host().isEmpty() ? url.scheme().toUpper() : url.host();
        const QString rootPath = normalizeLocation(QUrl(url.scheme() + QStringLiteral("://") + url.authority()).toString(QUrl::FullyEncoded));
        segments.append(QVariantMap{{QStringLiteral("label"), authority},
                                    {QStringLiteral("fullPath"), rootPath}});

        QString accumulatedPath;
        const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            accumulatedPath += QStringLiteral("/") + part;
            QUrl step = url;
            step.setPath(accumulatedPath);
            segments.append(QVariantMap{{QStringLiteral("label"), QUrl::fromPercentEncoding(part.toUtf8())},
                                        {QStringLiteral("fullPath"), normalizeLocation(step.toString(QUrl::FullyEncoded))}});
        }
        return segments;
    }

    if (normalized == QStringLiteral("/"))
        return segments;

    QString accumulated;
    const QStringList parts = normalized.split('/', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        accumulated += QStringLiteral("/") + part;
        segments.append(QVariantMap{{QStringLiteral("label"), part},
                                    {QStringLiteral("fullPath"), accumulated}});
    }

    return segments;
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

enum class ArchiveKind {
    None,
    Zip,
    Tar,
    TarGz,
    TarXz,
    TarBz2,
    Gz,
    Xz,
    Bz2,
    SevenZip,
    Rar,
};

ArchiveKind archiveKindForPath(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".tar.gz")) || lower.endsWith(QStringLiteral(".tgz")))
        return ArchiveKind::TarGz;
    if (lower.endsWith(QStringLiteral(".tar.xz")) || lower.endsWith(QStringLiteral(".txz")))
        return ArchiveKind::TarXz;
    if (lower.endsWith(QStringLiteral(".tar.bz2")) || lower.endsWith(QStringLiteral(".tbz2")))
        return ArchiveKind::TarBz2;
    if (lower.endsWith(QStringLiteral(".tar")))
        return ArchiveKind::Tar;
    if (lower.endsWith(QStringLiteral(".zip")))
        return ArchiveKind::Zip;
    if (lower.endsWith(QStringLiteral(".7z")))
        return ArchiveKind::SevenZip;
    if (lower.endsWith(QStringLiteral(".rar")))
        return ArchiveKind::Rar;
    if (lower.endsWith(QStringLiteral(".gz")))
        return ArchiveKind::Gz;
    if (lower.endsWith(QStringLiteral(".xz")))
        return ArchiveKind::Xz;
    if (lower.endsWith(QStringLiteral(".bz2")))
        return ArchiveKind::Bz2;
    return ArchiveKind::None;
}

bool archiveExtractCommand(const QString &archivePath, const QString &destination,
                           QString *program, QStringList *args)
{
    switch (archiveKindForPath(archivePath)) {
    case ArchiveKind::Zip:
        *program = QStringLiteral("unzip");
        *args = {QStringLiteral("-o"), archivePath, QStringLiteral("-d"), destination};
        return true;
    case ArchiveKind::TarGz:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-xzf"), archivePath, QStringLiteral("-C"), destination};
        return true;
    case ArchiveKind::TarXz:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-xJf"), archivePath, QStringLiteral("-C"), destination};
        return true;
    case ArchiveKind::TarBz2:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-xjf"), archivePath, QStringLiteral("-C"), destination};
        return true;
    case ArchiveKind::Tar:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"), destination};
        return true;
    case ArchiveKind::Gz:
        *program = QStringLiteral("gunzip");
        *args = {QStringLiteral("-k"), archivePath};
        return true;
    case ArchiveKind::Xz:
        *program = QStringLiteral("unxz");
        *args = {QStringLiteral("-k"), archivePath};
        return true;
    case ArchiveKind::Bz2:
        *program = QStringLiteral("bunzip2");
        *args = {QStringLiteral("-k"), archivePath};
        return true;
    case ArchiveKind::SevenZip:
    case ArchiveKind::Rar:
        if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
            *program = QStringLiteral("7z");
            *args = {QStringLiteral("x"), QStringLiteral("-aoa"),
                     QStringLiteral("-o%1").arg(destination), archivePath};
            return true;
        }
        if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
            *program = QStringLiteral("bsdtar");
            *args = {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"), destination};
            return true;
        }
        return false;
    case ArchiveKind::None:
        return false;
    }

    return false;
}

bool archiveListCommand(const QString &archivePath, QString *program, QStringList *args)
{
    switch (archiveKindForPath(archivePath)) {
    case ArchiveKind::Zip:
        *program = QStringLiteral("unzip");
        *args = {QStringLiteral("-Z1"), archivePath};
        return true;
    case ArchiveKind::TarGz:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-tzf"), archivePath};
        return true;
    case ArchiveKind::TarXz:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-tJf"), archivePath};
        return true;
    case ArchiveKind::TarBz2:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-tjf"), archivePath};
        return true;
    case ArchiveKind::Tar:
        *program = QStringLiteral("tar");
        *args = {QStringLiteral("-tf"), archivePath};
        return true;
    case ArchiveKind::SevenZip:
    case ArchiveKind::Rar:
        if (!QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty()) {
            *program = QStringLiteral("bsdtar");
            *args = {QStringLiteral("-tf"), archivePath};
            return true;
        }
        if (!QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
            *program = QStringLiteral("7z");
            *args = {QStringLiteral("l"), QStringLiteral("-ba"), QStringLiteral("-slt"), archivePath};
            return true;
        }
        return false;
    case ArchiveKind::Gz:
    case ArchiveKind::Xz:
    case ArchiveKind::Bz2:
    case ArchiveKind::None:
        return false;
    }

    return false;
}

QStringList archiveEntriesFromOutput(const QString &program, const QString &output)
{
    QStringList entries;

    if (program == QStringLiteral("7z")) {
        bool inEntries = false;
        const QStringList lines = output.split('\n');
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed == QStringLiteral("----------")) {
                inEntries = true;
                continue;
            }
            if (!inEntries || !trimmed.startsWith(QStringLiteral("Path = ")))
                continue;

            const QString entry = QDir::cleanPath(trimmed.mid(7).trimmed());
            if (!entry.isEmpty() && entry != QStringLiteral("."))
                entries.append(entry);
        }
        return entries;
    }

    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString entry = line.trimmed();
        if (entry.startsWith(QStringLiteral("./")))
            entry.remove(0, 2);
        entry = QDir::cleanPath(entry);
        if (!entry.isEmpty() && entry != QStringLiteral("."))
            entries.append(entry);
    }

    return entries;
}

QString commonArchiveRootFolder(const QStringList &entries)
{
    QString root;
    for (QString entry : entries) {
        while (entry.startsWith('/'))
            entry.remove(0, 1);
        if (entry.isEmpty())
            continue;

        const QString top = entry.section('/', 0, 0);
        if (top.isEmpty())
            return {};

        if (root.isEmpty())
            root = top;
        else if (top != root)
            return {};
    }

    return root;
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
    QString tempPath;
    do {
        tempPath = joinLocation(parentLocation(sourcePath),
                                QStringLiteral(".hyprfm-rename-%1.tmp")
                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    } while (pathExistsSync(tempPath));

    return tempPath;
}

QString renameTargetError(const QString &targetPath)
{
    const QString fileName = locationFileName(targetPath);
    if (fileName.isEmpty() || fileName == "." || fileName == "..")
        return QStringLiteral("Enter a valid target name");

    const QString parentDir = parentLocation(targetPath);
    if (parentDir.isEmpty() || !pathExistsSync(parentDir))
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
    const QString normalizedDestination = normalizeLocation(destination);

    for (const QString &sourcePath : sources) {
        const QVariantMap sourceInfo = sourceInfoForPath(sourcePath);
        if (!sourceInfo.value(QStringLiteral("exists")).toBool())
            continue;

        const QString sourceName = sourceInfo.value(QStringLiteral("fileName")).toString();
        const QString normalizedSourcePath = sourceInfo.value(QStringLiteral("path")).toString();
        const QString targetPath = normalizeLocation(joinLocation(normalizedDestination, sourceName));

        QVariantMap item;
        item["sourcePath"] = normalizedSourcePath;
        item["sourceName"] = sourceName;
        item["targetPath"] = targetPath;
        item["targetName"] = sourceName;
        item["targetExists"] = pathExistsSync(targetPath);
        item["samePath"] = (normalizedSourcePath == targetPath);
        item["isDir"] = sourceInfo.value(QStringLiteral("isDir")).toBool();
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
    const QString normalizedDestination = normalizeLocation(destinationDir);

    auto isBlocked = [&](const QString &candidate) {
        return blockedNames.contains(candidate) || pathExistsSync(joinLocation(normalizedDestination, candidate));
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
    QStringList uriTargets;
    bool localSuccess = true;

    for (const auto &path : paths) {
        const QString normalized = normalizeLocation(path);
        if (isUriPath(normalized)) {
            uriTargets.append(gioLocationArg(normalized));
            continue;
        }

        QFileInfo info(normalized);
        if (info.isDir())
            localSuccess = QDir(normalized).removeRecursively() && localSuccess;
        else
            localSuccess = QFile::remove(normalized) && localSuccess;
    }

    if (!uriTargets.isEmpty()) {
        m_statusText = QString("Deleting %1 item(s)...").arg(uriTargets.size());
        emit statusTextChanged();
        QStringList args = {"remove", "-f"};
        args.append(uriTargets);
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
    bool useGio = false;

    for (const QVariant &variant : operations) {
        const QVariantMap item = variant.toMap();
        const QString sourcePath = normalizeLocation(item.value("sourcePath").toString());
        const QString targetPath = normalizeLocation(item.value("targetPath").toString());
        const QString backupPath = item.value("backupPath").toString();
        const bool overwrite = item.value("overwrite").toBool();

        if (sourcePath.isEmpty() || targetPath.isEmpty()) {
            emit operationFinished(false, "Transfer operation is missing a source or destination path");
            return;
        }

        if (sourcePath == targetPath) {
            emit operationFinished(false, QString("Source and destination are the same for %1").arg(locationFileName(sourcePath)));
            return;
        }

        useGio = useGio || isUriPath(sourcePath) || isUriPath(targetPath);

        const QString targetDirPath = parentLocation(targetPath);
        if (useGio)
            commands.append(QString("gio mkdir -p %1").arg(shellQuote(gioLocationArg(targetDirPath))));
        else
            commands.append(QString("mkdir -p -- %1").arg(shellQuote(targetDirPath)));

        if (!useGio && overwrite && !backupPath.isEmpty()) {
            const QString backupDirPath = QFileInfo(backupPath).absolutePath();
            commands.append(QString("mkdir -p -- %1").arg(shellQuote(backupDirPath)));
            commands.append(QString("if [ -e %1 ] || [ -L %1 ]; then rm -rf -- %2 && mv -- %1 %2; fi")
                                .arg(shellQuote(targetPath), shellQuote(backupPath)));
        } else if (useGio && overwrite) {
            commands.append(QString("gio remove -f %1 || true").arg(shellQuote(gioLocationArg(targetPath))));
        }

        if (useGio) {
            commands.append(QString("gio %1 -T %2 %3")
                                .arg(moveOperation ? QStringLiteral("move") : QStringLiteral("copy"),
                                     shellQuote(gioLocationArg(sourcePath)),
                                     shellQuote(gioLocationArg(targetPath))));
        } else if (moveOperation) {
            commands.append(QString("mv -- %1 %2").arg(shellQuote(sourcePath), shellQuote(targetPath)));
        } else {
            commands.append(QString("cp -a -- %1 %2").arg(shellQuote(sourcePath), shellQuote(targetPath)));
        }

        ++itemCount;
    }

    m_statusText = QString(moveOperation ? "Moving %1 item(s)..." : "Copying %1 item(s)...").arg(itemCount);
    emit statusTextChanged();
    runProcess("sh", {"-c", commands.join(" && ")});
}

bool FileOperations::rename(const QString &path, const QString &newName)
{
    const QString normalizedPath = normalizeLocation(path);
    const QString targetPath = joinLocation(parentLocation(normalizedPath), newName);
    const QVariantMap result = renameResolvedItems({QVariantMap {
        {"sourcePath", normalizedPath},
        {"targetPath", targetPath}
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
        const QString sourcePath = normalizeLocation(item.value("sourcePath").toString());
        const QString targetPath = normalizeLocation(item.value("targetPath").toString());

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

        if (!pathExistsSync(sourcePath)) {
            return renameResult(false, QStringLiteral("%1 no longer exists")
                .arg(locationFileName(sourcePath)));
        }

        if (sourcePath == targetPath)
            continue;

        changedSourcePaths.insert(sourcePath);
        renameOperations.append({sourcePath, targetPath, temporaryRenamePathFor(sourcePath)});
    }

    for (const RenameOperation &op : renameOperations) {
        if (pathExistsSync(op.targetPath) && !changedSourcePaths.contains(op.targetPath)) {
            return renameResult(false, QStringLiteral("%1 already exists")
                .arg(locationFileName(op.targetPath)));
        }
    }

    if (renameOperations.isEmpty())
        return renameResult(true, {}, {});

    QList<int> stagedIndices;
    QList<int> finalizedIndices;
    auto rollback = [&renameOperations, &stagedIndices, &finalizedIndices]() {
        for (int i = finalizedIndices.size() - 1; i >= 0; --i) {
            const RenameOperation &op = renameOperations.at(finalizedIndices.at(i));
            if (pathExistsSync(op.targetPath))
                moveLocationSync(op.targetPath, op.sourcePath);
        }

        for (int i = stagedIndices.size() - 1; i >= 0; --i) {
            const RenameOperation &op = renameOperations.at(stagedIndices.at(i));
            if (pathExistsSync(op.tempPath))
                moveLocationSync(op.tempPath, op.sourcePath);
        }
    };

    for (int i = 0; i < renameOperations.size(); ++i) {
        const RenameOperation &op = renameOperations.at(i);
        QString error;
        if (!moveLocationSync(op.sourcePath, op.tempPath, &error)) {
            rollback();
            return renameResult(false, QStringLiteral("Could not prepare %1 for renaming")
                .arg(locationFileName(op.sourcePath)));
        }

        stagedIndices.append(i);
    }

    for (int i = 0; i < renameOperations.size(); ++i) {
        const RenameOperation &op = renameOperations.at(i);
        QString error;
        if (!moveLocationSync(op.tempPath, op.targetPath, &error)) {
            rollback();
            return renameResult(false, QStringLiteral("Could not rename %1")
                .arg(locationFileName(op.sourcePath)));
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
    const QString targetPath = joinLocation(parentPath, name);
    QString error;
    if (makeDirectorySync(targetPath, &error) || error.isEmpty())
        return;
    emit operationFinished(false, error);
}

void FileOperations::createFile(const QString &parentPath, const QString &name)
{
    const QString targetPath = joinLocation(parentPath, name);
    QString error;
    if (createEmptyFileSync(targetPath, &error) || error.isEmpty())
        return;
    emit operationFinished(false, error);
}

void FileOperations::openFile(const QString &path)
{
    auto *proc = new QProcess(this);
    const QString normalized = normalizeLocation(path);
    if (isUriPath(normalized))
        proc->start("gio", {"open", gioLocationArg(normalized)});
    else
        proc->start("xdg-open", {normalized});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

bool FileOperations::pathExists(const QString &path) const
{
    return pathExistsSync(path);
}

bool FileOperations::isRemotePath(const QString &path) const
{
    return isRemoteUriPath(normalizeLocation(path));
}

QString FileOperations::parentPath(const QString &path) const
{
    return parentLocation(path);
}

QString FileOperations::displayNameForPath(const QString &path) const
{
    return locationFileName(path);
}

QVariantList FileOperations::breadcrumbSegments(const QString &path) const
{
    return buildBreadcrumbs(path);
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
    proc->start("gtk-launch", {desktopFile, normalizeLocation(path)});
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
    if (isUriPath(dirPath)) {
        emit operationFinished(false, QStringLiteral("Open in Terminal is only available for local folders"));
        return;
    }

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
    } else if (format == "7z") {
        QString outPath = parentDir + "/" + baseName + ".7z";
        cmd = "cd " + shellQuote(parentDir) + " && 7z a " + shellQuote(outPath);
        for (const auto &p : paths)
            cmd += " " + shellQuote(QFileInfo(p).fileName());
    } else {
        return;
    }

    m_statusText = QString("Compressing %1 item(s)...").arg(paths.size());
    emit statusTextChanged();
    runProcess("sh", {"-c", cmd});
}

void FileOperations::extractArchive(const QString &archivePath, const QString &destination)
{
    QString program;
    QStringList args;
    if (!archiveExtractCommand(archivePath, destination, &program, &args))
        return;

    m_statusText = "Extracting...";
    emit statusTextChanged();
    runProcess(program, args);
}

QString FileOperations::archiveRootFolder(const QString &archivePath)
{
    QString program;
    QStringList args;
    if (!archiveListCommand(archivePath, &program, &args))
        return {};

    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return {};

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList entries = archiveEntriesFromOutput(program, output);
    if (entries.isEmpty())
        return {};

    return commonArchiveRootFolder(entries);
}

bool FileOperations::isArchive(const QString &path)
{
    return archiveKindForPath(path) != ArchiveKind::None;
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

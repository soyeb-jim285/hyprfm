#include "models/filesystemmodel.h"
#include "services/gitstatusservice.h"
#include <QLocale>
#include <QDateTime>
#include <QDebug>
#include <QMimeDatabase>
#include <QStorageInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QDirIterator>
#include <QUrl>
#include <algorithm>

// Forward declarations for helpers defined further down (used by methods
// that appear above their definition site).
static QString runHostTool(const QString &program, const QStringList &arguments,
                           int timeoutMs = 3000);

namespace {

// Shared MIME database: construction is cheap but keeping one instance
// avoids repeating the same static-init dance across every helper below.
QMimeDatabase &mimeDb()
{
    static QMimeDatabase db;
    return db;
}

bool isTrashUri(const QString &path)
{
    return QUrl(path).scheme() == "trash";
}

bool shouldSpawnHostTool()
{
    static const bool inSandbox = QFile::exists(QStringLiteral("/.flatpak-info"));
    return inSandbox;
}

void startHostToolProcess(QProcess *process, const QString &program, const QStringList &arguments)
{
    if (shouldSpawnHostTool()) {
        QStringList args;
        args << QStringLiteral("--host") << program << arguments;
        process->start(QStringLiteral("flatpak-spawn"), args);
        return;
    }

    process->start(program, arguments);
}

bool isRemoteUri(const QString &path)
{
    const QUrl url(path);
    return url.isValid() && !url.scheme().isEmpty()
        && url.scheme() != QStringLiteral("file")
        && url.scheme() != QStringLiteral("trash");
}

QString remoteAuthority(const QString &uri)
{
    const int schemeSep = uri.indexOf(QStringLiteral("://"));
    if (schemeSep < 0)
        return {};

    const int authorityStart = schemeSep + 3;
    int authorityEnd = uri.size();
    const int pathStart = uri.indexOf(QLatin1Char('/'), authorityStart);
    const int queryStart = uri.indexOf(QLatin1Char('?'), authorityStart);
    const int fragmentStart = uri.indexOf(QLatin1Char('#'), authorityStart);
    for (const int marker : {pathStart, queryStart, fragmentStart}) {
        if (marker >= 0)
            authorityEnd = std::min(authorityEnd, marker);
    }

    return uri.mid(authorityStart, authorityEnd - authorityStart);
}

QString normalizeRemoteUri(const QString &path)
{
    const QUrl url(path);
    if (!url.isValid() || url.scheme().isEmpty())
        return path;

    const QUrl normalizedUrl = url.adjusted(QUrl::NormalizePathSegments);

    const QString encodedPath = [&normalizedUrl]() {
        QString value = normalizedUrl.path(QUrl::FullyEncoded);
        if (value.isEmpty())
            value = QStringLiteral("/");
        if (value.size() > 1 && value.endsWith(QLatin1Char('/')))
            value.chop(1);
        return value;
    }();

    QString normalized = url.scheme().toLower() + QStringLiteral("://")
        + remoteAuthority(path)
        + encodedPath;

    const QString query = normalizedUrl.query(QUrl::FullyEncoded);
    if (!query.isEmpty())
        normalized += QLatin1Char('?') + query;

    const QString fragment = normalizedUrl.fragment(QUrl::FullyEncoded);
    if (!fragment.isEmpty())
        normalized += QLatin1Char('#') + fragment;

    return normalized;
}

QString normalizeLocation(const QString &path)
{
    if (path.isEmpty())
        return {};

    const QUrl url(path);
    if (url.isValid() && url.scheme() == QStringLiteral("file"))
        return QDir::cleanPath(url.toLocalFile());

    if (url.isValid() && !url.scheme().isEmpty())
        return normalizeRemoteUri(path);

    return QDir::cleanPath(path);
}

QString gioLocationArg(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    if (QUrl(normalized).scheme().isEmpty())
        return normalized;
    return normalized;
}

QString locationFileName(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    if (isRemoteUri(normalized)) {
        const QUrl url(normalized);
        QString fileName = QUrl::fromPercentEncoding(url.fileName().toUtf8());
        if (!fileName.isEmpty())
            return fileName;
        const QString authority = remoteAuthority(normalized);
        if (!authority.isEmpty())
            return QUrl::fromPercentEncoding(authority.toUtf8());
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
    if (isRemoteUri(normalized)) {
        QUrl url(normalized);
        QString urlPath = url.path(QUrl::FullyEncoded);
        const QString base = url.scheme().toLower() + QStringLiteral("://")
            + remoteAuthority(normalized);
        if (urlPath.isEmpty() || urlPath == QStringLiteral("/"))
            return base + QStringLiteral("/");
        if (urlPath.endsWith('/'))
            urlPath.chop(1);
        const int slashIndex = urlPath.lastIndexOf('/');
        return base + (slashIndex <= 0 ? QStringLiteral("/") : urlPath.left(slashIndex));
    }

    return QFileInfo(normalized).absolutePath();
}

QString afcDocumentsUriFor(const QString &path)
{
    const QString normalized = normalizeLocation(path);
    const QUrl url(normalized);
    if (url.scheme() != QLatin1String("afc"))
        return {};

    const QString authority = remoteAuthority(normalized);
    if (authority.isEmpty() || authority.contains(QLatin1String(":3")))
        return {};

    return QStringLiteral("afc://%1:3/").arg(authority);
}

QString expandUserPath(const QString &path)
{
    if (path == QStringLiteral("~"))
        return QDir::homePath();
    if (path.startsWith(QStringLiteral("~/")))
        return QDir::cleanPath(QDir::homePath() + path.mid(1));
    return path;
}

QString displayPathForSuggestion(const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString homePath = QDir::homePath();
    if (cleanPath == homePath)
        return QStringLiteral("~");
    if (cleanPath.startsWith(homePath + QLatin1Char('/')))
        return QStringLiteral("~") + cleanPath.mid(homePath.size());
    return cleanPath;
}

QDateTime dateTimeFromSeconds(const QString &value)
{
    return value.isEmpty() ? QDateTime() : QDateTime::fromSecsSinceEpoch(value.toLongLong());
}

QString permissionsStringFromMode(int mode)
{
    if (mode <= 0)
        return {};

    QString s;
    s += (mode & 0400) ? 'r' : '-';
    s += (mode & 0200) ? 'w' : '-';
    s += (mode & 0100) ? 'x' : '-';
    s += (mode & 0040) ? 'r' : '-';
    s += (mode & 0020) ? 'w' : '-';
    s += (mode & 0010) ? 'x' : '-';
    s += (mode & 0004) ? 'r' : '-';
    s += (mode & 0002) ? 'w' : '-';
    s += (mode & 0001) ? 'x' : '-';
    return s;
}

int accessIndexFromMode(int mode, int readMask, int writeMask, int execMask)
{
    const bool canRead = mode & readMask;
    const bool canWrite = mode & writeMask;
    const bool canExecute = mode & execMask;
    if (canRead && canWrite && canExecute)
        return 3;
    if (canRead && canWrite)
        return 2;
    if (canRead)
        return 1;
    return 0;
}

QString formattedSize(qint64 size, bool verbose = false)
{
    if (size < 0)
        return {};
    if (size < 1024)
        return verbose ? QString("%1 B (%2 bytes)").arg(size).arg(QLocale().toString(size))
                       : QString("%1 B").arg(size);
    if (size < 1024 * 1024)
        return verbose ? QString("%1 KB (%2 bytes)").arg(size / 1024.0, 0, 'f', 1).arg(QLocale().toString(size))
                       : QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    if (size < 1024LL * 1024 * 1024)
        return verbose ? QString("%1 MB (%2 bytes)").arg(size / (1024.0 * 1024.0), 0, 'f', 1).arg(QLocale().toString(size))
                       : QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
    return verbose ? QString("%1 GB (%2 bytes)").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2).arg(QLocale().toString(size))
                   : QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

// Resolve a MIME type name (e.g. "text/x-typescript", "video/mp2t") to a
// freedesktop icon theme name. Falls back to the type's generic icon, then
// to a plain text icon as last resort.
QString iconNameForMimeName(const QString &mimeName)
{
    if (mimeName.isEmpty())
        return QStringLiteral("text-x-generic");
    const QMimeType mime = mimeDb().mimeTypeForName(mimeName);
    if (!mime.isValid())
        return QStringLiteral("text-x-generic");
    QString icon = mime.iconName();
    if (icon.isEmpty())
        icon = mime.genericIconName();
    return icon.isEmpty() ? QStringLiteral("text-x-generic") : icon;
}

// Resolve an icon for a file from its name (and optional precomputed
// content type, e.g. from `gio list -a standard::content-type` for trash
// entries). When no content type is given, ask QMimeDatabase based on the
// name; for ambiguous extensions like .ts (TypeScript vs MPEG-TS) the
// MIME database picks based on glob priority and content sniffing rather
// than a hand-maintained suffix table.
QString iconNameForEntry(const QString &name, bool isDir, const QString &contentType = QString())
{
    if (isDir)
        return QStringLiteral("folder");

    if (!contentType.isEmpty())
        return iconNameForMimeName(contentType);

    // mimeTypeForFile with just a name uses extension/glob lookup. If the
    // path is a real local file, MatchDefault will additionally sniff the
    // content when the glob is ambiguous, which is what disambiguates
    // .ts files between TypeScript and MPEG-TS video.
    const QMimeType mime = mimeDb().mimeTypeForFile(name);
    return iconNameForMimeName(mime.name());
}

QString fileTypeForEntry(const QString &name, bool isDir, const QString &contentType = QString())
{
    if (isDir)
        return QStringLiteral("folder");

    if (!contentType.isEmpty()) {
        const QMimeType mime = mimeDb().mimeTypeForName(contentType);
        if (mime.isValid())
            return mime.comment();
        return contentType;
    }

    const QMimeType mime = mimeDb().mimeTypeForFile(name);
    return mime.isValid() ? mime.comment() : QFileInfo(name).suffix();
}

// Classify a file as image/video for thumbnail purposes. Prefers an
// already-known content type (e.g. from `gio list -a standard::content-type`
// for trash entries) and otherwise asks QMimeDatabase. For local files
// QMimeDatabase content-sniffs ambiguous extensions like .ts.
enum class PreviewKind { None, Image, Video };

PreviewKind previewKindForEntry(const QString &localPath, bool isDir,
                                const QString &contentType = QString())
{
    if (isDir)
        return PreviewKind::None;

    QString mimeName = contentType;
    if (mimeName.isEmpty()) {
        const QMimeType mime = mimeDb().mimeTypeForFile(localPath);
        if (mime.isValid())
            mimeName = mime.name();
    }
    if (mimeName.isEmpty())
        return PreviewKind::None;

    if (mimeName.startsWith(QLatin1String("image/")))
        return PreviewKind::Image;
    if (mimeName.startsWith(QLatin1String("video/")))
        return PreviewKind::Video;
    return PreviewKind::None;
}

// Build a cached permission string (e.g. "rwxr-xr-x") from QFileInfo.
QString permissionsString(const QFileInfo &info)
{
    const auto p = info.permissions();
    QString s;
    s += (p & QFile::ReadOwner)  ? 'r' : '-';
    s += (p & QFile::WriteOwner) ? 'w' : '-';
    s += (p & QFile::ExeOwner)   ? 'x' : '-';
    s += (p & QFile::ReadGroup)  ? 'r' : '-';
    s += (p & QFile::WriteGroup) ? 'w' : '-';
    s += (p & QFile::ExeGroup)   ? 'x' : '-';
    s += (p & QFile::ReadOther)  ? 'r' : '-';
    s += (p & QFile::WriteOther) ? 'w' : '-';
    s += (p & QFile::ExeOther)   ? 'x' : '-';
    return s;
}

QHash<QString, QString> parseGioAttributes(const QString &attributeText)
{
    QHash<QString, QString> attrs;
    static const QRegularExpression attrRe(R"(([A-Za-z0-9:-]+)=(.*?)(?= [A-Za-z0-9:-]+=|$))");

    auto it = attrRe.globalMatch(attributeText.trimmed());
    while (it.hasNext()) {
        const auto match = it.next();
        attrs.insert(match.captured(1), match.captured(2));
    }

    return attrs;
}

QVariantMap buildRemoteEntryFromLine(const QString &line)
{
    static const QRegularExpression lineRe(QStringLiteral("^([^\\t]+)\\t([0-9-]+)\\t\\(([^)]*)\\)(?:\\t(.*))?$"));
    const auto match = lineRe.match(line.trimmed());
    if (!match.hasMatch())
        return {};

    const QString uri = normalizeLocation(match.captured(1).trimmed());
    const qint64 size = match.captured(2).trimmed().toLongLong();
    const QString typeToken = match.captured(3).trimmed().toLower();
    const auto attrs = parseGioAttributes(match.captured(4));

    const bool isDir = typeToken.contains(QStringLiteral("directory"));
    const QString displayName = attrs.value(QStringLiteral("standard::display-name"), locationFileName(uri));
    const QString contentType = attrs.value(QStringLiteral("standard::content-type"));
    const int unixMode = attrs.value(QStringLiteral("unix::mode")).toInt();
    const QDateTime modified = dateTimeFromSeconds(attrs.value(QStringLiteral("time::modified")));

    QVariantMap entry;
    entry[QStringLiteral("fileName")] = displayName;
    entry[QStringLiteral("filePath")] = uri;
    entry[QStringLiteral("fileSize")] = isDir ? QVariant(qint64(-1)) : QVariant(size);
    entry[QStringLiteral("fileSizeText")] = isDir ? QString() : formattedSize(size);
    entry[QStringLiteral("fileType")] = fileTypeForEntry(displayName, isDir, contentType);
    entry[QStringLiteral("fileModified")] = modified;
    entry[QStringLiteral("fileModifiedText")] = modified.isValid() ? QLocale().toString(modified, QLocale::ShortFormat) : QString();
    entry[QStringLiteral("filePermissions")] = permissionsStringFromMode(unixMode);
    entry[QStringLiteral("isDir")] = isDir;
    entry[QStringLiteral("isSymlink")] = attrs.value(QStringLiteral("standard::is-symlink")) == QStringLiteral("TRUE");
    entry[QStringLiteral("fileIconName")] = iconNameForEntry(displayName, isDir, contentType);
    entry[QStringLiteral("mimeType")] = contentType;
    entry[QStringLiteral("symlinkTarget")] = attrs.value(QStringLiteral("standard::symlink-target"));
    return entry;
}

QVariantMap buildFallbackRemoteProperties(const QString &path)
{
    QVariantMap props;
    props[QStringLiteral("name")] = locationFileName(path);
    props[QStringLiteral("path")] = path;
    props[QStringLiteral("parentDir")] = parentLocation(path);
    props[QStringLiteral("isDir")] = false;
    props[QStringLiteral("isSymlink")] = false;
    props[QStringLiteral("iconName")] = iconNameForEntry(props.value(QStringLiteral("name")).toString(), false);
    props[QStringLiteral("size")] = qint64(-1);
    props[QStringLiteral("sizeText")] = QString();
    props[QStringLiteral("permissions")] = QString();
    props[QStringLiteral("ownerAccess")] = 0;
    props[QStringLiteral("groupAccess")] = 0;
    props[QStringLiteral("otherAccess")] = 0;
    props[QStringLiteral("isExecutable")] = false;
    props[QStringLiteral("canEditPermissions")] = false;
    return props;
}

QVariantMap buildRemotePropertiesFromEntry(const QVariantMap &entry)
{
    QVariantMap props;
    const QString displayName = entry.value(QStringLiteral("fileName")).toString();
    const QString path = entry.value(QStringLiteral("filePath")).toString();
    const bool isDir = entry.value(QStringLiteral("isDir")).toBool();
    const QString mimeType = entry.value(QStringLiteral("mimeType")).toString();
    const QDateTime modified = entry.value(QStringLiteral("fileModified")).toDateTime();
    const qint64 size = entry.value(QStringLiteral("fileSize")).toLongLong();

    props[QStringLiteral("name")] = displayName;
    props[QStringLiteral("path")] = path;
    props[QStringLiteral("parentDir")] = parentLocation(path);
    props[QStringLiteral("isDir")] = isDir;
    props[QStringLiteral("isSymlink")] = entry.value(QStringLiteral("isSymlink")).toBool();
    props[QStringLiteral("symlinkTarget")] = entry.value(QStringLiteral("symlinkTarget")).toString();
    props[QStringLiteral("iconName")] = entry.value(QStringLiteral("fileIconName")).toString();
    props[QStringLiteral("mimeType")] = mimeType;
    props[QStringLiteral("mimeDescription")] = mimeType.isEmpty() ? QString() : mimeDb().mimeTypeForName(mimeType).comment();
    props[QStringLiteral("created")] = QString();
    props[QStringLiteral("modified")] = modified.isValid() ? QLocale().toString(modified, QLocale::LongFormat) : QString();
    props[QStringLiteral("accessed")] = QString();
    props[QStringLiteral("owner")] = QString();
    props[QStringLiteral("group")] = QString();
    props[QStringLiteral("permissions")] = entry.value(QStringLiteral("filePermissions")).toString();
    props[QStringLiteral("ownerAccess")] = 0;
    props[QStringLiteral("groupAccess")] = 0;
    props[QStringLiteral("otherAccess")] = 0;
    props[QStringLiteral("isExecutable")] = false;
    props[QStringLiteral("canEditPermissions")] = false;

    if (isDir) {
        props[QStringLiteral("size")] = qint64(-1);
        props[QStringLiteral("sizeText")] = QString();
        props[QStringLiteral("contentText")] = QString();
    } else {
        props[QStringLiteral("size")] = size;
        props[QStringLiteral("sizeText")] = size >= 0 ? formattedSize(size, true) : QString();
    }

    return props;
}

QVariantMap buildTrashEntryFromLine(const QString &line)
{
    static const QRegularExpression lineRe(R"(^([^\t]+)\t(\d+)\t\(([^)]*)\)(?:\t(.*))?$)");
    const auto match = lineRe.match(line.trimmed());
    if (!match.hasMatch())
        return {};

    const QString uri = match.captured(1).trimmed();
    const qint64 size = match.captured(2).toLongLong();
    const QString typeToken = match.captured(3).trimmed().toLower();
    const auto attrs = parseGioAttributes(match.captured(4));

    const bool isDir = typeToken.contains("directory");
    QString displayName = attrs.value("standard::display-name");
    if (displayName.isEmpty()) {
        const QUrl url(uri);
        displayName = url.fileName();
        if (displayName.isEmpty())
            displayName = attrs.value("standard::name");
    }

    QDateTime modified;
    const QString modifiedSeconds = attrs.value("time::modified");
    if (!modifiedSeconds.isEmpty())
        modified = QDateTime::fromSecsSinceEpoch(modifiedSeconds.toLongLong());

    QDateTime deletedAt;
    const QString deletionDate = attrs.value("trash::deletion-date");
    if (!deletionDate.isEmpty()) {
        deletedAt = QDateTime::fromString(deletionDate, Qt::ISODate);
        if (!deletedAt.isValid())
            deletedAt = QDateTime::fromString(deletionDate, Qt::ISODateWithMs);
    }

    const QString contentType = attrs.value("standard::content-type");
    const QString mimeDescription = contentType.isEmpty()
        ? QString()
        : mimeDb().mimeTypeForName(contentType).comment();

    QVariantMap entry;
    entry["fileName"] = displayName;
    entry["filePath"] = uri;
    entry["fileSize"] = isDir ? QVariant(-1) : QVariant(size);
    entry["fileSizeText"] = isDir ? QString() : formattedSize(size);
    entry["fileType"] = fileTypeForEntry(displayName, isDir, contentType);
    entry["fileModified"] = modified;
    entry["fileModifiedText"] = modified.isValid() ? QLocale().toString(modified, QLocale::ShortFormat) : QString();
    entry["filePermissions"] = QString();
    entry["isDir"] = isDir;
    entry["isSymlink"] = typeToken.contains("symbolic");
    entry["fileIconName"] = iconNameForEntry(displayName, isDir, contentType);
    entry["originalPath"] = attrs.value("trash::orig-path");
    entry["deletedAt"] = deletedAt;
    entry["deletedAtText"] = deletedAt.isValid() ? QLocale().toString(deletedAt, QLocale::LongFormat) : QString();
    entry["mimeType"] = contentType;
    entry["mimeDescription"] = mimeDescription;
    return entry;
}

QVariantMap buildTrashProperties(const QVariantMap &entry)
{
    QVariantMap props;
    const QString originalPath = entry.value("originalPath").toString();
    const QFileInfo originalInfo(originalPath);

    props["name"] = entry.value("fileName").toString();
    props["path"] = entry.value("filePath").toString();
    props["parentDir"] = originalPath.isEmpty() ? QString() : originalInfo.absolutePath();
    props["originalPath"] = originalPath;
    props["isDir"] = entry.value("isDir").toBool();
    props["isSymlink"] = false;
    props["iconName"] = entry.value("fileIconName").toString();
    props["size"] = entry.value("fileSize");
    props["sizeText"] = entry.value("fileSizeText").toString();
    props["mimeType"] = entry.value("mimeType").toString();
    props["mimeDescription"] = entry.value("mimeDescription").toString();
    props["created"] = QString();
    props["modified"] = entry.value("fileModified").toDateTime().isValid()
        ? QLocale().toString(entry.value("fileModified").toDateTime(), QLocale::LongFormat)
        : QString();
    props["accessed"] = QString();
    props["owner"] = QString();
    props["group"] = QString();
    props["permissions"] = QString();
    props["ownerAccess"] = 0;
    props["groupAccess"] = 0;
    props["otherAccess"] = 0;
    props["isExecutable"] = false;
    props["canEditPermissions"] = false;
    props["isTrashItem"] = true;
    props["deleted"] = entry.value("deletedAtText").toString();
    return props;
}

}

FileSystemModel::FileSystemModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        if (!m_rootPath.isEmpty())
            emit watchedDirectoryChanged(m_rootPath);
        refresh();
    });
}

FileSystemModel::~FileSystemModel()
{
    cancelRemoteReload();
}

void FileSystemModel::setGitStatusService(GitStatusService *service)
{
    if (m_gitService)
        disconnect(m_gitService, nullptr, this, nullptr);
    m_gitService = service;
    if (m_gitService) {
        connect(m_gitService, &GitStatusService::statusChanged, this, [this]() {
            if (rowCount() > 0)
                emit dataChanged(index(0), index(rowCount() - 1), {GitStatusRole, GitStatusIconRole});
        });
        m_gitService->setRootPath(m_rootPath);
    }
}

int FileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    if (isTrashRoot())
        return m_trashEntries.size();
    if (isRemoteRoot())
        return m_remoteEntries.size();
    return m_entries.size();
}

QVariant FileSystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};

    if (isTrashRoot()) {
        const QVariantMap &entry = m_trashEntries.at(index.row());

        switch (role) {
        case FileNameRole:
            return entry.value("fileName");
        case FilePathRole:
            return entry.value("filePath");
        case FileSizeRole:
            return entry.value("fileSize");
        case FileSizeTextRole:
            return entry.value("fileSizeText");
        case FileTypeRole:
            return entry.value("fileType");
        case FileModifiedRole:
            return entry.value("fileModified");
        case FileModifiedTextRole:
            return entry.value("fileModifiedText");
        case FilePermissionsRole:
            return entry.value("filePermissions");
        case IsDirRole:
            return entry.value("isDir");
        case IsSymlinkRole:
            return entry.value("isSymlink");
        case FileIconNameRole:
            return entry.value("fileIconName");
        case GitStatusRole:
        case GitStatusIconRole:
            // Trashed files aren't git-tracked, but the view delegates
            // declare these as required properties.
            return QString();
        case HasImagePreviewRole: {
            const PreviewKind kind = previewKindForEntry(
                entry.value(QStringLiteral("fileName")).toString(),
                entry.value(QStringLiteral("isDir")).toBool(),
                entry.value(QStringLiteral("mimeType")).toString());
            return kind == PreviewKind::Image;
        }
        case HasVideoPreviewRole: {
            const PreviewKind kind = previewKindForEntry(
                entry.value(QStringLiteral("fileName")).toString(),
                entry.value(QStringLiteral("isDir")).toBool(),
                entry.value(QStringLiteral("mimeType")).toString());
            return kind == PreviewKind::Video;
        }
        default:
            return {};
        }
    }

    if (isRemoteRoot()) {
        const QVariantMap &entry = m_remoteEntries.at(index.row());

        switch (role) {
        case FileNameRole:
            return entry.value(QStringLiteral("fileName"));
        case FilePathRole:
            return entry.value(QStringLiteral("filePath"));
        case FileSizeRole:
            return entry.value(QStringLiteral("fileSize"));
        case FileSizeTextRole:
            return entry.value(QStringLiteral("fileSizeText"));
        case FileTypeRole:
            return entry.value(QStringLiteral("fileType"));
        case FileModifiedRole:
            return entry.value(QStringLiteral("fileModified"));
        case FileModifiedTextRole:
            return entry.value(QStringLiteral("fileModifiedText"));
        case FilePermissionsRole:
            return entry.value(QStringLiteral("filePermissions"));
        case IsDirRole:
            return entry.value(QStringLiteral("isDir"));
        case IsSymlinkRole:
            return entry.value(QStringLiteral("isSymlink"));
        case FileIconNameRole:
            return entry.value(QStringLiteral("fileIconName"));
        case GitStatusRole:
        case GitStatusIconRole:
            // Remote files (sftp/smb/dav) aren't git-tracked, but the view
            // delegates declare these as required properties.
            return QString();
        case HasImagePreviewRole:
        case HasVideoPreviewRole:
            // No thumbnails for remote files (the thumbnailer needs local
            // file access).
            return false;
        default:
            return {};
        }
    }

    const Entry &entry = m_entries.at(index.row());
    const QFileInfo &info = entry.info;

    switch (role) {
    // Path / filename / size / dir / symlink / modified come straight from
    // QFileInfo's own stat cache, so they don't need the lazy populate path.
    case FileNameRole:
        return info.fileName();
    case FilePathRole:
        return info.absoluteFilePath();
    case FileSizeRole:
        return info.isDir() ? QVariant(-1) : QVariant(info.size());
    case FileModifiedRole:
        return info.lastModified();
    case IsDirRole:
        return info.isDir();
    case IsSymlinkRole:
        return info.isSymLink();
    case FileSizeTextRole:
        ensurePopulated(entry);
        return entry.sizeText;
    case FileTypeRole:
        ensurePopulated(entry);
        return entry.fileType;
    case FileModifiedTextRole:
        ensurePopulated(entry);
        return entry.modifiedText;
    case FilePermissionsRole:
        ensurePopulated(entry);
        return entry.permissionsText;
    case FileIconNameRole:
        ensurePopulated(entry);
        return entry.iconName;
    case HasImagePreviewRole:
        ensurePopulated(entry);
        return entry.hasImagePreview;
    case HasVideoPreviewRole:
        ensurePopulated(entry);
        return entry.hasVideoPreview;
    case GitStatusRole:
        return m_gitService ? m_gitService->statusForPath(info.absoluteFilePath()) : QString();
    case GitStatusIconRole: {
        if (!m_gitService)
            return QString();
        const QString st = m_gitService->statusForPath(info.absoluteFilePath());
        if (st == "modified")   return QStringLiteral("git-modified");
        if (st == "staged")     return QStringLiteral("git-staged");
        if (st == "untracked")  return QStringLiteral("git-untracked");
        if (st == "deleted")    return QStringLiteral("git-deleted");
        if (st == "renamed")    return QStringLiteral("git-renamed");
        if (st == "conflicted") return QStringLiteral("git-conflicted");
        if (st == "ignored")    return QStringLiteral("git-ignored");
        if (st == "dirty")      return QStringLiteral("git-dirty");
        return QString();
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> FileSystemModel::roleNames() const
{
    return {
        {FileNameRole,         "fileName"},
        {FilePathRole,         "filePath"},
        {FileSizeRole,         "fileSize"},
        {FileSizeTextRole,     "fileSizeText"},
        {FileTypeRole,         "fileType"},
        {FileModifiedRole,     "fileModified"},
        {FileModifiedTextRole, "fileModifiedText"},
        {FilePermissionsRole,  "filePermissions"},
        {IsDirRole,            "isDir"},
        {IsSymlinkRole,        "isSymlink"},
        {FileIconNameRole,     "fileIconName"},
        {GitStatusRole,        "gitStatus"},
        {GitStatusIconRole,    "gitStatusIcon"},
        {HasImagePreviewRole,  "hasImagePreview"},
        {HasVideoPreviewRole,  "hasVideoPreview"},
    };
}

QString FileSystemModel::rootPath() const { return m_rootPath; }
bool FileSystemModel::showHidden() const { return m_showHidden; }
int FileSystemModel::fileCount() const { return m_fileCount; }
int FileSystemModel::folderCount() const { return m_folderCount; }

bool FileSystemModel::isTrashRoot() const
{
    return isTrashUri(m_rootPath);
}

bool FileSystemModel::isRemoteRoot() const
{
    return isRemoteUri(m_rootPath);
}

void FileSystemModel::setRootPath(const QString &path)
{
    const QString normalizedPath = normalizeLocation(path);
    if (m_rootPath == normalizedPath)
        return;

    // Stop watching old directory
    if (!m_rootPath.isEmpty() && !isTrashRoot() && !isRemoteRoot())
        m_watcher.removePath(m_rootPath);

    m_rootPath = normalizedPath;

    // Watch new directory
    if (!m_rootPath.isEmpty() && !isTrashRoot() && !isRemoteRoot())
        m_watcher.addPath(m_rootPath);

    reload();
    if (m_gitService)
        m_gitService->setRootPath(m_rootPath);
    emit rootPathChanged();
}

void FileSystemModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    if (!m_rootPath.isEmpty())
        reload();
    emit showHiddenChanged();
}

void FileSystemModel::sortByColumn(const QString &column, bool ascending)
{
    if (m_sortColumn == column && m_sortAscending == ascending)
        return;

    m_sortColumn = column;
    m_sortAscending = ascending;

    QDir::SortFlags flags = QDir::DirsFirst | QDir::IgnoreCase;
    if (column == "name")
        flags |= QDir::Name;
    else if (column == "size")
        flags |= QDir::Size;
    else if (column == "modified")
        flags |= QDir::Time;
    else if (column == "type")
        flags |= QDir::Type;
    else
        flags |= QDir::Name;

    if (!ascending)
        flags |= QDir::Reversed;

    m_sortFlags = flags;
    reload();
}

void FileSystemModel::refresh()
{
    if (isTrashRoot()) {
        reload();
        return;
    }

    if (isRemoteRoot()) {
        reload();
        return;
    }

    QList<Entry> newEntries = currentLocalEntries();
    if (applyLocalDiff(newEntries))
        return;

    beginResetModel();
    m_entries = std::move(newEntries);
    m_trashEntries.clear();
    updateLocalCounts();
    endResetModel();
    emit countsChanged();
}

QString FileSystemModel::filePath(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};

    if (isTrashRoot())
        return m_trashEntries.at(row).value("filePath").toString();

    if (isRemoteRoot())
        return m_remoteEntries.at(row).value(QStringLiteral("filePath")).toString();

    return m_entries.at(row).info.absoluteFilePath();
}

bool FileSystemModel::isDir(int row) const
{
    if (row < 0 || row >= rowCount())
        return false;

    if (isTrashRoot())
        return m_trashEntries.at(row).value("isDir").toBool();

    if (isRemoteRoot())
        return m_remoteEntries.at(row).value(QStringLiteral("isDir")).toBool();

    return m_entries.at(row).info.isDir();
}

QString FileSystemModel::fileName(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};

    if (isTrashRoot())
        return m_trashEntries.at(row).value("fileName").toString();

    if (isRemoteRoot())
        return m_remoteEntries.at(row).value(QStringLiteral("fileName")).toString();

    return m_entries.at(row).info.fileName();
}

void FileSystemModel::reload()
{
    cancelRemoteReload();
    ++m_remoteReloadGeneration;

    beginResetModel();
    m_entries.clear();
    m_remoteEntries.clear();
    m_trashEntries.clear();
    m_fileCount = 0;
    m_folderCount = 0;

    if (isTrashRoot())
        reloadTrash();
    else if (!isRemoteRoot())
        reloadLocal();

    endResetModel();
    emit countsChanged();

    if (isRemoteRoot())
        reloadRemote();
}

void FileSystemModel::reloadLocal()
{
    m_entries = currentLocalEntries();
    updateLocalCounts();
}

void FileSystemModel::reloadRemote()
{
    if (m_rootPath.isEmpty()) {
        m_fileCount = 0;
        m_folderCount = 0;
        return;
    }

    QStringList args = {
        QStringLiteral("list"),
        QStringLiteral("-l"),
        QStringLiteral("-u"),
        QStringLiteral("-a"),
        QStringLiteral("standard::display-name,standard::content-type,time::modified,unix::mode,standard::is-symlink,standard::symlink-target")
    };
    if (m_showHidden)
        args.append(QStringLiteral("-h"));
    args.append(gioLocationArg(m_rootPath));

    const int generation = m_remoteReloadGeneration;
    const QString rootPath = m_rootPath;
    auto *process = new QProcess(this);
    m_remoteReloadProcess = process;

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, generation, rootPath](int exitCode, QProcess::ExitStatus) {
        if (process != m_remoteReloadProcess || generation != m_remoteReloadGeneration) {
            process->deleteLater();
            return;
        }

        const QByteArray output = exitCode == 0 ? process->readAllStandardOutput() : QByteArray();
        m_remoteReloadProcess = nullptr;
        process->deleteLater();

        if (output.isEmpty() && exitCode != 0)
            return;

        applyRemoteReload(rootPath, output);
    });

    startHostToolProcess(process, QStringLiteral("gio"), args);
    QTimer::singleShot(8000, process, [this, process, generation]() {
        if (process == m_remoteReloadProcess
            && generation == m_remoteReloadGeneration
            && process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
}

void FileSystemModel::cancelRemoteReload()
{
    if (!m_remoteReloadProcess)
        return;

    m_remoteReloadProcess->disconnect();
    if (m_remoteReloadProcess->state() != QProcess::NotRunning) {
        m_remoteReloadProcess->kill();
        m_remoteReloadProcess->waitForFinished(100);
    }
    m_remoteReloadProcess->deleteLater();
    m_remoteReloadProcess = nullptr;
}

void FileSystemModel::applyRemoteReload(const QString &rootPath, const QByteArray &output)
{
    if (rootPath != m_rootPath || !isRemoteRoot())
        return;

    QList<QVariantMap> entries;
    const QStringList lines = QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QVariantMap entry = buildRemoteEntryFromLine(line);
        if (!entry.isEmpty())
            entries.append(entry);
    }

    const QString afcDocumentsUri = afcDocumentsUriFor(m_rootPath);
    if (!afcDocumentsUri.isEmpty()) {
        bool alreadyPresent = false;
        for (const QVariantMap &entry : std::as_const(entries)) {
            if (entry.value(QStringLiteral("filePath")).toString() == afcDocumentsUri) {
                alreadyPresent = true;
                break;
            }
        }

        if (!alreadyPresent) {
            QVariantMap entry;
            entry[QStringLiteral("fileName")] = QStringLiteral("Apps");
            entry[QStringLiteral("filePath")] = afcDocumentsUri;
            entry[QStringLiteral("fileSize")] = QVariant(qint64(-1));
            entry[QStringLiteral("fileSizeText")] = QString();
            entry[QStringLiteral("fileType")] = QStringLiteral("folder");
            entry[QStringLiteral("fileModified")] = QDateTime();
            entry[QStringLiteral("fileModifiedText")] = QString();
            entry[QStringLiteral("filePermissions")] = QString();
            entry[QStringLiteral("isDir")] = true;
            entry[QStringLiteral("isSymlink")] = false;
            entry[QStringLiteral("fileIconName")] = QStringLiteral("folder");
            entry[QStringLiteral("mimeType")] = QStringLiteral("inode/directory");
            entry[QStringLiteral("symlinkTarget")] = QString();
            entries.prepend(entry);
        }
    }

    std::sort(entries.begin(), entries.end(), [this](const QVariantMap &lhs, const QVariantMap &rhs) {
        const bool lhsDir = lhs.value(QStringLiteral("isDir")).toBool();
        const bool rhsDir = rhs.value(QStringLiteral("isDir")).toBool();
        if (lhsDir != rhsDir)
            return lhsDir > rhsDir;

        int comparison = 0;
        if (m_sortColumn == QStringLiteral("size")) {
            const qint64 leftSize = lhs.value(QStringLiteral("fileSize")).toLongLong();
            const qint64 rightSize = rhs.value(QStringLiteral("fileSize")).toLongLong();
            comparison = (leftSize < rightSize) ? -1 : (leftSize > rightSize ? 1 : 0);
        } else if (m_sortColumn == QStringLiteral("modified")) {
            const QDateTime leftModified = lhs.value(QStringLiteral("fileModified")).toDateTime();
            const QDateTime rightModified = rhs.value(QStringLiteral("fileModified")).toDateTime();
            comparison = (leftModified < rightModified) ? -1 : (leftModified > rightModified ? 1 : 0);
        } else if (m_sortColumn == QStringLiteral("type")) {
            comparison = QString::compare(lhs.value(QStringLiteral("fileType")).toString(),
                                          rhs.value(QStringLiteral("fileType")).toString(),
                                          Qt::CaseInsensitive);
        } else {
            comparison = QString::compare(lhs.value(QStringLiteral("fileName")).toString(),
                                          rhs.value(QStringLiteral("fileName")).toString(),
                                          Qt::CaseInsensitive);
        }

        return m_sortAscending ? comparison < 0 : comparison > 0;
    });

    int files = 0;
    int folders = 0;
    for (const auto &entry : std::as_const(entries)) {
        if (entry.value(QStringLiteral("isDir")).toBool())
            ++folders;
        else
            ++files;
    }

    beginResetModel();
    m_remoteEntries = std::move(entries);
    m_fileCount = files;
    m_folderCount = folders;
    endResetModel();
    emit countsChanged();
}

QList<FileSystemModel::Entry> FileSystemModel::currentLocalEntries() const
{
    if (m_rootPath.isEmpty())
        return {};

    QDir dir(m_rootPath);
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (m_showHidden)
        filters |= QDir::Hidden;

    // Fast path: only the syscall + QFileInfo construction. Derived fields
    // (icon name, mime-backed type, locale-formatted date, permission text)
    // populate lazily on first data() request for each row.
    const QFileInfoList infos = dir.entryInfoList(filters, m_sortFlags);
    QList<Entry> entries;
    entries.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        Entry e;
        e.info = info;
        entries.append(std::move(e));
    }
    return entries;
}

void FileSystemModel::ensurePopulated(const Entry &entry) const
{
    if (entry.populated)
        return;
    const bool isDir = entry.info.isDir();
    const QString absPath = entry.info.absoluteFilePath();
    entry.iconName = iconNameForEntry(absPath, isDir);
    entry.fileType = fileTypeForEntry(entry.info.fileName(), isDir);
    entry.sizeText = isDir ? QString() : formattedSize(entry.info.size());
    entry.modifiedText = QLocale().toString(entry.info.lastModified(), QLocale::ShortFormat);
    entry.permissionsText = permissionsString(entry.info);
    const PreviewKind kind = previewKindForEntry(absPath, isDir);
    entry.hasImagePreview = kind == PreviewKind::Image;
    entry.hasVideoPreview = kind == PreviewKind::Video;
    entry.populated = true;
}

void FileSystemModel::updateLocalCounts()
{
    int files = 0;
    int folders = 0;
    for (const Entry &entry : m_entries) {
        if (entry.info.isDir())
            ++folders;
        else
            ++files;
    }
    m_fileCount = files;
    m_folderCount = folders;
}

bool FileSystemModel::applyLocalDiff(const QList<Entry> &newEntries)
{
    const int oldCount = m_entries.size();
    const int newCount = newEntries.size();

    auto pathAt = [](const QList<Entry> &list, int row) {
        return list.at(row).info.absoluteFilePath();
    };

    if (newCount == oldCount + 1) {
        int insertRow = 0;
        while (insertRow < oldCount
               && pathAt(m_entries, insertRow) == pathAt(newEntries, insertRow)) {
            ++insertRow;
        }

        bool matches = true;
        for (int oldRow = insertRow, newRow = insertRow + 1; oldRow < oldCount; ++oldRow, ++newRow) {
            if (pathAt(m_entries, oldRow) != pathAt(newEntries, newRow)) {
                matches = false;
                break;
            }
        }

        if (matches) {
            beginInsertRows({}, insertRow, insertRow);
            m_entries.insert(insertRow, newEntries.at(insertRow));
            endInsertRows();
            updateLocalCounts();
            emit countsChanged();
            return true;
        }
    }

    if (newCount + 1 == oldCount) {
        int removeRow = 0;
        while (removeRow < newCount
               && pathAt(m_entries, removeRow) == pathAt(newEntries, removeRow)) {
            ++removeRow;
        }

        bool matches = true;
        for (int oldRow = removeRow + 1, newRow = removeRow; newRow < newCount; ++oldRow, ++newRow) {
            if (pathAt(m_entries, oldRow) != pathAt(newEntries, newRow)) {
                matches = false;
                break;
            }
        }

        if (matches) {
            beginRemoveRows({}, removeRow, removeRow);
            m_entries.removeAt(removeRow);
            endRemoveRows();
            updateLocalCounts();
            emit countsChanged();
            return true;
        }
    }

    if (newCount == oldCount) {
        bool sameOrder = true;
        for (int row = 0; row < newCount; ++row) {
            if (pathAt(m_entries, row) != pathAt(newEntries, row)) {
                sameOrder = false;
                break;
            }
        }

        if (sameOrder) {
            m_entries = newEntries;
            updateLocalCounts();
            if (newCount > 0) {
                static const QVector<int> changedRoles = {
                    FileSizeRole, FileSizeTextRole,
                    FileModifiedRole, FileModifiedTextRole,
                    FilePermissionsRole,
                    HasImagePreviewRole, HasVideoPreviewRole,
                };
                emit dataChanged(index(0, 0), index(newCount - 1, 0), changedRoles);
            }
            emit countsChanged();
            return true;
        }
    }

    return false;
}

void FileSystemModel::reloadTrash()
{
    if (m_rootPath.isEmpty()) {
        m_fileCount = 0;
        m_folderCount = 0;
        return;
    }

    // Inside a Flatpak this transparently runs `flatpak-spawn --host gio
    // list ...`. Without the host hop, the sandbox's overridden
    // XDG_DATA_HOME (~/.var/app/<app-id>/data) makes gio query an empty
    // sandbox-local trash instead of the user's real ~/.local/share/Trash.
    const QString output = runHostTool(QStringLiteral("gio"), {
        QStringLiteral("list"),
        QStringLiteral("-l"),
        QStringLiteral("-u"),
        QStringLiteral("-a"),
        QStringLiteral("standard::display-name,standard::name,standard::content-type,time::modified,trash::orig-path,trash::deletion-date"),
        QUrl(m_rootPath).toString(QUrl::FullyEncoded)
    }, 5000);
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QVariantMap entry = buildTrashEntryFromLine(line);
        if (!entry.isEmpty())
            m_trashEntries.append(entry);
    }

    std::sort(m_trashEntries.begin(), m_trashEntries.end(), [this](const QVariantMap &lhs, const QVariantMap &rhs) {
        const bool lhsDir = lhs.value("isDir").toBool();
        const bool rhsDir = rhs.value("isDir").toBool();
        if (lhsDir != rhsDir)
            return lhsDir > rhsDir;

        int comparison = 0;
        if (m_sortColumn == "size") {
            const qint64 leftSize = lhs.value("fileSize").toLongLong();
            const qint64 rightSize = rhs.value("fileSize").toLongLong();
            comparison = (leftSize < rightSize) ? -1 : (leftSize > rightSize ? 1 : 0);
        } else if (m_sortColumn == "modified") {
            const QDateTime leftModified = lhs.value("fileModified").toDateTime();
            const QDateTime rightModified = rhs.value("fileModified").toDateTime();
            comparison = (leftModified < rightModified) ? -1 : (leftModified > rightModified ? 1 : 0);
        } else if (m_sortColumn == "type") {
            comparison = QString::compare(lhs.value("fileType").toString(), rhs.value("fileType").toString(), Qt::CaseInsensitive);
        } else {
            comparison = QString::compare(lhs.value("fileName").toString(), rhs.value("fileName").toString(), Qt::CaseInsensitive);
        }

        return m_sortAscending ? comparison < 0 : comparison > 0;
    });

    int files = 0;
    int folders = 0;
    for (const auto &entry : std::as_const(m_trashEntries)) {
        if (entry.value("isDir").toBool())
            ++folders;
        else
            ++files;
    }

    m_fileCount = files;
    m_folderCount = folders;
}

QVariantMap FileSystemModel::folderItemCounts(const QStringList &paths) const
{
    QVariantMap result;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        QDir dir(path);
        if (!dir.exists())
            continue;
        const int count = dir.entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).count();
        result.insert(path, count);
    }
    return result;
}

QVariantMap FileSystemModel::fileProperties(const QString &path) const
{
    const QString normalizedPath = normalizeLocation(path);
    if (isTrashUri(normalizedPath))
        return trashFileProperties(normalizedPath);

    if (isRemoteUri(normalizedPath))
        return remoteFileProperties(normalizedPath);

    QFileInfo info(normalizedPath);
    QVariantMap props;

    props["name"] = info.fileName();
    props["path"] = info.absoluteFilePath();
    props["parentDir"] = info.absolutePath();
    props["isDir"] = info.isDir();
    props["isSymlink"] = info.isSymLink();

    // Icon name (reuse the same mapping as data())
    props["iconName"] = iconNameForEntry(info.fileName(), info.isDir());
    if (info.isSymLink())
        props["symlinkTarget"] = info.symLinkTarget();

    // Size
    if (info.isDir()) {
        QDir dir(normalizedPath);
        auto allEntries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        int fileCount = 0, folderCount = 0;
        for (const auto &e : allEntries) {
            if (e.isDir()) ++folderCount;
            else ++fileCount;
        }
        props["containedItems"] = allEntries.count();
        props["containedFiles"] = fileCount;
        props["containedFolders"] = folderCount;
        props["contentText"] = QString("%1 items (%2 files, %3 folders)").arg(allEntries.count()).arg(fileCount).arg(folderCount);
        props["sizeText"] = QString("%1 items").arg(allEntries.count());
        props["size"] = QVariant(static_cast<qint64>(-1));
    } else {
        qint64 size = info.size();
        props["size"] = size;
        props["sizeText"] = formattedSize(size, true);
    }

    // Disk usage
    QStorageInfo storage(info.absoluteFilePath());
    if (storage.isValid()) {
        qint64 total = storage.bytesTotal();
        qint64 free = storage.bytesAvailable();
        qint64 used = total - free;
        double usedPct = total > 0 ? (double)used / total : 0;
        double freePct = total > 0 ? (double)free / total : 0;

        auto fmtSize = [](qint64 s) -> QString {
            if (s < 1024) return QString("%1 B").arg(s);
            if (s < 1024LL * 1024) return QString("%1 KB").arg(s / 1024.0, 0, 'f', 1);
            if (s < 1024LL * 1024 * 1024) return QString("%1 MB").arg(s / (1024.0 * 1024.0), 0, 'f', 1);
            return QString("%1 GB").arg(s / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
        };
        props["diskTotal"] = fmtSize(total);
        props["diskUsed"] = fmtSize(used);
        props["diskFree"] = fmtSize(free);
        props["diskUsedPercent"] = usedPct;
        props["diskUsedPctText"] = QString("%1%").arg(qRound(usedPct * 100));
        props["diskFreePctText"] = QString("%1%").arg(qRound(freePct * 100));
    }

    // MIME type
    auto mime = mimeDb().mimeTypeForFile(info);
    props["mimeType"] = mime.name();
    props["mimeDescription"] = mime.comment();

    // Timestamps
    props["created"] = QLocale().toString(info.birthTime(), QLocale::LongFormat);
    props["modified"] = QLocale().toString(info.lastModified(), QLocale::LongFormat);
    props["accessed"] = QLocale().toString(info.lastRead(), QLocale::LongFormat);

    // Ownership
    props["owner"] = info.owner();
    props["group"] = info.group();

    // Permissions string
    auto p = info.permissions();
    QString permStr;
    permStr += (p & QFile::ReadOwner)  ? 'r' : '-';
    permStr += (p & QFile::WriteOwner) ? 'w' : '-';
    permStr += (p & QFile::ExeOwner)   ? 'x' : '-';
    permStr += (p & QFile::ReadGroup)  ? 'r' : '-';
    permStr += (p & QFile::WriteGroup) ? 'w' : '-';
    permStr += (p & QFile::ExeGroup)   ? 'x' : '-';
    permStr += (p & QFile::ReadOther)  ? 'r' : '-';
    permStr += (p & QFile::WriteOther) ? 'w' : '-';
    permStr += (p & QFile::ExeOther)   ? 'x' : '-';
    props["permissions"] = permStr;

    // Per-role access index: 0=None, 1=Read only, 2=Read & Write, 3=Read & Write & Execute
    // (for dropdown selectors)
    auto accessIndex = [](bool r, bool w, bool x) -> int {
        if (r && w && x) return 3;
        if (r && w)      return 2;
        if (r)           return 1;
        return 0;
    };
    props["ownerAccess"] = accessIndex(p & QFile::ReadOwner, p & QFile::WriteOwner, p & QFile::ExeOwner);
    props["groupAccess"] = accessIndex(p & QFile::ReadGroup, p & QFile::WriteGroup, p & QFile::ExeGroup);
    props["otherAccess"] = accessIndex(p & QFile::ReadOther, p & QFile::WriteOther, p & QFile::ExeOther);
    props["isExecutable"] = bool(p & QFile::ExeOwner);

    return props;
}

QVariantMap FileSystemModel::remoteFileProperties(const QString &path) const
{
    const QString normalizedPath = normalizeLocation(path);
    for (const auto &entry : m_remoteEntries) {
        if (entry.value(QStringLiteral("filePath")).toString() == normalizedPath)
            return buildRemotePropertiesFromEntry(entry);
    }

    QVariantMap props;
    QProcess proc;
    proc.start(QStringLiteral("gio"), {
        QStringLiteral("info"),
        QStringLiteral("-a"),
        QStringLiteral("standard::name,standard::display-name,standard::content-type,standard::size,standard::is-symlink,standard::symlink-target,time::created,time::modified,time::access,owner::user,owner::group,unix::mode,access::can-read,access::can-write,access::can-execute"),
        gioLocationArg(normalizedPath)
    });

    if (!proc.waitForFinished(8000) || proc.exitCode() != 0) {
        return buildFallbackRemoteProperties(normalizedPath);
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QHash<QString, QString> fields;
    bool inAttributes = false;
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed == QStringLiteral("attributes:")) {
            inAttributes = true;
            continue;
        }

        const int separator = trimmed.indexOf(':');
        if (separator < 0)
            continue;

        const QString key = trimmed.left(separator).trimmed();
        const QString value = trimmed.mid(separator + 1).trimmed();
        if (inAttributes)
            fields.insert(key, value);
        else
            fields.insert(key, value);
    }

    const QString typeText = fields.value(QStringLiteral("type")).toLower();
    const bool isDir = typeText.contains(QStringLiteral("directory"));
    const QString displayName = fields.value(QStringLiteral("display name"), locationFileName(normalizedPath));
    const QString mimeType = fields.value(QStringLiteral("standard::content-type"));
    const qint64 size = fields.value(QStringLiteral("standard::size")).toLongLong();
    const int unixMode = fields.value(QStringLiteral("unix::mode")).toInt();

    props["name"] = displayName;
    props["path"] = normalizedPath;
    props["parentDir"] = parentLocation(normalizedPath);
    props["isDir"] = isDir;
    props["isSymlink"] = fields.value(QStringLiteral("standard::is-symlink")) == QStringLiteral("TRUE");
    props["symlinkTarget"] = fields.value(QStringLiteral("standard::symlink-target"));
    props["iconName"] = iconNameForEntry(displayName, isDir, mimeType);
    props["mimeType"] = mimeType;
    props["mimeDescription"] = mimeType.isEmpty() ? QString() : mimeDb().mimeTypeForName(mimeType).comment();
    props["created"] = QLocale().toString(dateTimeFromSeconds(fields.value(QStringLiteral("time::created"))), QLocale::LongFormat);
    props["modified"] = QLocale().toString(dateTimeFromSeconds(fields.value(QStringLiteral("time::modified"))), QLocale::LongFormat);
    props["accessed"] = QLocale().toString(dateTimeFromSeconds(fields.value(QStringLiteral("time::access"))), QLocale::LongFormat);
    props["owner"] = fields.value(QStringLiteral("owner::user"));
    props["group"] = fields.value(QStringLiteral("owner::group"));
    props["permissions"] = permissionsStringFromMode(unixMode);
    props["ownerAccess"] = accessIndexFromMode(unixMode, 0400, 0200, 0100);
    props["groupAccess"] = accessIndexFromMode(unixMode, 0040, 0020, 0010);
    props["otherAccess"] = accessIndexFromMode(unixMode, 0004, 0002, 0001);
    props["isExecutable"] = bool(unixMode & 0100) || fields.value(QStringLiteral("access::can-execute")) == QStringLiteral("TRUE");
    props["canEditPermissions"] = false;

    if (isDir) {
        props["contentText"] = QString();
        props["sizeText"] = QString();
        props["size"] = qint64(-1);
    } else {
        props["size"] = size;
        props["sizeText"] = formattedSize(size, true);
    }

    return props;
}

QVariantMap FileSystemModel::trashFileProperties(const QString &path) const
{
    for (const auto &entry : m_trashEntries) {
        if (entry.value("filePath").toString() == path)
            return buildTrashProperties(entry);
    }

    QVariantMap props;
    props["name"] = QUrl(path).fileName();
    props["path"] = path;
    props["parentDir"] = QString();
    props["isDir"] = false;
    props["isSymlink"] = false;
    props["iconName"] = iconNameForEntry(props.value("name").toString(), false);
    props["size"] = qint64(-1);
    props["sizeText"] = QString();
    props["mimeType"] = QString();
    props["mimeDescription"] = QString();
    props["created"] = QString();
    props["modified"] = QString();
    props["accessed"] = QString();
    props["owner"] = QString();
    props["group"] = QString();
    props["permissions"] = QString();
    props["ownerAccess"] = 0;
    props["groupAccess"] = 0;
    props["otherAccess"] = 0;
    props["isExecutable"] = false;
    props["canEditPermissions"] = false;
    props["isTrashItem"] = true;
    props["deleted"] = QString();
    return props;
}

// True when this binary is running inside a Flatpak sandbox.
static bool runningInFlatpak()
{
    static const bool inSandbox = QFile::exists(QStringLiteral("/.flatpak-info"));
    return inSandbox;
}

// Directories to scan for installed application .desktop files. Inside a
// Flatpak sandbox, QStandardPaths::ApplicationsLocation only sees the
// runtime + bundled apps, so we point at the host paths exposed via
// `--filesystem=host` (which mounts host /usr at /run/host/usr).
static QStringList applicationDataDirs()
{
    if (!runningInFlatpak())
        return QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);

    QStringList dirs;
    const QString home = QDir::homePath();
    dirs << home + QStringLiteral("/.local/share/applications")
         << home + QStringLiteral("/.local/share/flatpak/exports/share/applications")
         << QStringLiteral("/run/host/usr/local/share/applications")
         << QStringLiteral("/run/host/usr/share/applications")
         << QStringLiteral("/run/host/var/lib/flatpak/exports/share/applications");
    return dirs;
}

// Run a host CLI tool, transparently wrapping it in `flatpak-spawn --host`
// when we're inside a Flatpak sandbox. Returns trimmed stdout. (Default
// for timeoutMs is on the forward declaration at the top of the file.)
static QString runHostTool(const QString &program, const QStringList &arguments,
                           int timeoutMs)
{
    QProcess proc;
    if (runningInFlatpak()) {
        QStringList args;
        args << QStringLiteral("--host") << program << arguments;
        proc.start(QStringLiteral("flatpak-spawn"), args);
    } else {
        proc.start(program, arguments);
    }
    proc.waitForFinished(timeoutMs);
    return QString::fromUtf8(proc.readAllStandardOutput());
}

static QString desktopFileName(const QString &desktopId)
{
    // Search standard application dirs for a .desktop file
    for (const auto &dir : applicationDataDirs()) {
        QString path = dir + "/" + desktopId;
        if (QFile::exists(path))
            return path;
    }
    return {};
}

static QString readDesktopField(const QString &desktopPath, const QString &field)
{
    if (desktopPath.isEmpty())
        return {};
    QSettings desktop(desktopPath, QSettings::IniFormat);
    desktop.beginGroup("Desktop Entry");
    return desktop.value(field).toString();
}

QVariantList FileSystemModel::availableApps(const QString &mimeType) const
{
    QVariantList apps;
    if (mimeType.isEmpty())
        return apps;

    // Inside a Flatpak this transparently runs `flatpak-spawn --host gio
    // mime <type>` so we see the host's MIME associations and host apps.
    QString output = runHostTool(QStringLiteral("gio"),
                                 {QStringLiteral("mime"), mimeType});

    // Parse "gio mime" output — registered apps appear after "Registered applications:"
    bool inRegistered = false;
    auto lines = output.split('\n');
    QSet<QString> seen;

    // Also grab the default
    QString defaultId;
    for (const auto &line : lines) {
        auto trimmed = line.trimmed();
        if (trimmed.startsWith("Default application")) {
            int colonIdx = trimmed.lastIndexOf(':');
            if (colonIdx >= 0)
                defaultId = trimmed.mid(colonIdx + 1).trimmed();
        }
        if (trimmed.startsWith("Registered applications:") || trimmed.startsWith("Recommended applications:")) {
            inRegistered = true;
            continue;
        }
        if (trimmed.isEmpty() || trimmed.startsWith("No ")) {
            inRegistered = false;
            continue;
        }
        if (inRegistered && trimmed.endsWith(".desktop") && !seen.contains(trimmed)) {
            seen.insert(trimmed);
            QString desktopId = trimmed;
            QString path = desktopFileName(desktopId);
            QString name = readDesktopField(path, "Name");
            if (name.isEmpty())
                name = desktopId.chopped(8);
            QString icon = readDesktopField(path, "Icon");

            QVariantMap app;
            app["desktopFile"] = desktopId;
            app["name"] = name;
            app["iconName"] = icon;
            app["isDefault"] = (desktopId == defaultId);
            apps.append(app);
        }
    }

    // Ensure the default app is in the list even if not registered/recommended
    if (!defaultId.isEmpty() && !seen.contains(defaultId)) {
        QString path = desktopFileName(defaultId);
        QString name = readDesktopField(path, "Name");
        if (name.isEmpty())
            name = defaultId.chopped(8);
        QString icon = readDesktopField(path, "Icon");

        QVariantMap app;
        app["desktopFile"] = defaultId;
        app["name"] = name;
        app["iconName"] = icon;
        app["isDefault"] = true;
        apps.prepend(app);
    }

    return apps;
}

QString FileSystemModel::defaultApp(const QString &mimeType) const
{
    return runHostTool(QStringLiteral("xdg-mime"),
                       {QStringLiteral("query"), QStringLiteral("default"), mimeType},
                       2000).trimmed();
}

void FileSystemModel::setDefaultApp(const QString &mimeType, const QString &desktopFile)
{
    runHostTool(QStringLiteral("xdg-mime"),
                {QStringLiteral("default"), desktopFile, mimeType}, 2000);
}

QVariantList FileSystemModel::allInstalledApps() const
{
    QVariantList apps;
    QSet<QString> seen;

    const auto dataDirs = applicationDataDirs();
    for (const auto &dir : dataDirs) {
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            // Use relative path as desktop ID (e.g. "org.kde.dolphin.desktop")
            QString desktopId = QDir(dir).relativeFilePath(it.filePath());
            if (seen.contains(desktopId))
                continue;
            seen.insert(desktopId);

            QString name = readDesktopField(it.filePath(), "Name");
            if (name.isEmpty())
                continue;
            QString icon = readDesktopField(it.filePath(), "Icon");
            QString noDisplay = readDesktopField(it.filePath(), "NoDisplay");
            if (noDisplay.compare("true", Qt::CaseInsensitive) == 0)
                continue;

            QVariantMap app;
            app["desktopFile"] = desktopId;
            app["name"] = name;
            app["iconName"] = icon;
            apps.append(app);
        }
    }

    // Sort by name
    std::sort(apps.begin(), apps.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["name"].toString().compare(b.toMap()["name"].toString(), Qt::CaseInsensitive) < 0;
    });

    return apps;
}

bool FileSystemModel::setFilePermissions(const QString &path, int ownerAccess, int groupAccess, int otherAccess)
{
    if (isTrashUri(path) || isRemoteUri(path))
        return false;

    // accessIndex: 0=None, 1=Read only, 2=Read & Write, 3=Read & Write & Execute
    QFile::Permissions perms;

    auto applyAccess = [](int access, QFile::Permission r, QFile::Permission w, QFile::Permission x) -> QFile::Permissions {
        QFile::Permissions p;
        if (access >= 1) p |= r;
        if (access >= 2) p |= w;
        if (access >= 3) p |= x;
        return p;
    };

    perms |= applyAccess(ownerAccess, QFile::ReadOwner, QFile::WriteOwner, QFile::ExeOwner);
    perms |= applyAccess(groupAccess, QFile::ReadGroup, QFile::WriteGroup, QFile::ExeGroup);
    perms |= applyAccess(otherAccess, QFile::ReadOther, QFile::WriteOther, QFile::ExeOther);

    // Also set the User variants (Qt uses both)
    perms |= applyAccess(ownerAccess, QFile::ReadUser, QFile::WriteUser, QFile::ExeUser);

    return QFile::setPermissions(path, perms);
}

QString FileSystemModel::homePath() const
{
    return QDir::homePath();
}

QVariantList FileSystemModel::pathSuggestions(const QString &input, int limit) const
{
    QVariantList suggestions;

    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty() || limit <= 0)
        return suggestions;

    const bool preferTildeDisplay = trimmed == QStringLiteral("~")
        || trimmed.startsWith(QStringLiteral("~/"));

    const QString expanded = expandUserPath(trimmed);
    if (isRemoteUri(expanded) || isTrashUri(expanded))
        return suggestions;

    QString parentPath;
    QString fragment;

    if (expanded == QStringLiteral("/")) {
        parentPath = QStringLiteral("/");
    } else if (expanded.endsWith(QLatin1Char('/'))) {
        parentPath = QDir::cleanPath(expanded);
    } else {
        const int slashIndex = expanded.lastIndexOf(QLatin1Char('/'));
        if (slashIndex < 0)
            return suggestions;

        parentPath = slashIndex == 0 ? QStringLiteral("/") : expanded.left(slashIndex);
        fragment = expanded.mid(slashIndex + 1);
    }

    const QDir dir(parentPath);
    if (!dir.exists())
        return suggestions;

    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (m_showHidden || fragment.startsWith(QLatin1Char('.')))
        filters |= QDir::Hidden;

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (!fragment.isEmpty() && !name.startsWith(fragment, Qt::CaseInsensitive))
            continue;

        QVariantMap suggestion;
        const QString absolutePath = QDir::cleanPath(entry.absoluteFilePath());
        suggestion.insert(QStringLiteral("path"), absolutePath);
        suggestion.insert(QStringLiteral("displayPath"), preferTildeDisplay ? displayPathForSuggestion(absolutePath) : absolutePath);
        suggestion.insert(QStringLiteral("name"), name);
        suggestions.append(suggestion);

        if (suggestions.size() >= limit)
            break;
    }

    return suggestions;
}

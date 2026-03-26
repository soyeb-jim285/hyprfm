#include "models/filesystemmodel.h"
#include <QLocale>
#include <QDateTime>
#include <QDebug>
#include <QMimeDatabase>
#include <QStorageInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

FileSystemModel::FileSystemModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        reload();
    });
}

int FileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant FileSystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const QFileInfo &info = m_entries.at(index.row());

    switch (role) {
    case FileNameRole:
        return info.fileName();
    case FilePathRole:
        return info.absoluteFilePath();
    case FileSizeRole:
        return info.isDir() ? QVariant(-1) : QVariant(info.size());
    case FileSizeTextRole: {
        if (info.isDir())
            return QString();
        qint64 size = info.size();
        if (size < 1024)
            return QString("%1 B").arg(size);
        else if (size < 1024 * 1024)
            return QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
        else if (size < 1024LL * 1024 * 1024)
            return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
        else
            return QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    }
    case FileTypeRole:
        return info.isDir() ? QStringLiteral("folder") : info.suffix().toLower();
    case FileModifiedRole:
        return info.lastModified();
    case FileModifiedTextRole:
        return QLocale().toString(info.lastModified(), QLocale::ShortFormat);
    case FilePermissionsRole: {
        auto p = info.permissions();
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
    case IsDirRole:
        return info.isDir();
    case IsSymlinkRole:
        return info.isSymLink();
    case FileIconNameRole: {
        if (info.isDir())
            return QStringLiteral("folder");
        QString suffix = info.suffix().toLower();
        // Map common suffixes to freedesktop icon names
        static const QHash<QString, QString> iconMap = {
            {"png", "image-x-generic"}, {"jpg", "image-x-generic"},
            {"jpeg", "image-x-generic"}, {"gif", "image-x-generic"},
            {"svg", "image-x-generic"}, {"webp", "image-x-generic"},
            {"bmp", "image-x-generic"},
            {"mp3", "audio-x-generic"}, {"flac", "audio-x-generic"},
            {"ogg", "audio-x-generic"}, {"wav", "audio-x-generic"},
            {"mp4", "video-x-generic"}, {"mkv", "video-x-generic"},
            {"avi", "video-x-generic"}, {"webm", "video-x-generic"},
            {"pdf", "application-pdf"},
            {"zip", "package-x-generic"}, {"tar", "package-x-generic"},
            {"gz", "package-x-generic"}, {"xz", "package-x-generic"},
            {"7z", "package-x-generic"}, {"rar", "package-x-generic"},
            {"txt", "text-x-generic"}, {"md", "text-x-generic"},
            {"cpp", "text-x-generic"}, {"h", "text-x-generic"},
            {"py", "text-x-generic"}, {"js", "text-x-generic"},
            {"rs", "text-x-generic"}, {"go", "text-x-generic"},
            {"sh", "text-x-script"}, {"bash", "text-x-script"},
            {"html", "text-html"}, {"css", "text-css"},
            {"json", "text-x-generic"}, {"xml", "text-xml"},
            {"toml", "text-x-generic"}, {"yaml", "text-x-generic"},
        };
        return iconMap.value(suffix, QStringLiteral("text-x-generic"));
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
    };
}

QString FileSystemModel::rootPath() const { return m_rootPath; }
bool FileSystemModel::showHidden() const { return m_showHidden; }
int FileSystemModel::fileCount() const { return m_fileCount; }
int FileSystemModel::folderCount() const { return m_folderCount; }

void FileSystemModel::setRootPath(const QString &path)
{
    if (m_rootPath == path)
        return;

    // Stop watching old directory
    if (!m_rootPath.isEmpty())
        m_watcher.removePath(m_rootPath);

    m_rootPath = path;

    // Watch new directory
    if (!m_rootPath.isEmpty())
        m_watcher.addPath(m_rootPath);

    reload();
    emit rootPathChanged();
}

void FileSystemModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    reload();
    emit showHiddenChanged();
}

void FileSystemModel::sortByColumn(const QString &column, bool ascending)
{
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
    reload();
}

QString FileSystemModel::filePath(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row).absoluteFilePath();
}

bool FileSystemModel::isDir(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return false;
    return m_entries.at(row).isDir();
}

QString FileSystemModel::fileName(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row).fileName();
}

void FileSystemModel::reload()
{
    beginResetModel();
    m_entries.clear();

    if (!m_rootPath.isEmpty()) {
        QDir dir(m_rootPath);
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (m_showHidden)
            filters |= QDir::Hidden;

        m_entries = dir.entryInfoList(filters, m_sortFlags);
    }

    // Count
    int files = 0, folders = 0;
    for (const auto &info : m_entries) {
        if (info.isDir())
            ++folders;
        else
            ++files;
    }
    m_fileCount = files;
    m_folderCount = folders;

    endResetModel();
    emit countsChanged();
}

QVariantMap FileSystemModel::fileProperties(const QString &path) const
{
    QFileInfo info(path);
    QVariantMap props;

    props["name"] = info.fileName();
    props["path"] = info.absoluteFilePath();
    props["parentDir"] = info.absolutePath();
    props["isDir"] = info.isDir();
    props["isSymlink"] = info.isSymLink();

    // Icon name (reuse the same mapping as data())
    if (info.isDir()) {
        props["iconName"] = QStringLiteral("folder");
    } else {
        QString suffix = info.suffix().toLower();
        static const QHash<QString, QString> iconMap = {
            {"png", "image-x-generic"}, {"jpg", "image-x-generic"},
            {"jpeg", "image-x-generic"}, {"gif", "image-x-generic"},
            {"svg", "image-x-generic"}, {"webp", "image-x-generic"},
            {"bmp", "image-x-generic"},
            {"mp3", "audio-x-generic"}, {"flac", "audio-x-generic"},
            {"ogg", "audio-x-generic"}, {"wav", "audio-x-generic"},
            {"mp4", "video-x-generic"}, {"mkv", "video-x-generic"},
            {"avi", "video-x-generic"}, {"webm", "video-x-generic"},
            {"pdf", "application-pdf"},
            {"zip", "package-x-generic"}, {"tar", "package-x-generic"},
            {"gz", "package-x-generic"}, {"xz", "package-x-generic"},
            {"7z", "package-x-generic"}, {"rar", "package-x-generic"},
            {"txt", "text-x-generic"}, {"md", "text-x-generic"},
            {"cpp", "text-x-generic"}, {"h", "text-x-generic"},
            {"py", "text-x-generic"}, {"js", "text-x-generic"},
            {"rs", "text-x-generic"}, {"go", "text-x-generic"},
            {"sh", "text-x-script"}, {"bash", "text-x-script"},
            {"html", "text-html"}, {"css", "text-css"},
            {"json", "text-x-generic"}, {"xml", "text-xml"},
            {"toml", "text-x-generic"}, {"yaml", "text-x-generic"},
        };
        props["iconName"] = iconMap.value(suffix, QStringLiteral("text-x-generic"));
    }
    if (info.isSymLink())
        props["symlinkTarget"] = info.symLinkTarget();

    // Size
    if (info.isDir()) {
        QDir dir(path);
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
        if (size < 1024)
            props["sizeText"] = QString("%1 B").arg(size);
        else if (size < 1024 * 1024)
            props["sizeText"] = QString("%1 KB (%2 bytes)").arg(size / 1024.0, 0, 'f', 1).arg(QLocale().toString(size));
        else if (size < 1024LL * 1024 * 1024)
            props["sizeText"] = QString("%1 MB (%2 bytes)").arg(size / (1024.0 * 1024.0), 0, 'f', 1).arg(QLocale().toString(size));
        else
            props["sizeText"] = QString("%1 GB (%2 bytes)").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2).arg(QLocale().toString(size));
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
    QMimeDatabase mimeDb;
    auto mime = mimeDb.mimeTypeForFile(info);
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

static QString desktopFileName(const QString &desktopId)
{
    // Search standard application dirs for a .desktop file
    auto dataDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const auto &dir : dataDirs) {
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

    QProcess proc;
    proc.start("gio", {"mime", mimeType});
    proc.waitForFinished(3000);
    QString output = QString::fromUtf8(proc.readAllStandardOutput());

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

    return apps;
}

QString FileSystemModel::defaultApp(const QString &mimeType) const
{
    QProcess proc;
    proc.start("xdg-mime", {"query", "default", mimeType});
    proc.waitForFinished(2000);
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

void FileSystemModel::setDefaultApp(const QString &mimeType, const QString &desktopFile)
{
    QProcess proc;
    proc.start("xdg-mime", {"default", desktopFile, mimeType});
    proc.waitForFinished(2000);
}

bool FileSystemModel::setFilePermissions(const QString &path, int ownerAccess, int groupAccess, int otherAccess)
{
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

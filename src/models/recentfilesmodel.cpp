#include "models/recentfilesmodel.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>

RecentFilesModel::RecentFilesModel(const QString &storagePath, QObject *parent)
    : QAbstractListModel(parent)
    , m_storagePath(storagePath)
{
    load();
}

int RecentFilesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant RecentFilesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const QFileInfo info(m_entries.at(index.row()).path);
    if (!info.exists())
        return {};

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

QHash<int, QByteArray> RecentFilesModel::roleNames() const
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

void RecentFilesModel::addRecent(const QString &path)
{
    // Remove existing entry for this path
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == path) {
            beginRemoveRows(QModelIndex(), i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            break;
        }
    }

    // Insert at front
    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.prepend({path, QDateTime::currentDateTime()});
    endInsertRows();

    // Trim if over max
    if (m_entries.size() > m_maxEntries) {
        beginRemoveRows(QModelIndex(), m_maxEntries, m_entries.size() - 1);
        m_entries.resize(m_maxEntries);
        endRemoveRows();
    }

    emit countChanged();
    save();
}

void RecentFilesModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    save();
}

void RecentFilesModel::load()
{
    QFile file(m_storagePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;

    for (const auto &val : doc.array()) {
        auto obj = val.toObject();
        QString path = obj["path"].toString();
        QDateTime time = QDateTime::fromString(obj["time"].toString(), Qt::ISODate);
        if (!path.isEmpty() && QFileInfo::exists(path))
            m_entries.append({path, time});
    }
}

void RecentFilesModel::save() const
{
    QFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QJsonArray arr;
    for (const auto &entry : m_entries) {
        QJsonObject obj;
        obj["path"] = entry.path;
        obj["time"] = entry.accessTime.toString(Qt::ISODate);
        arr.append(obj);
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

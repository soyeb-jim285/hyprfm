#include "models/recentfilesmodel.h"
#include "services/filesystemtasks.h"
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

    const RecentEntry &entry = m_entries.at(index.row());
    if (role == FileNameRole)
        return QFileInfo(entry.path).fileName();
    if (role == FilePathRole)
        return QFileInfo(entry.path).absoluteFilePath();
    if (!entry.details.isEmpty())
        return entry.details.value(role);
    switch (role) {
    case FileSizeRole:
        return qint64(-1);
    case FileModifiedRole:
        return QDateTime();
    case IsDirRole:
    case IsSymlinkRole:
    case HasImagePreviewRole:
    case HasVideoPreviewRole:
    case HasPdfPreviewRole:
        return false;
    default:
        return QString();
    }
}

QVariant RecentFilesModel::readRole(const QFileInfo &info, const QMimeType &mime, int role)
{
    switch (role) {
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
        if (mime.isValid()) {
            QString icon = mime.iconName();
            if (icon.isEmpty())
                icon = mime.genericIconName();
            if (!icon.isEmpty())
                return icon;
        }
        return QStringLiteral("text-x-generic");
    }
    case GitStatusRole:
    case GitStatusIconRole:
        return QString();
    case HasImagePreviewRole:
        return mime.name().startsWith(QLatin1String("image/"));
    case HasVideoPreviewRole:
        return mime.name().startsWith(QLatin1String("video/"));
    case HasPdfPreviewRole:
        return mime.name() == QLatin1String("application/pdf");
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
        {GitStatusRole,        "gitStatus"},
        {GitStatusIconRole,    "gitStatusIcon"},
        {HasImagePreviewRole,  "hasImagePreview"},
        {HasVideoPreviewRole,  "hasVideoPreview"},
        {HasPdfPreviewRole,    "hasPdfPreview"},
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
    const RecentEntry added{path, QDateTime::currentDateTime(), {}};
    m_entries.prepend(added);
    endInsertRows();

    // Trim if over max
    if (m_entries.size() > m_maxEntries) {
        beginRemoveRows(QModelIndex(), m_maxEntries, m_entries.size() - 1);
        m_entries.resize(m_maxEntries);
        endRemoveRows();
    }

    emit countChanged();
    save();
    probeEntries({added});
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
        if (!path.isEmpty())
            m_entries.append({path, time, {}});
    }
    probeEntries(m_entries);
}

// A recents list can span several mounts. A drive that stops responding
// must not delay the other entries or make a data() call wait for I/O.
void RecentFilesModel::probeEntries(const QList<RecentEntry> &entries)
{
    const MountTable mounts;
    QHash<QByteArray, QList<RecentEntry>> batches;
    for (const RecentEntry &entry : entries)
        batches[mounts.forPath(entry.path)].append(entry);

    for (auto it = batches.begin(); it != batches.end(); ++it) {
        const auto scan = [batch = std::move(it.value())]() mutable {
            const QMimeDatabase mimeDb;
            for (RecentEntry &entry : batch) {
                const QFileInfo info(entry.path);
                if (!info.exists())
                    continue;
                const QMimeType mime = mimeDb.mimeTypeForFile(info);
                for (int role = FileSizeRole; role <= HasPdfPreviewRole; ++role)
                    entry.details.insert(role, readRole(info, mime, role));
            }
            return std::move(batch);
        };
        watchFilesystemTask(this, it.key(), scan, [this](const QList<RecentEntry> &results) {
            for (const RecentEntry &result : results) {
                for (qsizetype row = 0; row < m_entries.size(); ++row) {
                    RecentEntry &entry = m_entries[row];
                    if (entry.path != result.path || entry.accessTime != result.accessTime)
                        continue;
                    if (result.details.isEmpty()) {
                        beginRemoveRows({}, row, row);
                        m_entries.removeAt(row);
                        endRemoveRows();
                        emit countChanged();
                    } else {
                        entry.details = result.details;
                        emit dataChanged(index(row), index(row));
                    }
                    break;
                }
            }
        });
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

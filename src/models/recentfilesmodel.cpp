#include "models/recentfilesmodel.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent>

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
        // Same MIME-driven approach as FileSystemModel — handles ambiguous
        // extensions like .ts correctly via QMimeDatabase content sniffing.
        static QMimeDatabase mimeDb;
        const QMimeType mime = mimeDb.mimeTypeForFile(info);
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
    case HasImagePreviewRole: {
        static QMimeDatabase mimeDb;
        return mimeDb.mimeTypeForFile(info).name()
            .startsWith(QLatin1String("image/"));
    }
    case HasVideoPreviewRole: {
        static QMimeDatabase mimeDb;
        return mimeDb.mimeTypeForFile(info).name()
            .startsWith(QLatin1String("video/"));
    }
    case HasPdfPreviewRole: {
        static QMimeDatabase mimeDb;
        return mimeDb.mimeTypeForFile(info).name() == QLatin1String("application/pdf");
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
        if (!path.isEmpty())
            m_entries.append({path, time});
    }
    dropMissingInBackground();
}

// Recent files can live on any disk, and checking that one still exists is
// a stat on it: a spun-down drive or an unreachable network mount held up
// the whole launch, since this model is built before the window. Load the
// list as saved and drop the missing entries once a worker has looked.
void RecentFilesModel::dropMissingInBackground()
{
    QStringList paths;
    for (const RecentEntry &entry : std::as_const(m_entries))
        paths.append(entry.path);
    if (paths.isEmpty())
        return;

    auto *watcher = new QFutureWatcher<QSet<QString>>(this);
    connect(watcher, &QFutureWatcher<QSet<QString>>::finished, this, [this, watcher]() {
        const QSet<QString> missing = watcher->future().takeResult();
        watcher->deleteLater();
        if (missing.isEmpty())
            return;
        beginResetModel();
        m_entries.removeIf([&](const RecentEntry &entry) { return missing.contains(entry.path); });
        endResetModel();
    });
    watcher->setFuture(QtConcurrent::run([paths]() {
        QSet<QString> missing;
        for (const QString &path : paths)
            if (!QFileInfo::exists(path))
                missing.insert(path);
        return missing;
    }));
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

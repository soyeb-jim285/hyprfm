#include "models/searchresultsmodel.h"
#include <QMimeDatabase>
#include <QLocale>

SearchResultsModel::SearchResultsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SearchResultsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_entries.size();
}

QVariant SearchResultsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const QFileInfo &info = m_entries.at(index.row());

    switch (role) {
    case FileNameRole:       return info.fileName();
    case FilePathRole:       return info.absoluteFilePath();
    case FileSizeRole:       return info.size();
    case FileSizeTextRole:   return info.isDir() ? QString() : formatSize(info.size());
    case FileTypeRole: {
        QMimeDatabase db;
        return db.mimeTypeForFile(info).comment();
    }
    case FileModifiedRole:     return info.lastModified();
    case FileModifiedTextRole: return QLocale().toString(info.lastModified(), QLocale::ShortFormat);
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
    case IsDirRole:       return info.isDir();
    case IsSymlinkRole:   return info.isSymLink();
    case FileIconNameRole: {
        QMimeDatabase db;
        return db.mimeTypeForFile(info).iconName();
    }
    }
    return {};
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const
{
    return {
        {FileNameRole,        "fileName"},
        {FilePathRole,        "filePath"},
        {FileSizeRole,        "fileSize"},
        {FileSizeTextRole,    "fileSizeText"},
        {FileTypeRole,        "fileType"},
        {FileModifiedRole,    "fileModified"},
        {FileModifiedTextRole,"fileModifiedText"},
        {FilePermissionsRole, "filePermissions"},
        {IsDirRole,           "isDir"},
        {IsSymlinkRole,       "isSymlink"},
        {FileIconNameRole,    "fileIconName"},
    };
}

QString SearchResultsModel::filePath(int row) const
{
    if (row < 0 || row >= m_entries.size()) return {};
    return m_entries.at(row).absoluteFilePath();
}

bool SearchResultsModel::isDir(int row) const
{
    if (row < 0 || row >= m_entries.size()) return false;
    return m_entries.at(row).isDir();
}

QString SearchResultsModel::fileName(int row) const
{
    if (row < 0 || row >= m_entries.size()) return {};
    return m_entries.at(row).fileName();
}

void SearchResultsModel::addResults(const QList<QFileInfo> &entries)
{
    if (entries.isEmpty()) return;
    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size() + entries.size() - 1);
    m_entries.append(entries);
    endInsertRows();
}

void SearchResultsModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

QString SearchResultsModel::formatSize(qint64 bytes)
{
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024 * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}

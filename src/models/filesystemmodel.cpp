#include "models/filesystemmodel.h"
#include <QLocale>
#include <QDateTime>

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

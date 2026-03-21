#include "models/filesystemmodel.h"
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>

FileSystemModel::FileSystemModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_fsModel(new QFileSystemModel(this))
{
    m_fsModel->setRootPath(QString());
    setSourceModel(m_fsModel);

    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &) {
        updateCounts();
    });

    connect(this, &QAbstractItemModel::modelReset, this, &FileSystemModel::updateCounts);
    connect(this, &QAbstractItemModel::rowsInserted, this, &FileSystemModel::updateCounts);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &FileSystemModel::updateCounts);
}

QString FileSystemModel::rootPath() const { return m_rootPath; }
bool FileSystemModel::showHidden() const { return m_showHidden; }
int FileSystemModel::fileCount() const { return m_fileCount; }
int FileSystemModel::folderCount() const { return m_folderCount; }

void FileSystemModel::setRootPath(const QString &path)
{
    if (m_rootPath == path)
        return;
    m_rootPath = path;
    m_fsModel->setRootPath(path);
    emit rootPathChanged();
    updateCounts();
}

void FileSystemModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    invalidate();
    emit showHiddenChanged();
    updateCounts();
}

QModelIndex FileSystemModel::rootIndex() const
{
    QModelIndex srcRoot = m_fsModel->index(m_rootPath);
    return mapFromSource(srcRoot);
}

void FileSystemModel::sortByColumn(const QString &column, bool ascending)
{
    Qt::SortOrder order = ascending ? Qt::AscendingOrder : Qt::DescendingOrder;
    if (column == "name")
        QSortFilterProxyModel::sort(0, order);
    else if (column == "size")
        QSortFilterProxyModel::sort(1, order);
    else if (column == "modified")
        QSortFilterProxyModel::sort(3, order);
    else if (column == "type")
        QSortFilterProxyModel::sort(2, order);
    else
        QSortFilterProxyModel::sort(0, order);
}

QString FileSystemModel::filePath(int row) const
{
    QModelIndex idx = index(row, 0);
    if (!idx.isValid())
        return QString();
    return m_fsModel->filePath(mapToSource(idx));
}

bool FileSystemModel::isDir(int row) const
{
    QModelIndex idx = index(row, 0);
    if (!idx.isValid())
        return false;
    return m_fsModel->isDir(mapToSource(idx));
}

QString FileSystemModel::fileName(int row) const
{
    QModelIndex idx = index(row, 0);
    if (!idx.isValid())
        return QString();
    return m_fsModel->fileName(mapToSource(idx));
}

QVariant FileSystemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    QModelIndex srcIndex = mapToSource(index);
    QFileInfo info = m_fsModel->fileInfo(srcIndex);

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
        else if (size < 1024 * 1024 * 1024)
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
    case FilePermissionsRole:
        return static_cast<int>(info.permissions());
    case IsDirRole:
        return info.isDir();
    case IsSymlinkRole:
        return info.isSymLink();
    default:
        return QSortFilterProxyModel::data(index, role);
    }
}

QHash<int, QByteArray> FileSystemModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles[FileNameRole]         = "fileName";
    roles[FilePathRole]         = "filePath";
    roles[FileSizeRole]         = "fileSize";
    roles[FileSizeTextRole]     = "fileSizeText";
    roles[FileTypeRole]         = "fileType";
    roles[FileModifiedRole]     = "fileModified";
    roles[FileModifiedTextRole] = "fileModifiedText";
    roles[FilePermissionsRole]  = "filePermissions";
    roles[IsDirRole]            = "isDir";
    roles[IsSymlinkRole]        = "isSymlink";
    return roles;
}

bool FileSystemModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex idx = m_fsModel->index(sourceRow, 0, sourceParent);
    QString name = m_fsModel->fileName(idx);

    if (!m_showHidden && name.startsWith('.'))
        return false;

    return true;
}

void FileSystemModel::updateCounts()
{
    int files = 0;
    int folders = 0;
    int rows = rowCount();
    for (int i = 0; i < rows; ++i) {
        if (isDir(i))
            ++folders;
        else
            ++files;
    }
    if (m_fileCount != files || m_folderCount != folders) {
        m_fileCount = files;
        m_folderCount = folders;
        emit countsChanged();
    }
}

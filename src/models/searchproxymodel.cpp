#include "models/searchproxymodel.h"
#include "models/filesystemmodel.h"
#include <QDateTime>
#include <QMimeDatabase>

SearchProxyModel::SearchProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
}

QString SearchProxyModel::searchQuery() const { return m_searchQuery; }

void SearchProxyModel::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query) return;
    m_searchQuery = query;

    m_isGlob = query.contains('*') || query.contains('?') || query.contains('[')
               || query.startsWith('^') || query.endsWith('$');

    if (m_isGlob) {
        QString pattern = QRegularExpression::wildcardToRegularExpression(query);
        m_regex = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
    }

    invalidateFilter();
    emit searchQueryChanged();
    emit searchActiveChanged();
}

QString SearchProxyModel::fileTypeFilter() const { return m_fileTypeFilter; }

void SearchProxyModel::setFileTypeFilter(const QString &filter)
{
    if (m_fileTypeFilter == filter) return;
    m_fileTypeFilter = filter;
    invalidateFilter();
    emit fileTypeFilterChanged();
    emit searchActiveChanged();
}

QString SearchProxyModel::dateFilter() const { return m_dateFilter; }

void SearchProxyModel::setDateFilter(const QString &filter)
{
    if (m_dateFilter == filter) return;
    m_dateFilter = filter;
    invalidateFilter();
    emit dateFilterChanged();
    emit searchActiveChanged();
}

QString SearchProxyModel::sizeFilter() const { return m_sizeFilter; }

void SearchProxyModel::setSizeFilter(const QString &filter)
{
    if (m_sizeFilter == filter) return;
    m_sizeFilter = filter;
    invalidateFilter();
    emit sizeFilterChanged();
    emit searchActiveChanged();
}

bool SearchProxyModel::searchActive() const
{
    return !m_searchQuery.isEmpty() || !m_fileTypeFilter.isEmpty()
           || !m_dateFilter.isEmpty() || !m_sizeFilter.isEmpty();
}

bool SearchProxyModel::isGlobPattern() const { return m_isGlob; }

void SearchProxyModel::switchSourceModel(QAbstractItemModel *model)
{
    setSourceModel(model);
}

void SearchProxyModel::clearSearch()
{
    m_searchQuery.clear();
    m_fileTypeFilter.clear();
    m_dateFilter.clear();
    m_sizeFilter.clear();
    m_isGlob = false;
    m_regex = QRegularExpression();
    invalidateFilter();
    emit searchQueryChanged();
    emit fileTypeFilterChanged();
    emit dateFilterChanged();
    emit sizeFilterChanged();
    emit searchActiveChanged();
}

QString SearchProxyModel::filePath(int row) const
{
    QModelIndex proxyIdx = index(row, 0);
    if (!proxyIdx.isValid()) return {};
    QModelIndex srcIdx = mapToSource(proxyIdx);
    return sourceModel()->data(srcIdx, FileSystemModel::FilePathRole).toString();
}

bool SearchProxyModel::isDir(int row) const
{
    QModelIndex proxyIdx = index(row, 0);
    if (!proxyIdx.isValid()) return false;
    QModelIndex srcIdx = mapToSource(proxyIdx);
    return sourceModel()->data(srcIdx, FileSystemModel::IsDirRole).toBool();
}

QString SearchProxyModel::fileName(int row) const
{
    QModelIndex proxyIdx = index(row, 0);
    if (!proxyIdx.isValid()) return {};
    QModelIndex srcIdx = mapToSource(proxyIdx);
    return sourceModel()->data(srcIdx, FileSystemModel::FileNameRole).toString();
}

bool SearchProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (!searchActive()) return true;

    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    QString name = sourceModel()->data(idx, FileSystemModel::FileNameRole).toString();

    if (!m_searchQuery.isEmpty() && !matchesText(name))
        return false;
    if (!m_fileTypeFilter.isEmpty() && !matchesType(idx))
        return false;
    if (!m_dateFilter.isEmpty() && !matchesDate(idx))
        return false;
    if (!m_sizeFilter.isEmpty() && !matchesSize(idx))
        return false;

    return true;
}

bool SearchProxyModel::matchesText(const QString &fileName) const
{
    if (m_isGlob)
        return m_regex.match(fileName).hasMatch();
    return fileName.contains(m_searchQuery, Qt::CaseInsensitive);
}

bool SearchProxyModel::matchesSingleType(const QString &type, bool isDirectory,
                                          const QString &mime, const QString &filePath) const
{
    if (type == "folders")
        return isDirectory;
    if (isDirectory) return false;
    if (type == "documents")
        return mime.startsWith("text/") || mime == "application/pdf"
               || mime.startsWith("application/vnd.oasis") || mime.startsWith("application/msword")
               || mime.startsWith("application/vnd.openxmlformats");
    if (type == "images")
        return mime.startsWith("image/");
    if (type == "audio")
        return mime.startsWith("audio/");
    if (type == "video")
        return mime.startsWith("video/");
    if (type == "code") {
        static const QStringList codeExts = {
            "cpp", "h", "hpp", "c", "cc", "cxx", "py", "js", "ts", "jsx", "tsx",
            "rs", "go", "java", "kt", "swift", "rb", "php", "cs", "sh", "bash",
            "zsh", "fish", "lua", "zig", "nim", "ml", "hs", "el", "clj", "scala",
            "qml", "cmake", "toml", "yaml", "yml", "json", "xml", "html", "css",
            "scss", "less", "sql", "md", "rst", "tex",
        };
        QFileInfo info(filePath);
        return codeExts.contains(info.suffix().toLower());
    }
    return true;
}

bool SearchProxyModel::matchesType(const QModelIndex &sourceIndex) const
{
    bool isDirectory = sourceModel()->data(sourceIndex, FileSystemModel::IsDirRole).toBool();
    QString fp = sourceModel()->data(sourceIndex, FileSystemModel::FilePathRole).toString();
    QMimeDatabase db;
    QString mime = db.mimeTypeForFile(fp).name();

    // Support comma-separated type filters (OR'd)
    const auto types = m_fileTypeFilter.split(',', Qt::SkipEmptyParts);
    for (const auto &type : types) {
        if (matchesSingleType(type.trimmed(), isDirectory, mime, fp))
            return true;
    }
    return false;
}

bool SearchProxyModel::matchesDate(const QModelIndex &sourceIndex) const
{
    QDateTime modified = sourceModel()->data(sourceIndex, FileSystemModel::FileModifiedRole).toDateTime();
    if (!modified.isValid()) return false;

    QDateTime now = QDateTime::currentDateTime();
    if (m_dateFilter == "today")
        return modified.date() == now.date();
    if (m_dateFilter == "week")
        return modified.daysTo(now) <= 7;
    if (m_dateFilter == "month")
        return modified.daysTo(now) <= 30;
    if (m_dateFilter == "year")
        return modified.daysTo(now) <= 365;
    return true;
}

bool SearchProxyModel::matchesSize(const QModelIndex &sourceIndex) const
{
    bool dir = sourceModel()->data(sourceIndex, FileSystemModel::IsDirRole).toBool();
    if (dir) return true;

    qint64 size = sourceModel()->data(sourceIndex, FileSystemModel::FileSizeRole).toLongLong();

    if (m_sizeFilter == "tiny")    return size < 10 * 1024;
    if (m_sizeFilter == "small")   return size < 1024 * 1024;
    if (m_sizeFilter == "medium")  return size < 100 * 1024 * 1024;
    if (m_sizeFilter == "large")   return size < 1024LL * 1024 * 1024;
    if (m_sizeFilter == "huge")    return size >= 1024LL * 1024 * 1024;
    return true;
}

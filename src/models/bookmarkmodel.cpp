#include "models/bookmarkmodel.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace {

bool isRemoteUri(const QString &path)
{
    const QUrl url(path);
    return url.isValid() && !url.scheme().isEmpty()
        && url.scheme() != QStringLiteral("file")
        && url.scheme() != QStringLiteral("trash");
}

QString bookmarkDisplayName(const QString &path)
{
    if (!isRemoteUri(path)) {
        QString name = QDir(path).dirName();
        return name.isEmpty() ? path : name;
    }

    const QUrl url(path);
    const QString fileName = QUrl::fromPercentEncoding(url.fileName().toUtf8());
    if (!fileName.isEmpty())
        return fileName;
    if (!url.host().isEmpty())
        return url.host();
    return url.scheme().toUpper();
}

}

BookmarkModel::BookmarkModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BookmarkModel::rowCount(const QModelIndex &) const
{
    return m_bookmarks.size();
}

QVariant BookmarkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_bookmarks.size())
        return {};

    const auto &bm = m_bookmarks.at(index.row());
    switch (role) {
    case NameRole: return bm.name;
    case PathRole: return bm.path;
    case IconRole: return bm.icon;
    }
    return {};
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IconRole, "icon"},
    };
}

void BookmarkModel::setBookmarks(const QStringList &paths)
{
    QList<Bookmark> updatedBookmarks;
    updatedBookmarks.reserve(paths.size());

    bool unchanged = paths.size() == m_bookmarks.size();
    for (int i = 0; i < paths.size(); ++i) {
        const Bookmark bookmark = makeBookmark(paths.at(i));
        updatedBookmarks.append(bookmark);

        if (unchanged) {
            const Bookmark &current = m_bookmarks.at(i);
            unchanged = current.path == bookmark.path
                        && current.name == bookmark.name
                        && current.icon == bookmark.icon;
        }
    }

    if (unchanged)
        return;

    beginResetModel();
    m_bookmarks = updatedBookmarks;
    endResetModel();
    emit countChanged();
}

QStringList BookmarkModel::paths() const
{
    QStringList result;
    const QString home = QDir::homePath();
    for (const auto &bm : m_bookmarks) {
        // Store as ~/... for portability
        if (bm.path.startsWith(home))
            result.append("~" + bm.path.mid(home.length()));
        else
            result.append(bm.path);
    }
    return result;
}

void BookmarkModel::addBookmark(const QString &path)
{
    QString expanded = expandPath(path);
    if (containsPath(expanded))
        return;

    int row = m_bookmarks.size();
    beginInsertRows(QModelIndex(), row, row);
    m_bookmarks.append(makeBookmark(expanded));
    endInsertRows();
    emit countChanged();
    emit bookmarksChanged();
}

void BookmarkModel::insertBookmark(const QString &path, int index)
{
    QString expanded = expandPath(path);
    if (containsPath(expanded))
        return;

    int row = qBound(0, index, m_bookmarks.size());
    beginInsertRows(QModelIndex(), row, row);
    m_bookmarks.insert(row, makeBookmark(expanded));
    endInsertRows();
    emit countChanged();
    emit bookmarksChanged();
}

void BookmarkModel::removeBookmark(int index)
{
    if (index < 0 || index >= m_bookmarks.size())
        return;

    beginRemoveRows(QModelIndex(), index, index);
    m_bookmarks.removeAt(index);
    endRemoveRows();
    emit countChanged();
    emit bookmarksChanged();
}

void BookmarkModel::moveBookmark(int from, int to)
{
    if (from < 0 || from >= m_bookmarks.size() ||
        to < 0 || to >= m_bookmarks.size() || from == to)
        return;

    // QAbstractListModel::beginMoveRows requires special handling when moving down
    int destRow = (to > from) ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destRow))
        return;
    m_bookmarks.move(from, to);
    endMoveRows();
    emit bookmarksChanged();
}

bool BookmarkModel::containsPath(const QString &path) const
{
    QString expanded = expandPath(path);
    for (const auto &bm : m_bookmarks) {
        if (bm.path == expanded)
            return true;
    }
    return false;
}

BookmarkModel::Bookmark BookmarkModel::makeBookmark(const QString &rawPath) const
{
    QString expanded = expandPath(rawPath);
    QString name = bookmarkDisplayName(expanded);
    return {name, expanded, iconForPath(name.toLower())};
}

QString BookmarkModel::expandPath(const QString &path)
{
    const QUrl url(path);
    if (url.isValid() && !url.scheme().isEmpty() && url.scheme() != QStringLiteral("file"))
        return url.toString(QUrl::FullyEncoded);
    if (path.startsWith("~/"))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString BookmarkModel::iconForPath(const QString &name)
{
    static const QMap<QString, QString> icons = {
        {"home", "go-home"},
        {"documents", "folder-documents"},
        {"downloads", "folder-download"},
        {"pictures", "folder-pictures"},
        {"music", "folder-music"},
        {"videos", "folder-videos"},
        {"desktop", "user-desktop"},
        {"projects", "folder-development"},
        {"sftp", "folder-remote"},
        {"smb", "folder-remote"},
        {"ftp", "folder-remote"},
        {"network", "folder-remote"},
    };
    return icons.value(name, "folder");
}

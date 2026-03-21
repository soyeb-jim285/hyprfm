#include "models/bookmarkmodel.h"
#include <QDir>

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
    beginResetModel();
    m_bookmarks.clear();
    for (const auto &p : paths) {
        QString expanded = expandPath(p);
        QDir dir(expanded);
        QString name = dir.dirName();
        m_bookmarks.append({name, expanded, iconForPath(name.toLower())});
    }
    endResetModel();
}

QString BookmarkModel::expandPath(const QString &path)
{
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
    };
    return icons.value(name, "folder");
}

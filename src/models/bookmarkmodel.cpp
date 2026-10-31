#include "models/bookmarkmodel.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <climits>

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
    case NameRole: return bm.customName.isEmpty() ? bm.name : bm.customName;
    case PathRole: return bm.secondaryPath.isEmpty() ? bm.path : QString();
    case IconRole: return bm.icon;
    case SplitRole: return splitAt(index.row());
    }
    return {};
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IconRole, "icon"},
        {SplitRole, "split"},
    };
}

void BookmarkModel::setBookmarks(const QStringList &paths, const QVariantMap &names,
                                 const QVariantList &splits)
{
    QList<Bookmark> updatedBookmarks;
    updatedBookmarks.reserve(paths.size());

    for (int i = 0; i < paths.size(); ++i) {
        Bookmark bookmark = makeBookmark(paths.at(i));
        bookmark.customName = names.value(portablePath(bookmark.path)).toString().trimmed();
        updatedBookmarks.append(bookmark);
    }

    // Old saved_splits have no position: append them to the same bookmark
    // list. New entries remember their place among ordinary bookmarks.
    QVariantList orderedSplits = splits;
    std::stable_sort(orderedSplits.begin(), orderedSplits.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("position", INT_MAX).toInt()
             < b.toMap().value("position", INT_MAX).toInt();
    });
    for (const auto &value : orderedSplits) {
        const QVariantMap split = value.toMap();
        const QString primary = split.value("primaryPath").toString();
        const QString secondary = split.value("secondaryPath").toString();
        if (primary.isEmpty() || secondary.isEmpty())
            continue;
        const int position = qBound(0, split.value("position", updatedBookmarks.size()).toInt(),
                                    int(updatedBookmarks.size()));
        updatedBookmarks.insert(position, makeSplitBookmark(primary, secondary,
            split.value("orientation").toString(), split.value("name").toString()));
    }
    const bool unchanged = updatedBookmarks.size() == m_bookmarks.size()
        && std::equal(updatedBookmarks.cbegin(), updatedBookmarks.cend(), m_bookmarks.cbegin(),
            [](const Bookmark &a, const Bookmark &b) {
                return a.path == b.path && a.name == b.name && a.icon == b.icon
                    && a.customName == b.customName && a.secondaryPath == b.secondaryPath
                    && a.orientation == b.orientation;
            });
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
    for (const auto &bm : m_bookmarks) {
        if (bm.secondaryPath.isEmpty())
            result.append(portablePath(bm.path));
    }
    return result;
}

QVariantMap BookmarkModel::names() const
{
    QVariantMap result;
    for (const auto &bm : m_bookmarks) {
        if (bm.secondaryPath.isEmpty() && !bm.customName.isEmpty())
            result.insert(portablePath(bm.path), bm.customName);
    }
    return result;
}

QString BookmarkModel::portablePath(const QString &path)
{
    // Store as ~/... for portability
    const QString home = QDir::homePath();
    if (path == home || path.startsWith(home + '/'))
        return "~" + path.mid(home.length());
    return path;
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

BookmarkModel::Bookmark BookmarkModel::makeSplitBookmark(const QString &primary, const QString &secondary,
                                                         const QString &orientation, const QString &name) const
{
    Bookmark bm = makeBookmark(primary);
    bm.secondaryPath = expandPath(secondary);
    bm.orientation = orientation.toLower() == "horizontal" ? "horizontal" : "vertical";
    bm.name += " + " + bookmarkDisplayName(bm.secondaryPath);
    bm.customName = name.trimmed();
    bm.icon = "view-split-left-right";
    return bm;
}

bool BookmarkModel::containsSplit(const QString &primary, const QString &secondary,
                                  const QString &orientation) const
{
    const QString first = expandPath(primary);
    const QString second = expandPath(secondary);
    const QString orient = orientation.toLower() == "horizontal" ? "horizontal" : "vertical";
    for (const auto &existing : m_bookmarks) {
        if (existing.path == first && existing.secondaryPath == second && existing.orientation == orient)
            return true;
    }
    return false;
}

bool BookmarkModel::addSplitBookmark(const QString &primary, const QString &secondary,
                                     const QString &orientation, const QString &name, int index)
{
    if (primary.isEmpty() || secondary.isEmpty())
        return false;
    const Bookmark bm = makeSplitBookmark(primary, secondary, orientation, name);
    if (bm.path == bm.secondaryPath || containsSplit(primary, secondary, orientation))
        return false;
    const int row = index < 0 || index > m_bookmarks.size() ? m_bookmarks.size() : index;
    beginInsertRows(QModelIndex(), row, row);
    m_bookmarks.insert(row, bm);
    endInsertRows();
    emit countChanged();
    emit bookmarksChanged();
    return true;
}

QVariantMap BookmarkModel::splitAt(int index) const
{
    if (index < 0 || index >= m_bookmarks.size() || m_bookmarks.at(index).secondaryPath.isEmpty())
        return {};
    const auto &bm = m_bookmarks.at(index);
    return {{"name", bm.customName.isEmpty() ? bm.name : bm.customName},
            {"primaryPath", bm.path}, {"secondaryPath", bm.secondaryPath},
            {"orientation", bm.orientation}};
}

QVariantList BookmarkModel::savedSplits() const
{
    QVariantList result;
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        auto split = splitAt(i);
        if (split.isEmpty())
            continue;
        split["primaryPath"] = portablePath(m_bookmarks.at(i).path);
        split["secondaryPath"] = portablePath(m_bookmarks.at(i).secondaryPath);
        split["name"] = m_bookmarks.at(i).customName;
        split["position"] = i;
        result.append(split);
    }
    return result;
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

void BookmarkModel::renameBookmark(int index, const QString &name)
{
    if (index < 0 || index >= m_bookmarks.size())
        return;

    const QString trimmed = name.trimmed();
    Bookmark &bm = m_bookmarks[index];
    if (bm.customName == trimmed)
        return;
    bm.customName = trimmed;
    const QModelIndex idx = this->index(index);
    emit dataChanged(idx, idx, {NameRole});
    emit bookmarksChanged();
}

bool BookmarkModel::containsPath(const QString &path) const
{
    QString expanded = expandPath(path);
    for (const auto &bm : m_bookmarks) {
        if (bm.secondaryPath.isEmpty() && bm.path == expanded)
            return true;
    }
    return false;
}

BookmarkModel::Bookmark BookmarkModel::makeBookmark(const QString &rawPath) const
{
    QString expanded = expandPath(rawPath);
    QString name = bookmarkDisplayName(expanded);
    return {name, expanded, iconForPath(expanded, name.toLower())};
}

QString BookmarkModel::expandPath(const QString &path)
{
    const QUrl url(path);
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());
    if (url.isValid() && !url.scheme().isEmpty() && url.scheme() != QStringLiteral("file"))
        return url.toString(QUrl::FullyEncoded);
    if (path == "~")
        return QDir::homePath();
    if (path.startsWith("~/"))
        return QDir::cleanPath(QDir::homePath() + path.mid(1));
    return path.isEmpty() ? path : QDir::cleanPath(path);
}

QString BookmarkModel::iconForPath(const QString &path, const QString &name)
{
    // Match the XDG user dirs first: on a localised system the folder is named
    // "Imagens" or "Documentos", which the English name table below misses.
    static const QVector<QPair<QStandardPaths::StandardLocation, QString>> xdgIcons = {
        {QStandardPaths::HomeLocation, QStringLiteral("go-home")},
        {QStandardPaths::DocumentsLocation, QStringLiteral("folder-documents")},
        {QStandardPaths::DownloadLocation, QStringLiteral("folder-download")},
        {QStandardPaths::PicturesLocation, QStringLiteral("folder-pictures")},
        {QStandardPaths::MusicLocation, QStringLiteral("folder-music")},
        {QStandardPaths::MoviesLocation, QStringLiteral("folder-videos")},
        {QStandardPaths::DesktopLocation, QStringLiteral("user-desktop")},
    };

    if (!path.isEmpty()) {
        const QString cleaned = QDir::cleanPath(path);
        for (const auto &entry : xdgIcons) {
            const QString resolved = QStandardPaths::writableLocation(entry.first);
            if (!resolved.isEmpty() && QDir::cleanPath(resolved) == cleaned)
                return entry.second;
        }
    }

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

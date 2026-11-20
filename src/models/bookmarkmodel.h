#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantMap>

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IconRole,
        SplitRole,
    };

    explicit BookmarkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // names: custom display names keyed by the portable path form paths() emits.
    void setBookmarks(const QStringList &paths, const QVariantMap &names = {},
                      const QVariantList &splits = {});
    QStringList paths() const;
    QVariantMap names() const;
    QVariantList savedSplits() const;

    // Favourites are folders: a local path that exists but is not a directory
    // (a dropped file) is refused. Remote URIs and paths that are not there now
    // (an unplugged drive) cannot be checked and are kept. Both return whether
    // a bookmark was added; a path already in the list is not added twice.
    Q_INVOKABLE bool addBookmark(const QString &path);
    // Inserts at index (-1 appends). Returns false for a duplicate or for two
    // panes on the same folder.
    Q_INVOKABLE bool addSplitBookmark(const QString &primaryPath, const QString &secondaryPath,
                                      const QString &orientation, const QString &name = {},
                                      int index = -1);
    Q_INVOKABLE bool containsSplit(const QString &primaryPath, const QString &secondaryPath,
                                   const QString &orientation) const;
    Q_INVOKABLE QVariantMap splitAt(int index) const;
    Q_INVOKABLE bool insertBookmark(const QString &path, int index);
    Q_INVOKABLE void removeBookmark(int index);
    Q_INVOKABLE void moveBookmark(int from, int to);
    // Empty or whitespace-only name reverts to the automatic one.
    Q_INVOKABLE void renameBookmark(int index, const QString &name);
    Q_INVOKABLE bool containsPath(const QString &path) const;

signals:
    void countChanged();
    void bookmarksChanged();

private:
    struct Bookmark {
        QString name;        // automatic name derived from the path
        QString path;
        QString icon;
        QString customName;  // user override, empty = use name
        QString secondaryPath; // nonempty for a split bookmark
        QString orientation;
    };

    QList<Bookmark> m_bookmarks;
    static QString expandPath(const QString &path);
    static bool isFolderPath(const QString &expanded);
    static QString portablePath(const QString &path);
    static QString iconForPath(const QString &path, const QString &name);
    Bookmark makeBookmark(const QString &path) const;
    Bookmark makeSplitBookmark(const QString &primary, const QString &secondary,
                                const QString &orientation, const QString &name) const;
};

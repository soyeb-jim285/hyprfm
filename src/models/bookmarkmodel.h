#pragma once

#include <QAbstractListModel>
#include <QStringList>

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IconRole,
    };

    explicit BookmarkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBookmarks(const QStringList &paths);
    QStringList paths() const;

    Q_INVOKABLE void addBookmark(const QString &path);
    Q_INVOKABLE void insertBookmark(const QString &path, int index);
    Q_INVOKABLE void removeBookmark(int index);
    Q_INVOKABLE void moveBookmark(int from, int to);
    Q_INVOKABLE bool containsPath(const QString &path) const;

signals:
    void countChanged();
    void bookmarksChanged();

private:
    struct Bookmark {
        QString name;
        QString path;
        QString icon;
    };

    QList<Bookmark> m_bookmarks;
    static QString expandPath(const QString &path);
    static QString iconForPath(const QString &name);
    Bookmark makeBookmark(const QString &path) const;
};

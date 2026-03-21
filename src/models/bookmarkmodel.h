#pragma once

#include <QAbstractListModel>
#include <QStringList>

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT

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

private:
    struct Bookmark {
        QString name;
        QString path;
        QString icon;
    };

    QList<Bookmark> m_bookmarks;
    static QString expandPath(const QString &path);
    static QString iconForPath(const QString &name);
};

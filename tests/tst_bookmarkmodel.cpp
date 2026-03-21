#include <QTest>
#include "models/bookmarkmodel.h"

class TestBookmarkModel : public QObject
{
    Q_OBJECT

private slots:
    void testLoadBookmarks()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads"});
        QCOMPARE(model.rowCount(), 2);
    }

    void testBookmarkData()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents"});
        QModelIndex idx = model.index(0);
        QString name = model.data(idx, BookmarkModel::NameRole).toString();
        QCOMPARE(name, QString("Documents"));
    }

    void testExpandTilde()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents"});
        QModelIndex idx = model.index(0);
        QString path = model.data(idx, BookmarkModel::PathRole).toString();
        QVERIFY(path.startsWith("/"));
        QVERIFY(path.endsWith("/Documents"));
    }

    void testIconForKnownPaths()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads", "~/Pictures", "~/Music"});
        QModelIndex idx0 = model.index(0);
        QString icon = model.data(idx0, BookmarkModel::IconRole).toString();
        QVERIFY(!icon.isEmpty());
    }
};

QTEST_MAIN(TestBookmarkModel)
#include "tst_bookmarkmodel.moc"

#include <QTest>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include "models/bookmarkmodel.h"

class TestBookmarkModel : public QObject
{
    Q_OBJECT

private slots:
    void testModelConsistency()
    {
        BookmarkModel model;
        auto *tester = new QAbstractItemModelTester(&model,
            QAbstractItemModelTester::FailureReportingMode::QtTest);
        Q_UNUSED(tester)

        model.setBookmarks({"~/Documents", "~/Downloads", "~/Pictures"});
        model.setBookmarks({"~/Music"});
        model.setBookmarks({});
    }

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
        QVERIFY(!path.contains("~"));
    }

    void testIconForKnownPaths_data()
    {
        QTest::addColumn<QString>("bookmark");
        QTest::addColumn<bool>("hasIcon");

        QTest::newRow("Documents") << "~/Documents" << true;
        QTest::newRow("Downloads") << "~/Downloads" << true;
        QTest::newRow("Pictures") << "~/Pictures" << true;
        QTest::newRow("Music") << "~/Music" << true;
        QTest::newRow("Videos") << "~/Videos" << true;
        QTest::newRow("Unknown") << "~/RandomDir" << true; // should have fallback icon
    }

    void testIconForKnownPaths()
    {
        QFETCH(QString, bookmark);
        QFETCH(bool, hasIcon);

        BookmarkModel model;
        model.setBookmarks({bookmark});
        QModelIndex idx = model.index(0);
        QString icon = model.data(idx, BookmarkModel::IconRole).toString();
        QCOMPARE(!icon.isEmpty(), hasIcon);
    }

    void testEmptyBookmarkList()
    {
        BookmarkModel model;
        model.setBookmarks({});
        QCOMPARE(model.rowCount(), 0);
    }

    void testReplaceBookmarks()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads"});
        QCOMPARE(model.rowCount(), 2);

        model.setBookmarks({"~/Music"});
        QCOMPARE(model.rowCount(), 1);

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, BookmarkModel::NameRole).toString(), QString("Music"));
    }

    void testRoleNames()
    {
        BookmarkModel model;
        auto roles = model.roleNames();
        QCOMPARE(roles[BookmarkModel::NameRole], QByteArray("name"));
        QCOMPARE(roles[BookmarkModel::PathRole], QByteArray("path"));
        QCOMPARE(roles[BookmarkModel::IconRole], QByteArray("icon"));
    }

    void testInvalidIndex()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents"});
        QModelIndex bad = model.index(999);
        QVERIFY(!model.data(bad, BookmarkModel::NameRole).isValid());
    }

    void testAbsolutePath()
    {
        BookmarkModel model;
        model.setBookmarks({"/tmp"});
        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, BookmarkModel::PathRole).toString(), QString("/tmp"));
        QCOMPARE(model.data(idx, BookmarkModel::NameRole).toString(), QString("tmp"));
    }

    void testRowsResetSignal()
    {
        BookmarkModel model;
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

        model.setBookmarks({"~/Documents"});
        QVERIFY(resetSpy.count() >= 1);
    }
};

QTEST_MAIN(TestBookmarkModel)
#include "tst_bookmarkmodel.moc"

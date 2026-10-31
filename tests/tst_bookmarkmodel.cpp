#include <QTest>
#include <QDir>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include "models/bookmarkmodel.h"

class TestBookmarkModel : public QObject
{
    Q_OBJECT

private slots:
    void testSplitBookmarkRoundTripAndReorder()
    {
        BookmarkModel model;
        QAbstractItemModelTester modelTester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setBookmarks({"~/Documents", "~/Downloads"});
        model.addSplitBookmark("~/Code", "~/Flowchart", "horizontal");
        model.addSplitBookmark(QDir::homePath() + "/Code/", "~/Flowchart/", "horizontal");
        QCOMPARE(model.rowCount(), 3); // duplicate is ignored
        QCOMPARE(model.paths(), QStringList({"~/Documents", "~/Downloads"}));
        QVERIFY(!model.containsPath("~/Code"));
        QVERIFY(model.data(model.index(2), BookmarkModel::PathRole).toString().isEmpty());
        model.renameBookmark(2, "Code and diagrams");
        model.moveBookmark(2, 1);

        BookmarkModel restored;
        restored.setBookmarks(model.paths(), model.names(), model.savedSplits());
        QCOMPARE(restored.rowCount(), 3);
        QCOMPARE(restored.splitAt(1).value("name").toString(), QString("Code and diagrams"));
        QCOMPARE(restored.splitAt(1).value("primaryPath").toString(), QDir::homePath() + "/Code");
        QCOMPARE(restored.splitAt(1).value("secondaryPath").toString(), QDir::homePath() + "/Flowchart");
        QCOMPARE(restored.splitAt(1).value("orientation").toString(), QString("horizontal"));
        QCOMPARE(restored.data(restored.index(2), BookmarkModel::NameRole).toString(), QString("Downloads"));
        restored.removeBookmark(1);
        QCOMPARE(restored.rowCount(), 2);
        QVERIFY(restored.savedSplits().isEmpty());
    }

    // containsSplit matches what addSplitBookmark stored, whatever form the
    // paths were given in; a split of one folder twice is not saved; an index
    // inserts in place.
    void testSplitLookupAndInsert()
    {
        BookmarkModel model;
        model.addBookmark("~/Downloads");
        QVERIFY(model.addSplitBookmark("sftp://host/My Docs", "~/Code", "vertical", {}, 0));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.splitAt(0).value("secondaryPath").toString(), QDir::homePath() + "/Code");
        QVERIFY(model.containsSplit("sftp://host/My Docs", QDir::homePath() + "/Code/", "vertical"));
        QVERIFY(!model.containsSplit("sftp://host/My Docs", "~/Code", "horizontal"));
        QVERIFY(!model.addSplitBookmark("sftp://host/My Docs", "~/Code", "vertical"));
        QVERIFY(!model.addSplitBookmark("~/Code", QDir::homePath() + "/Code/", "vertical"));
        QCOMPARE(model.rowCount(), 2);
    }

    void testLegacySavedSplitsBecomeBookmarks()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents"}, {}, {
            QVariantMap{{"name", "Code - Flowchart"}, {"primaryPath", "~/Code"},
                        {"secondaryPath", "~/Flowchart"}, {"orientation", "horizontal"}},
            QVariantMap{{"primaryPath", "/tmp"}, {"secondaryPath", "/var"}}
        });
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.splitAt(1).value("name").toString(), QString("Code - Flowchart"));
        QCOMPARE(model.splitAt(2).value("orientation").toString(), QString("vertical"));
        model.renameBookmark(1, "");
        QCOMPARE(model.splitAt(1).value("name").toString(), QString("Code + Flowchart"));
        QVERIFY(model.splitAt(0).isEmpty());
    }

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

    void testRemoteBookmarkName()
    {
        BookmarkModel model;
        model.setBookmarks({"sftp://example.com/home/jim"});
        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, BookmarkModel::PathRole).toString(), QString("sftp://example.com/home/jim"));
        QCOMPARE(model.data(idx, BookmarkModel::NameRole).toString(), QString("jim"));
    }

    void testRenameBookmarkOverridesName()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads"});
        QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy changedSpy(&model, &BookmarkModel::bookmarksChanged);

        model.renameBookmark(1, "  Stuff  ");

        QCOMPARE(model.data(model.index(1), BookmarkModel::NameRole).toString(), QString("Stuff"));
        QCOMPARE(model.data(model.index(0), BookmarkModel::NameRole).toString(), QString("Documents"));
        QCOMPARE(dataSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
        // Stored names are keyed by the portable path form paths() emits.
        QCOMPARE(model.names(), QVariantMap({{"~/Downloads", "Stuff"}}));
    }

    void testRenameBookmarkEmptyRevertsToAutoName()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Downloads"});
        model.renameBookmark(0, "Stuff");
        model.renameBookmark(0, "   ");
        QCOMPARE(model.data(model.index(0), BookmarkModel::NameRole).toString(), QString("Downloads"));
        QVERIFY(model.names().isEmpty());
    }

    void testRenameBookmarkInvalidIndexIgnored()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Downloads"});
        QSignalSpy changedSpy(&model, &BookmarkModel::bookmarksChanged);
        model.renameBookmark(-1, "x");
        model.renameBookmark(1, "x");
        QCOMPARE(changedSpy.count(), 0);
    }

    void testSetBookmarksAppliesNames()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads"}, QVariantMap{{"~/Downloads", "Stuff"}});
        QCOMPARE(model.data(model.index(1), BookmarkModel::NameRole).toString(), QString("Stuff"));
        QCOMPARE(model.data(model.index(1), BookmarkModel::IconRole).toString(), QString("folder-download"));

        // Same paths, name dropped: must not be treated as "unchanged".
        model.setBookmarks({"~/Documents", "~/Downloads"});
        QCOMPARE(model.data(model.index(1), BookmarkModel::NameRole).toString(), QString("Downloads"));
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

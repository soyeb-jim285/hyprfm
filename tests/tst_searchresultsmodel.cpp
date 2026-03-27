// tests/tst_searchresultsmodel.cpp
#include <QTest>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include <QFileInfo>
#include <QStandardPaths>
#include "models/searchresultsmodel.h"
#include "testdir.h"

class TestSearchResultsModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testRoleNamesMatchFileSystemModel()
    {
        SearchResultsModel model;
        auto roles = model.roleNames();
        QVERIFY(roles.values().contains("fileName"));
        QVERIFY(roles.values().contains("filePath"));
        QVERIFY(roles.values().contains("fileSize"));
        QVERIFY(roles.values().contains("fileSizeText"));
        QVERIFY(roles.values().contains("fileType"));
        QVERIFY(roles.values().contains("fileModified"));
        QVERIFY(roles.values().contains("fileModifiedText"));
        QVERIFY(roles.values().contains("filePermissions"));
        QVERIFY(roles.values().contains("isDir"));
        QVERIFY(roles.values().contains("isSymlink"));
        QVERIFY(roles.values().contains("fileIconName"));
    }

    void testEmptyModel()
    {
        SearchResultsModel model;
        QCOMPARE(model.rowCount(), 0);
    }

    void testAddResults()
    {
        TestDir dir;
        dir.createFile("hello.txt", "content");
        dir.createFile("world.cpp", "code");

        SearchResultsModel model;
        QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);

        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/hello.txt"));
        batch.append(QFileInfo(dir.path() + "/world.cpp"));
        model.addResults(batch);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(spy.count(), 1);

        QModelIndex idx0 = model.index(0);
        QCOMPARE(model.data(idx0, SearchResultsModel::FileNameRole).toString(), "hello.txt");
        QCOMPARE(model.data(idx0, SearchResultsModel::IsDirRole).toBool(), false);

        QModelIndex idx1 = model.index(1);
        QCOMPARE(model.data(idx1, SearchResultsModel::FileNameRole).toString(), "world.cpp");
    }

    void testClear()
    {
        TestDir dir;
        dir.createFile("a.txt");

        SearchResultsModel model;
        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/a.txt"));
        model.addResults(batch);
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
        model.clear();
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(spy.count(), 1);
    }

    void testFilePathAccessor()
    {
        TestDir dir;
        dir.createFile("test.txt");

        SearchResultsModel model;
        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/test.txt"));
        model.addResults(batch);

        QCOMPARE(model.filePath(0), dir.path() + "/test.txt");
        QCOMPARE(model.filePath(99), QString());
    }

    void testIsDirAccessor()
    {
        TestDir dir;
        dir.createFile("file.txt");
        dir.createDir("subdir");

        SearchResultsModel model;
        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/file.txt"));
        batch.append(QFileInfo(dir.path() + "/subdir"));
        model.addResults(batch);

        QCOMPARE(model.isDir(0), false);
        QCOMPARE(model.isDir(1), true);
    }

    void testFileNameAccessor()
    {
        TestDir dir;
        dir.createFile("readme.md");

        SearchResultsModel model;
        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/readme.md"));
        model.addResults(batch);

        QCOMPARE(model.fileName(0), "readme.md");
        QCOMPARE(model.fileName(-1), QString());
    }

    void testBatchInsert()
    {
        TestDir dir;
        dir.createFile("a.txt");
        dir.createFile("b.txt");
        dir.createFile("c.txt");

        SearchResultsModel model;
        QSignalSpy spy(&model, &QAbstractItemModel::rowsInserted);

        QList<QFileInfo> batch1;
        batch1.append(QFileInfo(dir.path() + "/a.txt"));
        model.addResults(batch1);

        QList<QFileInfo> batch2;
        batch2.append(QFileInfo(dir.path() + "/b.txt"));
        batch2.append(QFileInfo(dir.path() + "/c.txt"));
        model.addResults(batch2);

        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(spy.count(), 2);
    }

    void testDataRoles()
    {
        TestDir dir;
        dir.createFile("data.txt", "hello world");

        SearchResultsModel model;
        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/data.txt"));
        model.addResults(batch);

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, SearchResultsModel::FileNameRole).toString(), "data.txt");
        QVERIFY(model.data(idx, SearchResultsModel::FilePathRole).toString().endsWith("/data.txt"));
        QCOMPARE(model.data(idx, SearchResultsModel::FileSizeRole).toLongLong(), 11);
        QVERIFY(!model.data(idx, SearchResultsModel::FileSizeTextRole).toString().isEmpty());
        QCOMPARE(model.data(idx, SearchResultsModel::IsDirRole).toBool(), false);
        QCOMPARE(model.data(idx, SearchResultsModel::IsSymlinkRole).toBool(), false);
        QVERIFY(model.data(idx, SearchResultsModel::FileModifiedRole).toDateTime().isValid());
        QVERIFY(!model.data(idx, SearchResultsModel::FileModifiedTextRole).toString().isEmpty());
    }

    void testModelTester()
    {
        TestDir dir;
        dir.createFile("x.txt");

        SearchResultsModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        QList<QFileInfo> batch;
        batch.append(QFileInfo(dir.path() + "/x.txt"));
        model.addResults(batch);
        model.clear();
    }
};

QTEST_MAIN(TestSearchResultsModel)
#include "tst_searchresultsmodel.moc"

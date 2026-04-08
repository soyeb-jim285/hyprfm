#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include "services/searchservice.h"
#include "models/searchresultsmodel.h"
#include "testdir.h"

class TestSearchService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testFdDetection()
    {
        SearchService service;
        bool hasFd = !QStandardPaths::findExecutable("fd").isEmpty()
                  || !QStandardPaths::findExecutable("fdfind").isEmpty();
        QCOMPARE(service.hasFd(), hasFd);
    }

    void testSearchPopulatesModel()
    {
        TestDir dir;
        dir.createFile("sub/deep/hello.txt");
        dir.createFile("sub/world.cpp");
        dir.createFile("top.txt");

        SearchResultsModel model;
        SearchService service;
        service.setResultsModel(&model);

        QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
        service.startSearch(dir.path(), "txt", false);

        QVERIFY(finishedSpy.wait(5000));
        QVERIFY(model.rowCount() >= 2);
    }

    void testSearchClearsOldResults()
    {
        TestDir dir;
        // Use distinctive base names rather than single letters. The
        // search backend (fd) matches against the full path including the
        // random "tst_searchservice-XXXXXX" temp dir suffix, so a query
        // like "b" can spuriously match every file when the random hex
        // happens to contain a 'b'.
        dir.createFile("zebra.txt");
        dir.createFile("kangaroo.cpp");

        SearchResultsModel model;
        SearchService service;
        service.setResultsModel(&model);

        QSignalSpy spy1(&service, &SearchService::searchFinished);
        service.startSearch(dir.path(), "zebra", false);
        QVERIFY(spy1.wait(5000));
        QVERIFY(model.rowCount() >= 1);

        QSignalSpy spy2(&service, &SearchService::searchFinished);
        service.startSearch(dir.path(), "kangaroo", false);
        QVERIFY(spy2.wait(5000));
        for (int i = 0; i < model.rowCount(); i++) {
            QVERIFY(model.fileName(i).contains("kangaroo", Qt::CaseInsensitive));
        }
    }

    void testCancelSearch()
    {
        TestDir dir;
        for (int i = 0; i < 100; i++)
            dir.createFile(QString("file%1.txt").arg(i));

        SearchResultsModel model;
        SearchService service;
        service.setResultsModel(&model);

        service.startSearch(dir.path(), "file", false);
        service.cancelSearch();

        QCOMPARE(service.isSearching(), false);
    }

    void testResultCap()
    {
        SearchService service;
        QCOMPARE(service.maxResults(), 10000);
        service.setMaxResults(5);
        QCOMPARE(service.maxResults(), 5);
    }

    void testSearchGlobPattern()
    {
        TestDir dir;
        dir.createFile("main.cpp");
        dir.createFile("test.cpp");
        dir.createFile("readme.md");

        SearchResultsModel model;
        SearchService service;
        service.setResultsModel(&model);

        QSignalSpy spy(&service, &SearchService::searchFinished);
        service.startSearch(dir.path(), "*.cpp", false);
        QVERIFY(spy.wait(5000));
        QVERIFY(model.rowCount() >= 2);
    }

    void testSearchEmitsProgress()
    {
        TestDir dir;
        dir.createFile("a.txt");

        SearchResultsModel model;
        SearchService service;
        service.setResultsModel(&model);

        QSignalSpy progressSpy(&service, &SearchService::resultCountChanged);
        QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
        service.startSearch(dir.path(), "a", false);
        QVERIFY(finishedSpy.wait(5000));
        QVERIFY(progressSpy.count() >= 1);
    }

    void testSearchMatchesRelativePath()
    {
        TestDir dir;
        dir.createFile("iasa/nested/file.txt");
        dir.createFile("other.txt");

        SearchResultsModel model;
        SearchService service;
        service.setResultsModel(&model);

        QSignalSpy finishedSpy(&service, &SearchService::searchFinished);
        service.startSearch(dir.path(), "iasa", false);
        QVERIFY(finishedSpy.wait(5000));

        QStringList paths;
        for (int i = 0; i < model.rowCount(); ++i)
            paths << model.filePath(i);

        QVERIFY(paths.contains(dir.path() + "/iasa"));
        QVERIFY(paths.contains(dir.path() + "/iasa/nested"));
        QVERIFY(paths.contains(dir.path() + "/iasa/nested/file.txt"));
    }
};

QTEST_MAIN(TestSearchService)
#include "tst_searchservice.moc"

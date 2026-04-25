#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include "models/searchproxymodel.h"
#include "models/searchresultsmodel.h"
#include "models/filesystemmodel.h"
#include "testdir.h"

class TestSearchProxyModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testSubstringFilter()
    {
        TestDir dir;
        dir.createFile("hello.txt");
        dir.createFile("world.cpp");
        dir.createFile("hello_world.h");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        QCOMPARE(proxy.rowCount(), 3);

        proxy.setSearchQuery("hello");
        QCOMPARE(proxy.rowCount(), 2);
    }

    void testCaseInsensitive()
    {
        TestDir dir;
        dir.createFile("README.md");
        dir.createFile("readme.txt");
        dir.createFile("other.cpp");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSearchQuery("readme");
        QCOMPARE(proxy.rowCount(), 2);
    }

    void testGlobDetection()
    {
        SearchProxyModel proxy;
        proxy.setSearchQuery("hello");
        QCOMPARE(proxy.isGlobPattern(), false);

        proxy.setSearchQuery("*.cpp");
        QCOMPARE(proxy.isGlobPattern(), true);

        proxy.setSearchQuery("file?.txt");
        QCOMPARE(proxy.isGlobPattern(), true);

        proxy.setSearchQuery("[abc].txt");
        QCOMPARE(proxy.isGlobPattern(), true);

        proxy.setSearchQuery("^start");
        QCOMPARE(proxy.isGlobPattern(), true);

        proxy.setSearchQuery("end$");
        QCOMPARE(proxy.isGlobPattern(), true);
    }

    void testGlobFilter()
    {
        TestDir dir;
        dir.createFile("main.cpp");
        dir.createFile("util.cpp");
        dir.createFile("readme.md");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSearchQuery("*.cpp");
        QCOMPARE(proxy.rowCount(), 2);
    }

    void testTypeFilterFolders()
    {
        TestDir dir;
        dir.createFile("file.txt");
        dir.createDir("subdir");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setFileTypeFilter("folders");
        QCOMPARE(proxy.rowCount(), 1);
    }

    void testSizeFilter()
    {
        TestDir dir;
        dir.createFile("tiny.txt", "hi");
        dir.createFile("medium.txt", QByteArray(500000, 'x'));

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSizeFilter("small");
        QCOMPARE(proxy.rowCount(), 2);

        proxy.setSizeFilter("tiny");
        QCOMPARE(proxy.rowCount(), 1);
    }

    void testDateFilter()
    {
        TestDir dir;
        dir.createFile("old.txt", "old", QDateTime(QDate(2020, 1, 1), QTime(0, 0)));
        dir.createFile("new.txt", "new");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setDateFilter("today");
        QVERIFY(proxy.rowCount() >= 1);
    }

    void testCombinedFilters()
    {
        TestDir dir;
        dir.createFile("code.cpp", "int main(){}");
        dir.createFile("code.h", "class Foo{};");
        dir.createFile("readme.md", "# Hello");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSearchQuery("code");
        proxy.setFileTypeFilter("code");
        QCOMPARE(proxy.rowCount(), 2);
    }

    void testEmptyQueryWithTypeFilter()
    {
        TestDir dir;
        dir.createFile("a.cpp");
        dir.createFile("b.txt");
        dir.createDir("sub");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setFileTypeFilter("folders");
        QCOMPARE(proxy.rowCount(), 1);
    }

    void testClearSearch()
    {
        TestDir dir;
        dir.createFile("a.txt");
        dir.createFile("b.cpp");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSearchQuery("a");
        QCOMPARE(proxy.rowCount(), 1);

        proxy.clearSearch();
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(proxy.searchQuery(), QString());
        QCOMPARE(proxy.fileTypeFilter(), QString());
        QCOMPARE(proxy.dateFilter(), QString());
        QCOMPARE(proxy.sizeFilter(), QString());
    }

    void testFilePathAccessor()
    {
        TestDir dir;
        dir.createFile("alpha.txt");
        dir.createFile("beta.txt");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSearchQuery("alpha");
        QCOMPARE(proxy.rowCount(), 1);
        QVERIFY(proxy.filePath(0).endsWith("/alpha.txt"));
    }

    void testIsDirAccessor()
    {
        TestDir dir;
        dir.createDir("mydir");
        dir.createFile("myfile.txt");

        FileSystemModel source;
        source.setSynchronousReload(true);
        source.setRootPath(dir.path());

        SearchProxyModel proxy;
        proxy.setSourceModel(&source);
        proxy.setSearchQuery("my");
        QCOMPARE(proxy.rowCount(), 2);

        // Directories sort first by default
        QCOMPARE(proxy.isDir(0), true);
        QCOMPARE(proxy.isDir(1), false);
    }

    void testSearchActive()
    {
        SearchProxyModel proxy;
        QCOMPARE(proxy.searchActive(), false);

        proxy.setSearchQuery("test");
        QCOMPARE(proxy.searchActive(), true);

        proxy.clearSearch();
        QCOMPARE(proxy.searchActive(), false);

        proxy.setFileTypeFilter("code");
        QCOMPARE(proxy.searchActive(), true);
    }
};

QTEST_MAIN(TestSearchProxyModel)
#include "tst_searchproxymodel.moc"

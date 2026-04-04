#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "services/diskusageservice.h"

class TestDiskUsageService : public QObject
{
    Q_OBJECT

private slots:
    void testRequestSizeForNestedFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QFile rootFile(dir.filePath("root.txt"));
        QVERIFY(rootFile.open(QIODevice::WriteOnly));
        rootFile.write("1234");
        rootFile.close();

        QVERIFY(QDir(dir.path()).mkpath("nested/deeper"));

        QFile nestedFile(dir.filePath("nested/child.bin"));
        QVERIFY(nestedFile.open(QIODevice::WriteOnly));
        nestedFile.write("567");
        nestedFile.close();

        QFile deepFile(dir.filePath("nested/deeper/end.dat"));
        QVERIFY(deepFile.open(QIODevice::WriteOnly));
        deepFile.write("89");
        deepFile.close();

        DiskUsageService service;
        QSignalSpy spy(&service, &DiskUsageService::requestFinished);

        const int requestId = service.requestSize({dir.path()});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), qint64(9));
        QCOMPARE(result.value("sizeText").toString(), QString("9 B"));
        QCOMPARE(result.value("sizeTextVerbose").toString(), QString("9 B (9 bytes)"));
    }

    void testRequestSizeAggregatesPaths()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QFile fileA(dir.filePath("a.txt"));
        QVERIFY(fileA.open(QIODevice::WriteOnly));
        fileA.write("abcd");
        fileA.close();

        QVERIFY(QDir(dir.path()).mkpath("folder"));
        QFile fileB(dir.filePath("folder/b.txt"));
        QVERIFY(fileB.open(QIODevice::WriteOnly));
        fileB.write("efghij");
        fileB.close();

        DiskUsageService service;
        QSignalSpy spy(&service, &DiskUsageService::requestFinished);

        const int requestId = service.requestSize({dir.filePath("a.txt"), dir.filePath("folder")});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), qint64(10));
        QCOMPARE(result.value("sizeText").toString(), QString("10 B"));
    }

    void testRequestSizeForDirectorySymlinkUsesTarget()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QVERIFY(QDir(dir.path()).mkpath("real/bin"));

        QFile targetFile(dir.filePath("real/bin/tool"));
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        targetFile.write("1234567890");
        targetFile.close();

        const QString linkPath = dir.filePath("bin-link");
        if (!QFile::link(dir.filePath("real/bin"), linkPath))
            QSKIP("Directory symlinks are unavailable in this environment");

        DiskUsageService service;
        QSignalSpy spy(&service, &DiskUsageService::requestFinished);

        const int requestId = service.requestSize({linkPath});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), qint64(10));
        QCOMPARE(result.value("sizeText").toString(), QString("10 B"));
    }
};

QTEST_MAIN(TestDiskUsageService)
#include "tst_diskusageservice.moc"

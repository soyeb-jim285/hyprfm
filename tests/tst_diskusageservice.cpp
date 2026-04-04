#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

#include "services/diskusageservice.h"

namespace {

qint64 duApparentSize(const QStringList &paths)
{
    QProcess proc;
    QStringList args = {QStringLiteral("--apparent-size"), QStringLiteral("-sb")};
    args.append(paths);
    proc.start(QStringLiteral("du"), args);
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return -1;

    qint64 total = 0;
    const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines)
        total += line.section('\t', 0, 0).toLongLong();
    return total;
}

}

class TestDiskUsageService : public QObject
{
    Q_OBJECT

private slots:
    void testRequestSizeForNestedFolder()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

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
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({dir.path()}));
        QCOMPARE(result.value("sizeText").toString(), QString("9 B"));
        QCOMPARE(result.value("sizeTextVerbose").toString(), QString("9 B (9 bytes)"));
    }

    void testRequestSizeAggregatesPaths()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

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
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({dir.filePath("a.txt"), dir.filePath("folder")}));
        QCOMPARE(result.value("sizeText").toString(), QString("10 B"));
    }

    void testRequestSizeForDirectorySymlinkUsesLinkSize()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

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
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({linkPath}));
    }

    void testRequestSizeForFileSymlinkUsesLinkSize()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QFile targetFile(dir.filePath("target.txt"));
        QVERIFY(targetFile.open(QIODevice::WriteOnly));
        targetFile.write("1234567890");
        targetFile.close();

        const QString linkPath = dir.filePath("target-link");
        if (!QFile::link(targetFile.fileName(), linkPath))
            QSKIP("File symlinks are unavailable in this environment");

        DiskUsageService service;
        QSignalSpy spy(&service, &DiskUsageService::requestFinished);

        const int requestId = service.requestSize({linkPath});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({linkPath}));
    }

    void testRequestSizeCountsHardlinksOnce()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString originalPath = dir.filePath("original.bin");
        QFile originalFile(originalPath);
        QVERIFY(originalFile.open(QIODevice::WriteOnly));
        originalFile.write("1234567890");
        originalFile.close();

        const QString hardlinkPath = dir.filePath("duplicate.bin");
        if (::link(QFile::encodeName(originalPath).constData(), QFile::encodeName(hardlinkPath).constData()) != 0)
            QSKIP("Hardlinks are unavailable in this environment");

        DiskUsageService service;
        QSignalSpy spy(&service, &DiskUsageService::requestFinished);

        const int requestId = service.requestSize({dir.path()});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({dir.path()}));
    }

    void testRequestSizeAggregatesHardlinkedSelectionsLikeDu()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString originalPath = dir.filePath("original.bin");
        QFile originalFile(originalPath);
        QVERIFY(originalFile.open(QIODevice::WriteOnly));
        originalFile.write("1234567890");
        originalFile.close();

        const QString hardlinkPath = dir.filePath("duplicate.bin");
        if (::link(QFile::encodeName(originalPath).constData(), QFile::encodeName(hardlinkPath).constData()) != 0)
            QSKIP("Hardlinks are unavailable in this environment");

        DiskUsageService service;
        QSignalSpy spy(&service, &DiskUsageService::requestFinished);

        const int requestId = service.requestSize({originalPath, hardlinkPath});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({originalPath, hardlinkPath}));
    }

    void testSinglePathCacheEvictsOldestEntry()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        DiskUsageService service;

        QString firstPath;
        for (int i = 0; i <= DiskUsageService::MaxCachedPaths; ++i) {
            const QString folderPath = dir.filePath(QStringLiteral("folder_%1").arg(i));
            QVERIFY(QDir().mkpath(folderPath));

            QFile file(folderPath + QStringLiteral("/payload.txt"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("a");
            file.close();

            QSignalSpy spy(&service, &DiskUsageService::requestFinished);
            const int requestId = service.requestSize({folderPath});
            QVERIFY(spy.wait(5000));
            QCOMPARE(spy.takeFirst().at(0).toInt(), requestId);

            if (i == 0)
                firstPath = folderPath;
        }

        QFile updatedFile(firstPath + QStringLiteral("/payload.txt"));
        QVERIFY(updatedFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        updatedFile.write("updated");
        updatedFile.close();

        QSignalSpy spy(&service, &DiskUsageService::requestFinished);
        const int requestId = service.requestSize({firstPath});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({firstPath}));
    }

    void testInvalidatePathClearsAncestorCache()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("du")).isEmpty())
            QSKIP("du not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("nested"));

        const QString filePath = dir.filePath("nested/payload.txt");

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("1234");
        file.close();

        DiskUsageService service;

        {
            QSignalSpy spy(&service, &DiskUsageService::requestFinished);
            const int requestId = service.requestSize({dir.path()});
            QVERIFY(spy.wait(5000));
            QCOMPARE(spy.takeFirst().at(0).toInt(), requestId);
        }

        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("1234567890");
        file.close();

        {
            QSignalSpy spy(&service, &DiskUsageService::requestFinished);
            service.requestSize({dir.path()});
            QVERIFY(spy.wait(5000));
            const QVariantMap result = spy.takeFirst().at(1).toMap();
            QCOMPARE(result.value("size").toLongLong(), qint64(4));
        }

        service.invalidatePath(filePath);

        QSignalSpy spy(&service, &DiskUsageService::requestFinished);
        const int requestId = service.requestSize({dir.path()});
        QVERIFY(spy.wait(5000));

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), requestId);

        const QVariantMap result = args.at(1).toMap();
        QCOMPARE(result.value("size").toLongLong(), duApparentSize({dir.path()}));
    }
};

QTEST_MAIN(TestDiskUsageService)
#include "tst_diskusageservice.moc"

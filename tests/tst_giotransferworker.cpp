#include <QTest>
#include <QSignalSpy>
#include <QThread>
#include "testdir.h"
#include "services/giotransferworker.h"

class TestGioTransferWorker : public QObject
{
    Q_OBJECT

private slots:
    void testCopySingleFile()
    {
        TestDir src, dst;
        src.createFile("test.bin", QByteArray(4096, 'x'));

        GioTransferWorker worker;
        QList<GioTransferWorker::TransferItem> items;
        items.append({src.path() + "/test.bin", dst.path() + "/test.bin", {}, false});

        QSignalSpy finishSpy(&worker, &GioTransferWorker::finished);

        QThread thread;
        worker.moveToThread(&thread);
        connect(&thread, &QThread::started, &worker, [&]() {
            worker.execute(items, false);
        });
        connect(&worker, &GioTransferWorker::finished, &thread, &QThread::quit);
        thread.start();

        QVERIFY(finishSpy.wait(10000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
        QVERIFY(QFile::exists(dst.path() + "/test.bin"));
        QCOMPARE(QFileInfo(dst.path() + "/test.bin").size(), 4096);
        thread.wait();
    }

    void testCopyDirectory()
    {
        TestDir src, dst;
        src.createFile("dir/a.txt", QByteArray(1000, 'a'));
        src.createFile("dir/b.txt", QByteArray(2000, 'b'));
        src.createFile("dir/sub/c.txt", QByteArray(3000, 'c'));

        GioTransferWorker worker;
        QList<GioTransferWorker::TransferItem> items;
        items.append({src.path() + "/dir", dst.path() + "/dir", {}, false});

        QSignalSpy finishSpy(&worker, &GioTransferWorker::finished);

        QThread thread;
        worker.moveToThread(&thread);
        connect(&thread, &QThread::started, &worker, [&]() {
            worker.execute(items, false);
        });
        connect(&worker, &GioTransferWorker::finished, &thread, &QThread::quit);
        thread.start();

        QVERIFY(finishSpy.wait(10000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
        QVERIFY(QFile::exists(dst.path() + "/dir/a.txt"));
        QVERIFY(QFile::exists(dst.path() + "/dir/b.txt"));
        QVERIFY(QFile::exists(dst.path() + "/dir/sub/c.txt"));
        QCOMPARE(QFileInfo(dst.path() + "/dir/sub/c.txt").size(), 3000);
        thread.wait();
    }
};

QTEST_GUILESS_MAIN(TestGioTransferWorker)
#include "tst_giotransferworker.moc"

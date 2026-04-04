#include <QTest>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
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

    void testMoveFile()
    {
        TestDir src, dst;
        src.createFile("move_me.txt", QByteArray(2048, 'm'));

        GioTransferWorker worker;
        QList<GioTransferWorker::TransferItem> items;
        items.append({src.path() + "/move_me.txt", dst.path() + "/move_me.txt", {}, false});

        QSignalSpy finishSpy(&worker, &GioTransferWorker::finished);

        QThread thread;
        worker.moveToThread(&thread);
        connect(&thread, &QThread::started, &worker, [&]() {
            worker.execute(items, true);
        });
        connect(&worker, &GioTransferWorker::finished, &thread, &QThread::quit);
        thread.start();

        QVERIFY(finishSpy.wait(10000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
        QVERIFY(QFile::exists(dst.path() + "/move_me.txt"));
        QVERIFY(!QFile::exists(src.path() + "/move_me.txt"));
        thread.wait();
    }

    void testCopySymlink()
    {
        TestDir src, dst;
        src.createFile("real.txt", "hello");
        src.createSymlink(src.path() + "/real.txt", "link.txt");

        GioTransferWorker worker;
        QList<GioTransferWorker::TransferItem> items;
        items.append({src.path() + "/link.txt", dst.path() + "/link.txt", {}, false});

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
        QFileInfo linkInfo(dst.path() + "/link.txt");
        QVERIFY(linkInfo.isSymLink());
        QCOMPARE(linkInfo.symLinkTarget(), src.path() + "/real.txt");
        thread.wait();
    }

    void testCancel()
    {
        TestDir src, dst;
        src.createFile("big.bin", QByteArray(16 * 1024 * 1024, 'x'));

        GioTransferWorker worker;
        QList<GioTransferWorker::TransferItem> items;
        items.append({src.path() + "/big.bin", dst.path() + "/big.bin", {}, false});

        QSignalSpy finishSpy(&worker, &GioTransferWorker::finished);

        QThread thread;
        worker.moveToThread(&thread);
        connect(&thread, &QThread::started, &worker, [&]() {
            worker.execute(items, false);
        });
        connect(&worker, &GioTransferWorker::finished, &thread, &QThread::quit);

        // Cancel after first progress update
        connect(&worker, &GioTransferWorker::progressUpdated, &worker, [&](double, const QString &, const QString &) {
            worker.cancel();
        }, Qt::DirectConnection);

        thread.start();
        QVERIFY(finishSpy.wait(10000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), false);
        thread.wait();
    }

    void testPauseResume()
    {
        TestDir src, dst;
        src.createFile("pause_test.bin", QByteArray(8 * 1024 * 1024, 'p'));

        GioTransferWorker worker;
        QList<GioTransferWorker::TransferItem> items;
        items.append({src.path() + "/pause_test.bin", dst.path() + "/pause_test.bin", {}, false});

        QSignalSpy finishSpy(&worker, &GioTransferWorker::finished);
        bool paused = false;

        QThread thread;
        worker.moveToThread(&thread);
        connect(&thread, &QThread::started, &worker, [&]() {
            worker.execute(items, false);
        });
        connect(&worker, &GioTransferWorker::finished, &thread, &QThread::quit);

        // progressUpdated is queued to the main thread; call pause() directly,
        // then schedule resume() on the main thread via a single-shot timer so
        // it is posted back to the worker thread via an auto-connection.
        connect(&worker, &GioTransferWorker::progressUpdated, this, [&](double, const QString &, const QString &) {
            if (!paused) {
                paused = true;
                worker.pause();
                QTimer::singleShot(100, this, [&]() {
                    worker.resume();
                });
            }
        });

        thread.start();
        QVERIFY(finishSpy.wait(15000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
        QVERIFY(QFile::exists(dst.path() + "/pause_test.bin"));
        QCOMPARE(QFileInfo(dst.path() + "/pause_test.bin").size(), 8 * 1024 * 1024);
        thread.wait();
    }
};

QTEST_GUILESS_MAIN(TestGioTransferWorker)
#include "tst_giotransferworker.moc"

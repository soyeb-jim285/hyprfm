#include <QTest>
#include <QSignalSpy>
#include "services/clipboardmanager.h"

class TestClipboardManager : public QObject
{
    Q_OBJECT

private slots:
    void testInitialState()
    {
        ClipboardManager mgr;
        QCOMPARE(mgr.hasContent(), false);
        QCOMPARE(mgr.isCut(), false);
        QCOMPARE(mgr.paths().isEmpty(), true);
    }

    void testCopy()
    {
        ClipboardManager mgr;
        QSignalSpy spy(&mgr, &ClipboardManager::changed);

        mgr.copy({"/tmp/a.txt", "/tmp/b.txt"});

        QCOMPARE(mgr.hasContent(), true);
        QCOMPARE(mgr.isCut(), false);
        QCOMPARE(mgr.paths().size(), 2);
        QCOMPARE(spy.count(), 1);
    }

    void testCut()
    {
        ClipboardManager mgr;
        QSignalSpy spy(&mgr, &ClipboardManager::changed);

        mgr.cut({"/tmp/a.txt"});

        QCOMPARE(mgr.hasContent(), true);
        QCOMPARE(mgr.isCut(), true);
        QCOMPARE(spy.count(), 1);
    }

    void testClearAfterPaste()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt"});
        QStringList paths = mgr.take();
        QCOMPARE(paths.size(), 1);
        QCOMPARE(paths.at(0), QString("/tmp/a.txt"));
        QCOMPARE(mgr.hasContent(), false);
    }

    void testCopyDoesNotClearOnTake()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/a.txt"});
        QStringList paths = mgr.take();
        QCOMPARE(paths.size(), 1);
        QCOMPARE(mgr.hasContent(), true);
    }

    void testContains()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt", "/tmp/b.txt"});
        QCOMPARE(mgr.contains("/tmp/a.txt"), true);
        QCOMPARE(mgr.contains("/tmp/b.txt"), true);
        QCOMPARE(mgr.contains("/tmp/c.txt"), false);
    }

    void testClear()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/a.txt"});
        QCOMPARE(mgr.hasContent(), true);

        QSignalSpy spy(&mgr, &ClipboardManager::changed);
        mgr.clear();

        QCOMPARE(mgr.hasContent(), false);
        QCOMPARE(spy.count(), 1);
    }

    void testCopyOverwritesCut()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/cut.txt"});
        QCOMPARE(mgr.isCut(), true);

        mgr.copy({"/tmp/copy.txt"});
        QCOMPARE(mgr.isCut(), false);
        QCOMPARE(mgr.paths().size(), 1);
        QCOMPARE(mgr.paths().at(0), QString("/tmp/copy.txt"));
    }

    void testCutOverwritesCopy()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/copy.txt"});
        QCOMPARE(mgr.isCut(), false);

        mgr.cut({"/tmp/cut.txt"});
        QCOMPARE(mgr.isCut(), true);
        QCOMPARE(mgr.paths().at(0), QString("/tmp/cut.txt"));
    }

    void testMultipleTakeCycles()
    {
        ClipboardManager mgr;

        // Cycle 1: cut
        mgr.cut({"/tmp/a.txt"});
        QStringList p1 = mgr.take();
        QCOMPARE(p1.size(), 1);
        QCOMPARE(mgr.hasContent(), false);

        // Cycle 2: copy
        mgr.copy({"/tmp/b.txt"});
        QStringList p2 = mgr.take();
        QCOMPARE(p2.size(), 1);
        QCOMPARE(mgr.hasContent(), true); // copy doesn't clear

        // Cycle 3: take again (still has copy content)
        QStringList p3 = mgr.take();
        QCOMPARE(p3.size(), 1);
    }

    void testContainsAfterClear()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/a.txt"});
        mgr.clear();
        QCOMPARE(mgr.contains("/tmp/a.txt"), false);
    }

    void testEmptyCopy()
    {
        ClipboardManager mgr;
        mgr.copy({});
        QCOMPARE(mgr.hasContent(), false);
    }

    void testSignalCountOnMultipleOps()
    {
        ClipboardManager mgr;
        QSignalSpy spy(&mgr, &ClipboardManager::changed);

        mgr.copy({"/a"});    // 1
        mgr.cut({"/b"});     // 2
        mgr.clear();         // 3

        QCOMPARE(spy.count(), 3);
    }
};

QTEST_MAIN(TestClipboardManager)
#include "tst_clipboardmanager.moc"

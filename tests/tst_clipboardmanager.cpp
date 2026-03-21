#include <QTest>
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
    }

    void testCopy()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/a.txt", "/tmp/b.txt"});
        QCOMPARE(mgr.hasContent(), true);
        QCOMPARE(mgr.isCut(), false);
        QCOMPARE(mgr.paths().size(), 2);
    }

    void testCut()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt"});
        QCOMPARE(mgr.hasContent(), true);
        QCOMPARE(mgr.isCut(), true);
    }

    void testClearAfterPaste()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt"});
        QStringList paths = mgr.take();
        QCOMPARE(paths.size(), 1);
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
        QCOMPARE(mgr.contains("/tmp/c.txt"), false);
    }
};

QTEST_MAIN(TestClipboardManager)
#include "tst_clipboardmanager.moc"

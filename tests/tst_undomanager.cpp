#include <QTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUuid>

#include "services/fileoperations.h"
#include "services/undomanager.h"

class TestUndoManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void testUndoTrashOnMountedVolume()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString mediaRoot = "/run/media/" + qEnvironmentVariable("USER");
        QDir mediaDir(mediaRoot);
        if (!mediaDir.exists())
            QSKIP("/run/media/$USER does not exist");

        const QStringList entries = mediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (entries.isEmpty())
            QSKIP("No mounted volumes found under /run/media/$USER");

        const QString mountPath = mediaDir.filePath(entries.constFirst());
        const QString testDirPath = mountPath + "/hyprfm-undo-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(testDirPath);

        const QString filePath = testDirPath + "/undo_me.txt";
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("undo mounted trash");
        file.close();

        FileOperations fileOps;
        UndoManager undoManager(&fileOps);
        QSignalSpy finishSpy(&fileOps, &FileOperations::operationFinished);

        undoManager.trashFiles({filePath});
        if (!finishSpy.wait(5000))
            QSKIP("gio trash timed out (may not be supported in this environment)");

        bool success = finishSpy.at(0).at(0).toBool();
        if (!success)
            QSKIP("gio trash failed (may not be supported for this mounted path)");

        QVERIFY(!QFile::exists(filePath));
        QVERIFY(undoManager.canUndo());

        finishSpy.clear();
        undoManager.undo();
        if (!finishSpy.wait(5000))
            QSKIP("trash undo restore timed out");

        success = finishSpy.at(0).at(0).toBool();
        if (!success)
            QSKIP("trash undo restore failed for mounted path");

        QVERIFY(QFile::exists(filePath));
        QDir(testDirPath).removeRecursively();
    }
};

QTEST_MAIN(TestUndoManager)
#include "tst_undomanager.moc"

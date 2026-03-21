#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include "services/fileoperations.h"

class TestFileOperations : public QObject
{
    Q_OBJECT

private slots:
    void testCopyFile()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");
        QTemporaryDir srcDir, dstDir;
        QFile f(srcDir.path() + "/test.txt");
        f.open(QIODevice::WriteOnly);
        f.write("hello");
        f.close();

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({srcDir.path() + "/test.txt"}, dstDir.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dstDir.path() + "/test.txt"));
    }

    void testMoveFile()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");
        QTemporaryDir srcDir, dstDir;
        QString srcPath = srcDir.path() + "/test.txt";
        QFile f(srcPath);
        f.open(QIODevice::WriteOnly);
        f.write("hello");
        f.close();

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.moveFiles({srcPath}, dstDir.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dstDir.path() + "/test.txt"));
        QVERIFY(!QFile::exists(srcPath));
    }

    void testRenameFile()
    {
        QTemporaryDir dir;
        QString oldPath = dir.path() + "/old.txt";
        QFile f(oldPath);
        f.open(QIODevice::WriteOnly);
        f.write("rename me");
        f.close();

        FileOperations ops;
        bool result = ops.rename(oldPath, "new.txt");

        QVERIFY(result);
        QVERIFY(QFile::exists(dir.path() + "/new.txt"));
        QVERIFY(!QFile::exists(oldPath));
    }
};

QTEST_MAIN(TestFileOperations)
#include "tst_fileoperations.moc"

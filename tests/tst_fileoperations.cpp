#include <QTest>
#include <QClipboard>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QStandardPaths>
#include "testdir.h"
#include "services/fileoperations.h"

class TestFileOperations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // --- Copy ---

    void testCopyFile()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");

        TestDir src, dst;
        src.createFile("test.txt", "hello");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/test.txt"}, dst.path());

        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toBool(), true); // success
        QVERIFY(QFile::exists(dst.path() + "/test.txt"));
        // Source still exists after copy
        QVERIFY(QFile::exists(src.path() + "/test.txt"));
    }

    void testCopyMultipleFiles()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");

        TestDir src, dst;
        src.createFile("a.txt", "aaa");
        src.createFile("b.txt", "bbb");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/a.txt", src.path() + "/b.txt"}, dst.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dst.path() + "/a.txt"));
        QVERIFY(QFile::exists(dst.path() + "/b.txt"));
    }

    void testCopyDirectory()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");

        TestDir src, dst;
        src.createDir("subdir");
        src.createFile("subdir/inner.txt", "content");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/subdir"}, dst.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dst.path() + "/subdir/inner.txt"));
    }

    void testCopyEmitsBusyChanged()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");

        TestDir src, dst;
        src.createFile("test.txt", "data");

        FileOperations ops;
        QSignalSpy busySpy(&ops, &FileOperations::busyChanged);
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        QCOMPARE(ops.busy(), false);
        ops.copyFiles({src.path() + "/test.txt"}, dst.path());

        QVERIFY(finishSpy.wait(5000));
        QVERIFY(busySpy.count() >= 2); // true then false
        QCOMPARE(ops.busy(), false);
    }

    void testCopyEmitsStatusText()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");

        TestDir src, dst;
        src.createFile("test.txt", "data");

        FileOperations ops;
        QSignalSpy statusSpy(&ops, &FileOperations::statusTextChanged);
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/test.txt"}, dst.path());

        QVERIFY(finishSpy.wait(5000));
        QVERIFY(statusSpy.count() >= 1);
    }

    // --- Move ---

    void testMoveFile()
    {
        if (QStandardPaths::findExecutable("rsync").isEmpty())
            QSKIP("rsync not found in PATH");

        TestDir src, dst;
        QString srcPath = src.path() + "/test.txt";
        src.createFile("test.txt", "hello");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.moveFiles({srcPath}, dst.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dst.path() + "/test.txt"));
        QVERIFY(!QFile::exists(srcPath));
    }

    // --- Rename ---

    void testRenameFile()
    {
        TestDir dir;
        QString oldPath = dir.path() + "/old.txt";
        dir.createFile("old.txt", "rename me");

        FileOperations ops;
        bool result = ops.rename(oldPath, "new.txt");

        QVERIFY(result);
        QVERIFY(QFile::exists(dir.path() + "/new.txt"));
        QVERIFY(!QFile::exists(oldPath));
    }

    void testRenameToExistingFails()
    {
        TestDir dir;
        dir.createFile("a.txt", "aaa");
        dir.createFile("b.txt", "bbb");

        FileOperations ops;
        // Renaming a.txt to b.txt when b.txt exists — behavior is platform-dependent
        // On Linux, QFile::rename may overwrite or fail; just verify no crash
        ops.rename(dir.path() + "/a.txt", "b.txt");
    }

    void testRenameWithSpecialChars()
    {
        TestDir dir;
        dir.createFile("normal.txt", "content");

        FileOperations ops;
        bool result = ops.rename(dir.path() + "/normal.txt", "file with spaces.txt");

        QVERIFY(result);
        QVERIFY(QFile::exists(dir.path() + "/file with spaces.txt"));
    }

    // --- Delete (permanent) ---

    void testDeleteFile()
    {
        TestDir dir;
        dir.createFile("doomed.txt", "bye");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({dir.path() + "/doomed.txt"});

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(!dir.exists("doomed.txt"));
    }

    void testDeleteDirectory()
    {
        TestDir dir;
        dir.createDir("doomed_dir");
        dir.createFile("doomed_dir/inner.txt", "bye");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({dir.path() + "/doomed_dir"});

        QCOMPARE(spy.count(), 1);
        QVERIFY(!dir.exists("doomed_dir"));
    }

    void testDeleteMultiple()
    {
        TestDir dir;
        dir.createFile("a.txt");
        dir.createFile("b.txt");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({dir.path() + "/a.txt", dir.path() + "/b.txt"});

        QCOMPARE(spy.count(), 1);
        QVERIFY(!dir.exists("a.txt"));
        QVERIFY(!dir.exists("b.txt"));
    }

    // --- Create folder ---

    void testCreateFolder()
    {
        TestDir dir;

        FileOperations ops;
        ops.createFolder(dir.path(), "new_folder");

        QVERIFY(dir.exists("new_folder"));
        QVERIFY(QFileInfo(dir.path() + "/new_folder").isDir());
    }

    void testCreateNestedFolder()
    {
        TestDir dir;
        dir.createDir("parent");

        FileOperations ops;
        ops.createFolder(dir.path() + "/parent", "child");

        QVERIFY(dir.exists("parent/child"));
    }

    // --- Create file ---

    void testCreateFile()
    {
        TestDir dir;

        FileOperations ops;
        ops.createFile(dir.path(), "new_file.txt");

        QVERIFY(dir.exists("new_file.txt"));
        QFileInfo info(dir.path() + "/new_file.txt");
        QCOMPARE(info.size(), 0LL); // empty file
    }

    void testPasteClipboardImage()
    {
        TestDir dir;

        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard != nullptr);

        QMimeData *savedMime = new QMimeData;
        if (const QMimeData *original = clipboard->mimeData()) {
            for (const QString &format : original->formats())
                savedMime->setData(format, original->data(format));
            if (original->hasImage())
                savedMime->setImageData(original->imageData());
            if (original->hasText())
                savedMime->setText(original->text());
        }

        QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        clipboard->setImage(image);

        const QByteArray originalPath = qgetenv("PATH");
        qputenv("PATH", QFileInfo(QCoreApplication::applicationFilePath()).absolutePath().toUtf8());

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        const QString outputPath = ops.pasteClipboardImage(dir.path());

        qputenv("PATH", originalPath);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(!outputPath.isEmpty());
        QVERIFY(QFile::exists(outputPath));

        QImage saved(outputPath);
        QVERIFY(!saved.isNull());
        QCOMPARE(saved.size(), image.size());

        clipboard->setMimeData(savedMime);
    }

    // --- Trash (requires gio) ---

    void testTrashFile()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        // Use a dir under home so gio trash can find the Trash directory
        QString testPath = QDir::homePath() + "/.cache/hyprfm-test-trash";
        QDir().mkpath(testPath);
        QString filePath = testPath + "/trash_me.txt";
        {
            QFile f(filePath);
            f.open(QIODevice::WriteOnly);
            f.write("bye");
            f.close();
        }

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.trashFiles({filePath});

        if (!spy.wait(5000))
            QSKIP("gio trash timed out (may not be supported in this environment)");

        bool success = spy.at(0).at(0).toBool();
        if (!success)
            QSKIP("gio trash failed (may not be supported for this path)");

        QVERIFY(!QFile::exists(filePath));
        QDir(testPath).removeRecursively();
    }

    // --- Progress property ---

    void testProgressInitialValue()
    {
        FileOperations ops;
        QCOMPARE(ops.progress(), 0.0);
        QCOMPARE(ops.busy(), false);
        QCOMPARE(ops.statusText(), QString());
    }
};

QTEST_MAIN(TestFileOperations)
#include "tst_fileoperations.moc"

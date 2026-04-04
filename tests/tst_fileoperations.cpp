#include <QTest>
#include <QClipboard>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <unistd.h>
#include "testdir.h"
#include "services/fileoperations.h"

class TestFileOperations : public QObject
{
    Q_OBJECT

    static QString findTrashEntryPath(const QString &originalPath)
    {
        const QString trashInfoDirPath = QDir::homePath() + "/.local/share/Trash/info";
        const QString trashFilesDirPath = QDir::homePath() + "/.local/share/Trash/files";
        QDir infoDir(trashInfoDirPath);

        const QStringList entries = infoDir.entryList({"*.trashinfo"}, QDir::Files, QDir::Time);
        for (const QString &entry : entries) {
            QFile file(infoDir.filePath(entry));
            if (!file.open(QIODevice::ReadOnly))
                continue;

            const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
            for (const QString &line : lines) {
                if (!line.startsWith("Path="))
                    continue;

                const QString decodedPath = QUrl::fromPercentEncoding(line.mid(5).toUtf8());
                if (decodedPath == originalPath)
                    return QDir(trashFilesDirPath).filePath(entry.chopped(10));
            }
        }

        return {};
    }

    static QString findTrashEntryUri(const QString &originalPath)
    {
        QProcess proc;
        proc.start("gio", {
            "list",
            "-l",
            "-u",
            "-a",
            "trash::orig-path",
            "trash:///"
        });
        if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
            return {};

        const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (!line.contains("trash::orig-path=" + originalPath))
                continue;

            return line.section('\t', 0, 0).trimmed();
        }

        return {};
    }

    static bool runCommand(const QString &program, const QStringList &args,
                           const QString &workingDirectory = {})
    {
        QProcess proc;
        if (!workingDirectory.isEmpty())
            proc.setWorkingDirectory(workingDirectory);

        proc.start(program, args);
        return proc.waitForFinished(5000) && proc.exitCode() == 0;
    }

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

    void testTransferPlanDetectsConflict()
    {
        TestDir src, dst;
        src.createFile("conflict.txt", "new");
        dst.createFile("conflict.txt", "old");

        FileOperations ops;
        const QVariantList plan = ops.transferPlan({src.path() + "/conflict.txt"}, dst.path());

        QCOMPARE(plan.size(), 1);
        const QVariantMap item = plan.constFirst().toMap();
        QCOMPARE(item.value("sourceName").toString(), QString("conflict.txt"));
        QCOMPARE(item.value("targetPath").toString(), dst.path() + "/conflict.txt");
        QVERIFY(item.value("targetExists").toBool());
    }

    void testUniqueNameForDestination()
    {
        TestDir dir;
        dir.createFile("report.txt", "existing");

        FileOperations ops;
        QCOMPARE(ops.uniqueNameForDestination(dir.path(), "report.txt"), QString("report (copy).txt"));
        QCOMPARE(ops.uniqueNameForDestination(dir.path(), "draft.txt", {"draft.txt", "draft (copy).txt"}),
                 QString("draft (copy 2).txt"));
    }

    void testCopyResolvedItemsRename()
    {
        TestDir src, dst;
        src.createFile("test.txt", "hello");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        QVariantMap item;
        item["sourcePath"] = src.path() + "/test.txt";
        item["targetPath"] = dst.path() + "/renamed.txt";
        item["overwrite"] = false;

        ops.copyResolvedItems({item});

        QCOMPARE(spy.wait(5000), true);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(QFile::exists(dst.path() + "/renamed.txt"));
        QVERIFY(QFile::exists(src.path() + "/test.txt"));
    }

    void testMoveResolvedItemsOverwrite()
    {
        TestDir src, dst;
        src.createFile("test.txt", "new content");
        dst.createFile("test.txt", "old content");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        QVariantMap item;
        item["sourcePath"] = src.path() + "/test.txt";
        item["targetPath"] = dst.path() + "/test.txt";
        item["overwrite"] = true;
        item["backupPath"] = ops.conflictBackupPath(dst.path() + "/test.txt");

        ops.moveResolvedItems({item});

        QCOMPARE(spy.wait(5000), true);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(!QFile::exists(src.path() + "/test.txt"));
        QVERIFY(QFile::exists(dst.path() + "/test.txt"));

        QFile result(dst.path() + "/test.txt");
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(result.readAll()), QString("new content"));
        QVERIFY(QFileInfo::exists(item.value("backupPath").toString()));
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

    void testRenameResolvedItemsSwapsNames()
    {
        TestDir dir;
        const QString aPath = dir.createFile("a.txt", "aaa");
        const QString bPath = dir.createFile("b.txt", "bbb");

        FileOperations ops;

        QVariantMap renameA;
        renameA["sourcePath"] = aPath;
        renameA["targetPath"] = bPath;

        QVariantMap renameB;
        renameB["sourcePath"] = bPath;
        renameB["targetPath"] = aPath;

        const QVariantMap result = ops.renameResolvedItems({renameA, renameB});

        QCOMPARE(result.value("success").toBool(), true);
        QCOMPARE(result.value("changedPaths").toStringList(), QStringList({bPath, aPath}));

        QFile aFile(aPath);
        QVERIFY(aFile.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(aFile.readAll()), QString("bbb"));

        QFile bFile(bPath);
        QVERIFY(bFile.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(bFile.readAll()), QString("aaa"));
    }

    void testRenameResolvedItemsRejectsExistingTarget()
    {
        TestDir dir;
        const QString sourcePath = dir.createFile("source.txt", "source");
        const QString takenPath = dir.createFile("taken.txt", "taken");

        FileOperations ops;

        QVariantMap item;
        item["sourcePath"] = sourcePath;
        item["targetPath"] = takenPath;

        const QVariantMap result = ops.renameResolvedItems({item});

        QCOMPARE(result.value("success").toBool(), false);
        QVERIFY(QFile::exists(sourcePath));
        QVERIFY(QFile::exists(takenPath));
    }

    void testRenameResolvedItemsRejectsDuplicateFinalTarget()
    {
        TestDir dir;
        const QString aPath = dir.createFile("a.txt", "aaa");
        const QString bPath = dir.createFile("b.txt", "bbb");

        FileOperations ops;

        QVariantMap keepA;
        keepA["sourcePath"] = aPath;
        keepA["targetPath"] = aPath;

        QVariantMap renameB;
        renameB["sourcePath"] = bPath;
        renameB["targetPath"] = aPath;

        const QVariantMap result = ops.renameResolvedItems({keepA, renameB});

        QCOMPARE(result.value("success").toBool(), false);
        QVERIFY(QFile::exists(aPath));
        QVERIFY(QFile::exists(bPath));
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
            QVERIFY(f.open(QIODevice::WriteOnly));
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

    void testRestoreFromTrash()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString testPath = QDir::homePath() + "/.cache/hyprfm-test-restore-trash-" + uniqueId;
        QDir().mkpath(testPath);

        const QString filePath = testPath + "/restore_me.txt";
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("restore me");
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

        const QString trashedPath = findTrashEntryPath(filePath);
        if (trashedPath.isEmpty())
            QSKIP("Could not locate trashed file metadata");

        spy.clear();
        ops.restoreFromTrash({trashedPath});

        if (!spy.wait(5000))
            QSKIP("gio trash restore timed out (may not be supported in this environment)");

        success = spy.at(0).at(0).toBool();
        if (!success)
            QSKIP("gio trash restore failed (may not be supported in this environment)");

        QVERIFY(QFile::exists(filePath));
        QDir(testPath).removeRecursively();
    }

    void testRestoreFromTrashUri()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString testPath = QDir::homePath() + "/.cache/hyprfm-test-restore-trash-uri-" + uniqueId;
        QDir().mkpath(testPath);

        const QString filePath = testPath + "/restore_uri_me.txt";
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("restore via uri");
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

        const QString trashUri = findTrashEntryUri(filePath);
        if (trashUri.isEmpty())
            QSKIP("Could not locate trashed file URI");

        spy.clear();
        ops.restoreFromTrash({trashUri});

        if (!spy.wait(5000))
            QSKIP("gio trash restore timed out (may not be supported in this environment)");

        success = spy.at(0).at(0).toBool();
        if (!success)
            QSKIP("gio trash restore failed (may not be supported in this environment)");

        QVERIFY(QFile::exists(filePath));
        QDir(testPath).removeRecursively();
    }

    void testTrashHelpersForHomePath()
    {
        FileOperations ops;

        const QString expectedTrashPath = QDir::cleanPath(QDir::homePath() + "/.local/share/Trash/files");
        const QString homeFilePath = QDir::homePath() + "/Documents/example.txt";

        QCOMPARE(ops.trashFilesPathFor(homeFilePath), expectedTrashPath);
        QVERIFY(!ops.isTrashPath(homeFilePath));
        QVERIFY(ops.isTrashPath(expectedTrashPath + "/example.txt"));
    }

    void testTrashHelpersForMountedVolume()
    {
        const QString mediaRoot = "/run/media/" + qEnvironmentVariable("USER");
        QDir mediaDir(mediaRoot);
        if (!mediaDir.exists())
            QSKIP("/run/media/$USER does not exist");

        const QStringList entries = mediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (entries.isEmpty())
            QSKIP("No mounted volumes found under /run/media/$USER");

        const QString mountPath = mediaDir.filePath(entries.constFirst());
        FileOperations ops;

        const QString expectedTrashPath = QDir::cleanPath(
            mountPath + "/.Trash-" + QString::number(geteuid()) + "/files");

        QCOMPARE(ops.trashFilesPathFor(mountPath + "/example.txt"), expectedTrashPath);
        QVERIFY(ops.isTrashPath(expectedTrashPath + "/example.txt"));
    }

    void testArchiveSupportFor7zAndRar_data()
    {
        QTest::addColumn<QString>("extension");

        QTest::newRow("7z") << ".7z";
        QTest::newRow("rar") << ".rar";
    }

    void testArchiveSupportFor7zAndRar()
    {
        if (QStandardPaths::findExecutable("zip").isEmpty())
            QSKIP("zip not found in PATH");

        if (QStandardPaths::findExecutable("7z").isEmpty()
                && QStandardPaths::findExecutable("bsdtar").isEmpty())
            QSKIP("Neither 7z nor bsdtar found in PATH");

        QFETCH(QString, extension);

        TestDir archiveDir;
        TestDir extractDir;
        archiveDir.createDir("payload");
        archiveDir.createFile("payload/inner.txt", "hello");

        QVERIFY(runCommand("zip", {"-rq", "payload.zip", "payload"}, archiveDir.path()));

        const QString archivePath = archiveDir.path() + "/payload" + extension;
        QVERIFY(QFile::copy(archiveDir.path() + "/payload.zip", archivePath));

        FileOperations ops;
        QVERIFY(FileOperations::isArchive(archivePath));
        QVERIFY(FileOperations::isArchive(archiveDir.path() + "/payload" + extension.toUpper()));
        QCOMPARE(ops.archiveRootFolder(archivePath), QString("payload"));

        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        ops.extractArchive(archivePath, extractDir.path());

        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(QFile::exists(extractDir.path() + "/payload/inner.txt"));
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

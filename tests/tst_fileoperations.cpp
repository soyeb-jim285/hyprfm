#include <QTest>
#include <QScopeGuard>
#include <QClipboard>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <cstring>
#include <unistd.h>
#include "testdir.h"
#include "services/fileoperations.h"
#include "services/xdgtrash.h"

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

    // Built from the on-disk entry rather than `gio list trash:///`, so the
    // trash:// call sites stay covered on machines and CI containers with no
    // gvfs session daemon — which is the whole point of reading the trash
    // directly.
    static QString findTrashEntryUri(const QString &originalPath)
    {
        const QString entryPath = findTrashEntryPath(originalPath);
        if (entryPath.isEmpty())
            return {};

        QUrl uri;
        uri.setScheme("trash");
        uri.setPath("/" + QFileInfo(entryPath).fileName());
        return uri.toString(QUrl::FullyEncoded);
    }

    static bool runCommand(const QString &program, const QStringList &args,
                           const QString &workingDirectory = {}, int timeoutMs = 5000)
    {
        QProcess proc;
        if (!workingDirectory.isEmpty())
            proc.setWorkingDirectory(workingDirectory);

        proc.start(program, args);
        return proc.waitForFinished(timeoutMs) && proc.exitCode() == 0;
    }

    // One-level archive with enough incompressible data that extraction lasts
    // long enough to pause or cancel mid-flight. Empty on failure.
    static QString createWideArchive(TestDir &archiveDir, int fileCount, int perFileBytes)
    {
        QRandomGenerator rng(12345);
        for (int i = 0; i < fileCount; ++i) {
            QByteArray data(perFileBytes, Qt::Uninitialized);
            const int fullWords = perFileBytes / 4;
            for (int j = 0; j < fullWords; ++j) {
                const quint32 v = rng.generate();
                memcpy(data.data() + j * 4, &v, 4);
            }
            for (int j = fullWords * 4; j < perFileBytes; ++j)
                data[j] = static_cast<char>(rng.generate() & 0xFF);
            archiveDir.createFile("payload/f" + QString::number(i) + ".dat", data);
        }
        const QString archivePath = archiveDir.path() + "/wide.7z";
        if (!runCommand("7z", {"a", archivePath, "payload"}, archiveDir.path(), 120000))
            return {};
        return archivePath;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // --- Desktop entry Exec= parsing (Open With) ---

    void testDesktopExecArguments()
    {
        const QString file = "/tmp/clip.mkv";

        // The common field codes all stand in for the file.
        QCOMPARE(FileOperations::desktopExecArguments("mpv %U", file),
                 QStringList({"mpv", file}));
        QCOMPARE(FileOperations::desktopExecArguments("nvim %F", file),
                 QStringList({"nvim", file}));

        // Options around the field code survive, in order.
        QCOMPARE(FileOperations::desktopExecArguments(
                     "mpv --player-operation-mode=pseudo-gui -- %U", file),
                 QStringList({"mpv", "--player-operation-mode=pseudo-gui", "--", file}));

        // Codes that carry no meaning here are dropped, not passed through.
        QCOMPARE(FileOperations::desktopExecArguments("app %i %c %k %U", file),
                 QStringList({"app", file}));

        // An entry with no field code still gets the file appended.
        QCOMPARE(FileOperations::desktopExecArguments("micro", file),
                 QStringList({"micro", file}));

        // Quoted paths stay one argument, and %% is a literal percent.
        QCOMPARE(FileOperations::desktopExecArguments("\"/opt/My App/run\" -x 100%% %f", file),
                 QStringList({"/opt/My App/run", "-x", "100%", file}));

        // No file to open: the field code just disappears.
        QCOMPARE(FileOperations::desktopExecArguments("mpv %U", QString()),
                 QStringList({"mpv"}));
    }

    // The launched app inherits gio's stdio. If those are pipes back to us,
    // closing them once gio exits kills any app that later writes to stderr
    // (Krita, mpv, ...) with SIGPIPE — the "Open With does nothing" report.
    void testOpenFileWithSurvivesLauncherExit()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        QTemporaryDir dir;
        const QString marker = dir.filePath("alive");
        const QString script = dir.filePath("app.sh");
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\nsleep 0.5\necho still-running >&2\ntouch \"$1\"\n");
        }
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QString entry = dir.filePath("app.desktop");
        {
            QFile f(entry);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Test\nExec=%1 %f\n")
                        .arg(script).toUtf8());
        }

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        ops.openFileWith(marker, entry);

        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 5000);
        QVERIFY2(spy.isEmpty(), "launch reported a failure");
    }

    void testOpenInEditorRunsEditorInTerminal()
    {
        QTemporaryDir dir;
        const QString marker = dir.filePath("argv");
        const QString term = dir.filePath("term.sh");
        {
            QFile f(term);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("#!/bin/sh\nprintf '%s ' \"$@\" > \"%1\"\n").arg(marker).toUtf8());
        }
        QFile::setPermissions(term, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TERMINAL", term.toUtf8());
        qputenv("VISUAL", "");
        qputenv("EDITOR", "myeditor --wait");

        FileOperations ops;
        ops.openInEditor("/tmp/config.toml");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 5000);
        QFile f(marker);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()).trimmed(), QString("-e myeditor --wait /tmp/config.toml"));
        qunsetenv("TERMINAL"); qunsetenv("EDITOR");
    }

    // Ghostty (and other single-instance terminals) hand a bare launch to the
    // already-running instance, which ignores our inherited cwd. Known
    // terminals get an explicit working-directory flag.
    void testOpenInTerminalPassesDirFlagForKnownTerminal()
    {
        QTemporaryDir dir;
        const QString marker = dir.filePath("argv");
        const QString term = dir.filePath("ghostty");
        {
            QFile f(term);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("#!/bin/sh\n{ printf '%s ' \"$@\"; echo; pwd; } > \"%1\"\n").arg(marker).toUtf8());
        }
        QFile::setPermissions(term, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("TERMINAL", term.toUtf8());

        FileOperations ops;
        ops.openInTerminal(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 5000);
        QFile f(marker);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].trimmed(),
                 QStringLiteral("--gtk-single-instance=false --working-directory=%1").arg(dir.path()));
        QCOMPARE(lines[1], dir.path());
        qunsetenv("TERMINAL");
    }

    void testRunCustomActionSubstitutesPathAndRunsPerItem()
    {
        QTemporaryDir dir;
        const QString script = dir.filePath("act.sh");
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\ntouch \"$1.done\"\n");
        }
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QString a = dir.filePath("a.txt"), b = dir.filePath("b.txt");

        FileOperations ops;
        ops.runCustomAction(script + " %f", {a, b});
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(a + ".done"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(b + ".done"), 5000);
    }

    // --- Copy ---

    void testCopyFile()
    {
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
        TestDir src, dst;
        src.createFile("test.txt", "data");

        FileOperations ops;
        QSignalSpy statusSpy(&ops, &FileOperations::statusTextChanged);
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/test.txt"}, dst.path());

        QVERIFY(finishSpy.wait(5000));
        QVERIFY(statusSpy.count() >= 1);
    }

    void testCopyEmitsProgressChanged()
    {
        TestDir src, dst;
        src.createFile("large.bin", QByteArray(8 * 1024 * 1024, 'x'));

        FileOperations ops;
        QSignalSpy progressSpy(&ops, &FileOperations::progressChanged);
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/large.bin"}, dst.path());

        QVERIFY(finishSpy.wait(10000));
        QVERIFY(progressSpy.count() >= 1);
        QCOMPARE(ops.progress(), 1.0);
    }

    // --- Move ---

    void testMoveFile()
    {
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

    void testCopyDirectoryIntoItselfIsRejected()
    {
        TestDir dir;
        dir.createDir("a");
        dir.createFile("a/f.txt", "x");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({dir.path() + "/a"}, dir.path() + "/a");

        if (spy.isEmpty())
            QVERIFY(spy.wait(30000));
        QCOMPARE(spy.at(0).at(0).toBool(), false);
        QVERIFY(!dir.exists("a/a"));
    }

    void testDeleteFile()
    {
        TestDir dir;
        dir.createFile("doomed.txt", "bye");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({dir.path() + "/doomed.txt"});

        QVERIFY(spy.wait(5000));
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

        QVERIFY(spy.wait(5000));
        QVERIFY(!dir.exists("doomed_dir"));
    }

    void testDeleteSymlinkToDirectoryKeepsTarget()
    {
        TestDir dir;
        dir.createDir("real");
        dir.createFile("real/a.txt", "keep");
        dir.createSymlink(dir.path() + "/real", "link");
        QVERIFY(QFileInfo(dir.path() + "/link").isSymLink());

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({dir.path() + "/link"});

        QVERIFY(spy.wait(5000));
        QVERIFY(dir.exists("real/a.txt"));
        QVERIFY(!QFileInfo(dir.path() + "/link").isSymLink());
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void testDeleteDirectoryViaFileUri()
    {
        TestDir dir;
        dir.createDir("doomed_uri_dir");
        dir.createFile("doomed_uri_dir/inner.txt", "bye");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({QUrl::fromLocalFile(dir.path() + "/doomed_uri_dir").toString()});

        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(!dir.exists("doomed_uri_dir"));
    }

    void testDeleteMultiple()
    {
        TestDir dir;
        dir.createFile("a.txt");
        dir.createFile("b.txt");

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.deleteFiles({dir.path() + "/a.txt", dir.path() + "/b.txt"});

        QVERIFY(spy.wait(5000));
        QVERIFY(!dir.exists("a.txt"));
        QVERIFY(!dir.exists("b.txt"));
    }

    // --- Create folder ---

    // Names are basenames: a "name" with separators or ".." must not escape
    // the folder it was typed in.
    void testNamesWithPathSegmentsAreRejected()
    {
        TestDir dir;
        dir.createDir("inner");
        dir.createFile("inner/file.txt", "x");
        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.createFolder(dir.path() + "/inner", "../escaped");
        ops.createFolder(dir.path() + "/inner", "a/b");
        ops.createFile(dir.path() + "/inner", "../escaped.txt");
        QCOMPARE(spy.count(), 3);
        for (const auto &args : spy) QCOMPARE(args.at(0).toBool(), false);
        QVERIFY(!dir.exists("escaped"));
        QVERIFY(!dir.exists("inner/a"));
        QVERIFY(!dir.exists("escaped.txt"));

        QVERIFY(!ops.rename(dir.path() + "/inner/file.txt", "../moved.txt"));
        QVERIFY(!ops.rename(dir.path() + "/inner/file.txt", "sub/file.txt"));
        QVERIFY(!ops.rename(dir.path() + "/inner/file.txt", ".."));
        QVERIFY(dir.exists("inner/file.txt"));
        QVERIFY(!dir.exists("moved.txt"));
    }

    // New File must never truncate something that appeared after the
    // dialog's existence check.
    void testCreateFileKeepsExistingContent()
    {
        TestDir dir;
        dir.createFile("taken.txt", "precious");
        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        ops.createFile(dir.path(), "taken.txt");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.constFirst().at(0).toBool(), false);
        QFile f(dir.path() + "/taken.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("precious"));
    }

    // Two operations in flight report their own ids, so a caller waiting on
    // one cannot be satisfied by the other finishing first.
    void testOperationFinishedCarriesTheOperationId()
    {
        TestDir dir;
        dir.createFile("a.txt", "a");
        dir.createFile("b.txt", "b");
        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        const int first = ops.deleteFiles({dir.path() + "/a.txt"});
        const int second = ops.deleteFiles({dir.path() + "/b.txt"});
        QVERIFY(first >= 0 && second >= 0 && first != second);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 10000);
        QSet<int> ids{spy.at(0).at(2).toInt(), spy.at(1).at(2).toInt()};
        QCOMPARE(ids, QSet<int>({first, second}));
        QCOMPARE(ops.copyResolvedItems({}), -1);   // synchronous "nothing to do"
    }

    // Opening an archive extracts next to it into a folder that did not
    // exist before, never over the parent directory's files.
    void testNewExtractionFolderNeverReusesAnExistingOne()
    {
        TestDir dir;
        dir.createFile("photos.tar.gz", "not really an archive");
        dir.createDir("photos");
        dir.createFile("photos/keep.txt", "keep");
        FileOperations ops;
        const QString dest = ops.newExtractionFolder(dir.path() + "/photos.tar.gz");
        QVERIFY(!dest.isEmpty());
        QVERIFY(QFileInfo(dest).isDir());
        QVERIFY(dest != dir.path() + "/photos");
        QVERIFY(dest.startsWith(dir.path() + "/photos"));
        QVERIFY(dir.exists("photos/keep.txt"));
    }

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

    // Asked on every right-click: answer from the offered types, without
    // fetching the image or running wl-paste when Qt can see the clipboard.
    void testHasClipboardImageReadsOfferedTypesOnly()
    {
        QClipboard *clipboard = QGuiApplication::clipboard();
        const QByteArray originalPath = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");   // any wl-paste use would fail the test
        auto restore = qScopeGuard([&] { qputenv("PATH", originalPath); clipboard->clear(); });

        FileOperations ops;
        QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        clipboard->setImage(image);
        QVERIFY(ops.hasClipboardImage());

        clipboard->setText(QStringLiteral("just text"));
        QVERIFY(!ops.hasClipboardImage());
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

    void testPasteClipboardImagePrefersCurrentClipboardOverWlPaste()
    {
        TestDir dir;
        TestDir binDir;

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

        QImage currentImage(8, 8, QImage::Format_ARGB32_Premultiplied);
        currentImage.fill(Qt::red);
        clipboard->setImage(currentImage);

        QImage staleImage(8, 8, QImage::Format_ARGB32_Premultiplied);
        staleImage.fill(Qt::blue);
        const QString staleImagePath = dir.path() + "/stale.png";
        QVERIFY(staleImage.save(staleImagePath, "PNG"));

        QFile wlPaste(binDir.path() + "/wl-paste");
        QVERIFY(wlPaste.open(QIODevice::WriteOnly | QIODevice::Text));
        wlPaste.write("#!/bin/sh\n");
        wlPaste.write("if [ \"$1\" = \"--list-types\" ]; then\n");
        wlPaste.write("  printf 'image/png\\n'\n");
        wlPaste.write("  exit 0\n");
        wlPaste.write("fi\n");
        wlPaste.write("if [ \"$1\" = \"--no-newline\" ] && [ \"$2\" = \"--type\" ] && [ \"$3\" = \"image/png\" ]; then\n");
        wlPaste.write(QString("  cat '%1'\n").arg(staleImagePath).toUtf8());
        wlPaste.write("  exit 0\n");
        wlPaste.write("fi\n");
        wlPaste.write("exit 1\n");
        wlPaste.close();
        QVERIFY(QFile::setPermissions(wlPaste.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                                           | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                                           | QFileDevice::ReadOther | QFileDevice::ExeOther));

        const QByteArray originalPath = qgetenv("PATH");
        qputenv("PATH", (binDir.path().toUtf8() + ":" + originalPath));

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
        QCOMPARE(saved.pixelColor(0, 0), QColor(Qt::red));

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

    // Permanently deleting a trashed *directory* must work: the gvfs trash
    // backend refuses per-child deletes ("Items in the trash may not be
    // modified"), so only a delete of the top-level trash entry succeeds.
    void testDeleteTrashedDirectory()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString testPath = QDir::homePath() + "/.cache/hyprfm-test-delete-trash-dir-" + uniqueId;
        const QString dirPath = testPath + "/doomed_folder";
        QVERIFY(QDir().mkpath(dirPath + "/nested"));
        {
            QFile f(dirPath + "/nested/inside.txt");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("delete me");
            f.close();
        }

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.trashFiles({dirPath});
        if (!spy.wait(5000))
            QSKIP("gio trash timed out (may not be supported in this environment)");
        if (!spy.at(0).at(0).toBool())
            QSKIP("gio trash failed (may not be supported for this path)");

        const QString trashUri = findTrashEntryUri(dirPath);
        if (trashUri.isEmpty())
            QSKIP("Could not locate trashed directory URI");

        spy.clear();
        ops.deleteFiles({trashUri});
        QVERIFY(spy.wait(10000));
        QVERIFY2(spy.at(0).at(0).toBool(),
                 qPrintable(spy.at(0).at(1).toString()));
        QVERIFY(findTrashEntryUri(dirPath).isEmpty());

        QDir(testPath).removeRecursively();
    }

    void testDeleteFromTrashRemovesMetadata()
    {
        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString testPath = QDir::homePath() + "/.cache/hyprfm-test-trash-meta-" + uniqueId;
        QVERIFY(QDir().mkpath(testPath));
        const QString filePath = testPath + "/meta_me.txt";
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("meta");
            f.close();
        }

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        ops.trashFiles({filePath});
        if (!spy.wait(5000) || !spy.at(0).at(0).toBool())
            QSKIP("trashing is not supported in this environment");

        const QString trashedPath = findTrashEntryPath(filePath);
        if (trashedPath.isEmpty())
            QSKIP("Could not locate trashed file metadata");

        const QString infoPath = XdgTrash::infoPathFor(trashedPath);
        QVERIFY(!infoPath.isEmpty());
        QVERIFY(QFile::exists(infoPath));

        spy.clear();
        ops.deleteFiles({trashedPath});
        QVERIFY(spy.wait(10000));
        QVERIFY2(spy.at(0).at(0).toBool(), qPrintable(spy.at(0).at(1).toString()));

        QVERIFY(!QFile::exists(trashedPath));
        // The sidecar has to go with the file, or the trash keeps growing
        // metadata for items that no longer exist.
        QVERIFY(!QFile::exists(infoPath));

        QDir(testPath).removeRecursively();
    }

    void testRestoreDoesNotOverwriteExistingFile()
    {
        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString testPath = QDir::homePath() + "/.cache/hyprfm-test-restore-clash-" + uniqueId;
        QVERIFY(QDir().mkpath(testPath));
        const QString filePath = testPath + "/clash.txt";
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("original");
            f.close();
        }

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        ops.trashFiles({filePath});
        if (!spy.wait(5000) || !spy.at(0).at(0).toBool())
            QSKIP("trashing is not supported in this environment");

        const QString trashedPath = findTrashEntryPath(filePath);
        if (trashedPath.isEmpty())
            QSKIP("Could not locate trashed file metadata");

        // Something new now occupies the original location. Restoring must
        // report an error rather than silently destroying it.
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("replacement");
            f.close();
        }

        spy.clear();
        ops.restoreFromTrash({trashedPath});
        QVERIFY(spy.wait(10000));
        QCOMPARE(spy.at(0).at(0).toBool(), false);

        QFile check(filePath);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("replacement"));
        check.close();
        QVERIFY(QFile::exists(trashedPath));

        QFile::remove(trashedPath);
        XdgTrash::removeInfo(trashedPath);
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

    // Real archives made by the matching tool; a zip renamed to .7z only
    // worked with tools that sniff the content, which 7-Zip 23+ refuses.
    void testArchiveSupportFor7zAndRar()
    {
        QFETCH(QString, extension);
        const QString tool = extension == ".7z" ? QStringLiteral("7z") : QStringLiteral("rar");
        if (QStandardPaths::findExecutable(tool).isEmpty())
            QSKIP(qPrintable(tool + " not found in PATH"));

        TestDir archiveDir;
        TestDir extractDir;
        archiveDir.createDir("payload");
        archiveDir.createFile("payload/inner.txt", "hello");
        const QString archivePath = archiveDir.path() + "/payload" + extension;
        QVERIFY(runCommand(tool, {"a", archivePath, "payload"}, archiveDir.path()));
        QVERIFY(QFile::exists(archivePath));

        FileOperations ops;
        QVERIFY(FileOperations::isArchive(archivePath));
        QVERIFY(FileOperations::isArchive(archiveDir.path() + "/payload" + extension.toUpper()));

        QSignalSpy spy(&ops, &FileOperations::operationFinished);
        ops.extractArchive(archivePath, extractDir.path());

        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(QFile::exists(extractDir.path() + "/payload/inner.txt"));
    }

    // A password-protected archive must fail fast through the placeholder
    // password (never block waiting for stdin), report "password required",
    // and succeed once the real password is handed to the retry.
    void testExtractPasswordProtectedArchive()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
            QSKIP("7z not found in PATH");

        TestDir archiveDir;
        TestDir extractDir;
        archiveDir.createDir("payload");
        archiveDir.createFile("payload/inner.txt", "secret");
        const QString archivePath = archiveDir.path() + "/locked_headers.7z";
        QVERIFY(runCommand("7z",
            {"a", "-ptest", "-mhe=on", archivePath, "payload"}, archiveDir.path()));
        QVERIFY(QFile::exists(archivePath));

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);
        QSignalSpy passwordSpy(&ops, &FileOperations::passwordRequested);

        ops.extractArchive(archivePath, extractDir.path(), QString());

        QVERIFY(passwordSpy.wait(5000));
        // Queued from the worker before it returns, so passwordRequested lands
        // ahead of operationFinished. Wait rather than assume the order.
        QTRY_COMPARE(finishSpy.count(), 1);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), false);
        QCOMPARE(finishSpy.at(0).at(1).toString(), QStringLiteral("password required"));
        QVERIFY(!QFile::exists(extractDir.path() + "/payload/inner.txt"));

        finishSpy.clear();
        ops.extractArchive(archivePath, extractDir.path(), QStringLiteral("test"));

        QVERIFY(finishSpy.wait(5000));
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);
        QVERIFY(QFile::exists(extractDir.path() + "/payload/inner.txt"));
    }

    // Same contract as the 7z case, through unzip, which is present far more
    // often than 7z (the runner has it and p7zip is a separate install). Keeps
    // the password path covered even where the 7z tests skip.
    void testExtractPasswordProtectedZip()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty())
            QSKIP("zip/unzip not found in PATH");

        TestDir archiveDir;
        TestDir extractDir;
        archiveDir.createDir("payload");
        archiveDir.createFile("payload/inner.txt", "secret");
        const QString archivePath = archiveDir.path() + "/locked.zip";
        QVERIFY(runCommand("zip", {"-q", "-P", "testpass", "-r", archivePath, "payload"},
                           archiveDir.path()));
        QVERIFY(QFile::exists(archivePath));

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);
        QSignalSpy passwordSpy(&ops, &FileOperations::passwordRequested);

        ops.extractArchive(archivePath, extractDir.path(), QString());

        QVERIFY(passwordSpy.wait(5000));
        // First ask: nothing was cached, so this is not a retry.
        QCOMPARE(passwordSpy.constFirst().at(2).toBool(), false);
        // passwordRequested is queued from the worker before it returns, so it
        // lands ahead of operationFinished. Wait rather than assume the order.
        QTRY_COMPARE(finishSpy.count(), 1);
        QCOMPARE(finishSpy.at(0).at(1).toString(), QStringLiteral("password required"));
        QVERIFY(!QFile::exists(extractDir.path() + "/payload/inner.txt"));

        // A wrong password must come back as a retry, so the dialog can say so.
        ops.cacheArchivePassword(archivePath, QStringLiteral("wrongpass"));
        passwordSpy.clear();
        finishSpy.clear();
        ops.extractArchive(archivePath, extractDir.path(), QStringLiteral("wrongpass"));

        QVERIFY(passwordSpy.wait(5000));
        QCOMPARE(passwordSpy.constFirst().at(2).toBool(), true);
        QVERIFY(ops.archivePassword(archivePath).isEmpty());

        finishSpy.clear();
        ops.extractArchive(archivePath, extractDir.path(), QStringLiteral("testpass"));

        QVERIFY(finishSpy.wait(5000));
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);
        QVERIFY(QFile::exists(extractDir.path() + "/payload/inner.txt"));
    }

    // Double-clicking an archive that already holds a single top-level folder
    // ("code.zip" -> "code/") used to land as code/code/: the destination is
    // named after the archive, and the tool recreates the archive's own root
    // inside it. The root is lifted up so the folder holds the files directly.
    void testExtractingAnAlreadyRootedArchiveDoesNotNest()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty())
            QSKIP("zip/unzip not found in PATH");

        TestDir archiveDir;
        archiveDir.createDir("code");
        archiveDir.createFile("code/inner.txt", "hello");
        archiveDir.createFile("code/.hidden", "hidden");
        const QString archivePath = archiveDir.path() + "/code.zip";
        QVERIFY(runCommand("zip", {"-q", "-r", archivePath, "code"}, archiveDir.path()));

        FileOperations ops;
        const QString dest = ops.newExtractionFolder(archivePath);
        QVERIFY(!dest.isEmpty());
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.extractArchive(archivePath, dest, QString());
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 10000);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);

        QVERIFY2(!QFileInfo::exists(dest + "/code"),
                 qPrintable(QStringLiteral("nested: %1/code").arg(dest)));
        QVERIFY(QFile::exists(dest + "/inner.txt"));
        QVERIFY(QFile::exists(dest + "/.hidden"));
    }

    // An archive holding several top-level entries keeps the folder made for
    // it: there is no single root to lift, and dropping the wrapper would
    // scatter the entries next to the archive.
    void testExtractingAMultiRootArchiveKeepsItsFolder()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty())
            QSKIP("zip/unzip not found in PATH");

        TestDir archiveDir;
        archiveDir.createDir("bundle");
        archiveDir.createDir("bundle/one");
        archiveDir.createFile("bundle/one/a.txt", "a");
        archiveDir.createFile("bundle/b.txt", "b");
        const QString archivePath = archiveDir.path() + "/bundle.zip";
        QVERIFY(runCommand("zip", {"-q", "-r", archivePath, "one", "b.txt"},
                           archiveDir.path() + "/bundle"));

        FileOperations ops;
        const QString dest = ops.newExtractionFolder(archivePath);
        QVERIFY(!dest.isEmpty());
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.extractArchive(archivePath, dest, QString());
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 10000);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);

        QVERIFY(QFile::exists(dest + "/b.txt"));
        QVERIFY(QFile::exists(dest + "/one/a.txt"));
    }

    // A refused extraction used to leave the destination behind: unzip creates
    // the directory entries before it discovers it cannot decrypt anything, so
    // a bare "locked/payload/" tree appeared next to the archive and looked
    // like the extraction had half worked.
    void testRefusedExtractionLeavesNoEmptyFolder()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("unzip")).isEmpty())
            QSKIP("zip/unzip not found in PATH");

        TestDir archiveDir;
        archiveDir.createDir("payload");
        archiveDir.createFile("payload/inner.txt", "secret");
        const QString archivePath = archiveDir.path() + "/locked.zip";
        QVERIFY(runCommand("zip", {"-q", "-P", "testpass", "-r", archivePath, "payload"},
                           archiveDir.path()));

        FileOperations ops;
        const QString dest = ops.newExtractionFolder(archivePath);
        QVERIFY(!dest.isEmpty());
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.extractArchive(archivePath, dest, QString());
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 10000);
        QCOMPARE(finishSpy.at(0).at(1).toString(), QStringLiteral("password required"));
        QVERIFY2(!QFileInfo::exists(dest),
                 qPrintable(QStringLiteral("left behind: %1").arg(dest)));

        // The retry after a wrong password reuses the same destination, so the
        // cleanup has to survive more than one attempt.
        finishSpy.clear();
        ops.extractArchive(archivePath, dest, QStringLiteral("wrongpass"));
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 10000);
        QCOMPARE(finishSpy.at(0).at(1).toString(), QStringLiteral("password required"));
        QVERIFY2(!QFileInfo::exists(dest),
                 qPrintable(QStringLiteral("retry left behind: %1").arg(dest)));

        // ...and a correct password still extracts into it.
        finishSpy.clear();
        ops.cacheArchivePassword(archivePath, QStringLiteral("testpass"));
        ops.extractArchive(archivePath, dest, QStringLiteral("testpass"));
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 10000);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);
        // The archive's own "payload/" root is lifted out of the folder made
        // for it, so the file lands directly in the destination.
        QVERIFY(QFile::exists(dest + "/inner.txt"));

        // The archive is done with, so the password does not outlive it: held
        // only while in use, the way Ark and File Roller scope it.
        QTRY_VERIFY(ops.archivePassword(archivePath).isEmpty());
    }

    // Extracting via an external binary must stop promptly when cancelled;
    // previously the transfer had no worker and the cancel was a no-op that
    // let the extraction run to completion.
    void testCancelStopsExtractionMidFlight()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
            QSKIP("7z not found in PATH");

        TestDir archiveDir;
        TestDir extractDir;
        const QString archivePath = createWideArchive(archiveDir, 300, 256 * 1024);
        QVERIFY2(!archivePath.isEmpty(), "failed to create wide 7z archive");

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);
        const int id = ops.extractArchive(archivePath, extractDir.path());
        QVERIFY(id >= 0);

        QTRY_VERIFY_WITH_TIMEOUT(ops.busy(), 5000);
        // Cancel once extraction has clearly started (7z created its output
        // folder but cannot be done yet), so the operation must end early and
        // report a failure rather than extracting every file.
        QTRY_VERIFY_WITH_TIMEOUT(
            QFileInfo(extractDir.path() + QLatin1String("/payload")).exists(), 10000);
        ops.cancelTransfer(id);
        QTRY_VERIFY_WITH_TIMEOUT(!ops.busy(), 8000);

        QCOMPARE(finishSpy.count(), 1);
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), false);

        int landed = 0;
        bool allValid = true;
        QDirIterator it(extractDir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            // Extracted names must match the archive's own payload/f<index>.dat
            const QString name = it.fileName();
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            bool ok = false;
            const int index = name.mid(1, dot - 1).toInt(&ok);
            if (!ok || index < 0 || index >= 300)
                allValid = false;
            ++landed;
        }
        QVERIFY2(landed < 300, qPrintable(
            QStringLiteral("cancel did not stop extraction: %1 files").arg(landed)));
        QVERIFY(allValid);
    }

    // Pausing an extraction freezes the underlying process, so the byte-based
    // progress must stop moving until it is resumed.
    void testPauseFreezesExtractProgress()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
            QSKIP("7z not found in PATH");

        TestDir archiveDir;
        TestDir extractDir;
        const QString archivePath = createWideArchive(archiveDir, 300, 256 * 1024);
        QVERIFY2(!archivePath.isEmpty(), "failed to create wide 7z archive");

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);
        const int id = ops.extractArchive(archivePath, extractDir.path());
        QVERIFY(id >= 0);

        QTRY_VERIFY_WITH_TIMEOUT(ops.busy(), 5000);
        QTRY_VERIFY(ops.progress() >= 0.0);

        ops.pauseTransfer(id);
        QVERIFY(ops.paused());
        const double frozen = ops.progress();
        QTest::qWait(800);
        const double later = ops.progress();
        QVERIFY2(later - frozen < 0.05, qPrintable(
            QStringLiteral("progress moved while paused: %1 -> %2").arg(frozen).arg(later)));
        ops.resumeTransfer(id);

        if (finishSpy.isEmpty())
            QVERIFY(finishSpy.wait(15000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
    }

    // tar.zst round trip: the format the context menu offers has to be one the
    // app can also open again (issue #34).
    void testCompressAndExtractTarZst()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("zstd")).isEmpty())
            QSKIP("zstd not found in PATH");

        TestDir dir;
        TestDir extractDir;
        dir.createDir("payload");
        dir.createFile("payload/inner.txt", "hello zstd");

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        QVERIFY(ops.compressFiles({dir.path() + "/payload"}, QStringLiteral("tar.zst")) >= 0);
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 20000);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);

        const QString archivePath = dir.path() + "/payload.tar.zst";
        QVERIFY2(QFile::exists(archivePath), qPrintable(archivePath));
        QVERIFY(FileOperations::isArchive(archivePath));

        finishSpy.clear();
        QVERIFY(ops.extractArchive(archivePath, extractDir.path()) >= 0);
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 20000);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), true);
        QVERIFY(QFile::exists(extractDir.path() + "/payload/inner.txt"));
    }

    // An archive nothing on this system can open used to fail in total silence:
    // extractArchive() returned -1 and the caller dropped it, so pressing Enter
    // on the file did nothing at all. It has to say why.
    void testUnsupportedArchiveReportsWhyItFailed()
    {
        TestDir dir;
        TestDir extractDir;
        // A suffix no extractor claims, so the "no tool for this" path runs
        // whatever happens to be installed on the machine running the test.
        dir.createFile("mystery.madeupzip", "not really an archive");
        const QString archivePath = dir.path() + "/mystery.madeupzip";

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        QCOMPARE(ops.extractArchive(archivePath, extractDir.path()), -1);
        QCOMPARE(finishSpy.count(), 1);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), false);
        const QString error = finishSpy.at(0).at(1).toString();
        QVERIFY2(!error.isEmpty(), "failed without saying anything");
        QVERIFY2(error.contains(QStringLiteral("Install"), Qt::CaseInsensitive),
                 qPrintable(error));
    }

    // Same for a format the compressor does not know.
    void testUnknownCompressFormatReportsWhyItFailed()
    {
        TestDir dir;
        dir.createFile("a.txt", "x");

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        QCOMPARE(ops.compressFiles({dir.path() + "/a.txt"}, QStringLiteral("rar")), -1);
        QCOMPARE(finishSpy.count(), 1);
        QCOMPARE(finishSpy.at(0).at(0).toBool(), false);
        QVERIFY2(finishSpy.at(0).at(1).toString().contains(QStringLiteral("rar")),
                 qPrintable(finishSpy.at(0).at(1).toString()));
    }

    void testProgressNeverExceedsOneHundredPercent_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QString>("tool");
        QTest::newRow("7z") << "7z" << ".7z" << "7z";
        QTest::newRow("zip") << "zip" << ".zip" << "zip";
        QTest::newRow("tar.gz") << "tar.gz" << ".tar.gz" << "tar";
        QTest::newRow("tar.zst") << "tar.zst" << ".tar.zst" << "zstd";
    }

    void testProgressNeverExceedsOneHundredPercent()
    {
        QFETCH(QString, format);
        QFETCH(QString, extension);
        QFETCH(QString, tool);
        if (QStandardPaths::findExecutable(tool).isEmpty())
            QSKIP(qPrintable(tool + " not found in PATH"));

        TestDir dir;
        TestDir extractDir;
        dir.createDir("payload");
        for (int i = 0; i < 12; ++i)
            dir.createFile(QStringLiteral("payload/f%1.dat").arg(i), QByteArray(120000, 'a' + i));

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);
        QVERIFY(ops.compressFiles({dir.path() + "/payload"}, format) >= 0);
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 30000);
        QVERIFY(finishSpy.at(0).at(0).toBool());
        finishSpy.clear();

        double worst = 0.0;
        QObject::connect(&ops, &FileOperations::activeTransfersChanged, &ops, [&ops, &worst]() {
            worst = qMax(worst, ops.progress());
        });

        QVERIFY(ops.extractArchive(dir.path() + "/payload" + extension,
                                   extractDir.path()) >= 0);
        QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() > 0, 30000);
        QVERIFY(finishSpy.at(0).at(0).toBool());

        qInfo() << format << "peak progress" << worst;
        QVERIFY2(worst <= 1.001, qPrintable(
            QStringLiteral("%1 reported %2%").arg(format).arg(worst * 100.0)));
    }

    // The progress bar reached 500% while extracting (issue #38): byte-based
    // progress counted every byte already in the destination — "Extract Here"
    // unpacks into the folder the archive itself sits in — against the
    // archive's own size. The count now starts from what was already there,
    // and the fraction is clamped so no future miscount can overshoot either.
    void testProgressFractionNeverExceedsFull_data()
    {
        QTest::addColumn<int>("current");
        QTest::addColumn<int>("total");
        QTest::addColumn<double>("expected");

        QTest::newRow("start") << 0 << 100 << 0.0;
        QTest::newRow("half") << 50 << 100 << 0.5;
        QTest::newRow("full") << 100 << 100 << 1.0;
        // The values the bar actually reported when it showed 387%.
        QTest::newRow("overshoot") << 9920 << 2560 << 1.0;
        QTest::newRow("negative") << -5 << 100 << 0.0;
        QTest::newRow("no total") << 5 << 0 << -1.0;
    }

    void testProgressFractionNeverExceedsFull()
    {
        QFETCH(int, current);
        QFETCH(int, total);
        QFETCH(double, expected);
        QCOMPARE(FileOperations::progressFraction(current, total), expected);
    }

    void testCompressionDoesNotExecuteFileNames_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("extension");
        QTest::addColumn<QString>("executable");

        QTest::newRow("zip") << "zip" << ".zip" << "zip";
        QTest::newRow("tar") << "tar" << ".tar" << "tar";
        QTest::newRow("tar.zst") << "tar.zst" << ".tar.zst" << "zstd";
    }

    void testCompressionDoesNotExecuteFileNames()
    {
        QFETCH(QString, format);
        QFETCH(QString, extension);
        QFETCH(QString, executable);
        if (QStandardPaths::findExecutable(executable).isEmpty())
            QSKIP(qPrintable(executable + " not found in PATH"));

        TestDir dir;
        const QString fileName = "payload'; touch PWNED; #.txt";
        const QString sourcePath = dir.createFile(fileName, "safe");
        const QString markerPath = dir.path() + "/PWNED";

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);
        ops.compressFiles({sourcePath}, format);

        QVERIFY(finishSpy.wait(5000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
        QVERIFY(!QFileInfo::exists(markerPath));
        QVERIFY(QFileInfo::exists(dir.path() + "/payload'; touch PWNED; #" + extension));
    }

    // --- Progress property ---

    void testProgressInitialValue()
    {
        FileOperations ops;
        QCOMPARE(ops.progress(), 0.0);
        QCOMPARE(ops.busy(), false);
        QCOMPARE(ops.statusText(), QString());
    }

    // --- Pause/Resume/Cancel ---

    void testPauseResumeTransfer()
    {
        TestDir src, dst;
        src.createFile("pause_test.bin", QByteArray(8 * 1024 * 1024, 'p'));

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/pause_test.bin"}, dst.path());

        QTest::qWait(50);
        if (ops.busy()) {
            ops.pauseTransfer();
            QVERIFY(ops.paused());
            QTest::qWait(100);
            ops.resumeTransfer();
        }

        if (finishSpy.isEmpty())
            QVERIFY(finishSpy.wait(15000));
        QCOMPARE(finishSpy.constFirst().at(0).toBool(), true);
        QVERIFY(QFile::exists(dst.path() + "/pause_test.bin"));
    }

    void testCancelTransfer()
    {
        TestDir src, dst;
        src.createFile("cancel_test.bin", QByteArray(16 * 1024 * 1024, 'c'));

        FileOperations ops;
        QSignalSpy finishSpy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({src.path() + "/cancel_test.bin"}, dst.path());

        QTest::qWait(50);
        if (ops.busy())
            ops.cancelTransfer();

        if (finishSpy.isEmpty())
            QVERIFY(finishSpy.wait(10000));
    }

    // --- Breadcrumbs ---

    void testBreadcrumbSegmentsStartAtHome()
    {
        FileOperations ops;
        const QString home = QDir::homePath();

        const QVariantList atHome = ops.breadcrumbSegments(home);
        QCOMPARE(atHome.size(), 1);
        QCOMPARE(atHome.at(0).toMap().value("label").toString(), QString("Home"));
        QCOMPARE(atHome.at(0).toMap().value("fullPath").toString(), home);

        const QVariantList nested = ops.breadcrumbSegments(home + "/Downloads/reports");
        QCOMPARE(nested.size(), 3);
        QCOMPARE(nested.at(0).toMap().value("label").toString(), QString("Home"));
        QCOMPARE(nested.at(0).toMap().value("fullPath").toString(), home);
        QCOMPARE(nested.at(1).toMap().value("label").toString(), QString("Downloads"));
        QCOMPARE(nested.at(1).toMap().value("fullPath").toString(), home + "/Downloads");
        QCOMPARE(nested.at(2).toMap().value("label").toString(), QString("reports"));
        QCOMPARE(nested.at(2).toMap().value("fullPath").toString(), home + "/Downloads/reports");
    }

    void testBreadcrumbSegmentsOutsideHomeKeepAncestors()
    {
        FileOperations ops;

        const QVariantList segments = ops.breadcrumbSegments("/usr/share");
        QCOMPARE(segments.size(), 2);
        QCOMPARE(segments.at(0).toMap().value("label").toString(), QString("usr"));
        QCOMPARE(segments.at(0).toMap().value("fullPath").toString(), QString("/usr"));
        QCOMPARE(segments.at(1).toMap().value("label").toString(), QString("share"));
        QCOMPARE(segments.at(1).toMap().value("fullPath").toString(), QString("/usr/share"));
    }

    void testBreadcrumbSegmentsDoNotMatchHomePrefix()
    {
        FileOperations ops;
        const QString home = QDir::homePath();

        // A sibling directory that merely starts with the same characters is
        // not inside home, so it must not collapse to a "Home" crumb.
        const QVariantList segments = ops.breadcrumbSegments(home + "-backup");
        QVERIFY(!segments.isEmpty());
        QVERIFY(segments.at(0).toMap().value("label").toString() != QStringLiteral("Home"));
        QCOMPARE(segments.last().toMap().value("fullPath").toString(), home + "-backup");
    }
};

QTEST_MAIN(TestFileOperations)
#include "tst_fileoperations.moc"

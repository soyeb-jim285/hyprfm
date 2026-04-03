#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>
#include "models/filesystemmodel.h"
#include "testdir.h"

class TestFileSystemModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // 1. Initial state
    void testInitialState()
    {
        FileSystemModel model;
        QVERIFY(model.rootPath().isEmpty());
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.fileCount(), 0);
        QCOMPARE(model.folderCount(), 0);
        QCOMPARE(model.showHidden(), false);
    }

    // 2. homePath()
    void testHomePath()
    {
        FileSystemModel model;
        QCOMPARE(model.homePath(), QDir::homePath());
    }

    // 3. setRootPath loads entries and emits rootPathChanged
    void testSetRootPath()
    {
        TestDir dir;
        dir.createFile("alpha.txt", "hello");
        dir.createFile("beta.txt", "world");
        dir.createDir("subdir");

        FileSystemModel model;
        QSignalSpy rootSpy(&model, &FileSystemModel::rootPathChanged);
        QSignalSpy countSpy(&model, &FileSystemModel::countsChanged);

        model.setRootPath(dir.path());

        QCOMPARE(rootSpy.count(), 1);
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(model.rootPath(), dir.path());
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.fileCount(), 2);
        QCOMPARE(model.folderCount(), 1);
    }

    // 4. setRootPath same path emits no signal
    void testSetRootPathSamePath()
    {
        TestDir dir;
        dir.createFile("file.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QSignalSpy rootSpy(&model, &FileSystemModel::rootPathChanged);
        model.setRootPath(dir.path());

        QCOMPARE(rootSpy.count(), 0);
    }

    // 5. Empty root path yields 0 rows
    void testEmptyRootPath()
    {
        TestDir dir;
        dir.createFile("file.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        QVERIFY(model.rowCount() > 0);

        model.setRootPath(QString());
        QCOMPARE(model.rowCount(), 0);
    }

    // 6. Empty directory yields 0 rows, 0 counts
    void testEmptyDirectory()
    {
        TestDir dir;

        FileSystemModel model;
        model.setRootPath(dir.path());

        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.fileCount(), 0);
        QCOMPARE(model.folderCount(), 0);
    }

    void testUnifiedTrashRoot()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString fileName = "hyprfm-trash-model-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".txt";
        const QString dirPath = QDir::homePath() + "/.cache/hyprfm-test-trash-model";
        QDir().mkpath(dirPath);
        const QString filePath = dirPath + "/" + fileName;

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("trash model");
        file.close();

        QProcess trashProc;
        trashProc.start("gio", {"trash", filePath});
        if (!trashProc.waitForFinished(5000) || trashProc.exitCode() != 0)
            QSKIP("gio trash failed in this environment");

        FileSystemModel model;
        model.setRootPath("trash:///");

        QString foundUri;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.fileName(i) == fileName) {
                foundUri = model.filePath(i);
                break;
            }
        }

        QVERIFY(!foundUri.isEmpty());
        QVERIFY(foundUri.startsWith("trash:///"));

        const QVariantMap props = model.fileProperties(foundUri);
        QCOMPARE(props.value("name").toString(), fileName);
        QCOMPARE(props.value("originalPath").toString(), filePath);
        QVERIFY(props.value("isTrashItem").toBool());

        QProcess removeProc;
        removeProc.start("gio", {"remove", "-f", foundUri});
        QVERIFY(removeProc.waitForFinished(5000));
    }

    // 7. Hidden files
    void testHiddenFilesNotShownByDefault()
    {
        TestDir dir;
        dir.createFile("visible.txt");
        dir.createFile(".hidden");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QCOMPARE(model.rowCount(), 1);

        QSignalSpy spy(&model, &FileSystemModel::showHiddenChanged);
        model.setShowHidden(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.rowCount(), 2);
    }

    void testHiddenFilesToggleBack()
    {
        TestDir dir;
        dir.createFile("visible.txt");
        dir.createFile(".hidden");

        FileSystemModel model;
        model.setRootPath(dir.path());
        model.setShowHidden(true);
        QCOMPARE(model.rowCount(), 2);

        model.setShowHidden(false);
        QCOMPARE(model.rowCount(), 1);
    }

    void testSetShowHiddenSameValueNoSignal()
    {
        FileSystemModel model;
        QCOMPARE(model.showHidden(), false);

        QSignalSpy spy(&model, &FileSystemModel::showHiddenChanged);
        model.setShowHidden(false);
        QCOMPARE(spy.count(), 0);

        model.setShowHidden(true);
        QCOMPARE(spy.count(), 1);

        model.setShowHidden(true);
        QCOMPARE(spy.count(), 1);
    }

    // 8. All role data
    void testRoleDataFileName()
    {
        TestDir dir;
        dir.createFile("hello.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, FileSystemModel::FileNameRole).toString(), QString("hello.txt"));
    }

    void testRoleDataFilePath()
    {
        TestDir dir;
        QString fullPath = dir.createFile("hello.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, FileSystemModel::FilePathRole).toString(), fullPath);
    }

    void testRoleDataFileSizeForFile()
    {
        TestDir dir;
        dir.createFile("data.txt", QByteArray(500, 'x'));

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, FileSystemModel::FileSizeRole).toLongLong(), qint64(500));
    }

    void testRoleDataFileSizeForDir()
    {
        TestDir dir;
        dir.createDir("subdir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        // Find the directory row
        int dirRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isDir(i)) { dirRow = i; break; }
        }
        QVERIFY(dirRow >= 0);
        QModelIndex idx = model.index(dirRow);
        QCOMPARE(model.data(idx, FileSystemModel::FileSizeRole).toLongLong(), qint64(-1));
    }

    void testRoleDataFileSizeText_data()
    {
        QTest::addColumn<int>("size");
        QTest::addColumn<QString>("expected");

        QTest::newRow("bytes")     << 512         << "512 B";
        QTest::newRow("kilobytes") << 2048         << "2.0 KB";
        QTest::newRow("megabytes") << (3 * 1024 * 1024) << "3.0 MB";
    }

    void testRoleDataFileSizeText()
    {
        QFETCH(int, size);
        QFETCH(QString, expected);

        TestDir dir;
        dir.createFile("file.bin", QByteArray(size, 'a'));

        FileSystemModel model;
        model.setRootPath(dir.path());

        QCOMPARE(model.rowCount(), 1);
        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, FileSystemModel::FileSizeTextRole).toString(), expected);
    }

    void testRoleDataFileSizeTextDirEmpty()
    {
        TestDir dir;
        dir.createDir("emptydir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        int dirRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isDir(i)) { dirRow = i; break; }
        }
        QVERIFY(dirRow >= 0);
        QModelIndex idx = model.index(dirRow);
        QCOMPARE(model.data(idx, FileSystemModel::FileSizeTextRole).toString(), QString());
    }

    void testRoleDataFileTypeFolder()
    {
        TestDir dir;
        dir.createDir("mydir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        int dirRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isDir(i)) { dirRow = i; break; }
        }
        QVERIFY(dirRow >= 0);
        QModelIndex idx = model.index(dirRow);
        QCOMPARE(model.data(idx, FileSystemModel::FileTypeRole).toString(), QString("folder"));
    }

    void testRoleDataFileTypeExtension()
    {
        TestDir dir;
        dir.createFile("image.PNG");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, FileSystemModel::FileTypeRole).toString(), QString("png"));
    }

    void testRoleDataFileModified()
    {
        TestDir dir;
        dir.createFile("mod.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QVariant v = model.data(idx, FileSystemModel::FileModifiedRole);
        QVERIFY(v.isValid());
        QVERIFY(v.toDateTime().isValid());
    }

    void testRoleDataFileModifiedText()
    {
        TestDir dir;
        dir.createFile("mod.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QString text = model.data(idx, FileSystemModel::FileModifiedTextRole).toString();
        QVERIFY(!text.isEmpty());
    }

    void testRoleDataFilePermissions()
    {
        TestDir dir;
        dir.createFile("perm.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QModelIndex idx = model.index(0);
        QString perms = model.data(idx, FileSystemModel::FilePermissionsRole).toString();
        QCOMPARE(perms.length(), 9);
        // Owner typically has read+write
        QVERIFY(perms.startsWith("rw"));
    }

    void testRoleDataIsDir()
    {
        TestDir dir;
        dir.createFile("file.txt");
        dir.createDir("subdir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        bool foundFile = false, foundDir = false;
        for (int i = 0; i < model.rowCount(); ++i) {
            QModelIndex idx = model.index(i);
            bool isDir = model.data(idx, FileSystemModel::IsDirRole).toBool();
            if (model.fileName(i) == "file.txt") {
                QVERIFY(!isDir);
                foundFile = true;
            } else if (model.fileName(i) == "subdir") {
                QVERIFY(isDir);
                foundDir = true;
            }
        }
        QVERIFY(foundFile);
        QVERIFY(foundDir);
    }

    void testRoleDataIsSymlink()
    {
        TestDir dir;
        QString target = dir.createFile("real.txt");
        dir.createSymlink(target, "link.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());

        bool foundSymlink = false;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.fileName(i) == "link.txt") {
                QModelIndex idx = model.index(i);
                QVERIFY(model.data(idx, FileSystemModel::IsSymlinkRole).toBool());
                foundSymlink = true;
            }
        }
        QVERIFY(foundSymlink);
    }

    void testRoleDataFileIconName_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<QString>("expectedIcon");

        QTest::newRow("png")  << "photo.png"  << "image-x-generic";
        QTest::newRow("jpg")  << "photo.jpg"  << "image-x-generic";
        QTest::newRow("mp3")  << "song.mp3"   << "audio-x-generic";
        QTest::newRow("mp4")  << "video.mp4"  << "video-x-generic";
        QTest::newRow("pdf")  << "doc.pdf"    << "application-pdf";
        QTest::newRow("zip")  << "arch.zip"   << "package-x-generic";
        QTest::newRow("txt")  << "readme.txt" << "text-x-generic";
        QTest::newRow("sh")   << "run.sh"     << "text-x-script";
        QTest::newRow("html") << "page.html"  << "text-html";
        QTest::newRow("css")  << "style.css"  << "text-css";
        QTest::newRow("xml")  << "data.xml"   << "text-xml";
        QTest::newRow("unknown") << "binary.xyz" << "text-x-generic";
    }

    void testRoleDataFileIconName()
    {
        QFETCH(QString, filename);
        QFETCH(QString, expectedIcon);

        TestDir dir;
        dir.createFile(filename);

        FileSystemModel model;
        model.setRootPath(dir.path());

        QCOMPARE(model.rowCount(), 1);
        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, FileSystemModel::FileIconNameRole).toString(), expectedIcon);
    }

    void testRoleDataFileIconNameDir()
    {
        TestDir dir;
        dir.createDir("mydir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        int dirRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isDir(i)) { dirRow = i; break; }
        }
        QVERIFY(dirRow >= 0);
        QModelIndex idx = model.index(dirRow);
        QCOMPARE(model.data(idx, FileSystemModel::FileIconNameRole).toString(), QString("folder"));
    }

    void testDirSizeTextRoleReturnsEmpty()
    {
        TestDir dir;
        dir.createDir("subdir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        int dirRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isDir(i)) { dirRow = i; break; }
        }
        QVERIFY(dirRow >= 0);
        QModelIndex idx = model.index(dirRow);
        // FileSizeTextRole returns empty string for directories
        QCOMPARE(model.data(idx, FileSystemModel::FileSizeTextRole).toString(), QString());
    }

    // 9. Invalid/out-of-bounds index returns empty QVariant
    void testInvalidIndexReturnsEmpty()
    {
        FileSystemModel model;

        QModelIndex invalid;
        QVERIFY(!model.data(invalid, FileSystemModel::FileNameRole).isValid());

        TestDir dir;
        dir.createFile("file.txt");
        model.setRootPath(dir.path());

        QModelIndex outOfBounds = model.index(999);
        QVERIFY(!model.data(outOfBounds, FileSystemModel::FileNameRole).isValid());
    }

    // 10. filePath(), isDir(), fileName() accessor methods
    void testAccessorMethodsValid()
    {
        TestDir dir;
        QString filePath = dir.createFile("test.txt");
        dir.createDir("mydir");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QCOMPARE(model.rowCount(), 2);

        // Find file and dir rows
        int fileRow = -1, dirRow = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.isDir(i))
                dirRow = i;
            else
                fileRow = i;
        }
        QVERIFY(fileRow >= 0);
        QVERIFY(dirRow >= 0);

        QCOMPARE(model.fileName(fileRow), QString("test.txt"));
        QCOMPARE(model.filePath(fileRow), filePath);
        QVERIFY(!model.isDir(fileRow));

        QCOMPARE(model.fileName(dirRow), QString("mydir"));
        QVERIFY(model.filePath(dirRow).endsWith("mydir"));
        QVERIFY(model.isDir(dirRow));
    }

    void testAccessorMethodsOutOfBounds()
    {
        FileSystemModel model;

        QVERIFY(model.filePath(-1).isEmpty());
        QVERIFY(model.filePath(0).isEmpty());
        QVERIFY(model.fileName(-1).isEmpty());
        QVERIFY(model.fileName(0).isEmpty());
        QVERIFY(!model.isDir(-1));
        QVERIFY(!model.isDir(0));
    }

    // 11. Sorting
    void testSortByNameAscending()
    {
        TestDir dir;
        dir.createFile("charlie.txt");
        dir.createFile("alpha.txt");
        dir.createFile("bravo.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        model.sortByColumn("name", true);

        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.fileName(0), QString("alpha.txt"));
        QCOMPARE(model.fileName(1), QString("bravo.txt"));
        QCOMPARE(model.fileName(2), QString("charlie.txt"));
    }

    void testSortByNameDescending()
    {
        TestDir dir;
        dir.createFile("charlie.txt");
        dir.createFile("alpha.txt");
        dir.createFile("bravo.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        model.sortByColumn("name", false);

        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.fileName(0), QString("charlie.txt"));
        QCOMPARE(model.fileName(1), QString("bravo.txt"));
        QCOMPARE(model.fileName(2), QString("alpha.txt"));
    }

    void testSortBySize()
    {
        TestDir dir;
        dir.createFile("small.txt", QByteArray(10, 'a'));
        dir.createFile("large.txt", QByteArray(1000, 'b'));
        dir.createFile("medium.txt", QByteArray(100, 'c'));

        FileSystemModel model;
        model.setRootPath(dir.path());
        model.sortByColumn("size", true);

        QCOMPARE(model.rowCount(), 3);
        // QDir::Size default order is largest first
        QCOMPARE(model.fileName(0), QString("large.txt"));
        QCOMPARE(model.fileName(1), QString("medium.txt"));
        QCOMPARE(model.fileName(2), QString("small.txt"));
    }

    void testSortDirsFirst()
    {
        TestDir dir;
        dir.createFile("zfile.txt");
        dir.createDir("asubdir");

        FileSystemModel model;
        model.setRootPath(dir.path());
        model.sortByColumn("name", true);

        QCOMPARE(model.rowCount(), 2);
        // dirs-first: subdir should come before file even though 'a' < 'z'
        QVERIFY(model.isDir(0));
        QVERIFY(!model.isDir(1));
    }

    void testSortUnknownColumnDefaultsToName()
    {
        TestDir dir;
        dir.createFile("zeta.txt");
        dir.createFile("alpha.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        model.sortByColumn("unknown_column", true);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.fileName(0), QString("alpha.txt"));
        QCOMPARE(model.fileName(1), QString("zeta.txt"));
    }

    // 12. refresh() detects externally added files
    void testRefreshDetectsNewFile()
    {
        TestDir dir;
        dir.createFile("existing.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        QCOMPARE(model.rowCount(), 1);

        dir.createFile("newfile.txt");
        model.refresh();

        QCOMPARE(model.rowCount(), 2);
    }

    // 13. fileProperties()
    void testFilePropertiesFile()
    {
        TestDir dir;
        QString path = dir.createFile("test.txt", QByteArray(200, 'x'));

        FileSystemModel model;
        QVariantMap props = model.fileProperties(path);

        QCOMPARE(props["name"].toString(), QString("test.txt"));
        QCOMPARE(props["size"].toLongLong(), qint64(200));
        QVERIFY(props["sizeText"].toString().contains("200"));
        QVERIFY(!props["mimeType"].toString().isEmpty());
        QVERIFY(!props["permissions"].toString().isEmpty());
        QCOMPARE(props["isDir"].toBool(), false);
        QCOMPARE(props["isSymlink"].toBool(), false);
    }

    void testFilePropertiesDir()
    {
        TestDir dir;
        QString subdir = dir.createDir("mydir");
        dir.createFile("mydir/file1.txt");
        dir.createFile("mydir/file2.txt");
        dir.createDir("mydir/subsubdir");

        FileSystemModel model;
        QVariantMap props = model.fileProperties(subdir);

        QCOMPARE(props["isDir"].toBool(), true);
        QCOMPARE(props["containedFiles"].toInt(), 2);
        QCOMPARE(props["containedFolders"].toInt(), 1);
        QCOMPARE(props["containedItems"].toInt(), 3);
    }

    void testFilePropertiesSymlink()
    {
        TestDir dir;
        QString target = dir.createFile("real.txt", "content");
        QString linkPath = dir.createSymlink(target, "link.txt");

        FileSystemModel model;
        QVariantMap props = model.fileProperties(linkPath);

        QCOMPARE(props["isSymlink"].toBool(), true);
        QCOMPARE(props["symlinkTarget"].toString(), target);
    }

    void testFilePropertiesOwnership()
    {
        TestDir dir;
        QString path = dir.createFile("owned.txt");

        FileSystemModel model;
        QVariantMap props = model.fileProperties(path);

        QVERIFY(props.contains("owner"));
        QVERIFY(props.contains("group"));
    }

    void testFilePropertiesTimestamps()
    {
        TestDir dir;
        QString path = dir.createFile("timed.txt");

        FileSystemModel model;
        QVariantMap props = model.fileProperties(path);

        QVERIFY(props.contains("created"));
        QVERIFY(props.contains("modified"));
        QVERIFY(props.contains("accessed"));
        QVERIFY(!props["modified"].toString().isEmpty());
    }

    void testFilePropertiesDiskUsage()
    {
        TestDir dir;
        QString path = dir.createFile("disk.txt");

        FileSystemModel model;
        QVariantMap props = model.fileProperties(path);

        // Disk info should be present (running on a real FS)
        QVERIFY(props.contains("diskTotal"));
        QVERIFY(props.contains("diskUsed"));
        QVERIFY(props.contains("diskFree"));
        QVERIFY(props.contains("diskUsedPercent"));
    }

    void testFilePropertiesAccessIndex()
    {
        TestDir dir;
        QString path = dir.createFile("access.txt");

        FileSystemModel model;
        QVariantMap props = model.fileProperties(path);

        QVERIFY(props.contains("ownerAccess"));
        QVERIFY(props.contains("groupAccess"));
        QVERIFY(props.contains("otherAccess"));
        // Owner typically has read+write = index 2
        int ownerAccess = props["ownerAccess"].toInt();
        QVERIFY(ownerAccess >= 1); // at least read
    }

    // 14. setFilePermissions()
    void testSetFilePermissionsReadOnly()
    {
        TestDir dir;
        QString path = dir.createFile("perms.txt", "data");

        FileSystemModel model;
        bool ok = model.setFilePermissions(path, 1, 0, 0); // owner read-only
        QVERIFY(ok);

        QFileInfo info(path);
        QVERIFY(info.isReadable());
        QVERIFY(!info.isWritable());

        // Restore write permission for cleanup
        model.setFilePermissions(path, 2, 0, 0);
    }

    void testSetFilePermissionsFullAccess()
    {
        TestDir dir;
        QString path = dir.createFile("fullperms.txt", "data");

        // First make it read-only
        model_setPermissionsHelper(path, 1, 0, 0);

        FileSystemModel model;
        bool ok = model.setFilePermissions(path, 2, 1, 1); // owner rw, group r, other r
        QVERIFY(ok);

        QFileInfo info(path);
        QVERIFY(info.isReadable());
        QVERIFY(info.isWritable());
    }

    // 15. roleNames()
    void testRoleNames()
    {
        FileSystemModel model;
        auto roles = model.roleNames();

        QCOMPARE(roles.count(), 11);
        QCOMPARE(roles[FileSystemModel::FileNameRole],         QByteArray("fileName"));
        QCOMPARE(roles[FileSystemModel::FilePathRole],         QByteArray("filePath"));
        QCOMPARE(roles[FileSystemModel::FileSizeRole],         QByteArray("fileSize"));
        QCOMPARE(roles[FileSystemModel::FileSizeTextRole],     QByteArray("fileSizeText"));
        QCOMPARE(roles[FileSystemModel::FileTypeRole],         QByteArray("fileType"));
        QCOMPARE(roles[FileSystemModel::FileModifiedRole],     QByteArray("fileModified"));
        QCOMPARE(roles[FileSystemModel::FileModifiedTextRole], QByteArray("fileModifiedText"));
        QCOMPARE(roles[FileSystemModel::FilePermissionsRole],  QByteArray("filePermissions"));
        QCOMPARE(roles[FileSystemModel::IsDirRole],            QByteArray("isDir"));
        QCOMPARE(roles[FileSystemModel::IsSymlinkRole],        QByteArray("isSymlink"));
        QCOMPARE(roles[FileSystemModel::FileIconNameRole],     QByteArray("fileIconName"));
    }

    // 16. QAbstractItemModelTester
    void testModelTesterEmpty()
    {
        FileSystemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        Q_UNUSED(tester);
        // Just constructing with no path should pass consistency checks
    }

    void testModelTesterWithData()
    {
        TestDir dir;
        dir.createFile("a.txt");
        dir.createFile("b.txt");
        dir.createDir("subdir");

        FileSystemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        model.setRootPath(dir.path());
        QCOMPARE(model.rowCount(), 3);
    }

    void testModelTesterAfterSort()
    {
        TestDir dir;
        dir.createFile("z.txt");
        dir.createFile("a.txt");
        dir.createDir("mydir");

        FileSystemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        model.setRootPath(dir.path());
        model.sortByColumn("name", true);
        model.sortByColumn("name", false);
    }

    void testModelTesterShowHidden()
    {
        TestDir dir;
        dir.createFile("visible.txt");
        dir.createFile(".hidden");

        FileSystemModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);

        model.setRootPath(dir.path());
        model.setShowHidden(true);
        model.setShowHidden(false);
    }

    // 17. Directory watcher detects new files
    void testDirectoryWatcherDetectsNewFile()
    {
        TestDir dir;
        dir.createFile("initial.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        QCOMPARE(model.rowCount(), 1);

        dir.createFile("added.txt");

        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 2, 5000);
    }

    void testDirectoryWatcherDetectsRemovedFile()
    {
        TestDir dir;
        dir.createFile("file1.txt");
        dir.createFile("file2.txt");

        FileSystemModel model;
        model.setRootPath(dir.path());
        QCOMPARE(model.rowCount(), 2);

        dir.removeFile("file1.txt");

        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
    }

    // 18. countsChanged signal emitted on setRootPath
    void testCountsChangedOnSetRootPath()
    {
        TestDir dir;
        dir.createFile("a.txt");
        dir.createFile("b.txt");
        dir.createDir("subdir");

        FileSystemModel model;
        QSignalSpy spy(&model, &FileSystemModel::countsChanged);

        model.setRootPath(dir.path());

        QVERIFY(spy.count() >= 1);
        QCOMPARE(model.fileCount(), 2);
        QCOMPARE(model.folderCount(), 1);
    }

    void testCountsChangedOnShowHiddenToggle()
    {
        TestDir dir;
        dir.createFile("visible.txt");
        dir.createFile(".hidden");

        FileSystemModel model;
        model.setRootPath(dir.path());

        QSignalSpy spy(&model, &FileSystemModel::countsChanged);
        model.setShowHidden(true);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(model.fileCount(), 2);
    }

private:
    // Helper to set permissions without a FileSystemModel instance
    void model_setPermissionsHelper(const QString &path, int ownerAccess, int groupAccess, int otherAccess)
    {
        FileSystemModel m;
        m.setFilePermissions(path, ownerAccess, groupAccess, otherAccess);
    }
};

QTEST_MAIN(TestFileSystemModel)
#include "tst_filesystemmodel.moc"

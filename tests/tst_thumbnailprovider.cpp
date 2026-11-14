#include <QTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QImage>
#include <QQuickTextureFactory>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include "providers/iconprovider.h"
#include "providers/thumbnailprovider.h"
#include <memory>
#include "testdir.h"

class TestThumbnailProvider : public QObject
{
    Q_OBJECT

private:
    QString createTestImage(TestDir &dir, const QString &name, int w, int h)
    {
        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(Qt::blue);
        QString path = dir.path() + "/" + name;
        img.save(path, "PNG");
        return path;
    }

    static void waitFinished(ThumbnailResponse &response)
    {
        // Same pattern as the tests below: run() starts in the constructor.
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);
    }

    static QImage folderImage(const QString &dir, IconProvider *icons = nullptr)
    {
        ThumbnailResponse response(dir + "?mtime=1", QSize(64, 64), icons);
        waitFinished(response);
        std::unique_ptr<QQuickTextureFactory> factory(response.textureFactory());
        return factory ? factory->image() : QImage();
    }

    QString findTrashEntryUri(const QString &originalPath)
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
            if (line.contains("trash::orig-path=" + originalPath))
                return line.section('\t', 0, 0).trimmed();
        }

        return {};
    }

private slots:
    void testUnreadableImageReportsError()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("empty.png");
        { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); }   // zero bytes

        ThumbnailResponse response(path, QSize(64, 64));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        QVERIFY(spy.wait(5000));
        QVERIFY(response.textureFactory() == nullptr);
        QVERIFY(!response.errorString().isEmpty());
    }

    void testLoadValidImage()
    {
        TestDir dir;
        QString path = createTestImage(dir, "test.png", 200, 200);

        ThumbnailResponse response(path, QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);

        QImage result = factory->image();
        QVERIFY(result.width() <= 80);
        QVERIFY(result.height() <= 80);

        delete factory;
    }

    void testDoesNotScaleUp()
    {
        TestDir dir;
        QString path = createTestImage(dir, "small.png", 30, 30);

        ThumbnailResponse response(path, QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);

        QImage result = factory->image();
        QCOMPARE(result.width(), 30);
        QCOMPARE(result.height(), 30);

        delete factory;
    }

    void testLoadImageWithCacheBustingQuery()
    {
        TestDir dir;
        QString path = createTestImage(dir, "queried.png", 120, 120);

        ThumbnailResponse response(path + "?mtime=123", QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);

        QImage result = factory->image();
        QVERIFY(!result.isNull());
        QVERIFY(result.width() <= 80);
        QVERIFY(result.height() <= 80);

        delete factory;
    }

    void testDefaultSize()
    {
        TestDir dir;
        QString path = createTestImage(dir, "large.png", 500, 500);

        ThumbnailResponse response(path, QSize(-1, -1));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);

        QImage result = factory->image();
        QVERIFY(result.width() <= 80);
        QVERIFY(result.height() <= 80);

        delete factory;
    }

    void testAspectRatioPreserved()
    {
        TestDir dir;
        // 400x200 scaled into 80x80 box with aspect ratio: 80x40
        QString path = createTestImage(dir, "wide.png", 400, 200);

        ThumbnailResponse response(path, QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);

        QImage result = factory->image();
        QVERIFY(result.width() <= 80);
        QVERIFY(result.height() <= 41);

        delete factory;
    }

    void testMissingFile()
    {
        ThumbnailResponse response("/nonexistent/path.png", QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        // Should not crash; factory may return null or a factory for a null image
        QQuickTextureFactory *factory = response.textureFactory();
        // No crash is the main assertion; factory could be null for a missing file
        delete factory;
    }

    void testCorruptedFile()
    {
        TestDir dir;
        QString path = dir.createFile("corrupt.png", QByteArray("this is not a real PNG"));

        ThumbnailResponse response(path, QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        // Should not crash
        QQuickTextureFactory *factory = response.textureFactory();
        delete factory;
    }

    void testCustomRequestedSize()
    {
        TestDir dir;
        QString path = createTestImage(dir, "big.png", 500, 500);

        ThumbnailResponse response(path, QSize(120, 120));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);

        QImage result = factory->image();
        QVERIFY(result.width() <= 120);
        QVERIFY(result.height() <= 120);

        delete factory;
    }

    // Custom folder icons (#68): a folder id resolves to a picture file.
    void testFolderDirectoryFileRelativeIcon()
    {
        TestDir dir;
        createTestImage(dir, "art.png", 100, 100);
        dir.createFile(".directory", "[Desktop Entry]\nIcon=./art.png\n");
        QVERIFY(!folderImage(dir.path()).isNull());
    }

    void testFolderCoverFallback()
    {
        TestDir dir;
        QImage img(100, 100, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(dir.path() + "/cover.jpg", "JPG"));
        QVERIFY(!folderImage(dir.path()).isNull());

        // An Icon= that points nowhere falls through to the cover.
        dir.createFile(".directory", "[Desktop Entry]\nIcon=./missing.png\n");
        QVERIFY(!folderImage(dir.path()).isNull());
    }

    void testFolderGioCustomIcon()
    {
        TestDir dir;
        QVERIFY(QDir(dir.path()).mkdir("album"));
        const QString art = createTestImage(dir, "art.png", 100, 100);
        const int rc = QProcess::execute("gio", {"set", dir.path() + "/album",
            "metadata::custom-icon", QUrl::fromLocalFile(art).toString()});
        if (rc != 0)
            QSKIP("gio set metadata unavailable (no gvfs)");
        QVERIFY(!folderImage(dir.path() + "/album").isNull());
    }

    void testFolderNamedLikeImage()
    {
        TestDir dir;
        QVERIFY(QDir(dir.path()).mkdir("album.png"));
        createTestImage(dir, "album.png/folder.png", 100, 100);
        QVERIFY(!folderImage(dir.path() + "/album.png").isNull());
    }

    void testFolderWithoutIconReportsError()
    {
        TestDir dir;
        ThumbnailResponse response(dir.path() + "?mtime=1", QSize(64, 64));
        waitFinished(response);
        QVERIFY(response.textureFactory() == nullptr);
        QVERIFY(!response.errorString().isEmpty());
    }

    void testFolderIgnoresIconOutsideDesktopEntry()
    {
        TestDir dir;
        createTestImage(dir, "art.png", 100, 100);
        dir.createFile(".directory", "[Dolphin]\nIcon=./art.png\n");
        QVERIFY(folderImage(dir.path()).isNull());
    }

    void testFolderThemeIconName()
    {
        TestDir data;
        QVERIFY(QDir().mkpath(data.path() + "/icons/HyprfmTestTheme/scalable/places"));
        QFile svg(data.path() + "/icons/HyprfmTestTheme/scalable/places/hyprfm-test-folder.svg");
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
                  "<rect width='16' height='16' fill='red'/></svg>");
        svg.close();
        const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", data.path().toUtf8());
        IconProvider icons(QStringLiteral("HyprfmTestTheme"));
        qputenv("XDG_DATA_DIRS", oldDirs);

        TestDir dir;
        dir.createFile(".directory", "[Desktop Entry]\nIcon=hyprfm-test-folder\n");
        QVERIFY(!folderImage(dir.path(), &icons).isNull());

        // A name no theme has must not turn into a generic file icon.
        dir.createFile(".directory", "[Desktop Entry]\nIcon=hyprfm-no-such-icon\n");
        QVERIFY(folderImage(dir.path(), &icons).isNull());
    }

    void testProviderCreatesResponse()
    {
        ThumbnailProvider provider;
        QQuickImageResponse *response = provider.requestImageResponse("some/path.png", QSize(80, 80));
        QVERIFY(response != nullptr);

        // Wait for the async operation to finish before deleting
        QSignalSpy spy(response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        delete response;
    }

    void testLoadTrashedImage()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString dirPath = QDir::homePath() + "/.cache/hyprfm-test-trash-thumb-" + uniqueId;
        QDir().mkpath(dirPath);

        QImage img(200, 120, QImage::Format_ARGB32);
        img.fill(Qt::green);
        const QString filePath = dirPath + "/thumb.png";
        QVERIFY(img.save(filePath, "PNG"));

        QProcess trashProc;
        trashProc.start("gio", {"trash", filePath});
        if (!trashProc.waitForFinished(5000) || trashProc.exitCode() != 0)
            QSKIP("gio trash failed in this environment");

        const QString trashUri = findTrashEntryUri(filePath);
        if (trashUri.isEmpty())
            QSKIP("Could not find trashed image URI");

        ThumbnailResponse response(trashUri, QSize(80, 80));
        QSignalSpy spy(&response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(5000);

        QQuickTextureFactory *factory = response.textureFactory();
        QVERIFY(factory != nullptr);
        const QImage result = factory->image();
        QVERIFY(!result.isNull());
        QVERIFY(result.width() <= 80);
        QVERIFY(result.height() <= 80);
        delete factory;

        QProcess removeProc;
        removeProc.start("gio", {"remove", "-f", trashUri});
        removeProc.waitForFinished(5000);
        QDir(dirPath).removeRecursively();
    }
};

QTEST_MAIN(TestThumbnailProvider)
#include "tst_thumbnailprovider.moc"

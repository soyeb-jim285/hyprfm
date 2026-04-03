#include <QTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QImage>
#include <QQuickTextureFactory>
#include <QStandardPaths>
#include <QUuid>
#include "providers/thumbnailprovider.h"
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

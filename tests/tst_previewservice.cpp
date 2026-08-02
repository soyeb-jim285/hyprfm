#include <QTest>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "services/previewservice.h"

class TestPreviewService : public QObject
{
    Q_OBJECT

private:
    static bool batAvailable()
    {
        return !QStandardPaths::findExecutable("bat").isEmpty()
            || !QStandardPaths::findExecutable("batcat").isEmpty();
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
            if (line.contains("trash::orig-path=" + originalPath))
                return line.section('\t', 0, 0).trimmed();
        }

        return {};
    }

private slots:
    void testLoadTextPreview()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/notes.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("alpha\nbeta\ngamma\n");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 1024, 20);

        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("isBinary").toBool(), false);
        QVERIFY(preview.value("content").toString().contains("beta"));
        if (batAvailable()) {
            QCOMPARE(preview.value("usesBat").toBool(), true);
            QVERIFY(preview.value("html").toString().contains("alpha"));
        }
    }

    void testBinaryPreviewDetection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/blob.bin";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("89504e470d0a1a0a00000000"));
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 1024, 20);

        QCOMPARE(preview.value("isBinary").toBool(), true);
        QCOMPARE(preview.value("content").toString(), QString());
    }

    void testLargeTextPreviewIsByteBounded()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/large.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, 'a'));
        file.write("TAIL-MUST-NOT-BE-PREVIEWED");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 1024, 20);

        QCOMPARE(preview.value("error").toString(), QString());
        QVERIFY(preview.value("truncated").toBool());
        QCOMPARE(preview.value("content").toString().size(), 1024);
        QVERIFY(!preview.value("content").toString().contains("TAIL-MUST"));
        if (preview.value("usesBat").toBool())
            QVERIFY(!preview.value("html").toString().contains("TAIL-MUST"));
    }

    void testInvalidByteLimitIsClamped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/text.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 0, 20);
        QVERIFY(preview.value("truncated").toBool());
        QCOMPARE(preview.value("content").toString(), QStringLiteral("h"));
    }

    void testTextPreviewHasHardSafetyCap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/huge.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(256 * 1024, 'x'));
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 10 * 1024 * 1024, 10000);
        QVERIFY(preview.value("truncated").toBool());
        QCOMPARE(preview.value("content").toString().size(), 65536);
    }

    void testJsonLinesUsesSmallPreviewCap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/events.jsonl";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(128 * 1024, 'j'));
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 65536, 1000);
        QVERIFY(preview.value("truncated").toBool());
        QCOMPARE(preview.value("content").toString().size(), 16384);
    }

    void testDirectoryPreview()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QDir root(dir.path());
        QVERIFY(root.mkdir("Folder"));
        QFile file(root.filePath("alpha.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadDirectoryPreview(dir.path(), 20);
        const QStringList entries = preview.value("entries").toStringList();

        QVERIFY(entries.contains("Folder/"));
        QVERIFY(entries.contains("alpha.txt"));
    }

    void testArchivePreviewIsEntryBounded()
    {
        if (QStandardPaths::findExecutable("tar").isEmpty())
            QSKIP("tar not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const QString &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                    QStringLiteral("c.txt")}) {
            QFile file(dir.path() + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name.toUtf8());
        }

        const QString archivePath = dir.path() + "/files.tar";
        QProcess tar;
        tar.start("tar", {"-cf", archivePath, "-C", dir.path(),
                          "a.txt", "b.txt", "c.txt"});
        QVERIFY(tar.waitForFinished(5000));
        QCOMPARE(tar.exitCode(), 0);

        PreviewService service;
        const QVariantMap preview = service.loadArchivePreview(archivePath, 2);
        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("entries").toStringList().size(), 2);
        QVERIFY(preview.value("truncated").toBool());
    }

    void testLocalPreviewPathForRegularFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/doc.pdf";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("dummy");
        file.close();

        PreviewService service;
        QCOMPARE(service.localPreviewPath(path), path);
    }

    void testLoadPdfPreview()
    {
        PreviewService service;
        if (!service.pdfPreviewAvailable())
            QSKIP("PDF preview support is unavailable in this build");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/preview.pdf";
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&writer);
        painter.drawText(QPointF(72.0, 100.0), QStringLiteral("Preview Test"));
        painter.end();

        const QVariantMap preview = service.loadPdfPreview(path);
        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("localPath").toString(), path);
        QVERIFY(preview.value("pageCount").toInt() >= 1);
    }

    void testTrashTextPreview()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString dirPath = QDir::homePath() + "/.cache/hyprfm-test-preview-service-" + uniqueId;
        QDir().mkpath(dirPath);

        const QString filePath = dirPath + "/preview.txt";
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("trash preview text");
        file.close();

        QProcess trashProc;
        trashProc.start("gio", {"trash", filePath});
        if (!trashProc.waitForFinished(5000) || trashProc.exitCode() != 0)
            QSKIP("gio trash failed in this environment");

        const QString trashUri = findTrashEntryUri(filePath);
        if (trashUri.isEmpty())
            QSKIP("Could not find trashed file URI");

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(trashUri, 1024, 20);

        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("isBinary").toBool(), false);
        QVERIFY(preview.value("content").toString().contains("trash preview text"));
        if (batAvailable())
            QCOMPARE(preview.value("usesBat").toBool(), true);

        const QString cachedPath = service.localPreviewPath(trashUri);
        QVERIFY(!cachedPath.isEmpty());
        QVERIFY(QFileInfo::exists(cachedPath));

        QProcess removeProc;
        removeProc.start("gio", {"remove", "-f", trashUri});
        removeProc.waitForFinished(5000);
        QDir(dirPath).removeRecursively();
    }
};

QTEST_MAIN(TestPreviewService)
#include "tst_previewservice.moc"

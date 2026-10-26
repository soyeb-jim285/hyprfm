#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <functional>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif
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
    static constexpr const char *kRequester = "test";

    static bool batAvailable()
    {
        return !QStandardPaths::findExecutable("bat").isEmpty()
            || !QStandardPaths::findExecutable("batcat").isEmpty();
    }

    static bool md2htmlAvailable()
    {
        return !QStandardPaths::findExecutable("md2html").isEmpty();
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

    // Qt loads <img> through QQuickPixmap, which fetches http(s) URLs, so a
    // Markdown file from anywhere untrusted could report back that it had been
    // previewed and hand over the reader's IP. Remote images never reach the
    // rendered HTML; local ones stay, since they cost no network request.
    void testMarkdownPreviewDropsRemoteImages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/notes.md";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Title\n\n"
                   "![tracker](http://tracker.invalid/pixel.png)\n\n"
                   "![secure](https://tracker.invalid/pixel.png)\n\n"
                   "<img src=\"http://tracker.invalid/raw.png\">\n\n"
                   "<img src='//tracker.invalid/scheme-relative.png'>\n\n"
                   "![local](diagram.png)\n");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 4096, 40);
        const QString html = preview.value("html").toString();

        QCOMPARE(preview.value("markdown").toBool(), true);
        QVERIFY2(!html.contains("tracker.invalid"), qPrintable(html));
        QVERIFY(html.contains("diagram.png"));
        QVERIFY(html.contains("Title"));
    }

    // The Markdown branch renders the text the line cap already trimmed, so a
    // long document costs no more than it does through bat.
    // CSV/TSV come back as table rows, not bat output: quotes may hold the
    // delimiter, "" and newlines, a semicolon export is sniffed, and a read
    // that stops mid-row drops that row instead of showing half of it.
    void testDelimitedFilesPreviewAsTables()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&](const QString &name, const QByteArray &data) {
            QFile file(dir.filePath(name));
            if (file.open(QIODevice::WriteOnly))
                file.write(data);
            return file.fileName();
        };
        const auto rows = [](const QVariantMap &preview) {
            QList<QStringList> out;
            for (const QVariant &row : preview.value("table").toMap().value("rows").toList())
                out.append(row.toStringList());
            return out;
        };
        PreviewService service;

        QVariantMap preview = service.loadTextPreview(write("a.csv",
            "name,note\r\n\"Doe, Jane\",\"said \"\"hi\"\"\nthen left\"\r\nBob,\r\n"));
        QCOMPARE(preview.value("usesBat").toBool(), false);
        QCOMPARE(rows(preview), (QList<QStringList>{
            {"name", "note"}, {"Doe, Jane", "said \"hi\" then left"}, {"Bob", ""}}));
        QCOMPARE(preview.value("truncated").toBool(), false);

        preview = service.loadTextPreview(write("eu.csv", "a;b;c\n1,5;2;3\n"));
        QCOMPARE(rows(preview), (QList<QStringList>{{"a", "b", "c"}, {"1,5", "2", "3"}}));

        preview = service.loadTextPreview(write("t.tsv", "x\ty,z\n1\t2\n"));
        QCOMPARE(rows(preview), (QList<QStringList>{{"x", "y,z"}, {"1", "2"}}));

        // 16 bytes ends inside "row2": that half row is dropped.
        preview = service.loadTextPreview(write("cut.csv", "h1,h2\nrow1,v\nrow2,v\n"), 16);
        QCOMPARE(rows(preview), (QList<QStringList>{{"h1", "h2"}, {"row1", "v"}}));
        QCOMPARE(preview.value("truncated").toBool(), true);
    }

    // JSON is re-indented, not parsed: key order survives, braces and quotes
    // inside strings are left alone, a cut-off file still formats, and deep
    // nesting cannot blow the output up.
    void testJsonPreviewIsPrettyPrinted()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&](const QString &name, const QByteArray &data) {
            QFile file(dir.filePath(name));
            if (file.open(QIODevice::WriteOnly))
                file.write(data);
            return file.fileName();
        };
        PreviewService service;

        QVariantMap preview = service.loadTextPreview(write("a.json",
            "{\"z\":1,\"a\":[1,2,{}],\"s\":\"x{,}\\\"y\",\"e\":[]}"));
        QCOMPARE(preview.value("content").toString(), QStringLiteral(
            "{\n  \"z\": 1,\n  \"a\": [\n    1,\n    2,\n    {}\n  ],\n"
            "  \"s\": \"x{,}\\\"y\",\n  \"e\": []\n}"));

        // A read that stops mid-file still formats what it got.
        preview = service.loadTextPreview(write("cut.json", "{\"k\":[1,2,3,4]}"), 9);
        QCOMPARE(preview.value("content").toString(), QStringLiteral("{\n  \"k\": [\n    1,\n    2"));

        preview = service.loadTextPreview(write("deep.json", QByteArray(100000, '[')), 1024 * 1024, 400);
        QVERIFY(preview.value("content").toString().size() < 400 * 70);
        QCOMPARE(preview.value("truncated").toBool(), true);
    }

    void testCsvColumnsAreCapped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath("wide.csv"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(100000, ',') + "\n");
        file.close();
        PreviewService service;
        const QVariantMap table = service.loadTextPreview(file.fileName(), 1024 * 1024)
                                      .value("table").toMap();
        QCOMPARE(table.value("columnWidths").toList().size(), 200);
        QCOMPARE(table.value("rows").toList().constFirst().toStringList().size(), 200);
    }

    void testMarkdownPreviewHonoursTheLineCap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + "/long.md";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int line = 0; line < 400; ++line)
            file.write(QStringLiteral("- entry %1\n").arg(line).toUtf8());
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 1024 * 1024, 20);

        QCOMPARE(preview.value("markdown").toBool(), true);
        const QString html = preview.value("html").toString();
        QVERIFY(html.contains("entry 0"));
        QVERIFY2(!html.contains("entry 300"), qPrintable(html.left(200)));
    }

    // Runs fn on a worker thread and reports whether it returned in time.
    // A stuck thread is leaked on purpose: deleting a running QThread qFatals.
    static bool returnsWithin(int ms, std::function<void()> fn)
    {
        QThread *thread = QThread::create(std::move(fn));
        thread->start();
        const bool finished = thread->wait(ms);
        if (finished)
            delete thread;
        return finished;
    }

    void testAnsiToHtmlSurvivesBareEscape()
    {
        // bat passes non-CSI escapes (e.g. ESC ( B) through verbatim; the
        // converter must consume them instead of spinning forever.
        QString html;
        QVERIFY2(returnsWithin(3000, [&html]() {
            html = PreviewService::ansiToHtml(QByteArray("hi \x1b(B there\n"));
        }), "ansiToHtml did not return on a bare ESC byte");
        QVERIFY(html.contains("hi"));
        QVERIFY(html.contains("there"));
    }

    void testFifoPreviewDoesNotBlock()
    {
#ifdef Q_OS_WIN
        QSKIP("no FIFOs on Windows");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fifo = dir.path() + "/notes";
        QCOMPARE(::mkfifo(fifo.toLocal8Bit().constData(), 0600), 0);

        PreviewService service;
        QVariantMap preview;
        QVERIFY2(returnsWithin(3000, [&]() {
            preview = service.loadTextPreview(fifo, 1024, 20);
        }), "loadTextPreview blocked on a FIFO");
        QVERIFY(!preview.value("error").toString().isEmpty());
#endif
    }

    void testBatOutputIsBoundedByMaxBytes()
    {
        if (!batAvailable())
            QSKIP("bat not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/huge.json";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[");
        file.write(QByteArray(1024 * 1024, '1'));   // one 1 MB line
        file.write("]");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 1024, 20);
        QCOMPARE(preview.value("usesBat").toBool(), true);
        // highlighting 1 KB of input can never need anything near 64 KB of HTML
        QVERIFY2(preview.value("html").toString().size() < 64 * 1024,
                 qPrintable(QString::number(preview.value("html").toString().size())));
    }

    // Markdown is rendered rather than run through bat, so the preview pane
    // can show a styled document: by md2html, or by Qt's own Markdown reader
    // where md2html is not installed (always on Windows).
    void testMarkdownPreviewRendersHtml()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/README.md";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Title\n\nSome *emphasis* here.\n");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 4096, 100);

        QCOMPARE(preview.value("error").toString(), QString());
        QCOMPARE(preview.value("isBinary").toBool(), false);
        QCOMPARE(preview.value("markdown").toBool(), true);
        // QML routes the RichText branch off usesBat, so it must be set too.
        QCOMPARE(preview.value("usesBat").toBool(), true);
        const QString html = preview.value("html").toString();
        QVERIFY2(html.contains("<h1"), qPrintable(html.left(200)));
        QVERIFY2(html.contains("emphasis") && !html.contains("*emphasis*"), qPrintable(html.left(200)));
        // Raw source must not leak through as the body.
        QVERIFY2(!html.contains("# Title"), qPrintable(html.left(200)));
    }

    void testMarkdownTablesGetBorders()
    {
        if (!md2htmlAvailable())
            QSKIP("md2html not found in PATH");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/table.md";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("| a | b |\n|---|---|\n| 1 | 2 |\n");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 4096, 100);

        // Qt only draws a bordered grid when the tag carries a border
        // attribute; md2html emits a bare <table>.
        QVERIFY2(preview.value("html").toString().contains("border=\"1\""),
                 "table border attribute was not injected");
    }

    void testPlainTextIsNotTreatedAsMarkdown()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/notes.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# not a heading here\n");
        file.close();

        PreviewService service;
        const QVariantMap preview = service.loadTextPreview(path, 4096, 20);

        QCOMPARE(preview.value("markdown").toBool(), false);
        if (batAvailable()) {
            QCOMPARE(preview.value("usesBat").toBool(), true);
            QVERIFY(!preview.value("html").toString().contains("<h1"));
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

    void testTrashPreviewCacheKeepsOnlyCurrentFile()
    {
        if (QStandardPaths::findExecutable("gio").isEmpty())
            QSKIP("gio not found in PATH");

        const QString uniqueId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString dirPath = QDir::homePath() + "/.cache/hyprfm-test-preview-evict-" + uniqueId;
        QDir().mkpath(dirPath);
        QStringList uris;
        for (const char *name : {"one.txt", "two.txt"}) {
            const QString filePath = dirPath + "/" + name;
            QFile file(filePath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
            file.close();
            QProcess trashProc;
            trashProc.start("gio", {"trash", filePath});
            if (!trashProc.waitForFinished(5000) || trashProc.exitCode() != 0)
                QSKIP("gio trash failed in this environment");
            const QString uri = findTrashEntryUri(filePath);
            if (uri.isEmpty())
                QSKIP("Could not find trashed file URI");
            uris.append(uri);
        }
        QDir().rmdir(dirPath);

        PreviewService service;
        const QString first = service.localPreviewPath(uris.at(0));
        QVERIFY(QFile::exists(first));
        const QString second = service.localPreviewPath(uris.at(1));
        QVERIFY(QFile::exists(second));
        QVERIFY2(!QFile::exists(first), "previous trash preview copy was not evicted");
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

    // ── Async path ──

    void testRequestPreviewDeliversOffThread()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/note.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello async\n");
        f.close();

        PreviewService service;
        QSignalSpy spy(&service, &PreviewService::previewReady);
        service.requestPreview(kRequester, path, QStringLiteral("text"));

        // Must not have completed synchronously -- the whole point is that
        // the GUI thread returns immediately.
        QCOMPARE(spy.count(), 0);
        QVERIFY(spy.wait(10000));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), kRequester);
        QCOMPARE(spy.at(0).at(1).toString(), path);
        const QVariantMap data = spy.at(0).at(2).toMap();
        QVERIFY(data.contains("text"));
        QVERIFY(data.value("text").toMap().value("content").toString().contains("hello async"));
    }

    void testSupersededRequestIsDropped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        auto write = [&](const QString &name, const QByteArray &body) {
            const QString path = dir.path() + "/" + name;
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write(body);
            f.close();
            return path;
        };
        const QString first = write("first.txt", "FIRST");
        const QString second = write("second.txt", "SECOND");

        PreviewService service;
        QSignalSpy spy(&service, &PreviewService::previewReady);

        // Simulates holding an arrow key: the earlier request must never
        // reach QML, or the panel shows the wrong file's contents.
        service.requestPreview(kRequester, first, QStringLiteral("text"));
        service.requestPreview(kRequester, second, QStringLiteral("text"));

        QVERIFY(spy.wait(10000));
        QTest::qWait(500);   // give any stale worker time to misbehave

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toString(), second);
    }

    void testCancelPreviewSuppressesResult()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/cancelled.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("nobody sees this");
        f.close();

        PreviewService service;
        QSignalSpy spy(&service, &PreviewService::previewReady);
        service.requestPreview(kRequester, path, QStringLiteral("text"));
        service.cancelPreview(kRequester);

        QTest::qWait(1000);
        QCOMPARE(spy.count(), 0);
    }

    void testRequestersDoNotCancelEachOther()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/shared.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("shared");
        f.close();

        PreviewService service;
        QSignalSpy spy(&service, &PreviewService::previewReady);

        // The Miller column and the quick-preview overlay share one service.
        // With a single global generation counter, whichever asked first
        // would be silently dropped and its panel would stay blank.
        service.requestPreview(QStringLiteral("miller"), path, QStringLiteral("text"));
        service.requestPreview(QStringLiteral("quick"), path, QStringLiteral("text"));

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 10000);

        QStringList requesters{spy.at(0).at(0).toString(), spy.at(1).at(0).toString()};
        requesters.sort();
        QCOMPARE(requesters, (QStringList{"miller", "quick"}));
    }
};

QTEST_MAIN(TestPreviewService)
#include "tst_previewservice.moc"

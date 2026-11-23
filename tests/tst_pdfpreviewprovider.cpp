#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QThreadPool>
#include <QPainter>
#include <QPdfWriter>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QDir>
#include <QTemporaryDir>
#include <QUrl>

#include "providers/pdfpreviewprovider.h"

class TestPdfPreviewProvider : public QObject
{
    Q_OBJECT

private:
    static QString createPdf(const QString &dirPath)
    {
        const QString filePath = dirPath + "/sample.pdf";
        QPdfWriter writer(filePath);
        writer.setPageSize(QPageSize(QPageSize::A4));

        QPainter painter(&writer);
        painter.setPen(Qt::black);
        painter.setFont(QFont(QStringLiteral("Sans"), 18));
        painter.drawText(QPointF(96.0, 120.0), QStringLiteral("HyprFM PDF Preview"));
        painter.end();
        return filePath;
    }

    static QImage renderAndWait(PdfPreviewProvider &provider, const QString &id,
                                const QSize &size)
    {
        QQuickImageResponse *response = provider.requestImageResponse(id, size);
        QSignalSpy spy(response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            spy.wait(15000);
        QQuickTextureFactory *factory = response->textureFactory();
        const QImage image = factory ? factory->image() : QImage();
        delete factory;
        delete response;
        return image;
    }

private slots:
    void testProviderRendersPdfPage()
    {
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("poppler-utils (pdftoppm/pdfinfo) not installed");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString pdfPath = createPdf(dir.path());

        PdfPreviewProvider provider;
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(pdfPath)) + "?page=0";
        QQuickImageResponse *response = provider.requestImageResponse(id, QSize(400, 500));
        QVERIFY(response != nullptr);

        QSignalSpy spy(response, &QQuickImageResponse::finished);
        if (spy.isEmpty())
            QVERIFY(spy.wait(5000));

        QQuickTextureFactory *factory = response->textureFactory();
        QVERIFY(factory != nullptr);
        const QImage image = factory->image();
        QVERIFY(!image.isNull());
        QVERIFY(image.width() > 0);
        QVERIFY(image.height() > 0);

        delete factory;
        delete response;
    }

    // QML requests the same page more than once on first paint (an Image's
    // sourceSize is part of Qt's cache key and changes as the item lays out),
    // and the adjacent-page prefetch adds more. Those requests are issued
    // concurrently, so serialising work for a mount lets them share the cache.
    void testConcurrentRequestsForSamePageAgree()
    {
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("poppler-utils (pdftoppm/pdfinfo) not installed");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdfPath = createPdf(dir.path());
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(pdfPath)) + "?page=0";

        PdfPreviewProvider provider;

        QQuickImageResponse *a = provider.requestImageResponse(id, QSize(400, 500));
        QQuickImageResponse *b = provider.requestImageResponse(id, QSize(400, 500));
        QSignalSpy spyA(a, &QQuickImageResponse::finished);
        QSignalSpy spyB(b, &QQuickImageResponse::finished);
        if (spyA.isEmpty())
            QVERIFY(spyA.wait(15000));
        if (spyB.isEmpty())
            QVERIFY(spyB.wait(15000));

        QQuickTextureFactory *fa = a->textureFactory();
        QQuickTextureFactory *fb = b->textureFactory();
        QVERIFY(fa && fb);
        const QImage ia = fa->image();
        const QImage ib = fb->image();
        QVERIFY(!ia.isNull());
        QVERIFY(!ib.isNull());
        QCOMPARE(ia, ib);

        delete fa; delete fb; delete a; delete b;
    }

    void testSlowRenderDoesNotHoldQtOpen()
    {
#ifndef Q_OS_UNIX
        QSKIP("Requires a shell stub for pdftoppm");
#else
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("poppler-utils (pdftoppm/pdfinfo) not installed");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdf = createPdf(dir.path());
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(pdf)) + "?page=0";
        const QString started = dir.filePath("started");
        QFile renderer(dir.filePath("pdftoppm"));
        QVERIFY(renderer.open(QIODevice::WriteOnly));
        QVERIFY(renderer.write("#!/bin/sh\nprintf '%s\\n' started > \"${0%/*}/started\"\nexec sleep 30\n") > 0);
        renderer.close();
        QVERIFY(renderer.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray previousPath = qgetenv("PATH");
        qputenv("PATH", dir.path().toLocal8Bit() + ':' + previousPath);
        const auto restorePath = qScopeGuard([&] { qputenv("PATH", previousPath); });

        PdfPreviewProvider provider;
        auto *running = provider.requestImageResponse(id, QSize(400, 500));
        QSignalSpy runningFinished(running, &QQuickImageResponse::finished);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(started), 5000);

        // QCoreApplication waits for its global pool during shutdown.
        QVERIFY(QThreadPool::globalInstance()->waitForDone(500));

        auto *queued = provider.requestImageResponse(id, QSize(460, 520));
        QSignalSpy queuedFinished(queued, &QQuickImageResponse::finished);
        queued->cancel();
        QVERIFY(queuedFinished.wait(2000));
        QCOMPARE(runningFinished.size(), 0);
        delete queued;

        running->cancel();
        QVERIFY(runningFinished.wait(2000));
        delete running;

        // Cancellation must leave this page eligible to render again.
        qputenv("PATH", previousPath);
        QVERIFY(!renderAndWait(provider, id, QSize(400, 500)).isNull());
#endif
    }

    // finished() must reach a listener that connects after
    // requestImageResponse() returns, as QML's pixmap reader does. A cache hit
    // finishes on the worker thread at once; emitting there directly lost the
    // signal (an image that never arrives; the CI flake behind a 15 s "warm"
    // render). Sleep past the worker before connecting to force that order.
    void testFinishedReachesALateListener()
    {
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("poppler-utils (pdftoppm/pdfinfo) not installed");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(createPdf(dir.path())))
                           + "?page=0";
        PdfPreviewProvider provider;
        QVERIFY(!renderAndWait(provider, id, QSize(400, 500)).isNull());   // warm the cache

        QQuickImageResponse *response = provider.requestImageResponse(id, QSize(400, 500));
        QThread::msleep(200);
        QSignalSpy spy(response, &QQuickImageResponse::finished);
        QVERIFY(spy.wait(2000));
        QQuickTextureFactory *factory = response->textureFactory();
        QVERIFY(factory && !factory->image().isNull());
        delete factory;
        delete response;
    }

    // A page flip must come back from the cache rather than re-running
    // pdftoppm, which measured ~70 ms per page.
    void testRepeatRequestIsServedFromCache()
    {
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("poppler-utils (pdftoppm/pdfinfo) not installed");

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdfPath = createPdf(dir.path());
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(pdfPath)) + "?page=0";

        PdfPreviewProvider provider;

        QElapsedTimer timer;
        timer.start();
        const QImage first = renderAndWait(provider, id, QSize(400, 500));
        const qint64 coldMs = timer.elapsed();
        QVERIFY(!first.isNull());

        timer.restart();
        const QImage second = renderAndWait(provider, id, QSize(400, 500));
        const qint64 warmMs = timer.elapsed();

        QCOMPARE(second, first);
        // Deliberately loose: this asserts "no subprocess ran", not a
        // specific speedup, so it does not flake on a loaded machine.
        QVERIFY2(warmMs <= coldMs, qPrintable(QStringLiteral("cold %1 ms, warm %2 ms")
                                                  .arg(coldMs).arg(warmMs)));
    }

    // A file that merely ends in .pdf must not cost a pdftoppm run: MIME
    // detection falls back to the extension when the content says nothing, so
    // a directory of such files used to spawn (and serialise) one subprocess
    // each, leaving the view blank meanwhile.
    void testNonPdfContentIsRejectedWithoutRunningPoppler()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fakePath = dir.filePath(QStringLiteral("not-really.pdf"));
        QFile fake(fakePath);
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write(QByteArray(4096, '\x01'));
        fake.close();

        PdfPreviewProvider provider;
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(fakePath)) + "?page=0";

        QElapsedTimer timer;
        timer.start();
        const QImage image = renderAndWait(provider, id, QSize(400, 500));
        const qint64 elapsed = timer.elapsed();

        QVERIFY(image.isNull());
        // Rejecting on the header is a 1 KB read; spawning pdftoppm and
        // waiting for it to fail is tens of milliseconds.
        QVERIFY2(elapsed < 50, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
    }

    // The rendered page outlives the process, so a second run of the app does
    // not re-render what it already has.
    void testRenderedPageSurvivesANewProvider()
    {
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("poppler-utils (pdftoppm/pdfinfo) not installed");

        QTemporaryDir cacheDir;
        QVERIFY(cacheDir.isValid());
        const QByteArray previousCacheHome = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", cacheDir.path().toLocal8Bit());
        const auto restore = qScopeGuard([&] {
            if (previousCacheHome.isEmpty())
                qunsetenv("XDG_CACHE_HOME");
            else
                qputenv("XDG_CACHE_HOME", previousCacheHome);
        });

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pdfPath = createPdf(dir.path());
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(pdfPath)) + "?page=0";

        QImage first;
        {
            PdfPreviewProvider provider;
            first = renderAndWait(provider, id, QSize(400, 500));
        }
        QVERIFY(!first.isNull());
        QVERIFY(!QDir(cacheDir.path() + QStringLiteral("/hyprfm/pdf-pages"))
                     .entryList({QStringLiteral("*.jpg")}, QDir::Files).isEmpty());

        // A provider with an empty in-memory cache still answers from disk.
        PdfPreviewProvider fresh;
        const QImage second = renderAndWait(fresh, id, QSize(400, 500));
        QCOMPARE(second.size(), first.size());
    }
};

QTEST_MAIN(TestPdfPreviewProvider)
#include "tst_pdfpreviewprovider.moc"

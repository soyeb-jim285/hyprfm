#include <QTest>
#include <QPainter>
#include <QPdfWriter>
#include <QQuickTextureFactory>
#include <QSignalSpy>
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

private slots:
    void testProviderRendersPdfPage()
    {
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
};

QTEST_MAIN(TestPdfPreviewProvider)
#include "tst_pdfpreviewprovider.moc"

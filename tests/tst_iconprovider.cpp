#include <QTest>
#include <QImage>
#include <QSize>
#include "providers/iconprovider.h"

class TestIconProvider : public QObject
{
    Q_OBJECT

private slots:
    void testConstruction()
    {
        // Should not crash with a valid theme name
        IconProvider provider("Adwaita");
        Q_UNUSED(provider);

        // Should not crash with a nonexistent theme name
        IconProvider provider2("nonexistent-theme");
        Q_UNUSED(provider2);
    }

    void testMissingIconReturnsImage()
    {
        IconProvider provider("Adwaita");
        QSize size;
        QImage img = provider.requestImage("completely-nonexistent-icon-xyz", &size, QSize(48, 48));
        QVERIFY(!img.isNull());
    }

    void testDefaultSizeWhenNotRequested()
    {
        IconProvider provider("Adwaita");
        QSize size;
        // QSize(-1,-1) should trigger the default 48x48
        QImage img = provider.requestImage("completely-nonexistent-icon-xyz", &size, QSize(-1, -1));
        QVERIFY(!img.isNull());
        QCOMPARE(img.width(), 48);
        QCOMPARE(img.height(), 48);
    }

    void testRequestedSize()
    {
        IconProvider provider("Adwaita");
        QSize size;
        QImage img = provider.requestImage("completely-nonexistent-icon-xyz", &size, QSize(24, 24));
        QVERIFY(!img.isNull());
        QCOMPARE(img.width(), 24);
        QCOMPARE(img.height(), 24);
    }

    void testColorTintParsing()
    {
        IconProvider provider("Adwaita");
        QSize size;
        // Should not crash and should return a non-null image
        QImage img = provider.requestImage("text-x-generic?color=#ff0000", &size, QSize(48, 48));
        QVERIFY(!img.isNull());
    }

    void testInvalidTintColor()
    {
        IconProvider provider("Adwaita");
        QSize size;
        // "notacolor" is not a valid QColor — should be handled gracefully
        QImage img = provider.requestImage("text-x-generic?color=notacolor", &size, QSize(48, 48));
        QVERIFY(!img.isNull());
    }

    void testSymbolicIconFallback()
    {
        IconProvider provider("Adwaita");
        QSize size;
        // A nonexistent symbolic icon should return a 1x1 transparent image
        // with *size set to QSize(0,0)
        QImage img = provider.requestImage("nonexistent-symbolic", &size, QSize(48, 48));
        QVERIFY(!img.isNull());
        QCOMPARE(size, QSize(0, 0));
        // The pixel should be transparent
        QCOMPARE(qAlpha(img.pixel(0, 0)), 0);
    }

    void testKnownIcon_data()
    {
        QTest::addColumn<QString>("iconName");
        QTest::newRow("folder")          << "folder";
        QTest::newRow("text-x-generic")  << "text-x-generic";
        QTest::newRow("image-x-generic") << "image-x-generic";
        QTest::newRow("audio-x-generic") << "audio-x-generic";
    }

    void testKnownIcon()
    {
        QFETCH(QString, iconName);
        IconProvider provider("Adwaita");
        QSize size;
        QImage img = provider.requestImage(iconName, &size, QSize(48, 48));
        // Even if the icon isn't installed, we should get a valid image back
        QVERIFY(!img.isNull());
        QCOMPARE(img.width(), 48);
        QCOMPARE(img.height(), 48);
    }

    void testMultipleQueryParams()
    {
        IconProvider provider("Adwaita");
        QSize size;
        // Extra unknown params should be silently ignored; color param still parsed
        QImage img = provider.requestImage("text-x-generic?color=#00ff00&other=value", &size, QSize(48, 48));
        QVERIFY(!img.isNull());
    }

    void testTintChangesPixels()
    {
        IconProvider provider("Adwaita");
        QSize size1, size2;

        QImage untinted = provider.requestImage("text-x-generic", &size1, QSize(48, 48));
        QImage tinted   = provider.requestImage("text-x-generic?color=#ff0000", &size2, QSize(48, 48));

        QVERIFY(!untinted.isNull());
        QVERIFY(!tinted.isNull());

        // Check whether the icon was actually found (i.e., has any opaque pixels).
        // If no opaque pixels are present the icon is not installed; skip the comparison.
        bool hasOpaquePixel = false;
        for (int y = 0; y < untinted.height() && !hasOpaquePixel; ++y)
            for (int x = 0; x < untinted.width() && !hasOpaquePixel; ++x)
                if (qAlpha(untinted.pixel(x, y)) > 0)
                    hasOpaquePixel = true;

        if (!hasOpaquePixel) {
            QSKIP("text-x-generic icon not found on this system; skipping tint pixel check");
        }

        // Every opaque pixel in the tinted image should have R=255, G=0, B=0
        bool foundTintedPixel = false;
        for (int y = 0; y < tinted.height(); ++y) {
            for (int x = 0; x < tinted.width(); ++x) {
                QRgb px = tinted.pixel(x, y);
                if (qAlpha(px) > 0) {
                    QCOMPARE(qRed(px),   255);
                    QCOMPARE(qGreen(px), 0);
                    QCOMPARE(qBlue(px),  0);
                    foundTintedPixel = true;
                }
            }
        }
        QVERIFY(foundTintedPixel);
    }
};

QTEST_MAIN(TestIconProvider)
#include "tst_iconprovider.moc"

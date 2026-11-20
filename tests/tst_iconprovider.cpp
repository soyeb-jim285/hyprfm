#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include "providers/iconprovider.h"

class TestIconProvider : public QObject
{
    Q_OBJECT

    static void writeFile(QTemporaryDir &dir, const QString &rel, const QByteArray &data)
    {
        QFileInfo fi(dir.filePath(rel));
        QDir().mkpath(fi.absolutePath());
        QFile f(fi.absoluteFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    static void writeSvg(QTemporaryDir &dir, const QString &rel, const QString &fill)
    {
        writeFile(dir, rel, QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                                           "<rect width='10' height='10' fill='%1'/></svg>").arg(fill).toUtf8());
    }

private slots:
    void testStrataAssociations_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<QString>("expected");
        QTest::newRow("rust") << "main.RS" << "strata-ctp-rust";
        QTest::newRow("cargo") << "Cargo.toml" << "strata-ctp-cargo";
        QTest::newRow("compound-extension") << "file.d.ts" << "strata-ctp-typescript-def";
        QTest::newRow("docker-affix") << "Dockerfile.unusual" << "strata-ctp-docker";
        QTest::newRow("private-key") << "id_ed25519" << "strata-key-round";
        QTest::newRow("unknown") << "unknown-file-42" << "";
    }

    void testStrataAssociations()
    {
        QFETCH(QString, filename);
        QFETCH(QString, expected);
        QCOMPARE(StrataIcons::fileIcon(filename), expected);
        QCOMPARE(StrataIcons::resolve("inode-directory", filename), QString("strata-folder"));
    }

    void testAllStrataIconsRender_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString &category : {QString("actions"), QString("catppuccin")}) {
            const auto names = QDir(":/HyprFM/icons/strata/" + category).entryList({"*.svg"});
            QVERIFY(!names.isEmpty());
            for (const QString &file : names) {
                const QString name = QFileInfo(file).completeBaseName();
                QTest::newRow(qPrintable(name)) << name;
            }
        }
    }

    void testAllStrataIconsRender()
    {
        QFETCH(QString, name);
        IconProvider provider("Strata");
        QSize size;
        const QImage image = provider.requestImage(name, &size, QSize(48, 48));
        QVERIFY(!StrataIcons::render(name, 48, QColor("#89b4fa"), QColor("#1e1e2e"), false, 48, false).isNull());
        QCOMPARE(size, QSize(48, 48));
        QVERIFY(!image.isNull());
        bool visible = false;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                visible |= image.pixelColor(x, y).alpha() > 0;
        QVERIFY(visible);
    }

    void testStrataPaletteAndCacheFollowThemeChanges()
    {
        IconProvider provider("Strata");
        QSize size;
        const auto render = [&](const QString &id) { return provider.requestImage(id, &size, QSize(48, 48)); };
        provider.setPalette(QColor("#ff0000"), QColor("#1e1e2e"), false);
        const QImage redFolder = render("folder");
        const QImage mocha = render("application-octet-stream?name=main.rs");
        provider.setPalette(QColor("#00ff00"), QColor("#1e1e2e"), false);
        QVERIFY(redFolder != render("folder"));
        QCOMPARE(mocha, render("application-octet-stream?name=main.rs"));
        provider.setPalette(QColor("#00ff00"), QColor("#eff1f5"), false);
        QVERIFY(mocha != render("application-octet-stream?name=main.rs"));
        provider.setPalette(QColor("#00ff00"), QColor("#eff1f5"), true);
        const QImage themed = render("application-octet-stream?name=main.rs");
        bool foundOpaque = false;
        for (int y = 0; y < themed.height(); ++y) {
            for (int x = 0; x < themed.width(); ++x) {
                const QColor pixel = themed.pixelColor(x, y);
                if (pixel.alpha() == 255) {
                    QCOMPARE(pixel, QColor("#00ff00"));
                    foundOpaque = true;
                }
            }
        }
        QVERIFY(foundOpaque);
        QVERIFY(themed != mocha);
        // Explicit URL colors win even before the application updates provider state.
        QCOMPARE(render("folder?accent=%23ff0000&background=%231e1e2e"), redFolder);
        QCOMPARE(render("text-plain?name=spaces%20%26%20hash%23.rs"), render("text-plain?name=main.rs"));
        // Distinct filenames that map to the same icon share the cached image instead of re-rendering per file.
        QCOMPARE(render("text-plain?name=first.rs").cacheKey(), render("text-plain?name=second.rs").cacheKey());
        QCOMPARE(render("inode-directory?name=FolderA").cacheKey(), render("inode-directory?name=FolderB").cacheKey());
    }

    void testConstruction()
    {
        // Should not crash with a valid theme name
        IconProvider provider("Adwaita");
        Q_UNUSED(provider);

        // Should not crash with a nonexistent theme name
        IconProvider provider2("nonexistent-theme");
        Q_UNUSED(provider2);
    }

    // Breeze-style themes ship one file per size; the closest size at or
    // above the request must win, not whichever directory is listed first.
    void testPrefersClosestLargerSizeDirectory()
    {
        QTemporaryDir dir;
        auto writeSvg = [&](const QString &rel, const QString &fill) {
            QFileInfo fi(dir.filePath(rel));
            QDir().mkpath(fi.absolutePath());
            QFile f(fi.absoluteFilePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                                   "<rect width='10' height='10' fill='%1'/></svg>").arg(fill).toUtf8());
        };
        writeSvg("icons/t/places/24/folder.svg", "#0000ff");
        writeSvg("icons/t/places/64/folder.svg", "#ff0000");
        writeSvg("icons/t/places/256/folder.svg", "#00ff00");
        qputenv("XDG_DATA_DIRS", dir.path().toUtf8());

        IconProvider provider("t");
        QSize size;
        QImage big = provider.requestImage("folder", &size, QSize(96, 96));
        QImage small = provider.requestImage("folder", &size, QSize(40, 40));
        qunsetenv("XDG_DATA_DIRS");
        QCOMPARE(big.pixelColor(48, 48), QColor("#00ff00"));    // 96 → 256, the smallest size at or above
        QCOMPARE(small.pixelColor(20, 20), QColor("#ff0000"));  // 40 → 64, never the 24 below it
    }

    // The URL's ?theme= wins over whatever setPrimaryTheme() last set, so a
    // live theme switch cannot serve stale icons under the new URLs.
    void testThemeInUrlOverridesCurrentTheme()
    {
        QTemporaryDir dir;
        auto writeSvg = [&](const QString &rel, const QString &fill) {
            QFileInfo fi(dir.filePath(rel));
            QDir().mkpath(fi.absolutePath());
            QFile f(fi.absoluteFilePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                                   "<rect width='10' height='10' fill='%1'/></svg>").arg(fill).toUtf8());
        };
        writeSvg("icons/one/places/64/folder.svg", "#ff0000");
        writeSvg("icons/two/places/64/folder.svg", "#00ff00");
        qputenv("XDG_DATA_DIRS", dir.path().toUtf8());

        IconProvider provider("one");
        QSize size;
        QCOMPARE(provider.requestImage("folder?theme=one", &size, QSize(32, 32)).pixelColor(16, 16), QColor("#ff0000"));
        QCOMPARE(provider.requestImage("folder?theme=two", &size, QSize(32, 32)).pixelColor(16, 16), QColor("#00ff00"));
        qunsetenv("XDG_DATA_DIRS");
    }

    // MacTahoe-style layout: colour icons in <category>/scalable, symbolic
    // ones in <category>/24; the colour one must win.
    void testFindsCategoryScalableBeforeSizedSymbolic()
    {
        QTemporaryDir dir;
        writeSvg(dir, "icons/t/places/24/folder.svg", "#0000ff");
        writeSvg(dir, "icons/t/places/scalable/folder.svg", "#ff0000");
        qputenv("XDG_DATA_DIRS", dir.path().toUtf8());
        IconProvider provider("t");
        QSize size;
        const QImage img = provider.requestImage("folder", &size, QSize(32, 32));
        qunsetenv("XDG_DATA_DIRS");
        QCOMPARE(img.pixelColor(16, 16), QColor("#ff0000"));
    }

    // A file icon the theme lacks comes from the theme it inherits, ahead
    // of the generic fallbacks (Adwaita here).
    void testInheritedThemeBeforeGenericFallbacks()
    {
        QTemporaryDir dir;
        writeFile(dir, "icons/child/index.theme", "[Icon Theme]\nName=child\nInherits=parent\n");
        writeSvg(dir, "icons/child/places/scalable/folder.svg", "#000000");
        writeSvg(dir, "icons/parent/mimetypes/scalable/text-plain.svg", "#00ff00");
        writeSvg(dir, "icons/Adwaita/mimetypes/scalable/text-plain.svg", "#ff0000");
        qputenv("XDG_DATA_DIRS", dir.path().toUtf8());
        IconProvider provider("child");
        QSize size;
        const QImage img = provider.requestImage("text-plain", &size, QSize(32, 32));
        qunsetenv("XDG_DATA_DIRS");
        QCOMPARE(img.pixelColor(16, 16), QColor("#00ff00"));
    }

    // MacTahoe names its shell script icon "shellscript": the theme's own
    // icon under that alias beats the fallback theme's exact name.
    void testPrimaryThemeAliasBeforeFallback()
    {
        QTemporaryDir dir;
        writeSvg(dir, "icons/t/mimes/scalable/shellscript.svg", "#00ff00");
        writeSvg(dir, "icons/Adwaita/mimetypes/scalable/text-x-shellscript.svg", "#ff0000");
        qputenv("XDG_DATA_DIRS", dir.path().toUtf8());
        IconProvider provider("t");
        QSize size;
        const QImage img = provider.requestImage("text-x-shellscript", &size, QSize(32, 32));
        qunsetenv("XDG_DATA_DIRS");
        QCOMPARE(img.pixelColor(16, 16), QColor("#00ff00"));
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

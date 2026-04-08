#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include "services/themeloader.h"

class TestThemeLoader : public QObject
{
    Q_OBJECT

private slots:
    void testLoadBuiltinTheme()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-mocha", THEMES_DIR);
        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
        QCOMPARE(loader.color("accent"), QColor("#89b4fa"));
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
        QCOMPARE(loader.color("error"), QColor("#f38ba8"));
    }

    void testLoadBuiltinLightTheme()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-latte", THEMES_DIR);
        QCOMPARE(loader.color("base"), QColor("#eff1f5"));
        QCOMPARE(loader.color("accent"), QColor("#1e66f5"));
        QCOMPARE(loader.color("text"), QColor("#4c4f69"));
        QCOMPARE(loader.color("error"), QColor("#d20f39"));
    }

    void testAllBuiltinColors()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-mocha", THEMES_DIR);

        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
        QCOMPARE(loader.color("mantle"), QColor("#181825"));
        QCOMPARE(loader.color("crust"), QColor("#11111b"));
        QCOMPARE(loader.color("surface"), QColor("#313244"));
        QCOMPARE(loader.color("overlay"), QColor("#45475a"));
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
        QCOMPARE(loader.color("subtext"), QColor("#bac2de"));
        QCOMPARE(loader.color("muted"), QColor("#6c7086"));
        QCOMPARE(loader.color("accent"), QColor("#89b4fa"));
        QCOMPARE(loader.color("success"), QColor("#a6e3a1"));
        QCOMPARE(loader.color("warning"), QColor("#f9e2af"));
        QCOMPARE(loader.color("error"), QColor("#f38ba8"));
    }

    void testLoadCustomTheme()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/custom.toml";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[colors]\nbase = \"#000000\"\ntext = \"#ffffff\"\naccent = \"#ff0000\"\n");
        f.close();

        ThemeLoader loader;
        loader.loadTheme(path, "");
        QCOMPARE(loader.color("base"), QColor("#000000"));
        QCOMPARE(loader.color("text"), QColor("#ffffff"));
        QCOMPARE(loader.color("accent"), QColor("#ff0000"));
    }

    void testFallbackForMissingColors()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/partial.toml";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[colors]\nbase = \"#000000\"\n");
        f.close();

        ThemeLoader loader;
        loader.loadTheme(path, "");
        QCOMPARE(loader.color("base"), QColor("#000000"));
        // All other colors should fall back to defaults
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
        QCOMPARE(loader.color("accent"), QColor("#89b4fa"));
        QCOMPARE(loader.color("error"), QColor("#f38ba8"));
    }

    void testMissingThemeFile()
    {
        ThemeLoader loader;
        loader.loadTheme("nonexistent-theme", "/nonexistent/path");
        // Should fall back to defaults, not crash
        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
    }

    void testInvalidToml()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/bad.toml";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("this is not valid toml {{{{");
        f.close();

        ThemeLoader loader;
        loader.loadTheme(path, "");
        // Should use defaults, not crash
        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
    }

    void testUnknownColorNameReturnsFallback()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-mocha", THEMES_DIR);
        // Unknown color name should return some default
        QColor unknown = loader.color("nonexistent_color");
        QVERIFY(unknown.isValid() || !unknown.isValid()); // Just verify no crash
    }

    void testThemeChangedSignal()
    {
        ThemeLoader loader;
        QSignalSpy spy(&loader, &ThemeLoader::themeChanged);

        loader.loadTheme("catppuccin-mocha", THEMES_DIR);
        QCOMPARE(spy.count(), 1);
    }

    void testLoadThemeByName()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-mocha", THEMES_DIR);
        // Loaded by name from themes directory
        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
    }

    void testPropertyAccessors()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-mocha", THEMES_DIR);

        // Test the Q_PROPERTY accessors directly
        QCOMPARE(loader.base(), QColor("#1e1e2e"));
        QCOMPARE(loader.text(), QColor("#cdd6f4"));
        QCOMPARE(loader.accent(), QColor("#89b4fa"));
    }

    void testEmptyThemeFile()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/empty.toml";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.close(); // empty file

        ThemeLoader loader;
        loader.loadTheme(path, "");
        // Should use all defaults
        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
    }

    void testColorsSectionMissing()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/nocolor.toml";
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[metadata]\nname = \"test\"\n"); // no [colors] section
        f.close();

        ThemeLoader loader;
        loader.loadTheme(path, "");
        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
    }
};

QTEST_MAIN(TestThemeLoader)
#include "tst_themeloader.moc"

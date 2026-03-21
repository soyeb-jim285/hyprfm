#include <QTest>
#include <QTemporaryDir>
#include <QFile>
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
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
    }
};
QTEST_MAIN(TestThemeLoader)
#include "tst_themeloader.moc"

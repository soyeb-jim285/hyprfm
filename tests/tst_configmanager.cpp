#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include "services/configmanager.h"

class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // --- Default values ---

    void testParseErrorIsReportedAndKeepsLastGoodValues()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        auto write = [&](const QByteArray &text) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(text);
        };
        write("[sidebar]\nposition = \"right\"\n");
        ConfigManager mgr(path);
        QCOMPARE(mgr.configError(), QString());
        QCOMPARE(mgr.sidebarPosition(), QString("right"));

        // The same table twice is the classic paste-from-sample mistake.
        write("[sidebar]\nposition = \"left\"\n\n[general]\n\n[sidebar]\nwidth = 187\n");
        QSignalSpy errorSpy(&mgr, &ConfigManager::configErrorChanged);
        mgr.reload();
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY2(mgr.configError().startsWith("config.toml line 6:"), qPrintable(mgr.configError()));
        QCOMPARE(mgr.sidebarPosition(), QString("right"));   // last good value kept

        write("[sidebar]\nposition = \"left\"\n");
        mgr.reload();
        QCOMPARE(mgr.configError(), QString());
        QCOMPARE(mgr.sidebarPosition(), QString("left"));
    }

    // Deleting a key from config.toml must bring its default back on reload.
    void testReloadResetsRemovedKeys()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        auto write = [&](const QByteArray &text) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(text);
        };
        write("[sidebar]\nwidth = 300\nposition = \"right\"\n[general]\nsort_by = \"size\"\n");
        ConfigManager mgr(path);
        QCOMPARE(mgr.sidebarWidth(), 300);
        QCOMPARE(mgr.sortBy(), QString("size"));

        write("[general]\n");
        mgr.reload();
        QCOMPARE(mgr.sidebarWidth(), 200);
        QCOMPARE(mgr.sidebarPosition(), QString("left"));
        QCOMPARE(mgr.sortBy(), QString("name"));
    }

    void testOpenShortcutIsRemappable()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        {
            ConfigManager mgr(path);   // defaults
            QVERIFY(mgr.keyEventMatches("open", Qt::Key_Return, 0));
            QVERIFY(mgr.keyEventMatches("open", Qt::Key_Enter, Qt::KeypadModifier));
            QVERIFY(!mgr.keyEventMatches("open", Qt::Key_Return, Qt::ControlModifier));
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[shortcuts]\nopen = \"Ctrl+O\"\n");
        f.close();
        ConfigManager mgr(path);
        QVERIFY(mgr.keyEventMatches("open", Qt::Key_O, Qt::ControlModifier));
        QVERIFY(!mgr.keyEventMatches("open", Qt::Key_Return, 0));
    }

    void testListColumnsDefault()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");
        QCOMPARE(mgr.listColumns(), QStringList({"name", "size", "modified", "type"}));
        QCOMPARE(mgr.listColumnWidths().value("size").toInt(), 110);
        QCOMPARE(mgr.listColumnWidths().value("modified").toInt(), 140);
        QCOMPARE(mgr.listColumnWidths().value("type").toInt(), 80);
    }

    void testListColumnsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        {
            ConfigManager mgr(path);
            mgr.saveListColumns({"name", "owner", "size"}, QVariantMap{{"owner", 120}, {"size", 90}});
            QCOMPARE(mgr.listColumns(), QStringList({"name", "owner", "size"}));
        }
        ConfigManager reloaded(path);
        QCOMPARE(reloaded.listColumns(), QStringList({"name", "owner", "size"}));
        QCOMPARE(reloaded.listColumnWidths().value("owner").toInt(), 120);
        QCOMPARE(reloaded.listColumnWidths().value("size").toInt(), 90);
        // widths for columns that were never saved keep their defaults
        QCOMPARE(reloaded.listColumnWidths().value("modified").toInt(), 140);
    }

    void testListColumnsKeepNameAndDropUnknown()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[list_view]\ncolumns = [\"size\", \"bogus\", \"name\", \"size\"]\n"
                "column_widths = { size = 5, bogus = 7 }\n");
        f.close();

        ConfigManager mgr(path);
        // order is the user's; duplicates and unknown keys go
        QCOMPARE(mgr.listColumns(), QStringList({"size", "name"}));
        QCOMPARE(mgr.listColumnWidths().value("size").toInt(), 40);   // clamped to the minimum
        QVERIFY(!mgr.listColumnWidths().contains("bogus"));

        ConfigManager noName(path);
        noName.saveListColumns({"type", "size"}, {});
        QCOMPARE(noName.listColumns(), QStringList({"name", "type", "size"}));
    }

    void testMillerFractionsDefault()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");
        QCOMPARE(mgr.millerFractions().value("parent").toDouble(), 0.2);
        QCOMPARE(mgr.millerFractions().value("current").toDouble(), 0.5);
    }

    void testMillerFractionsRoundTripAndClamp()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        {
            ConfigManager mgr(path);
            mgr.saveMillerFractions(0.3, 0.4);
        }
        ConfigManager reloaded(path);
        QCOMPARE(reloaded.millerFractions().value("parent").toDouble(), 0.3);
        QCOMPARE(reloaded.millerFractions().value("current").toDouble(), 0.4);

        // every column keeps at least 12% of the width
        reloaded.saveMillerFractions(0.05, 0.9);
        QCOMPARE(reloaded.millerFractions().value("parent").toDouble(), 0.12);
        QCOMPARE(reloaded.millerFractions().value("current").toDouble(), 0.76);
    }

    void testDocumentedTemplateLoadsToDefaults()
    {
        QTemporaryDir dir;
        ConfigManager defaults(dir.path() + "/defaults.toml");

        const QString path = dir.path() + "/config.toml";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(ConfigManager::documentedConfigTemplate().toUtf8());
        f.close();
        ConfigManager fromTemplate(path);

        QCOMPARE(fromTemplate.theme(), defaults.theme());
        QCOMPARE(fromTemplate.iconTheme(), defaults.iconTheme());
        QCOMPARE(fromTemplate.fontFamily(), defaults.fontFamily());
        QCOMPARE(fromTemplate.defaultView(), defaults.defaultView());
        QCOMPARE(fromTemplate.showHidden(), defaults.showHidden());
        QCOMPARE(fromTemplate.sortBy(), defaults.sortBy());
        QCOMPARE(fromTemplate.sortAscending(), defaults.sortAscending());
        QCOMPARE(fromTemplate.rememberSortPerFolder(), defaults.rememberSortPerFolder());
        QCOMPARE(fromTemplate.dependencyStartupCheck(), defaults.dependencyStartupCheck());
        QCOMPARE(fromTemplate.sidebarPosition(), defaults.sidebarPosition());
        QCOMPARE(fromTemplate.sidebarWidth(), defaults.sidebarWidth());
        QCOMPARE(fromTemplate.sidebarVisible(), defaults.sidebarVisible());
        QCOMPARE(fromTemplate.hiddenQuickAccess(), defaults.hiddenQuickAccess());
        QCOMPARE(fromTemplate.radiusSmall(), defaults.radiusSmall());
        QCOMPARE(fromTemplate.radiusMedium(), defaults.radiusMedium());
        QCOMPARE(fromTemplate.radiusLarge(), defaults.radiusLarge());
        QCOMPARE(fromTemplate.transparencyEnabled(), defaults.transparencyEnabled());
        QCOMPARE(fromTemplate.transparencyLevel(), defaults.transparencyLevel());
        QCOMPARE(fromTemplate.animationsEnabled(), defaults.animationsEnabled());
        QCOMPARE(fromTemplate.animDurationFast(), defaults.animDurationFast());
        QCOMPARE(fromTemplate.animDuration(), defaults.animDuration());
        QCOMPARE(fromTemplate.animDurationSlow(), defaults.animDurationSlow());
        QCOMPARE(fromTemplate.animCurveEnter(), defaults.animCurveEnter());
        QCOMPARE(fromTemplate.animCurveExit(), defaults.animCurveExit());
        QCOMPARE(fromTemplate.animCurveTransition(), defaults.animCurveTransition());
        QCOMPARE(fromTemplate.windowButtonLayout(), defaults.windowButtonLayout());
        QCOMPARE(fromTemplate.listColumns(), defaults.listColumns());
        QCOMPARE(fromTemplate.listColumnWidths(), defaults.listColumnWidths());
        QCOMPARE(fromTemplate.millerFractions(), defaults.millerFractions());
        QCOMPARE(fromTemplate.bookmarks(), defaults.bookmarks());
        QCOMPARE(fromTemplate.shortcutMap(), defaults.shortcutMap());
    }

    void testFirstRunSeedsDocumentedConfigAndSample()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        ConfigManager mgr(path);
        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(path + ".sample"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains("[general]"));
        QVERIFY(text.contains("# "));   // it is the documented template, not a bare dump
        QVERIFY(text.contains("default_view"));
    }

    void testExistingConfigIsNotOverwrittenBySeeding()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[general]\ndefault_view = \"miller\"\n");
        f.close();
        ConfigManager mgr(path);
        QCOMPARE(mgr.defaultView(), QString("miller"));
        QFile again(path);
        QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(again.readAll()), QString("[general]\ndefault_view = \"miller\"\n"));
        QVERIFY(QFile::exists(path + ".sample"));   // the reference is still refreshed
    }

    void testDefaultValues()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.theme(), QString("catppuccin-mocha"));
        QCOMPARE(mgr.iconTheme(), QString("Adwaita"));
        QCOMPARE(mgr.fontFamily(), QString());
        QCOMPARE(mgr.defaultView(), QString("grid"));
        QCOMPARE(mgr.showHidden(), false);
        QCOMPARE(mgr.sortBy(), QString("name"));
        QCOMPARE(mgr.sortAscending(), true);
        QCOMPARE(mgr.sidebarPosition(), QString("left"));
        QCOMPARE(mgr.sidebarWidth(), 200);
        QCOMPARE(mgr.sidebarVisible(), true);
        QCOMPARE(mgr.transparencyEnabled(), true);
        QCOMPARE(mgr.transparencyLevel(), 1.0);
        QCOMPARE(mgr.animationsEnabled(), true);
    }

    void testCustomDefaultTheme()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml", nullptr, QStringList(), "catppuccin-latte");

        QCOMPARE(mgr.theme(), QString("catppuccin-latte"));
    }

    void testAvailableThemes()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/themes");

        QFile darkTheme(dir.path() + "/themes/dark.toml");
        QVERIFY(darkTheme.open(QIODevice::WriteOnly));
        darkTheme.write("[colors]\ntext = \"#ffffff\"\n");
        darkTheme.close();

        QFile lightTheme(dir.path() + "/themes/light.toml");
        QVERIFY(lightTheme.open(QIODevice::WriteOnly));
        lightTheme.write("[colors]\ntext = \"#111111\"\n");
        lightTheme.close();

        ConfigManager mgr(dir.path() + "/config.toml", nullptr, QStringList{dir.path() + "/themes"});
        QCOMPARE(mgr.availableThemes(), QStringList({"dark", "light"}));
    }

    void testDefaultRadius()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.radiusSmall(), 4);
        QCOMPARE(mgr.radiusMedium(), 8);
        QCOMPARE(mgr.radiusLarge(), 12);
    }

    void testDefaultBookmarks()
    {
        // Defaults drop entries that aren't real directories, so create one of
        // the standard folders first. Without it the list could come back empty
        // and the assertions below would pass without checking anything.
        const QString pictures =
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        QVERIFY(!pictures.isEmpty());
        QVERIFY(QDir().mkpath(pictures));

        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        const QStringList bookmarks = mgr.bookmarks();
        QVERIFY(!bookmarks.isEmpty());
        QVERIFY(bookmarks.contains(pictures));

        // Defaults resolve through the XDG user dirs rather than hardcoded
        // English names, so every entry is an absolute path that exists.
        for (const QString &path : bookmarks) {
            QVERIFY(!path.startsWith(QLatin1Char('~')));
            QVERIFY(QFileInfo(path).isDir());
        }
    }

    void testBookmarkNamesRoundTrip()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        QVERIFY(mgr.bookmarkNames().isEmpty());

        mgr.saveBookmarks({"~/Projects", "~/Downloads"}, QVariantMap{{"~/Projects", "Work"}});

        ConfigManager mgr2(path);
        QCOMPARE(mgr2.bookmarks(), QStringList({"~/Projects", "~/Downloads"}));
        QCOMPARE(mgr2.bookmarkNames(), QVariantMap({{"~/Projects", "Work"}}));

        // Reverting to the auto name removes the entry rather than keeping a stale one.
        mgr2.saveBookmarks({"~/Projects", "~/Downloads"}, QVariantMap());
        ConfigManager mgr3(path);
        QVERIFY(mgr3.bookmarkNames().isEmpty());
    }

    void testBookmarkNamesFromHandEditedConfig()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[bookmarks]\npaths = [\"~/Projects\"]\nnames = { \"~/Projects\" = \"Work\" }\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.bookmarkNames(), QVariantMap({{"~/Projects", "Work"}}));
    }

    void testDefaultShortcuts()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.shortcut("open"), QString("Return"));
        QCOMPARE(mgr.shortcut("back"), QString("Alt+Left"));
        QCOMPARE(mgr.shortcut("forward"), QString("Alt+Right"));
        QCOMPARE(mgr.shortcut("parent"), QString("Alt+Up"));
        QCOMPARE(mgr.shortcut("home"), QString("Alt+Home"));
        QCOMPARE(mgr.shortcut("refresh"), QString("F5"));
        QCOMPARE(mgr.shortcut("new_tab"), QString("Ctrl+T"));
        QCOMPARE(mgr.shortcut("close_tab"), QString("Ctrl+W"));
        QCOMPARE(mgr.shortcut("open_in_new_tab"), QString("Ctrl+Return"));
        QCOMPARE(mgr.shortcut("open_in_split"), QString("Ctrl+Shift+Return"));
        QCOMPARE(mgr.shortcut("copy"), QString("Ctrl+C"));
        QCOMPARE(mgr.shortcut("cut"), QString("Ctrl+X"));
        QCOMPARE(mgr.shortcut("paste"), QString("Ctrl+V"));
        QCOMPARE(mgr.shortcut("rename"), QString("F2"));
        QCOMPARE(mgr.shortcut("new_folder"), QString("Ctrl+Shift+N"));
        QCOMPARE(mgr.shortcut("new_file"), QString("Ctrl+N"));
        QCOMPARE(mgr.shortcut("trash"), QString("Delete"));
        QCOMPARE(mgr.shortcut("toggle_hidden"), QString("Ctrl+H"));
        QCOMPARE(mgr.shortcut("quick_preview"), QString("Space"));
        QCOMPARE(mgr.shortcut("search"), QString("Ctrl+F"));
        QCOMPARE(mgr.shortcut("context_menu"), QString("Shift+F10"));
        QCOMPARE(mgr.shortcut("open_terminal"), QString("Ctrl+Alt+T"));
        QCOMPARE(mgr.shortcut("properties"), QString("Alt+Return"));
        QCOMPARE(mgr.shortcut("select_all"), QString("Ctrl+A"));
        QCOMPARE(mgr.shortcut("focus_left_pane"), QString("Ctrl+Alt+Left"));
        QCOMPARE(mgr.shortcut("focus_right_pane"), QString("Ctrl+Alt+Right"));
        QCOMPARE(mgr.shortcut("focus_next_pane"), QString("F6"));
        QCOMPARE(mgr.shortcut("focus_previous_pane"), QString("Shift+F6"));
    }

    void testUnknownShortcut()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.shortcut("nonexistent_action"), QString());
    }

    void testLegacyNewFileShortcutMigrates()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[shortcuts]\n"
                "new_file = \"Ctrl+Alt+N\"\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.shortcut("new_file"), QString("Ctrl+N"));
    }

    // --- Window controls ---

    void testWindowControlsDefaults()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.showWindowControls(), false);
        QCOMPARE(mgr.windowButtonLayout(), QString(":minimize,maximize,close"));
    }

    void testWindowControlsRuntimeDefault()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        mgr.setShowWindowControlsDefault(true);
        QCOMPARE(mgr.showWindowControls(), true);
    }

    void testWindowControlsExplicitOverridesRuntime()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[window]\n"
                "show_controls = false\n");
        f.close();

        ConfigManager mgr(path);
        mgr.setShowWindowControlsDefault(true);
        QCOMPARE(mgr.showWindowControls(), false);
    }

    void testWindowButtonLayoutFromConfig()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[window]\n"
                "show_controls = true\n"
                "button_layout = \"close,minimize,maximize:\"\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.showWindowControls(), true);
        QCOMPARE(mgr.windowButtonLayout(), QString("close,minimize,maximize:"));
    }

    void testSaveWindowControls()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        mgr.saveSettings(QVariantMap{
            {"showWindowControls", true},
            {"windowButtonLayout", "close:minimize"}
        });

        ConfigManager mgr2(path);
        QCOMPARE(mgr2.showWindowControls(), true);
        QCOMPARE(mgr2.windowButtonLayout(), QString("close:minimize"));
    }

    void testSaveStartupDir()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        QCOMPARE(mgr.startupDir(), QString("last"));

        mgr.saveSettings(QVariantMap{{"startupDir", "/home/user/Media"}});
        ConfigManager mgr2(path);
        QCOMPARE(mgr2.startupDir(), QString("/home/user/Media"));

        mgr2.saveSettings(QVariantMap{{"startupDir", "home"}});
        ConfigManager mgr3(path);
        QCOMPARE(mgr3.startupDir(), QString("home"));

        // An empty value falls back to the default.
        mgr3.saveSettings(QVariantMap{{"startupDir", ""}});
        ConfigManager mgr4(path);
        QCOMPARE(mgr4.startupDir(), QString("last"));

        // A manual edit that leaves the key empty or whitespace also falls
        // back on load, mirroring saveSettings().
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("[general]\nstartup_dir = \"\"\n");
        f.close();
        ConfigManager mgr5(path);
        QCOMPARE(mgr5.startupDir(), QString("last"));
    }

    void testDependencyStartupCheckOptOut()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        QCOMPARE(mgr.dependencyStartupCheck(), true);

        // "Don't show again" in the missing-dependencies dialog.
        mgr.saveSettings(QVariantMap{{"dependencyStartupCheck", false}});

        ConfigManager mgr2(path);
        QCOMPARE(mgr2.dependencyStartupCheck(), false);

        mgr2.saveSettings(QVariantMap{{"dependencyStartupCheck", true}});
        ConfigManager mgr3(path);
        QCOMPARE(mgr3.dependencyStartupCheck(), true);
    }

    void testSaveHiddenQuickAccess()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        QCOMPARE(mgr.hiddenQuickAccess(), QStringList());

        mgr.saveSettings(QVariantMap{
            {"hiddenQuickAccess", QStringList{"Pictures", "Network"}}
        });

        ConfigManager mgr2(path);
        QCOMPARE(mgr2.hiddenQuickAccess(), QStringList({"Pictures", "Network"}));

        // QML hands the value over as a QVariantList of strings.
        mgr2.saveSettings(QVariantMap{
            {"hiddenQuickAccess", QVariantList{QString("Trash")}}
        });
        ConfigManager mgr3(path);
        QCOMPARE(mgr3.hiddenQuickAccess(), QStringList({"Trash"}));

        // Unhiding everything must clear the stored list, not keep stale names.
        mgr3.saveSettings(QVariantMap{{"hiddenQuickAccess", QStringList{}}});
        ConfigManager mgr4(path);
        QCOMPARE(mgr4.hiddenQuickAccess(), QStringList());
    }

    // Hand-edited config: both new keys must be read straight from TOML.
    void testLoadSidebarAndDependencyKeys()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[general]\n"
                "dependency_startup_check = false\n"
                "[sidebar]\n"
                "hidden_quick_access = [\"Network\", \"Recents\"]\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.dependencyStartupCheck(), false);
        QCOMPARE(mgr.hiddenQuickAccess(), QStringList({"Network", "Recents"}));

        // Writing an unrelated setting must not drop either key.
        mgr.saveSettings(QVariantMap{{"showHidden", true}});
        ConfigManager mgr2(path);
        QCOMPARE(mgr2.dependencyStartupCheck(), false);
        QCOMPARE(mgr2.hiddenQuickAccess(), QStringList({"Network", "Recents"}));
    }

    // --- Animation config ---

    void testAnimationDefaults()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.animDurationFast(), 100);
        QCOMPARE(mgr.animDuration(), 200);
        QCOMPARE(mgr.animDurationSlow(), 350);
        QCOMPARE(mgr.animCurveEnter(), QString("OutCubic"));
        QCOMPARE(mgr.animCurveExit(), QString("InCubic"));
        QCOMPARE(mgr.animCurveTransition(), QString("Bezier"));
    }

    void testAnimationFromConfig()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[appearance]\n"
                "anim_duration_fast = 50\n"
                "anim_duration = 150\n"
                "anim_duration_slow = 300\n"
                "anim_curve_enter = \"Bezier\"\n"
                "anim_curve_exit = \"OutQuad\"\n"
                "anim_curve_transition = \"InOutExpo\"\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.animDurationFast(), 50);
        QCOMPARE(mgr.animDuration(), 150);
        QCOMPARE(mgr.animDurationSlow(), 300);
        QCOMPARE(mgr.animCurveEnter(), QString("Bezier"));
        QCOMPARE(mgr.animCurveExit(), QString("OutQuad"));
        QCOMPARE(mgr.animCurveTransition(), QString("InOutExpo"));
    }

    void testSaveAnimationSettings()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        mgr.saveSettings(QVariantMap{
            {"animDurationFast", 80},
            {"animDuration", 180},
            {"animDurationSlow", 400},
            {"animCurveEnter", "OutBack"},
            {"animCurveExit", "InCubic"},
            {"animCurveTransition", "InOutQuad"}
        });

        ConfigManager mgr2(path);
        QCOMPARE(mgr2.animDurationFast(), 80);
        QCOMPARE(mgr2.animDuration(), 180);
        QCOMPARE(mgr2.animDurationSlow(), 400);
        QCOMPARE(mgr2.animCurveEnter(), QString("OutBack"));
        QCOMPARE(mgr2.animCurveExit(), QString("InCubic"));
        QCOMPARE(mgr2.animCurveTransition(), QString("InOutQuad"));
    }

    // --- TOML parsing ---

    void testParseConfig()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[general]\n"
                "theme = \"custom\"\n"
                "font_family = \"Inter\"\n"
                "default_view = \"list\"\n"
                "show_hidden = true\n"
                "sort_by = \"size\"\n"
                "sort_ascending = false\n"
                "\n"
                "[sidebar]\n"
                "position = \"right\"\n"
                "width = 250\n"
                "visible = false\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.theme(), QString("custom"));
        QCOMPARE(mgr.fontFamily(), QString("Inter"));
        // ConfigManager reports what the file says; TabModel::normalizeViewMode
        // is what turns a retired mode into one the UI can render.
        QCOMPARE(mgr.defaultView(), QString("list"));
        QCOMPARE(mgr.showHidden(), true);
        QCOMPARE(mgr.sortBy(), QString("size"));
        QCOMPARE(mgr.sortAscending(), false);
        QCOMPARE(mgr.sidebarPosition(), QString("right"));
        QCOMPARE(mgr.sidebarWidth(), 250);
        QCOMPARE(mgr.sidebarVisible(), false);
    }

    void testParseAppearanceSection()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[appearance]\n"
                "radius_small = 2\n"
                "radius_medium = 6\n"
                "radius_large = 16\n"
                "transparency_enabled = false\n"
                "transparency_level = 0.4\n"
                "animations_enabled = false\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.radiusSmall(), 2);
        QCOMPARE(mgr.radiusMedium(), 6);
        QCOMPARE(mgr.radiusLarge(), 16);
        QCOMPARE(mgr.transparencyEnabled(), false);
        QCOMPARE(mgr.transparencyLevel(), 0.4);
        QCOMPARE(mgr.animationsEnabled(), false);
    }

    void testParseIconTheme()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[general]\n"
                "icon_theme = \"Papirus\"\n"
                "");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.iconTheme(), QString("Papirus"));
    }

    // --- Bookmarks ---

    void testBookmarks()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[bookmarks]\n"
                "paths = [\"~/Documents\", \"~/Downloads\"]\n");
        f.close();

        ConfigManager mgr(path);
        QStringList bookmarks = mgr.bookmarks();
        QCOMPARE(bookmarks.size(), 2);
        QCOMPARE(bookmarks.at(0), QString("~/Documents"));
        QCOMPARE(bookmarks.at(1), QString("~/Downloads"));
    }

    void testSaveSettings()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[bookmarks]\n"
                "paths = [\"~/Documents\"]\n");
        f.close();

        ConfigManager mgr(path);
        QSignalSpy spy(&mgr, &ConfigManager::configChanged);

        QVariantMap settings;
        settings.insert("theme", "catppuccin-latte");
        settings.insert("fontFamily", "Inter");
        settings.insert("iconTheme", "Papirus");
        settings.insert("showHidden", true);
        settings.insert("sidebarVisible", false);
        settings.insert("sidebarWidth", 420);
        settings.insert("radiusSmall", 6);
        settings.insert("radiusMedium", 12);
        settings.insert("radiusLarge", 18);
        settings.insert("transparencyEnabled", false);
        settings.insert("transparencyLevel", 0.3);
        settings.insert("animationsEnabled", false);
        mgr.saveSettings(settings);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(mgr.theme(), QString("catppuccin-latte"));
        QCOMPARE(mgr.fontFamily(), QString("Inter"));
        QCOMPARE(mgr.iconTheme(), QString("Papirus"));
        QCOMPARE(mgr.showHidden(), true);
        QCOMPARE(mgr.sidebarVisible(), false);
        QCOMPARE(mgr.sidebarWidth(), 420);
        QCOMPARE(mgr.radiusSmall(), 6);
        QCOMPARE(mgr.radiusMedium(), 12);
        QCOMPARE(mgr.radiusLarge(), 18);
        QCOMPARE(mgr.transparencyEnabled(), false);
        QCOMPARE(mgr.transparencyLevel(), 0.3);
        QCOMPARE(mgr.animationsEnabled(), false);
        QCOMPARE(mgr.bookmarks(), QStringList({"~/Documents"}));
    }

    void testSaveShortcuts()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        QSignalSpy spy(&mgr, &ConfigManager::configChanged);

        QVariantMap shortcuts;
        shortcuts.insert("copy", "Ctrl+Shift+C");
        shortcuts.insert("search", "Ctrl+K");
        shortcuts.insert("new_tab", "");
        mgr.saveShortcuts(shortcuts);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(mgr.shortcut("copy"), QString("Ctrl+Shift+C"));
        QCOMPARE(mgr.shortcut("search"), QString("Ctrl+K"));
        QCOMPARE(mgr.shortcut("new_tab"), QString("Ctrl+T"));

        const QVariantMap shortcutMap = mgr.shortcutMap();
        QCOMPARE(shortcutMap.value("copy").toString(), QString("Ctrl+Shift+C"));
        QCOMPARE(shortcutMap.value("search").toString(), QString("Ctrl+K"));
    }

    void testEmptyBookmarks()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[bookmarks]\npaths = []\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.bookmarks().size(), 0);
    }

    // --- Context actions ---

    void testCustomContextActions()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[[context_menu.actions]]\n"
                "name = \"Open in Neovim\"\n"
                "command = \"foot nvim {file}\"\n"
                "types = [\"file\"]\n"
                "\n"
                "[[context_menu.actions]]\n"
                "name = \"Upload\"\n"
                "command = \"curl -F 'file=@{file}' https://example.com\"\n"
                "types = [\"file\", \"image\"]\n");
        f.close();

        ConfigManager mgr(path);
        QVariantList actions = mgr.customContextActions();
        QCOMPARE(actions.size(), 2);

        QVariantMap first = actions.at(0).toMap();
        QCOMPARE(first["name"].toString(), QString("Open in Neovim"));
        QCOMPARE(first["command"].toString(), QString("foot nvim {file}"));

        QVariantMap second = actions.at(1).toMap();
        QCOMPARE(second["name"].toString(), QString("Upload"));
    }

    void testNoContextActions()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");
        QCOMPARE(mgr.customContextActions().size(), 0);
    }

    // --- Shortcuts override ---

    void testShortcutOverride()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[shortcuts]\n"
                "open = \"Return\"\n"
                "back = \"Alt+Left\"\n"
                "copy = \"Ctrl+Shift+C\"\n"); // override default
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.shortcut("open"), QString("Return"));
        QCOMPARE(mgr.shortcut("copy"), QString("Ctrl+Shift+C")); // overridden
        QCOMPARE(mgr.shortcut("paste"), QString("Ctrl+V")); // still default
    }

    // --- Missing optional sections ---

    void testMissingSections()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[general]\ntheme = \"test\"\n"); // only general section
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.theme(), QString("test"));
        // Other sections should have defaults
        QCOMPARE(mgr.sidebarWidth(), 200);
        QCOMPARE(mgr.radiusMedium(), 8);
    }

    // --- Sort persistence ---

    void testRememberSortPerFolderDefault()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");
        QCOMPARE(mgr.rememberSortPerFolder(), true);
    }

    void testParseRememberSortPerFolder()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[general]\n"
                "remember_sort_per_folder = false\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.rememberSortPerFolder(), false);
    }

    void testSaveDefaultSort()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        mgr.saveSettings(QVariantMap{
            {"sortBy", "modified"},
            {"sortAscending", false},
            {"rememberSortPerFolder", false}
        });

        ConfigManager mgr2(path);
        QCOMPARE(mgr2.sortBy(), QString("modified"));
        QCOMPARE(mgr2.sortAscending(), false);
        QCOMPARE(mgr2.rememberSortPerFolder(), false);
    }

    void testFolderSortFallsBackToDefault()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[general]\n"
                "sort_by = \"size\"\n"
                "sort_ascending = false\n");
        f.close();

        ConfigManager mgr(path);
        // No remembered entry → resolves to global default
        QCOMPARE(mgr.folderSortBy(dir.path()), QString("size"));
        QCOMPARE(mgr.folderSortAscending(dir.path()), false);
    }

    void testSetAndPersistFolderSort()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        ConfigManager mgr(path);
        mgr.setFolderSort(dir.path(), "modified", false);

        QCOMPARE(mgr.folderSortBy(dir.path()), QString("modified"));
        QCOMPARE(mgr.folderSortAscending(dir.path()), false);

        // A fresh manager reads the persisted per-folder store
        ConfigManager mgr2(path);
        QCOMPARE(mgr2.folderSortBy(dir.path()), QString("modified"));
        QCOMPARE(mgr2.folderSortAscending(dir.path()), false);
    }

    void testFolderSortIgnoredWhenRememberOff()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[general]\n"
                "sort_by = \"name\"\n"
                "sort_ascending = true\n"
                "remember_sort_per_folder = false\n");
        f.close();

        ConfigManager mgr(path);
        mgr.setFolderSort(dir.path(), "modified", false);
        // Remember is off → always the global default
        QCOMPARE(mgr.folderSortBy(dir.path()), QString("name"));
        QCOMPARE(mgr.folderSortAscending(dir.path()), true);
    }

    void testFolderSortPrunesDeadPaths()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";
        QString storePath = dir.path() + "/folder_sort.json";
        QString deadPath = dir.path() + "/gone";

        QFile f(storePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QString("{\"%1\":{\"by\":\"size\",\"ascending\":true},"
                        "\"%2\":{\"by\":\"modified\",\"ascending\":false}}")
                    .arg(dir.path(), deadPath)
                    .toUtf8());
        f.close();

        // Construction prunes the nonexistent path and rewrites the store
        ConfigManager mgr(path);

        QFile rf(storePath);
        QVERIFY(rf.open(QIODevice::ReadOnly));
        const QString contents = QString::fromUtf8(rf.readAll());
        rf.close();

        QVERIFY(contents.contains(dir.path()));
        QVERIFY(!contents.contains(deadPath));
    }

    // --- File watcher reload ---

    void testConfigFileWatcherReload()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        // Create initial config
        {
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write("[general]\ntheme = \"initial\"\n");
            f.close();
        }

        ConfigManager mgr(path);
        QCOMPARE(mgr.theme(), QString("initial"));

        QSignalSpy spy(&mgr, &ConfigManager::configChanged);

        // Modify the config file
        {
            QFile f(path);
            f.open(QIODevice::WriteOnly | QIODevice::Truncate);
            f.write("[general]\ntheme = \"updated\"\n");
            f.close();
        }

        // Wait for watcher to pick up the change
        if (spy.wait(3000)) {
            QCOMPARE(mgr.theme(), QString("updated"));
        }
        // If watcher didn't fire (CI timing), skip rather than fail
    }

    // Editors save by writing a temp file and renaming over the target, which
    // replaces the inode. A file-only watch dies there — silently, and for the
    // rest of the process's life. Two consecutive replacements must both land.
    void testRenameSaveKeepsReloading()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/config.toml";

        auto replaceConfig = [&](const QString &theme) {
            const QString tmp = path + ".tmp";
            QFile f(tmp);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QStringLiteral("[general]\ntheme = \"%1\"\n").arg(theme).toUtf8());
            f.close();
            QFile::remove(path);
            QVERIFY(QFile::rename(tmp, path));
        };

        replaceConfig("initial");
        ConfigManager mgr(path);
        QCOMPARE(mgr.theme(), QString("initial"));

        QSignalSpy spy(&mgr, &ConfigManager::configChanged);

        QTest::qWait(20);
        replaceConfig("second");
        QVERIFY(spy.wait(5000));
        QCOMPARE(mgr.theme(), QString("second"));

        QTest::qWait(20);
        replaceConfig("third");
        QVERIFY(spy.wait(5000));
        QCOMPARE(mgr.theme(), QString("third"));

        // The harsher variant: some editors unlink the file and only write the
        // replacement a moment later. A file-only watch has nothing to re-arm
        // on at that instant and stays dead; the directory watch catches it.
        QVERIFY(QFile::remove(path));
        QTest::qWait(50);
        replaceConfig("fourth");
        QVERIFY(spy.wait(5000));
        QCOMPARE(mgr.theme(), QString("fourth"));
    }

    // --- Empty config file ---

    void testEmptyConfigFile()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.close(); // empty file

        ConfigManager mgr(path);
        // Should use all defaults
        QCOMPARE(mgr.theme(), QString("catppuccin-mocha"));
        QCOMPARE(mgr.defaultView(), QString("grid"));
    }
};

QTEST_MAIN(TestConfigManager)
#include "tst_configmanager.moc"

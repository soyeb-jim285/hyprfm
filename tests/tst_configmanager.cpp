#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "services/configmanager.h"

class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultValues()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.theme(), QString("catppuccin-mocha"));
        QCOMPARE(mgr.defaultView(), QString("grid"));
        QCOMPARE(mgr.showHidden(), false);
        QCOMPARE(mgr.sortBy(), QString("name"));
        QCOMPARE(mgr.sortAscending(), true);
        QCOMPARE(mgr.sidebarPosition(), QString("left"));
        QCOMPARE(mgr.sidebarWidth(), 200);
        QCOMPARE(mgr.sidebarVisible(), true);
    }

    void testParseConfig()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[general]\n"
                "theme = \"custom\"\n"
                "default_view = \"list\"\n"
                "show_hidden = true\n"
                "\n"
                "[sidebar]\n"
                "position = \"right\"\n"
                "width = 250\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.theme(), QString("custom"));
        QCOMPARE(mgr.defaultView(), QString("list"));
        QCOMPARE(mgr.showHidden(), true);
        QCOMPARE(mgr.sidebarPosition(), QString("right"));
        QCOMPARE(mgr.sidebarWidth(), 250);
    }

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
    }

    void testCustomContextActions()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[[context_menu.actions]]\n"
                "name = \"Open in Neovim\"\n"
                "command = \"foot nvim {file}\"\n"
                "types = [\"file\"]\n");
        f.close();

        ConfigManager mgr(path);
        QVariantList actions = mgr.customContextActions();
        QCOMPARE(actions.size(), 1);
        QVariantMap action = actions.at(0).toMap();
        QCOMPARE(action["name"].toString(), QString("Open in Neovim"));
    }

    void testShortcuts()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[shortcuts]\n"
                "open = \"Return\"\n"
                "back = \"Alt+Left\"\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.shortcut("open"), QString("Return"));
        QCOMPARE(mgr.shortcut("back"), QString("Alt+Left"));
        QCOMPARE(mgr.shortcut("new_tab"), QString("Ctrl+T"));
    }
};

QTEST_MAIN(TestConfigManager)
#include "tst_configmanager.moc"

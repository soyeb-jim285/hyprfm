#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QDebug>

#include "services/configmanager.h"
#include "services/themeloader.h"
#include "services/fileoperations.h"
#include "services/clipboardmanager.h"
#include "models/filesystemmodel.h"
#include "models/tablistmodel.h"
#include "models/bookmarkmodel.h"
#include "models/devicemodel.h"
#include "providers/thumbnailprovider.h"
#include "providers/iconprovider.h"
#include <QIcon>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("HyprFM");
    app.setOrganizationName("hyprfm");
    app.setDesktopFileName("hyprfm");

    QQuickStyle::setStyle("Basic");

    // Ensure config directory exists
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                              + "/.config/hyprfm";
    QDir().mkpath(configDir);
    const QString configPath = configDir + "/config.toml";

    // Create backend instances
    ConfigManager *config = new ConfigManager(configPath, &app);

    // Look for themes in multiple locations
    QString themesDir;
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/../themes",        // installed layout
        QCoreApplication::applicationDirPath() + "/../../themes",     // build dir (build/src/)
        QStringLiteral(HYPRFM_SOURCE_DIR) + "/themes",               // source dir via compile def
    };
    for (const auto &path : searchPaths) {
        if (QDir(path).exists()) {
            themesDir = path;
            break;
        }
    }
    ThemeLoader *theme = new ThemeLoader(&app);
    theme->loadTheme(config->theme(), themesDir);

    TabListModel *tabModel = new TabListModel(&app);

    BookmarkModel *bookmarks = new BookmarkModel(&app);
    bookmarks->setBookmarks(config->bookmarks());

    FileOperations *fileOps = new FileOperations(&app);
    ClipboardManager *clipboard = new ClipboardManager(&app);

    FileSystemModel *fsModel = new FileSystemModel(&app);
    fsModel->setRootPath(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    fsModel->setShowHidden(config->showHidden());

    // Connect config changes to reload theme, bookmarks, and showHidden
    QObject::connect(config, &ConfigManager::configChanged, [=]() {
        theme->loadTheme(config->theme(), themesDir);
        bookmarks->setBookmarks(config->bookmarks());
        fsModel->setShowHidden(config->showHidden());
    });

    // Connect lastWindowClosed to quit
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, &QGuiApplication::quit);

    // Create DeviceModel
    DeviceModel *devices = new DeviceModel(&app);

    // Check for required CLI tools and warn if missing
    const QStringList requiredTools = {"rsync", "gio", "xdg-open", "wl-copy"};
    for (const QString &tool : requiredTools) {
        QProcess which;
        which.setProgram("which");
        which.setArguments({tool});
        which.start();
        which.waitForFinished(2000);
        if (which.exitCode() != 0)
            qWarning() << "HyprFM: optional tool not found:" << tool;
    }

    QQmlApplicationEngine engine;

    // Register image providers
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    engine.addImageProvider("icon", new IconProvider);

    // Use system icon theme (Adwaita, Papirus, etc.)
    QIcon::setThemeName(QIcon::themeName().isEmpty() ? "Adwaita" : QIcon::themeName());

    // Register context properties
    engine.rootContext()->setContextProperty("config", config);
    engine.rootContext()->setContextProperty("theme", theme);
    engine.rootContext()->setContextProperty("tabModel", tabModel);
    engine.rootContext()->setContextProperty("bookmarks", bookmarks);
    engine.rootContext()->setContextProperty("fileOps", fileOps);
    engine.rootContext()->setContextProperty("clipboard", clipboard);
    engine.rootContext()->setContextProperty("fsModel", fsModel);
    engine.rootContext()->setContextProperty("devices", devices);

    engine.loadFromModule("HyprFM", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}

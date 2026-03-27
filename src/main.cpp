#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QDebug>
#include <QLoggingCategory>

#include "services/configmanager.h"
#include "services/themeloader.h"
#include "services/fileoperations.h"
#include "services/clipboardmanager.h"
#include "services/draghelper.h"
#include "models/filesystemmodel.h"
#include "models/tablistmodel.h"
#include "models/bookmarkmodel.h"
#include "models/devicemodel.h"
#include "models/recentfilesmodel.h"
#include "providers/thumbnailprovider.h"
#include "providers/iconprovider.h"
#include <QIcon>

int main(int argc, char *argv[])
{
    // Suppress harmless portal registration warning on non-sandboxed apps
    QLoggingCategory::setFilterRules("qt.qpa.services.warning=false");

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
        QStringLiteral(HYPRFM_DATA_DIR) + "/themes",                 // data dir via compile def
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
    // DragHelper created after IconProvider below

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

    // Create RecentFilesModel
    RecentFilesModel *recentFiles = new RecentFilesModel(configDir + "/recents.json", &app);

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

    // Add import paths for installed modules
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR));            // HyprFM module
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR "/src/qml")); // Quill module

    // Set icon theme so QIcon::fromTheme() works (e.g. for drag pixmaps)
    QIcon::setThemeName(config->iconTheme());

    // Register image providers (keep pointer to IconProvider for DragHelper)
    auto *iconProvider = new IconProvider(config->iconTheme());
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    engine.addImageProvider("icon", iconProvider);

    DragHelper *dragHelper = new DragHelper(iconProvider, &app);

    // Register context properties
    engine.rootContext()->setContextProperty("config", config);
    engine.rootContext()->setContextProperty("theme", theme);
    engine.rootContext()->setContextProperty("tabModel", tabModel);
    engine.rootContext()->setContextProperty("bookmarks", bookmarks);
    engine.rootContext()->setContextProperty("fileOps", fileOps);
    engine.rootContext()->setContextProperty("clipboard", clipboard);
    engine.rootContext()->setContextProperty("dragHelper", dragHelper);
    engine.rootContext()->setContextProperty("fsModel", fsModel);
    engine.rootContext()->setContextProperty("devices", devices);
    engine.rootContext()->setContextProperty("recentFiles", recentFiles);

    engine.loadFromModule("HyprFM", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}

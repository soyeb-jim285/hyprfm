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
#include <QSurfaceFormat>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>

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
#include "models/searchresultsmodel.h"
#include "models/searchproxymodel.h"
#include "services/searchservice.h"
#include "services/undomanager.h"
#include "services/previewservice.h"
#include "services/diskusageservice.h"
#include "services/remoteaccessservice.h"
#include "services/runtimefeaturesservice.h"
#include "services/gitstatusservice.h"
#include "providers/thumbnailprovider.h"
#include "providers/iconprovider.h"
#ifdef HYPRFM_HAS_POPPLER_QT6
#include "providers/pdfpreviewprovider.h"
#endif
#include <QIcon>

int main(int argc, char *argv[])
{
    // Suppress harmless portal registration warning on non-sandboxed apps
    QLoggingCategory::setFilterRules("qt.qpa.services.warning=false");

    // Enable multisampling for smoother Shape/path rendering
    QSurfaceFormat fmt;
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName("HyprFM");
    app.setOrganizationName("hyprfm");
    app.setDesktopFileName("hyprfm");

    QQuickStyle::setStyle("Basic");

    // Use native text rendering (FreeType/fontconfig) for crisp fonts matching GTK apps
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    // Use system default font with proper hinting for crisp text
    QFont defaultFont = app.font();
    defaultFont.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(defaultFont);

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

    // Restore session (tabs + window geometry)
    const QString sessionPath = configDir + "/session.json";
    QJsonObject sessionData;
    {
        QFile sf(sessionPath);
        if (sf.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(sf.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject())
                sessionData = doc.object();
        }
    }
    if (sessionData.contains("tabs"))
        tabModel->restoreSession(sessionData.value("tabs").toArray(),
                                 sessionData.value("activeTab").toInt(0));

    BookmarkModel *bookmarks = new BookmarkModel(&app);
    bookmarks->setBookmarks(config->bookmarks());

    // Persist bookmark changes to config
    QObject::connect(bookmarks, &BookmarkModel::bookmarksChanged, [=]() {
        config->saveBookmarks(bookmarks->paths());
    });

    FileOperations *fileOps = new FileOperations(&app);
    UndoManager *undoManager = new UndoManager(fileOps, &app);
    ClipboardManager *clipboard = new ClipboardManager(&app);
    // DragHelper created after IconProvider below

    const QString homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString initialPrimaryPath = tabModel->activeTab() && !tabModel->activeTab()->currentPath().isEmpty()
        ? tabModel->activeTab()->currentPath()
        : homePath;
    const QString initialSecondaryPath = tabModel->activeTab() && !tabModel->activeTab()->secondaryCurrentPath().isEmpty()
        ? tabModel->activeTab()->secondaryCurrentPath()
        : initialPrimaryPath;

    FileSystemModel *fsModel = new FileSystemModel(&app);
    fsModel->setRootPath(initialPrimaryPath);
    fsModel->setShowHidden(config->showHidden());

    FileSystemModel *splitFsModel = new FileSystemModel(&app);
    splitFsModel->setRootPath(initialSecondaryPath);
    splitFsModel->setShowHidden(config->showHidden());

    SearchResultsModel *searchResults = new SearchResultsModel(&app);
    SearchProxyModel *searchProxy = new SearchProxyModel(&app);
    searchProxy->setSourceModel(searchResults);

    SearchResultsModel *splitSearchResults = new SearchResultsModel(&app);
    SearchProxyModel *splitSearchProxy = new SearchProxyModel(&app);
    splitSearchProxy->setSourceModel(splitSearchResults);

    SearchService *searchService = new SearchService(&app);
    searchService->setObjectName("primary");
    searchService->setResultsModel(searchResults);

    SearchService *splitSearchService = new SearchService(&app);
    splitSearchService->setObjectName("secondary");
    splitSearchService->setResultsModel(splitSearchResults);

    PreviewService *previewService = new PreviewService(&app);
    DiskUsageService *diskUsageService = new DiskUsageService(&app);
    RemoteAccessService *remoteAccessService = new RemoteAccessService(&app);
    RuntimeFeaturesService *runtimeFeatures = new RuntimeFeaturesService(&app);
    GitStatusService *gitService = new GitStatusService(&app);
    fsModel->setGitStatusService(gitService);
    splitFsModel->setGitStatusService(gitService);

    // Connect config changes to reload theme, bookmarks, and showHidden
    QObject::connect(config, &ConfigManager::configChanged, [=]() {
        theme->loadTheme(config->theme(), themesDir);
        bookmarks->setBookmarks(config->bookmarks());
        fsModel->setShowHidden(config->showHidden());
        splitFsModel->setShowHidden(config->showHidden());
    });

    // Connect lastWindowClosed to quit
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, &QGuiApplication::quit);

    // Create RecentFilesModel
    RecentFilesModel *recentFiles = new RecentFilesModel(configDir + "/recents.json", &app);

    // Create DeviceModel
    DeviceModel *devices = new DeviceModel(&app);

    // Check for CLI tools and warn if missing
    const QStringList requiredTools = {"fd", "rsync", "gio", "xdg-open"};
    const QStringList optionalTools = {"wl-copy", "wl-paste", "ffmpeg", "udisksctl", "bat", "batcat"};

    for (const QString &tool : requiredTools) {
        QProcess which;
        which.setProgram("which");
        which.setArguments({tool});
        which.start();
        which.waitForFinished(2000);
        if (which.exitCode() != 0)
            qWarning() << "HyprFM: required tool not found:" << tool;
    }

    for (const QString &tool : optionalTools) {
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
#ifdef HYPRFM_HAS_POPPLER_QT6
    engine.addImageProvider("pdfpreview", new PdfPreviewProvider);
#endif

    DragHelper *dragHelper = new DragHelper(iconProvider, &app);

    // Register context properties
    engine.rootContext()->setContextProperty("config", config);
    engine.rootContext()->setContextProperty("theme", theme);
    engine.rootContext()->setContextProperty("tabModel", tabModel);
    engine.rootContext()->setContextProperty("bookmarks", bookmarks);
    engine.rootContext()->setContextProperty("fileOps", fileOps);
    engine.rootContext()->setContextProperty("undoManager", undoManager);
    engine.rootContext()->setContextProperty("clipboard", clipboard);
    engine.rootContext()->setContextProperty("dragHelper", dragHelper);
    engine.rootContext()->setContextProperty("fsModel", fsModel);
    engine.rootContext()->setContextProperty("splitFsModel", splitFsModel);
    engine.rootContext()->setContextProperty("devices", devices);
    engine.rootContext()->setContextProperty("recentFiles", recentFiles);
    engine.rootContext()->setContextProperty("searchProxy", searchProxy);
    engine.rootContext()->setContextProperty("searchResults", searchResults);
    engine.rootContext()->setContextProperty("searchService", searchService);
    engine.rootContext()->setContextProperty("splitSearchProxy", splitSearchProxy);
    engine.rootContext()->setContextProperty("splitSearchResults", splitSearchResults);
    engine.rootContext()->setContextProperty("splitSearchService", splitSearchService);
    engine.rootContext()->setContextProperty("previewService", previewService);
    engine.rootContext()->setContextProperty("diskUsageService", diskUsageService);
    engine.rootContext()->setContextProperty("remoteAccessService", remoteAccessService);
    engine.rootContext()->setContextProperty("runtimeFeatures", runtimeFeatures);

    engine.loadFromModule("HyprFM", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    QTimer sessionSaveTimer;
    sessionSaveTimer.setSingleShot(true);
    sessionSaveTimer.setInterval(250);

    auto saveSession = [&]() {
        QJsonObject session;
        session["tabs"] = tabModel->saveSession();
        session["activeTab"] = tabModel->activeIndex();

        if (!engine.rootObjects().isEmpty()) {
            QObject *win = engine.rootObjects().first();
            session["windowX"] = win->property("x").toInt();
            session["windowY"] = win->property("y").toInt();
            session["windowWidth"] = win->property("width").toInt();
            session["windowHeight"] = win->property("height").toInt();
        }

        QSaveFile sf(sessionPath);
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(session).toJson(QJsonDocument::Compact));
            sf.commit();
        }
    };

    auto scheduleSessionSave = [&]() {
        sessionSaveTimer.start();
    };

    QObject::connect(&sessionSaveTimer, &QTimer::timeout, &app, saveSession);
    QObject::connect(tabModel, &TabListModel::sessionChanged, &app, scheduleSessionSave);

    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        QObject::connect(win, &QQuickWindow::xChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::yChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::widthChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::heightChanged, &app, scheduleSessionSave);
    }

    // Save session on quit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        sessionSaveTimer.stop();
        saveSession();
    });

    // Restore window geometry
    if (sessionData.contains("windowWidth") && !engine.rootObjects().isEmpty()) {
        QObject *win = engine.rootObjects().first();
        win->setProperty("x", sessionData.value("windowX").toInt());
        win->setProperty("y", sessionData.value("windowY").toInt());
        win->setProperty("width", sessionData.value("windowWidth").toInt());
        win->setProperty("height", sessionData.value("windowHeight").toInt());
    }

    return app.exec();
}

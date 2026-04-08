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
#include <QFontDatabase>

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
#include "services/metadataextractor.h"
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

    auto resolveUiFont = [&](const QString &preferredFamily) {
        // Resolve the platform UI font first so the app does not depend on
        // theme-local font defaults that may not exist inside a sandbox.
        QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        if (font.family().isEmpty())
            font = app.font();

        if (!preferredFamily.trimmed().isEmpty())
            font.setFamily(preferredFamily.trimmed());

        font.setHintingPreference(QFont::PreferFullHinting);
        return font;
    };

    // Ensure config directory exists
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                              + "/.config/hyprfm";
    QDir().mkpath(configDir);
    const QString configPath = configDir + "/config.toml";

    auto firstExistingDir = [](const QStringList &paths) {
        for (const QString &path : paths) {
            const QString cleanPath = QDir::cleanPath(path);
            if (QDir(cleanPath).exists())
                return cleanPath;
        }
        return QString();
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dataDir = firstExistingDir({
        QDir(appDir).filePath("../share/hyprfm"),
        QDir(appDir).filePath("../../share/hyprfm"),
        QStringLiteral(HYPRFM_DATA_DIR),
        QStringLiteral(HYPRFM_SOURCE_DIR),
    });

    QStringList themeSearchPaths = {
        QDir(appDir).filePath("../themes"),
        QDir(appDir).filePath("../../themes"),
        QStringLiteral(HYPRFM_DATA_DIR) + "/themes",
        QStringLiteral(HYPRFM_SOURCE_DIR) + "/themes",
    };
    if (!dataDir.isEmpty())
        themeSearchPaths.prepend(QDir(dataDir).filePath("themes"));

    const QString themesDir = firstExistingDir(themeSearchPaths);
    if (dataDir.isEmpty())
        qWarning() << "HyprFM: unable to locate data directory";
    if (themesDir.isEmpty())
        qWarning() << "HyprFM: unable to locate themes directory";

    // Create backend instances
    ConfigManager *config = new ConfigManager(configPath, &app, themesDir);
    app.setFont(resolveUiFont(config->fontFamily()));
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

    FileSystemModel *millerParentModel = new FileSystemModel(&app);
    millerParentModel->setShowHidden(config->showHidden());

    FileSystemModel *millerPreviewModel = new FileSystemModel(&app);
    millerPreviewModel->setShowHidden(config->showHidden());

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
    MetadataExtractor *metadataExtractor = new MetadataExtractor(&app);
    DiskUsageService *diskUsageService = new DiskUsageService(&app);
    RemoteAccessService *remoteAccessService = new RemoteAccessService(&app);
    RuntimeFeaturesService *runtimeFeatures = new RuntimeFeaturesService(&app);
    GitStatusService *primaryGitService = new GitStatusService(&app);
    GitStatusService *secondaryGitService = new GitStatusService(&app);
    fsModel->setGitStatusService(primaryGitService);
    splitFsModel->setGitStatusService(secondaryGitService);

    // Keep the live UI in sync with persisted config values.
    QObject::connect(config, &ConfigManager::configChanged, [=, &app, &resolveUiFont]() {
        theme->loadTheme(config->theme(), themesDir);
        bookmarks->setBookmarks(config->bookmarks());
        fsModel->setShowHidden(config->showHidden());
        splitFsModel->setShowHidden(config->showHidden());
        millerParentModel->setShowHidden(config->showHidden());
        millerPreviewModel->setShowHidden(config->showHidden());
        app.setFont(resolveUiFont(config->fontFamily()));
    });

    // Connect lastWindowClosed to quit
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, &QGuiApplication::quit);

    // Create RecentFilesModel
    RecentFilesModel *recentFiles = new RecentFilesModel(configDir + "/recents.json", &app);

    // Create DeviceModel
    DeviceModel *devices = new DeviceModel(&app);

    // Check for CLI tools and warn if missing
    // Local-file opens go through QDesktopServices (which uses the OpenURI
    // portal under Flatpak and xdg-open on a regular host), so xdg-open is
    // no longer a hard requirement here.
    const QStringList requiredTools = {"rsync", "gio"};
    // Each entry is a list of equivalent alternatives — only warn if none exist
    const QList<QStringList> optionalToolGroups = {
        {"fd", "fdfind"}, // fd on Arch, fdfind on Debian/Ubuntu (fd-find pkg)
        {"wl-copy"},
        {"wl-paste"},
        {"ffmpeg"},
        // udisksctl no longer needed: DeviceModel talks to UDisks2 over DBus
        {"bat", "batcat"}, // bat on Arch, batcat on Debian/Ubuntu
    };

    auto hasTool = [](const QString &tool) {
        QProcess which;
        which.setProgram("which");
        which.setArguments({tool});
        which.start();
        which.waitForFinished(2000);
        return which.exitCode() == 0;
    };

    for (const QString &tool : requiredTools) {
        if (!hasTool(tool))
            qWarning() << "HyprFM: required tool not found:" << tool;
    }

    for (const QStringList &group : optionalToolGroups) {
        bool found = false;
        for (const QString &tool : group) {
            if (hasTool(tool)) {
                found = true;
                break;
            }
        }
        if (!found)
            qWarning() << "HyprFM: optional tool not found:" << group.join(QStringLiteral(" or "));
    }

    QQmlApplicationEngine engine;

    // Prefer the installed data layout, but keep source-tree fallbacks for dev builds.
    if (!dataDir.isEmpty()) {
        engine.addImportPath(dataDir);                           // HyprFM module
        engine.addImportPath(QDir(dataDir).filePath("src/qml")); // Quill module
    }
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR));
    engine.addImportPath(QStringLiteral(HYPRFM_DATA_DIR "/src/qml"));
    engine.addImportPath(QStringLiteral(HYPRFM_SOURCE_DIR));
    engine.addImportPath(QStringLiteral(HYPRFM_SOURCE_DIR "/src/qml"));

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

    QObject::connect(config, &ConfigManager::configChanged, [=]() {
        QIcon::setThemeName(config->iconTheme());
        iconProvider->setPrimaryTheme(config->iconTheme());
    });

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
    engine.rootContext()->setContextProperty("millerParentModel", millerParentModel);
    engine.rootContext()->setContextProperty("millerPreviewModel", millerPreviewModel);
    engine.rootContext()->setContextProperty("devices", devices);
    engine.rootContext()->setContextProperty("recentFiles", recentFiles);
    engine.rootContext()->setContextProperty("searchProxy", searchProxy);
    engine.rootContext()->setContextProperty("searchResults", searchResults);
    engine.rootContext()->setContextProperty("searchService", searchService);
    engine.rootContext()->setContextProperty("splitSearchProxy", splitSearchProxy);
    engine.rootContext()->setContextProperty("splitSearchResults", splitSearchResults);
    engine.rootContext()->setContextProperty("splitSearchService", splitSearchService);
    engine.rootContext()->setContextProperty("previewService", previewService);
    engine.rootContext()->setContextProperty("metadataExtractor", metadataExtractor);
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

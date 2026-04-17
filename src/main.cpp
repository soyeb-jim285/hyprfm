#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
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
#include <QStyleHints>
#ifdef HYPRFM_HAS_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

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
#include "services/dependencychecker.h"
#include "services/gitstatusservice.h"
#include "providers/thumbnailprovider.h"
#include "providers/iconprovider.h"
#include "providers/pdfpreviewprovider.h"
#include <QIcon>
#include <QUrl>

int main(int argc, char *argv[])
{
    // Suppress noisy warnings:
    //   - qt.qpa.services: harmless portal registration warning on non-sandboxed apps
    //   - qt.svg: Qt's SVG parser complains about unsupported filter elements
    //     (feTurbulence, feColorMatrix, etc.) on every draw when such SVGs
    //     are previewed/thumbnailed, even though the file still renders.
    QLoggingCategory::setFilterRules(
        "qt.qpa.services.warning=false\n"
        "qt.svg.warning=false");

    // Enable multisampling for smoother Shape/path rendering
    QSurfaceFormat fmt;
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    // HyprFM is a Wayland-only application (wl-copy clipboard, Hyprland
    // integration, KWin blur effects). Detect a non-Wayland session before
    // Qt tries to load the wayland QPA plugin so users see an actionable
    // message instead of the cryptic "Failed to create wl_display" error.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
        const char *session = sessionType.isEmpty() ? "unknown" : sessionType.constData();
        fprintf(stderr,
                "\n"
                "HyprFM: no Wayland display available (XDG_SESSION_TYPE=%s).\n"
                "\n"
                "HyprFM only supports Wayland sessions. Your current session\n"
                "appears to be X11 or does not expose $WAYLAND_DISPLAY.\n"
                "\n"
                "To run HyprFM:\n"
                "  * Log out and pick a Wayland session at the login screen\n"
                "    (e.g. \"Ubuntu on Wayland\", GNOME on Wayland, Hyprland, KDE\n"
                "    Plasma Wayland).\n"
                "  * If running via Flatpak, also grant Wayland socket access:\n"
                "      flatpak override --user --socket=wayland io.github.soyeb_jim285.HyprFM\n"
                "\n",
                session);
        return 1;
    }

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

    const QString systemDefaultTheme = app.styleHints()->colorScheme() == Qt::ColorScheme::Light
        ? QStringLiteral("catppuccin-latte")
        : QStringLiteral("catppuccin-mocha");

    // Create backend instances
    ConfigManager *config = new ConfigManager(configPath, &app, themesDir, systemDefaultTheme);
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
    config->setShowWindowControlsDefault(runtimeFeatures->useIntegratedWindowControls());
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

    // Aggregate runtime tools + compile-time features + DBus services for the
    // in-app MissingDependenciesDialog. Replaces the older hand-rolled
    // `which` loop that only logged to stderr.
    DependencyChecker *dependencies = new DependencyChecker(&app);

    // When the user installs a missing tool and clicks "Re-check", propagate
    // the refresh into feature services so their Q_PROPERTY bindings (e.g.
    // pdfPreviewAvailable) re-evaluate without requiring an app restart.
    QObject::connect(dependencies, &DependencyChecker::dependenciesChanged,
                     previewService, &PreviewService::refreshSupport);
    QObject::connect(dependencies, &DependencyChecker::dependenciesChanged,
                     metadataExtractor, &MetadataExtractor::refreshSupport);

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
    engine.addImageProvider("pdfpreview", new PdfPreviewProvider);

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
    engine.rootContext()->setContextProperty("dependencies", dependencies);

    const QString installedMainQml = dataDir.isEmpty()
        ? QString()
        : QDir(dataDir).filePath(QStringLiteral("HyprFM/qml/Main.qml"));

    // Prefer the installed on-disk module when it exists so deployed bundles
    // keep working even if Qt's embedded qrc payload is incomplete.
    if (!installedMainQml.isEmpty() && QFile::exists(installedMainQml)) {
        engine.load(QUrl::fromLocalFile(installedMainQml));
    } else {
        engine.loadFromModule("HyprFM", "Main");
    }

    if (engine.rootObjects().isEmpty())
        return -1;

    auto applyWindowEffects = [config](QQuickWindow *window) {
        if (!window)
            return;

#ifdef HYPRFM_HAS_KWINDOWSYSTEM
        // KWin blur only shows through translucent content; Hyprland keeps
        // using compositor rules against the same transparent window surface.
        const bool blurRequested = config->transparencyEnabled();
        const bool blurAvailable = KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
        KWindowEffects::enableBlurBehind(window, blurRequested && blurAvailable);

        const bool contrastAvailable = KWindowEffects::isEffectAvailable(KWindowEffects::BackgroundContrast);
        KWindowEffects::enableBackgroundContrast(window, blurRequested && contrastAvailable);
#else
        Q_UNUSED(window)
#endif
    };

    QTimer sessionSaveTimer;
    sessionSaveTimer.setSingleShot(true);
    sessionSaveTimer.setInterval(250);

    auto saveSession = [&]() {
        QJsonObject session;
        session["tabs"] = tabModel->saveSession();
        session["activeTab"] = tabModel->activeIndex();

        if (auto *win = !engine.rootObjects().isEmpty()
                ? qobject_cast<QQuickWindow *>(engine.rootObjects().first())
                : nullptr) {
            session["windowX"] = win->x();
            session["windowY"] = win->y();
            session["windowWidth"] = win->width();
            session["windowHeight"] = win->height();

            QWindow::Visibility savedVisibility = win->visibility();
            if (savedVisibility == QWindow::Hidden
                    || savedVisibility == QWindow::AutomaticVisibility
                    || savedVisibility == QWindow::Minimized) {
                savedVisibility = QWindow::Windowed;
            }
            session["windowVisibility"] = static_cast<int>(savedVisibility);
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
        applyWindowEffects(win);
        QObject::connect(config, &ConfigManager::configChanged, win, [=]() {
            applyWindowEffects(win);
        });
        QObject::connect(win, &QQuickWindow::xChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::yChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::widthChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::heightChanged, &app, scheduleSessionSave);
        QObject::connect(win, &QQuickWindow::visibilityChanged, &app, scheduleSessionSave);
    }

    // Save session on quit
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        sessionSaveTimer.stop();
        saveSession();
    });

    // Restore window geometry
    if (sessionData.contains("windowWidth") && !engine.rootObjects().isEmpty()) {
        if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            win->setX(sessionData.value("windowX").toInt());
            win->setY(sessionData.value("windowY").toInt());
            win->setWidth(sessionData.value("windowWidth").toInt());
            win->setHeight(sessionData.value("windowHeight").toInt());

            QWindow::Visibility restoredVisibility = QWindow::Windowed;
            if (sessionData.contains("windowVisibility")) {
                restoredVisibility = static_cast<QWindow::Visibility>(
                    sessionData.value("windowVisibility").toInt());
            }

            if (restoredVisibility == QWindow::Maximized
                    || restoredVisibility == QWindow::FullScreen
                    || restoredVisibility == QWindow::Windowed) {
                win->setVisibility(restoredVisibility);
            } else {
                win->showNormal();
            }
        }
    }

    return app.exec();
}

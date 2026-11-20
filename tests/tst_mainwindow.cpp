// Whole-window harness: everything main.cpp wires up, loading the real
// Main.qml headless. Used for behaviour that only shows with the full item
// tree in place (event routing between overlays, toolbar, views).
#include <QTest>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include "services/filepicker.h"
#include <QSignalSpy>
#include <QGuiApplication>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QPointingDevice>
#include <QPdfWriter>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStyleHints>
#include <QStandardPaths>
#include <algorithm>
#include <functional>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStandardItemModel>
#include <QUrl>
#include "models/bookmarkmodel.h"
#include "models/devicemodel.h"
#include "models/computermodel.h"
#include "models/filesystemmodel.h"
#include "models/recentfilesmodel.h"
#include "models/searchproxymodel.h"
#include "models/searchresultsmodel.h"
#include "models/tablistmodel.h"
#include "providers/iconprovider.h"
#include "providers/pdfpreviewprovider.h"
#include "providers/thumbnailprovider.h"
#include "services/clipboardmanager.h"
#include "services/configmanager.h"
#include "services/dependencychecker.h"
#include "services/diskusageservice.h"
#include "services/draghelper.h"
#include "services/fileoperations.h"
#include "services/gitstatusservice.h"
#include "services/metadataextractor.h"
#include "services/previewservice.h"
#include "services/remoteaccessservice.h"
#include "services/rcloneservice.h"
#include "services/runtimefeaturesservice.h"
#include "services/searchservice.h"
#include "services/sessionstate.h"
#include "services/themeloader.h"
#include "services/undomanager.h"
#ifdef HYPRFM_TERMINAL
#include "terminal/terminalview.h"
#endif

// Root (prepended by ComputerModel), a mounted data drive and two
// unmounted drives that have neither a device path nor a mount point.
struct FakeDrives
{
    QStandardItemModel devices;
    QString dataMount;
    explicit FakeDrives(const QString &mount) : dataMount(mount)
    {
        devices.setItemRoleNames({{Qt::UserRole + 1, "deviceName"}, {Qt::UserRole + 2, "devicePath"},
                                  {Qt::UserRole + 3, "mountPoint"}, {Qt::UserRole + 4, "totalSize"},
                                  {Qt::UserRole + 5, "freeSpace"}, {Qt::UserRole + 6, "mounted"}});
        append("Data", "/dev/fake-data", mount, 1000, 500, true);
        append("USB A", "", "", 0, 0, false);
        append("USB B", "", "", 0, 0, false);
    }
    void append(const QString &name, const QString &device, const QString &mount,
                qint64 total, qint64 free, bool mounted)
    {
        auto *item = new QStandardItem;
        item->setData(name, Qt::UserRole + 1);
        item->setData(device, Qt::UserRole + 2);
        item->setData(mount, Qt::UserRole + 3);
        item->setData(total, Qt::UserRole + 4);
        item->setData(free, Qt::UserRole + 5);
        item->setData(mounted, Qt::UserRole + 6);
        devices.appendRow(item);
    }
};

// Sidebar rows for the mount / unmount buttons: the system root, a USB stick
// the desktop mounted, an unmounted drive and a phone that needs gvfs-mtp.
struct FakeSidebarDevices
{
    QStandardItemModel devices;
    FakeSidebarDevices()
    {
        devices.setItemRoleNames({{Qt::UserRole + 1, "deviceName"}, {Qt::UserRole + 2, "devicePath"},
                                  {Qt::UserRole + 3, "mountPoint"}, {Qt::UserRole + 4, "totalSize"},
                                  {Qt::UserRole + 5, "freeSpace"}, {Qt::UserRole + 6, "usagePercent"},
                                  {Qt::UserRole + 7, "removable"}, {Qt::UserRole + 8, "mounted"},
                                  {Qt::UserRole + 9, "backend"}, {Qt::UserRole + 10, "userUnmountable"}});
        append("System", "/", true, "udisks2", false);
        append("USB stick", "/run/media/u/USB", true, "udisks2", true);
        append("Spare", "", false, "udisks2", false);
        append("Phone", "", false, "mtp-missing", false);
    }
    void append(const QString &name, const QString &mount, bool mounted, const QString &backend,
                bool userUnmountable)
    {
        auto *item = new QStandardItem;
        item->setData(name, Qt::UserRole + 1);
        item->setData(QStringLiteral("/dev/") + name, Qt::UserRole + 2);
        item->setData(mount, Qt::UserRole + 3);
        item->setData(qint64(1000), Qt::UserRole + 4);
        item->setData(qint64(500), Qt::UserRole + 5);
        item->setData(50, Qt::UserRole + 6);
        item->setData(true, Qt::UserRole + 7);
        item->setData(mounted, Qt::UserRole + 8);
        item->setData(backend, Qt::UserRole + 9);
        item->setData(userUnmountable, Qt::UserRole + 10);
        devices.appendRow(item);
    }
};

class TestMainWindow : public QObject
{
    Q_OBJECT

    struct App {
        QTemporaryDir home;
        QTemporaryDir moduleDir;
        QObject owner;
        QQmlApplicationEngine engine; // destroy QML before its context services
        TabListModel *tabModel = nullptr;
        SessionState *sessionState = nullptr;
        BookmarkModel *bookmarks = nullptr;
        SearchService *searchService = nullptr;
        SearchProxyModel *searchProxy = nullptr;
        QQuickWindow *window = nullptr;

        FilePicker *picker = nullptr;
        // Off by default, as for builds without the terminal; a test that
        // drives the built-in terminal sets it before load().
        bool builtinTerminal = false;
        bool load(const QJsonObject &pickerRequest = {})
        {
            QDir().mkpath(moduleDir.path() + "/HyprFM");
            QFile src(QStringLiteral(TEST_MODULE_DIR "/HyprFM/qmldir"));
            QFile dst(moduleDir.path() + "/HyprFM/qmldir");
            if (!src.open(QIODevice::ReadOnly) || !dst.open(QIODevice::WriteOnly))
                return false;
            for (const QByteArray &line : src.readAll().split('\n'))
                if (!line.startsWith("prefer "))
                    dst.write(line + '\n');
            dst.close();
            QFile::link(QStringLiteral(TEST_MODULE_DIR "/HyprFM/qml"), moduleDir.path() + "/HyprFM/qml");

            if (QQuickStyle::name() != QStringLiteral("Basic")) QQuickStyle::setStyle("Basic");
            const QString configDir = home.path() + "/.config/hyprfm";
            QDir().mkpath(configDir);
            const QStringList themeDirs {QStringLiteral(TEST_SOURCE_DIR "/themes")};

            auto *config = new ConfigManager(configDir + "/config.toml", &owner, themeDirs);
            auto *theme = new ThemeLoader(&owner);
            theme->loadTheme(config->theme(), themeDirs);
            tabModel = new TabListModel(&owner);
            bookmarks = new BookmarkModel(&owner);
            QObject::connect(bookmarks, &BookmarkModel::bookmarksChanged, config, [this, config] {
                config->saveBookmarks(bookmarks->paths(), bookmarks->names(), bookmarks->savedSplits());
            });
            auto *fileOps = new FileOperations(&owner);
            auto *undoManager = new UndoManager(fileOps, &owner);
            auto *clipboard = new ClipboardManager(&owner);
            auto *fsModel = new FileSystemModel(&owner);
            fsModel->setRootPath(QStringLiteral(TEST_SOURCE_DIR "/src"));
            auto *splitFsModel = new FileSystemModel(&owner);
            auto *millerParentModel = new FileSystemModel(&owner);
            auto *millerPreviewModel = new FileSystemModel(&owner);
            auto *searchResults = new SearchResultsModel(&owner);
            searchProxy = new SearchProxyModel(&owner);
            searchProxy->setSourceModel(searchResults);
            auto *splitSearchResults = new SearchResultsModel(&owner);
            auto *splitSearchProxy = new SearchProxyModel(&owner);
            splitSearchProxy->setSourceModel(splitSearchResults);
            searchService = new SearchService(&owner);
            searchService->setResultsModel(searchResults);
            auto *splitSearchService = new SearchService(&owner);
            splitSearchService->setResultsModel(splitSearchResults);
            auto *previewService = new PreviewService(&owner);
            auto *metadataExtractor = new MetadataExtractor(&owner);
            previewService->setMetadataExtractor(metadataExtractor);   // as main.cpp does
            auto *diskUsageService = new DiskUsageService(&owner);
            auto *remoteAccessService = new RemoteAccessService(&owner);
            auto *rcloneService = new RcloneService(&owner);
            auto *runtimeFeatures = new RuntimeFeaturesService(&owner);
            auto *recentFiles = new RecentFilesModel(configDir + "/recents.json", &owner);
            auto *devices = new DeviceModel(&owner, true);
            auto *computerModel = new ComputerModel(&owner);
            computerModel->setDevices(devices);
            auto *dependencies = new DependencyChecker(&owner);
            sessionState = new SessionState(&owner);
            auto *iconProvider = new IconProvider(config->iconTheme());
            engine.addImageProvider("thumbnail", new ThumbnailProvider);
            engine.addImageProvider("icon", iconProvider);
            engine.addImageProvider("pdfpreview", new PdfPreviewProvider);
            auto *dragHelper = new DragHelper(iconProvider, &owner);

            engine.addImportPath(moduleDir.path());
            engine.addImportPath(QStringLiteral(TEST_SOURCE_DIR "/src/qml"));
            QQmlContext *ctx = engine.rootContext();
            ctx->setContextProperty("config", config);
            ctx->setContextProperty("theme", theme);
            ctx->setContextProperty("tabModel", tabModel);
            ctx->setContextProperty("bookmarks", bookmarks);
            ctx->setContextProperty("fileOps", fileOps);
            ctx->setContextProperty("undoManager", undoManager);
            ctx->setContextProperty("clipboard", clipboard);
            ctx->setContextProperty("dragHelper", dragHelper);
            ctx->setContextProperty("fsModel", fsModel);
            ctx->setContextProperty("splitFsModel", splitFsModel);
            ctx->setContextProperty("millerParentModel", millerParentModel);
            ctx->setContextProperty("millerPreviewModel", millerPreviewModel);
            ctx->setContextProperty("devices", devices);
            ctx->setContextProperty("computerModel", computerModel);
            ctx->setContextProperty("recentFiles", recentFiles);
            ctx->setContextProperty("searchProxy", searchProxy);
            ctx->setContextProperty("searchResults", searchResults);
            ctx->setContextProperty("searchService", searchService);
            ctx->setContextProperty("splitSearchProxy", splitSearchProxy);
            ctx->setContextProperty("splitSearchResults", splitSearchResults);
            ctx->setContextProperty("splitSearchService", splitSearchService);
            ctx->setContextProperty("previewService", previewService);
            ctx->setContextProperty("metadataExtractor", metadataExtractor);
            ctx->setContextProperty("diskUsageService", diskUsageService);
            ctx->setContextProperty("remoteAccessService", remoteAccessService);
            ctx->setContextProperty("rcloneService", rcloneService);
            ctx->setContextProperty("runtimeFeatures", runtimeFeatures);
            ctx->setContextProperty("dependencies", dependencies);
            ctx->setContextProperty("sessionState", sessionState);

            if (!pickerRequest.isEmpty()) {
                picker = new FilePicker(pickerRequest, &owner);
                picker->setSourceModel(fsModel);
                fsModel->setRootPath(picker->initialFolder());
                ctx->setContextProperty("picker", picker);
            }
#ifdef HYPRFM_TERMINAL
            qmlRegisterType<TerminalView>("HyprFM", 1, 0, "TerminalView");   // as main.cpp does
            if (builtinTerminal) {
                ctx->setContextProperty("hyprfmBuiltinTerminal", true);
                ctx->setContextProperty("terminalDefaultFontSize", TerminalView::defaultFontPointSize());
            }
#endif
            engine.load(QUrl::fromLocalFile(picker ? QStringLiteral(TEST_SOURCE_DIR "/src/qml/FilePicker.qml")
                                                 : QStringLiteral(TEST_SOURCE_DIR "/src/qml/Main.qml")));
            if (engine.rootObjects().isEmpty())
                return false;
            window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window)
                return false;
            if (!picker) window->resize(1100, 700);
            window->show();
            return QTest::qWaitForWindowExposed(window);
        }

        static QQuickItem *findItem(QQuickItem *parent, const QString &name)
        {
            for (QQuickItem *child : parent->childItems()) {
                if (child->objectName() == name)
                    return child;
                if (QQuickItem *hit = findItem(child, name))
                    return hit;
            }
            return nullptr;
        }
        QQuickItem *item(const QString &name) { return findItem(window->contentItem(), name); }
        static QQuickItem *findText(QQuickItem *parent, const QString &text)
        {
            for (QQuickItem *child : parent->childItems()) {
                if (child->property("text").toString() == text)
                    return child;
                if (QQuickItem *hit = findText(child, text))
                    return hit;
            }
            return nullptr;
        }
        static QQuickItem *findTextContaining(QQuickItem *parent, const QString &text)
        {
            for (QQuickItem *child : parent->childItems()) {
                if (child->property("text").toString().contains(text))
                    return child;
                if (QQuickItem *hit = findTextContaining(child, text))
                    return hit;
            }
            return nullptr;
        }
        QPoint center(QQuickItem *it) { return it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint(); }
        void wheel(const QPoint &pos, const QPoint &angle)
        {
            QWheelEvent ev(pos, window->mapToGlobal(pos), QPoint(), angle,
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window, &ev);
        }
    };

private slots:
    void testStrataIconsUpdateWithThemeAndAppearancePreference()
    {
        App app;
        QVERIFY(app.load());
        auto *config = app.engine.rootContext()->contextProperty("config").value<ConfigManager *>();
        auto *theme = app.engine.rootContext()->contextProperty("theme").value<ThemeLoader *>();
        QVERIFY(config && theme);
        QCOMPARE(config->iconTheme(), QString("Strata"));
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        std::function<QQuickItem *(QQuickItem *)> interfaceIcon = [&](QQuickItem *parent) -> QQuickItem * {
            if (parent->property("iconName").toString() == "strata-icons") return parent;
            for (auto *child : parent->childItems())
                if (auto *found = interfaceIcon(child)) return found;
            return nullptr;
        };
        auto *button = app.item("viewModeButton_grid");
        QVERIFY(button);
        QTRY_VERIFY(interfaceIcon(button));
        QTRY_COMPARE(interfaceIcon(button)->property("effectiveColor").value<QColor>(), theme->accent());
        auto *grid = container->property("gridViewItem").value<QQuickItem *>();
        QVERIFY(grid);
        std::function<QQuickItem *(QQuickItem *)> fileImage = [&](QQuickItem *parent) -> QQuickItem * {
            for (auto *child : parent->childItems()) {
                if (child->property("source").toString().startsWith("image://icon/")
                    && child->property("source").toString().contains("name=")) return child;
                if (auto *found = fileImage(child)) return found;
            }
            return nullptr;
        };
        QTRY_VERIFY(fileImage(grid));
        auto *image = fileImage(grid);
        const QString oldSource = image->property("source").toString();
        theme->loadTheme("strata-nord-light", {QStringLiteral(TEST_SOURCE_DIR "/themes")});
        QTRY_VERIFY(image->property("source").toString() != oldSource);
        QVERIFY(image->property("source").toString().contains("fileColors=brand"));
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        auto *panel = app.window->findChild<QObject *>("settingsPanel");
        QVERIFY(panel);
        auto *choice = panel->findChild<QObject *>("fileIconColors");
        QVERIFY(choice);
        QVERIFY(QMetaObject::invokeMethod(choice, "selected", Q_ARG(int, 1), Q_ARG(QString, QString("Theme accent"))));
        QTRY_COMPARE(config->fileIconColors(), QString("theme"));
        QTRY_VERIFY(image->property("source").toString().contains("fileColors=theme"));
        // The saved preference also initializes a view created later.
        app.tabModel->activeTab()->setViewMode("compact");
        QTRY_VERIFY(container->property("compactViewItem").value<QQuickItem *>());
        auto *compact = container->property("compactViewItem").value<QQuickItem *>();
        QTRY_VERIFY(fileImage(compact));
        QVERIFY(fileImage(compact)->property("source").toString().contains("fileColors=theme"));
    }

    void testPreviewZoomIsLocalAndFollowsContent_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::addColumn<bool>("touchpad");
        for (bool quick : {true, false}) {
            QTest::newRow(quick ? "quick-mouse" : "miller-mouse") << quick << false;
            QTest::newRow(quick ? "quick-touchpad" : "miller-touchpad") << quick << true;
        }
    }

    void testPreviewZoomIsLocalAndFollowsContent()
    {
        QFETCH(bool, quick);
        QFETCH(bool, touchpad);
        App app;
        const QString first = app.home.filePath("first.txt");
        const QString second = app.home.filePath("second.txt");
        for (const auto &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray("Preview zoom content\n").repeated(120));
        }
        QVERIFY(app.load());
        app.tabModel->activeTab()->navigateTo(app.home.path());
        if (!quick) app.tabModel->activeTab()->setViewMode("miller");
        auto *baseModel = app.engine.rootContext()->contextProperty("fsModel").value<QObject *>();
        QTRY_COMPARE(baseModel->property("rootPath").toString(), app.home.path());
        QTRY_VERIFY(!baseModel->property("isLoading").toBool());
        // The two entry points have different signatures.
        auto preview = [&](const QString &path) {
            if (quick) return QMetaObject::invokeMethod(app.window, "showQuickPreview", Q_ARG(QVariant, path));
            return QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                             Q_ARG(QVariant, path), Q_ARG(QVariant, true));
        };
        QVERIFY(preview(first));
        QTRY_VERIFY(app.item(quick ? "quickPreviewZoom" : "millerPreviewZoom"));
        auto *controls = app.item(quick ? "quickPreviewZoom" : "millerPreviewZoom");
        QTRY_VERIFY(controls->property("available").toBool());
        auto *scope = quick ? app.window->property("quickPreview").value<QQuickItem *>() : app.item("millerPreviewColumn");
        QVERIFY(scope);
        QTRY_COMPARE(scope->property(quick ? "filePath" : "previewFilePath").toString(), first);
        auto *text = app.item(quick ? "quickPreviewText" : "millerPreviewText");
        QVERIFY(text);
        auto *document = text->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(document);
        QTRY_VERIFY2(document->textDocument()->toPlainText().contains("Preview zoom content"),
                     qPrintable(document->textDocument()->toPlainText().left(300)));
        QCOMPARE(controls->property("factor").toReal(), 1.0);
        const qreal fontSize = text->property("font").value<QFont>().pointSizeF();
        const int fileRowSize = app.sessionState->rowHeightMiller();
        const int gridZoom = app.sessionState->gridColumns();
        auto *slider = App::findItem(controls, "previewZoomSlider");
        QVERIFY(slider);
        QVERIFY(QMetaObject::invokeMethod(slider, "moved", Q_ARG(qreal, 2.0)));
        QTRY_COMPARE(controls->property("factor").toReal(), 2.0);
        QTRY_COMPARE(text->property("font").value<QFont>().pointSizeF(), fontSize * 2);
        QCOMPARE(app.sessionState->rowHeightMiller(), fileRowSize);
        QCOMPARE(app.sessionState->gridColumns(), gridZoom);
        QVERIFY(QMetaObject::invokeMethod(controls, "setFactor", Q_ARG(QVariant, 99)));
        QCOMPARE(controls->property("factor").toReal(), 4.0);
        QVERIFY(QMetaObject::invokeMethod(controls, "setFactor", Q_ARG(QVariant, 0)));
        QCOMPARE(controls->property("factor").toReal(), 0.25);
        scope->forceActiveFocus();
        QTest::keyClick(app.window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(controls->property("factor").toReal(), 1.0);
        const QPoint point = app.center(controls);
        QWheelEvent wheel(point, app.window->mapToGlobal(point), {}, QPoint(0, 120),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &wheel);
        QTRY_VERIFY(controls->property("factor").toReal() > 1.0);
        QCOMPARE(app.sessionState->rowHeightMiller(), fileRowSize);
        auto *content = controls->property("scrollTarget").value<QQuickItem *>();
        QVERIFY(content && content->isVisible());
        const QPoint contentPoint = app.center(content);
        const qreal beforeContentWheel = controls->property("factor").toReal();
        const QPointingDevice touchpadDevice("Preview touchpad", 8102, QInputDevice::DeviceType::TouchPad,
                                             QPointingDevice::PointerType::Finger,
                                             QInputDevice::Capability::Position | QInputDevice::Capability::Scroll,
                                             2, 0);
        QTest::keyPress(app.window, Qt::Key_Control);
        QWheelEvent contentWheel(contentPoint, app.window->mapToGlobal(contentPoint), touchpad ? QPoint(0, 10) : QPoint(), QPoint(0, 120),
                                 Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false,
                                 Qt::MouseEventNotSynthesized, touchpad ? &touchpadDevice : QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(app.window, &contentWheel);
        QTest::keyRelease(app.window, Qt::Key_Control);
        QVERIFY(controls->property("factor").toReal() > beforeContentWheel);
        QCOMPARE(app.sessionState->rowHeightMiller(), fileRowSize);
        QVERIFY(QMetaObject::invokeMethod(controls, "setFactor", Q_ARG(QVariant, 2.5)));
        QVERIFY(preview(second));
        QTRY_COMPARE(scope->property(quick ? "filePath" : "previewFilePath").toString(), second);
        QCOMPARE(controls->property("factor").toReal(), 2.5);
        auto *reset = App::findItem(controls, "resetPreviewZoom");
        QVERIFY(reset);
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(reset));
        QTRY_COMPARE(controls->property("factor").toReal(), 1.0);
    }

    void testMagnifiedImageCanScroll()
    {
        App app;
        const QString path = app.home.filePath("image.png");
        QImage fixture(800, 600, QImage::Format_RGB32);
        fixture.fill(Qt::red);
        QVERIFY(fixture.save(path));
        QVERIFY(app.load());
        QVERIFY(QMetaObject::invokeMethod(app.window, "showQuickPreview", Q_ARG(QVariant, path)));
        auto *controls = app.item("quickPreviewZoom");
        auto *image = app.item("quickVisualPreview");
        QVERIFY(controls && image);
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
        QVERIFY(QMetaObject::invokeMethod(controls, "setFactor", Q_ARG(QVariant, 3)));
        QTRY_VERIFY(image->property("contentHeight").toReal() > image->height());
        const QPoint point = app.center(image);
        app.wheel(point, QPoint(0, -120));
        QTRY_VERIFY(image->property("contentY").toReal() > 0);
        QCOMPARE(controls->property("factor").toReal(), 3.0);
    }

    void testPlainWheelStillScrollsFileViews_data()
    {
        QTest::addColumn<QString>("mode");
        for (const QString &mode : {QString("grid"), QString("compact"), QString("detailed"), QString("miller")})
            QTest::newRow(qPrintable(mode)) << mode;
    }
    void testPlainWheelStillScrollsFileViews()
    {
        QFETCH(QString, mode);
        App app;
        for (int i = 0; i < 200; ++i) {
            QFile file(app.home.filePath(QString("file%1.txt").arg(i, 3, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("scroll fixture\n");
        }
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(app.home.path());
        tab->setViewMode(mode);
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        const QByteArray viewProperty = (mode + "ViewItem").toLatin1();
        QTRY_VERIFY(container->property(viewProperty).value<QQuickItem *>());
        auto *view = container->property(viewProperty).value<QQuickItem *>();
        auto *scroll = mode == "detailed" ? App::findItem(view, "listView")
            : mode == "miller" ? App::findItem(view, "millerCurrentColumn") : view;
        QVERIFY(scroll);
        const bool horizontal = mode == "compact";
        QTRY_VERIFY(scroll->property(horizontal ? "contentWidth" : "contentHeight").toReal()
                    > (horizontal ? scroll->width() : scroll->height()));
        scroll->setProperty(horizontal ? "contentX" : "contentY", 0);
        app.wheel(app.center(scroll), QPoint(0, -120));
        QTRY_VERIFY_WITH_TIMEOUT(scroll->property(horizontal ? "contentX" : "contentY").toReal() > 0, 2000);
    }

    void testPreviewThemeAndFocus_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::addColumn<QString>("extension");
        for (bool quick : {true, false})
            for (const QString &ext : {QString("rs"), QString("py"), QString("js")})
                QTest::newRow(qPrintable((quick ? "quick-" : "miller-") + ext)) << quick << ext;
    }

    void testPreviewThemeAndFocus()
    {
        if (QStandardPaths::findExecutable("bat").isEmpty() && QStandardPaths::findExecutable("batcat").isEmpty())
            QSKIP("bat not installed");
        QFETCH(bool, quick);
        QFETCH(QString, extension);
        App app;
        const QString path = app.home.filePath("source." + extension);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(extension == "rs" ? "// Comment\nfn main() { let value = 42; }\n"
            : extension == "py" ? "# Comment\ndef main():\n    value = 42\n" : "// Comment\nfunction main() { const value = 42; }\n");
        file.close();
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(app.home.path());
        tab->setViewMode("miller");
        auto *model = app.engine.rootContext()->contextProperty("fsModel").value<QObject *>();
        QTRY_COMPARE(model->property("rootPath").toString(), app.home.path());
        QTRY_VERIFY(!model->property("isLoading").toBool());
        QVERIFY(QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                          Q_ARG(QVariant, path), Q_ARG(QVariant, true)));
        if (quick) QVERIFY(QMetaObject::invokeMethod(app.window, "showQuickPreview", Q_ARG(QVariant, path)));
        QTRY_VERIFY(app.item(quick ? "quickPreviewText" : "millerPreviewText"));
        auto *text = app.item(quick ? "quickPreviewText" : "millerPreviewText");
        QTRY_VERIFY(text->property("text").toString().contains("value"));
        const QString dark = text->property("text").toString();
        auto *theme = qobject_cast<ThemeLoader *>(app.engine.rootContext()->contextProperty("theme").value<QObject *>());
        QVERIFY(theme);
        theme->loadTheme("strata-nord-light", {QStringLiteral(TEST_SOURCE_DIR "/themes")});
        QTRY_VERIFY(text->property("text").toString().contains("value") && text->property("text").toString() != dark);
        const QString light = text->property("text").toString();
        theme->loadTheme("catppuccin-mocha", {QStringLiteral(TEST_SOURCE_DIR "/themes")});
        QTRY_VERIFY(text->property("text").toString().contains("value") && text->property("text").toString() != light);
        text->forceActiveFocus();
        QTest::keyClick(app.window, Qt::Key_Left, Qt::ControlModifier);
        auto *previewScope = quick ? app.window->property("quickPreview").value<QQuickItem *>() : app.item("millerPreviewColumn");
        QVERIFY(previewScope);
        if (quick) QTRY_VERIFY(!previewScope->property("active").toBool());
        QTRY_VERIFY(!previewScope->hasActiveFocus());
        QCOMPARE(tab->currentPath(), app.home.path());
        if (!quick) {
            QTest::keyClick(app.window, Qt::Key_Right, Qt::ControlModifier);
            QTRY_VERIFY(previewScope->hasActiveFocus());
            QCOMPARE(tab->currentPath(), app.home.path());
        }
    }

    void testStrataPaneControls_data()
    {
        QTest::addColumn<QString>("mode");
        for (const QString &mode : {QString("grid"), QString("detailed"), QString("miller"), QString("compact")})
            QTest::newRow(qPrintable(mode)) << mode;
    }
    void testStrataPaneControls()
    {
        QFETCH(QString, mode);
        App app;
        const QString source = app.home.filePath("source");
        const QString target = app.home.filePath("target");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(target));
        for (const auto &name : {"copy.txt", "move.txt", "untouched.txt"}) {
            QFile file(source + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->setViewMode(mode);
        tab->navigateTo(source);
        tab->resetSecondaryTo(target);
        tab->setSplitViewEnabled(true);
        auto focusFile = [&](const QString &name) {
            return QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                             Q_ARG(QVariant, source + "/" + name), Q_ARG(QVariant, true));
        };
        QVERIFY(focusFile("copy.txt"));
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        QTest::keyClick(app.window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(QFile::exists(target + "/copy.txt"));
        QVERIFY(QFile::exists(source + "/copy.txt"));
        QVERIFY(focusFile("move.txt"));
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        QTest::keyClick(app.window, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(QFile::exists(target + "/move.txt"));
        QVERIFY(!QFile::exists(source + "/move.txt"));
        QVERIFY(focusFile("untouched.txt"));
        QTest::keyClick(app.window, Qt::Key_L, Qt::ControlModifier);
        QTest::keyClick(app.window, Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(app.window, Qt::Key_F3);
        QVERIFY(tab->splitViewEnabled());
        QVERIFY(QFile::exists(source + "/untouched.txt"));
        QVERIFY(!QFile::exists(target + "/untouched.txt"));
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTest::keyClick(app.window, Qt::Key_F6);
        QTRY_COMPARE(app.window->property("activePane").toString(), QString("secondary"));
        QVERIFY(QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("secondary")),
                                          Q_ARG(QVariant, target + "/copy.txt"), Q_ARG(QVariant, true)));
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        QTest::keyClick(app.window, Qt::Key_F3);
        QTRY_VERIFY(!tab->splitViewEnabled());
        QCOMPARE(tab->currentPath(), target);
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        QVariant selection;
        QVERIFY(QMetaObject::invokeMethod(app.window, "getSelectedPaths", Q_RETURN_ARG(QVariant, selection),
                                          Q_ARG(QVariant, QString("primary"))));
        QCOMPARE(selection.toStringList(), QStringList{target + "/copy.txt"});
    }

    void testInterfaceWheelZoomAcrossColumns()
    {
        App app;
        QVERIFY(app.load());
        app.tabModel->activeTab()->navigateTo(app.home.path());
        app.tabModel->activeTab()->setViewMode("miller");
        auto *config = qobject_cast<ConfigManager *>(app.engine.rootContext()->contextProperty("config").value<QObject *>());
        QVERIFY(config);
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        QTRY_VERIFY(container->property("millerViewItem").value<QQuickItem *>());
        auto *miller = container->property("millerViewItem").value<QQuickItem *>();
        const int rowHeight = app.sessionState->rowHeightMiller();
        // Ctrl+Shift+Wheel over the sidebar, parent column and current column
        // sizes the shared interface text.
        const QPoint sidebarPoint = app.center(app.item("quickAccess_Home"));
        const QPoint points[] = {sidebarPoint,
            miller->mapToScene(QPointF(miller->width() * 0.1, 80)).toPoint(),
            miller->mapToScene(QPointF(miller->width() * 0.45, 80)).toPoint()};
        for (const QPoint &point : points) {
            config->saveSettings({{"fontSize", 10}});
            QWheelEvent wheel(point, app.window->mapToGlobal(point), {}, QPoint(0,120), Qt::NoButton,
                              Qt::ControlModifier | Qt::ShiftModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(app.window, &wheel);
            QTRY_COMPARE(config->fontSize(), 11.0);
        }
        QCOMPARE(app.sessionState->rowHeightMiller(), rowHeight);
        // Plain Ctrl+Wheel over the sidebar zooms nothing: no file view there.
        QWheelEvent sidebarWheel(sidebarPoint, app.window->mapToGlobal(sidebarPoint), {}, QPoint(0, 120),
                                 Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &sidebarWheel);
        QTest::qWait(50);
        QCOMPARE(config->fontSize(), 11.0);
        QCOMPARE(app.sessionState->rowHeightMiller(), rowHeight);
        QTest::keyClick(app.window, Qt::Key_Minus, Qt::ControlModifier);
        QTRY_COMPARE(config->fontSize(), 10.0);
        QTest::keyClick(app.window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(config->fontSize(), 0.0);
        app.tabModel->activeTab()->setViewMode("grid");
        QTRY_VERIFY(container->property("gridViewItem").value<QQuickItem *>());
        auto *grid = container->property("gridViewItem").value<QQuickItem *>();
        QVERIFY(QMetaObject::invokeMethod(grid, "setZoomIconSize", Q_ARG(QVariant, 184), Q_ARG(QVariant, false)));
        const QPoint gridPoint = app.center(grid);
        // Ctrl+Wheel: this pane's icons grow ~15% a notch; text stays.
        QWheelEvent iconsWheel(gridPoint, app.window->mapToGlobal(gridPoint), {}, QPoint(0, 120), Qt::NoButton,
                               Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &iconsWheel);
        QTRY_COMPARE(qRound(grid->property("zoomIconSize").toDouble()), qRound(184 * 1.15));
        QCOMPARE(config->fontSize(), 0.0);
        QTest::keyClick(app.window, Qt::Key_Minus, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(qRound(grid->property("zoomIconSize").toDouble()), 184);
        QVERIFY(QMetaObject::invokeMethod(grid, "setZoomIconSize", Q_ARG(QVariant, 96), Q_ARG(QVariant, false)));
        QTest::keyClick(app.window, Qt::Key_0, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(qRound(grid->property("zoomIconSize").toDouble()), 184);
        // ... and it animates there: what is drawn follows the target.
        QTRY_COMPARE(grid->property("iconSize").toInt(), 184);
    }

    // Split panes in two folders: F4 in each pane opens that pane's own
    // terminal, under that pane, its shell in that pane's folder (the panel
    // used to be one, across the window, in the active pane's folder only).
    // F4 closes just the active pane's; clicking into a terminal makes its
    // pane the active one and keeps the keyboard there.
    void testEachSplitPaneOpensItsOwnTerminal()
    {
#ifndef HYPRFM_TERMINAL
        QSKIP("built without the built-in terminal");
#else
        qputenv("SHELL", "/bin/sh");
        qputenv("ENV", "");   // no ~/.shrc for an interactive sh
        App app;
        app.builtinTerminal = true;
        QVERIFY(app.load());
        const QString left = app.home.path() + "/left";
        const QString right = app.home.path() + "/right";
        QVERIFY(QDir().mkpath(left) && QDir().mkpath(right));
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(left);
        tab->resetSecondaryTo(right);
        tab->setSplitViewEnabled(true);
        QTRY_COMPARE(app.window->property("splitTransitionProgress").toReal(), 1.0);

        auto *primaryFrame = app.item("primaryPaneFrame");
        auto *secondaryFrame = app.item("secondaryPaneLoader");
        QVERIFY(primaryFrame && secondaryFrame);
        QTRY_VERIFY(secondaryFrame->property("item").value<QQuickItem *>());
        auto *primaryView = app.item("primaryFileView");
        auto *secondaryView = secondaryFrame->property("item").value<QQuickItem *>()
                                  ->property("fileView").value<QQuickItem *>();
        QVERIFY(primaryView && secondaryView);
        auto shellDir = [](QObject *shell) {
            QString dir;
            QMetaObject::invokeMethod(shell, "currentDirectory", Q_RETURN_ARG(QString, dir));
            return dir;
        };
        auto sceneRect = [](QQuickItem *it) { return it->mapRectToScene(QRectF(0, 0, it->width(), it->height())); };
        auto activePane = [&] { return app.window->property("activePane").toString(); };

        // Left pane: click its empty space, F4.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(primaryView));
        QTRY_COMPARE(activePane(), QString("primary"));
        QTest::keyClick(app.window, Qt::Key_F4);
        auto *leftTerm = app.item("primaryPaneTerminal");
        QVERIFY(leftTerm);
        QTRY_VERIFY(leftTerm->isVisible() && leftTerm->height() > 0);
        auto *leftShell = leftTerm->property("terminal").value<QQuickItem *>();
        QVERIFY(leftShell);
        QTRY_COMPARE_WITH_TIMEOUT(shellDir(leftShell), left, 5000);
        QVERIFY(sceneRect(primaryFrame).contains(sceneRect(leftTerm).adjusted(1, 1, -1, -1)));

        // Right pane: click, F4 -- a second shell, in the right folder.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(secondaryView));
        QTRY_COMPARE(activePane(), QString("secondary"));
        QTest::keyClick(app.window, Qt::Key_F4);
        auto *rightTerm = app.item("secondaryPaneTerminal");
        QVERIFY(rightTerm);
        QTRY_VERIFY(rightTerm->isVisible() && rightTerm->height() > 0);
        auto *rightShell = rightTerm->property("terminal").value<QQuickItem *>();
        QVERIFY(rightShell && rightShell != leftShell);
        QTRY_COMPARE_WITH_TIMEOUT(shellDir(rightShell), right, 5000);
        QVERIFY(sceneRect(secondaryFrame).contains(sceneRect(rightTerm).adjusted(1, 1, -1, -1)));
        QVERIFY(leftTerm->isVisible());
        QCOMPARE(shellDir(leftShell), left);

        // F4 from inside the right terminal closes only the right one.
        QTRY_VERIFY(rightShell->hasActiveFocus());
        QTest::keyClick(app.window, Qt::Key_F4);
        QTRY_VERIFY(!rightTerm->isVisible());
        QVERIFY(leftTerm->isVisible());

        // Clicking into the left terminal makes the left pane active and the
        // keyboard stays in the terminal; F4 then closes the left one.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(leftShell));
        QTRY_COMPARE(activePane(), QString("primary"));
        QTest::qWait(100);
        QVERIFY(leftShell->hasActiveFocus());
        QTest::keyClick(app.window, Qt::Key_F4);
        QTRY_VERIFY(!leftTerm->isVisible());

        // F4 again in the right pane brings back its running shell.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(secondaryView));
        QTRY_COMPARE(activePane(), QString("secondary"));
        QTest::keyClick(app.window, Qt::Key_F4);
        QTRY_VERIFY(rightTerm->isVisible());
        QCOMPARE(rightTerm->property("terminal").value<QQuickItem *>(), rightShell);
        QVERIFY(!leftTerm->isVisible());

        // With builtin_terminal = true, Open in Terminal (Ctrl+Alt+T) in the
        // left pane opens the left pane's terminal, not the right one's.
        auto *config = qobject_cast<ConfigManager *>(app.engine.rootContext()->contextProperty("config").value<QObject *>());
        QVERIFY(config);
        config->saveSettings({{"builtinTerminal", true}});
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(primaryView));
        QTRY_COMPARE(activePane(), QString("primary"));
        QTest::keyClick(app.window, Qt::Key_T, Qt::ControlModifier | Qt::AltModifier);
        QTRY_VERIFY(leftTerm->isVisible());
        QCOMPARE(leftTerm->property("terminal").value<QQuickItem *>(), leftShell);
        QTRY_VERIFY(leftShell->hasActiveFocus());
        QCOMPARE(shellDir(leftShell), left);
        QVERIFY(rightTerm->isVisible());
        QCOMPARE(shellDir(rightShell), right);
#endif
    }

    // User flag: Computer -> split -> click the left pane -> F4 -> click the
    // right pane -> F4 closed the left terminal and opened none. A click on the
    // Computer page did not make its pane active (only a drive card did), which
    // also sent a sidebar place to the right pane after a click on the left one.
    void testComputerPagesTakeClicksForTerminalsAndSidebar()
    {
#ifndef HYPRFM_TERMINAL
        QSKIP("built without the built-in terminal");
#else
        qputenv("SHELL", "/bin/sh");
        qputenv("ENV", "");   // no ~/.shrc for an interactive sh
        App app;
        app.builtinTerminal = true;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(app.home.path());
        auto activePane = [&] { return app.window->property("activePane").toString(); };

        // Computer, then F3 for the split.
        auto *computer = app.item("quickAccess_Computer");
        QVERIFY(computer && computer->isVisible());
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(computer));
        QTRY_COMPARE(tab->currentPath(), QString("computer:///"));
        QTest::keyClick(app.window, Qt::Key_F3);
        QTRY_COMPARE(app.window->property("splitTransitionProgress").toReal(), 1.0);
        auto *secondaryLoader = app.item("secondaryPaneLoader");
        QVERIFY(secondaryLoader);
        QTRY_VERIFY(secondaryLoader->property("item").value<QQuickItem *>());
        auto *left = app.item("primaryFileView");
        auto *right = secondaryLoader->property("item").value<QQuickItem *>()
                          ->property("fileView").value<QQuickItem *>();
        QVERIFY(left && right);
        QTRY_COMPARE(tab->secondaryCurrentPath(), QString("computer:///"));
        QTRY_VERIFY(left->property("computerViewItem").value<QQuickItem *>()
                    && right->property("computerViewItem").value<QQuickItem *>());
        // The page itself, not a drive card (a card already took the click):
        // its "Devices and drives" subtitle.
        auto emptySpot = [&](QQuickItem *container) {
            QQuickItem *page = container->property("computerViewItem").value<QQuickItem *>();
            QQuickItem *subtitle = page ? App::findText(page, QStringLiteral("Devices and drives")) : nullptr;
            return subtitle ? app.center(subtitle) : QPoint(-1, -1);
        };
        QVERIFY(emptySpot(left).x() >= 0 && emptySpot(right).x() >= 0);
        auto *leftTerm = app.item("primaryPaneTerminal");
        auto *rightTerm = app.item("secondaryPaneTerminal");
        QVERIFY(leftTerm && rightTerm);

        // Left pane click, F4: the left terminal.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, emptySpot(left));
        QTRY_COMPARE(activePane(), QString("primary"));
        QTest::keyClick(app.window, Qt::Key_F4);
        QTRY_VERIFY(leftTerm->isVisible());

        // Right pane click, F4: the right terminal, and the left one stays.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, emptySpot(right));
        QTRY_COMPARE(activePane(), QString("secondary"));
        QTest::keyClick(app.window, Qt::Key_F4);
        QTRY_VERIFY(rightTerm->isVisible());
        QVERIFY(leftTerm->isVisible());
        auto *rightShell = rightTerm->property("terminal").value<QQuickItem *>();
        QVERIFY(rightShell);
        QTRY_VERIFY(rightShell->hasActiveFocus());

        // Left pane click, then Home in the sidebar: Home opens on the left.
        QTest::mouseClick(app.window, Qt::LeftButton, {}, emptySpot(left));
        QTRY_COMPARE(activePane(), QString("primary"));
        QVERIFY(!rightShell->hasActiveFocus());
        auto *home = app.item("quickAccess_Home");
        QVERIFY(home && home->isVisible());
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(home));
        QTRY_COMPARE(tab->currentPath(), QDir::homePath());
        QCOMPARE(tab->secondaryCurrentPath(), QString("computer:///"));

        // A press on a pane's header bar makes that pane active as well.
        auto *rightHeader = app.item("secondaryPaneHeader");
        QVERIFY(rightHeader && rightHeader->isVisible());
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(rightHeader));
        QTRY_COMPARE(activePane(), QString("secondary"));
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(app.item("primaryPaneHeader")));
        QTRY_COMPARE(activePane(), QString("primary"));
#endif
    }

    // Ctrl+Shift+Wheel over the Icons view sizes the text only: a bigger label
    // makes the cell taller instead of shrinking the icon (it used to shrink
    // as the text grew, and grow back as it shrank). Ctrl+Wheel sizes icons.
    void testIconsKeepTheirSizeWhenInterfaceTextGrows()
    {
        App app;
        QVERIFY(app.load());
        for (const char *name : {"alpha.txt", "beta.txt", "gamma.txt"})
            QVERIFY(QFile(app.home.path() + "/" + name).open(QIODevice::WriteOnly));
        app.tabModel->activeTab()->navigateTo(app.home.path());
        app.tabModel->activeTab()->setViewMode("grid");
        auto *config = qobject_cast<ConfigManager *>(app.engine.rootContext()->contextProperty("config").value<QObject *>());
        QVERIFY(config);
        config->saveSettings({{"fontSize", 10}});
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        QTRY_VERIFY(container->property("gridViewItem").value<QQuickItem *>());
        auto *grid = container->property("gridViewItem").value<QQuickItem *>();
        QTRY_VERIFY(grid->property("count").toInt() >= 3);
        const QPoint point = app.center(grid);
        auto ctrlWheel = [&](int notches, Qt::KeyboardModifiers mods) {
            for (int i = 0; i < qAbs(notches); ++i) {
                QWheelEvent wheel(point, app.window->mapToGlobal(point), {}, QPoint(0, notches > 0 ? 120 : -120),
                                  Qt::NoButton, mods, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(app.window, &wheel);
            }
        };
        const int icon = grid->property("iconSize").toInt();
        const int label = grid->property("labelHeight").toInt();
        const qreal cell = grid->property("cellHeight").toReal();

        ctrlWheel(4, Qt::ControlModifier | Qt::ShiftModifier);   // text 10 -> 14 pt
        QTRY_COMPARE(config->fontSize(), 14.0);
        QTRY_VERIFY(grid->property("labelHeight").toInt() > label);
        QCOMPARE(grid->property("iconSize").toInt(), icon);
        QCOMPARE(grid->property("cellHeight").toReal(),
                 cell + grid->property("labelHeight").toInt() - label);

        ctrlWheel(-6, Qt::ControlModifier | Qt::ShiftModifier);  // 14 -> 8 pt
        QTRY_COMPARE(config->fontSize(), 8.0);
        QTRY_COMPARE(grid->property("labelHeight").toInt(), label);
        QCOMPARE(grid->property("iconSize").toInt(), icon);

        ctrlWheel(1, Qt::ControlModifier);                       // icons, not text
        QTRY_VERIFY(grid->property("iconSize").toInt() > icon);
        QCOMPARE(config->fontSize(), 8.0);
    }

    void testPdfAndTablePreviewZoom_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("quick-preview") << true;
        QTest::newRow("miller-pane") << false;
    }

    void testPdfAndTablePreviewZoom()
    {
        if (QStandardPaths::findExecutable("pdftoppm").isEmpty()
            || QStandardPaths::findExecutable("pdfinfo").isEmpty())
            QSKIP("PDF preview requires poppler-utils");
        QFETCH(bool, quick);
        App app;
        const QString pdf = app.home.filePath("preview.pdf");
        {
            QPdfWriter writer(pdf);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            painter.drawText(QPoint(100, 200), "Preview zoom page one");
            QVERIFY(writer.newPage());
            painter.drawText(QPoint(100, 200), "Preview zoom page two");
        }
        const QString csv = app.home.filePath("preview.csv");
        QFile tableFile(csv);
        QVERIFY(tableFile.open(QIODevice::WriteOnly));
        tableFile.write("Name,Value\nZoom,3\n");
        tableFile.close();
        QVERIFY(app.load());
        app.tabModel->activeTab()->navigateTo(app.home.path());
        if (!quick) app.tabModel->activeTab()->setViewMode("miller");
        auto *model = app.engine.rootContext()->contextProperty("fsModel").value<QObject *>();
        QTRY_COMPARE(model->property("rootPath").toString(), app.home.path());
        QTRY_VERIFY(!model->property("isLoading").toBool());
        auto preview = [&](const QString &path) {
            if (quick) return QMetaObject::invokeMethod(app.window, "showQuickPreview", Q_ARG(QVariant, path));
            return QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                             Q_ARG(QVariant, path), Q_ARG(QVariant, true));
        };
        QVERIFY(preview(pdf));
        QTRY_VERIFY(app.item(quick ? "quickPdfPreviewImage" : "millerPdfPreviewImage"));
        auto *image = app.item(quick ? "quickPdfPreviewImage" : "millerPdfPreviewImage");
        auto *controls = app.item(quick ? "quickPreviewZoom" : "millerPreviewZoom");
        auto *scope = quick ? app.window->property("quickPreview").value<QQuickItem *>() : app.item("millerPreviewColumn");
        QVERIFY(image && controls && scope);
        QTRY_VERIFY(image->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 15000);
        const QPoint point = app.center(image);
        app.wheel(point, QPoint(0, -120));
        QTRY_COMPARE(scope->property("pdfPageIndex").toInt(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 15000);
        QVERIFY(QMetaObject::invokeMethod(controls, "setFactor", Q_ARG(QVariant, 3)));
        QTRY_VERIFY(image->property("contentHeight").toReal() > image->height());
        app.wheel(point, QPoint(0, -120));
        QTRY_VERIFY(image->property("contentY").toReal() > 0);
        QCOMPARE(scope->property("pdfPageIndex").toInt(), 1);
        QVERIFY(preview(csv));
        QTRY_VERIFY(scope->property("isTable").toBool());
        auto *table = app.item(quick ? "quickPreviewTable" : "millerPreviewTable");
        QVERIFY(table && table->isVisible());
        QCOMPARE(controls->property("factor").toReal(), 3.0);
        const qreal magnifiedFont = table->property("fontSize").toReal();
        QVERIFY(QMetaObject::invokeMethod(controls, "reset"));
        QTRY_COMPARE(table->property("fontSize").toReal(), magnifiedFont / 3);
    }

    void testFilePickerSaveAndOverwrite()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QString::fromUtf8("lezione è #1.pdf"));
        QFile existing(path);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("original"); existing.close();
        App app;
        QVERIFY(app.load(QJsonObject{{"mode", "save"}, {"current_folder", dir.path()},
                                     {"current_name", QFileInfo(path).fileName()}}));
        QCOMPARE(app.window->size(), QSize(980, 640));
        QCOMPARE(app.window->type(), Qt::Dialog);
        QVERIFY(app.window->flags().testFlag(Qt::FramelessWindowHint));
        QSignalSpy complete(app.picker, &FilePicker::completed);
        QSignalSpy overwrite(app.picker, &FilePicker::overwriteRequested);
        auto *filename = app.item("pickerFilename");
        QVERIFY(filename);
        QCOMPARE(filename->property("text").toString(), QFileInfo(path).fileName());
        QVERIFY(!app.picker->submit(dir.path(), "../escape", {}));
        QVERIFY(!app.picker->submit(dir.path(), QFileInfo(path).fileName(), {}));
        QCOMPARE(overwrite.size(), 1);
        QCOMPARE(complete.size(), 0);
        QVERIFY(QMetaObject::invokeMethod(app.item("pickerOverwrite"), "accept"));
        QCOMPARE(complete.size(), 1);
        QCOMPARE(complete.first().first().toJsonObject().value("uris").toArray().first().toString(),
                 QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("original"));
        filename->setProperty("text", "new.pdf");
        QVERIFY(QMetaObject::invokeMethod(app.window, "submit", Q_ARG(QVariant, false)));
        QCOMPARE(complete.size(), 2);
        QVERIFY(!QFileInfo::exists(dir.filePath("new.pdf")));
        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            QTRY_VERIFY(!app.item("pickerOverwrite")->isVisible());
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_TEST_GRAB"));
        }
    }

    void testFilePickerCompactViewSelectsAFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("chosen.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        App app;
        QDir().mkpath(app.home.path() + "/.config/hyprfm");
        QFile cfg(app.home.path() + "/.config/hyprfm/config.toml");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[general]\ndefault_view = \"compact\"\n");
        cfg.close();
        QVERIFY(app.load(QJsonObject{{"mode", "open"}, {"current_folder", dir.path()}}));
        QCOMPARE(app.window->property("viewMode").toString(), QString("compact"));
        auto *container = app.item("pickerFiles");
        QVERIFY(container);
        QTRY_VERIFY(container->property("compactViewItem").value<QQuickItem *>());
        auto *compact = container->property("compactViewItem").value<QQuickItem *>();
        QTRY_COMPARE(compact->property("count").toInt(), 1);
        compact->forceActiveFocus();
        QTest::keyClick(app.window, Qt::Key_Home);
        QTRY_COMPARE(app.window->property("selectedPaths").toStringList(), QStringList{path});
        QSignalSpy complete(app.picker, &FilePicker::completed);
        QVERIFY(QMetaObject::invokeMethod(app.window, "submit", Q_ARG(QVariant, false)));
        QCOMPARE(complete.size(), 1);
        QCOMPARE(complete.first().first().toJsonObject().value("uris").toArray().first().toString(), QUrl::fromLocalFile(path).toString());
    }

    void testFilePickerCompactLayoutAndControls()
    {
        QTemporaryDir dir;
        const QString downloads = dir.filePath("Downloads");
        QVERIFY(QDir().mkpath(downloads));
        for (const auto &name : {"Documents", "Images", "Projects"})
            QVERIFY(QDir(downloads).mkdir(name));
        for (const auto &name : {"Lecture notes.pdf", "Research.pdf", "Report.pdf", "Reading list.txt", "References.csv"}) {
            QFile file(downloads + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        QJsonArray filters{QJsonObject{{"name", "All files"}, {"rules", QJsonArray{QJsonObject{{"type", 0}, {"pattern", "*"}}}}},
                           QJsonObject{{"name", "PDF documents"}, {"rules", QJsonArray{QJsonObject{{"type", 0}, {"pattern", "*.pdf"}}}}}};
        App app;
        QVERIFY(app.load(QJsonObject{{"mode", "save"}, {"current_folder", downloads},
                                    {"current_name", "Lecture notes.pdf"}, {"filters", filters}}));
        QCOMPARE(app.window->size(), QSize(980, 640));
        auto *filename = app.item("pickerFilename");
        auto *accept = app.item("pickerAccept");
        auto *cancel = app.item("pickerCancel");
        auto *toolbar = app.item("pickerToolbar");
        QVERIFY(filename && accept && cancel && toolbar);
        QVERIFY(toolbar->property("filePickerMode").toBool());
        QVERIFY(!app.item("viewModeButton_miller"));
        auto *listButton = app.item("viewModeButton_detailed");
        QVERIFY(listButton);
        QTest::mouseClick(app.window, Qt::LeftButton, Qt::NoModifier,
                          listButton->mapToScene(QPointF(listButton->width()/2, listButton->height()/2)).toPoint());
        QCOMPARE(app.window->property("viewMode").toString(), QStringLiteral("detailed"));
        if (qEnvironmentVariableIsSet("HYPRFM_COMPACT_GRAB")) {
            QTest::qWait(400);
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_COMPACT_GRAB"));
        }
        auto *newFolder = app.item("pickerNewFolder");
        QVERIFY(newFolder);
        QVERIFY(QMetaObject::invokeMethod(newFolder, "open"));
        QTRY_VERIFY(newFolder->isVisible());
        app.item("pickerFolderName")->setProperty("text", "New folder");
        QVERIFY(QMetaObject::invokeMethod(newFolder, "createFolder"));
        QVERIFY(QFileInfo(downloads + "/New folder").isDir());
        QTRY_VERIFY(!newFolder->isVisible());
        app.window->resize(620, 400);
        QTest::qWait(100);
        QVERIFY(!app.item("pickerSidebar")->isVisible());
        const QRectF bounds(0, 0, 620, 400);
        for (auto *item : {filename, accept, cancel, toolbar})
            QVERIFY2(bounds.contains(item->mapRectToScene(item->boundingRect())), qPrintable(item->objectName()));
        QVERIFY(filename->width() > 400);
        QSignalSpy complete(app.picker, &FilePicker::completed);
        filename->setProperty("text", "New document.pdf");
        accept->forceActiveFocus();
        QTest::keyClick(app.window, Qt::Key_Return);
        QCOMPARE(complete.size(), 1);
    }

    void testFilePickerBackReturnsToStartFolder()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("sub"));
        App app;
        QVERIFY(app.load(QJsonObject{{"mode", "open"}, {"current_folder", dir.path()}}));
        auto *fs = qobject_cast<FileSystemModel *>(app.picker->sourceModel());
        QVERIFY(fs);
        const QString start = fs->rootPath();

        QVERIFY(QMetaObject::invokeMethod(app.window, "navigate",
                                          Q_ARG(QVariant, dir.filePath("sub")), Q_ARG(QVariant, true)));
        QCOMPARE(fs->rootPath(), dir.filePath("sub"));

        QVERIFY(QMetaObject::invokeMethod(app.window, "goBack"));
        QCOMPARE(fs->rootPath(), start);
    }

    void testFilePickerEscapeClosesOpenDropdownFirst()
    {
        App app;
        QVERIFY(app.load(QJsonObject{{"mode", "open"}}));
        app.window->show();
        auto *filter = app.window->findChild<QObject *>("pickerFilter");
        QVERIFY(filter);
        filter->setProperty("dropdownOpen", true);

        QVERIFY(QMetaObject::invokeMethod(app.window, "handleEscape"));
        QCOMPARE(filter->property("dropdownOpen").toBool(), false);
        QVERIFY(app.window->isVisible());

        QVERIFY(QMetaObject::invokeMethod(app.window, "handleEscape"));
        QVERIFY(!app.window->isVisible());
    }

    void testFilePickerStripsMarkupFromAppLabels()
    {
        const QString markup = QStringLiteral("<img src=\"http://example.invalid/x?leak=1\">");
        const QJsonObject item{{"id", "a"}, {"label", markup}};
        FilePicker picker(QJsonObject{
            {"title", markup}, {"accept_label", markup},
            {"filters", QJsonArray{QJsonObject{{"name", markup}, {"rules", QJsonArray{}}}}},
            {"choices", QJsonArray{QJsonObject{{"id", "c"}, {"label", markup}, {"items", QJsonArray{item}}}}}});

        const QString options = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(picker.options())).toJson());
        QVERIFY2(!options.contains(u'<'), qPrintable(options));
        QVERIFY(options.contains(QStringLiteral("img src"))); // the text itself survives
        QCOMPARE(picker.options().value("choices").toList().first().toMap().value("id").toString(),
                 QStringLiteral("c"));
    }

    void testFilePickerFiltersSelectionAndFolders()
    {
        QTemporaryDir dir;
        for (const auto &name : {"test.pdf", "photo.png", "another.pdf"}) {
            QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(QDir(dir.path()).mkdir("subfolder"));
        FileSystemModel fs;
        QJsonArray filters{QJsonObject{{"name", "PDF"}, {"rules", QJsonArray{QJsonObject{{"type", 0}, {"pattern", "*.pdf"}}}}}};
        FilePicker picker(QJsonObject{{"mode", "open"}, {"filters", filters}});
        picker.setSourceModel(&fs);
        QVERIFY(picker.navigate(dir.path()));
        QTRY_COMPARE(picker.rowCount(), 3); // Two PDFs, plus navigable directories.
        QSignalSpy complete(&picker, &FilePicker::completed);
        QVERIFY(!picker.submit(dir.path(), {}, {dir.filePath("test.pdf"), dir.filePath("another.pdf")}));
        QVERIFY(!picker.submit(dir.path(), {}, {dir.filePath("missing.pdf")}));
        QVERIFY(!picker.submit(dir.path(), {}, {dir.filePath("subfolder")}));
        QVERIFY(picker.submit(dir.path(), {}, {dir.filePath("test.pdf")}));
        QCOMPARE(complete.size(), 1);
        FilePicker multi(QJsonObject{{"mode", "open"}, {"multiple", true}});
        multi.setSourceModel(&fs);
        QVERIFY(multi.submit(dir.path(), {}, {dir.filePath("test.pdf"), dir.filePath("another.pdf")}));
        FilePicker folder(QJsonObject{{"mode", "open"}, {"directory", true}});
        folder.setSourceModel(&fs);
        QTRY_COMPARE(folder.rowCount(), 1);
        QVERIFY(folder.submit(dir.path(), {}, {}));
        QVERIFY(!folder.navigate("network:///"));
        FilePicker batch(QJsonObject{{"mode", "save_files"}, {"directory", true}, {"files", QJsonArray{"test.pdf", "test.pdf"}}});
        batch.setSourceModel(&fs);
        QSignalSpy batchComplete(&batch, &FilePicker::completed);
        QVERIFY(batch.submit(dir.path(), {}, {}));
        auto uris = batchComplete.first().first().toJsonObject().value("uris").toArray();
        QCOMPARE(uris.size(), 2);
        QVERIFY(uris[0] != uris[1]);
        QVERIFY(!QFileInfo::exists(QUrl(uris[0].toString()).toLocalFile()));
    }
    // #74: an unreadable folder shows why in the view, not an empty grid.
    void testUnreadableFolderShowsLoadError()
    {
        App app;
        QVERIFY(app.load());

        QTemporaryDir dir;
        const QString locked = dir.path() + "/locked";
        QVERIFY(QDir().mkpath(locked));
        QVERIFY(QFile::setPermissions(locked, QFileDevice::Permissions()));
        auto restore = qScopeGuard([&] {
            QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        });
        if (QDir(locked).isReadable())
            QSKIP("running as root: permissions are not enforced");

        QQuickItem *view = app.item(QStringLiteral("primaryFileView"));
        QVERIFY(view);
        QQuickItem *errorText = view->findChild<QQuickItem *>(QStringLiteral("loadErrorText"));
        QVERIFY(errorText);

        app.tabModel->activeTab()->navigateTo(locked);
        QTRY_VERIFY(errorText->isVisible());
        QVERIFY(errorText->property("text").toString().contains(QStringLiteral("permission")));

        // The Computer page lists drives, not a folder: the error the file
        // model still holds from the locked folder must not cover it.
        app.tabModel->activeTab()->navigateTo(QStringLiteral("computer:///"));
        QTRY_VERIFY(app.item("computerView") && app.item("computerView")->isVisible());
        QVERIFY(!errorText->isVisible());

        app.tabModel->activeTab()->navigateTo(dir.path());
        QTRY_VERIFY(!errorText->isVisible());
    }

    // Search results land in the proxy, but the view and the status bar have
    // to show them: the grid is bound to the proxy through paneModel() and
    // the item count reads the proxy's rowCount().
    void testSearchResultsReachTheGridAndStatusBar()
    {
        App app;
        QVERIFY(app.load());

        QTemporaryDir dir;
        QDir().mkpath(dir.path() + "/Final Project");
        for (const char *name : {"final_report.txt", "notfinal.md", "other.txt",
                                 "Final Project/report.pdf", "Final Project/final.txt"}) {
            QFile f(dir.path() + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }

        app.tabModel->activeTab()->navigateTo(dir.path());
        QTRY_COMPARE(app.tabModel->activeTab()->currentPath(), dir.path());

        QVERIFY(QMetaObject::invokeMethod(app.window, "openSearch"));
        QTRY_VERIFY(app.window->property("searchMode").toBool());
        QSignalSpy finished(app.searchService, &SearchService::searchFinished);
        QVERIFY(QMetaObject::invokeMethod(app.window, "handleSearchQuery",
                                          Q_ARG(QVariant, QStringLiteral("final"))));
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 10000);
        QVERIFY(app.searchProxy->rowCount() > 0);

        QQuickItem *view = app.item(QStringLiteral("primaryFileView"));
        QVERIFY(view);
        QObject *grid = view->property("gridViewItem").value<QObject *>();
        QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), app.searchProxy->rowCount());
        // count follows the model even when every delegate fails to build
        // (a required property the model doesn't supply), so look for the
        // label a delegate draws.
        // The matched part of the name is marked up in accent + bold.
        // (The grid also keeps a hidden plain-text copy for measuring, so
        // look for the markup itself.)
        const QString marked = QStringLiteral("<b>final</b></font>_report.txt");
        QTRY_VERIFY(App::findTextContaining(view, marked));
        QQuickItem *label = App::findTextContaining(view, marked);
        QCOMPARE(label->property("textFormat").toInt(), 4 /* Text.StyledText */);

        QQuickItem *statusBar = app.item(QStringLiteral("statusBar"));
        QVERIFY(statusBar);
        QTRY_COMPARE(statusBar->property("itemCount").toInt(), app.searchProxy->rowCount());

        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            QTest::qWait(800);   // let the add transition finish
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_TEST_GRAB"));
        }

        // #72: emptying the field keeps search open but shows the folder
        // again (4 entries), not the cleared results.
        QVERIFY(QMetaObject::invokeMethod(app.window, "handleSearchQuery",
                                          Q_ARG(QVariant, QString())));
        QVERIFY(app.window->property("searchMode").toBool());
        QTRY_COMPARE(grid->property("count").toInt(), 4);
        QTRY_COMPARE(statusBar->property("itemCount").toInt(), 4);
    }

    // Clicking the current folder or the empty space in the breadcrumb edits
    // the path (#62); the other segments still navigate.
    void testBreadcrumbCurrentFolderClickEditsPath()
    {
        App app;
        QVERIFY(app.load());

        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + "/crumbparent/crumbcurrent"));
        app.tabModel->activeTab()->navigateTo(dir.path() + "/crumbparent/crumbcurrent");
        QTRY_VERIFY(App::findText(app.window->contentItem(), QStringLiteral("crumbcurrent")));
        QTest::qWait(100);   // navigating queues a pane focus; let it land first

        QQuickItem *current = App::findText(app.window->contentItem(), QStringLiteral("crumbcurrent"));
        QQuickItem *breadcrumb = current;
        while (breadcrumb && !breadcrumb->property("editMode").isValid())
            breadcrumb = breadcrumb->parentItem();
        QVERIFY(breadcrumb);
        QVERIFY(!breadcrumb->property("editMode").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(current));
        QTRY_VERIFY(breadcrumb->property("editMode").toBool());
        QTest::qWait(600);   // the editor closes itself when it loses focus
        QVERIFY(breadcrumb->property("editMode").toBool());
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(!breadcrumb->property("editMode").toBool());

        QQuickItem *parent = App::findText(breadcrumb, QStringLiteral("crumbparent"));
        QVERIFY(parent);
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(parent));
        QTRY_COMPARE(app.tabModel->activeTab()->currentPath(), dir.path() + "/crumbparent");
        QVERIFY(!breadcrumb->property("editMode").toBool());

        // At / the bar has no segments: all of it is empty space.
        app.tabModel->activeTab()->navigateTo(QStringLiteral("/"));
        QTRY_VERIFY(!App::findText(breadcrumb, QStringLiteral("crumbparent")));
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(breadcrumb));
        QTRY_VERIFY(breadcrumb->property("editMode").toBool());
        QTest::qWait(600);
        QVERIFY(breadcrumb->property("editMode").toBool());
    }

    // Hovering a toolbar button names it and its shortcut (#63), and the
    // chip has a real size: Quill's Tooltip once rendered 0x0 with no
    // background.
    void testToolbarTooltipShowsShortcut()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop")));
        App app;
        QVERIFY(app.load());
        QQuickItem *grid = app.item(QStringLiteral("viewModeButton_grid"));
        QVERIFY(grid);
        QTest::mouseMove(app.window, app.center(grid));
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();
        const QString keys = config->property("shortcutMap").toMap().value("grid_view").toString();
        QVERIFY(!keys.isEmpty());
        const QString text = QStringLiteral("Grid view  (") + keys + ")";
        QTRY_VERIFY_WITH_TIMEOUT(App::findText(app.window->contentItem(), text), 3000);
        QQuickItem *label = App::findText(app.window->contentItem(), text);
        QTRY_VERIFY(label->isVisible() && label->parentItem()->width() > label->implicitWidth());
        QVERIFY(label->parentItem()->height() > label->implicitHeight());

        // Shortcuts are user settings: a remap shows without a restart.
        const QVariantMap remapped{{QStringLiteral("grid_view"), QStringLiteral("Ctrl+9")}};
        QVERIFY(QMetaObject::invokeMethod(config, "saveShortcuts", Q_ARG(QVariantMap, remapped)));
        QTRY_VERIFY(App::findText(app.window->contentItem(), "Grid view  (Ctrl+9)"));

        // Leaving fades the tooltip out and then unloads it.
        QTest::mouseMove(app.window, QPoint(app.window->width() / 2, app.window->height() / 2));
        QTRY_VERIFY(!App::findText(app.window->contentItem(), "Grid view  (Ctrl+9)"));
    }

    // Every dropdown in Settings is fed by a list built in
    // syncFromCurrentState(). Miss one and the control renders empty with no
    // error anywhere, which is exactly how the light/dark theme pickers
    // shipped blank.
    // #60: Settings has an About page with the running version.
    void testSettingsAboutShowsVersion()
    {
        QCoreApplication::setApplicationVersion(QStringLiteral("v9.9.9-test"));
        App app;
        QVERIFY(app.load());
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 4)));
        auto *panelWindow = qobject_cast<QQuickWindow *>(panel);
        QVERIFY(panelWindow);
        QTRY_VERIFY(App::findText(panelWindow->contentItem(), QStringLiteral("v9.9.9-test")));
    }

    void testSettingsDropdownsAreAllPopulated()
    {
        App app;
        QVERIFY(app.load());
        // The panel is only created the first time it opens.
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);

        for (const char *list : {"themeOptions", "lightThemeOptions", "darkThemeOptions",
                                 "iconThemeOptions", "fontOptions"}) {
            const QVariantList values = panel->property(list).toList();
            QVERIFY2(!values.isEmpty(), list);
        }

        // And the value each picker starts on has to exist in its own list,
        // or the control opens on a blank row.
        for (const auto &[draft, list] : {std::pair{"draftTheme", "themeOptions"},
                                          std::pair{"draftLightTheme", "lightThemeOptions"},
                                          std::pair{"draftDarkTheme", "darkThemeOptions"}}) {
            const QString value = panel->property(draft).toString();
            QVERIFY2(!value.isEmpty(), draft);
            QVERIFY2(panel->property(list).toList().contains(value), draft);
        }
    }

    // minimumHeight used to be bound to the live window height, so once the
    // compositor grew the settings window the minimum grew with it and the
    // window could never be shrunk back down. (The horizontal direction always
    // worked because minimumWidth is a fixed dialogWidth.) Growing the window
    // must leave the minimum untouched.
    void testSettingsWindowCanShrinkBackAfterGrowing()
    {
        App app;
        QVERIFY(app.load());
        // The panel is only created the first time it opens.
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);

        const int minHeight = panel->property("minimumHeight").toInt();
        QVERIFY(minHeight > 0);
        panel->setProperty("height", minHeight + 200);
        QTRY_COMPARE(panel->property("minimumHeight").toInt(), minHeight);
    }

    // The Dark Mode switch changes the theme without going through the Theme
    // dropdown, and Quill's Dropdown breaks its own currentIndex binding the
    // first time a row is picked. Together that left the field naming one
    // theme while the app rendered another.
    void testThemeFieldFollowsTheDarkModeSwitch()
    {
        App app;
        QVERIFY(app.load());
        // The panel is only created the first time it opens.
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);

        QObject *dropdown = app.window->findChild<QObject *>(QStringLiteral("themeDropdown"));
        QVERIFY(dropdown);
        const QVariantList options = panel->property("themeOptions").toList();
        QVERIFY(options.size() > 1);

        // Pick a theme that is genuinely not the current one, or setDraftTheme
        // is a no-op and the test proves nothing.
        const QString current = panel->property("draftTheme").toString();
        int otherIndex = -1;
        for (int i = 0; i < options.size(); ++i) {
            if (options.at(i).toString() != current) { otherIndex = i; break; }
        }
        QVERIFY(otherIndex >= 0);
        const QString other = options.at(otherIndex).toString();

        // Pick a row the way a user would. The Dropdown reports the choice and
        // the panel moves currentIndex through its binding; the Dropdown must
        // not write to it, or that binding is gone and every later change from
        // elsewhere stops showing up.
        const int pickIndex = otherIndex == 0 ? 1 : 0;
        QVERIFY(QMetaObject::invokeMethod(dropdown, "_commit", Q_ARG(QVariant, pickIndex)));
        QCOMPARE(dropdown->property("currentIndex").toInt(), pickIndex);

        QVERIFY(QMetaObject::invokeMethod(panel, "setDraftTheme", Q_ARG(QVariant, other)));

        QCOMPARE(panel->property("draftTheme").toString(), other);
        QCOMPARE(dropdown->property("currentIndex").toInt(), otherIndex);
    }

    // The list used to be its own native window placed by screen
    // coordinates, which Wayland does not expose: on GNOME it opened far
    // below the field. It now lives in the settings window, right under the
    // field, and opening a second dropdown closes the first.
    void testSettingsDropdownOpensAtItsFieldAndClosesTheOther()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        auto *panel = qobject_cast<QQuickWindow *>(
            app.window->findChild<QObject *>(QStringLiteral("settingsPanel")));
        QVERIFY(panel);
        QTRY_VERIFY(panel->isExposed());

        QList<QQuickItem *> dropdowns;
        for (QQuickItem *item : app.window->findChildren<QQuickItem *>()) {
            if (item->metaObject()->indexOfProperty("dropdownOpen") >= 0
                && item->window() == panel && item->isVisible() && item->width() > 0)
                dropdowns.append(item);
        }
        // Top to bottom.
        std::sort(dropdowns.begin(), dropdowns.end(), [](QQuickItem *a, QQuickItem *b) {
            return a->mapToScene(QPointF()).y() < b->mapToScene(QPointF()).y();
        });
        QVERIFY2(dropdowns.size() >= 2, "the first settings page has two dropdowns");
        // Open the lower one first: its list hangs below it, so the upper
        // field stays clickable.
        QQuickItem *first = dropdowns.at(1);
        QQuickItem *second = dropdowns.at(0);
        auto popupOf = [](QQuickItem *d) { return d->findChild<QObject *>(QStringLiteral("dropdownPopup")); };
        QObject *firstPopup = popupOf(first);
        QObject *secondPopup = popupOf(second);
        QVERIFY(firstPopup && secondPopup);

        // The field is the right-hand part of the row; click near its end.
        auto fieldPoint = [](QQuickItem *d) {
            return d->mapToScene(QPointF(d->width() - 20, 17)).toPoint();
        };

        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            QTest::qWait(300);
            panel->grabWindow().save(qEnvironmentVariable("HYPRFM_TEST_GRAB") + QStringLiteral("-dropdown-closed.png"));
        }

        QTest::mouseClick(panel, Qt::LeftButton, {}, fieldPoint(first));
        QTRY_VERIFY(firstPopup->property("opened").toBool());
        // Right under the field: field bottom (34) plus the 6 px gap, or above it.
        const qreal y = firstPopup->property("y").toReal();
        const qreal h = firstPopup->property("height").toReal();
        QVERIFY2(qFuzzyCompare(y, 40.0) || qFuzzyCompare(y, -h - 6),
                 qPrintable(QStringLiteral("popup y %1").arg(y)));

        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            QTest::qWait(300);
            panel->grabWindow().save(qEnvironmentVariable("HYPRFM_TEST_GRAB") + QStringLiteral("-dropdown.png"));
        }

        // One click on the other field closes this one and opens that one.
        QTest::mouseClick(panel, Qt::LeftButton, {}, fieldPoint(second));
        QTRY_VERIFY(secondPopup->property("opened").toBool());
        QTRY_VERIFY(!firstPopup->property("visible").toBool());
        QVERIFY(!first->property("dropdownOpen").toBool());

        // Clicking its own field again closes it.
        QTest::mouseClick(panel, Qt::LeftButton, {}, fieldPoint(second));
        QTRY_VERIFY(!secondPopup->property("visible").toBool());
        QVERIFY(!second->property("dropdownOpen").toBool());

        // And it opens again after closing itself.
        QTest::mouseClick(panel, Qt::LeftButton, {}, fieldPoint(second));
        QTRY_VERIFY(secondPopup->property("opened").toBool());
        QTest::keyClick(panel, Qt::Key_Escape);
        QTRY_VERIFY(!secondPopup->property("visible").toBool());
        QVERIFY(!second->property("dropdownOpen").toBool());

        // Clicking a row picks it. A press on the list once counted as
        // outside the field and closed the list before the click landed.
        QTest::mouseClick(panel, Qt::LeftButton, {}, fieldPoint(second));
        QTRY_VERIFY(secondPopup->property("opened").toBool());
        QSignalSpy picked(second, SIGNAL(selected(int,QString)));
        const int before = second->property("currentIndex").toInt();
        const int target = before == 0 ? 1 : 0;
        QQuickItem *list = secondPopup->property("contentItem").value<QQuickItem *>();
        QVERIFY(list);
        QTest::qWait(250);   // let the open animation settle
        // Rows are 32 px under a 5 px margin, scrolled so the current one is
        // centred; find the target row's delegate instead of guessing.
        QQuickItem *row = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->property("modelData").isValid() && item->property("index").toInt() == target
                && item->isVisible())
                row = item;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(list);
        QVERIFY(row);
        QTest::mouseClick(panel, Qt::LeftButton, {},
                          row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QTRY_COMPARE(picked.count(), 1);
        QCOMPARE(picked.at(0).at(0).toInt(), target);
        QTRY_VERIFY(!secondPopup->property("visible").toBool());

        // The top dropdown is Theme: the pick has to reach the config.
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();
        QTRY_COMPARE(config->property("theme").toString(), picked.at(0).at(1).toString());
    }

    // "Default sort" sits low on the Layout page. Its list has to open inside
    // the settings window, flipping above the field when there is no room.
    void testLowSettingsDropdownStaysInsideTheWindow()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        auto *panel = qobject_cast<QQuickWindow *>(
            app.window->findChild<QObject *>(QStringLiteral("settingsPanel")));
        QVERIFY(panel);
        QTRY_VERIFY(panel->isExposed());
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 1)));

        QQuickItem *sort = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem *item : app.window->findChildren<QQuickItem *>()) {
                if (item->window() == panel && item->property("label").toString() == QLatin1String("Default sort"))
                    sort = item;
            }
            return sort != nullptr;
        }());
        // Bring it to the bottom edge of the page, the worst case.
        QQuickItem *flick = sort->parentItem();
        while (flick && flick->metaObject()->indexOfProperty("contentY") < 0)
            flick = flick->parentItem();
        QVERIFY(flick);
        const qreal sortY = sort->mapToItem(flick->property("contentItem").value<QQuickItem *>(), QPointF()).y();
        flick->setProperty("contentY", qMax<qreal>(0, sortY + sort->height() - flick->height()));
        QTRY_VERIFY(sort->mapToScene(QPointF(0, sort->height())).y() <= panel->height());

        QTest::mouseClick(panel, Qt::LeftButton, {},
                          sort->mapToScene(QPointF(sort->width() - 20, 17)).toPoint());
        auto *popup = sort->findChild<QObject *>(QStringLiteral("dropdownPopup"));
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QQuickItem *content = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        const QRectF inScene = content->mapRectToScene(QRectF(0, 0, content->width(), content->height()));
        QVERIFY2(inScene.top() >= 0 && inScene.bottom() <= panel->height(),
                 qPrintable(QStringLiteral("popup %1..%2 in a %3 tall window")
                                .arg(inScene.top()).arg(inScene.bottom()).arg(panel->height())));
        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            QTest::qWait(300);
            panel->grabWindow().save(qEnvironmentVariable("HYPRFM_TEST_GRAB") + QStringLiteral("-sort.png"));
        }
    }

    // Picking the dark half of the pair while in dark mode (or the light half
    // in light mode) is picking the theme on screen, so it applies at once.
    void testPickingTheActiveHalfOfThePairAppliesIt()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();

        QQuickItem *dark = nullptr;
        QQuickItem *light = nullptr;
        for (QQuickItem *item : app.window->findChildren<QQuickItem *>()) {
            if (item->property("label").toString() == QLatin1String("Dark theme")) dark = item;
            if (item->property("label").toString() == QLatin1String("Light theme")) light = item;
        }
        QVERIFY(dark && light);

        const bool darkMode = panel->property("draftDarkMode").toBool();
        QQuickItem *active = darkMode ? dark : light;
        QQuickItem *other = darkMode ? light : dark;
        const QString before = config->property("theme").toString();

        // The other half only changes the pair.
        QVERIFY(QMetaObject::invokeMethod(other, "selected", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("nord"))));
        QCOMPARE(config->property("theme").toString(), before);

        QVERIFY(QMetaObject::invokeMethod(active, "selected", Q_ARG(int, 0),
                                          Q_ARG(QString, QStringLiteral("dracula"))));
        QTRY_COMPARE(config->property("theme").toString(), QStringLiteral("dracula"));
    }

    // Issue #13: menus used a fixed width, so long rows overflowed the popup.
    void testContextMenuWidensForLongLabels()
    {
        App app;
        QVERIFY(app.load());
        // Built on first use; switch its Loader on as the app would.
        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QQuickItem *menu = app.item("contextMenu");
        QVERIFY(menu);
        const int base = menu->property("menuWidth").toInt();

        const QString label = QStringLiteral("Remove this rather long entry from Bookmarks right now");
        menu->setProperty("customItems", QVariantList{QVariantMap{
            {"text", label}, {"action", "noop"}, {"shortcut", "Ctrl+Shift+Alt+B"}}});
        QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QVariant, 20), Q_ARG(QVariant, 20)));
        QTRY_VERIFY(menu->property("effectiveMenuWidth").toInt() > base);
        QQuickItem *text = App::findText(menu, label);
        QVERIFY(text);
        QTRY_VERIFY2(text->property("contentWidth").toReal() <= text->width() + 0.5,
                     qPrintable(QStringLiteral("label %1px wide in a %2px slot")
                                    .arg(text->property("contentWidth").toReal()).arg(text->width())));

        // Short rows keep the compact default width.
        menu->setProperty("customItems", QVariantList{QVariantMap{{"text", "Open"}, {"action", "noop"}}});
        QTRY_COMPARE(menu->property("effectiveMenuWidth").toInt(), base);
    }

    // The menu never takes focus, so nothing handled Escape and it stayed
    // open (caught by the Windows showcase run).
    void testEscapeClosesContextMenu()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QQuickItem *menu = app.item("contextMenu");
        QVERIFY(menu);
        menu->setProperty("customItems", QVariantList{QVariantMap{{"text", "Open"}, {"action", "noop"}}});
        QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QVariant, 20), Q_ARG(QVariant, 20)));
        QTRY_VERIFY(menu->isVisible());
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(!menu->isVisible());
    }

    // The Properties dialog is a plain Item overlay, not a Popup, so nothing
    // gave it keyboard focus and Escape fell through to the file view behind
    // it. Opening it and pressing Escape must close it again (issue #28).
    void testEscapeClosesThePropertiesDialog()
    {
        App app;
        QVERIFY(app.load());
        // Built the first time it opens.
        QVERIFY(QMetaObject::invokeMethod(app.window->contentItem()->parent(), "showPropertiesFor",
                                          Q_ARG(QVariant, app.home.path())));
        QQuickItem *dialog = app.item("propertiesDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());

        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY2(!dialog->isVisible(), "Escape left the properties dialog open");
    }

    // The label column was a fixed 80px, so with a wide or large UI font
    // "Disk usage" ran into its value (issue #39). Every label must end
    // before its value starts.
    void testPropertiesLabelsDoNotOverlapValues()
    {
        const QFont oldFont = QGuiApplication::font();
        QFont big(QStringLiteral("monospace"), 16);
        QGuiApplication::setFont(big);
        auto restore = qScopeGuard([&] { QGuiApplication::setFont(oldFont); });

        App app;
        QVERIFY(app.load());
        // Built the first time it opens.
        QVERIFY(QMetaObject::invokeMethod(app.window->contentItem()->parent(), "showPropertiesFor",
                                          Q_ARG(QVariant, app.home.path())));
        QQuickItem *dialog = app.item("propertiesDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());

        for (const QString &name : {QStringLiteral("Disk usage"), QStringLiteral("Content"),
                                     QStringLiteral("Location"), QStringLiteral("Modified")}) {
            QQuickItem *label = App::findText(dialog, name);
            QVERIFY2(label, qPrintable(name));
            QQuickItem *value = nullptr;
            for (QQuickItem *sibling : label->parentItem()->childItems())
                if (sibling != label && sibling->property("text").isValid())
                    value = sibling;
            QVERIFY2(value, qPrintable(name));
            const qreal labelEnd = label->x() + label->property("contentWidth").toReal();
            QTRY_VERIFY2(labelEnd <= value->x(),
                         qPrintable(QStringLiteral("%1 label ends at %2, value starts at %3")
                                        .arg(name).arg(labelEnd).arg(value->x())));
        }

        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            QTest::qWait(500);
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_TEST_GRAB"));
        }
    }

    // Re-opening the menu while it is already visible and laid out must
    // render it again. popup() used to wait on menuColumn.onHeightChanged,
    // which cannot fire when the new menu has the same height, leaving the
    // menu open but invisible: right-clicks seemed dead and hovering lit up
    // hidden rows.
    void testRepopupRendersAnOpenMenu()
    {
        App app;
        QVERIFY(app.load());
        // Built on first use; switch its Loader on as the app would.
        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QQuickItem *menu = app.item("contextMenu");
        QVERIFY(menu);
        QQuickItem *container = app.item("contextMenuContainer");
        QVERIFY(container);

        menu->setProperty("customItems", QVariantList{QVariantMap{
            {"text", "Open"}, {"action", "noop"}}});
        QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QVariant, 20), Q_ARG(QVariant, 20)));
        QTRY_VERIFY2(container->property("opacity").toReal() > 0.99,
                     qPrintable(QStringLiteral("first popup opacity %1")
                                    .arg(container->property("opacity").toReal())));
        // Wait for the first open animation to finish; otherwise a re-popup
        // could become visible on its own through the still-running animation
        // and mask the regression. Waiting on the final animated values below
        // is deterministic across environments and theme timings.
        QTRY_COMPARE(container->property("opacity").toReal(), 1.0);
        QTRY_COMPARE(container->property("yOffset").toReal(), 0.0);

        // Second popup, same (same-height) menu, menu already visible.
        QVERIFY(QMetaObject::invokeMethod(menu, "popup", Q_ARG(QVariant, 60), Q_ARG(QVariant, 60)));
        QTRY_VERIFY2(container->property("opacity").toReal() > 0.99,
                     qPrintable(QStringLiteral("re-opened menu opacity %1")
                                    .arg(container->property("opacity").toReal())));
    }

    // A right-click outside an open menu closes it and passes on to the view,
    // which asks for the menu again. The same target must stay closed (a
    // second right-click toggles the menu off); a different target reopens.
    // Only the press is sent before popup(): the release would let the real
    // view under the cursor open its own menu and muddle the check.
    void testRightClickSameTargetClosesMenu()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QQuickItem *menu = app.item("contextMenu");
        QVERIFY(menu);
        QQuickItem *container = app.item("contextMenuContainer");
        QVERIFY(container);

        auto open = [&](const QString &target) {
            menu->setProperty("targetPath", target);
            menu->setProperty("isEmptySpace", false);
            return QMetaObject::invokeMethod(menu, "popup", Q_ARG(QVariant, 20), Q_ARG(QVariant, 20));
        };
        const QPoint outside(app.window->width() - 40, app.window->height() - 40);

        QVERIFY(open("/tmp/a"));
        QTRY_COMPARE(container->property("opacity").toReal(), 1.0);
        QTest::mousePress(app.window, Qt::RightButton, {}, outside);
        QVERIFY(open("/tmp/a"));
        QTRY_VERIFY(!menu->isVisible());
        QTest::mouseRelease(app.window, Qt::RightButton, {}, outside);

        app.item("contextMenuLoader")->setProperty("active", true);
        QVERIFY(open("/tmp/a"));
        QTRY_COMPARE(container->property("opacity").toReal(), 1.0);
        QTest::mousePress(app.window, Qt::RightButton, {}, outside);
        QVERIFY(open("/tmp/b"));
        QTRY_COMPARE(container->property("opacity").toReal(), 1.0);
        QVERIFY(menu->isVisible());
        QCOMPARE(menu->property("targetPath").toString(), QStringLiteral("/tmp/b"));
        QTest::mouseRelease(app.window, Qt::RightButton, {}, outside);
    }

    // The late-relayout guard in ContextMenu's onHeightChanged only runs once
    // _pendingPopup is already cleared, so it stayed unexercised: `&&`
    // short-circuits while a popup is pending. Growing an open menu takes that
    // branch, and a scripting error there aborts the handler, leaving the menu
    // laid out for its old height and hanging off the bottom of the window.
    void testGrowingAnOpenMenuKeepsItOnScreen()
    {
        App app;
        QVERIFY(app.load());
        // Built on first use; switch its Loader on as the app would.
        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QQuickItem *menu = app.item("contextMenu");
        QVERIFY(menu);
        QQuickItem *container = app.item("contextMenuContainer");
        QVERIFY(container);

        menu->setProperty("customItems", QVariantList{QVariantMap{
            {"text", "Open"}, {"action", "noop"}}});
        const int nearBottom = app.window->height() - 80;
        QVERIFY(QMetaObject::invokeMethod(menu, "popup",
                                          Q_ARG(QVariant, 20), Q_ARG(QVariant, nearBottom)));
        QTRY_COMPARE(container->property("opacity").toReal(), 1.0);

        // Same menu, many more rows: the height change must reposition it.
        QVariantList many;
        for (int i = 0; i < 10; ++i)
            many << QVariantMap{{"text", QStringLiteral("Item %1").arg(i)}, {"action", "noop"}};
        const qreal shortHeight = container->height();
        menu->setProperty("customItems", many);
        // Wait for the taller layout first: asserting straight away passes on
        // the short menu, which still fits, and proves nothing.
        QTRY_VERIFY(container->height() > shortHeight + 100);

        QTRY_VERIFY2(container->y() + container->height() <= app.window->height(),
                     qPrintable(QStringLiteral("menu bottom at %1 in a %2px window")
                                    .arg(container->y() + container->height())
                                    .arg(app.window->height())));
    }

    // A refused password used to close the prompt and reopen it, which read as
    // a flicker and threw away what was typed. The dialog now stays up and
    // reports in place, and only a password that actually worked closes it.
    void testWrongArchivePasswordKeepsThePromptOpen()
    {
        App app;
        QVERIFY(app.load());
        // Built on first use; switch its Loader on as the app would.
        QVERIFY(app.item("archivePasswordDialogLoader")->setProperty("active", true));
        QQuickItem *dialog = app.item("archivePasswordDialog");
        QVERIFY(dialog);

        QVERIFY(QMetaObject::invokeMethod(dialog, "openFor",
                                          Q_ARG(QVariant, QStringLiteral("/tmp/x.7z"))));
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(dialog->property("errorText").toString().isEmpty());

        QVERIFY(QMetaObject::invokeMethod(dialog, "failed"));
        // Long enough that a close animation would have finished and hidden it.
        QTest::qWait(400);
        QVERIFY2(dialog->isVisible(), "the prompt closed on a wrong password");
        QCOMPARE(dialog->property("errorText").toString(),
                 QStringLiteral("Wrong password. Try again."));
        QCOMPARE(dialog->property("checking").toBool(), false);

        // Only success dismisses it.
        QVERIFY(QMetaObject::invokeMethod(dialog, "succeeded"));
        QTRY_VERIFY(!dialog->isVisible());
    }

    // The whole password flow end to end: activating an encrypted archive
    // fails, the prompt opens, and the password the user types must extract it
    // AND dismiss the prompt. The dialog sat on "Checking..." forever when the
    // success never made it back.
    void testCorrectPasswordExtractsAndDismissesThePrompt()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty())
            QSKIP("7z not found in PATH");

        App app;
        QVERIFY(app.load());

        const QString dir = app.home.path();
        QDir().mkpath(dir + "/payload");
        QFile f(dir + "/payload/inner.txt");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("secret");
        f.close();
        QProcess zip;
        zip.setWorkingDirectory(dir);
        zip.start("7z", {"a", "-ptest", "-mhe=on", dir + "/locked.7z", "payload"});
        QVERIFY(zip.waitForFinished(20000));
        QCOMPARE(zip.exitCode(), 0);

        // Built on first use; switch its Loader on as the app would.
        QVERIFY(app.item("archivePasswordDialogLoader")->setProperty("active", true));
        QQuickItem *dialog = app.item("archivePasswordDialog");
        QVERIFY(dialog);

        QVERIFY(QMetaObject::invokeMethod(app.window->contentItem()->parent(),
                                          "handlePaneFileActivated",
                                          Q_ARG(QVariant, QStringLiteral("primary")),
                                          Q_ARG(QVariant, dir + "/locked.7z"),
                                          Q_ARG(QVariant, false)));

        QTRY_VERIFY_WITH_TIMEOUT(dialog->isVisible(), 15000);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, dir + "/locked.7z")));
        QTRY_VERIFY(dialog->isVisible());
        dialog->setProperty("filePath", dir + "/locked.7z");

        // Type the right password and submit, as the user would.
        QQuickItem *field = app.item("archivePasswordField");
        QVERIFY2(field, "password field not found");
        field->setProperty("text", QStringLiteral("test"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "submit"));

        QTRY_VERIFY_WITH_TIMEOUT(!dialog->isVisible(), 20000);
        QCOMPARE(dialog->property("checking").toBool(), false);
    }

    // Closing an overlay has to hand keyboard focus back to the file view, or
    // arrows and Backspace do nothing until you click something (issue #37).
    // Only the properties dialog did; rename, new folder and new file did not.
    void testClosingAnOverlayRestoresPaneFocus_data()
    {
        QTest::addColumn<QString>("opener");
        QTest::addColumn<QString>("dialog");
        QTest::newRow("rename") << "openRenameDialogForPath" << "renameDialog";
        QTest::newRow("new folder") << "showNewFolderDialog" << "newFolderDialog";
        QTest::newRow("new file") << "showNewFileDialog" << "newFileDialog";
    }

    void testClosingAnOverlayRestoresPaneFocus()
    {
        QFETCH(QString, opener);
        QFETCH(QString, dialog);

        App app;
        QVERIFY(app.load());
        QQuickItem *view = app.item(QStringLiteral("primaryFileView"));
        QVERIFY(view);
        QObject *root = app.window->contentItem()->parent();
        QVERIFY(root);

        QVERIFY(QMetaObject::invokeMethod(root, opener.toUtf8().constData(),
                                          Q_ARG(QVariant, app.home.path() + "/x.txt")));
        QTRY_VERIFY(app.window->activeFocusItem() != nullptr);

        // Dismiss it the way Escape does.
        QQuickItem *d = App::findItem(app.window->contentItem(), dialog);
        QVERIFY2(d, qPrintable(dialog));
        QVERIFY(QMetaObject::invokeMethod(d, "reject"));
        QTRY_VERIFY(!d->isVisible());

        // The pane takes focus back on the next event-loop turn
        // (scheduleActivePaneFocus), so wait for it rather than look at once:
        // under a loaded machine the check could run before that turn.
        auto focusInsideView = [&] {
            for (QQuickItem *i = app.window->activeFocusItem(); i; i = i->parentItem())
                if (i == view)
                    return true;
            return false;
        };
        auto focusName = [&] {
            QQuickItem *focused = app.window->activeFocusItem();
            if (!focused)
                return QStringLiteral("nothing");
            return focused->objectName().isEmpty()
                ? QString::fromLatin1(focused->metaObject()->className()) : focused->objectName();
        };
        QTRY_VERIFY2_WITH_TIMEOUT(focusInsideView(), qPrintable(QStringLiteral("focus went to %1, not the file view")
                                                                    .arg(focusName())), 2000);
    }

    void testSidebarNavigationKeepsSplit_data()
    {
        QTest::addColumn<QString>("orientation");
        QTest::addColumn<QString>("pane");
        QTest::addColumn<QString>("viewMode");
        for (const QString &orientation : {QString("vertical"), QString("horizontal")})
            for (const QString &pane : {QString("primary"), QString("secondary")})
                for (const QString &view : {QString("grid"), QString("detailed"), QString("miller"), QString("compact")})
                    QTest::newRow(qPrintable(orientation + "-" + pane + "-" + view)) << orientation << pane << view;
    }

    void testSidebarNavigationKeepsSplit()
    {
        QFETCH(QString, orientation);
        QFETCH(QString, pane);
        QFETCH(QString, viewMode);
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        QVERIFY(tab);
        const QString primary = app.home.path() + "/primary";
        const QString secondary = app.home.path() + "/secondary";
        const QString bookmark = app.home.path() + "/bookmark";
        const QString mounted = app.home.path() + "/mounted";
        for (const auto &path : {primary, secondary, bookmark, mounted})
            QVERIFY(QDir().mkpath(path));
        tab->navigateTo(primary);
        tab->resetSecondaryTo(secondary);
        tab->setViewMode(viewMode);
        tab->setSplitOrientation(orientation);
        tab->setSplitViewEnabled(true);
        app.bookmarks->setBookmarks({QDir::homePath(), bookmark});
        QObject *root = app.window;
        const QString other = pane == "primary" ? "secondary" : "primary";
        const QString starting = pane == "primary" ? primary : secondary;
        const auto currentPath = [&]() { return pane == "primary" ? tab->currentPath() : tab->secondaryCurrentPath(); };
        auto *sidebar = app.item("sidebarPanel");
        QVERIFY(sidebar);
        for (const QString &action : {QString("Home"), QString("Computer"), QString("Recents"), QString("Trash"), QString("Network"),
                                      QString("Pictures"), QString("Downloads"), QString("bookmark_0"),
                                      QString("bookmark_1"), QString("mounted")}) {
            QVERIFY(QMetaObject::invokeMethod(root, "setActivePane", Q_ARG(QVariant, pane)));
            QVERIFY(QMetaObject::invokeMethod(root, "navigateActivePaneTo", Q_ARG(QVariant, starting)));
            // Sidebar navigation must leave the other pane's search intact.
            QVERIFY(QMetaObject::invokeMethod(root, "setPaneSearchMode", Q_ARG(QVariant, other), Q_ARG(QVariant, true)));
            QVERIFY(QMetaObject::invokeMethod(root, "openSearch"));
            QString expected = starting;
            if (action == "mounted") {
                expected = mounted;
                auto *devices = app.engine.rootContext()->contextProperty("devices").value<QObject *>();
                QVERIFY(devices);
                // Exercise the sidebar's mount-completion route without mounting a real device.
                QVERIFY(QMetaObject::invokeMethod(devices, "deviceMounted", Q_ARG(QString, mounted)));
            } else {
                auto *item = app.item(action.startsWith("bookmark_") ? action : "quickAccess_" + action);
                QVERIFY2(item, qPrintable(action));
                QTRY_VERIFY(item->isVisible());
                if (action.startsWith("bookmark_"))
                    expected = action == "bookmark_0" ? QDir::homePath() : bookmark;
                else if (action != "Recents")
                    expected = item->property("resolvedPath").toString();
                QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(item));
            }
            QTRY_VERIFY2(tab->splitViewEnabled(), qPrintable(action));
            QTRY_COMPARE(currentPath(), expected);
            QCOMPARE(tab->currentPath(), pane == "primary" ? expected : primary);
            QCOMPARE(tab->secondaryCurrentPath(), pane == "secondary" ? expected : secondary);
            QCOMPARE(root->property("activePane").toString(), pane);
            QCOMPARE(tab->splitOrientation(), orientation);
            QCOMPARE(tab->viewMode(), viewMode);
            const char *searchProperty = pane == "primary" ? "primaryPaneSearchMode" : "secondaryPaneSearchMode";
            const char *otherSearchProperty = pane == "primary" ? "secondaryPaneSearchMode" : "primaryPaneSearchMode";
            const char *recentsProperty = pane == "primary" ? "primaryPaneIsRecents" : "secondaryPaneIsRecents";
            QTRY_VERIFY2(!root->property(searchProperty).toBool(), qPrintable(action));
            QVERIFY2(root->property(otherSearchProperty).toBool(), qPrintable(action));
            QCOMPARE(root->property(recentsProperty).toBool(), action == "Recents");
        }
    }

    void testComputerOverview_data()
    {
        QTest::addColumn<QString>("viewMode");
        for (const QString &view : {QString("grid"), QString("compact"), QString("detailed"), QString("miller")})
            QTest::newRow(qPrintable(view)) << view;
    }

    void testComputerOverview()
    {
        QFETCH(QString, viewMode);
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(app.home.path());
        tab->resetSecondaryTo(app.home.path());
        tab->setViewMode(viewMode);
        tab->setSplitViewEnabled(true);
        QVERIFY(QMetaObject::invokeMethod(app.window, "setActivePane", Q_ARG(QVariant, QString("secondary"))));
        auto *shortcut = app.item("quickAccess_Computer");
        QVERIFY(shortcut && shortcut->isVisible());
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(shortcut));
        QTRY_COMPARE(tab->secondaryCurrentPath(), QString("computer:///"));
        QTRY_VERIFY(app.item("computerView"));
        auto *view = app.item("computerView");
        QTRY_VERIFY(view->isVisible());
        auto *model = app.engine.rootContext()->contextProperty("computerModel").value<QObject *>();
        QVERIFY(model);
        QTRY_VERIFY(model->property("count").toInt() > 0);
        QTRY_VERIFY(app.item("computerDrive_0"));
        QVERIFY(QMetaObject::invokeMethod(view, "choose", Q_ARG(QVariant, 0), Q_ARG(QVariant, false), Q_ARG(QVariant, false)));
        // Background discovery and capacity updates preserve drive identity,
        // including replacing the fallback root with the discovered root.
        QVERIFY(QMetaObject::invokeMethod(model, "refresh"));
        QCOMPARE(view->property("cursorIndex").toInt(), 0);
        QCOMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        for (const auto *action : {"currentOrSelectedDirectoryPath", "selectedOrCurrentPropertiesPath", "selectedOrCurrentTerminalPath"}) {
            QVariant target;
            QVERIFY(QMetaObject::invokeMethod(app.window, action, Q_RETURN_ARG(QVariant, target)));
            QCOMPARE(target.toString(), QString("/"));
        }
        QVariant paths;
        QVERIFY(QMetaObject::invokeMethod(app.window, "getSelectedPaths", Q_RETURN_ARG(QVariant, paths),
                                          Q_ARG(QVariant, QString("secondary"))));
        QVERIFY(paths.toStringList().isEmpty());
        // Drive selection cannot become a delete/rename target or paste destination.
        QVERIFY(QMetaObject::invokeMethod(app.window, "showNewFolderDialog", Q_ARG(QVariant, QString("computer:///"))));
        QVERIFY(!app.window->property("newFolderDialog").value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(app.window, "goActivePaneUp"));
        QCOMPARE(tab->secondaryCurrentPath(), QString("computer:///"));
        if (qEnvironmentVariableIsSet("HYPRFM_COMPUTER_GRAB")) {
            QTRY_COMPARE(app.window->property("splitTransitionProgress").toReal(), 1.0);
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_COMPUTER_GRAB") + "-" + viewMode + ".png");
        }
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_COMPARE(tab->secondaryCurrentPath(), QString("/"));
        QCOMPARE(tab->currentPath(), app.home.path());
        QVERIFY(tab->splitViewEnabled());
        QVERIFY(QMetaObject::invokeMethod(app.window, "goActivePaneBack"));
        QTRY_COMPARE(tab->secondaryCurrentPath(), QString("computer:///"));
        QTRY_VERIFY(view->isVisible());
        QVERIFY(QMetaObject::invokeMethod(app.window, "goActivePaneBack"));
        QTRY_COMPARE(tab->secondaryCurrentPath(), app.home.path());
        QTRY_VERIFY(!view->isVisible());
        // A wide monitor keeps List/Column drive cards readable rather than
        // stretching a usage meter across the whole window.
        tab->setSplitViewEnabled(false);
        tab->navigateTo("computer:///");
        app.window->resize(1800, 700);
        auto *primaryContainer = app.item("primaryFileView");
        QVERIFY(primaryContainer);
        QTRY_VERIFY(primaryContainer->property("computerViewItem").value<QQuickItem *>());
        auto *primaryView = primaryContainer->property("computerViewItem").value<QQuickItem *>();
        QTRY_VERIFY(App::findItem(primaryView, "computerDrive_0"));
        if (viewMode == "detailed" || viewMode == "miller")
            QTRY_VERIFY(App::findItem(primaryView, "computerDrive_0")->width() < 900);
        if (qEnvironmentVariableIsSet("HYPRFM_COMPUTER_GRAB")) {
            auto *theme = qobject_cast<ThemeLoader *>(app.engine.rootContext()->contextProperty("theme").value<QObject *>());
            theme->loadTheme("strata-nord-light", {QStringLiteral(TEST_SOURCE_DIR "/themes")});
            QTest::qWait(350);
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_COMPUTER_GRAB") + "-wide-" + viewMode + ".png");
        }
    }

    // Both panes get about half the space in either orientation. The
    // horizontal-split change once let the secondary pane fill the width
    // too, which squeezed the primary pane to a sliver in a vertical split.
    void testSplitPanesShareTheSpace()
    {
        App app;
        QVERIFY(app.load());
        app.window->resize(1100, 700);
        QObject *root = app.window->contentItem()->parent();
        QVERIFY(QMetaObject::invokeMethod(root, "toggleSplitView"));
        auto *primary = app.window->findChild<QQuickItem *>("primaryPaneFrame");
        auto *secondary = app.window->findChild<QQuickItem *>("secondaryPaneLoader");
        QVERIFY(primary && secondary);

        auto halves = [](qreal a, qreal b) { return qAbs(a - b) < 0.1 * (a + b); };
        // Mid-animation the widths cross, so only judge the settled layout.
        QTRY_COMPARE(root->property("splitTransitionProgress").toReal(), 1.0);
        app.window->grabWindow();   // offscreen layouts only polish on a render
        QVERIFY(halves(primary->width(), secondary->width()));
        QVERIFY(primary->x() < secondary->x());

        QVERIFY(QMetaObject::invokeMethod(root, "toggleSplitOrientation"));
        // Equal widths only once the fold animation has finished.
        QTRY_COMPARE(primary->width(), secondary->width());
        QVERIFY(primary->y() < secondary->y());
        QVERIFY(halves(primary->height(), secondary->height()));
    }

    // While a split opens or closes the pane width animates. The grid used
    // to recompute its columns from that width every frame, so icons grew,
    // snapped smaller and jumped rows at every column step. It now lays out
    // at the width it will settle at for the whole animation.
    void testGridKeepsItsColumnsWhileTheSplitCloses()
    {
        App app;
        QDir().mkpath(app.home.path() + "/.config/hyprfm");
        QFile cfg(app.home.path() + "/.config/hyprfm/config.toml");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[appearance]\nanim_duration_slow = 1500\n");
        cfg.close();
        const QString dir = app.home.path() + "/many";
        for (int i = 0; i < 20; ++i)
            QVERIFY(QDir().mkpath(dir + QString("/folder%1").arg(i)));
        QVERIFY(app.load());
        app.window->resize(1100, 700);
        QObject *root = app.window->contentItem()->parent();
        TabModel *tab = app.tabModel->activeTab();
        tab->navigateTo(dir);
        QVERIFY(QMetaObject::invokeMethod(root, "toggleSplitView"));
        QTRY_COMPARE_WITH_TIMEOUT(root->property("splitTransitionProgress").toReal(), 1.0, 5000);

        QObject *container = app.window->findChild<QObject *>("primaryFileView");
        QVERIFY(container);
        auto *grid = container->property("gridViewItem").value<QQuickItem *>();
        QVERIFY(grid);

        QVERIFY(QMetaObject::invokeMethod(root, "toggleSplitView"));
        QTest::qWait(50);
        const int columns = grid->property("effectiveColumnCount").toInt();
        const int cell = grid->property("cellWidth").toInt();
        int samples = 0;
        while (root->property("splitTransitionProgress").toReal() > 0.0) {
            QCOMPARE(grid->property("effectiveColumnCount").toInt(), columns);
            QCOMPARE(grid->property("cellWidth").toInt(), cell);
            ++samples;
            QTest::qWait(30);
        }
        QVERIFY2(samples > 10, qPrintable(QString::number(samples)));
        QCOMPARE(grid->property("effectiveColumnCount").toInt(), columns);
    }

    // A tab with no orientation of its own follows split_orientation, so the
    // first toggle has to flip that, not blindly pick "horizontal".
    void testToggleSplitOrientationFromConfigDefault()
    {
        App app;
        QDir().mkpath(app.home.path() + "/.config/hyprfm");
        QFile cfg(app.home.path() + "/.config/hyprfm/config.toml");
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        cfg.write("[general]\nsplit_orientation = \"horizontal\"\n");
        cfg.close();
        QVERIFY(app.load());

        QObject *root = app.window->contentItem()->parent();
        QVERIFY(root);
        QVERIFY(root->property("isSplitHorizontal").toBool());
        QVERIFY(QMetaObject::invokeMethod(root, "toggleSplitOrientation"));
        QVERIFY(!root->property("isSplitHorizontal").toBool());
    }

    void testSavingTwoSelectedFoldersAsSplit()
    {
        App app;
        QVERIFY(app.load());
        const QString first = app.home.path() + "/first";
        const QString second = app.home.path() + "/second";
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));

        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QObject *menu = app.item("contextMenu");
        QVERIFY(menu);
        menu->setProperty("selectedFolderPaths", QStringList{first, second});

        QVariant modelResult;
        QVERIFY(QMetaObject::invokeMethod(menu, "buildModel", Q_RETURN_ARG(QVariant, modelResult)));
        const QVariantList items = modelResult.toList();
        QVERIFY(std::any_of(items.cbegin(), items.cend(), [](const QVariant &entry) {
            return entry.toMap().value("action").toString() == "save_split";
        }));

        QVERIFY(QMetaObject::invokeMethod(menu, "executeAction",
                                          Q_ARG(QVariant, QStringLiteral("save_split")),
                                          Q_ARG(QVariant, QVariant()),
                                          Q_ARG(QVariant, QVariant())));
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();
        QVERIFY(config);
        const QVariantList saved = config->property("savedSplits").toList();
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().toMap().value("primaryPath").toString(), first);
        QCOMPARE(saved.first().toMap().value("secondaryPath").toString(), second);

        // Saving the same pair again does not add a duplicate.
        QVERIFY(QMetaObject::invokeMethod(menu, "executeAction",
                                          Q_ARG(QVariant, QStringLiteral("save_split")),
                                          Q_ARG(QVariant, QVariant()),
                                          Q_ARG(QVariant, QVariant())));
        QCOMPARE(config->property("savedSplits").toList().size(), 1);
    }

    // With a split open, right-clicking empty space offers to save it.
    void testEmptySpaceMenuSavesTheOpenSplit()
    {
        App app;
        const QString first = app.home.path() + "/first";
        const QString second = app.home.path() + "/second";
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));
        QVERIFY(app.load());
        TabModel *tab = app.tabModel->activeTab();
        tab->navigateTo(first);
        tab->setSplitViewEnabled(true);
        tab->resetSecondaryTo(second);

        QVERIFY(app.item("contextMenuLoader")->setProperty("active", true));
        QObject *menu = app.item("contextMenu");
        QVERIFY(menu);
        menu->setProperty("isEmptySpace", true);
        QTRY_VERIFY(menu->property("splitViewEnabled").toBool());

        QVariant modelResult;
        QVERIFY(QMetaObject::invokeMethod(menu, "buildModel", Q_RETURN_ARG(QVariant, modelResult)));
        const QVariantList items = modelResult.toList();
        QVERIFY(std::any_of(items.cbegin(), items.cend(), [](const QVariant &entry) {
            return entry.toMap().value("action").toString() == "save_current_split";
        }));

        QVERIFY(QMetaObject::invokeMethod(menu, "executeAction",
                                          Q_ARG(QVariant, QStringLiteral("save_current_split")),
                                          Q_ARG(QVariant, QVariant()),
                                          Q_ARG(QVariant, QVariant())));
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();
        const QVariantList saved = config->property("savedSplits").toList();
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().toMap().value("primaryPath").toString(), first);
        QCOMPARE(saved.first().toMap().value("secondaryPath").toString(), second);
    }

    void testSavedSplitIsOneBookmark_data()
    {
        QTest::addColumn<QString>("orientation");
        QTest::newRow("top-bottom") << QString("horizontal");
        QTest::newRow("left-right") << QString("vertical");
    }

    void testSavedSplitIsOneBookmark()
    {
        QFETCH(QString, orientation);
        App app;
        QVERIFY(app.load());
        const QString first = app.home.path() + "/first";
        const QString second = app.home.path() + "/second";
        QVERIFY(QDir().mkpath(first));
        QVERIFY(QDir().mkpath(second));

        QVERIFY(QMetaObject::invokeMethod(app.window, "saveSplitPaths",
                                          Q_ARG(QVariant, first), Q_ARG(QVariant, second),
                                          Q_ARG(QVariant, orientation), Q_ARG(QVariant, -1)));
        QCOMPARE(app.bookmarks->rowCount(), 1);
        QVERIFY(!app.bookmarks->containsPath(first));
        QVERIFY(!app.bookmarks->containsPath(second));

        // Reopen through the real sidebar row, with a search active beforehand.
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSearch"));
        QQuickItem *entry = app.item("bookmark_0");
        QTRY_VERIFY(entry = app.item("bookmark_0"));
        QTest::qWait(300); // bookmark insertion animation
        QTest::mouseClick(app.window, Qt::LeftButton, {},
                         entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2)).toPoint());
        TabModel *tab = app.tabModel->activeTab();
        QTRY_VERIFY(tab->splitViewEnabled());
        QCOMPARE(tab->currentPath(), first);
        QCOMPARE(tab->secondaryCurrentPath(), second);
        QCOMPARE(tab->splitOrientation(), orientation);
        QVERIFY(!app.window->property("searchMode").toBool());
        QCOMPARE(app.window->property("activePane").toString(), QString("primary"));

        if (qEnvironmentVariableIsSet("HYPRFM_SPLIT_GRAB")) {
            QTest::qWait(400);
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_SPLIT_GRAB") + "-" + orientation + ".png");
        }

        // The shared bookmark menu renames/removes the pair as one entry.
        QVERIFY(app.item("sidebarContextMenuLoader")->setProperty("active", true));
        QObject *menu = app.item("sidebarContextMenuLoader")->property("item").value<QObject *>();
        QVERIFY(menu);
        const QVariantMap item{{"kind", "savedSplit"}, {"index", 0}};
        QVERIFY(menu->setProperty("sidebarItem", item));
        QVERIFY(QMetaObject::invokeMethod(menu, "executeAction",
                                          Q_ARG(QVariant, QStringLiteral("removebookmark")),
                                          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QCOMPARE(app.bookmarks->rowCount(), 0);
        QVERIFY(app.bookmarks->savedSplits().isEmpty());
    }

    // #69: "Empty Trash" in the sidebar's Trash menu did nothing.
    void testSidebarSectionHeadersAndDeviceMountButtons()
    {
        App app;
        QVERIFY(app.load());
        QQuickItem *content = app.window->contentItem();
        QQuickItem *places = App::findText(content, QStringLiteral("PLACES"));
        QQuickItem *favourites = App::findText(content, QStringLiteral("FAVOURITES"));
        QQuickItem *devicesHeader = App::findText(content, QStringLiteral("DEVICES"));
        QVERIFY(places && favourites && devicesHeader);
        QVERIFY(places->isVisible() && favourites->isVisible());
        // No devices yet (the test model defers its scan): no empty DEVICES heading.
        QVERIFY(!devicesHeader->isVisible());

        FakeSidebarDevices fake;
        app.engine.rootContext()->setContextProperty("devices", &fake.devices);
        QTRY_VERIFY(devicesHeader->isVisible());
        // Places come first, then Favourites, then Devices (once the layout has
        // placed the heading that just became visible).
        QTRY_VERIFY(places->mapToScene({0, 0}).y() < favourites->mapToScene({0, 0}).y());
        QTRY_VERIFY2(favourites->mapToScene({0, 0}).y() < devicesHeader->mapToScene({0, 0}).y(),
                     qPrintable(QStringLiteral("favourites %1, devices %2")
                                    .arg(favourites->mapToScene({0, 0}).y())
                                    .arg(devicesHeader->mapToScene({0, 0}).y())));

        QList<QQuickItem *> buttons;
        std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
            for (QQuickItem *child : item->childItems()) {
                if (child->objectName() == QLatin1String("deviceMountButton"))
                    buttons.append(child);
                collect(child);
            }
        };
        QTRY_VERIFY((buttons.clear(), collect(content), buttons.size() == 4));
        const auto buttonFor = [&](const QString &name) -> QQuickItem * {
            for (QQuickItem *button : std::as_const(buttons))
                if (button->property("deviceName").toString() == name)
                    return button;
            return nullptr;
        };
        QQuickItem *system = buttonFor(QStringLiteral("System"));
        QQuickItem *usb = buttonFor(QStringLiteral("USB stick"));
        QQuickItem *spare = buttonFor(QStringLiteral("Spare"));
        QQuickItem *phone = buttonFor(QStringLiteral("Phone"));
        QVERIFY(system && usb && spare && phone);
        QVERIFY(!system->isVisible());                       // never unmount /
        QVERIFY(usb->isVisible());
        QCOMPARE(usb->property("tooltip").toString(), QStringLiteral("Unmount"));
        QVERIFY(spare->isVisible());
        QCOMPARE(spare->property("tooltip").toString(), QStringLiteral("Mount"));
        QVERIFY(!phone->isVisible());                        // its row click explains gvfs-mtp
        app.engine.rootContext()->setContextProperty("devices", nullptr);
    }

    void testSidebarEmptyTrashAsksForConfirmation()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(app.item("sidebarContextMenuLoader")->setProperty("active", true));
        QObject *menu = app.item("sidebarContextMenuLoader")->property("item").value<QObject *>();
        QVERIFY(menu);
        QVERIFY(menu->setProperty("sidebarItem", QVariantMap{{"kind", "quickAccess"}, {"path", "trash:///"}}));
        QVERIFY(QMetaObject::invokeMethod(menu, "executeAction",
                                          Q_ARG(QVariant, QStringLiteral("emptytrash")),
                                          Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant())));
        QVERIFY(app.item("emptyTrashConfirmDialogLoader")->property("active").toBool());
    }

    void testClickingPathBarEditsAndEscapeCancels()
    {
        App app;
        QVERIFY(app.load());
        QQuickItem *bar = app.item("breadcrumb");
        QVERIFY(bar);
        const QString original = app.tabModel->activeTab()->currentPath();
        QTest::mouseClick(app.window, Qt::LeftButton, {},
                         bar->mapToScene(QPointF(bar->width() - 8, bar->height() / 2)).toPoint());
        QVERIFY(bar->property("editMode").toBool());
        QQuickItem *input = app.item("pathInput");
        QVERIFY(input);
        QVERIFY(input->hasActiveFocus());
        QCOMPARE(input->property("selectedText").toString(), original);
        for (const QChar c : QStringLiteral("/tmp"))
            QTest::keyClick(app.window, c.toLatin1());
        QTest::keyClick(app.window, Qt::Key_Escape);
        QVERIFY(!bar->property("editMode").toBool());
        QCOMPARE(app.tabModel->activeTab()->currentPath(), original);
        // Focus goes back to the files, so the keyboard works straight away.
        auto *grid = app.item("primaryFileView")->property("gridViewItem").value<QQuickItem *>();
        QTRY_VERIFY(grid->hasActiveFocus());
    }

    void testPathBarNavigatesTypedDirectory_data()
    {
        QTest::addColumn<QString>("inputKind");
        for (const char *kind : {"absolute", "relative", "colon", "tilde", "completion", "secondary"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }

    void testPathBarNavigatesTypedDirectory()
    {
        QFETCH(QString, inputKind);
        App app;
        QVERIFY(app.load());
        // "v1:old" looks like a URI scheme but is a folder in the current one.
        const QString target = app.home.path() + (inputKind == "colon" ? "/v1:old" : "/Folder with spaces");
        QVERIFY(QDir().mkpath(target + "/child"));
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(app.home.path());
        if (inputKind == "secondary") {
            tab->resetSecondaryTo(app.home.path());
            tab->setSplitViewEnabled(true);
            QVERIFY(QMetaObject::invokeMethod(app.window, "setActivePane",
                                              Q_ARG(QVariant, QStringLiteral("secondary"))));
        }
        QTest::keyClick(app.window, Qt::Key_L, Qt::ControlModifier);
        QQuickItem *bar = app.item("breadcrumb");
        QVERIFY(bar);
        QTRY_VERIFY(bar->property("editMode").toBool());
        QQuickItem *input = app.item("pathInput");
        QVERIFY(input);
        const QString typed = inputKind == "relative" ? "Folder with spaces/"
            : inputKind == "colon" ? "v1:old"
            : inputKind == "tilde" ? "~/"
            : inputKind == "completion" ? target.left(target.size() - 3)
            : target + "/";
        input->setProperty("text", typed);
        if (inputKind == "completion") {
            QTRY_VERIFY(!bar->property("suggestionItems").toList().isEmpty());
            QTest::keyClick(app.window, Qt::Key_Tab);
        }
        QTest::keyClick(app.window, Qt::Key_Return);
        const QString expected = inputKind == "tilde" ? QDir::homePath() : target;
        QTRY_COMPARE(QDir::cleanPath(inputKind == "secondary" ? tab->secondaryCurrentPath() : tab->currentPath()), expected);
        QVERIFY(!bar->property("editMode").toBool());
        if (inputKind == "secondary") {
            QCOMPARE(tab->currentPath(), app.home.path());
            QVERIFY(tab->splitViewEnabled());
        }
    }

    // The explicit toolbar/keyboard Home action closes the split; ordinary
    // navigation to ~, including the sidebar, keeps the other pane.
    void testExplicitHomeActionClosesTheSplit()
    {
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        const QString sub = app.home.path() + "/sub";
        QVERIFY(QDir().mkpath(sub));
        tab->navigateTo(app.home.path());
        tab->resetSecondaryTo(sub);
        tab->setSplitViewEnabled(true);
        QVERIFY(QMetaObject::invokeMethod(app.window, "setActivePane",
                                          Q_ARG(QVariant, QStringLiteral("secondary"))));
        QVERIFY(QMetaObject::invokeMethod(app.window, "navigateActivePaneTo",
                                          Q_ARG(QVariant, QDir::homePath())));
        QTRY_COMPARE(tab->secondaryCurrentPath(), QDir::homePath());
        QVERIFY(tab->splitViewEnabled());
        QCOMPARE(tab->currentPath(), app.home.path());

        QVERIFY(QMetaObject::invokeMethod(app.window, "goHome"));
        QTRY_VERIFY(!tab->splitViewEnabled());
        QTRY_COMPARE(tab->currentPath(), QDir::homePath());
    }

    // Tab rebuilds the suggestion list for the completed text; the index of
    // the row picked before must not survive into the new list.
    void testPathBarTabThenReturnOpensCompletedPath()
    {
        App app;
        QVERIFY(app.load());
        for (const char *name : {"Dall", "Dbox", "Dcar"})
            QVERIFY(QDir().mkpath(app.home.path() + "/" + name));
        QTest::keyClick(app.window, Qt::Key_L, Qt::ControlModifier);
        QQuickItem *bar = app.item("breadcrumb");
        QTRY_VERIFY(bar->property("editMode").toBool());
        app.item("pathInput")->setProperty("text", app.home.path() + "/D");
        QTRY_COMPARE(bar->property("suggestionItems").toList().size(), 3);
        QTest::keyClick(app.window, Qt::Key_Down);
        QTest::keyClick(app.window, Qt::Key_Down);
        QTest::keyClick(app.window, Qt::Key_Down);
        const QString picked = bar->property("suggestionItems").toList().at(2).toMap().value("path").toString();
        QTest::keyClick(app.window, Qt::Key_Tab);
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_COMPARE(QDir::cleanPath(app.tabModel->activeTab()->currentPath()), QDir::cleanPath(picked));
    }

    // Completing "~/..." offers and inserts "~" paths, not the expanded home.
    void testPathBarCompletionKeepsTilde()
    {
        App app;
        QVERIFY(app.load());
        QTest::keyClick(app.window, Qt::Key_L, Qt::ControlModifier);
        QQuickItem *bar = app.item("breadcrumb");
        QTRY_VERIFY(bar->property("editMode").toBool());
        auto *fsModel = app.owner.findChild<FileSystemModel *>();
        QVERIFY(fsModel);
        QSignalSpy suggestions(fsModel, &FileSystemModel::pathSuggestionsReady);
        app.item("pathInput")->setProperty("text", "~/");
        QTRY_VERIFY(!suggestions.isEmpty());
        const QVariantList items = bar->property("suggestionItems").toList();
        if (items.isEmpty())
            QSKIP("home has no folders to suggest");
        for (const QVariant &item : items) {
            const QString shown = item.toMap().value("displayPath").toString();
            QVERIFY2(shown.startsWith("~"), qPrintable(shown));
        }
        // Nothing is picked until the user chooses, so Return opens what was typed.
        QCOMPARE(bar->property("suggestionIndex").toInt(), -1);
    }

    // Quick preview is created the first time Space is pressed rather than at
    // startup; the first press has to build it and show the file.
    void testQuickPreviewIsCreatedOnFirstUse()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(!App::findItem(app.window->contentItem(), QStringLiteral("quickPreview")));

        QFile file(app.home.path() + "/note.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello\n");
        file.close();

        QObject *root = app.window->contentItem()->parent();
        QVERIFY(QMetaObject::invokeMethod(root, "showQuickPreview",
                                          Q_ARG(QVariant, file.fileName())));
        QQuickItem *preview = App::findItem(app.window->contentItem(), QStringLiteral("quickPreview"));
        QVERIFY(preview);
        QVERIFY(preview->property("active").toBool());
        QCOMPARE(preview->property("filePath").toString(), file.fileName());
    }

    // The cut and paste badges only build their icon while they show; cutting
    // a file still has to put the scissors on it.
    void testCutFileShowsItsScissorsBadge()
    {
        App app;
        QVERIFY(app.load());
        auto scissors = [&]() {
            QList<QQuickItem *> found;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (QByteArray(item->metaObject()->className()).startsWith("IconScissors")
                    && item->isVisible())
                    found.append(item);
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            walk(app.window->contentItem());
            return found.size();
        };
        QFile file(app.home.path() + "/cut-me.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QObject *root = app.window->contentItem()->parent();
        QVERIFY(QMetaObject::invokeMethod(root, "navigateActivePaneTo",
                                          Q_ARG(QVariant, app.home.path())));
        QTRY_VERIFY(App::findText(app.window->contentItem(), QStringLiteral("cut-me.txt")));
        QCOMPARE(scissors(), 0);

        auto *clipboard = qvariant_cast<QObject *>(
            app.engine.rootContext()->contextProperty("clipboard"));
        QVERIFY(clipboard);
        QVERIFY(QMetaObject::invokeMethod(clipboard, "cut",
                                          Q_ARG(QStringList, QStringList{file.fileName()})));
        QTRY_COMPARE(scissors(), 1);
    }

    // The confirmation, conflict and app-chooser dialogs are built the first
    // time they open; each has to come up from a cold start.
    void testLazyDialogsOpen_data()
    {
        QTest::addColumn<QString>("dialog");
        QTest::newRow("delete") << "deleteConfirmDialog";
        QTest::newRow("empty trash") << "emptyTrashConfirmDialog";
        QTest::newRow("bulk custom action") << "customActionConfirmDialog";
        QTest::newRow("run executable") << "runExecutableDialog";
        QTest::newRow("conflict") << "conflictDialog";
        QTest::newRow("app chooser") << "appChooserDialog";
    }

    void testLazyDialogsOpen()
    {
        QFETCH(QString, dialog);
        App app;
        QVERIFY(app.load());
        QObject *root = app.window->contentItem()->parent();
        QQuickItem *loader = app.item(dialog + "Loader");
        QVERIFY(loader);
        QCOMPARE(root->property(dialog.toUtf8()).value<QObject *>(), nullptr);

        QVERIFY(QMetaObject::invokeMethod(root, "openDialog",
                                          Q_ARG(QVariant, QVariant::fromValue<QObject *>(loader))));
        auto *item = root->property(dialog.toUtf8()).value<QQuickItem *>();
        QVERIFY(item);
        QTRY_VERIFY(item->isVisible());
    }

    // single_click_opens_folders: a click opens a folder, Ctrl+click only
    // selects it, and a habitual double-click does not carry its second
    // click into the folder it just opened (#51).
    void testSingleClickOpensFolders()
    {
        App app;
        const QString configDir = app.home.path() + "/.config/hyprfm";
        QDir().mkpath(configDir);
        {
            QFile f(configDir + "/config.toml");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[general]\nsingle_click_opens_folders = true\n");
        }
        QVERIFY(app.load());
        // "a" holds "b" in the same spot, where a leaked second click lands.
        const QString root = app.home.path() + "/root";
        QVERIFY(QDir().mkpath(root + "/a/b"));
        QObject *main = app.window->contentItem()->parent();
        QVERIFY(QMetaObject::invokeMethod(main, "navigateActivePaneTo", Q_ARG(QVariant, root)));
        QTRY_VERIFY(App::findText(app.window->contentItem(), QStringLiteral("a")));
        // findText hits the tile's hidden measuring label; click the tile.
        QQuickItem *tile = App::findText(app.window->contentItem(), QStringLiteral("a"))->parentItem();
        QTRY_VERIFY(tile->isVisible() && tile->width() > 0);
        const QPoint a = app.center(tile);

        QTest::mouseClick(app.window, Qt::LeftButton, Qt::ControlModifier, a);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        QCOMPARE(app.tabModel->activeTab()->currentPath(), root);

        QTest::mouseDClick(app.window, Qt::LeftButton, {}, a);
        QTRY_COMPARE(app.tabModel->activeTab()->currentPath(), root + "/a");
        QTest::qWait(300);
        QCOMPARE(app.tabModel->activeTab()->currentPath(), root + "/a");
    }

    // Activating a program asks how to run it instead of handing it to
    // xdg-open, which only knows to open scripts in an editor (#48).
    void testActivatingExecutableAsksToRun()
    {
        App app;
        QVERIFY(app.load());
        const QString script = app.home.path() + "/tool.sh";
        {
            QFile f(script);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n");
        }
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        QObject *root = app.window->contentItem()->parent();
        QVERIFY(QMetaObject::invokeMethod(root, "handlePaneFileActivated",
                                          Q_ARG(QVariant, QStringLiteral("primary")),
                                          Q_ARG(QVariant, script),
                                          Q_ARG(QVariant, false)));
        auto *dialog = root->property("runExecutableDialog").value<QQuickItem *>();
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(root->property("runCandidatePath").toString(), script);
    }

    // Typing debounces the recursive search: a burst of keystrokes starts one
    // search, for the last query, once typing pauses.
    void testSearchTypingStartsOneDebouncedSearch()
    {
        App app;
        QVERIFY(app.load());
        QObject *root = app.window->contentItem()->parent();
        QSignalSpy started(app.searchService, &SearchService::isSearchingChanged);
        QVERIFY(QMetaObject::invokeMethod(root, "handleSearchQuery", Q_ARG(QVariant, QStringLiteral("ma"))));
        QVERIFY(QMetaObject::invokeMethod(root, "handleSearchQuery", Q_ARG(QVariant, QStringLiteral("mai"))));
        QVERIFY(QMetaObject::invokeMethod(root, "handleSearchQuery", Q_ARG(QVariant, QStringLiteral("main"))));
        QTest::qWait(300);
        QCOMPARE(started.count(), 0);   // still inside the debounce
        QTRY_VERIFY(started.count() >= 1);
        QCOMPARE(app.searchProxy->property("searchQuery").toString(), QStringLiteral("main"));
    }

    // Properties metadata comes from the preview service's workers now, not
    // from exiftool/ffprobe run on the GUI thread: it has to arrive.
    void testPropertiesMetadataArrivesAsynchronously()
    {
        App app;
        QVERIFY(app.load());
        const QString png = app.home.path() + "/pixel.png";
        QImage image(3, 2, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(png));

        QObject *root = app.window->contentItem()->parent();
        QVERIFY(QMetaObject::invokeMethod(root, "showPropertiesFor", Q_ARG(QVariant, png)));
        QQuickItem *dialog = app.item("propertiesDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(!dialog->property("_metadataKeys").toList().isEmpty());
    }

    void testWheelOverTabStripScrollsTabsInTheFullWindow()
    {
        App app;
        QVERIFY(app.load());
        while (app.tabModel->rowCount() < 12)
            app.tabModel->addTab();
        QTest::qWait(700);   // tab bar height animation + enter animations
        QQuickItem *strip = app.item("tabStrip");
        QVERIFY(strip);
        app.tabModel->setActiveIndex(0);
        QTRY_COMPARE(strip->property("contentX").toReal(), 0.0);
        QVERIFY(strip->property("contentWidth").toReal() > strip->width());

        app.wheel(app.center(strip), QPoint(0, -120));
        QTRY_VERIFY2(strip->property("contentX").toReal() > 0,
                     qPrintable(QStringLiteral("contentX stayed %1").arg(strip->property("contentX").toReal())));
    }

    void testInlineBookmarkRenameCommitsOnReturnAndCancelsOnEscape()
    {
        App app;
        QVERIFY(app.load());
        app.bookmarks->setBookmarks({"~/Documents"});
        QQuickItem *sidebar = app.item("sidebarPanel");
        QVERIFY(sidebar);

        QVERIFY(QMetaObject::invokeMethod(sidebar, "startBookmarkRename", Q_ARG(QVariant, 0)));
        QQuickItem *input = app.item("bookmarkRenameInput");
        QVERIFY(input);
        QTRY_VERIFY(input->hasActiveFocus());
        for (QChar c : QStringLiteral("Work"))   // replaces the selected auto name
            QTest::keyClick(app.window, c.toLatin1());
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_COMPARE(sidebar->property("renamingBookmarkIndex").toInt(), -1);
        QCOMPARE(app.bookmarks->data(app.bookmarks->index(0), BookmarkModel::NameRole).toString(),
                 QString("Work"));

        QVERIFY(QMetaObject::invokeMethod(sidebar, "startBookmarkRename", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(input->hasActiveFocus());
        for (QChar c : QStringLiteral("Nope"))
            QTest::keyClick(app.window, c.toLatin1());
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_COMPARE(sidebar->property("renamingBookmarkIndex").toInt(), -1);
        QCOMPARE(app.bookmarks->data(app.bookmarks->index(0), BookmarkModel::NameRole).toString(),
                 QString("Work"));
    }

    // Settings > Layout > Icon Size writes straight into sessionState, so the
    // views have to follow it live. Before this they only read it once at
    // startup and the sliders moved nothing.
    void testIconSizeFromSessionStateReachesTheViews()
    {
        App app;
        QVERIFY(app.load());
        QQuickItem *container = app.item("primaryFileView");
        QVERIFY(container);
        auto subView = [&](const char *alias) {
            return qvariant_cast<QQuickItem *>(container->property(alias));
        };
        QQuickItem *grid = subView("gridViewItem");
        QVERIFY(grid);
        // The other views are only built the first time they are shown.
        QVERIFY(!subView("detailedViewItem") && !subView("millerViewItem"));

        // What the settings sliders do: write, then apply to every pane.
        app.sessionState->setGridColumns(4);
        app.sessionState->setGridIconSize(300);
        app.sessionState->setRowHeightDetailed(40);
        app.sessionState->setRowHeightMiller(48);
        app.sessionState->setRowHeightCompact(34);
        app.sessionState->applyZoomToAllPanes();
        QTRY_COMPARE(grid->property("columnCount").toInt(), 4);
        QTRY_COMPARE(qRound(grid->property("zoomIconSize").toDouble()), 300);

        // A view built later has to start at the saved zoom, and must not
        // write its own default back over it.
        app.tabModel->activeTab()->setViewMode(QStringLiteral("detailed"));
        QTRY_VERIFY(subView("detailedViewItem"));
        QQuickItem *detailed = subView("detailedViewItem");
        QTRY_COMPARE(detailed->property("rowHeight").toInt(), 40);
        app.tabModel->activeTab()->setViewMode(QStringLiteral("miller"));
        QTRY_VERIFY(subView("millerViewItem"));
        QQuickItem *miller = subView("millerViewItem");
        QTRY_COMPARE(miller->property("rowHeight").toInt(), 48);
        QCOMPARE(app.sessionState->rowHeightDetailed(), 40);
        QCOMPARE(app.sessionState->rowHeightMiller(), 48);
        app.tabModel->activeTab()->setViewMode(QStringLiteral("compact"));
        QTRY_VERIFY(subView("compactViewItem"));
        QQuickItem *compact = subView("compactViewItem");
        QTRY_COMPARE(compact->property("rowHeight").toInt(), 34);
        app.sessionState->setRowHeightCompact(99);
        app.sessionState->applyZoomToAllPanes();
        QTRY_COMPARE(compact->property("rowHeight").toInt(), 56);
        QCOMPARE(app.sessionState->rowHeightCompact(), 56);

        // Once built, a hidden view still takes a value applied to every pane...
        app.sessionState->setRowHeightDetailed(36);
        app.sessionState->applyZoomToAllPanes();
        QTRY_COMPARE(detailed->property("rowHeight").toInt(), 36);
        // ... but not a value another pane merely wrote (each pane zooms on its own).
        app.sessionState->setRowHeightDetailed(44);
        QTest::qWait(50);
        QCOMPARE(detailed->property("rowHeight").toInt(), 36);

        // A view clamps what it accepts and the mirror writes the clamped
        // value back, so session.json never keeps something unusable.
        app.sessionState->setGridColumns(99);
        app.sessionState->setGridIconSize(9999);
        app.sessionState->applyZoomToAllPanes();
        QTRY_COMPARE(grid->property("columnCount").toInt(), 12);
        QTRY_COMPARE(app.sessionState->gridColumns(), 12);
        QTRY_COMPARE(qRound(grid->property("zoomIconSize").toDouble()), 512);
        QTRY_COMPARE(app.sessionState->gridIconSize(), 512);
    }

    // Grid zoom is a continuous icon size: Ctrl+Wheel scales it ~15% a notch
    // (a touchpad by fractions), it animates, it goes down to 32 px (a column
    // count stopped at 88 px), and the icons are not reloaded every frame:
    // their URLs follow the settled size, and the last render stays while
    // the new one loads.
    void testGridZoomIsSmoothContinuousAndGoesSmall()
    {
        App app;
        QVERIFY(app.load());
        app.window->resize(1600, 700);   // wide enough that small icons need >12 columns
        for (int i = 0; i < 6; ++i)
            QVERIFY(QFile(app.home.path() + QStringLiteral("/file%1.txt").arg(i)).open(QIODevice::WriteOnly));
        app.tabModel->activeTab()->navigateTo(app.home.path());
        app.tabModel->activeTab()->setViewMode("grid");
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        QTRY_VERIFY(container->property("gridViewItem").value<QQuickItem *>());
        auto *grid = container->property("gridViewItem").value<QQuickItem *>();
        QTRY_VERIFY(grid->property("count").toInt() >= 6);
        QVERIFY(QMetaObject::invokeMethod(grid, "setZoomIconSize", Q_ARG(QVariant, 184), Q_ARG(QVariant, false)));
        QTRY_COMPARE(grid->property("iconSize").toInt(), 184);
        app.window->grabWindow();

        const auto iconImages = [&]() {
            QList<QQuickItem *> found;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                for (QQuickItem *child : item->childItems()) {
                    if (child->property("source").toString().startsWith(QLatin1String("image://icon/")))
                        found.append(child);
                    walk(child);
                }
            };
            walk(grid);
            return found;
        };
        QTRY_VERIFY(!iconImages().isEmpty());
        QQuickItem *icon = iconImages().first();
        if (icon->metaObject()->indexOfProperty("retainWhileLoading") >= 0)
            QVERIFY(icon->property("retainWhileLoading").toBool());
        const QString sourceBefore = icon->property("source").toString();
        QVERIFY(sourceBefore.contains(QLatin1String("logicalSize=184")));

        const QPoint point = app.center(grid);
        const auto wheel = [&](QPoint angle, QPoint pixels) {
            QWheelEvent event(point, app.window->mapToGlobal(point), pixels, angle, Qt::NoButton,
                              Qt::ControlModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(app.window, &event);
        };
        wheel(QPoint(0, 120), QPoint());
        QCOMPARE(qRound(grid->property("zoomIconSize").toDouble()), qRound(184 * 1.15));
        // Animated: right after the notch the drawn size has not jumped yet.
        QVERIFY(grid->property("iconSize").toInt() < qRound(184 * 1.15));
        // Mid-zoom the URL is unchanged, so nothing reloads...
        QCOMPARE(icon->property("source").toString(), sourceBefore);
        QTRY_COMPARE(grid->property("iconSize").toInt(), qRound(184 * 1.15));
        // ... and once the zoom settles the icon is rendered at its new size.
        QTRY_VERIFY(icon->property("source").toString().contains(
            QStringLiteral("logicalSize=%1").arg(qRound(184 * 1.15))));

        // A touchpad's half notch zooms by half a step.
        const qreal beforeHalf = grid->property("zoomIconSize").toDouble();
        wheel(QPoint(0, 60), QPoint(0, 5));
        QCOMPARE(qRound(grid->property("zoomIconSize").toDouble()), qRound(beforeHalf * std::pow(1.15, 0.5)));

        // Far out: 32 px icons, and more columns than a column count allowed.
        for (int i = 0; i < 40; ++i)
            wheel(QPoint(0, -120), QPoint());
        QTRY_COMPARE(grid->property("iconSize").toInt(), 32);
        QCOMPARE(qRound(grid->property("zoomIconSize").toDouble()), 32);
        QVERIFY(grid->property("effectiveColumnCount").toInt() > 12);
        QCOMPARE(app.sessionState->gridIconSize(), 32);
    }

    // Resizing the window changes how many grid columns fit, not the icon
    // size (#61). grid_fixed_columns brings back the old column-count zoom,
    // where the icons stretch with the window instead.
    void testGridIconSizeFollowsZoomNotWindow()
    {
        App app;
        QVERIFY(app.load());
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        auto *grid = qvariant_cast<QQuickItem *>(container->property("gridViewItem"));
        QVERIFY(grid);
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();

        app.window->resize(1100, 700);
        QTRY_VERIFY(grid->width() > 700);
        const int wideIcon = grid->property("iconSize").toInt();
        const int wideColumns = grid->property("effectiveColumnCount").toInt();
        app.window->resize(700, 700);
        QTRY_VERIFY(grid->property("effectiveColumnCount").toInt() < wideColumns);
        QCOMPARE(grid->property("iconSize").toInt(), wideIcon);

        // Zooming in still makes the icons bigger.
        app.sessionState->setGridIconSize(wideIcon + 100);
        app.sessionState->setGridColumns(4);
        app.sessionState->applyZoomToAllPanes();
        QTRY_VERIFY(grid->property("iconSize").toInt() > wideIcon);

        QVariantMap settings;
        settings.insert(QStringLiteral("gridFixedColumns"), true);
        QMetaObject::invokeMethod(config, "saveSettings", Q_ARG(QVariantMap, settings));
        app.window->resize(1100, 700);
        QTRY_COMPARE(grid->property("effectiveColumnCount").toInt(), 4);
        const int fixedWideIcon = grid->property("iconSize").toInt();
        app.window->resize(900, 700);
        QTRY_VERIFY(grid->property("iconSize").toInt() < fixedWideIcon);
        QCOMPARE(grid->property("effectiveColumnCount").toInt(), 4);
    }

    // The Icon Size sliders sit on the Layout page (section 1), which lives in
    // a Loader. Dragging one has to move the zoom, and coming back to the page
    // has to show whatever the zoom is now — including a Ctrl+wheel made while
    // the panel was on another page.
    void testSettingsIconSizeSlidersReadAndWriteTheZoom()
    {
        App app;
        QVERIFY(app.load());
        // The panel is only created the first time it opens.
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 1)));

        auto slider = [&](const char *name) {
            return panel->findChild<QObject *>(QLatin1String(name));
        };
        QObject *gridSlider = slider("iconSizeGrid");
        QVERIFY(gridSlider);
        QVERIFY(slider("iconSizeDetailed") && slider("iconSizeMiller") && slider("iconSizeCompact"));

        // The grid slider is the icon size in px, and it reaches the pane.
        auto *container = app.item("primaryFileView");
        auto *grid = qvariant_cast<QQuickItem *>(container->property("gridViewItem"));
        QVERIFY(grid);
        QVERIFY(QMetaObject::invokeMethod(gridSlider, "moved", Q_ARG(qreal, 96)));
        QCOMPARE(app.sessionState->gridIconSize(), 96);
        QTRY_COMPARE(qRound(grid->property("zoomIconSize").toDouble()), 96);

        // With fixed columns the slider is the column count again, flipped:
        // one notch right enlarges the icons, which means one column fewer.
        QObject *columnsSlider = slider("gridColumnsFixed");
        QVERIFY(columnsSlider);
        const int columnsBefore = app.sessionState->gridColumns();
        QVERIFY(QMetaObject::invokeMethod(columnsSlider, "moved",
                                          Q_ARG(qreal, columnsSlider->property("value").toReal() + 1)));
        QCOMPARE(app.sessionState->gridColumns(), columnsBefore - 1);

        // Zoom changed elsewhere shows up when the page is next shown.
        app.sessionState->setGridIconSize(240);
        app.sessionState->setGridColumns(3);
        app.sessionState->setRowHeightDetailed(50);
        app.sessionState->setRowHeightMiller(24);
        app.sessionState->setRowHeightCompact(32);
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 0)));
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 1)));

        QObject *reloadedGrid = slider("iconSizeGrid");
        QVERIFY(reloadedGrid);
        QCOMPARE(reloadedGrid->property("value").toInt(), 240);
        QObject *reloadedColumns = slider("gridColumnsFixed");
        QVERIFY(reloadedColumns);
        QCOMPARE(reloadedColumns->property("value").toInt(),
                 reloadedColumns->property("gridSpan").toInt() - 3);
        QCOMPARE(slider("iconSizeDetailed")->property("value").toInt(), 50);
        QCOMPARE(slider("iconSizeMiller")->property("value").toInt(), 24);
        QCOMPARE(slider("iconSizeCompact")->property("value").toInt(), 32);
    }

    // Switching view used to be reachable only through Ctrl+1/2/3 or the
    // right-click menu. The toolbar now carries the four modes, and the
    // active one is marked.
    void testToolbarViewSwitcherChangesTheViewMode()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(app.tabModel->activeTab());
        QCOMPARE(app.tabModel->activeTab()->viewMode(), QString("grid"));

        auto button = [&](const char *mode) {
            return app.item(QStringLiteral("viewModeButton_") + QLatin1String(mode));
        };
        QQuickItem *grid = button("grid");
        QQuickItem *miller = button("miller");
        QQuickItem *detailed = button("detailed");
        QQuickItem *compact = button("compact");
        QVERIFY(grid && miller && detailed && compact);
        QVERIFY(grid->property("current").toBool());
        QVERIFY(!miller->property("current").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(miller));
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("miller"));
        QTRY_VERIFY(miller->property("current").toBool());
        QVERIFY(!grid->property("current").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(detailed));
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("detailed"));

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(compact));
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("compact"));
        QTRY_VERIFY(compact->property("current").toBool());

        // The Ctrl+1/2/3/4 shortcuts still drive the same state, so the toolbar
        // has to follow them too.
        QTest::keyClick(app.window, Qt::Key_1, Qt::ControlModifier);
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("grid"));
        QTRY_VERIFY(grid->property("current").toBool());
        QTest::keyClick(app.window, Qt::Key_4, Qt::ControlModifier);
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("compact"));
        QTRY_VERIFY(compact->property("current").toBool());
    }

    void testCompactViewRoutesSelectionInSplitPanesAndSearch()
    {
        App app;
        const QString directory = app.home.path() + "/files";
        QVERIFY(QDir().mkpath(directory));
        const QString path = directory + "/compact-match.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(directory);
        tab->setViewMode("compact");
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        QTRY_VERIFY(container->property("compactViewItem").value<QQuickItem *>());
        auto *compact = container->property("compactViewItem").value<QQuickItem *>();
        QTRY_COMPARE(compact->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(compact, "focusPath", Q_ARG(QVariant, path), Q_ARG(QVariant, true)));
        QVariant paths;
        QVERIFY(QMetaObject::invokeMethod(app.window, "getSelectedPaths", Q_RETURN_ARG(QVariant, paths),
                                          Q_ARG(QVariant, QStringLiteral("primary"))));
        QCOMPARE(paths.toStringList(), QStringList{path});

        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitView"));
        QTRY_VERIFY(tab->splitViewEnabled());
        QVariant secondary;
        QVERIFY(QMetaObject::invokeMethod(app.window, "fileViewForPane", Q_RETURN_ARG(QVariant, secondary),
                                          Q_ARG(QVariant, QStringLiteral("secondary"))));
        auto *secondaryContainer = secondary.value<QObject *>();
        QVERIFY(secondaryContainer);
        QTRY_VERIFY(secondaryContainer->property("compactViewItem").value<QQuickItem *>());
        auto *other = secondaryContainer->property("compactViewItem").value<QQuickItem *>();
        QTRY_COMPARE(other->property("count").toInt(), 1);
        // The settings slider means both panes...
        app.sessionState->setRowHeightCompact(36);
        app.sessionState->applyZoomToAllPanes();
        QTRY_COMPARE(compact->property("rowHeight").toInt(), 36);
        QCOMPARE(other->property("rowHeight").toInt(), 36);
        // ... Ctrl+Wheel over one pane zooms that pane only (and is remembered).
        app.window->grabWindow();
        const QPoint left = compact->mapToScene(QPointF(compact->width() / 2, 40)).toPoint();
        QWheelEvent zoom(left, app.window->mapToGlobal(left), {}, QPoint(0, 120), Qt::NoButton,
                         Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &zoom);
        QTRY_COMPARE(compact->property("rowHeight").toInt(), 38);
        QTest::qWait(50);
        QCOMPARE(other->property("rowHeight").toInt(), 36);
        QCOMPARE(app.sessionState->rowHeightCompact(), 38);

        QVERIFY(QMetaObject::invokeMethod(app.window, "setActivePane", Q_ARG(QVariant, QStringLiteral("primary"))));
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSearch"));
        QVERIFY(QMetaObject::invokeMethod(app.window, "handleSearchQuery", Q_ARG(QVariant, QStringLiteral("compact-match"))));
        QTRY_COMPARE(app.searchProxy->rowCount(), 1);
        QTRY_COMPARE(compact->property("count").toInt(), 1);
        QCOMPARE(compact->property("highlightQuery").toString(), QString("compact-match"));
        QVERIFY(QMetaObject::invokeMethod(compact, "focusPath", Q_ARG(QVariant, path), Q_ARG(QVariant, true)));
        QVERIFY(QMetaObject::invokeMethod(app.window, "getSelectedPaths", Q_RETURN_ARG(QVariant, paths),
                                          Q_ARG(QVariant, QStringLiteral("primary"))));
        QCOMPARE(paths.toStringList(), QStringList{path});
    }

    void testToolbarSplitButtonsSelectTheirOrientation()
    {
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        QVERIFY(tab);
        auto *vertical = app.item("verticalSplitButton");
        auto *horizontal = app.item("horizontalSplitButton");
        QVERIFY(vertical && horizontal);
        auto *grid = app.item("viewModeButton_grid");
        QVERIFY(grid);
        QCOMPARE(vertical->parentItem(), horizontal->parentItem());
        QVERIFY(vertical->parentItem() != grid->parentItem());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(vertical));
        QTRY_VERIFY(tab->splitViewEnabled());
        QCOMPARE(tab->splitOrientation(), QStringLiteral("vertical"));
        QVERIFY(vertical->property("current").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(horizontal));
        QTRY_COMPARE(tab->splitOrientation(), QStringLiteral("horizontal"));
        QVERIFY(tab->splitViewEnabled());
        QVERIFY(horizontal->property("current").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(horizontal));
        QTRY_VERIFY(!tab->splitViewEnabled());
    }

    static void showOtherFolderInSecondary(App &app)
    {
        const QString other = app.home.path() + "/other";
        QVERIFY(QDir().mkpath(other));
        app.tabModel->activeTab()->navigateSecondaryTo(other);
        QTRY_COMPARE(app.tabModel->activeTab()->secondaryCurrentPath(), other);
    }

    // Opening a split in the other orientation used to animate the side-by-side
    // to stacked fold while the new pane grew in, so a horizontal split first
    // appeared on the right and then dropped down. With no split on screen the
    // orientation must jump, so the pane grows in from below from the start.
    void testOpeningHorizontalSplitDoesNotFold()
    {
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        QVERIFY(tab && !tab->splitViewEnabled());
        auto *frame = app.item("primaryPaneFrame");
        QVERIFY(frame && frame->parentItem());
        QQuickItem *paneRow = frame->parentItem();
        QCOMPARE(paneRow->property("orientationProgress").toReal(), 0.0);

        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitInOrientation",
                                          Q_ARG(QVariant, QStringLiteral("horizontal"))));
        QVERIFY(tab->splitViewEnabled());
        QCOMPARE(paneRow->property("orientationProgress").toReal(), 1.0);
        auto *secondary = app.item("secondaryPaneLoader");
        QVERIFY(secondary);
        QCOMPARE(secondary->x(), 0.0);

        // A split already on screen still folds when its orientation changes.
        QTRY_COMPARE(app.window->property("splitTransitionProgress").toReal(), 1.0);
        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitOrientation"));
        QTRY_VERIFY(paneRow->property("orientationProgress").toReal() < 1.0
                    && paneRow->property("orientationProgress").toReal() > 0.0);
    }

    void testActiveSplitAppearsInSidebarUntilSaved()
    {
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        QVERIFY(tab);
        auto *sidebar = app.item("sidebarPanel");
        auto *activeSplit = app.item("activeSplitRow");
        auto *save = app.item("saveActiveSplitButton");
        auto *emptyHint = app.item("emptyBookmarksHint");
        QVERIFY(sidebar && activeSplit && save && emptyHint);
        QVERIFY(!sidebar->property("activeSplitVisible").toBool());

        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitView"));
        // A new split shows the same folder twice: nothing worth saving yet.
        QTRY_VERIFY(tab->splitViewEnabled());
        QVERIFY(!sidebar->property("activeSplitVisible").toBool());
        showOtherFolderInSecondary(app);
        QTRY_VERIFY(sidebar->property("activeSplitVisible").toBool());
        QVERIFY(emptyHint->isVisible());
        app.window->grabWindow();
        QTRY_VERIFY(app.center(save).x() > 0);
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(save));
        QTRY_COMPARE(app.bookmarks->rowCount(), 1);
        QVERIFY(tab->splitViewEnabled());
        QTRY_VERIFY(!sidebar->property("activeSplitVisible").toBool());
        const auto split = app.bookmarks->splitAt(0);
        QCOMPARE(split.value("primaryPath").toString(), tab->currentPath());
        QCOMPARE(split.value("secondaryPath").toString(), tab->secondaryCurrentPath());
    }

    void testDragActiveSplitIntoEmptyBookmarks()
    {
        App app;
        QVERIFY(app.load());
        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitView"));
        showOtherFolderInSecondary(app);
        auto *activeSplit = app.item("activeSplitRow");
        auto *emptyHint = app.item("emptyBookmarksHint");
        QVERIFY(activeSplit && emptyHint);
        QTRY_VERIFY(emptyHint->isVisible());
        app.window->grabWindow();
        QTRY_VERIFY(app.center(activeSplit).x() > 0);

        const QPoint start = app.center(activeSplit);
        const QPoint finish = app.center(emptyHint);
        QTest::mousePress(app.window, Qt::LeftButton, {}, start);
        QTest::mouseMove(app.window, start + QPoint(0, 12));
        QTest::mouseMove(app.window, finish);
        QTest::mouseRelease(app.window, Qt::LeftButton, {}, finish);
        QTRY_COMPARE(app.bookmarks->rowCount(), 1);
    }

    void testDragActiveSplitAmongExistingBookmarks()
    {
        App app;
        QVERIFY(app.load());
        const QString folder = app.home.path() + "/favorite";
        QVERIFY(QDir().mkpath(folder));
        app.bookmarks->addBookmark(folder);
        QCOMPARE(app.bookmarks->rowCount(), 1);
        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitView"));
        showOtherFolderInSecondary(app);
        auto *activeSplit = app.item("activeSplitRow");
        QQuickItem *firstBookmark = nullptr;
        QTRY_VERIFY(firstBookmark = app.item("bookmark_0"));
        QVERIFY(activeSplit);
        app.window->grabWindow();

        const QPoint start = app.center(activeSplit);
        const QPoint finish = firstBookmark->mapToScene(
            QPointF(firstBookmark->width() / 2, 4)).toPoint();
        QTest::mousePress(app.window, Qt::LeftButton, {}, start);
        QTest::mouseMove(app.window, start + QPoint(0, 12));
        QTest::mouseMove(app.window, finish);
        QTest::mouseRelease(app.window, Qt::LeftButton, {}, finish);
        QTRY_COMPARE(app.bookmarks->rowCount(), 2);
        QCOMPARE(app.bookmarks->splitAt(0).value("primaryPath").toString(),
                 app.tabModel->activeTab()->currentPath());
    }

    // The status bar reports free space as a Dolphin-style meter: the bar
    // fills with what is used and the label sits on it. The fill has to track
    // the ratio and step through accent/warning/error at 75% and 90%, since a
    // meter nobody can read at a glance is just a coloured box.
    // The sidebar folds away on its own in a narrow window, comes back when
    // there is room again, and stops second-guessing the user the moment they
    // decide for themselves.
    void testSidebarFoldsAwayInANarrowWindow()
    {
        App app;
        QVERIFY(app.load());
        QQuickWindow *window = app.window;
        // The default threshold is 700; the window starts at 1100x700.
        QVERIFY(window->property("sidebarVisible").toBool());
        QVERIFY(!window->property("windowTooNarrowForSidebar").toBool());

        // Narrow: folds away.
        window->resize(560, 700);
        QTRY_VERIFY(!window->property("sidebarVisible").toBool());

        // Still toggleable by hand while narrow.
        QMetaObject::invokeMethod(window, "toggleSidebar");
        QTRY_VERIFY(window->property("sidebarVisible").toBool());

        // And that choice survives widening - no second restore on top of it.
        window->resize(1100, 700);
        QTRY_VERIFY(window->property("sidebarVisible").toBool());

        // Closing it by hand in a wide window keeps it closed when narrowed
        // and widened again.
        QMetaObject::invokeMethod(window, "toggleSidebar");
        QTRY_VERIFY(!window->property("sidebarVisible").toBool());
        window->resize(560, 700);
        QTest::qWait(50);
        QVERIFY(!window->property("sidebarVisible").toBool());
        window->resize(1100, 700);
        QTest::qWait(50);
        QVERIFY(!window->property("sidebarVisible").toBool());

        // HYPRFM_TEST_GRAB=<prefix> writes wide/narrow frames for eyeballing.
        if (qEnvironmentVariableIsSet("HYPRFM_TEST_GRAB")) {
            const QString prefix = qEnvironmentVariable("HYPRFM_TEST_GRAB");
            QMetaObject::invokeMethod(window, "toggleSidebar");   // back on
            QTest::qWait(400);
            window->grabWindow().save(prefix + QStringLiteral("-wide.png"));
            window->resize(560, 700);
            QTest::qWait(600);
            window->grabWindow().save(prefix + QStringLiteral("-narrow.png"));
            window->resize(1100, 700);
            QTest::qWait(600);
            window->grabWindow().save(prefix + QStringLiteral("-wide-again.png"));
        }
    }

    void testSidebarHeaderChoices()
    {
        App app;
        QVERIFY(app.load());
        QQuickWindow *window = app.window;
        auto *config = app.engine.rootContext()->contextProperty("config").value<QObject *>();
        QQuickItem *root = window->contentItem();
        QQuickItem *header = App::findItem(root, QStringLiteral("sidebarHeader"));
        QQuickItem *title = App::findItem(root, QStringLiteral("sidebarTitle"));
        QQuickItem *logo = App::findItem(root, QStringLiteral("sidebarLogo"));
        QQuickItem *toggle = App::findItem(root, QStringLiteral("sidebarToggleButton"));
        QVERIFY(header && title && logo && toggle);

        auto choose = [&](const char *value) {
            QVariantMap settings;
            settings.insert(QStringLiteral("sidebarHeader"), QString::fromLatin1(value));
            QMetaObject::invokeMethod(config, "saveSettings", Q_ARG(QVariantMap, settings));
        };

        QVERIFY(header->isVisible() && title->isVisible() && !logo->isVisible());
        QVERIFY(!toggle->property("shown").toBool());

        choose("logo");
        QTRY_VERIFY(logo->isVisible());
        QVERIFY(header->isVisible() && !title->isVisible());
        QTRY_VERIFY(logo->property("status").toInt() == 1);   // Image.Ready

        choose("button");
        QTRY_VERIFY(!logo->isVisible());
        QVERIFY(header->isVisible() && !title->isVisible());
        QVERIFY(!toggle->property("shown").toBool());

        // No header: the toolbar button closes the sidebar, and opens it again.
        choose("none");
        QTRY_VERIFY(!header->isVisible());
        QVERIFY(toggle->property("shown").toBool());
        QVERIFY(window->property("sidebarVisible").toBool());
        QMetaObject::invokeMethod(toggle, "clicked");
        QTRY_VERIFY(!window->property("sidebarVisible").toBool());
        QMetaObject::invokeMethod(toggle, "clicked");
        QTRY_VERIFY(window->property("sidebarVisible").toBool());
    }

    void testStatusBarDiskMeterTracksUsage()
    {
        App app;
        QVERIFY(app.load());
        QQuickItem *statusBar = app.item("statusBar");
        QVERIFY(statusBar);
        QQuickItem *meter = App::findItem(statusBar, QStringLiteral("diskMeter"));
        QQuickItem *fill = App::findItem(statusBar, QStringLiteral("diskMeterFill"));
        QVERIFY(meter && fill);

        // A real directory is on screen, so the meter is showing something.
        QVERIFY(meter->isVisible());

        auto show = [&](qint64 total, qint64 free) {
            statusBar->setProperty("diskTotal", double(total));
            statusBar->setProperty("diskFree", double(free));
        };
        auto tierColour = [&] { return meter->property("fillColor").value<QColor>(); };

        show(1000, 750);
        QTRY_COMPARE(meter->property("usedFraction").toDouble(), 0.25);
        QTRY_VERIFY(qAbs(fill->width() - meter->width() * 0.25) < 1.0);
        const QColor normal = tierColour();

        // Just under and just over each threshold, so the boundaries are the
        // thing under test rather than three arbitrary values.
        show(1000, 251);
        QTRY_COMPARE(tierColour(), normal);
        show(1000, 250);
        QTRY_VERIFY(tierColour() != normal);
        const QColor warning = tierColour();

        show(1000, 101);
        QTRY_COMPARE(tierColour(), warning);
        show(1000, 100);
        QTRY_VERIFY(tierColour() != warning);
        QVERIFY(tierColour() != normal);

        show(1000, 50);
        QTRY_VERIFY(qAbs(fill->width() - meter->width() * 0.95) < 1.0);

        // Yellow from 75% used, red from 90%, whatever the theme: its own
        // warning and error colours where they are yellow and red, amber and
        // red where not. The user's light theme has a teal warning; darcula
        // a blue error; tokyo-night a cyan warning.
        auto *theme = app.engine.rootContext()->contextProperty("theme").value<ThemeLoader *>();
        QVERIFY(theme);
        const auto yellow = [](const QColor &c) {
            return c.hslHueF() >= 0 && c.hslHueF() * 360 >= 25 && c.hslHueF() * 360 <= 70;
        };
        const auto red = [](const QColor &c) {
            return c.hslHueF() >= 0 && (c.hslHueF() * 360 >= 330 || c.hslHueF() * 360 <= 20);
        };
        const QList<std::pair<QString, QColor>> themes = {
            {"strata-omarchy-light", QColor("#006b82")}, {"strata-material-darker", QColor("#ffcb6b")},
            {"strata-tokyo-night", QColor("#0db9d7")}, {"strata-darcula", QColor("#bbb529")}};
        for (const auto &[name, ownWarning] : themes) {
            theme->loadTheme(name, {QStringLiteral(TEST_SOURCE_DIR "/themes")});
            QCOMPARE(theme->warning(), ownWarning);
            show(1000, 200);
            QTRY_VERIFY2(yellow(tierColour()), qPrintable(name + " " + tierColour().name()));
            show(1000, 50);
            QTRY_VERIFY2(red(tierColour()), qPrintable(name + " " + tierColour().name()));
        }
        // A theme whose colours already fit keeps them.
        theme->loadTheme(QStringLiteral("strata-material-darker"), {QStringLiteral(TEST_SOURCE_DIR "/themes")});
        show(1000, 200);
        QTRY_COMPARE(tierColour(), QColor("#ffcb6b"));
        show(1000, 50);
        QTRY_COMPARE(tierColour(), QColor("#F07178"));

        // Nothing to report hides the meter rather than drawing an empty bar.
        statusBar->setProperty("diskTotal", -1.0);
        statusBar->setProperty("diskFree", -1.0);
        QTRY_VERIFY(!meter->isVisible());
    }

    // ── Zoom keys and wheels ────────────────────────────────────────────

    static ConfigManager *configOf(App &app)
    {
        return qobject_cast<ConfigManager *>(app.engine.rootContext()->contextProperty("config").value<QObject *>());
    }
    static const char *viewAlias(const QString &mode)
    {
        return mode == "detailed" ? "detailedViewItem" : mode == "miller" ? "millerViewItem" : "compactViewItem";
    }
    static int savedRowHeight(App &app, const QString &mode)
    {
        return mode == "detailed" ? app.sessionState->rowHeightDetailed()
             : mode == "miller" ? app.sessionState->rowHeightMiller()
             : app.sessionState->rowHeightCompact();
    }

    void testTextSizeKeysZoomAFocusedPreview_data()
    {
        QTest::addColumn<bool>("quick");
        QTest::newRow("quick") << true;
        QTest::newRow("miller") << false;
    }

    // Ctrl++ / Ctrl+= / Ctrl+- belong to a focused preview as Ctrl+0 does.
    // They used to resize the interface text from inside the preview.
    void testTextSizeKeysZoomAFocusedPreview()
    {
        QFETCH(bool, quick);
        App app;
        const QString path = app.home.filePath("notes.txt");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray("Preview keys\n").repeated(80));
        }
        QVERIFY(app.load());
        auto *config = configOf(app);
        QVERIFY(config);
        config->saveSettings({{"fontSize", 10}});
        app.tabModel->activeTab()->navigateTo(app.home.path());
        if (!quick) app.tabModel->activeTab()->setViewMode("miller");
        auto *baseModel = app.engine.rootContext()->contextProperty("fsModel").value<QObject *>();
        QTRY_COMPARE(baseModel->property("rootPath").toString(), app.home.path());
        QTRY_VERIFY(!baseModel->property("isLoading").toBool());
        if (quick)
            QVERIFY(QMetaObject::invokeMethod(app.window, "showQuickPreview", Q_ARG(QVariant, path)));
        else
            QVERIFY(QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                              Q_ARG(QVariant, path), Q_ARG(QVariant, true)));
        QTRY_VERIFY(app.item(quick ? "quickPreviewZoom" : "millerPreviewZoom"));
        auto *controls = app.item(quick ? "quickPreviewZoom" : "millerPreviewZoom");
        QTRY_VERIFY(controls->property("available").toBool());
        auto *scope = quick ? app.window->property("quickPreview").value<QQuickItem *>() : app.item("millerPreviewColumn");
        QVERIFY(scope);
        scope->forceActiveFocus();
        QTRY_VERIFY(scope->hasActiveFocus());

        QTest::keyClick(app.window, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_VERIFY(controls->property("factor").toReal() > 1.0);
        const qreal once = controls->property("factor").toReal();
        QTest::keyClick(app.window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_VERIFY(controls->property("factor").toReal() > once);
        QTest::keyClick(app.window, Qt::Key_Minus, Qt::ControlModifier);
        QTRY_VERIFY(qAbs(controls->property("factor").toReal() - once) < 1e-6);
        QTest::keyClick(app.window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(controls->property("factor").toReal(), 1.0);
        QCOMPARE(config->fontSize(), 10.0);

        // Back in the file view the same keys size the interface text.
        QTest::keyClick(app.window, Qt::Key_Left, Qt::ControlModifier);
        QTRY_VERIFY(!scope->hasActiveFocus());
        QTest::keyClick(app.window, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(config->fontSize(), 11.0);
        QTest::keyClick(app.window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(config->fontSize(), 0.0);
        QCOMPARE(controls->property("factor").toReal(), 1.0);
    }

    // Text entry keeps Ctrl+= / Ctrl+- / Ctrl+0 like it keeps every pane command.
    void testTextSizeKeysLeaveTextEntryAlone()
    {
        App app;
        QVERIFY(app.load());
        auto *config = configOf(app);
        QVERIFY(config);
        config->saveSettings({{"fontSize", 10}});
        QTest::keyClick(app.window, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(app.window->activeFocusItem()
                    && app.window->activeFocusItem()->property("readOnly").isValid()
                    && !app.window->activeFocusItem()->property("readOnly").toBool());
        for (const Qt::Key key : {Qt::Key_Equal, Qt::Key_Plus, Qt::Key_Minus, Qt::Key_0})
            QTest::keyClick(app.window, key, Qt::ControlModifier);
        QTest::qWait(50);
        QCOMPARE(config->fontSize(), 10.0);
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(!app.window->activeFocusItem()
                    || !app.window->activeFocusItem()->property("readOnly").isValid()
                    || app.window->activeFocusItem()->property("readOnly").toBool());
        QTest::keyClick(app.window, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(config->fontSize(), 11.0);
    }

    void testLargeTextDoesNotInflateSavedRowHeights_data()
    {
        QTest::addColumn<QString>("viewMode");
        for (const QString &mode : {QString("detailed"), QString("miller"), QString("compact")})
            QTest::newRow(qPrintable(mode)) << mode;
    }

    // A big interface font raises the minimum row height. That is shown, but
    // the saved row height stays the user's, so resetting the font brings rows
    // back instead of leaving them at the inflated size for good.
    void testLargeTextDoesNotInflateSavedRowHeights()
    {
        QFETCH(QString, viewMode);
        App app;
        QVERIFY(app.load());
        auto *config = configOf(app);
        QVERIFY(config);
        config->saveSettings({{"fontSize", 10}});
        app.tabModel->activeTab()->navigateTo(app.home.path());
        app.tabModel->activeTab()->setViewMode(viewMode);
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        QTRY_VERIFY(container->property(viewAlias(viewMode)).value<QQuickItem *>());
        auto *view = container->property(viewAlias(viewMode)).value<QQuickItem *>();
        QTRY_VERIFY(savedRowHeight(app, viewMode) > 0);
        const int chosen = savedRowHeight(app, viewMode);
        QCOMPARE(view->property("rowHeight").toInt(), chosen);

        config->saveSettings({{"fontSize", 20}});
        QTRY_VERIFY(view->property("minRowHeight").toInt() > chosen);
        QTRY_COMPARE(view->property("rowHeight").toInt(), view->property("minRowHeight").toInt());
        QCOMPARE(savedRowHeight(app, viewMode), chosen);

        QTest::keyClick(app.window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(config->fontSize(), 0.0);
        QTRY_COMPARE(view->property("rowHeight").toInt(), chosen);
        QCOMPARE(savedRowHeight(app, viewMode), chosen);
    }

    void testCtrlWheelZoomsRowsAndCtrlShiftWheelSizesText_data()
    {
        QTest::addColumn<QString>("viewMode");
        for (const QString &mode : {QString("compact"), QString("detailed"), QString("miller")})
            QTest::newRow(qPrintable(mode)) << mode;
    }

    // As in other file managers, Ctrl+Wheel zooms the view under the pointer
    // (rows grow and shrink in the list views); Ctrl+Shift+Wheel sizes the
    // interface text. Until 11-20 it was the other way round.
    void testCtrlWheelZoomsRowsAndCtrlShiftWheelSizesText()
    {
        QFETCH(QString, viewMode);
        App app;
        QVERIFY(app.load());
        auto *config = configOf(app);
        QVERIFY(config);
        config->saveSettings({{"fontSize", 10}});
        app.tabModel->activeTab()->navigateTo(app.home.path());
        app.tabModel->activeTab()->setViewMode(viewMode);
        auto *container = app.item("primaryFileView");
        QVERIFY(container);
        QTRY_VERIFY(container->property(viewAlias(viewMode)).value<QQuickItem *>());
        auto *view = container->property(viewAlias(viewMode)).value<QQuickItem *>();
        QTRY_VERIFY(view->isVisible());
        QTRY_VERIFY(savedRowHeight(app, viewMode) > 0);
        const int before = savedRowHeight(app, viewMode);
        app.window->grabWindow();
        const QPoint point = viewMode == "miller"
            ? view->mapToScene(QPointF(view->width() * 0.45, 80)).toPoint()
            : view->mapToScene(QPointF(view->width() / 2, 80)).toPoint();

        QWheelEvent rows(point, app.window->mapToGlobal(point), {}, QPoint(0, 120), Qt::NoButton,
                         Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &rows);
        QTRY_COMPARE(savedRowHeight(app, viewMode), before + 2);
        QCOMPARE(view->property("rowHeight").toInt(), before + 2);
        QCOMPARE(config->fontSize(), 10.0);

        QWheelEvent text(point, app.window->mapToGlobal(point), {}, QPoint(0, 120), Qt::NoButton,
                         Qt::ControlModifier | Qt::ShiftModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(app.window, &text);
        QTRY_COMPARE(config->fontSize(), 11.0);
        QCOMPARE(savedRowHeight(app, viewMode), before + 2);
    }

    // ── Split view and Computer ─────────────────────────────────────────

    // Closing the split from the secondary pane hands its folder to the
    // primary pane; the primary pane's old search must not stay on top of it.
    // A different folder already clears it (onCurrentPathChanged), so the
    // case that stuck is the one a new split starts with: both panes on the
    // same folder.
    void testClosingSplitFromSecondaryDropsPrimarySearch()
    {
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        QVERIFY(tab);
        const QString folder = app.home.path() + "/shared";
        QVERIFY(QDir().mkpath(folder));
        tab->navigateTo(folder);
        QTRY_COMPARE(tab->currentPath(), folder);
        QVERIFY(QMetaObject::invokeMethod(app.window, "toggleSplitView"));
        QTRY_VERIFY(tab->splitViewEnabled());
        QCOMPARE(tab->secondaryCurrentPath(), folder);
        const QString secondary = folder;
        QObject *root = app.window;
        QVERIFY(QMetaObject::invokeMethod(root, "setPaneSearchMode", Q_ARG(QVariant, QString("primary")),
                                          Q_ARG(QVariant, true)));
        QVERIFY(root->property("primaryPaneSearchMode").toBool());
        QVERIFY(QMetaObject::invokeMethod(root, "setActivePane", Q_ARG(QVariant, QString("secondary"))));
        QVERIFY(QMetaObject::invokeMethod(root, "toggleSplitView"));
        QTRY_VERIFY(!tab->splitViewEnabled());
        QCOMPARE(tab->currentPath(), secondary);
        QVERIFY(!root->property("primaryPaneSearchMode").toBool());
        QCOMPARE(root->property("activePane").toString(), QString("primary"));
    }

    static QQuickItem *showFakeComputer(App &app, FakeDrives &drives, ComputerModel **modelOut)
    {
        auto *model = qobject_cast<ComputerModel *>(
            app.engine.rootContext()->contextProperty("computerModel").value<QObject *>());
        if (!model) return nullptr;
        model->setDevices(&drives.devices);
        model->refresh();
        app.tabModel->activeTab()->navigateTo("computer:///");
        auto *container = app.item("primaryFileView");
        if (!container) return nullptr;
        if (!QTest::qWaitFor([&] { return container->property("computerViewItem").value<QQuickItem *>(); }))
            return nullptr;
        auto *view = container->property("computerViewItem").value<QQuickItem *>();
        if (!QTest::qWaitFor([&] { return view->isVisible() && model->rowCount() == 4; }))
            return nullptr;
        *modelOut = model;
        return view;
    }
    static int driveIndex(ComputerModel *model, const QString &name)
    {
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->get(i).value("deviceName").toString() == name) return i;
        return -1;
    }
    static bool choose(QQuickItem *view, int index, bool control = false, bool shift = false)
    {
        return QMetaObject::invokeMethod(view, "choose", Q_ARG(QVariant, index), Q_ARG(QVariant, control),
                                         Q_ARG(QVariant, shift));
    }
    static QVariantList selectedDrives(QQuickItem *view)
    {
        return view->property("selectedDevices").toList();
    }

    // Ctrl+Shift+Return needs one mounted drive; an unmounted drive or several
    // drives used to open computer:/// in the split instead of nothing.
    void testOpenInSplitNeedsOneMountedDrive()
    {
        App app;
        const QString mount = app.home.path() + "/data";
        QVERIFY(QDir().mkpath(mount));
        FakeDrives drives(mount);
        QVERIFY(app.load());
        ComputerModel *model = nullptr;
        auto *view = showFakeComputer(app, drives, &model);
        QVERIFY(view);
        auto *tab = app.tabModel->activeTab();
        const int data = driveIndex(model, "Data");
        const int usb = driveIndex(model, "USB A");
        QVERIFY(data >= 0 && usb >= 0);

        QVERIFY(choose(view, usb));
        QTest::keyClick(app.window, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(100);
        QVERIFY(!tab->splitViewEnabled());
        QVERIFY(choose(view, data));
        QVERIFY(choose(view, driveIndex(model, "File System"), true));
        QCOMPARE(selectedDrives(view).size(), 2);
        QTest::keyClick(app.window, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(100);
        QVERIFY(!tab->splitViewEnabled());

        QVERIFY(choose(view, data));
        QTest::keyClick(app.window, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(tab->splitViewEnabled());
        QCOMPARE(tab->secondaryCurrentPath(), mount);
    }

    void testComputerSelectionAnchorsAndModifiedKeys()
    {
        App app;
        const QString mount = app.home.path() + "/data";
        QVERIFY(QDir().mkpath(mount));
        FakeDrives drives(mount);
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        tab->navigateTo(app.home.path());
        QTRY_COMPARE(tab->currentPath(), app.home.path());
        ComputerModel *model = nullptr;
        auto *view = showFakeComputer(app, drives, &model);
        QVERIFY(view);

        // Escape forgets the range anchor along with the selection.
        QVERIFY(choose(view, 1));
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(selectedDrives(view).isEmpty());
        QCOMPARE(view->property("anchorIndex").toInt(), -1);
        QVERIFY(choose(view, 3, false, true));
        QCOMPARE(selectedDrives(view), QVariantList{3});

        // Drives without a device path or mount point have no identity to
        // restore: a refresh must not select every such drive.
        const int usbA = driveIndex(model, "USB A");
        const int usbB = driveIndex(model, "USB B");
        QVERIFY(usbA >= 0 && usbB >= 0);
        QVERIFY(choose(view, usbA));
        model->refresh();
        QTRY_COMPARE(model->rowCount(), 4);
        QVERIFY(!selectedDrives(view).contains(usbB));

        // Modified keys reach the window: history and open-in-new-tab.
        view->forceActiveFocus();
        QTest::keyClick(app.window, Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(tab->currentPath(), app.home.path());
        QTest::keyClick(app.window, Qt::Key_Right, Qt::AltModifier);
        QTRY_COMPARE(tab->currentPath(), QString("computer:///"));
        QTRY_VERIFY(view->isVisible());
        QVERIFY(choose(view, driveIndex(model, "Data")));
        const int tabs = app.tabModel->rowCount();
        QTest::keyClick(app.window, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE(app.tabModel->rowCount(), tabs + 1);
        QTRY_COMPARE(app.tabModel->activeTab()->currentPath(), mount);
    }

    // ── Light and dark ─────────────────────────────────────────────────

    static qreal luminance(const QColor &color)
    {
        return 0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF();
    }

    // The toolbar's sun/moon button flips between the Light and Dark themes
    // chosen in Settings; an open Settings window follows it and does not
    // save the old theme back on its next change.
    void testEveryInterfaceIconHasAStrataVersion()
    {
        // With the Strata icon theme (the default) icons come from the
        // Strata set, in the accent colour. One missing there falls back to
        // a plain text-coloured icon and stands out, as the toolbar's
        // sun/moon and the Projects breadcrumb's rocket did.
        QFile mapping(TEST_SOURCE_DIR "/src/qml/strataicons/IconMapping.js");
        QVERIFY(mapping.open(QIODevice::ReadOnly));
        const QString strata = QString::fromUtf8(mapping.readAll());
        static const QRegularExpression use(
            QStringLiteral(R"re(\b(Icon[A-Z]\w*)\s*\{|iconComponentSource\("(Icon[A-Z]\w*)"\))re"));
        QStringList missing;
        int checked = 0;
        QDirIterator it(TEST_SOURCE_DIR "/src/qml", {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.contains("/qml/icons/") || path.contains("/qml/strataicons/") || path.contains("/qml/Quill/"))
                continue;
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto matches = use.globalMatch(QString::fromUtf8(file.readAll()));
            while (matches.hasNext()) {
                const auto match = matches.next();
                const QString type = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
                // Git status icons keep their own status colours.
                if (type.startsWith("IconGit") || !QFile::exists(TEST_SOURCE_DIR "/src/qml/icons/" + type + ".qml"))
                    continue;
                ++checked;
                if (!strata.contains('"' + type + "\": true"))
                    missing << type + " (" + QFileInfo(path).fileName() + ")";
            }
        }
        missing.removeDuplicates();
        QVERIFY(checked > 50);
        QVERIFY2(missing.isEmpty(), qPrintable("No Strata icon for: " + missing.join(", ")));
    }

    void testToolbarDarkModeToggleUsesThePresets()
    {
        App app;
        QVERIFY(app.load());
        auto *config = configOf(app);
        auto *theme = app.engine.rootContext()->contextProperty("theme").value<ThemeLoader *>();
        QVERIFY(config && theme);
        // main.cpp reloads the theme whenever the config changes; the harness does not.
        QObject::connect(config, &ConfigManager::configChanged, theme, [config, theme] {
            theme->loadTheme(config->theme(), {QStringLiteral(TEST_SOURCE_DIR "/themes")});
        });
        config->saveSettings({{"lightTheme", "rose-pine-dawn"}, {"darkTheme", "rose-pine"}, {"theme", "rose-pine"}});
        QTRY_COMPARE(config->theme(), QString("rose-pine"));
        auto *button = app.item("darkModeButton");
        QVERIFY(button);
        QTRY_VERIFY(button->isVisible());
        QTRY_COMPARE(button->property("tooltip").toString(), QString("Switch to light theme"));
        app.window->grabWindow();
        QTRY_VERIFY(app.center(button).x() > 0);
        // The sun and moon are Strata icons like the rest of the toolbar,
        // drawn in the accent colour; plain ones were text-coloured and
        // stood out (user report).
        QCOMPARE(config->iconTheme(), QString("Strata"));
        const auto toggleIconColor = [button] {
            for (QQuickItem *child : button->childItems())
                if (child->isVisible() && child->property("effectiveColor").isValid())
                    return child->property("effectiveColor").value<QColor>();
            return QColor();
        };
        QTRY_COMPARE(toggleIconColor(), theme->accent());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(button));
        QTRY_COMPARE(config->theme(), QString("rose-pine-dawn"));
        QTRY_VERIFY(luminance(theme->base()) > 0.5);
        QTRY_COMPARE(button->property("tooltip").toString(), QString("Switch to dark theme"));
        QTRY_COMPARE(toggleIconColor(), theme->accent());
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(button));
        QTRY_COMPARE(config->theme(), QString("rose-pine"));
        QTRY_VERIFY(luminance(theme->base()) < 0.5);

        // A theme outside the pair counts by its own background.
        config->saveSettings({{"theme", "dracula"}});
        QTRY_COMPARE(config->theme(), QString("dracula"));
        QTRY_COMPARE(button->property("tooltip").toString(), QString("Switch to light theme"));
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(button));
        QTRY_COMPARE(config->theme(), QString("rose-pine-dawn"));

        // Settings shows the same state and keeps up with the toolbar.
        QVERIFY(QMetaObject::invokeMethod(app.window, "openSettingsPanel"));
        auto *panel = app.window->findChild<QObject *>("settingsPanel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->findChild<QObject *>("darkModeSwitch"));
        auto *darkSwitch = panel->findChild<QObject *>("darkModeSwitch");
        QTRY_VERIFY(!darkSwitch->property("checked").toBool());
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(button));
        QTRY_COMPARE(config->theme(), QString("rose-pine"));
        QTRY_VERIFY(darkSwitch->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(panel, "applySettingsNow"));
        QTest::qWait(50);
        QCOMPARE(config->theme(), QString("rose-pine"));

        // And its own switch still flips the pair.
        darkSwitch->setProperty("checked", false);
        QVERIFY(QMetaObject::invokeMethod(darkSwitch, "toggled", Q_ARG(bool, false)));
        QTRY_COMPARE(config->theme(), QString("rose-pine-dawn"));
        QTRY_VERIFY(!darkSwitch->property("checked").toBool());
        QTRY_COMPARE(button->property("tooltip").toString(), QString("Switch to dark theme"));
    }

    // ── Naming in place ────────────────────────────────────────────────

    static QQuickItem *findVisible(QQuickItem *parent, const QString &name)
    {
        for (QQuickItem *child : parent->childItems()) {
            if (child->objectName() == name && child->isVisible())
                return child;
            if (QQuickItem *hit = findVisible(child, name))
                return hit;
        }
        return nullptr;
    }
    // QTest::keyClicks takes widgets only.
    static void typeText(QWindow *window, const QString &text)
    {
        for (const QChar c : text)
            QTest::keyClick(window, c.toLatin1());
    }
    static bool dialogShown(App &app, const char *property)
    {
        auto *dialog = app.window->property(property).value<QQuickItem *>();
        return dialog && dialog->isVisible();
    }
    static bool openHomeIn(App &app, const QString &viewMode)
    {
        app.tabModel->activeTab()->navigateTo(app.home.path());
        app.tabModel->activeTab()->setViewMode(viewMode);
        auto *baseModel = app.engine.rootContext()->contextProperty("fsModel").value<QObject *>();
        return QTest::qWaitFor([&] {
            return baseModel->property("rootPath").toString() == app.home.path()
                && !baseModel->property("isLoading").toBool();
        });
    }

    void testRenameInPlaceInEveryView_data()
    {
        QTest::addColumn<QString>("viewMode");
        for (const QString &mode : {QString("grid"), QString("compact"), QString("detailed"), QString("miller")})
            QTest::newRow(qPrintable(mode)) << mode;
    }

    // F2 edits the name inside the view instead of opening a dialog.
    void testRenameInPlaceInEveryView()
    {
        QFETCH(QString, viewMode);
        App app;
        const QString notes = app.home.filePath("notes.txt");
        for (const QString &path : {notes, app.home.filePath("other.txt")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(app.load());
        QVERIFY(openHomeIn(app, viewMode));
        QVERIFY(QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                          Q_ARG(QVariant, notes), Q_ARG(QVariant, true)));
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);

        auto *content = app.window->contentItem();
        QTest::keyClick(app.window, Qt::Key_F2);
        QTRY_VERIFY(findVisible(content, "inlineNameInput"));
        auto *input = findVisible(content, "inlineNameInput");
        QTRY_VERIFY(input->hasActiveFocus());
        QCOMPARE(input->property("text").toString(), QString("notes.txt"));
        QCOMPARE(input->property("selectedText").toString(), QString("notes"));
        QVERIFY(!dialogShown(app, "renameDialog"));

        // Editing keys stay in the field: Delete removes text, not the file.
        QTest::keyClick(app.window, Qt::Key_Delete);
        QCOMPARE(input->property("text").toString(), QString(".txt"));
        QVERIFY(QFile::exists(notes));
        // A taken name keeps the field open and says why.
        typeText(app.window, "other");
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_VERIFY(findVisible(content, "inlineNameError"));
        QVERIFY(findVisible(content, "inlineNameInput"));
        QVERIFY(QFile::exists(notes));
        if (qEnvironmentVariableIsSet("HYPRFM_INLINE_GRAB"))
            app.window->grabWindow().save(qEnvironmentVariable("HYPRFM_INLINE_GRAB") + "-" + viewMode + ".png");
        // Escape leaves the name alone.
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(!findVisible(content, "inlineNameInput"));
        QVERIFY(QFile::exists(notes));

        // Enter renames; a space is text, not Quick Preview.
        QTest::keyClick(app.window, Qt::Key_F2);
        QTRY_VERIFY(findVisible(content, "inlineNameInput"));
        QTRY_VERIFY(findVisible(content, "inlineNameInput")->hasActiveFocus());
        typeText(app.window, "my plans");
        QTest::keyClick(app.window, Qt::Key_Return);
        const QString renamed = app.home.filePath("my plans.txt");
        QTRY_VERIFY(QFile::exists(renamed));
        QVERIFY(!QFile::exists(notes));
        QTRY_VERIFY(!findVisible(content, "inlineNameInput"));
        // Keyboard focus is back in the view, not on the hidden field.
        QTRY_VERIFY(app.window->activeFocusItem()
                    && app.window->activeFocusItem()->objectName() != "inlineNameInput");
        auto *quick = app.window->property("quickPreview").value<QObject *>();
        QVERIFY(!quick || !quick->property("active").toBool());
        QTRY_VERIFY([&] {
            QVariant selection;
            QMetaObject::invokeMethod(app.window, "getSelectedPaths", Q_RETURN_ARG(QVariant, selection),
                                      Q_ARG(QVariant, QString("primary")));
            return selection.toStringList() == QStringList{renamed};
        }());
    }

    // Clicking another entry applies the typed name and leaves the
    // selection on what was clicked.
    void testClickingElsewhereAppliesTheName()
    {
        App app;
        const QString first = app.home.filePath("first.txt");
        const QString second = app.home.filePath("second.txt");
        for (const QString &path : {first, second}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(app.load());
        app.window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(app.window));
        QVERIFY(openHomeIn(app, "grid"));
        auto *container = app.item("primaryFileView");
        auto *grid = container->property("gridViewItem").value<QQuickItem *>();
        QVERIFY(grid);
        QVERIFY(QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                          Q_ARG(QVariant, first), Q_ARG(QVariant, true)));
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        auto *content = app.window->contentItem();
        QTest::keyClick(app.window, Qt::Key_F2);
        QTRY_VERIFY(findVisible(content, "inlineNameInput"));
        QTRY_VERIFY(findVisible(content, "inlineNameInput")->hasActiveFocus());
        typeText(app.window, "draft");

        QVariant row;
        QVERIFY(QMetaObject::invokeMethod(grid, "rowForPath", Q_RETURN_ARG(QVariant, row), Q_ARG(QVariant, second)));
        QQuickItem *target = nullptr;
        QVERIFY(QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, target), Q_ARG(int, row.toInt())));
        QVERIFY(target);
        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(target));
        QTRY_VERIFY(QFile::exists(app.home.filePath("draft.txt")));
        QVERIFY(!QFile::exists(first));
        QTRY_VERIFY(!findVisible(content, "inlineNameInput"));
        QTRY_VERIFY([&] {
            QVariant selection;
            QMetaObject::invokeMethod(app.window, "getSelectedPaths", Q_RETURN_ARG(QVariant, selection),
                                      Q_ARG(QVariant, QString("primary")));
            return selection.toStringList() == QStringList{second};
        }());
    }

    void testNewFolderAndFileNamedInPlace_data()
    {
        QTest::addColumn<QString>("viewMode");
        for (const QString &mode : {QString("grid"), QString("compact"), QString("detailed"), QString("miller")})
            QTest::newRow(qPrintable(mode)) << mode;
    }

    // New Folder / New File appear at once under a default name, already
    // being renamed in place. Escape keeps the default name.
    void testNewFolderAndFileNamedInPlace()
    {
        QFETCH(QString, viewMode);
        App app;
        QVERIFY(app.load());
        QVERIFY(openHomeIn(app, viewMode));
        auto *content = app.window->contentItem();
        auto editorReady = [&]() {
            auto *input = findVisible(content, "inlineNameInput");
            return input && input->hasActiveFocus();
        };

        QTest::keyClick(app.window, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(QFileInfo(app.home.filePath("New Folder")).isDir());
        QTRY_VERIFY(editorReady());
        auto *input = findVisible(content, "inlineNameInput");
        QCOMPARE(input->property("text").toString(), QString("New Folder"));
        QCOMPARE(input->property("selectedText").toString(), QString("New Folder"));
        QVERIFY(!dialogShown(app, "newFolderDialog"));
        typeText(app.window, "Projects");
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo(app.home.filePath("Projects")).isDir());
        QVERIFY(!QFile::exists(app.home.filePath("New Folder")));
        QTRY_VERIFY(!findVisible(content, "inlineNameInput"));

        QTest::keyClick(app.window, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(editorReady());
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(!findVisible(content, "inlineNameInput"));
        QVERIFY(QFileInfo(app.home.filePath("New Folder")).isDir());
        // The next default name does not collide with the one kept.
        QTest::keyClick(app.window, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(QFileInfo(app.home.filePath("New Folder 2")).isDir());
        QTRY_VERIFY(editorReady());
        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY(!findVisible(content, "inlineNameInput"));

        QTest::keyClick(app.window, Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(QFileInfo(app.home.filePath("New File")).isFile());
        QTRY_VERIFY(editorReady());
        QVERIFY(!dialogShown(app, "newFileDialog"));
        typeText(app.window, "todo.md");
        QTest::keyClick(app.window, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo(app.home.filePath("todo.md")).isFile());
        QVERIFY(!QFile::exists(app.home.filePath("New File")));
    }

    // ── Favourites ─────────────────────────────────────────────────────

    static void dropUrls(App &app, const QPoint &pos, const QList<QUrl> &urls)
    {
        QMimeData mime;
        mime.setUrls(urls);
        QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(app.window, &enter);
        QDragMoveEvent move(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(app.window, &move);
        QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(app.window, &drop);
    }

    // With no favourites and no split the section used to collapse to 0 px,
    // so nothing could be dropped on it; and a dropped file became a bookmark.
    void testFoldersDropOntoEmptyFavourites()
    {
        App app;
        const QString folder = app.home.path() + "/keep";
        const QString file = app.home.path() + "/note.txt";
        QVERIFY(QDir().mkpath(folder));
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
        }
        QVERIFY(app.load());
        app.bookmarks->setBookmarks({});
        auto *hint = app.item("emptyBookmarksHint");
        QVERIFY(hint);
        QTRY_VERIFY(hint->isVisible());
        QCOMPARE(hint->property("text").toString(), QString("Drop folders here"));
        app.window->grabWindow();
        QTRY_VERIFY(app.center(hint).y() > 0);
        const QPoint target = app.center(hint);

        dropUrls(app, target, {QUrl::fromLocalFile(file)});
        QTest::qWait(50);
        QCOMPARE(app.bookmarks->rowCount(), 0);
        QTRY_VERIFY(hint->isVisible());

        dropUrls(app, target, {QUrl::fromLocalFile(file), QUrl::fromLocalFile(folder)});
        QTRY_COMPARE(app.bookmarks->rowCount(), 1);
        QVERIFY(app.bookmarks->containsPath(folder));
        QTRY_VERIFY(!hint->isVisible());
    }

    static QStringList menuActions(QObject *menu)
    {
        QVariant result;
        QMetaObject::invokeMethod(menu, "buildModel", Q_RETURN_ARG(QVariant, result));
        QStringList actions;
        for (const QVariant &entry : result.toList())
            actions << entry.toMap().value("action").toString();
        return actions;
    }

    // Folders could reach Favourites only by dragging; now also from the
    // right-click menu and with Ctrl+D.
    void testAddFoldersToFavouritesFromMenuAndKeyboard()
    {
        App app;
        const QString folder = app.home.path() + "/projects";
        const QString file = app.home.path() + "/readme.txt";
        QVERIFY(QDir().mkpath(folder));
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
        }
        QVERIFY(app.load());
        app.bookmarks->setBookmarks({});
        app.tabModel->activeTab()->navigateTo(app.home.path());
        auto *baseModel = app.engine.rootContext()->contextProperty("fsModel").value<QObject *>();
        QTRY_COMPARE(baseModel->property("rootPath").toString(), app.home.path());
        QTRY_VERIFY(!baseModel->property("isLoading").toBool());
        auto showMenu = [&](const QString &path, bool isDir) {
            return QMetaObject::invokeMethod(app.window, "showContextMenuForPane", Q_ARG(QVariant, QString("primary")),
                                             Q_ARG(QVariant, path), Q_ARG(QVariant, isDir),
                                             Q_ARG(QVariant, QVariant::fromValue(QPointF(200, 200))));
        };
        auto run = [&](QObject *menu, const QString &action) {
            return QMetaObject::invokeMethod(menu, "executeAction", Q_ARG(QVariant, action),
                                             Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
        };

        QVERIFY(showMenu(folder, true));
        QObject *menu = app.item("contextMenu");
        QVERIFY(menu);
        QVERIFY(menuActions(menu).contains("add_favourite"));
        QVERIFY(run(menu, "add_favourite"));
        QTRY_VERIFY(app.bookmarks->containsPath(folder));
        // Already a favourite, a file, or the trash: no item.
        QVERIFY(showMenu(folder, true));
        QVERIFY(!menuActions(menu).contains("add_favourite"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QVERIFY(showMenu(file, false));
        QVERIFY(!menuActions(menu).contains("add_favourite"));
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        // Empty space offers the folder being shown.
        QVERIFY(showMenu(QString(), true));
        QVERIFY(menuActions(menu).contains("add_favourite"));
        QVERIFY(run(menu, "add_favourite"));
        QTRY_VERIFY(app.bookmarks->containsPath(app.home.path()));
        QCOMPARE(app.bookmarks->rowCount(), 2);

        // Ctrl+D adds the selected folder, once.
        app.bookmarks->setBookmarks({});
        QVERIFY(QMetaObject::invokeMethod(app.window, "focusPathInPane", Q_ARG(QVariant, QString("primary")),
                                          Q_ARG(QVariant, folder), Q_ARG(QVariant, true)));
        QTRY_COMPARE(app.window->property("currentSelectedCount").toInt(), 1);
        QTest::keyClick(app.window, Qt::Key_D, Qt::ControlModifier);
        QTRY_VERIFY(app.bookmarks->containsPath(folder));
        QTest::keyClick(app.window, Qt::Key_D, Qt::ControlModifier);
        QTest::qWait(50);
        QCOMPARE(app.bookmarks->rowCount(), 1);
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"

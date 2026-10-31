// Whole-window harness: everything main.cpp wires up, loading the real
// Main.qml headless. Used for behaviour that only shows with the full item
// tree in place (event routing between overlays, toolbar, views).
#include <QTest>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStyleHints>
#include <algorithm>
#include <functional>
#include <QTemporaryDir>
#include <QWheelEvent>
#include "models/bookmarkmodel.h"
#include "models/devicemodel.h"
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

        bool load()
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

            QQuickStyle::setStyle("Basic");
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

            engine.load(QUrl::fromLocalFile(QStringLiteral(TEST_SOURCE_DIR "/src/qml/Main.qml")));
            if (engine.rootObjects().isEmpty())
                return false;
            window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window)
                return false;
            window->resize(1100, 700);
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

        QQuickItem *focused = app.window->activeFocusItem();
        QVERIFY2(focused, "nothing has keyboard focus after closing the overlay");
        bool insideView = false;
        for (QQuickItem *i = focused; i; i = i->parentItem())
            if (i == view)
                insideView = true;
        QVERIFY2(insideView, qPrintable(QStringLiteral("focus went to %1, not the file view")
                                            .arg(focused->objectName().isEmpty()
                                                 ? QString::fromLatin1(focused->metaObject()->className())
                                                 : focused->objectName())));
    }

    void testHomeQuickAccessClosesSplit_data()
    {
        QTest::addColumn<QString>("orientation");
        QTest::newRow("top-bottom") << QStringLiteral("horizontal");
        QTest::newRow("side-by-side") << QStringLiteral("vertical");
    }

    void testHomeQuickAccessClosesSplit()
    {
        QFETCH(QString, orientation);
        App app;
        QVERIFY(app.load());
        auto *tab = app.tabModel->activeTab();
        QVERIFY(tab);
        const QString other = app.home.path() + "/other";
        QVERIFY(QDir().mkpath(other));
        tab->resetSecondaryTo(other);
        tab->setSplitOrientation(orientation);
        tab->setSplitViewEnabled(true);
        QObject *root = app.window->contentItem()->parent();
        QVERIFY(root);
        QVERIFY(QMetaObject::invokeMethod(root, "setActivePane", Q_ARG(QVariant, QStringLiteral("secondary"))));

        QQuickItem *home = app.item("quickAccess_Home");
        QVERIFY(home);
        QTest::mouseClick(app.window, Qt::LeftButton, {},
                         home->mapToScene(QPointF(home->width() / 2, home->height() / 2)).toPoint());
        QTRY_VERIFY(!tab->splitViewEnabled());
        QCOMPARE(tab->currentPath(), QDir::homePath());
        QCOMPARE(root->property("activePane").toString(), QStringLiteral("primary"));
        QTRY_COMPARE(root->property("splitTransitionProgress").toReal(), 0.0);
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
        if (inputKind == "completion")
            QTest::keyClick(app.window, Qt::Key_Tab);
        QTest::keyClick(app.window, Qt::Key_Return);
        const QString expected = inputKind == "tilde" ? QDir::homePath() : target;
        QTRY_COMPARE(QDir::cleanPath(inputKind == "secondary" ? tab->secondaryCurrentPath() : tab->currentPath()), expected);
        QVERIFY(!bar->property("editMode").toBool());
        if (inputKind == "secondary") {
            QCOMPARE(tab->currentPath(), app.home.path());
            QVERIFY(tab->splitViewEnabled());
        }
    }

    // Only Home itself closes a split. Reaching ~ any other way (breadcrumb,
    // typed path, a terminal link) navigates the active pane and keeps it.
    void testOnlyHomeClosesTheSplit()
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
        QCOMPARE(bar->property("suggestionItems").toList().size(), 3);
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
        app.item("pathInput")->setProperty("text", "~/");
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

        app.sessionState->setGridColumns(4);
        app.sessionState->setRowHeightDetailed(40);
        app.sessionState->setRowHeightMiller(48);
        QTRY_COMPARE(grid->property("columnCount").toInt(), 4);

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

        // Once built, a hidden view keeps following the saved zoom.
        app.sessionState->setRowHeightDetailed(36);
        QTRY_COMPARE(detailed->property("rowHeight").toInt(), 36);

        // A view clamps what it accepts and the mirror writes the clamped
        // value back, so session.json never keeps something unusable.
        app.sessionState->setGridColumns(99);
        QTRY_COMPARE(grid->property("columnCount").toInt(), 12);
        QTRY_COMPARE(app.sessionState->gridColumns(), 12);
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
        app.sessionState->setGridColumns(4);
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
        QVERIFY(slider("iconSizeDetailed") && slider("iconSizeMiller"));

        // Dragging the grid slider one notch right must enlarge the icons,
        // which means one column fewer.
        const int columnsBefore = app.sessionState->gridColumns();
        QVERIFY(QMetaObject::invokeMethod(gridSlider, "moved",
                                          Q_ARG(qreal, gridSlider->property("value").toReal() + 1)));
        QCOMPARE(app.sessionState->gridColumns(), columnsBefore - 1);

        // Zoom changed elsewhere shows up when the page is next shown.
        app.sessionState->setGridColumns(3);
        app.sessionState->setRowHeightDetailed(50);
        app.sessionState->setRowHeightMiller(24);
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 0)));
        QVERIFY(QMetaObject::invokeMethod(panel, "showSection", Q_ARG(QVariant, 1)));

        QObject *reloadedGrid = slider("iconSizeGrid");
        QVERIFY(reloadedGrid);
        QCOMPARE(reloadedGrid->property("value").toInt(),
                 reloadedGrid->property("gridSpan").toInt() - 3);
        QCOMPARE(slider("iconSizeDetailed")->property("value").toInt(), 50);
        QCOMPARE(slider("iconSizeMiller")->property("value").toInt(), 24);
    }

    // Switching view used to be reachable only through Ctrl+1/2/3 or the
    // right-click menu. The toolbar now carries the three modes, and the
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
        QVERIFY(grid && miller && detailed);
        QVERIFY(grid->property("current").toBool());
        QVERIFY(!miller->property("current").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(miller));
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("miller"));
        QTRY_VERIFY(miller->property("current").toBool());
        QVERIFY(!grid->property("current").toBool());

        QTest::mouseClick(app.window, Qt::LeftButton, {}, app.center(detailed));
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("detailed"));

        // The Ctrl+1/2/3 shortcuts still drive the same state, so the toolbar
        // has to follow them too.
        QTest::keyClick(app.window, Qt::Key_1, Qt::ControlModifier);
        QTRY_COMPARE(app.tabModel->activeTab()->viewMode(), QString("grid"));
        QTRY_VERIFY(grid->property("current").toBool());
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

        // Nothing to report hides the meter rather than drawing an empty bar.
        statusBar->setProperty("diskTotal", -1.0);
        statusBar->setProperty("diskFree", -1.0);
        QTRY_VERIFY(!meter->isVisible());
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"

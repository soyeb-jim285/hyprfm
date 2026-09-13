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
        QQmlApplicationEngine engine;
        QObject owner;   // parents everything so teardown order is sane
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

    // Every dropdown in Settings is fed by a list built in
    // syncFromCurrentState(). Miss one and the control renders empty with no
    // error anywhere, which is exactly how the light/dark theme pickers
    // shipped blank.
    void testSettingsDropdownsAreAllPopulated()
    {
        App app;
        QVERIFY(app.load());
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "openPanel"));

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
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "openPanel"));

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
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "openPanel"));

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

    // The Properties dialog is a plain Item overlay, not a Popup, so nothing
    // gave it keyboard focus and Escape fell through to the file view behind
    // it. Opening it and pressing Escape must close it again (issue #28).
    void testEscapeClosesThePropertiesDialog()
    {
        App app;
        QVERIFY(app.load());
        QQuickItem *dialog = app.item("propertiesDialog");
        QVERIFY(dialog);

        QVERIFY(QMetaObject::invokeMethod(dialog, "showProperties",
                                          Q_ARG(QVariant, app.home.path())));
        QTRY_VERIFY(dialog->isVisible());

        QTest::keyClick(app.window, Qt::Key_Escape);
        QTRY_VERIFY2(!dialog->isVisible(), "Escape left the properties dialog open");
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
        QQuickItem *detailed = subView("detailedViewItem");
        QQuickItem *miller = subView("millerViewItem");
        QVERIFY(grid && detailed && miller);

        app.sessionState->setGridColumns(4);
        app.sessionState->setRowHeightDetailed(40);
        app.sessionState->setRowHeightMiller(48);

        QTRY_COMPARE(grid->property("columnCount").toInt(), 4);
        QTRY_COMPARE(detailed->property("rowHeight").toInt(), 40);
        QTRY_COMPARE(miller->property("rowHeight").toInt(), 48);

        // A view clamps what it accepts and the mirror writes the clamped
        // value back, so session.json never keeps something unusable.
        app.sessionState->setGridColumns(99);
        QTRY_COMPARE(grid->property("columnCount").toInt(), 12);
        QTRY_COMPARE(app.sessionState->gridColumns(), 12);
    }

    // The Icon Size sliders sit on the Layout page (section 1), which lives in
    // a Loader. Dragging one has to move the zoom, and coming back to the page
    // has to show whatever the zoom is now — including a Ctrl+wheel made while
    // the panel was on another page.
    void testSettingsIconSizeSlidersReadAndWriteTheZoom()
    {
        App app;
        QVERIFY(app.load());
        QObject *panel = app.window->findChild<QObject *>(QStringLiteral("settingsPanel"));
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "openPanel"));
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

    // The status bar reports free space as a Dolphin-style meter: the bar
    // fills with what is used and the label sits on it. The fill has to track
    // the ratio and step through accent/warning/error at 75% and 90%, since a
    // meter nobody can read at a glance is just a coloured box.
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

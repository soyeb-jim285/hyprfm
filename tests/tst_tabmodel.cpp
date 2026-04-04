#include <QTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include <QFileInfo>
#include "models/tabmodel.h"
#include "models/tablistmodel.h"

class TestTabModel : public QObject
{
    Q_OBJECT

private slots:
    // === TabModel tests ===

    void testTabInitialState()
    {
        TabModel tab;
        QCOMPARE(tab.currentPath(), QDir::homePath());
        QCOMPARE(tab.viewMode(), QString("grid"));
        QCOMPARE(tab.canGoBack(), false);
        QCOMPARE(tab.canGoForward(), false);
        QCOMPARE(tab.sortBy(), QString("name"));
        QCOMPARE(tab.sortAscending(), true);
    }

    void testNavigate()
    {
        TabModel tab;
        QSignalSpy spy(&tab, &TabModel::currentPathChanged);

        tab.navigateTo("/tmp");

        QCOMPARE(tab.currentPath(), QString("/tmp"));
        QCOMPARE(tab.canGoBack(), true);
        QCOMPARE(tab.canGoForward(), false);
        QCOMPARE(spy.count(), 1);
    }

    void testNavigateMultiple()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");
        tab.navigateTo("/var");

        QCOMPARE(tab.currentPath(), QString("/var"));
        QCOMPARE(tab.canGoBack(), true);
    }

    void testBackForward()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");

        QSignalSpy historySpy(&tab, &TabModel::historyChanged);

        tab.goBack();
        QCOMPARE(tab.currentPath(), QString("/tmp"));
        QCOMPARE(tab.canGoForward(), true);
        QVERIFY(historySpy.count() >= 1);

        tab.goForward();
        QCOMPARE(tab.currentPath(), QString("/usr"));
    }

    void testSplitViewState()
    {
        TabModel tab;

        QCOMPARE(tab.splitViewEnabled(), false);
        QCOMPARE(tab.secondaryCurrentPath(), QDir::homePath());
        QCOMPARE(tab.secondaryCanGoBack(), false);
        QCOMPARE(tab.secondaryCanGoForward(), false);

        tab.setSplitViewEnabled(true);
        QCOMPARE(tab.splitViewEnabled(), true);
        QCOMPARE(tab.secondaryCurrentPath(), tab.currentPath());
    }

    void testSecondaryNavigation()
    {
        TabModel tab;
        tab.setSplitViewEnabled(true);

        QSignalSpy pathSpy(&tab, &TabModel::secondaryCurrentPathChanged);

        tab.navigateSecondaryTo("/tmp");
        QCOMPARE(tab.secondaryCurrentPath(), QString("/tmp"));
        QCOMPARE(tab.secondaryCanGoBack(), true);
        QCOMPARE(tab.secondaryCanGoForward(), false);
        QCOMPARE(pathSpy.count(), 1);

        tab.navigateSecondaryTo("/usr");
        tab.secondaryGoBack();
        QCOMPARE(tab.secondaryCurrentPath(), QString("/tmp"));
        QCOMPARE(tab.secondaryCanGoForward(), true);

        tab.secondaryGoForward();
        QCOMPARE(tab.secondaryCurrentPath(), QString("/usr"));
    }

    void testNavigateClearsForwardHistory()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");
        tab.goBack();
        QCOMPARE(tab.canGoForward(), true);

        tab.navigateTo("/var");
        QCOMPARE(tab.canGoForward(), false);
    }

    void testGoBackAtStart()
    {
        TabModel tab;
        // At start, back should be no-op
        tab.goBack();
        QCOMPARE(tab.currentPath(), QDir::homePath());
    }

    void testGoForwardAtEnd()
    {
        TabModel tab;
        // At end, forward should be no-op
        tab.goForward();
        QCOMPARE(tab.currentPath(), QDir::homePath());
    }

    void testGoUp()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.goUp();
        QCOMPARE(tab.currentPath(), QString("/"));
    }

    void testGoUpAtRoot()
    {
        TabModel tab;
        tab.navigateTo("/");
        tab.goUp();
        // Should stay at root, not crash
        QCOMPARE(tab.currentPath(), QString("/"));
    }

    void testViewMode()
    {
        TabModel tab;
        QSignalSpy spy(&tab, &TabModel::viewModeChanged);

        tab.setViewMode("list");
        QCOMPARE(tab.viewMode(), QString("list"));
        QCOMPARE(spy.count(), 1);

        tab.setViewMode("detailed");
        QCOMPARE(tab.viewMode(), QString("detailed"));
        QCOMPARE(spy.count(), 2);

        tab.setViewMode("grid");
        QCOMPARE(tab.viewMode(), QString("grid"));
    }

    void testSortProperties()
    {
        TabModel tab;
        QSignalSpy spy(&tab, &TabModel::sortChanged);

        tab.setSortBy("size");
        QCOMPARE(tab.sortBy(), QString("size"));
        QVERIFY(spy.count() >= 1);

        tab.setSortAscending(false);
        QCOMPARE(tab.sortAscending(), false);
    }

    void testTabTitle()
    {
        TabModel tab;
        QCOMPARE(tab.title(), QDir::home().dirName());

        tab.navigateTo("/tmp");
        QCOMPARE(tab.title(), QString("tmp"));

        tab.navigateTo("/");
        QCOMPARE(tab.title(), QString("/"));

        tab.navigateTo("/usr/local/bin");
        QCOMPARE(tab.title(), QString("bin"));
    }

    // === TabListModel tests ===

    void testTabListModelInitialState()
    {
        TabListModel model;
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.activeIndex(), 0);
        QVERIFY(model.activeTab() != nullptr);
    }

    void testTabListModelConsistency()
    {
        TabListModel model;
        auto *tester = new QAbstractItemModelTester(&model,
            QAbstractItemModelTester::FailureReportingMode::QtTest);
        Q_UNUSED(tester)

        model.addTab();
        model.addTab();
        model.closeTab(1);
        model.addTab();
    }

    void testTabListModelAddRemove()
    {
        TabListModel model;
        QSignalSpy countSpy(&model, &TabListModel::countChanged);

        model.addTab();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.activeIndex(), 1); // New tab becomes active
        QVERIFY(countSpy.count() >= 1);

        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.activeIndex(), 0);
    }

    void testTabListModelCannotCloseLastTab()
    {
        TabListModel model;
        QSignalSpy lastTabSpy(&model, &TabListModel::lastTabClosed);

        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1); // Still one tab
    }

    void testTabListModelActiveIndex()
    {
        TabListModel model;
        model.addTab(); // idx 1
        model.addTab(); // idx 2

        QSignalSpy spy(&model, &TabListModel::activeIndexChanged);

        model.setActiveIndex(0);
        QCOMPARE(model.activeIndex(), 0);
        QVERIFY(spy.count() >= 1);
    }

    void testReopenClosedTab()
    {
        TabListModel model;
        model.activeTab()->navigateTo("/tmp");
        model.activeTab()->setSplitViewEnabled(true);
        model.activeTab()->navigateSecondaryTo("/usr");
        model.addTab();
        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1);

        model.reopenClosedTab();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.tabAt(1)->currentPath(), QString("/tmp"));
        QCOMPARE(model.tabAt(1)->secondaryCurrentPath(), QString("/usr"));
        QCOMPARE(model.tabAt(1)->splitViewEnabled(), true);
    }

    void testReopenMultipleClosedTabs()
    {
        TabListModel model;
        model.activeTab()->navigateTo("/tmp");
        model.addTab();
        model.activeTab()->navigateTo("/usr");
        model.addTab();

        // Close tabs (LIFO order matters)
        model.closeTab(1); // /usr
        model.closeTab(0); // /tmp

        model.reopenClosedTab();
        // Most recently closed should come back first
        QCOMPARE(model.rowCount(), 2);
    }

    void testReopenWhenNothingClosed()
    {
        TabListModel model;
        // Should be no-op
        model.reopenClosedTab();
        QCOMPARE(model.rowCount(), 1);
    }

    void testTabListModelRoles()
    {
        TabListModel model;
        model.activeTab()->navigateTo("/tmp");

        QModelIndex idx = model.index(0);
        QCOMPARE(model.data(idx, TabListModel::TitleRole).toString(), QString("tmp"));
        QCOMPARE(model.data(idx, TabListModel::PathRole).toString(), QString("/tmp"));
        QVERIFY(model.data(idx, TabListModel::TabObjectRole).value<TabModel*>() != nullptr);
    }

    void testCloseTabAdjustsActiveIndex()
    {
        TabListModel model;
        model.addTab();
        model.addTab();
        model.setActiveIndex(2);
        QCOMPARE(model.activeIndex(), 2);

        // Close tab before active
        model.closeTab(0);
        // Active index should adjust down
        QCOMPARE(model.activeIndex(), 1);
    }

    void testCloseActiveTabSelectsPrevious()
    {
        TabListModel model;
        model.addTab();
        model.addTab();
        model.setActiveIndex(1);

        model.closeTab(1);
        QVERIFY(model.activeIndex() >= 0);
        QVERIFY(model.activeIndex() < model.rowCount());
    }

    void testTabListModelSessionChanged()
    {
        TabListModel model;
        QSignalSpy sessionSpy(&model, &TabListModel::sessionChanged);

        model.activeTab()->navigateTo("/tmp");
        QVERIFY(sessionSpy.count() >= 1);

        sessionSpy.clear();
        model.addTab();
        QVERIFY(sessionSpy.count() >= 1);
    }

    void testRestoreSessionFallsBackToExistingPath()
    {
        TabListModel model;

        QJsonArray tabs;
        QJsonObject tab;
        tab["path"] = "/definitely/missing/path/for/hyprfm";
        tab["viewMode"] = "grid";
        tab["splitViewEnabled"] = false;
        tab["secondaryPath"] = "/another/missing/path";
        tab["sortBy"] = "name";
        tab["sortAscending"] = true;
        tabs.append(tab);

        model.restoreSession(tabs, 0);

        QVERIFY(model.activeTab() != nullptr);
        QVERIFY(QFileInfo::exists(model.activeTab()->currentPath()));
        QVERIFY(QFileInfo::exists(model.activeTab()->secondaryCurrentPath()));
    }
};

QTEST_MAIN(TestTabModel)
#include "tst_tabmodel.moc"

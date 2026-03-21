#include <QTest>
#include "models/tabmodel.h"
#include "models/tablistmodel.h"

class TestTabModel : public QObject
{
    Q_OBJECT

private slots:
    void testTabInitialState()
    {
        TabModel tab;
        QCOMPARE(tab.currentPath(), QDir::homePath());
        QCOMPARE(tab.viewMode(), QString("grid"));
        QCOMPARE(tab.canGoBack(), false);
        QCOMPARE(tab.canGoForward(), false);
    }

    void testNavigate()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        QCOMPARE(tab.currentPath(), QString("/tmp"));
        QCOMPARE(tab.canGoBack(), true);
        QCOMPARE(tab.canGoForward(), false);
    }

    void testBackForward()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");
        tab.goBack();
        QCOMPARE(tab.currentPath(), QString("/tmp"));
        QCOMPARE(tab.canGoForward(), true);
        tab.goForward();
        QCOMPARE(tab.currentPath(), QString("/usr"));
    }

    void testNavigateClearsForwardHistory()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");
        tab.goBack();
        tab.navigateTo("/var");
        QCOMPARE(tab.canGoForward(), false);
    }

    void testViewMode()
    {
        TabModel tab;
        tab.setViewMode("list");
        QCOMPARE(tab.viewMode(), QString("list"));
        tab.setViewMode("detailed");
        QCOMPARE(tab.viewMode(), QString("detailed"));
    }

    void testTabListModelAddRemove()
    {
        TabListModel model;
        QCOMPARE(model.rowCount(), 1); // Starts with one tab

        model.addTab();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.activeIndex(), 1); // New tab becomes active

        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.activeIndex(), 0);
    }

    void testTabListModelCannotCloseLastTab()
    {
        TabListModel model;
        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1); // Still one tab -- closing last closes window
    }

    void testReopenClosedTab()
    {
        TabListModel model;
        model.activeTab()->navigateTo("/tmp");
        model.addTab();
        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1);

        model.reopenClosedTab();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.tabAt(1)->currentPath(), QString("/tmp"));
    }

    void testTabTitle()
    {
        TabModel tab;
        QCOMPARE(tab.title(), QDir::home().dirName());
        tab.navigateTo("/tmp");
        QCOMPARE(tab.title(), QString("tmp"));
        tab.navigateTo("/");
        QCOMPARE(tab.title(), QString("/"));
    }
};

QTEST_MAIN(TestTabModel)
#include "tst_tabmodel.moc"

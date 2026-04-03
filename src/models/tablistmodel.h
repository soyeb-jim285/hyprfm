#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QJsonArray>
#include "models/tabmodel.h"

class TabListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex NOTIFY activeIndexChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(TabModel* activeTab READ activeTab NOTIFY activeIndexChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        TabObjectRole,
    };

    explicit TabListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeIndex() const;
    void setActiveIndex(int index);

    TabModel *activeTab() const;
    TabModel *tabAt(int index) const;

    Q_INVOKABLE void addTab();
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void reopenClosedTab();

    QJsonArray saveSession() const;
    void restoreSession(const QJsonArray &tabs, int activeIdx);

signals:
    void activeIndexChanged();
    void countChanged();
    void lastTabClosed();

private:
    void connectTab(int row, TabModel *tab);
    QList<TabModel *> m_tabs;
    int m_activeIndex = 0;

    struct ClosedTabInfo {
        QString path;
        QString viewMode;
        QString secondaryPath;
        QString sortBy;
        bool sortAscending = true;
        bool splitViewEnabled = false;
    };
    QList<ClosedTabInfo> m_closedTabs;
};

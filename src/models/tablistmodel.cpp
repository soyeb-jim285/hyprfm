#include "models/tablistmodel.h"
#include <QDir>
#include <QJsonObject>
#include <QFileInfo>
#include <QUrl>

namespace {

QString normalizedSessionPath(const QString &path)
{
    if (path.isEmpty())
        return QDir::homePath();

    const QUrl url(path);
    if (url.scheme() == QStringLiteral("trash"))
        return path;
    if (url.isValid() && !url.scheme().isEmpty() && url.scheme() != QStringLiteral("file"))
        return path;

    QFileInfo info(path);
    QString candidate = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    while (!candidate.isEmpty() && !QFileInfo::exists(candidate)) {
        const QString parent = QFileInfo(candidate).absolutePath();
        if (parent == candidate)
            break;
        candidate = parent;
    }

    return QFileInfo::exists(candidate) ? QDir(candidate).absolutePath() : QDir::homePath();
}

}

TabListModel::TabListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    auto *tab = new TabModel(this);
    m_tabs.append(tab);
    connectTab(0, tab);
}

void TabListModel::connectTab(int row, TabModel *tab)
{
    connect(tab, &TabModel::currentPathChanged, this, [this, tab]() {
        int idx = m_tabs.indexOf(tab);
        if (idx >= 0) {
            QModelIndex mi = index(idx);
            emit dataChanged(mi, mi, {PathRole});
        }
        emit sessionChanged();
    });
    connect(tab, &TabModel::titleChanged, this, [this, tab]() {
        int idx = m_tabs.indexOf(tab);
        if (idx >= 0) {
            QModelIndex mi = index(idx);
            emit dataChanged(mi, mi, {TitleRole});
        }
    });
    connect(tab, &TabModel::secondaryCurrentPathChanged, this, &TabListModel::sessionChanged);
    connect(tab, &TabModel::viewModeChanged, this, &TabListModel::sessionChanged);
    connect(tab, &TabModel::splitViewEnabledChanged, this, &TabListModel::sessionChanged);
    connect(tab, &TabModel::splitOrientationChanged, this, &TabListModel::sessionChanged);
    connect(tab, &TabModel::sortChanged, this, &TabListModel::sessionChanged);
}

int TabListModel::rowCount(const QModelIndex &) const
{
    return m_tabs.size();
}

QVariant TabListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tabs.size())
        return {};

    TabModel *tab = m_tabs.at(index.row());
    switch (role) {
    case TitleRole: return tab->title();
    case PathRole: return tab->currentPath();
    case TabObjectRole: return QVariant::fromValue(tab);
    }
    return {};
}

QHash<int, QByteArray> TabListModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {PathRole, "path"},
        {TabObjectRole, "tabObject"},
    };
}

int TabListModel::activeIndex() const { return m_activeIndex; }

void TabListModel::setActiveIndex(int index)
{
    if (index >= 0 && index < m_tabs.size() && m_activeIndex != index) {
        m_activeIndex = index;
        emit activeIndexChanged();
        emit sessionChanged();
    }
}

TabModel *TabListModel::activeTab() const
{
    if (m_activeIndex >= 0 && m_activeIndex < m_tabs.size())
        return m_tabs.at(m_activeIndex);
    return nullptr;
}

TabModel *TabListModel::tabAt(int index) const
{
    if (index >= 0 && index < m_tabs.size())
        return m_tabs.at(index);
    return nullptr;
}

void TabListModel::setDefaultViewMode(const QString &mode)
{
    const QString normalized = TabModel::normalizeViewMode(mode);
    if (m_defaultViewMode == normalized)
        return;
    m_defaultViewMode = normalized;
    // Apply to tabs still on the built-in default (startup tab before session restore)
    for (TabModel *tab : m_tabs) {
        if (tab->viewMode() == QStringLiteral("grid"))
            tab->setViewMode(normalized);
    }
}

void TabListModel::openPath(const QString &path)
{
    const QString wanted = QDir::cleanPath(path);
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (!m_tabs.at(i)->splitViewEnabled()
            && QDir::cleanPath(m_tabs.at(i)->currentPath()) == wanted) {
            setActiveIndex(i);
            return;
        }
    }
    addTab();
    if (auto *tab = activeTab())
        tab->navigateTo(wanted);
}

void TabListModel::moveTab(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_tabs.size() || to >= m_tabs.size())
        return;
    // beginMoveRows wants the destination as "before this row" in the
    // pre-move numbering; moving down means one past the target.
    const int destinationRow = to > from ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destinationRow))
        return;
    m_tabs.move(from, to);
    endMoveRows();

    if (m_activeIndex == from)
        m_activeIndex = to;
    else if (from < m_activeIndex && to >= m_activeIndex)
        --m_activeIndex;
    else if (from > m_activeIndex && to <= m_activeIndex)
        ++m_activeIndex;
    emit activeIndexChanged();
    emit sessionChanged();
}

void TabListModel::addTab()
{
    beginInsertRows(QModelIndex(), m_tabs.size(), m_tabs.size());
    auto *tab = new TabModel(this);
    // A new tab continues what you were just doing: it inherits the current
    // tab's view, falling back to the configured default when there is none.
    const TabModel *current = activeTab();
    tab->setViewMode(current ? current->viewMode() : m_defaultViewMode);
    m_tabs.append(tab);
    connectTab(m_tabs.size() - 1, tab);
    endInsertRows();
    setActiveIndex(m_tabs.size() - 1);
    emit countChanged();
    emit sessionChanged();
}

void TabListModel::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;

    if (m_tabs.size() <= 1) {
        emit lastTabClosed();
        return;
    }

    TabModel *tab = m_tabs.at(index);
    m_closedTabs.append({
        tab->currentPath(),
        tab->viewMode(),
        tab->secondaryCurrentPath(),
        tab->sortBy(),
        tab->sortAscending(),
        tab->splitViewEnabled(),
        tab->splitOrientation(),
    });

    beginRemoveRows(QModelIndex(), index, index);
    m_tabs.removeAt(index);
    tab->deleteLater();
    endRemoveRows();

    if (m_activeIndex >= m_tabs.size())
        setActiveIndex(m_tabs.size() - 1);
    else if (m_activeIndex == index && m_activeIndex > 0)
        setActiveIndex(m_activeIndex - 1);
    else
        emit activeIndexChanged();

    emit countChanged();
    emit sessionChanged();
}

void TabListModel::reopenClosedTab()
{
    if (m_closedTabs.isEmpty())
        return;

    auto info = m_closedTabs.takeLast();

    beginInsertRows(QModelIndex(), m_tabs.size(), m_tabs.size());
    auto *tab = new TabModel(this);
    tab->navigateTo(info.path);
    tab->setViewMode(info.viewMode);
    tab->setSecondaryCurrentPath(info.secondaryPath);
    tab->setSortBy(info.sortBy);
    tab->setSortAscending(info.sortAscending);
    tab->setSplitViewEnabled(info.splitViewEnabled);
    tab->setSplitOrientation(info.splitOrientation);
    m_tabs.append(tab);
    connectTab(m_tabs.size() - 1, tab);
    endInsertRows();
    setActiveIndex(m_tabs.size() - 1);
    emit countChanged();
    emit sessionChanged();
}

QJsonArray TabListModel::saveSession() const
{
    QJsonArray arr;
    for (const auto *tab : m_tabs) {
        arr.append(QJsonObject{
            {"path", tab->currentPath()},
            {"viewMode", tab->viewMode()},
            {"splitViewEnabled", tab->splitViewEnabled()},
            {"splitOrientation", tab->splitOrientation()},
            {"secondaryPath", tab->secondaryCurrentPath()},
            {"sortBy", tab->sortBy()},
            {"sortAscending", tab->sortAscending()},
        });
    }
    return arr;
}

void TabListModel::restoreSession(const QJsonArray &tabs, int activeIdx)
{
    if (tabs.isEmpty())
        return;

    beginResetModel();
    qDeleteAll(m_tabs);
    m_tabs.clear();

    for (const auto &val : tabs) {
        QJsonObject obj = val.toObject();
        auto *tab = new TabModel(this);
        tab->navigateTo(normalizedSessionPath(obj.value("path").toString()));
        tab->setViewMode(obj.value("viewMode").toString(m_defaultViewMode));
        tab->setSplitViewEnabled(obj.value("splitViewEnabled").toBool(false));
        tab->setSplitOrientation(obj.value("splitOrientation").toString());
        tab->setSecondaryCurrentPath(normalizedSessionPath(obj.value("secondaryPath").toString(tab->currentPath())));
        tab->setSortBy(obj.value("sortBy").toString("name"));
        tab->setSortAscending(obj.value("sortAscending").toBool(true));
        m_tabs.append(tab);
        connectTab(m_tabs.size() - 1, tab);
    }
    endResetModel();

    m_activeIndex = qBound(0, activeIdx, m_tabs.size() - 1);
    emit activeIndexChanged();
    emit countChanged();
    emit sessionChanged();
}

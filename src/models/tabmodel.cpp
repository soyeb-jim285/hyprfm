#include "models/tabmodel.h"
#include <QFileInfo>

TabModel::TabModel(QObject *parent)
    : QObject(parent)
    , m_currentPath(QDir::homePath())
    , m_secondaryCurrentPath(QDir::homePath())
    , m_viewMode("grid")
    , m_sortBy("name")
    , m_sortAscending(true)
{
}

QString TabModel::currentPath() const { return m_currentPath; }

QString TabModel::title() const
{
    if (m_currentPath == "/")
        return "/";
    QDir dir(m_currentPath);
    return dir.dirName();
}

QString TabModel::viewMode() const { return m_viewMode; }
bool TabModel::canGoBack() const { return !m_backStack.isEmpty(); }
bool TabModel::canGoForward() const { return !m_forwardStack.isEmpty(); }
bool TabModel::splitViewEnabled() const { return m_splitViewEnabled; }
QString TabModel::secondaryCurrentPath() const { return m_secondaryCurrentPath; }
bool TabModel::secondaryCanGoBack() const { return !m_secondaryBackStack.isEmpty(); }
bool TabModel::secondaryCanGoForward() const { return !m_secondaryForwardStack.isEmpty(); }
QString TabModel::sortBy() const { return m_sortBy; }
bool TabModel::sortAscending() const { return m_sortAscending; }

void TabModel::setViewMode(const QString &mode)
{
    if (m_viewMode != mode) {
        m_viewMode = mode;
        emit viewModeChanged();
    }
}

void TabModel::setSplitViewEnabled(bool enabled)
{
    if (m_splitViewEnabled == enabled)
        return;

    if (enabled && !m_secondaryInitialized) {
        m_secondaryCurrentPath = m_currentPath;
        m_secondaryInitialized = true;
        emit secondaryCurrentPathChanged();
    }

    m_splitViewEnabled = enabled;
    emit splitViewEnabledChanged();
}

void TabModel::setSecondaryCurrentPath(const QString &path)
{
    if (path.isEmpty() || m_secondaryCurrentPath == path)
        return;

    m_secondaryCurrentPath = path;
    m_secondaryInitialized = true;
    emit secondaryCurrentPathChanged();
}

void TabModel::setSortBy(const QString &column)
{
    if (m_sortBy != column) {
        m_sortBy = column;
        emit sortChanged();
    }
}

void TabModel::setSortAscending(bool ascending)
{
    if (m_sortAscending != ascending) {
        m_sortAscending = ascending;
        emit sortChanged();
    }
}

void TabModel::navigateTo(const QString &path)
{
    if (path == m_currentPath)
        return;
    m_backStack.append(m_currentPath);
    m_forwardStack.clear();
    m_currentPath = path;
    emit currentPathChanged();
    emit historyChanged();
}

void TabModel::navigateSecondaryTo(const QString &path)
{
    if (path == m_secondaryCurrentPath || path.isEmpty())
        return;

    m_secondaryBackStack.append(m_secondaryCurrentPath);
    m_secondaryForwardStack.clear();
    m_secondaryCurrentPath = path;
    m_secondaryInitialized = true;
    emit secondaryCurrentPathChanged();
    emit secondaryHistoryChanged();
}

void TabModel::goBack()
{
    if (m_backStack.isEmpty())
        return;
    m_forwardStack.append(m_currentPath);
    m_currentPath = m_backStack.takeLast();
    emit currentPathChanged();
    emit historyChanged();
}

void TabModel::secondaryGoBack()
{
    if (m_secondaryBackStack.isEmpty())
        return;

    m_secondaryForwardStack.append(m_secondaryCurrentPath);
    m_secondaryCurrentPath = m_secondaryBackStack.takeLast();
    emit secondaryCurrentPathChanged();
    emit secondaryHistoryChanged();
}

void TabModel::goForward()
{
    if (m_forwardStack.isEmpty())
        return;
    m_backStack.append(m_currentPath);
    m_currentPath = m_forwardStack.takeLast();
    emit currentPathChanged();
    emit historyChanged();
}

void TabModel::secondaryGoForward()
{
    if (m_secondaryForwardStack.isEmpty())
        return;

    m_secondaryBackStack.append(m_secondaryCurrentPath);
    m_secondaryCurrentPath = m_secondaryForwardStack.takeLast();
    emit secondaryCurrentPathChanged();
    emit secondaryHistoryChanged();
}

void TabModel::goUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

void TabModel::secondaryGoUp()
{
    QDir dir(m_secondaryCurrentPath);
    if (dir.cdUp())
        navigateSecondaryTo(dir.absolutePath());
}

void TabModel::resetSecondaryTo(const QString &path)
{
    if (path.isEmpty())
        return;

    const bool pathChanged = m_secondaryCurrentPath != path;
    const bool historyChanged = !m_secondaryBackStack.isEmpty() || !m_secondaryForwardStack.isEmpty();

    m_secondaryBackStack.clear();
    m_secondaryForwardStack.clear();
    m_secondaryCurrentPath = path;
    m_secondaryInitialized = true;

    if (pathChanged)
        emit secondaryCurrentPathChanged();
    if (historyChanged)
        emit secondaryHistoryChanged();
}

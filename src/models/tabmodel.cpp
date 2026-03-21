#include "models/tabmodel.h"
#include <QFileInfo>

TabModel::TabModel(QObject *parent)
    : QObject(parent)
    , m_currentPath(QDir::homePath())
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
QString TabModel::sortBy() const { return m_sortBy; }
bool TabModel::sortAscending() const { return m_sortAscending; }

void TabModel::setViewMode(const QString &mode)
{
    if (m_viewMode != mode) {
        m_viewMode = mode;
        emit viewModeChanged();
    }
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

void TabModel::goBack()
{
    if (m_backStack.isEmpty())
        return;
    m_forwardStack.append(m_currentPath);
    m_currentPath = m_backStack.takeLast();
    emit currentPathChanged();
    emit historyChanged();
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

void TabModel::goUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

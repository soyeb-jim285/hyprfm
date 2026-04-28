#include "models/tabmodel.h"
#include <QFileInfo>
#include <QUrl>

namespace {

bool isRemoteUri(const QString &path)
{
    const QUrl url(path);
    return url.isValid() && !url.scheme().isEmpty()
        && url.scheme() != QStringLiteral("file")
        && url.scheme() != QStringLiteral("trash");
}

QString displayNameForPath(const QString &path)
{
    const QUrl url(path);
    if (url.scheme() == QStringLiteral("trash")) {
        const QString fileName = url.fileName();
        return fileName.isEmpty() ? QStringLiteral("Trash") : fileName;
    }

    if (isRemoteUri(path)) {
        const QString fileName = QUrl::fromPercentEncoding(url.fileName().toUtf8());
        if (!fileName.isEmpty())
            return fileName;
        if (!url.host().isEmpty())
            return url.host();
        return url.scheme().toUpper();
    }

    if (path == QStringLiteral("/"))
        return QStringLiteral("/");

    const QDir dir(path);
    return dir.dirName();
}

QString parentLocation(const QString &path)
{
    const QUrl url(path);
    if (url.scheme() == QStringLiteral("trash")) {
        QString current = path;
        if (current.size() > 9 && current.endsWith('/'))
            current.chop(1);
        if (current == QStringLiteral("trash:///") || current == QStringLiteral("trash://"))
            return QStringLiteral("trash:///");
        const int slashIndex = current.lastIndexOf('/');
        return slashIndex <= 8 ? QStringLiteral("trash:///") : current.left(slashIndex);
    }

    if (isRemoteUri(path)) {
        QUrl parentUrl(url);
        QString urlPath = parentUrl.path();
        if (urlPath.isEmpty() || urlPath == QStringLiteral("/"))
            return path;
        if (urlPath.endsWith('/'))
            urlPath.chop(1);
        const int slashIndex = urlPath.lastIndexOf('/');
        parentUrl.setPath(slashIndex <= 0 ? QStringLiteral("/") : urlPath.left(slashIndex));
        return parentUrl.toString(QUrl::FullyEncoded);
    }

    QDir dir(path);
    if (dir.cdUp())
        return dir.absolutePath();
    return path;
}

}

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
    const QString primaryTitle = displayNameForPath(m_currentPath);
    if (!m_splitViewEnabled)
        return primaryTitle;

    return primaryTitle + QStringLiteral(" / ") + displayNameForPath(m_secondaryCurrentPath);
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
    emit titleChanged();
}

void TabModel::setSecondaryCurrentPath(const QString &path)
{
    if (path.isEmpty() || m_secondaryCurrentPath == path)
        return;

    m_secondaryCurrentPath = path;
    m_secondaryInitialized = true;
    emit secondaryCurrentPathChanged();
    emit titleChanged();
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
    emit titleChanged();
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
    emit titleChanged();
    emit secondaryHistoryChanged();
}

void TabModel::goBack()
{
    if (m_backStack.isEmpty())
        return;
    m_forwardStack.append(m_currentPath);
    m_currentPath = m_backStack.takeLast();
    emit currentPathChanged();
    emit titleChanged();
    emit historyChanged();
}

void TabModel::secondaryGoBack()
{
    if (m_secondaryBackStack.isEmpty())
        return;

    m_secondaryForwardStack.append(m_secondaryCurrentPath);
    m_secondaryCurrentPath = m_secondaryBackStack.takeLast();
    emit secondaryCurrentPathChanged();
    emit titleChanged();
    emit secondaryHistoryChanged();
}

void TabModel::goForward()
{
    if (m_forwardStack.isEmpty())
        return;
    m_backStack.append(m_currentPath);
    m_currentPath = m_forwardStack.takeLast();
    emit currentPathChanged();
    emit titleChanged();
    emit historyChanged();
}

void TabModel::secondaryGoForward()
{
    if (m_secondaryForwardStack.isEmpty())
        return;

    m_secondaryBackStack.append(m_secondaryCurrentPath);
    m_secondaryCurrentPath = m_secondaryForwardStack.takeLast();
    emit secondaryCurrentPathChanged();
    emit titleChanged();
    emit secondaryHistoryChanged();
}

void TabModel::goUp()
{
    const QString parent = parentLocation(m_currentPath);
    if (parent != m_currentPath)
        navigateTo(parent);
}

void TabModel::secondaryGoUp()
{
    const QString parent = parentLocation(m_secondaryCurrentPath);
    if (parent != m_secondaryCurrentPath)
        navigateSecondaryTo(parent);
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

    if (pathChanged) {
        emit secondaryCurrentPathChanged();
        emit titleChanged();
    }
    if (historyChanged)
        emit secondaryHistoryChanged();
}

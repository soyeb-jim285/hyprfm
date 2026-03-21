#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>

class TabModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentPathChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)

public:
    explicit TabModel(QObject *parent = nullptr);

    QString currentPath() const;
    QString title() const;
    QString viewMode() const;
    bool canGoBack() const;
    bool canGoForward() const;
    QString sortBy() const;
    bool sortAscending() const;

    void setViewMode(const QString &mode);
    void setSortBy(const QString &column);
    void setSortAscending(bool ascending);

    Q_INVOKABLE void navigateTo(const QString &path);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goUp();

signals:
    void currentPathChanged();
    void viewModeChanged();
    void historyChanged();
    void sortChanged();

private:
    QString m_currentPath;
    QString m_viewMode;
    QStringList m_backStack;
    QStringList m_forwardStack;
    QString m_sortBy;
    bool m_sortAscending;
};

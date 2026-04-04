#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QFileSystemWatcher>
#include <QMap>

class ConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme NOTIFY configChanged)
    Q_PROPERTY(QString iconTheme READ iconTheme NOTIFY configChanged)
    Q_PROPERTY(bool builtinIcons READ builtinIcons NOTIFY configChanged)
    Q_PROPERTY(QString defaultView READ defaultView NOTIFY configChanged)
    Q_PROPERTY(bool showHidden READ showHidden NOTIFY configChanged)
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY configChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY configChanged)
    Q_PROPERTY(QString sidebarPosition READ sidebarPosition NOTIFY configChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY configChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible NOTIFY configChanged)
    Q_PROPERTY(QStringList bookmarks READ bookmarks NOTIFY configChanged)
    Q_PROPERTY(int radiusSmall READ radiusSmall NOTIFY configChanged)
    Q_PROPERTY(int radiusMedium READ radiusMedium NOTIFY configChanged)
    Q_PROPERTY(int radiusLarge READ radiusLarge NOTIFY configChanged)

public:
    explicit ConfigManager(const QString &configPath, QObject *parent = nullptr);

    QString theme() const;
    QString iconTheme() const;
    bool builtinIcons() const;
    QString defaultView() const;
    bool showHidden() const;
    QString sortBy() const;
    bool sortAscending() const;
    QString sidebarPosition() const;
    int sidebarWidth() const;
    bool sidebarVisible() const;
    QStringList bookmarks() const;
    int radiusSmall() const;
    int radiusMedium() const;
    int radiusLarge() const;
    QVariantList customContextActions() const;
    Q_INVOKABLE QString shortcut(const QString &action) const;
    Q_INVOKABLE void saveBookmarks(const QStringList &paths);
    Q_INVOKABLE void saveSidebarWidth(int width);

signals:
    void configChanged();

private:
    void loadConfig();
    void setDefaults();

    QString m_configPath;
    QFileSystemWatcher m_watcher;

    QString m_theme;
    QString m_iconTheme;
    bool m_builtinIcons;
    QString m_defaultView;
    bool m_showHidden;
    QString m_sortBy;
    bool m_sortAscending;
    QString m_sidebarPosition;
    int m_sidebarWidth;
    bool m_sidebarVisible;
    QStringList m_bookmarks;
    int m_radiusSmall;
    int m_radiusMedium;
    int m_radiusLarge;
    QVariantList m_customContextActions;
    QMap<QString, QString> m_shortcuts;
    static QMap<QString, QString> s_defaultShortcuts;
};

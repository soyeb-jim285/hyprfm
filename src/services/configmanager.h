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
    Q_PROPERTY(QStringList availableFonts READ availableFonts CONSTANT)
    Q_PROPERTY(QStringList availableIconThemes READ availableIconThemes CONSTANT)
    Q_PROPERTY(QStringList availableThemes READ availableThemes CONSTANT)
    Q_PROPERTY(QString theme READ theme NOTIFY configChanged)
    Q_PROPERTY(QString iconTheme READ iconTheme NOTIFY configChanged)
    Q_PROPERTY(bool builtinIcons READ builtinIcons NOTIFY configChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY configChanged)
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
    Q_PROPERTY(bool transparencyEnabled READ transparencyEnabled NOTIFY configChanged)
    Q_PROPERTY(double transparencyLevel READ transparencyLevel NOTIFY configChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY configChanged)
    Q_PROPERTY(int animDurationFast READ animDurationFast NOTIFY configChanged)
    Q_PROPERTY(int animDuration READ animDuration NOTIFY configChanged)
    Q_PROPERTY(int animDurationSlow READ animDurationSlow NOTIFY configChanged)
    Q_PROPERTY(QString animCurveEnter READ animCurveEnter NOTIFY configChanged)
    Q_PROPERTY(QString animCurveExit READ animCurveExit NOTIFY configChanged)
    Q_PROPERTY(QString animCurveTransition READ animCurveTransition NOTIFY configChanged)
    Q_PROPERTY(bool showWindowControls READ showWindowControls NOTIFY configChanged)
    Q_PROPERTY(QString windowButtonLayout READ windowButtonLayout NOTIFY configChanged)
    Q_PROPERTY(QVariantMap shortcutMap READ shortcutMap NOTIFY configChanged)
    Q_PROPERTY(QVariantList shortcutDefinitions READ shortcutDefinitions NOTIFY configChanged)

public:
    explicit ConfigManager(const QString &configPath, QObject *parent = nullptr,
                           const QString &themesDir = QString(),
                           const QString &defaultTheme = QStringLiteral("catppuccin-mocha"));

    QStringList availableFonts() const;
    QStringList availableIconThemes() const;
    QStringList availableThemes() const;
    QString theme() const;
    QString iconTheme() const;
    bool builtinIcons() const;
    QString fontFamily() const;
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
    bool transparencyEnabled() const;
    double transparencyLevel() const;
    bool animationsEnabled() const;
    int animDurationFast() const;
    int animDuration() const;
    int animDurationSlow() const;
    QString animCurveEnter() const;
    QString animCurveExit() const;
    QString animCurveTransition() const;
    bool showWindowControls() const;
    void setShowWindowControlsDefault(bool value);
    QString windowButtonLayout() const;
    QVariantMap shortcutMap() const;
    QVariantList shortcutDefinitions() const;
    QVariantList customContextActions() const;
    Q_INVOKABLE QString shortcut(const QString &action) const;
    Q_INVOKABLE void saveSettings(const QVariantMap &settings);
    Q_INVOKABLE void saveShortcuts(const QVariantMap &shortcuts);
    Q_INVOKABLE void saveBookmarks(const QStringList &paths);
    Q_INVOKABLE void saveSidebarWidth(int width);

signals:
    void configChanged();

private:
    void loadConfig();
    void setDefaults();

    QString m_configPath;
    QString m_themesDir;
    QString m_defaultThemeName;
    QFileSystemWatcher m_watcher;

    QString m_theme;
    QString m_iconTheme;
    bool m_builtinIcons;
    QString m_fontFamily;
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
    bool m_transparencyEnabled;
    double m_transparencyLevel;
    bool m_animationsEnabled;
    int m_animDurationFast;
    int m_animDuration;
    int m_animDurationSlow;
    QString m_animCurveEnter;
    QString m_animCurveExit;
    QString m_animCurveTransition;
    bool m_showWindowControls;
    bool m_showWindowControlsExplicit;  // true when user set it in config
    QString m_windowButtonLayout;
    QVariantList m_customContextActions;
    QMap<QString, QString> m_shortcuts;
    static QMap<QString, QString> s_defaultShortcuts;
};

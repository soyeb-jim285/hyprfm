#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
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
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY configChanged)
    Q_PROPERTY(QString defaultView READ defaultView NOTIFY configChanged)
    Q_PROPERTY(QString startupDir READ startupDir NOTIFY configChanged)
    Q_PROPERTY(bool showHidden READ showHidden NOTIFY configChanged)
    Q_PROPERTY(bool rightClickToEditPath READ rightClickToEditPath NOTIFY configChanged)
    Q_PROPERTY(bool dependencyStartupCheck READ dependencyStartupCheck NOTIFY configChanged)
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY configChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY configChanged)
    Q_PROPERTY(bool rememberSortPerFolder READ rememberSortPerFolder NOTIFY configChanged)
    Q_PROPERTY(QString sidebarPosition READ sidebarPosition NOTIFY configChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY configChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible NOTIFY configChanged)
    Q_PROPERTY(QStringList hiddenQuickAccess READ hiddenQuickAccess NOTIFY configChanged)
    Q_PROPERTY(QStringList bookmarks READ bookmarks NOTIFY configChanged)
    Q_PROPERTY(QVariantMap bookmarkNames READ bookmarkNames NOTIFY configChanged)
    Q_PROPERTY(QStringList listColumns READ listColumns NOTIFY listColumnsChanged)
    Q_PROPERTY(QVariantMap listColumnWidths READ listColumnWidths NOTIFY listColumnsChanged)
    Q_PROPERTY(QVariantMap millerFractions READ millerFractions NOTIFY millerFractionsChanged)
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
    // The pair the Dark Mode switch flips between.
    Q_PROPERTY(QString lightTheme READ lightTheme NOTIFY configChanged)
    Q_PROPERTY(QString darkTheme READ darkTheme NOTIFY configChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    // Non-empty when config.toml failed to parse; the last good values stay in effect.
    Q_PROPERTY(QString configError READ configError NOTIFY configErrorChanged)
    Q_PROPERTY(QVariantMap shortcutMap READ shortcutMap NOTIFY configChanged)
    Q_PROPERTY(QVariantList customContextActions READ customContextActions NOTIFY configChanged)
    Q_PROPERTY(QVariantList shortcutDefinitions READ shortcutDefinitions NOTIFY configChanged)

public:
    explicit ConfigManager(const QString &configPath, QObject *parent = nullptr,
                           const QStringList &themesDirs = QStringList(),
                           const QString &defaultTheme = QStringLiteral("catppuccin-mocha"));

    QStringList availableFonts() const;
    QStringList availableIconThemes() const;
    QStringList availableThemes() const;
    QString configPath() const;
    QString theme() const;
    QString lightTheme() const;
    QString darkTheme() const;
    QString iconTheme() const;
    QString fontFamily() const;
    QString defaultView() const;
    // "last" (wherever the previous session was), "home", or an absolute path.
    QString startupDir() const;
    bool showHidden() const;
    // Right click on the address bar starts path editing (Ctrl+L behaviour).
    // Clicking a breadcrumb segment still navigates.
    bool rightClickToEditPath() const;
    bool dependencyStartupCheck() const;
    QString sortBy() const;
    bool sortAscending() const;
    bool rememberSortPerFolder() const;
    QString sidebarPosition() const;
    int sidebarWidth() const;
    bool sidebarVisible() const;
    QStringList hiddenQuickAccess() const;
    // Detailed (list) view columns, in display order. "name" is always present.
    QStringList listColumns() const;
    QVariantMap listColumnWidths() const;
    static QStringList knownListColumns();
    // Fully commented config.toml with every key at its default. Seeded as
    // the user's config on first run and refreshed as config.toml.sample on
    // every start (the app rewrites config.toml without comments on save).
    static QString documentedConfigTemplate();
    // Miller view column widths as fractions of the view: {parent, current};
    // preview takes the rest. Each column keeps at least kMillerMinFraction.
    QVariantMap millerFractions() const;
    static constexpr double kMillerMinFraction = 0.12;
    QStringList bookmarks() const;
    // Custom bookmark display names keyed by the entry in bookmarks().
    QVariantMap bookmarkNames() const;
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
    QString configError() const { return m_configError; }
    // Re-read config.toml now (the file watcher does this on its own).
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString shortcut(const QString &action) const;
    Q_INVOKABLE bool keyEventMatches(const QString &action, int key, int modifiers) const;
    Q_INVOKABLE void saveSettings(const QVariantMap &settings);
    // Per-folder sort memory. The getters resolve to the global default
    // (sortBy/sortAscending) when per-folder memory is off or the folder has
    // no stored entry.
    Q_INVOKABLE QString folderSortBy(const QString &path) const;
    Q_INVOKABLE bool folderSortAscending(const QString &path) const;
    Q_INVOKABLE void setFolderSort(const QString &path, const QString &sortBy,
                                   bool ascending);
    Q_INVOKABLE void saveShortcuts(const QVariantMap &shortcuts);
    Q_INVOKABLE void saveBookmarks(const QStringList &paths, const QVariantMap &names = {});
    Q_INVOKABLE void saveListColumns(const QStringList &columns, const QVariantMap &widths);
    Q_INVOKABLE void saveMillerFractions(double parent, double current);
    Q_INVOKABLE void saveSidebarWidth(int width);

signals:
    void configChanged();
    void configErrorChanged();
    void listColumnsChanged();
    void millerFractionsChanged();

private:
    void loadConfig();
    void setDefaults();
    QString folderSortStorePath() const;
    void loadFolderSort();
    void saveFolderSort() const;

    // path -> {"by": QString, "ascending": bool}
    QMap<QString, QVariantMap> m_folderSort;

    QString m_configPath;
    QStringList m_themesDirs;
    QString m_defaultThemeName;
    QFileSystemWatcher m_watcher;
    QDateTime m_configModified;

    QString m_theme;
    QString m_lightTheme;
    QString m_darkTheme;
    QString m_iconTheme;
    QString m_fontFamily;
    QString m_defaultView;
    QString m_startupDir;
    bool m_showHidden;
    bool m_rightClickToEditPath;
    bool m_dependencyStartupCheck;
    QString m_sortBy;
    bool m_sortAscending;
    bool m_rememberSortPerFolder;
    QString m_sidebarPosition;
    int m_sidebarWidth;
    bool m_sidebarVisible;
    QStringList m_hiddenQuickAccess;
    QStringList m_listColumns;
    QVariantMap m_listColumnWidths;
    void setListColumnsNormalized(const QStringList &columns, const QVariantMap &widths);
    void seedDocumentedConfig();
    double m_millerParent = 0.2;
    double m_millerCurrent = 0.5;
    void setMillerFractionsClamped(double parent, double current);
    QStringList m_bookmarks;
    QVariantMap m_bookmarkNames;
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
    bool m_showWindowControlsRuntimeDefault = false;
    QString m_windowButtonLayout;
    QVariantList m_customContextActions;
    QString m_configError;
    QMap<QString, QString> m_shortcuts;
    static QMap<QString, QString> s_defaultShortcuts;
};

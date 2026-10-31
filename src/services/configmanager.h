#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QMap>
#include <QTimer>

class ConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList availableFonts READ availableFonts CONSTANT)
    Q_PROPERTY(QStringList availableMonospaceFonts READ availableMonospaceFonts CONSTANT)
    Q_PROPERTY(QStringList availableIconThemes READ availableIconThemes CONSTANT)
    Q_PROPERTY(QStringList availableThemes READ availableThemes CONSTANT)
    Q_PROPERTY(QString theme READ theme NOTIFY configChanged)
    Q_PROPERTY(QString iconTheme READ iconTheme NOTIFY configChanged)
    Q_PROPERTY(bool autoRefreshIcons READ autoRefreshIcons NOTIFY configChanged)
    Q_PROPERTY(int iconRevision READ iconRevision NOTIFY iconRevisionChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY configChanged)
    Q_PROPERTY(QString defaultView READ defaultView NOTIFY configChanged)
    Q_PROPERTY(QString splitOrientation READ splitOrientation NOTIFY configChanged)
    Q_PROPERTY(bool showHidden READ showHidden NOTIFY configChanged)
    Q_PROPERTY(bool hiddenLast READ hiddenLast NOTIFY configChanged)
    Q_PROPERTY(bool gridFixedColumns READ gridFixedColumns NOTIFY configChanged)
    Q_PROPERTY(bool rightClickToEditPath READ rightClickToEditPath NOTIFY configChanged)
    Q_PROPERTY(bool singleClickOpensFolders READ singleClickOpensFolders NOTIFY configChanged)
    Q_PROPERTY(bool builtinTerminal READ builtinTerminal NOTIFY configChanged)
    Q_PROPERTY(QString terminalFontFamily READ terminalFontFamily NOTIFY configChanged)
    Q_PROPERTY(double terminalFontSize READ terminalFontSize NOTIFY configChanged)
    Q_PROPERTY(bool terminalLigatures READ terminalLigatures NOTIFY configChanged)
    Q_PROPERTY(QString terminalCursorStyle READ terminalCursorStyle NOTIFY configChanged)
    Q_PROPERTY(bool terminalCursorBlink READ terminalCursorBlink NOTIFY configChanged)
    Q_PROPERTY(int terminalScrollbackLines READ terminalScrollbackLines NOTIFY configChanged)
    Q_PROPERTY(bool dependencyStartupCheck READ dependencyStartupCheck NOTIFY configChanged)
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY configChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY configChanged)
    Q_PROPERTY(bool rememberSortPerFolder READ rememberSortPerFolder NOTIFY configChanged)
    Q_PROPERTY(QString sidebarPosition READ sidebarPosition NOTIFY configChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY configChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible NOTIFY configChanged)
    Q_PROPERTY(int sidebarAutoHideWidth READ sidebarAutoHideWidth NOTIFY configChanged)
    Q_PROPERTY(QStringList hiddenQuickAccess READ hiddenQuickAccess NOTIFY configChanged)
    Q_PROPERTY(QStringList bookmarks READ bookmarks NOTIFY configChanged)
    Q_PROPERTY(QVariantMap bookmarkNames READ bookmarkNames NOTIFY configChanged)
    Q_PROPERTY(QVariantList savedSplits READ savedSplits NOTIFY savedSplitsChanged)
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

    QStringList availableMonospaceFonts() const;
    QStringList availableFonts() const;
    QStringList availableIconThemes() const;
    QStringList availableThemes() const;
    QString configPath() const;
    QString theme() const;
    QString lightTheme() const;
    QString darkTheme() const;
    QString iconTheme() const;
    bool autoRefreshIcons() const { return m_autoRefreshIcons; }
    int iconRevision() const { return m_iconRevision; }
    // What an empty icon_theme resolves to: the desktop's own theme, which
    // only main.cpp can ask the portal for. Set once at startup.
    void setIconThemeFallback(const QString &theme);
    QString fontFamily() const;
    QString defaultView() const;
    QString splitOrientation() const;
    bool showHidden() const;
    // Right click on the address bar starts path editing (Ctrl+L behaviour).
    // Clicking a breadcrumb segment still navigates.
    bool hiddenLast() const;
    bool gridFixedColumns() const;
    bool rightClickToEditPath() const;
    bool singleClickOpensFolders() const;
    bool builtinTerminal() const;
    QString terminalFontFamily() const { return m_terminalFontFamily; }
    double terminalFontSize() const { return m_terminalFontSize; }
    bool terminalLigatures() const { return m_terminalLigatures; }
    QString terminalCursorStyle() const { return m_terminalCursorStyle; }
    bool terminalCursorBlink() const { return m_terminalCursorBlink; }
    int terminalScrollbackLines() const { return m_terminalScrollbackLines; }
    bool dependencyStartupCheck() const;
    QString sortBy() const;
    bool sortAscending() const;
    bool rememberSortPerFolder() const;
    QString sidebarPosition() const;
    int sidebarWidth() const;
    bool sidebarVisible() const;
    // Window width below which the sidebar folds away on its own; 0 disables it.
    int sidebarAutoHideWidth() const;
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
    // Saved split views: [{ name, primaryPath, secondaryPath, orientation }]
    QVariantList savedSplits() const;
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
    // Does customContextActions()[index] apply to this item? `types` entries are
    // "*" (anything), "dir", an extension ("png") or a MIME pattern ("image/*",
    // "text/plain"); an unset or empty list means everything.
    Q_INVOKABLE bool customActionMatches(int index, const QString &path,
                                         const QString &mimeType, bool isDir) const;
    Q_INVOKABLE void saveSettings(const QVariantMap &settings);
    // Per-folder sort memory. The getters resolve to the global default
    // (sortBy/sortAscending) when per-folder memory is off or the folder has
    // no stored entry.
    Q_INVOKABLE QString folderSortBy(const QString &path) const;
    Q_INVOKABLE bool folderSortAscending(const QString &path) const;
    Q_INVOKABLE void setFolderSort(const QString &path, const QString &sortBy,
                                   bool ascending);
    Q_INVOKABLE void saveShortcuts(const QVariantMap &shortcuts);
    // Folder bookmarks and split bookmarks, in one config.toml write.
    void saveBookmarks(const QStringList &paths, const QVariantMap &names,
                       const QVariantList &splits);
    Q_INVOKABLE void saveListColumns(const QStringList &columns, const QVariantMap &widths);
    Q_INVOKABLE void saveMillerFractions(double parent, double current);
    Q_INVOKABLE void saveSidebarWidth(int width);

signals:
    void configChanged();
    void configErrorChanged();
    void savedSplitsChanged();
    void listColumnsChanged();
    void millerFractionsChanged();
    void iconRevisionChanged();

private:
    void loadConfig();
    void setConfigError(const QString &message);
    // Empty when every custom action's `shortcut` parses and is unclaimed;
    // otherwise a message naming the ones that will not fire.
    QString customActionShortcutProblems() const;
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
    QFileSystemWatcher m_iconWatcher;
    QTimer m_iconRefreshTimer;
    bool m_autoRefreshIcons = true;
    QDateTime m_configModified;
    int m_iconRevision = 0;
    void watchIconTheme();

    QString m_theme;
    QString m_lightTheme;
    QString m_darkTheme;
    QString m_iconTheme;          // as configured; empty means follow the desktop
    QString m_iconThemeFallback;  // what empty resolves to
    QString m_fontFamily;
    QString m_defaultView;
    QString m_splitOrientation;
    bool m_showHidden;
    bool m_hiddenLast;
    bool m_gridFixedColumns;
    bool m_rightClickToEditPath;
    bool m_singleClickOpensFolders;
    bool m_builtinTerminal;
    QString m_terminalFontFamily;
    double m_terminalFontSize = 0;
    bool m_terminalLigatures = false;
    QString m_terminalCursorStyle = QStringLiteral("block");
    bool m_terminalCursorBlink = true;
    int m_terminalScrollbackLines = 10000;
    bool m_dependencyStartupCheck;
    QString m_sortBy;
    bool m_sortAscending;
    bool m_rememberSortPerFolder;
    QString m_sidebarPosition;
    int m_sidebarWidth;
    bool m_sidebarVisible;
    int m_sidebarAutoHideWidth;
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
    QVariantList m_savedSplits;
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

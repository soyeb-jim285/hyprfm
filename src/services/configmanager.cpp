#include "services/configmanager.h"

#define TOML_HEADER_ONLY 1
#include "third_party/toml.hpp"

#include <QFile>
#include <QKeySequence>
#include <QSaveFile>
#include <sstream>

namespace {

// Write the document atomically: a crash or full disk mid-write must not
// leave a truncated config.toml behind (the sample writer already does this).
bool writeConfigDocument(const QString &path, const toml::table &config)
{
    std::ostringstream out;
    out << config;
    const std::string text = out.str();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not open config for writing:" << path << file.errorString();
        return false;
    }
    file.write(text.data(), qint64(text.size()));
    if (!file.commit()) {
        qWarning() << "Could not write config:" << path << file.errorString();
        return false;
    }
    return true;
}

} // namespace
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

struct ShortcutSpec {
    const char *action;
    const char *label;
};

const ShortcutSpec kShortcutSpecs[] = {
    {"open", "Open"},
    {"back", "Back"},
    {"forward", "Forward"},
    {"parent", "Go to Parent"},
    {"home", "Home"},
    {"refresh", "Refresh"},
    {"new_tab", "New Tab"},
    {"new_window", "New Window"},
    {"close_tab", "Close Tab"},
    {"next_tab", "Next Tab"},
    {"previous_tab", "Previous Tab"},
    {"reopen_tab", "Reopen Closed Tab"},
    {"open_in_new_tab", "Open in New Tab"},
    {"open_in_split", "Open in Split View"},
    {"copy", "Copy"},
    {"cut", "Cut"},
    {"paste", "Paste"},
    {"rename", "Rename"},
    {"new_folder", "New Folder"},
    {"new_file", "New File"},
    {"trash", "Move to Trash"},
    {"permanent_delete", "Permanent Delete"},
    {"toggle_hidden", "Toggle Hidden Files"},
    {"toggle_transparency", "Toggle Transparency"},
    {"quick_preview", "Quick Preview"},
    {"search", "Search"},
    {"context_menu", "Show Context Menu"},
    {"open_terminal", "Open in Terminal"},
    {"properties", "Properties"},
    {"path_bar", "Focus Path Bar"},
    {"toggle_sidebar", "Toggle Sidebar"},
    {"split_view", "Toggle Split View"},
    {"focus_next_pane", "Focus Next Pane"},
    {"focus_previous_pane", "Focus Previous Pane"},
    {"focus_left_pane", "Focus Left Pane"},
    {"focus_right_pane", "Focus Right Pane"},
    {"grid_view", "Grid View"},
    {"miller_view", "Miller View"},
    {"detailed_view", "Detailed View"},
    {"select_all", "Select All"},
    {"undo", "Undo"},
    {"redo", "Redo"},
    {"settings", "Open Settings"},
    {"edit_config", "Edit config.toml"},
    {"keyboard_shortcuts", "Open Keyboard Shortcuts"},
};

QStringList iconSearchDirs()
{
    QStringList searchDirs;
    const QString home = QDir::homePath();
    searchDirs.append(home + "/.icons");
    searchDirs.append(home + "/.local/share/icons");
    searchDirs.append("/usr/share/icons");
    searchDirs.append("/usr/local/share/icons");

    const QString xdgDirs = qEnvironmentVariable("XDG_DATA_DIRS", "/usr/share:/usr/local/share");
    for (const QString &dir : xdgDirs.split(':')) {
        const QString iconDir = dir + "/icons";
        if (!searchDirs.contains(iconDir))
            searchDirs.append(iconDir);
    }

    return searchDirs;
}

// Default bookmarks follow the XDG user dirs, so they keep working on systems
// where the folders are localised (Documentos, Imagens, ...). Entries that
// don't resolve to a real directory are dropped instead of shipping a
// bookmark that opens an empty view.
QStringList defaultBookmarkPaths()
{
    QStringList paths = {
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QDir::homePath() + QStringLiteral("/Projects"),
    };

    paths.removeAll(QString());
    paths.removeDuplicates();
    paths.removeIf([](const QString &path) { return !QFileInfo(path).isDir(); });
    return paths;
}

} // namespace

QMap<QString, QString> ConfigManager::s_defaultShortcuts = {
    {"open", "Return"},
    {"back", "Alt+Left"},
    {"forward", "Alt+Right"},
    {"parent", "Alt+Up"},
    {"home", "Alt+Home"},
    {"refresh", "F5"},
    {"new_tab", "Ctrl+T"},
    {"new_window", "Ctrl+Alt+N"},
    {"close_tab", "Ctrl+W"},
    {"next_tab", "Ctrl+Tab"},
    {"previous_tab", "Ctrl+Shift+Tab"},
    {"reopen_tab", "Ctrl+Shift+T"},
    {"open_in_new_tab", "Ctrl+Return"},
    {"open_in_split", "Ctrl+Shift+Return"},
    {"copy", "Ctrl+C"},
    {"cut", "Ctrl+X"},
    {"paste", "Ctrl+V"},
    {"rename", "F2"},
    {"new_folder", "Ctrl+Shift+N"},
    {"new_file", "Ctrl+N"},
    {"trash", "Delete"},
    {"permanent_delete", "Shift+Delete"},
    {"toggle_hidden", "Ctrl+H"},
    {"toggle_transparency", "Ctrl+Shift+B"},
    {"quick_preview", "Space"},
    {"search", "Ctrl+F"},
    {"context_menu", "Shift+F10"},
    {"open_terminal", "Ctrl+Alt+T"},
    {"properties", "Alt+Return"},
    {"path_bar", "Ctrl+L"},
    {"toggle_sidebar", "F9"},
    {"split_view", "F3"},
    {"focus_next_pane", "F6"},
    {"focus_previous_pane", "Shift+F6"},
    {"focus_left_pane", "Ctrl+Alt+Left"},
    {"focus_right_pane", "Ctrl+Alt+Right"},
    {"grid_view", "Ctrl+1"},
    {"miller_view", "Ctrl+2"},
    {"detailed_view", "Ctrl+3"},
    {"select_all", "Ctrl+A"},
    {"undo", "Ctrl+Z"},
    {"redo", "Ctrl+Shift+Z"},
    {"settings", "Ctrl+,"},
    {"edit_config", "Ctrl+Shift+,"},
    {"keyboard_shortcuts", "Ctrl+?"},
};

ConfigManager::ConfigManager(const QString &configPath, QObject *parent, const QStringList &themesDirs,
                             const QString &defaultTheme)
    : QObject(parent)
    , m_configPath(configPath)
    , m_themesDirs(themesDirs)
    , m_defaultThemeName(defaultTheme)
{
    setDefaults();
    seedDocumentedConfig();
    loadConfig();
    loadFolderSort();

    // Editors save by writing a temp file and renaming over the target, which
    // gives config.toml a new inode and silently drops a file-only watch. Watch
    // the directory too — it survives the replacement — and re-arm the file
    // watch from either signal.
    m_configModified = QFileInfo(m_configPath).lastModified();
    const auto rearmAndReload = [this]() {
        const QFileInfo info(m_configPath);
        if (!info.exists())
            return;
        if (!m_watcher.files().contains(m_configPath))
            m_watcher.addPath(m_configPath);
        // The directory also changes when session.json and friends are written,
        // so only reload when config.toml itself moved on.
        if (info.lastModified() == m_configModified)
            return;
        m_configModified = info.lastModified();
        reload();
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, rearmAndReload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, rearmAndReload);

    if (QFile::exists(m_configPath))
        m_watcher.addPath(m_configPath);
    m_watcher.addPath(QFileInfo(m_configPath).absolutePath());
}

QStringList ConfigManager::availableThemes() const
{
    QStringList themes;
    for (const QString &themesDir : m_themesDirs) {
        if (themesDir.isEmpty())
            continue;
        QDir dir(themesDir);
        const QStringList files =
            dir.entryList({"*.toml"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QString &fileName : files) {
            const QString name = QFileInfo(fileName).completeBaseName();
            if (!themes.contains(name))
                themes.append(name);
        }
    }
    themes.sort(Qt::CaseInsensitive);
    return themes;
}

QStringList ConfigManager::availableFonts() const
{
    QStringList fonts = QFontDatabase().families();
    fonts.removeDuplicates();
    fonts.sort(Qt::CaseInsensitive);
    return fonts;
}

QStringList ConfigManager::availableIconThemes() const
{
    QStringList themes;
    for (const QString &baseDir : iconSearchDirs()) {
        QDir dir(baseDir);
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (!QFile::exists(entry.filePath() + "/index.theme"))
                continue;

            const QString name = entry.fileName();
            if (!themes.contains(name))
                themes.append(name);
        }
    }

    themes.sort(Qt::CaseInsensitive);
    return themes;
}

void ConfigManager::setDefaults()
{
    m_theme = m_defaultThemeName.trimmed().isEmpty()
        ? QStringLiteral("catppuccin-mocha")
        : m_defaultThemeName.trimmed();
    m_lightTheme = QStringLiteral("catppuccin-latte");
    m_darkTheme = QStringLiteral("catppuccin-mocha");
    m_iconTheme = "Adwaita";
    m_fontFamily.clear();
    m_defaultView = "grid";
    m_startupDir = "last";
    m_showHidden = false;
    m_rightClickToEditPath = true;
    m_sortBy = "name";
    m_sortAscending = true;
    m_rememberSortPerFolder = true;
    m_sidebarPosition = "left";
    m_sidebarWidth = 200;
    m_dependencyStartupCheck = true;
    m_sidebarVisible = true;
    m_hiddenQuickAccess.clear();
    setListColumnsNormalized({}, {});
    setMillerFractionsClamped(0.2, 0.5);
    m_bookmarks = defaultBookmarkPaths();
    m_bookmarkNames.clear();
    m_radiusSmall = 4;
    m_radiusMedium = 8;
    m_radiusLarge = 12;
    m_transparencyEnabled = true;
    m_transparencyLevel = 1.0;
    m_animationsEnabled = true;
    m_animDurationFast = 100;
    m_animDuration = 200;
    m_animDurationSlow = 350;
    m_animCurveEnter = QStringLiteral("OutCubic");
    m_animCurveExit = QStringLiteral("InCubic");
    m_animCurveTransition = QStringLiteral("Bezier");
    m_showWindowControls = false;  // overridden by runtime detection
    m_showWindowControlsExplicit = false;
    m_windowButtonLayout = QStringLiteral(":minimize,maximize,close");
    m_shortcuts = s_defaultShortcuts;
}

void ConfigManager::reload()
{
    loadConfig();
    emit configChanged();
}

void ConfigManager::loadConfig()
{
    if (!QFile::exists(m_configPath))
        return;

    try {
        // Parse before touching any member: a broken file (a duplicated table,
        // a typo) must leave the last good configuration in effect.
        auto config = toml::parse_file(m_configPath.toStdString());
        // Every key that is not in the file goes back to its default, so
        // deleting a line from config.toml takes effect on reload instead of
        // leaving the old value in memory. The window-controls default comes
        // from the compositor at runtime, not from the file; keep it.
        setDefaults();
        m_showWindowControls = m_showWindowControlsRuntimeDefault;


        if (auto v = config["general"]["theme"].value<std::string>())
            m_theme = QString::fromStdString(*v);
        if (auto v = config["general"]["light_theme"].value<std::string>())
            m_lightTheme = QString::fromStdString(*v);
        if (auto v = config["general"]["dark_theme"].value<std::string>())
            m_darkTheme = QString::fromStdString(*v);
        if (auto v = config["general"]["icon_theme"].value<std::string>())
            m_iconTheme = QString::fromStdString(*v);
        if (auto v = config["general"]["font_family"].value<std::string>())
            m_fontFamily = QString::fromStdString(*v);
        if (auto v = config["general"]["default_view"].value<std::string>())
            m_defaultView = QString::fromStdString(*v);
        if (auto v = config["general"]["startup_dir"].value<std::string>()) {
            const QString value = QString::fromStdString(*v).trimmed();
            m_startupDir = value.isEmpty() ? QStringLiteral("last") : value;
        }
        if (auto v = config["general"]["show_hidden"].value<bool>())
            m_showHidden = *v;
        if (auto v = config["general"]["right_click_to_edit_path"].value<bool>())
            m_rightClickToEditPath = *v;
        if (auto v = config["general"]["dependency_startup_check"].value<bool>())
            m_dependencyStartupCheck = *v;
        if (auto v = config["general"]["sort_by"].value<std::string>())
            m_sortBy = QString::fromStdString(*v);
        if (auto v = config["general"]["sort_ascending"].value<bool>())
            m_sortAscending = *v;
        if (auto v = config["general"]["remember_sort_per_folder"].value<bool>())
            m_rememberSortPerFolder = *v;

        if (auto v = config["sidebar"]["position"].value<std::string>())
            m_sidebarPosition = QString::fromStdString(*v);
        if (auto v = config["sidebar"]["width"].value<int64_t>())
            m_sidebarWidth = static_cast<int>(*v);
        if (auto v = config["sidebar"]["visible"].value<bool>())
            m_sidebarVisible = *v;

        // Quick-access entries the user removed from the sidebar, by name
        // ("Pictures", "Network", ...). Absent key = show everything.
        m_hiddenQuickAccess.clear();
        if (auto arr = config["sidebar"]["hidden_quick_access"].as_array()) {
            for (const auto &item : *arr) {
                if (auto v = item.value<std::string>())
                    m_hiddenQuickAccess.append(QString::fromStdString(*v));
            }
        }

        // Detailed view columns
        if (auto arr = config["list_view"]["columns"].as_array()) {
            QStringList columns;
            for (const auto &item : *arr) {
                if (auto v = item.value<std::string>())
                    columns.append(QString::fromStdString(*v));
            }
            QVariantMap widths;
            if (auto tbl = config["list_view"]["column_widths"].as_table()) {
                for (const auto &[key, value] : *tbl) {
                    if (auto w = value.value<int64_t>())
                        widths.insert(QString::fromStdString(std::string(key.str())), static_cast<int>(*w));
                }
            }
            setListColumnsNormalized(columns, widths);
        }

        // Miller view column widths
        {
            double parent = m_millerParent, current = m_millerCurrent;
            if (auto v = config["miller_view"]["parent_fraction"].value<double>())
                parent = *v;
            if (auto v = config["miller_view"]["current_fraction"].value<double>())
                current = *v;
            setMillerFractionsClamped(parent, current);
        }

        // Appearance
        if (auto v = config["appearance"]["radius_small"].value<int64_t>())
            m_radiusSmall = static_cast<int>(*v);
        if (auto v = config["appearance"]["radius_medium"].value<int64_t>())
            m_radiusMedium = static_cast<int>(*v);
        if (auto v = config["appearance"]["radius_large"].value<int64_t>())
            m_radiusLarge = static_cast<int>(*v);
        if (auto v = config["appearance"]["transparency_enabled"].value<bool>())
            m_transparencyEnabled = *v;
        if (auto v = config["appearance"]["transparency_level"].value<double>())
            m_transparencyLevel = qBound(0.0, *v, 1.0);
        if (auto v = config["appearance"]["animations_enabled"].value<bool>())
            m_animationsEnabled = *v;
        if (auto v = config["appearance"]["anim_duration_fast"].value<int64_t>())
            m_animDurationFast = qBound(0, static_cast<int>(*v), 1000);
        if (auto v = config["appearance"]["anim_duration"].value<int64_t>())
            m_animDuration = qBound(0, static_cast<int>(*v), 2000);
        if (auto v = config["appearance"]["anim_duration_slow"].value<int64_t>())
            m_animDurationSlow = qBound(0, static_cast<int>(*v), 3000);
        if (auto v = config["appearance"]["anim_curve_enter"].value<std::string>())
            m_animCurveEnter = QString::fromStdString(*v);
        if (auto v = config["appearance"]["anim_curve_exit"].value<std::string>())
            m_animCurveExit = QString::fromStdString(*v);
        if (auto v = config["appearance"]["anim_curve_transition"].value<std::string>())
            m_animCurveTransition = QString::fromStdString(*v);

        // Window controls
        if (auto v = config["window"]["show_controls"].value<bool>()) {
            m_showWindowControls = *v;
            m_showWindowControlsExplicit = true;
        }
        if (auto v = config["window"]["button_layout"].value<std::string>())
            m_windowButtonLayout = QString::fromStdString(*v);

        if (auto arr = config["bookmarks"]["paths"].as_array()) {
            m_bookmarks.clear();
            for (const auto &item : *arr) {
                if (auto v = item.value<std::string>())
                    m_bookmarks.append(QString::fromStdString(*v));
            }
        }
        if (auto names = config["bookmarks"]["names"].as_table()) {
            for (const auto &[key, val] : *names) {
                if (auto v = val.value<std::string>())
                    m_bookmarkNames.insert(QString::fromStdString(std::string(key)),
                                           QString::fromStdString(*v));
            }
        }

        m_customContextActions.clear();
        if (auto arr = config["context_menu"]["actions"].as_array()) {
            for (const auto &item : *arr) {
                if (auto tbl = item.as_table()) {
                    QVariantMap action;
                    if (auto v = (*tbl)["name"].value<std::string>())
                        action["name"] = QString::fromStdString(*v);
                    if (auto v = (*tbl)["command"].value<std::string>())
                        action["command"] = QString::fromStdString(*v);
                    if (auto types = (*tbl)["types"].as_array()) {
                        QStringList typeList;
                        for (const auto &t : *types) {
                            if (auto v = t.value<std::string>())
                                typeList.append(QString::fromStdString(*v));
                        }
                        action["types"] = typeList;
                    }
                    m_customContextActions.append(action);
                }
            }
        }

        if (auto tbl = config["shortcuts"].as_table()) {
            for (const auto &[key, val] : *tbl) {
                if (auto v = val.value<std::string>()) {
                    m_shortcuts[QString::fromStdString(std::string(key))] =
                        QString::fromStdString(*v);
                }
            }

            // Migrate the old default new-file shortcut so existing configs
            // pick up Ctrl+N unless the user chose a different custom binding.
            if (m_shortcuts.value(QStringLiteral("new_file")) == QStringLiteral("Ctrl+Alt+N"))
                m_shortcuts[QStringLiteral("new_file")] = s_defaultShortcuts.value(QStringLiteral("new_file"));
        }

        if (!m_configError.isEmpty()) {
            m_configError.clear();
            emit configErrorChanged();
        }
    } catch (const toml::parse_error &err) {
        qWarning() << "Config parse error:" << err.what();
        const auto desc = err.description();
        const QString message = QStringLiteral("config.toml line %1: %2")
            .arg(err.source().begin.line)
            .arg(QString::fromUtf8(desc.data(), int(desc.size())));
        if (message != m_configError) {
            m_configError = message;
            emit configErrorChanged();
        }
    }
    emit listColumnsChanged();
    emit millerFractionsChanged();
}

// Keeps every Miller column at least kMillerMinFraction wide: parent first,
// then current, then whatever is left must still fit the preview column.
void ConfigManager::setMillerFractionsClamped(double parent, double current)
{
    const double minF = kMillerMinFraction;
    parent = qBound(minF, parent, 1.0 - 2 * minF);
    current = qBound(minF, current, 1.0 - minF - parent);
    m_millerParent = parent;
    m_millerCurrent = current;
}

QVariantMap ConfigManager::millerFractions() const
{
    return {{"parent", m_millerParent}, {"current", m_millerCurrent}};
}

void ConfigManager::saveMillerFractions(double parent, double current)
{
    setMillerFractionsClamped(parent, current);

    const bool wasWatchingConfig = m_watcher.files().contains(m_configPath);
    if (wasWatchingConfig)
        m_watcher.removePath(m_configPath);

    toml::table config;
    if (QFile::exists(m_configPath)) {
        try {
            config = toml::parse_file(m_configPath.toStdString());
        } catch (...) {}
    }
    config.insert_or_assign("miller_view", toml::table{{"parent_fraction", m_millerParent},
                                                       {"current_fraction", m_millerCurrent}});

    writeConfigDocument(m_configPath, config);

    if (QFile::exists(m_configPath)) {
        m_configModified = QFileInfo(m_configPath).lastModified();
        m_watcher.addPath(m_configPath);
    }
    emit millerFractionsChanged();
}

QString ConfigManager::documentedConfigTemplate()
{
    return QStringLiteral(R"(# HyprFM configuration — ~/.config/hyprfm/config.toml
#
# Every key below is at its default; delete or comment out anything you do
# not want to override. Changes are picked up live while HyprFM runs.
#
# NOTE: when you change a setting inside HyprFM (Settings panel, column
# drags, bookmarks…) the app rewrites this file and these comments are lost.
# config.toml.sample next to it is regenerated on every start and always
# has the full documented set of keys for the version you are running.

[general]
# Colour theme: a file name from /usr/share/hyprfm/themes or
# ~/.config/hyprfm/themes without ".toml". Unset = follow the system
# light/dark preference (catppuccin-latte / catppuccin-mocha).
# theme = "catppuccin-mocha"

# The pair the Dark Mode switch in Settings flips between. Point them at any
# two themes; a script can also just rewrite "theme" above and HyprFM reloads.
light_theme = "catppuccin-latte"
dark_theme = "catppuccin-mocha"

# Icon theme for file and folder icons (a directory name under
# /usr/share/icons or ~/.icons). Toolbar and sidebar icons are built in.
icon_theme = "Adwaita"

# UI font family. Empty = the desktop's UI font.
font_family = ""

# View for new tabs: "grid" | "detailed" | "miller"
default_view = "grid"

show_hidden = false

# Right click anywhere on the address bar enters path edit mode with the
# whole path selected, just like Ctrl+L. Left clicks on breadcrumb segments
# still navigate.
right_click_to_edit_path = true

# Sort order: "name" | "size" | "modified" | "type"
sort_by = "name"
sort_ascending = true

# Remember a different sort per folder (stored in folder_sort.json).
remember_sort_per_folder = true

# Warn at startup when a tool HyprFM uses (gvfs, ffmpeg, bat, pdftoppm…) is missing.
dependency_startup_check = true

# Folder opened at startup when no path is passed on the command line:
# "last" = wherever the previous session was, "home" = your home folder, or an
# absolute path (a leading ~ is expanded). A command line path always wins.
startup_dir = "last"

[sidebar]
# "left" | "right"
position = "left"
width = 200
visible = true
# Quick-access entries to hide. Valid names:
# "Home", "Recents", "Trash", "Network", "Pictures", "Downloads"
hidden_quick_access = []

[appearance]
# Corner radii in pixels.
radius_small = 4
radius_medium = 8
radius_large = 12

# Window transparency (needs compositor blur rules to look good).
# transparency_level: 0.0 (fully transparent) .. 1.0 (opaque)
transparency_enabled = true
transparency_level = 1.0

# Animations. Durations in milliseconds; curves are Qt easing names:
# Linear | InCubic | OutCubic | InOutCubic | OutBack | InOutQuad | OutQuad
# | OutExpo | InOutExpo | Bezier
animations_enabled = true
anim_duration_fast = 100
anim_duration = 200
anim_duration_slow = 350
anim_curve_enter = "OutCubic"
anim_curve_exit = "InCubic"
anim_curve_transition = "Bezier"

[window]
# Draw minimize/maximize/close buttons in HyprFM's own title bar.
# Unset = on only when the compositor provides no decorations.
# show_controls = false
# Button order, ":" separates left from right side.
button_layout = ":minimize,maximize,close"

[list_view]
# Columns of the detailed view, in display order. "name" is always present.
# Available: name, size, modified, type, permissions, owner, group, created,
# accessed, extension, mime, git, symlink
# Right-click the header to toggle columns, drag headers to reorder, drag the
# line between two headers to resize.
columns = ["name", "size", "modified", "type"]
column_widths = { size = 110, modified = 140, type = 80 }

[miller_view]
# Column widths as fractions of the view; the preview column takes the rest.
# Each column keeps at least 0.12. Drag the lines between columns to change.
parent_fraction = 0.2
current_fraction = 0.5

[bookmarks]
# Sidebar bookmarks. Unset = the XDG user folders that exist on this machine.
# paths = ["~/Documents", "~/Downloads", "~/Pictures", "~/Projects"]
# Custom display names, keyed by an entry of paths. Right-click a bookmark
# and choose "Rename" to set one in-app.
# names = { "~/Projects" = "Work" }

[context_menu]
# Extra entries at the bottom of a file's or folder's right-click menu.
# `command` takes desktop-entry field codes (%f or %u = the item's path) and runs once
# per selected item, from that item's folder. `types` limits where the entry
# shows: "*" (default), "dir", an extension ("png"), or a MIME pattern
# ("image/*", "text/plain").
# [[context_menu.actions]]
# name = "Optimize PNG"
# command = "oxipng -o 4 %f"
# types = ["png"]
# [[context_menu.actions]]
# name = "Open in VS Code"
# command = "code %f"
# types = ["dir", "text/*"]

[shortcuts]
# Override any shortcut with a Qt key sequence. Defaults:
# open              = "Return"
# back              = "Alt+Left"
# forward           = "Alt+Right"
# parent            = "Alt+Up"
# home              = "Alt+Home"
# refresh           = "F5"
# new_tab           = "Ctrl+T"
# new_window        = "Ctrl+Alt+N"
# close_tab         = "Ctrl+W"
# next_tab          = "Ctrl+Tab"
# previous_tab      = "Ctrl+Shift+Tab"
# reopen_tab        = "Ctrl+Shift+T"
# open_in_new_tab   = "Ctrl+Return"
# open_in_split     = "Ctrl+Shift+Return"
# copy              = "Ctrl+C"
# cut               = "Ctrl+X"
# paste             = "Ctrl+V"
# rename            = "F2"
# new_folder        = "Ctrl+Shift+N"
# new_file          = "Ctrl+N"
# trash             = "Delete"
# permanent_delete  = "Shift+Delete"
# toggle_hidden     = "Ctrl+H"
# toggle_transparency = "Ctrl+Shift+B"
# quick_preview     = "Space"
# search            = "Ctrl+F"
# context_menu      = "Shift+F10"
# open_terminal     = "Ctrl+Alt+T"
# properties        = "Alt+Return"
# path_bar          = "Ctrl+L"
# toggle_sidebar    = "F9"
# split_view        = "F3"
# focus_next_pane   = "F6"
# focus_previous_pane = "Shift+F6"
# focus_left_pane   = "Ctrl+Alt+Left"
# focus_right_pane  = "Ctrl+Alt+Right"
# grid_view         = "Ctrl+1"
# miller_view       = "Ctrl+2"
# detailed_view     = "Ctrl+3"
# select_all        = "Ctrl+A"
# undo              = "Ctrl+Z"
# redo              = "Ctrl+Shift+Z"
# settings          = "Ctrl+,"
# edit_config       = "Ctrl+Shift+,"
# keyboard_shortcuts = "Ctrl+?"
)");
}

// First run: give the user the documented file instead of nothing. Every
// run: refresh the .sample so it documents this version's keys even after
// the app has rewritten config.toml without comments.
void ConfigManager::seedDocumentedConfig()
{
    if (m_configPath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    const QByteArray text = documentedConfigTemplate().toUtf8();
    if (!QFile::exists(m_configPath)) {
        QSaveFile config(m_configPath);
        if (config.open(QIODevice::WriteOnly)) {
            config.write(text);
            config.commit();
        }
    }
    QSaveFile sample(m_configPath + ".sample");
    if (sample.open(QIODevice::WriteOnly)) {
        sample.write(text);
        sample.commit();
    }
}

QStringList ConfigManager::knownListColumns()
{
    return {"name", "size", "modified", "type", "permissions", "owner", "group",
            "created", "accessed", "extension", "mime", "git", "symlink"};
}

// Drops unknown/duplicate keys, keeps "name" present, fills every width with
// a default and clamps to the minimum the view can render.
void ConfigManager::setListColumnsNormalized(const QStringList &columns, const QVariantMap &widths)
{
    static const QVariantMap defaultWidths {
        {"size", 110}, {"modified", 140}, {"type", 80}, {"permissions", 90},
        {"owner", 90}, {"group", 90}, {"created", 140}, {"accessed", 140},
        {"extension", 70}, {"mime", 160}, {"git", 70}, {"symlink", 180},
    };
    constexpr int kMinWidth = 40;
    const QStringList known = knownListColumns();

    QStringList normalized;
    for (const QString &c : columns) {
        if (known.contains(c) && !normalized.contains(c))
            normalized.append(c);
    }
    if (!normalized.contains("name"))
        normalized.prepend("name");
    if (normalized.size() == 1)
        normalized = {"name", "size", "modified", "type"};
    m_listColumns = normalized;

    m_listColumnWidths = defaultWidths;
    for (auto it = widths.constBegin(); it != widths.constEnd(); ++it) {
        if (defaultWidths.contains(it.key()))
            m_listColumnWidths[it.key()] = qMax(kMinWidth, it.value().toInt());
    }
}

QStringList ConfigManager::listColumns() const { return m_listColumns; }
QVariantMap ConfigManager::listColumnWidths() const { return m_listColumnWidths; }

void ConfigManager::saveListColumns(const QStringList &columns, const QVariantMap &widths)
{
    setListColumnsNormalized(columns, widths);

    const bool wasWatchingConfig = m_watcher.files().contains(m_configPath);
    if (wasWatchingConfig)
        m_watcher.removePath(m_configPath);

    toml::table config;
    if (QFile::exists(m_configPath)) {
        try {
            config = toml::parse_file(m_configPath.toStdString());
        } catch (...) {}
    }

    toml::array cols;
    for (const QString &c : m_listColumns)
        cols.push_back(c.toStdString());
    toml::table widthTable;
    for (auto it = m_listColumnWidths.constBegin(); it != m_listColumnWidths.constEnd(); ++it)
        widthTable.insert_or_assign(it.key().toStdString(), static_cast<int64_t>(it.value().toInt()));
    config.insert_or_assign("list_view", toml::table{{"columns", std::move(cols)},
                                                     {"column_widths", std::move(widthTable)}});

    writeConfigDocument(m_configPath, config);

    if (QFile::exists(m_configPath)) {
        m_configModified = QFileInfo(m_configPath).lastModified();
        m_watcher.addPath(m_configPath);
    }
    emit listColumnsChanged();
}

QString ConfigManager::theme() const { return m_theme; }
QString ConfigManager::lightTheme() const { return m_lightTheme; }
QString ConfigManager::darkTheme() const { return m_darkTheme; }
QString ConfigManager::iconTheme() const { return m_iconTheme; }
QString ConfigManager::fontFamily() const { return m_fontFamily; }
QString ConfigManager::defaultView() const { return m_defaultView; }
QString ConfigManager::startupDir() const { return m_startupDir; }
bool ConfigManager::showHidden() const { return m_showHidden; }

bool ConfigManager::rightClickToEditPath() const { return m_rightClickToEditPath; }
bool ConfigManager::dependencyStartupCheck() const { return m_dependencyStartupCheck; }
QString ConfigManager::sortBy() const { return m_sortBy; }
bool ConfigManager::sortAscending() const { return m_sortAscending; }
bool ConfigManager::rememberSortPerFolder() const { return m_rememberSortPerFolder; }
QString ConfigManager::sidebarPosition() const { return m_sidebarPosition; }
int ConfigManager::sidebarWidth() const { return m_sidebarWidth; }
bool ConfigManager::sidebarVisible() const { return m_sidebarVisible; }
QStringList ConfigManager::hiddenQuickAccess() const { return m_hiddenQuickAccess; }
QStringList ConfigManager::bookmarks() const { return m_bookmarks; }
QVariantMap ConfigManager::bookmarkNames() const { return m_bookmarkNames; }
int ConfigManager::radiusSmall() const { return m_radiusSmall; }
int ConfigManager::radiusMedium() const { return m_radiusMedium; }
int ConfigManager::radiusLarge() const { return m_radiusLarge; }
QString ConfigManager::configPath() const { return m_configPath; }

bool ConfigManager::transparencyEnabled() const { return m_transparencyEnabled; }
double ConfigManager::transparencyLevel() const { return m_transparencyLevel; }
bool ConfigManager::animationsEnabled() const { return m_animationsEnabled; }
int ConfigManager::animDurationFast() const { return m_animDurationFast; }
int ConfigManager::animDuration() const { return m_animDuration; }
int ConfigManager::animDurationSlow() const { return m_animDurationSlow; }
QString ConfigManager::animCurveEnter() const { return m_animCurveEnter; }
QString ConfigManager::animCurveExit() const { return m_animCurveExit; }
QString ConfigManager::animCurveTransition() const { return m_animCurveTransition; }
bool ConfigManager::showWindowControls() const { return m_showWindowControls; }

void ConfigManager::setShowWindowControlsDefault(bool value)
{
    m_showWindowControlsRuntimeDefault = value;
    if (!m_showWindowControlsExplicit)
        m_showWindowControls = value;
}

QString ConfigManager::windowButtonLayout() const { return m_windowButtonLayout; }

QVariantMap ConfigManager::shortcutMap() const
{
    QVariantMap shortcuts;
    for (const auto &spec : kShortcutSpecs) {
        const QString action = QString::fromUtf8(spec.action);
        shortcuts.insert(action, m_shortcuts.value(action, s_defaultShortcuts.value(action)));
    }
    return shortcuts;
}

QVariantList ConfigManager::shortcutDefinitions() const
{
    QVariantList definitions;
    definitions.reserve(static_cast<qsizetype>(sizeof(kShortcutSpecs) / sizeof(kShortcutSpecs[0])));

    for (const auto &spec : kShortcutSpecs) {
        const QString action = QString::fromUtf8(spec.action);
        QVariantMap definition;
        definition.insert("action", action);
        definition.insert("label", QString::fromUtf8(spec.label));
        definition.insert("defaultSequence", s_defaultShortcuts.value(action));
        definition.insert("sequence", m_shortcuts.value(action, s_defaultShortcuts.value(action)));
        definitions.append(definition);
    }

    return definitions;
}

QVariantList ConfigManager::customContextActions() const { return m_customContextActions; }

QString ConfigManager::shortcut(const QString &action) const
{
    return m_shortcuts.value(action, s_defaultShortcuts.value(action));
}

// Lets QML key handlers honour a remapped [shortcuts] entry: true when the
// pressed key + modifiers equal the sequence bound to `action`. Keypad Enter
// counts as Return so the default "Return" works from both keys.
bool ConfigManager::keyEventMatches(const QString &action, int key, int modifiers) const
{
    if (key == Qt::Key_Enter)
        key = Qt::Key_Return;
    const auto mods = Qt::KeyboardModifiers(modifiers) & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const QKeySequence want(shortcut(action));
    return !want.isEmpty() && want == QKeySequence(QKeyCombination(mods, Qt::Key(key)));
}

void ConfigManager::saveSettings(const QVariantMap &settings)
{
    if (settings.isEmpty())
        return;

    const bool wasWatchingConfig = m_watcher.files().contains(m_configPath);
    if (wasWatchingConfig)
        m_watcher.removePath(m_configPath);

    toml::table config;
    if (QFile::exists(m_configPath)) {
        try {
            config = toml::parse_file(m_configPath.toStdString());
        } catch (...) {}
    }

    toml::table general;
    if (auto existingGeneral = config["general"].as_table())
        general = *existingGeneral;

    if (settings.contains("theme")) {
        const QString theme = settings.value("theme").toString().trimmed();
        if (!theme.isEmpty()) {
            m_theme = theme;
            general.insert_or_assign("theme", theme.toStdString());
        }
    }

    for (const auto &[key, tomlKey] : {std::pair{"lightTheme", "light_theme"},
                                       std::pair{"darkTheme", "dark_theme"}}) {
        if (!settings.contains(key))
            continue;
        const QString value = settings.value(key).toString().trimmed();
        if (value.isEmpty())
            continue;
        (QLatin1String(key) == QLatin1String("lightTheme") ? m_lightTheme : m_darkTheme) = value;
        general.insert_or_assign(tomlKey, value.toStdString());
    }

    if (settings.contains("iconTheme")) {
        const QString iconTheme = settings.value("iconTheme").toString().trimmed();
        if (!iconTheme.isEmpty()) {
            m_iconTheme = iconTheme;
            general.insert_or_assign("icon_theme", iconTheme.toStdString());
        }
    }

    if (settings.contains("fontFamily")) {
        m_fontFamily = settings.value("fontFamily").toString().trimmed();
        general.insert_or_assign("font_family", m_fontFamily.toStdString());
    }

    if (settings.contains("startupDir")) {
        const QString value = settings.value("startupDir").toString().trimmed();
        m_startupDir = value.isEmpty() ? QStringLiteral("last") : value;
        general.insert_or_assign("startup_dir", m_startupDir.toStdString());
    }

    if (settings.contains("showHidden")) {
        m_showHidden = settings.value("showHidden").toBool();
        general.insert_or_assign("show_hidden", m_showHidden);
    }

    if (settings.contains("rightClickToEditPath")) {
        m_rightClickToEditPath = settings.value("rightClickToEditPath").toBool();
        general.insert_or_assign("right_click_to_edit_path", m_rightClickToEditPath);
    }

    if (settings.contains("dependencyStartupCheck")) {
        m_dependencyStartupCheck = settings.value("dependencyStartupCheck").toBool();
        general.insert_or_assign("dependency_startup_check", m_dependencyStartupCheck);
    }

    if (settings.contains("sortBy")) {
        const QString sortBy = settings.value("sortBy").toString().trimmed();
        if (!sortBy.isEmpty()) {
            m_sortBy = sortBy;
            general.insert_or_assign("sort_by", sortBy.toStdString());
        }
    }

    if (settings.contains("sortAscending")) {
        m_sortAscending = settings.value("sortAscending").toBool();
        general.insert_or_assign("sort_ascending", m_sortAscending);
    }

    if (settings.contains("rememberSortPerFolder")) {
        m_rememberSortPerFolder = settings.value("rememberSortPerFolder").toBool();
        general.insert_or_assign("remember_sort_per_folder", m_rememberSortPerFolder);
    }

    if (!general.empty())
        config.insert_or_assign("general", std::move(general));

    toml::table sidebar;
    if (auto existingSidebar = config["sidebar"].as_table())
        sidebar = *existingSidebar;

    if (settings.contains("sidebarVisible")) {
        m_sidebarVisible = settings.value("sidebarVisible").toBool();
        sidebar.insert_or_assign("visible", m_sidebarVisible);
    }

    if (settings.contains("sidebarWidth")) {
        m_sidebarWidth = qBound(160, settings.value("sidebarWidth").toInt(), 480);
        sidebar.insert_or_assign("width", m_sidebarWidth);
    }

    if (settings.contains("sidebarPosition")) {
        const QString pos = settings.value("sidebarPosition").toString().trimmed();
        if (pos == "left" || pos == "right") {
            m_sidebarPosition = pos;
            sidebar.insert_or_assign("position", pos.toStdString());
        }
    }

    if (settings.contains("hiddenQuickAccess")) {
        m_hiddenQuickAccess = settings.value("hiddenQuickAccess").toStringList();
        m_hiddenQuickAccess.removeDuplicates();
        toml::array hidden;
        for (const QString &name : m_hiddenQuickAccess)
            hidden.push_back(name.toStdString());
        sidebar.insert_or_assign("hidden_quick_access", std::move(hidden));
    }

    if (!sidebar.empty())
        config.insert_or_assign("sidebar", std::move(sidebar));

    const bool updatesAppearance = settings.contains("radiusSmall")
        || settings.contains("radiusMedium")
        || settings.contains("radiusLarge")
        || settings.contains("transparencyEnabled")
        || settings.contains("transparencyLevel")
        || settings.contains("animationsEnabled")
        || settings.contains("animDurationFast")
        || settings.contains("animDuration")
        || settings.contains("animDurationSlow")
        || settings.contains("animCurveEnter")
        || settings.contains("animCurveExit")
        || settings.contains("animCurveTransition");
    if (updatesAppearance) {
        int radiusSmall = settings.contains("radiusSmall")
            ? qMax(0, settings.value("radiusSmall").toInt())
            : m_radiusSmall;
        int radiusMedium = settings.contains("radiusMedium")
            ? qMax(0, settings.value("radiusMedium").toInt())
            : m_radiusMedium;
        int radiusLarge = settings.contains("radiusLarge")
            ? qMax(0, settings.value("radiusLarge").toInt())
            : m_radiusLarge;

        radiusMedium = qMax(radiusMedium, radiusSmall);
        radiusLarge = qMax(radiusLarge, radiusMedium);

        m_radiusSmall = radiusSmall;
        m_radiusMedium = radiusMedium;
        m_radiusLarge = radiusLarge;
        m_transparencyEnabled = settings.contains("transparencyEnabled")
            ? settings.value("transparencyEnabled").toBool()
            : m_transparencyEnabled;
        m_transparencyLevel = settings.contains("transparencyLevel")
            ? qBound(0.0, settings.value("transparencyLevel").toDouble(), 1.0)
            : m_transparencyLevel;
        m_animationsEnabled = settings.contains("animationsEnabled")
            ? settings.value("animationsEnabled").toBool()
            : m_animationsEnabled;

        toml::table appearance;
        if (auto existingAppearance = config["appearance"].as_table())
            appearance = *existingAppearance;

        appearance.insert_or_assign("radius_small", m_radiusSmall);
        appearance.insert_or_assign("radius_medium", m_radiusMedium);
        appearance.insert_or_assign("radius_large", m_radiusLarge);
        appearance.insert_or_assign("transparency_enabled", m_transparencyEnabled);
        appearance.insert_or_assign("transparency_level", m_transparencyLevel);
        appearance.insert_or_assign("animations_enabled", m_animationsEnabled);

        if (settings.contains("animDurationFast"))
            m_animDurationFast = qBound(0, settings.value("animDurationFast").toInt(), 1000);
        if (settings.contains("animDuration"))
            m_animDuration = qBound(0, settings.value("animDuration").toInt(), 2000);
        if (settings.contains("animDurationSlow"))
            m_animDurationSlow = qBound(0, settings.value("animDurationSlow").toInt(), 3000);
        if (settings.contains("animCurveEnter"))
            m_animCurveEnter = settings.value("animCurveEnter").toString().trimmed();
        if (settings.contains("animCurveExit"))
            m_animCurveExit = settings.value("animCurveExit").toString().trimmed();
        if (settings.contains("animCurveTransition"))
            m_animCurveTransition = settings.value("animCurveTransition").toString().trimmed();

        appearance.insert_or_assign("anim_duration_fast", m_animDurationFast);
        appearance.insert_or_assign("anim_duration", m_animDuration);
        appearance.insert_or_assign("anim_duration_slow", m_animDurationSlow);
        appearance.insert_or_assign("anim_curve_enter", m_animCurveEnter.toStdString());
        appearance.insert_or_assign("anim_curve_exit", m_animCurveExit.toStdString());
        appearance.insert_or_assign("anim_curve_transition", m_animCurveTransition.toStdString());
        config.insert_or_assign("appearance", std::move(appearance));
    }

    // Window controls
    const bool updatesWindow = settings.contains("showWindowControls")
        || settings.contains("windowButtonLayout");
    if (updatesWindow) {
        toml::table windowTbl;
        if (auto existingWindow = config["window"].as_table())
            windowTbl = *existingWindow;

        if (settings.contains("showWindowControls")) {
            m_showWindowControls = settings.value("showWindowControls").toBool();
            m_showWindowControlsExplicit = true;
            windowTbl.insert_or_assign("show_controls", m_showWindowControls);
        }

        if (settings.contains("windowButtonLayout")) {
            m_windowButtonLayout = settings.value("windowButtonLayout").toString().trimmed();
            windowTbl.insert_or_assign("button_layout", m_windowButtonLayout.toStdString());
        }

        if (!windowTbl.empty())
            config.insert_or_assign("window", std::move(windowTbl));
    }

    writeConfigDocument(m_configPath, config);

    if (QFile::exists(m_configPath)) {
        // Our own write, not an external edit — keep the stamp in sync so the
        // watcher does not reload what we just saved.
        m_configModified = QFileInfo(m_configPath).lastModified();
        m_watcher.addPath(m_configPath);
    }

    emit configChanged();
}

QString ConfigManager::folderSortStorePath() const
{
    return QFileInfo(m_configPath).dir().filePath(QStringLiteral("folder_sort.json"));
}

void ConfigManager::loadFolderSort()
{
    m_folderSort.clear();

    QFile f(folderSortStorePath());
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;

    bool pruned = false;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString path = it.key();
        // Prune stale local paths that no longer exist. Non-local locations
        // (trash:/, sftp://, …) are kept since they can't be stat'd.
        if (path.startsWith(QLatin1Char('/')) && !QFileInfo::exists(path)) {
            pruned = true;
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        m_folderSort.insert(path, QVariantMap{
            {QStringLiteral("by"), entry.value(QStringLiteral("by")).toString(QStringLiteral("name"))},
            {QStringLiteral("ascending"), entry.value(QStringLiteral("ascending")).toBool(true)},
        });
    }

    if (pruned)
        saveFolderSort();
}

void ConfigManager::saveFolderSort() const
{
    QJsonObject obj;
    for (auto it = m_folderSort.constBegin(); it != m_folderSort.constEnd(); ++it) {
        obj.insert(it.key(), QJsonObject{
            {QStringLiteral("by"), it.value().value(QStringLiteral("by")).toString()},
            {QStringLiteral("ascending"), it.value().value(QStringLiteral("ascending")).toBool()},
        });
    }

    QSaveFile f(folderSortStorePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

QString ConfigManager::folderSortBy(const QString &path) const
{
    if (m_rememberSortPerFolder && m_folderSort.contains(path))
        return m_folderSort.value(path).value(QStringLiteral("by")).toString();
    return m_sortBy;
}

bool ConfigManager::folderSortAscending(const QString &path) const
{
    if (m_rememberSortPerFolder && m_folderSort.contains(path))
        return m_folderSort.value(path).value(QStringLiteral("ascending")).toBool();
    return m_sortAscending;
}

void ConfigManager::setFolderSort(const QString &path, const QString &sortBy,
                                  bool ascending)
{
    if (path.isEmpty())
        return;

    m_folderSort.insert(path, QVariantMap{
        {QStringLiteral("by"), sortBy},
        {QStringLiteral("ascending"), ascending},
    });
    saveFolderSort();
}

void ConfigManager::saveShortcuts(const QVariantMap &shortcuts)
{
    const bool wasWatchingConfig = m_watcher.files().contains(m_configPath);
    if (wasWatchingConfig)
        m_watcher.removePath(m_configPath);

    toml::table config;
    if (QFile::exists(m_configPath)) {
        try {
            config = toml::parse_file(m_configPath.toStdString());
        } catch (...) {}
    }

    m_shortcuts = s_defaultShortcuts;

    toml::table shortcutTable;
    for (const auto &spec : kShortcutSpecs) {
        const QString action = QString::fromUtf8(spec.action);
        const QString defaultSequence = s_defaultShortcuts.value(action);
        const QString sequence = shortcuts.value(action, defaultSequence).toString().trimmed();

        if (sequence.isEmpty())
            continue;

        m_shortcuts[action] = sequence;
        if (sequence != defaultSequence)
            shortcutTable.insert_or_assign(action.toStdString(), sequence.toStdString());
    }

    config.insert_or_assign("shortcuts", std::move(shortcutTable));

    writeConfigDocument(m_configPath, config);

    if (QFile::exists(m_configPath)) {
        // Our own write, not an external edit — keep the stamp in sync so the
        // watcher does not reload what we just saved.
        m_configModified = QFileInfo(m_configPath).lastModified();
        m_watcher.addPath(m_configPath);
    }

    emit configChanged();
}

void ConfigManager::saveBookmarks(const QStringList &paths, const QVariantMap &names)
{
    m_bookmarks = paths;
    m_bookmarkNames = names;

    const bool wasWatchingConfig = m_watcher.files().contains(m_configPath);
    if (wasWatchingConfig)
        m_watcher.removePath(m_configPath);

    // Read existing config or create new
    toml::table config;
    if (QFile::exists(m_configPath)) {
        try {
            config = toml::parse_file(m_configPath.toStdString());
        } catch (...) {}
    }

    // Update bookmarks array
    toml::array arr;
    for (const auto &p : paths)
        arr.push_back(p.toStdString());
    toml::table bookmarksTable{{"paths", std::move(arr)}};
    if (!names.isEmpty()) {
        toml::table nameTable;
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            nameTable.insert_or_assign(it.key().toStdString(), it.value().toString().toStdString());
        bookmarksTable.insert_or_assign("names", std::move(nameTable));
    }
    config.insert_or_assign("bookmarks", std::move(bookmarksTable));

    // Write back
    writeConfigDocument(m_configPath, config);

    if (QFile::exists(m_configPath)) {
        // Our own write, not an external edit — keep the stamp in sync so the
        // watcher does not reload what we just saved.
        m_configModified = QFileInfo(m_configPath).lastModified();
        m_watcher.addPath(m_configPath);
    }
}

void ConfigManager::saveSidebarWidth(int width)
{
    saveSettings(QVariantMap{{"sidebarWidth", width}});
}

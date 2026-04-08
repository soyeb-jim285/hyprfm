#include "services/configmanager.h"

#define TOML_HEADER_ONLY 1
#include "third_party/toml.hpp"

#include <QFile>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStandardPaths>
#include <fstream>

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
    {"new_tab", "New Tab"},
    {"close_tab", "Close Tab"},
    {"reopen_tab", "Reopen Closed Tab"},
    {"copy", "Copy"},
    {"cut", "Cut"},
    {"paste", "Paste"},
    {"rename", "Rename"},
    {"new_folder", "New Folder"},
    {"new_file", "New File"},
    {"trash", "Move to Trash"},
    {"permanent_delete", "Permanent Delete"},
    {"toggle_hidden", "Toggle Hidden Files"},
    {"quick_preview", "Quick Preview"},
    {"search", "Search"},
    {"path_bar", "Focus Path Bar"},
    {"toggle_sidebar", "Toggle Sidebar"},
    {"split_view", "Toggle Split View"},
    {"grid_view", "Grid View"},
    {"miller_view", "Miller View"},
    {"detailed_view", "Detailed View"},
    {"select_all", "Select All"},
    {"undo", "Undo"},
    {"redo", "Redo"},
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

} // namespace

QMap<QString, QString> ConfigManager::s_defaultShortcuts = {
    {"open", "Return"},
    {"back", "Alt+Left"},
    {"forward", "Alt+Right"},
    {"parent", "Alt+Up"},
    {"new_tab", "Ctrl+T"},
    {"close_tab", "Ctrl+W"},
    {"reopen_tab", "Ctrl+Shift+T"},
    {"copy", "Ctrl+C"},
    {"cut", "Ctrl+X"},
    {"paste", "Ctrl+V"},
    {"rename", "F2"},
    {"new_folder", "Ctrl+Shift+N"},
    {"new_file", "Ctrl+Alt+N"},
    {"trash", "Delete"},
    {"permanent_delete", "Shift+Delete"},
    {"toggle_hidden", "Ctrl+H"},
    {"quick_preview", "Space"},
    {"search", "Ctrl+F"},
    {"path_bar", "Ctrl+L"},
    {"toggle_sidebar", "F9"},
    {"split_view", "F3"},
    {"grid_view", "Ctrl+1"},
    {"miller_view", "Ctrl+2"},
    {"detailed_view", "Ctrl+3"},
    {"select_all", "Ctrl+A"},
    {"undo", "Ctrl+Z"},
    {"redo", "Ctrl+Shift+Z"},
};

ConfigManager::ConfigManager(const QString &configPath, QObject *parent, const QString &themesDir)
    : QObject(parent)
    , m_configPath(configPath)
    , m_themesDir(themesDir)
{
    setDefaults();
    loadConfig();

    if (QFile::exists(m_configPath)) {
        m_watcher.addPath(m_configPath);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
            loadConfig();
            if (QFile::exists(m_configPath))
                m_watcher.addPath(m_configPath);
            emit configChanged();
        });
    }
}

QStringList ConfigManager::availableThemes() const
{
    if (m_themesDir.isEmpty())
        return {};

    QDir dir(m_themesDir);
    const QStringList files = dir.entryList({"*.toml"}, QDir::Files, QDir::Name | QDir::IgnoreCase);

    QStringList themes;
    themes.reserve(files.size());
    for (const QString &fileName : files)
        themes.append(QFileInfo(fileName).completeBaseName());
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
    m_theme = "catppuccin-mocha";
    m_iconTheme = "Adwaita";
    m_builtinIcons = true;
    m_fontFamily.clear();
    m_defaultView = "grid";
    m_showHidden = false;
    m_sortBy = "name";
    m_sortAscending = true;
    m_sidebarPosition = "left";
    m_sidebarWidth = 200;
    m_sidebarVisible = true;
    m_bookmarks = {"~/Documents", "~/Downloads", "~/Pictures", "~/Projects"};
    m_radiusSmall = 4;
    m_radiusMedium = 8;
    m_radiusLarge = 12;
    m_transparencyEnabled = true;
    m_transparencyLevel = 1.0;
    m_animationsEnabled = true;
    m_shortcuts = s_defaultShortcuts;
}

void ConfigManager::loadConfig()
{
    if (!QFile::exists(m_configPath))
        return;

    try {
        m_fontFamily.clear();
        m_transparencyEnabled = true;
        m_transparencyLevel = 1.0;
        m_animationsEnabled = true;
        m_shortcuts = s_defaultShortcuts;

        auto config = toml::parse_file(m_configPath.toStdString());

        if (auto v = config["general"]["theme"].value<std::string>())
            m_theme = QString::fromStdString(*v);
        if (auto v = config["general"]["icon_theme"].value<std::string>())
            m_iconTheme = QString::fromStdString(*v);
        if (auto v = config["general"]["builtin_icons"].value<bool>())
            m_builtinIcons = *v;
        if (auto v = config["general"]["font_family"].value<std::string>())
            m_fontFamily = QString::fromStdString(*v);
        if (auto v = config["general"]["default_view"].value<std::string>())
            m_defaultView = QString::fromStdString(*v);
        if (auto v = config["general"]["show_hidden"].value<bool>())
            m_showHidden = *v;
        if (auto v = config["general"]["sort_by"].value<std::string>())
            m_sortBy = QString::fromStdString(*v);
        if (auto v = config["general"]["sort_ascending"].value<bool>())
            m_sortAscending = *v;

        if (auto v = config["sidebar"]["position"].value<std::string>())
            m_sidebarPosition = QString::fromStdString(*v);
        if (auto v = config["sidebar"]["width"].value<int64_t>())
            m_sidebarWidth = static_cast<int>(*v);
        if (auto v = config["sidebar"]["visible"].value<bool>())
            m_sidebarVisible = *v;

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

        if (auto arr = config["bookmarks"]["paths"].as_array()) {
            m_bookmarks.clear();
            for (const auto &item : *arr) {
                if (auto v = item.value<std::string>())
                    m_bookmarks.append(QString::fromStdString(*v));
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
        }

    } catch (const toml::parse_error &err) {
        qWarning() << "Config parse error:" << err.what();
    }
}

QString ConfigManager::theme() const { return m_theme; }
QString ConfigManager::iconTheme() const { return m_iconTheme; }
bool ConfigManager::builtinIcons() const { return m_builtinIcons; }
QString ConfigManager::fontFamily() const { return m_fontFamily; }
QString ConfigManager::defaultView() const { return m_defaultView; }
bool ConfigManager::showHidden() const { return m_showHidden; }
QString ConfigManager::sortBy() const { return m_sortBy; }
bool ConfigManager::sortAscending() const { return m_sortAscending; }
QString ConfigManager::sidebarPosition() const { return m_sidebarPosition; }
int ConfigManager::sidebarWidth() const { return m_sidebarWidth; }
bool ConfigManager::sidebarVisible() const { return m_sidebarVisible; }
QStringList ConfigManager::bookmarks() const { return m_bookmarks; }
int ConfigManager::radiusSmall() const { return m_radiusSmall; }
int ConfigManager::radiusMedium() const { return m_radiusMedium; }
int ConfigManager::radiusLarge() const { return m_radiusLarge; }
bool ConfigManager::transparencyEnabled() const { return m_transparencyEnabled; }
double ConfigManager::transparencyLevel() const { return m_transparencyLevel; }
bool ConfigManager::animationsEnabled() const { return m_animationsEnabled; }

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

    if (settings.contains("iconTheme")) {
        const QString iconTheme = settings.value("iconTheme").toString().trimmed();
        if (!iconTheme.isEmpty()) {
            m_iconTheme = iconTheme;
            general.insert_or_assign("icon_theme", iconTheme.toStdString());
        }
    }

    if (settings.contains("builtinIcons")) {
        m_builtinIcons = settings.value("builtinIcons").toBool();
        general.insert_or_assign("builtin_icons", m_builtinIcons);
    }

    if (settings.contains("fontFamily")) {
        m_fontFamily = settings.value("fontFamily").toString().trimmed();
        general.insert_or_assign("font_family", m_fontFamily.toStdString());
    }

    if (settings.contains("showHidden")) {
        m_showHidden = settings.value("showHidden").toBool();
        general.insert_or_assign("show_hidden", m_showHidden);
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

    if (!sidebar.empty())
        config.insert_or_assign("sidebar", std::move(sidebar));

    const bool updatesAppearance = settings.contains("radiusSmall")
        || settings.contains("radiusMedium")
        || settings.contains("radiusLarge")
        || settings.contains("transparencyEnabled")
        || settings.contains("transparencyLevel")
        || settings.contains("animationsEnabled");
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
        config.insert_or_assign("appearance", std::move(appearance));
    }

    std::ofstream ofs(m_configPath.toStdString());
    if (ofs.is_open()) {
        ofs << config;
        ofs.close();
    }

    if (QFile::exists(m_configPath))
        m_watcher.addPath(m_configPath);

    emit configChanged();
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

    std::ofstream ofs(m_configPath.toStdString());
    if (ofs.is_open()) {
        ofs << config;
        ofs.close();
    }

    if (QFile::exists(m_configPath))
        m_watcher.addPath(m_configPath);

    emit configChanged();
}

void ConfigManager::saveBookmarks(const QStringList &paths)
{
    m_bookmarks = paths;

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
    config.insert_or_assign("bookmarks", toml::table{{"paths", std::move(arr)}});

    // Write back
    std::ofstream ofs(m_configPath.toStdString());
    if (ofs.is_open()) {
        ofs << config;
        ofs.close();
    }

    if (QFile::exists(m_configPath))
        m_watcher.addPath(m_configPath);
}

void ConfigManager::saveSidebarWidth(int width)
{
    saveSettings(QVariantMap{{"sidebarWidth", width}});
}

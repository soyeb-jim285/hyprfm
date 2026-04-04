#include "services/configmanager.h"

#define TOML_HEADER_ONLY 1
#include "third_party/toml.hpp"

#include <QFile>
#include <QDir>
#include <QDebug>
#include <fstream>

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
    {"path_bar", "Ctrl+L"},
    {"toggle_sidebar", "F9"},
    {"split_view", "F3"},
    {"grid_view", "Ctrl+1"},
    {"list_view", "Ctrl+2"},
    {"detailed_view", "Ctrl+3"},
    {"select_all", "Ctrl+A"},
    {"undo", "Ctrl+Z"},
    {"redo", "Ctrl+Shift+Z"},
};

ConfigManager::ConfigManager(const QString &configPath, QObject *parent)
    : QObject(parent)
    , m_configPath(configPath)
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

void ConfigManager::setDefaults()
{
    m_theme = "catppuccin-mocha";
    m_iconTheme = "Adwaita";
    m_builtinIcons = true;
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
    m_shortcuts = s_defaultShortcuts;
}

void ConfigManager::loadConfig()
{
    if (!QFile::exists(m_configPath))
        return;

    try {
        auto config = toml::parse_file(m_configPath.toStdString());

        if (auto v = config["general"]["theme"].value<std::string>())
            m_theme = QString::fromStdString(*v);
        if (auto v = config["general"]["icon_theme"].value<std::string>())
            m_iconTheme = QString::fromStdString(*v);
        if (auto v = config["general"]["builtin_icons"].value<bool>())
            m_builtinIcons = *v;
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
QVariantList ConfigManager::customContextActions() const { return m_customContextActions; }

QString ConfigManager::shortcut(const QString &action) const
{
    return m_shortcuts.value(action, s_defaultShortcuts.value(action));
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
    const int clampedWidth = qBound(160, width, 480);
    m_sidebarWidth = clampedWidth;

    const bool wasWatchingConfig = m_watcher.files().contains(m_configPath);
    if (wasWatchingConfig)
        m_watcher.removePath(m_configPath);

    toml::table config;
    if (QFile::exists(m_configPath)) {
        try {
            config = toml::parse_file(m_configPath.toStdString());
        } catch (...) {}
    }

    toml::table sidebar;
    if (auto existingSidebar = config["sidebar"].as_table())
        sidebar = *existingSidebar;

    sidebar.insert_or_assign("width", clampedWidth);
    config.insert_or_assign("sidebar", std::move(sidebar));

    std::ofstream ofs(m_configPath.toStdString());
    if (ofs.is_open()) {
        ofs << config;
        ofs.close();
    }

    if (QFile::exists(m_configPath))
        m_watcher.addPath(m_configPath);

    emit configChanged();
}

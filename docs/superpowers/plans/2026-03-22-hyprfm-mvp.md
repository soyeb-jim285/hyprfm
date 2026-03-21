# HyprFM MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a lightweight, polished file manager for Hyprland with three view modes, tabs, sidebar with bookmarks and devices, file operations, quick preview, drag-and-drop, context menus, and custom theming.

**Architecture:** Pure Qt6 QML frontend with a thin C++ backend. QML handles all UI rendering and animations. C++ exposes models and services via Q_PROPERTY/Q_INVOKABLE. Heavy file operations delegate to CLI tools (rsync, gio) via QProcess.

**Tech Stack:** Qt6 (Core, Gui, Qml, Quick, QuickControls2, DBus), C++17, CMake, toml++ (header-only TOML parser)

---

## File Structure

```
hyprfm/
├── CMakeLists.txt                          # Top-level build
├── src/
│   ├── CMakeLists.txt                      # Source build config
│   ├── main.cpp                            # Entry point, engine setup, context registration
│   ├── models/
│   │   ├── filesystemmodel.h               # QFileSystemModel wrapper with sorting/filtering
│   │   ├── filesystemmodel.cpp
│   │   ├── tabmodel.h                      # Per-tab state (dir, history, view mode, selection)
│   │   ├── tabmodel.cpp
│   │   ├── tablistmodel.h                  # QAbstractListModel of tabs
│   │   ├── tablistmodel.cpp
│   │   ├── bookmarkmodel.h                 # Bookmark list model from config
│   │   ├── bookmarkmodel.cpp
│   │   ├── devicemodel.h                   # UDisks2 device list model
│   │   └── devicemodel.cpp
│   ├── services/
│   │   ├── configmanager.h                 # TOML config parsing, hot-reload, defaults
│   │   ├── configmanager.cpp
│   │   ├── themeloader.h                   # Theme TOML to QML color properties
│   │   ├── themeloader.cpp
│   │   ├── fileoperations.h                # Async copy/move/trash/delete via QProcess
│   │   ├── fileoperations.cpp
│   │   ├── clipboardmanager.h              # Internal file clipboard (cut/copy state)
│   │   └── clipboardmanager.cpp
│   ├── providers/
│   │   ├── thumbnailprovider.h             # Async image thumbnail QQuickImageProvider
│   │   └── thumbnailprovider.cpp
│   └── qml/
│       ├── main.qml                        # Root window, layout scaffold
│       ├── components/
│       │   ├── FileTabBar.qml              # Browser-style tab bar
│       │   ├── Toolbar.qml                 # Nav buttons, breadcrumb, view toggle
│       │   ├── Breadcrumb.qml              # Clickable path segments
│       │   ├── Sidebar.qml                 # Bookmarks + devices + operations
│       │   ├── StatusBar.qml               # Item count, selection info
│       │   ├── ContextMenu.qml             # Right-click context menu
│       │   ├── QuickPreview.qml            # Spacebar preview overlay
│       │   ├── OperationsBar.qml           # File operation progress
│       │   ├── Toast.qml                   # Toast notification component
│       │   └── RubberBand.qml              # Drag-to-select rectangle
│       ├── views/
│       │   ├── FileGridView.qml            # Grid icon view
│       │   ├── FileListView.qml            # Simple list view
│       │   ├── FileDetailedView.qml        # Sortable column view
│       │   └── FileViewContainer.qml       # Switches between view modes
│       └── theme/
│           └── Theme.qml                   # QML singleton exposing theme colors
├── themes/
│   └── catppuccin-mocha.toml               # Default theme
├── tests/
│   ├── CMakeLists.txt                      # Test build config
│   ├── tst_configmanager.cpp               # Config parsing tests
│   ├── tst_themeloader.cpp                 # Theme loading tests
│   ├── tst_bookmarkmodel.cpp               # Bookmark model tests
│   ├── tst_tabmodel.cpp                    # Tab state tests
│   ├── tst_fileoperations.cpp              # File operation tests
│   └── tst_clipboardmanager.cpp            # Clipboard state tests
└── resources/
    └── icons/                              # App icons
```

---

## Task 1: Project Skeleton and Build System

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `src/qml/main.qml`
- Create: `tests/CMakeLists.txt`

- [ ] **Step 1: Create top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(hyprfm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus)

add_subdirectory(src)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Create src/CMakeLists.txt**

```cmake
qt_add_executable(hyprfm main.cpp)

qt_add_qml_module(hyprfm
    URI HyprFM
    VERSION 1.0
    QML_FILES
        qml/main.qml
)

target_link_libraries(hyprfm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
)

target_include_directories(hyprfm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

- [ ] **Step 3: Create src/main.cpp with minimal app**

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("HyprFM");
    app.setOrganizationName("hyprfm");

    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    engine.loadFromModule("HyprFM", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
```

- [ ] **Step 4: Create src/qml/main.qml with empty window**

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: "#1e1e2e"

    Text {
        anchors.centerIn: parent
        text: "HyprFM"
        color: "#cdd6f4"
        font.pixelSize: 24
    }
}
```

- [ ] **Step 5: Create tests/CMakeLists.txt (empty for now)**

```cmake
# Tests will be added as backend classes are implemented
```

- [ ] **Step 6: Create .gitignore**

```
build/
.cache/
.superpowers/
CMakeLists.txt.user
*.o
*.so
compile_commands.json
```

- [ ] **Step 7: Build and verify the app launches**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/src/hyprfm
```

Expected: A dark window with "HyprFM" centered text appears.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/ tests/ .gitignore
git commit -m "feat: project skeleton with CMake and empty Qt6 QML window"
```

---

## Task 2: Configuration Manager

**Files:**
- Create: `src/services/configmanager.h`
- Create: `src/services/configmanager.cpp`
- Create: `tests/tst_configmanager.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Dependencies:** Download toml++ header. We use the single-header version.

- [ ] **Step 1: Add toml++ to the project**

```bash
mkdir -p src/third_party
curl -L -o src/third_party/toml.hpp https://raw.githubusercontent.com/marzer/tomlplusplus/master/toml.hpp
```

- [ ] **Step 2: Write failing test for ConfigManager**

Create `tests/tst_configmanager.cpp`:

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "services/configmanager.h"

class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultValues()
    {
        QTemporaryDir dir;
        ConfigManager mgr(dir.path() + "/config.toml");

        QCOMPARE(mgr.theme(), QString("catppuccin-mocha"));
        QCOMPARE(mgr.defaultView(), QString("grid"));
        QCOMPARE(mgr.showHidden(), false);
        QCOMPARE(mgr.sortBy(), QString("name"));
        QCOMPARE(mgr.sortAscending(), true);
        QCOMPARE(mgr.sidebarPosition(), QString("left"));
        QCOMPARE(mgr.sidebarWidth(), 200);
        QCOMPARE(mgr.sidebarVisible(), true);
    }

    void testParseConfig()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[general]\n"
                "theme = \"custom\"\n"
                "default_view = \"list\"\n"
                "show_hidden = true\n"
                "\n"
                "[sidebar]\n"
                "position = \"right\"\n"
                "width = 250\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.theme(), QString("custom"));
        QCOMPARE(mgr.defaultView(), QString("list"));
        QCOMPARE(mgr.showHidden(), true);
        QCOMPARE(mgr.sidebarPosition(), QString("right"));
        QCOMPARE(mgr.sidebarWidth(), 250);
    }

    void testBookmarks()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[bookmarks]\n"
                "paths = [\"~/Documents\", \"~/Downloads\"]\n");
        f.close();

        ConfigManager mgr(path);
        QStringList bookmarks = mgr.bookmarks();
        QCOMPARE(bookmarks.size(), 2);
        QCOMPARE(bookmarks.at(0), QString("~/Documents"));
    }

    void testCustomContextActions()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[[context_menu.actions]]\n"
                "name = \"Open in Neovim\"\n"
                "command = \"foot nvim {file}\"\n"
                "types = [\"file\"]\n");
        f.close();

        ConfigManager mgr(path);
        QVariantList actions = mgr.customContextActions();
        QCOMPARE(actions.size(), 1);
        QVariantMap action = actions.at(0).toMap();
        QCOMPARE(action["name"].toString(), QString("Open in Neovim"));
    }

    void testShortcuts()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/config.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[shortcuts]\n"
                "open = \"Return\"\n"
                "back = \"Alt+Left\"\n");
        f.close();

        ConfigManager mgr(path);
        QCOMPARE(mgr.shortcut("open"), QString("Return"));
        QCOMPARE(mgr.shortcut("back"), QString("Alt+Left"));
        // Default for unset shortcut
        QCOMPARE(mgr.shortcut("new_tab"), QString("Ctrl+T"));
    }
};

QTEST_MAIN(TestConfigManager)
#include "tst_configmanager.moc"
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build
cd build && ctest --test-dir . -R configmanager -V
```

Expected: Build fails -- `configmanager.h` not found.

- [ ] **Step 4: Implement ConfigManager**

Create `src/services/configmanager.h`:

```cpp
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
    Q_PROPERTY(QString defaultView READ defaultView NOTIFY configChanged)
    Q_PROPERTY(bool showHidden READ showHidden NOTIFY configChanged)
    Q_PROPERTY(QString sortBy READ sortBy NOTIFY configChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY configChanged)
    Q_PROPERTY(QString sidebarPosition READ sidebarPosition NOTIFY configChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY configChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible NOTIFY configChanged)
    Q_PROPERTY(QStringList bookmarks READ bookmarks NOTIFY configChanged)

public:
    explicit ConfigManager(const QString &configPath, QObject *parent = nullptr);

    QString theme() const;
    QString defaultView() const;
    bool showHidden() const;
    QString sortBy() const;
    bool sortAscending() const;
    QString sidebarPosition() const;
    int sidebarWidth() const;
    bool sidebarVisible() const;
    QStringList bookmarks() const;
    QVariantList customContextActions() const;
    Q_INVOKABLE QString shortcut(const QString &action) const;

signals:
    void configChanged();

private:
    void loadConfig();
    void setDefaults();

    QString m_configPath;
    QFileSystemWatcher m_watcher;

    // General
    QString m_theme;
    QString m_defaultView;
    bool m_showHidden;
    QString m_sortBy;
    bool m_sortAscending;

    // Sidebar
    QString m_sidebarPosition;
    int m_sidebarWidth;
    bool m_sidebarVisible;

    // Bookmarks
    QStringList m_bookmarks;

    // Context menu custom actions
    QVariantList m_customContextActions;

    // Shortcuts
    QMap<QString, QString> m_shortcuts;
    static QMap<QString, QString> s_defaultShortcuts;
};
```

Create `src/services/configmanager.cpp`:

```cpp
#include "services/configmanager.h"

#define TOML_HEADER_ONLY 1
#include "third_party/toml.hpp"

#include <QFile>
#include <QDir>
#include <QDebug>

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
    {"trash", "Delete"},
    {"permanent_delete", "Shift+Delete"},
    {"toggle_hidden", "Ctrl+H"},
    {"quick_preview", "Space"},
    {"path_bar", "Ctrl+L"},
    {"toggle_sidebar", "F9"},
    {"grid_view", "Ctrl+1"},
    {"list_view", "Ctrl+2"},
    {"detailed_view", "Ctrl+3"},
    {"select_all", "Ctrl+A"},
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
            // Re-add watch (some editors replace the file)
            if (QFile::exists(m_configPath))
                m_watcher.addPath(m_configPath);
            emit configChanged();
        });
    }
}

void ConfigManager::setDefaults()
{
    m_theme = "catppuccin-mocha";
    m_defaultView = "grid";
    m_showHidden = false;
    m_sortBy = "name";
    m_sortAscending = true;
    m_sidebarPosition = "left";
    m_sidebarWidth = 200;
    m_sidebarVisible = true;
    m_bookmarks = {"~/Documents", "~/Downloads", "~/Pictures", "~/Projects"};
    m_shortcuts = s_defaultShortcuts;
}

void ConfigManager::loadConfig()
{
    if (!QFile::exists(m_configPath))
        return;

    try {
        auto config = toml::parse_file(m_configPath.toStdString());

        // General
        if (auto v = config["general"]["theme"].value<std::string>())
            m_theme = QString::fromStdString(*v);
        if (auto v = config["general"]["default_view"].value<std::string>())
            m_defaultView = QString::fromStdString(*v);
        if (auto v = config["general"]["show_hidden"].value<bool>())
            m_showHidden = *v;
        if (auto v = config["general"]["sort_by"].value<std::string>())
            m_sortBy = QString::fromStdString(*v);
        if (auto v = config["general"]["sort_ascending"].value<bool>())
            m_sortAscending = *v;

        // Sidebar
        if (auto v = config["sidebar"]["position"].value<std::string>())
            m_sidebarPosition = QString::fromStdString(*v);
        if (auto v = config["sidebar"]["width"].value<int64_t>())
            m_sidebarWidth = static_cast<int>(*v);
        if (auto v = config["sidebar"]["visible"].value<bool>())
            m_sidebarVisible = *v;

        // Bookmarks
        if (auto arr = config["bookmarks"]["paths"].as_array()) {
            m_bookmarks.clear();
            for (const auto &item : *arr) {
                if (auto v = item.value<std::string>())
                    m_bookmarks.append(QString::fromStdString(*v));
            }
        }

        // Context menu custom actions
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

        // Shortcuts
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
QString ConfigManager::defaultView() const { return m_defaultView; }
bool ConfigManager::showHidden() const { return m_showHidden; }
QString ConfigManager::sortBy() const { return m_sortBy; }
bool ConfigManager::sortAscending() const { return m_sortAscending; }
QString ConfigManager::sidebarPosition() const { return m_sidebarPosition; }
int ConfigManager::sidebarWidth() const { return m_sidebarWidth; }
bool ConfigManager::sidebarVisible() const { return m_sidebarVisible; }
QStringList ConfigManager::bookmarks() const { return m_bookmarks; }
QVariantList ConfigManager::customContextActions() const { return m_customContextActions; }

QString ConfigManager::shortcut(const QString &action) const
{
    return m_shortcuts.value(action, s_defaultShortcuts.value(action));
}
```

- [ ] **Step 5: Update src/CMakeLists.txt to include ConfigManager**

Add to the `qt_add_executable` sources:
```cmake
qt_add_executable(hyprfm
    main.cpp
    services/configmanager.cpp
)
```

Add include path for toml++:
```cmake
target_include_directories(hyprfm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
```

- [ ] **Step 6: Update tests/CMakeLists.txt**

```cmake
add_executable(tst_configmanager tst_configmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/configmanager.cpp
)
target_include_directories(tst_configmanager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_link_libraries(tst_configmanager PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_configmanager COMMAND tst_configmanager)
```

- [ ] **Step 7: Run tests and verify they pass**

```bash
cmake --build build
cd build && ctest -R configmanager -V
```

Expected: All 5 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/services/configmanager.h src/services/configmanager.cpp \
    src/third_party/toml.hpp tests/tst_configmanager.cpp \
    src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add ConfigManager with TOML parsing and hot-reload"
```

---

## Task 3: Theme Loader

**Files:**
- Create: `src/services/themeloader.h`
- Create: `src/services/themeloader.cpp`
- Create: `themes/catppuccin-mocha.toml`
- Create: `tests/tst_themeloader.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create default theme file**

Create `themes/catppuccin-mocha.toml`:

```toml
[colors]
base = "#1e1e2e"
mantle = "#181825"
crust = "#11111b"
surface = "#313244"
overlay = "#45475a"
text = "#cdd6f4"
subtext = "#bac2de"
muted = "#6c7086"
accent = "#89b4fa"
success = "#a6e3a1"
warning = "#f9e2af"
error = "#f38ba8"
purple = "#cba6f7"
```

- [ ] **Step 2: Write failing test for ThemeLoader**

Create `tests/tst_themeloader.cpp`:

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "services/themeloader.h"

class TestThemeLoader : public QObject
{
    Q_OBJECT

private slots:
    void testLoadBuiltinTheme()
    {
        ThemeLoader loader;
        loader.loadTheme("catppuccin-mocha", THEMES_DIR);

        QCOMPARE(loader.color("base"), QColor("#1e1e2e"));
        QCOMPARE(loader.color("accent"), QColor("#89b4fa"));
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
        QCOMPARE(loader.color("error"), QColor("#f38ba8"));
    }

    void testLoadCustomTheme()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/custom.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[colors]\n"
                "base = \"#000000\"\n"
                "text = \"#ffffff\"\n"
                "accent = \"#ff0000\"\n");
        f.close();

        ThemeLoader loader;
        loader.loadTheme(path, "");

        QCOMPARE(loader.color("base"), QColor("#000000"));
        QCOMPARE(loader.color("text"), QColor("#ffffff"));
        QCOMPARE(loader.color("accent"), QColor("#ff0000"));
    }

    void testFallbackForMissingColors()
    {
        QTemporaryDir dir;
        QString path = dir.path() + "/partial.toml";

        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("[colors]\n"
                "base = \"#000000\"\n");
        f.close();

        ThemeLoader loader;
        loader.loadTheme(path, "");

        QCOMPARE(loader.color("base"), QColor("#000000"));
        // Missing keys should return Catppuccin Mocha defaults
        QCOMPARE(loader.color("text"), QColor("#cdd6f4"));
    }
};

QTEST_MAIN(TestThemeLoader)
#include "tst_themeloader.moc"
```

- [ ] **Step 3: Run test to verify it fails**

Expected: Build fails -- `themeloader.h` not found.

- [ ] **Step 4: Implement ThemeLoader**

Create `src/services/themeloader.h`:

```cpp
#pragma once

#include <QObject>
#include <QColor>
#include <QMap>

class ThemeLoader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor base READ base NOTIFY themeChanged)
    Q_PROPERTY(QColor mantle READ mantle NOTIFY themeChanged)
    Q_PROPERTY(QColor crust READ crust NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor overlay READ overlay NOTIFY themeChanged)
    Q_PROPERTY(QColor text READ text NOTIFY themeChanged)
    Q_PROPERTY(QColor subtext READ subtext NOTIFY themeChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor error READ error NOTIFY themeChanged)

public:
    explicit ThemeLoader(QObject *parent = nullptr);

    void loadTheme(const QString &nameOrPath, const QString &themesDir);
    QColor color(const QString &name) const;

    QColor base() const { return color("base"); }
    QColor mantle() const { return color("mantle"); }
    QColor crust() const { return color("crust"); }
    QColor surface() const { return color("surface"); }
    QColor overlay() const { return color("overlay"); }
    QColor text() const { return color("text"); }
    QColor subtext() const { return color("subtext"); }
    QColor muted() const { return color("muted"); }
    QColor accent() const { return color("accent"); }
    QColor success() const { return color("success"); }
    QColor warning() const { return color("warning"); }
    QColor error() const { return color("error"); }

signals:
    void themeChanged();

private:
    QMap<QString, QColor> m_colors;
    static QMap<QString, QColor> s_defaults;
};
```

Create `src/services/themeloader.cpp`:

```cpp
#include "services/themeloader.h"

#define TOML_HEADER_ONLY 1
#include "third_party/toml.hpp"

#include <QFile>
#include <QDir>
#include <QDebug>

QMap<QString, QColor> ThemeLoader::s_defaults = {
    {"base", QColor("#1e1e2e")},
    {"mantle", QColor("#181825")},
    {"crust", QColor("#11111b")},
    {"surface", QColor("#313244")},
    {"overlay", QColor("#45475a")},
    {"text", QColor("#cdd6f4")},
    {"subtext", QColor("#bac2de")},
    {"muted", QColor("#6c7086")},
    {"accent", QColor("#89b4fa")},
    {"success", QColor("#a6e3a1")},
    {"warning", QColor("#f9e2af")},
    {"error", QColor("#f38ba8")},
    {"purple", QColor("#cba6f7")},
};

ThemeLoader::ThemeLoader(QObject *parent)
    : QObject(parent)
    , m_colors(s_defaults)
{
}

void ThemeLoader::loadTheme(const QString &nameOrPath, const QString &themesDir)
{
    m_colors = s_defaults;

    QString filePath;
    if (QFile::exists(nameOrPath)) {
        filePath = nameOrPath;
    } else if (!themesDir.isEmpty()) {
        filePath = QDir(themesDir).filePath(nameOrPath + ".toml");
    }

    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        qWarning() << "Theme not found:" << nameOrPath;
        emit themeChanged();
        return;
    }

    try {
        auto config = toml::parse_file(filePath.toStdString());
        if (auto colors = config["colors"].as_table()) {
            for (const auto &[key, val] : *colors) {
                if (auto v = val.value<std::string>()) {
                    QString colorStr = QString::fromStdString(*v);
                    QColor c(colorStr);
                    if (c.isValid()) {
                        m_colors[QString::fromStdString(std::string(key))] = c;
                    }
                }
            }
        }
    } catch (const toml::parse_error &err) {
        qWarning() << "Theme parse error:" << err.what();
    }

    emit themeChanged();
}

QColor ThemeLoader::color(const QString &name) const
{
    return m_colors.value(name, s_defaults.value(name, QColor("#ff00ff")));
}
```

- [ ] **Step 5: Update build files**

Add `services/themeloader.cpp` to `src/CMakeLists.txt` sources.

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(tst_themeloader tst_themeloader.cpp
    ${CMAKE_SOURCE_DIR}/src/services/themeloader.cpp
)
target_include_directories(tst_themeloader PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/third_party
)
target_compile_definitions(tst_themeloader PRIVATE
    THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
)
target_link_libraries(tst_themeloader PRIVATE Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_themeloader COMMAND tst_themeloader)
```

- [ ] **Step 6: Run tests and verify they pass**

```bash
cmake --build build && cd build && ctest -R themeloader -V
```

Expected: All 3 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/services/themeloader.h src/services/themeloader.cpp \
    themes/catppuccin-mocha.toml tests/tst_themeloader.cpp \
    src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add ThemeLoader with Catppuccin Mocha default theme"
```

---

## Task 4: Tab Model

**Files:**
- Create: `src/models/tabmodel.h`
- Create: `src/models/tabmodel.cpp`
- Create: `src/models/tablistmodel.h`
- Create: `src/models/tablistmodel.cpp`
- Create: `tests/tst_tabmodel.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test for TabModel**

Create `tests/tst_tabmodel.cpp`:

```cpp
#include <QTest>
#include "models/tabmodel.h"
#include "models/tablistmodel.h"

class TestTabModel : public QObject
{
    Q_OBJECT

private slots:
    void testTabInitialState()
    {
        TabModel tab;
        QCOMPARE(tab.currentPath(), QDir::homePath());
        QCOMPARE(tab.viewMode(), QString("grid"));
        QCOMPARE(tab.canGoBack(), false);
        QCOMPARE(tab.canGoForward(), false);
    }

    void testNavigate()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        QCOMPARE(tab.currentPath(), QString("/tmp"));
        QCOMPARE(tab.canGoBack(), true);
        QCOMPARE(tab.canGoForward(), false);
    }

    void testBackForward()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");
        tab.goBack();
        QCOMPARE(tab.currentPath(), QString("/tmp"));
        QCOMPARE(tab.canGoForward(), true);
        tab.goForward();
        QCOMPARE(tab.currentPath(), QString("/usr"));
    }

    void testNavigateClearsForwardHistory()
    {
        TabModel tab;
        tab.navigateTo("/tmp");
        tab.navigateTo("/usr");
        tab.goBack();
        tab.navigateTo("/var");
        QCOMPARE(tab.canGoForward(), false);
    }

    void testViewMode()
    {
        TabModel tab;
        tab.setViewMode("list");
        QCOMPARE(tab.viewMode(), QString("list"));
        tab.setViewMode("detailed");
        QCOMPARE(tab.viewMode(), QString("detailed"));
    }

    void testTabListModelAddRemove()
    {
        TabListModel model;
        QCOMPARE(model.rowCount(), 1); // Starts with one tab

        model.addTab();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.activeIndex(), 1); // New tab becomes active

        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.activeIndex(), 0);
    }

    void testTabListModelCannotCloseLastTab()
    {
        TabListModel model;
        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1); // Still one tab -- closing last closes window
    }

    void testReopenClosedTab()
    {
        TabListModel model;
        model.activeTab()->navigateTo("/tmp");
        model.addTab();
        model.closeTab(0);
        QCOMPARE(model.rowCount(), 1);

        model.reopenClosedTab();
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.tabAt(1)->currentPath(), QString("/tmp"));
    }

    void testTabTitle()
    {
        TabModel tab;
        QCOMPARE(tab.title(), QDir::home().dirName());
        tab.navigateTo("/tmp");
        QCOMPARE(tab.title(), QString("tmp"));
        tab.navigateTo("/");
        QCOMPARE(tab.title(), QString("/"));
    }
};

QTEST_MAIN(TestTabModel)
#include "tst_tabmodel.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Expected: Build fails -- headers not found.

- [ ] **Step 3: Implement TabModel**

Create `src/models/tabmodel.h`:

```cpp
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
```

Create `src/models/tabmodel.cpp`:

```cpp
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
```

- [ ] **Step 4: Implement TabListModel**

Create `src/models/tablistmodel.h`:

```cpp
#pragma once

#include <QAbstractListModel>
#include <QList>
#include "models/tabmodel.h"

class TabListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex NOTIFY activeIndexChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        TabObjectRole,
    };

    explicit TabListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeIndex() const;
    void setActiveIndex(int index);

    TabModel *activeTab() const;
    TabModel *tabAt(int index) const;

    Q_INVOKABLE void addTab();
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void reopenClosedTab();

signals:
    void activeIndexChanged();
    void countChanged();
    void lastTabClosed();

private:
    QList<TabModel *> m_tabs;
    int m_activeIndex = 0;

    struct ClosedTabInfo {
        QString path;
        QString viewMode;
    };
    QList<ClosedTabInfo> m_closedTabs;
};
```

Create `src/models/tablistmodel.cpp`:

```cpp
#include "models/tablistmodel.h"

TabListModel::TabListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_tabs.append(new TabModel(this));
}

int TabListModel::rowCount(const QModelIndex &) const
{
    return m_tabs.size();
}

QVariant TabListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tabs.size())
        return {};

    TabModel *tab = m_tabs.at(index.row());
    switch (role) {
    case TitleRole: return tab->title();
    case PathRole: return tab->currentPath();
    case TabObjectRole: return QVariant::fromValue(tab);
    }
    return {};
}

QHash<int, QByteArray> TabListModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {PathRole, "path"},
        {TabObjectRole, "tabObject"},
    };
}

int TabListModel::activeIndex() const { return m_activeIndex; }

void TabListModel::setActiveIndex(int index)
{
    if (index >= 0 && index < m_tabs.size() && m_activeIndex != index) {
        m_activeIndex = index;
        emit activeIndexChanged();
    }
}

TabModel *TabListModel::activeTab() const
{
    if (m_activeIndex >= 0 && m_activeIndex < m_tabs.size())
        return m_tabs.at(m_activeIndex);
    return nullptr;
}

TabModel *TabListModel::tabAt(int index) const
{
    if (index >= 0 && index < m_tabs.size())
        return m_tabs.at(index);
    return nullptr;
}

void TabListModel::addTab()
{
    beginInsertRows(QModelIndex(), m_tabs.size(), m_tabs.size());
    auto *tab = new TabModel(this);
    m_tabs.append(tab);
    endInsertRows();
    setActiveIndex(m_tabs.size() - 1);
    emit countChanged();
}

void TabListModel::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;

    if (m_tabs.size() <= 1) {
        emit lastTabClosed();
        return;
    }

    TabModel *tab = m_tabs.at(index);
    m_closedTabs.append({tab->currentPath(), tab->viewMode()});

    beginRemoveRows(QModelIndex(), index, index);
    m_tabs.removeAt(index);
    tab->deleteLater();
    endRemoveRows();

    if (m_activeIndex >= m_tabs.size())
        setActiveIndex(m_tabs.size() - 1);
    else if (m_activeIndex == index && m_activeIndex > 0)
        setActiveIndex(m_activeIndex - 1);
    else
        emit activeIndexChanged();

    emit countChanged();
}

void TabListModel::reopenClosedTab()
{
    if (m_closedTabs.isEmpty())
        return;

    auto info = m_closedTabs.takeLast();

    beginInsertRows(QModelIndex(), m_tabs.size(), m_tabs.size());
    auto *tab = new TabModel(this);
    tab->navigateTo(info.path);
    tab->setViewMode(info.viewMode);
    m_tabs.append(tab);
    endInsertRows();
    setActiveIndex(m_tabs.size() - 1);
    emit countChanged();
}
```

- [ ] **Step 5: Update build files**

Add `models/tabmodel.cpp models/tablistmodel.cpp` to `src/CMakeLists.txt`.

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(tst_tabmodel tst_tabmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tabmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/models/tablistmodel.cpp
)
target_include_directories(tst_tabmodel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_tabmodel PRIVATE Qt6::Test Qt6::Core)
add_test(NAME tst_tabmodel COMMAND tst_tabmodel)
```

- [ ] **Step 6: Run tests and verify they pass**

```bash
cmake --build build && cd build && ctest -R tabmodel -V
```

Expected: All 9 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/models/ tests/tst_tabmodel.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add TabModel and TabListModel with navigation history"
```

---

## Task 5: Clipboard Manager

**Files:**
- Create: `src/services/clipboardmanager.h`
- Create: `src/services/clipboardmanager.cpp`
- Create: `tests/tst_clipboardmanager.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

Create `tests/tst_clipboardmanager.cpp`:

```cpp
#include <QTest>
#include "services/clipboardmanager.h"

class TestClipboardManager : public QObject
{
    Q_OBJECT

private slots:
    void testInitialState()
    {
        ClipboardManager mgr;
        QCOMPARE(mgr.hasContent(), false);
        QCOMPARE(mgr.isCut(), false);
    }

    void testCopy()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/a.txt", "/tmp/b.txt"});
        QCOMPARE(mgr.hasContent(), true);
        QCOMPARE(mgr.isCut(), false);
        QCOMPARE(mgr.paths().size(), 2);
    }

    void testCut()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt"});
        QCOMPARE(mgr.hasContent(), true);
        QCOMPARE(mgr.isCut(), true);
    }

    void testClearAfterPaste()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt"});
        QStringList paths = mgr.take();
        QCOMPARE(paths.size(), 1);
        QCOMPARE(mgr.hasContent(), false);
    }

    void testCopyDoesNotClearOnTake()
    {
        ClipboardManager mgr;
        mgr.copy({"/tmp/a.txt"});
        QStringList paths = mgr.take();
        QCOMPARE(paths.size(), 1);
        QCOMPARE(mgr.hasContent(), true);
    }

    void testContains()
    {
        ClipboardManager mgr;
        mgr.cut({"/tmp/a.txt", "/tmp/b.txt"});
        QCOMPARE(mgr.contains("/tmp/a.txt"), true);
        QCOMPARE(mgr.contains("/tmp/c.txt"), false);
    }
};

QTEST_MAIN(TestClipboardManager)
#include "tst_clipboardmanager.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Expected: Build fails.

- [ ] **Step 3: Implement ClipboardManager**

Create `src/services/clipboardmanager.h`:

```cpp
#pragma once

#include <QObject>
#include <QStringList>

class ClipboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY changed)
    Q_PROPERTY(bool isCut READ isCut NOTIFY changed)

public:
    explicit ClipboardManager(QObject *parent = nullptr);

    bool hasContent() const;
    bool isCut() const;
    QStringList paths() const;

    Q_INVOKABLE void copy(const QStringList &paths);
    Q_INVOKABLE void cut(const QStringList &paths);
    Q_INVOKABLE void clear();
    bool contains(const QString &path) const;
    QStringList take();

signals:
    void changed();

private:
    QStringList m_paths;
    bool m_isCut = false;
};
```

Create `src/services/clipboardmanager.cpp`:

```cpp
#include "services/clipboardmanager.h"

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardManager::hasContent() const { return !m_paths.isEmpty(); }
bool ClipboardManager::isCut() const { return m_isCut; }
QStringList ClipboardManager::paths() const { return m_paths; }

void ClipboardManager::copy(const QStringList &paths)
{
    m_paths = paths;
    m_isCut = false;
    emit changed();
}

void ClipboardManager::cut(const QStringList &paths)
{
    m_paths = paths;
    m_isCut = true;
    emit changed();
}

void ClipboardManager::clear()
{
    m_paths.clear();
    m_isCut = false;
    emit changed();
}

bool ClipboardManager::contains(const QString &path) const
{
    return m_paths.contains(path);
}

QStringList ClipboardManager::take()
{
    QStringList result = m_paths;
    if (m_isCut) {
        m_paths.clear();
        m_isCut = false;
        emit changed();
    }
    return result;
}
```

- [ ] **Step 4: Update build files and run tests**

```bash
cmake --build build && cd build && ctest -R clipboardmanager -V
```

Expected: All 6 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/services/clipboardmanager.h src/services/clipboardmanager.cpp \
    tests/tst_clipboardmanager.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add ClipboardManager for copy/cut file operations"
```

---

## Task 6: Bookmark Model

**Files:**
- Create: `src/models/bookmarkmodel.h`
- Create: `src/models/bookmarkmodel.cpp`
- Create: `tests/tst_bookmarkmodel.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

Create `tests/tst_bookmarkmodel.cpp`:

```cpp
#include <QTest>
#include "models/bookmarkmodel.h"

class TestBookmarkModel : public QObject
{
    Q_OBJECT

private slots:
    void testLoadBookmarks()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads"});
        QCOMPARE(model.rowCount(), 2);
    }

    void testBookmarkData()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents"});
        QModelIndex idx = model.index(0);
        QString name = model.data(idx, BookmarkModel::NameRole).toString();
        QCOMPARE(name, QString("Documents"));
    }

    void testExpandTilde()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents"});
        QModelIndex idx = model.index(0);
        QString path = model.data(idx, BookmarkModel::PathRole).toString();
        QVERIFY(path.startsWith("/"));
        QVERIFY(path.endsWith("/Documents"));
    }

    void testIconForKnownPaths()
    {
        BookmarkModel model;
        model.setBookmarks({"~/Documents", "~/Downloads", "~/Pictures", "~/Music"});
        QModelIndex idx0 = model.index(0);
        QString icon = model.data(idx0, BookmarkModel::IconRole).toString();
        QVERIFY(!icon.isEmpty());
    }
};

QTEST_MAIN(TestBookmarkModel)
#include "tst_bookmarkmodel.moc"
```

- [ ] **Step 2: Implement BookmarkModel**

Create `src/models/bookmarkmodel.h`:

```cpp
#pragma once

#include <QAbstractListModel>
#include <QStringList>

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IconRole,
    };

    explicit BookmarkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBookmarks(const QStringList &paths);

private:
    struct Bookmark {
        QString name;
        QString path;
        QString icon;
    };

    QList<Bookmark> m_bookmarks;
    static QString expandPath(const QString &path);
    static QString iconForPath(const QString &name);
};
```

Create `src/models/bookmarkmodel.cpp`:

```cpp
#include "models/bookmarkmodel.h"
#include <QDir>

BookmarkModel::BookmarkModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BookmarkModel::rowCount(const QModelIndex &) const
{
    return m_bookmarks.size();
}

QVariant BookmarkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_bookmarks.size())
        return {};

    const auto &bm = m_bookmarks.at(index.row());
    switch (role) {
    case NameRole: return bm.name;
    case PathRole: return bm.path;
    case IconRole: return bm.icon;
    }
    return {};
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {IconRole, "icon"},
    };
}

void BookmarkModel::setBookmarks(const QStringList &paths)
{
    beginResetModel();
    m_bookmarks.clear();
    for (const auto &p : paths) {
        QString expanded = expandPath(p);
        QDir dir(expanded);
        QString name = dir.dirName();
        m_bookmarks.append({name, expanded, iconForPath(name.toLower())});
    }
    endResetModel();
}

QString BookmarkModel::expandPath(const QString &path)
{
    if (path.startsWith("~/"))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString BookmarkModel::iconForPath(const QString &name)
{
    static const QMap<QString, QString> icons = {
        {"home", "go-home"},
        {"documents", "folder-documents"},
        {"downloads", "folder-download"},
        {"pictures", "folder-pictures"},
        {"music", "folder-music"},
        {"videos", "folder-videos"},
        {"desktop", "user-desktop"},
        {"projects", "folder-development"},
    };
    return icons.value(name, "folder");
}
```

- [ ] **Step 3: Update build files, run tests**

```bash
cmake --build build && cd build && ctest -R bookmarkmodel -V
```

Expected: All 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/models/bookmarkmodel.h src/models/bookmarkmodel.cpp \
    tests/tst_bookmarkmodel.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add BookmarkModel with path expansion and icons"
```

---

## Task 7: File Operations Service

**Files:**
- Create: `src/services/fileoperations.h`
- Create: `src/services/fileoperations.cpp`
- Create: `tests/tst_fileoperations.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

Create `tests/tst_fileoperations.cpp`:

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include "services/fileoperations.h"

class TestFileOperations : public QObject
{
    Q_OBJECT

private slots:
    void testCopyFile()
    {
        QTemporaryDir srcDir, dstDir;
        QFile f(srcDir.path() + "/test.txt");
        f.open(QIODevice::WriteOnly);
        f.write("hello");
        f.close();

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.copyFiles({srcDir.path() + "/test.txt"}, dstDir.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dstDir.path() + "/test.txt"));
    }

    void testMoveFile()
    {
        QTemporaryDir srcDir, dstDir;
        QString srcPath = srcDir.path() + "/test.txt";
        QFile f(srcPath);
        f.open(QIODevice::WriteOnly);
        f.write("hello");
        f.close();

        FileOperations ops;
        QSignalSpy spy(&ops, &FileOperations::operationFinished);

        ops.moveFiles({srcPath}, dstDir.path());

        QVERIFY(spy.wait(5000));
        QVERIFY(QFile::exists(dstDir.path() + "/test.txt"));
        QVERIFY(!QFile::exists(srcPath));
    }

    void testRenameFile()
    {
        QTemporaryDir dir;
        QString oldPath = dir.path() + "/old.txt";
        QFile f(oldPath);
        f.open(QIODevice::WriteOnly);
        f.write("rename me");
        f.close();

        FileOperations ops;
        bool result = ops.rename(oldPath, "new.txt");

        QVERIFY(result);
        QVERIFY(QFile::exists(dir.path() + "/new.txt"));
        QVERIFY(!QFile::exists(oldPath));
    }
};

QTEST_MAIN(TestFileOperations)
#include "tst_fileoperations.moc"
```

- [ ] **Step 2: Implement FileOperations**

Create `src/services/fileoperations.h`:

```cpp
#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

class FileOperations : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit FileOperations(QObject *parent = nullptr);

    bool busy() const;
    double progress() const;
    QString statusText() const;

    Q_INVOKABLE void copyFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void moveFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void trashFiles(const QStringList &paths);
    Q_INVOKABLE void deleteFiles(const QStringList &paths);
    Q_INVOKABLE bool rename(const QString &path, const QString &newName);
    Q_INVOKABLE void createFolder(const QString &parentPath, const QString &name);
    Q_INVOKABLE void createFile(const QString &parentPath, const QString &name);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void copyPathToClipboard(const QString &path);

signals:
    void busyChanged();
    void progressChanged(double progress, const QString &speed, const QString &eta);
    void statusTextChanged();
    void operationFinished(bool success, const QString &error);

private:
    void runProcess(const QString &program, const QStringList &args);
    void parseRsyncProgress(const QByteArray &data);

    QProcess *m_process = nullptr;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusText;
};
```

Create `src/services/fileoperations.cpp`:

```cpp
#include "services/fileoperations.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

FileOperations::FileOperations(QObject *parent)
    : QObject(parent)
{
}

bool FileOperations::busy() const { return m_busy; }
double FileOperations::progress() const { return m_progress; }
QString FileOperations::statusText() const { return m_statusText; }

void FileOperations::copyFiles(const QStringList &sources, const QString &destination)
{
    QStringList args = {"--progress", "-r"};
    for (const auto &src : sources)
        args.append(src);
    args.append(destination + "/");

    m_statusText = QString("Copying %1 item(s)...").arg(sources.size());
    emit statusTextChanged();
    runProcess("rsync", args);
}

void FileOperations::moveFiles(const QStringList &sources, const QString &destination)
{
    QStringList args = {"--progress", "--remove-source-files", "-r"};
    for (const auto &src : sources)
        args.append(src);
    args.append(destination + "/");

    m_statusText = QString("Moving %1 item(s)...").arg(sources.size());
    emit statusTextChanged();
    runProcess("rsync", args);
}

void FileOperations::trashFiles(const QStringList &paths)
{
    QStringList args = {"trash"};
    args.append(paths);
    m_statusText = QString("Trashing %1 item(s)...").arg(paths.size());
    emit statusTextChanged();
    runProcess("gio", args);
}

void FileOperations::deleteFiles(const QStringList &paths)
{
    for (const auto &path : paths) {
        QFileInfo info(path);
        if (info.isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
    }
    emit operationFinished(true, QString());
}

bool FileOperations::rename(const QString &path, const QString &newName)
{
    QFileInfo info(path);
    QString newPath = info.dir().filePath(newName);
    return QFile::rename(path, newPath);
}

void FileOperations::createFolder(const QString &parentPath, const QString &name)
{
    QDir(parentPath).mkdir(name);
}

void FileOperations::createFile(const QString &parentPath, const QString &name)
{
    QFile f(QDir(parentPath).filePath(name));
    f.open(QIODevice::WriteOnly);
    f.close();
}

void FileOperations::openFile(const QString &path)
{
    auto *proc = new QProcess(this);
    proc->start("xdg-open", {path});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

void FileOperations::copyPathToClipboard(const QString &path)
{
    auto *proc = new QProcess(this);
    proc->start("wl-copy", {path});
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            proc, &QProcess::deleteLater);
}

void FileOperations::runProcess(const QString &program, const QStringList &args)
{
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_busy = true;
    m_progress = 0.0;
    emit busyChanged();

    m_process = new QProcess(this);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        parseRsyncProgress(m_process->readAllStandardOutput());
    });

    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        m_busy = false;
        m_progress = 1.0;
        m_statusText.clear();
        emit busyChanged();
        emit statusTextChanged();
        emit operationFinished(exitCode == 0,
            exitCode != 0 ? QString::fromUtf8(m_process->readAllStandardError()) : QString());
        m_process->deleteLater();
        m_process = nullptr;
    });

    m_process->start(program, args);
}

void FileOperations::parseRsyncProgress(const QByteArray &data)
{
    static QRegularExpression re(R"((\d+)%\s+(\S+/s))");
    QString text = QString::fromUtf8(data);
    auto match = re.match(text);
    if (match.hasMatch()) {
        m_progress = match.captured(1).toDouble() / 100.0;
        QString speed = match.captured(2);
        emit progressChanged(m_progress, speed, QString());
    }
}
```

- [ ] **Step 3: Update build files, run tests**

```bash
cmake --build build && cd build && ctest -R fileoperations -V
```

Expected: All 3 tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/services/fileoperations.h src/services/fileoperations.cpp \
    tests/tst_fileoperations.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add FileOperations service with rsync copy and gio trash"
```

---

## Task 8: FileSystem Model Wrapper

**Files:**
- Create: `src/models/filesystemmodel.h`
- Create: `src/models/filesystemmodel.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Implement FileSystemModel wrapper**

This wraps QFileSystemModel in a QSortFilterProxyModel, adding custom roles (fileName, filePath, fileSize, fileSizeText, fileType, fileModified, fileModifiedText, filePermissions, isDir, isSymlink) for QML consumption. Includes hidden file filtering and item counting.

See spec for full implementation. Key features:
- Custom roles exposed via roleNames()
- `setRootPath(path)` to change directory
- `setShowHidden(bool)` to toggle hidden files
- `fileCount` and `folderCount` properties
- Helper methods: `filePath(row)`, `isDir(row)`, `fileName(row)`

- [ ] **Step 2: Update build files, build and verify**

```bash
cmake --build build
```

Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add src/models/filesystemmodel.h src/models/filesystemmodel.cpp src/CMakeLists.txt
git commit -m "feat: add FileSystemModel wrapper with custom roles and filtering"
```

---

## Task 9: Register C++ Types and Wire Up main.cpp

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Update main.cpp to register all backend types with QML engine**

Import all headers. Create instances of ConfigManager, ThemeLoader, TabListModel, BookmarkModel, FileOperations, ClipboardManager, FileSystemModel. Set context properties on the QML engine. Connect config changes to reload theme and bookmarks.

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: register all C++ types with QML engine"
```

---

## Task 10: QML Theme Singleton and Main Layout

**Files:**
- Create: `src/qml/theme/Theme.qml`
- Create: `src/qml/components/StatusBar.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create Theme.qml singleton**

QML singleton binding to C++ ThemeLoader properties. Also defines layout constants (radiusSmall/Medium/Large, spacing, font sizes).

- [ ] **Step 2: Create StatusBar.qml**

Bottom bar showing item count (folders + files) and selection info.

- [ ] **Step 3: Update main.qml with layout scaffold**

ColumnLayout with: TabBar placeholder, Toolbar placeholder, RowLayout (Sidebar placeholder + FileView placeholder), StatusBar.

- [ ] **Step 4: Build and verify**

```bash
cmake --build build && ./build/src/hyprfm
```

Expected: Dark window with placeholder areas and functioning StatusBar.

- [ ] **Step 5: Commit**

```bash
git add src/qml/ src/CMakeLists.txt
git commit -m "feat: add QML layout scaffold with Theme singleton and StatusBar"
```

---

## Task 11: Sidebar Component

**Files:**
- Create: `src/qml/components/Sidebar.qml`
- Create: `src/qml/components/OperationsBar.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create OperationsBar.qml**

Shows when `fileOps.busy` is true. Progress bar bound to `fileOps.progress`. Status text from `fileOps.statusText`.

- [ ] **Step 2: Create Sidebar.qml**

Contains: Bookmarks section (Repeater over bookmarks model with hover/active states, emoji icons), Devices section (placeholder for Task 24), OperationsBar anchored at bottom. Emits `bookmarkClicked(path)` signal. Supports `currentPath` property for active highlight.

- [ ] **Step 3: Wire into main.qml**

Replace sidebar placeholder. Connect bookmark clicks to tab navigation.

- [ ] **Step 4: Build and verify**

```bash
cmake --build build && ./build/src/hyprfm
```

Expected: Sidebar with clickable bookmarks. Operations bar hidden (no active operations).

- [ ] **Step 5: Commit**

```bash
git add src/qml/components/Sidebar.qml src/qml/components/OperationsBar.qml \
    src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add Sidebar with bookmarks and operations progress bar"
```

---

## Task 12: Tab Bar Component

**Files:**
- Create: `src/qml/components/FileTabBar.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create FileTabBar.qml**

Browser-style tabs. Repeater over tabModel. Active tab blends with content area. Close button per tab (hover turns red). `+` button to add tabs. Middle-click to close. Supports tab switching on click.

- [ ] **Step 2: Wire into main.qml**

Replace tab bar placeholder.

- [ ] **Step 3: Build and verify**

Expected: Tab bar with one tab. `+` adds tabs. `x` closes. Click switches.

- [ ] **Step 4: Commit**

```bash
git add src/qml/components/FileTabBar.qml src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add FileTabBar component with add/close/switch"
```

---

## Task 13: Toolbar and Breadcrumb

**Files:**
- Create: `src/qml/components/Toolbar.qml`
- Create: `src/qml/components/Breadcrumb.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create Breadcrumb.qml**

Clickable path segments. Double-click or Ctrl+L enters edit mode with TextInput. Escape cancels edit. Enter navigates to typed path.

- [ ] **Step 2: Create Toolbar.qml**

Contains: back/forward buttons (disabled state when no history), Breadcrumb, view mode toggle (grid/list/detailed with active indicator).

- [ ] **Step 3: Wire into main.qml**

Connect toolbar signals to active tab navigation and view mode changes.

- [ ] **Step 4: Build and verify**

Expected: Breadcrumb shows path. Navigation buttons work. View toggle switches modes.

- [ ] **Step 5: Commit**

```bash
git add src/qml/components/Toolbar.qml src/qml/components/Breadcrumb.qml \
    src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add Toolbar with breadcrumb navigation and view mode toggle"
```

---

## Task 14: File Grid View

**Files:**
- Create: `src/qml/views/FileViewContainer.qml`
- Create: `src/qml/views/FileGridView.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create FileGridView.qml**

GridView with elastic overscroll (Flickable rebound with Easing.OutBack). Delegates show folder/file icons with names. Selection support: click to select, Ctrl+click to toggle, Shift+click for range. Right-click emits contextMenuRequested. Double-click emits fileActivated.

- [ ] **Step 2: Create FileViewContainer.qml**

Switches between grid/list/detailed based on viewMode property. Placeholders for list and detailed views.

- [ ] **Step 3: Wire into main.qml**

Connect to fsModel. Handle fileActivated to navigate into directories or open files.

- [ ] **Step 4: Build and verify**

Expected: Home directory files in grid. Click folders to navigate. Breadcrumb updates. Back works.

- [ ] **Step 5: Commit**

```bash
git add src/qml/views/ src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add FileGridView with selection and elastic scrolling"
```

---

## Task 15: File List View

**Files:**
- Create: `src/qml/views/FileListView.qml`
- Modify: `src/qml/views/FileViewContainer.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create FileListView.qml**

ListView with elastic overscroll. Rows: icon, name, size, date. Same selection and interaction patterns as grid view.

- [ ] **Step 2: Wire into FileViewContainer.qml**

- [ ] **Step 3: Build and verify**

Expected: List view shows files in rows with name, size, date.

- [ ] **Step 4: Commit**

```bash
git add src/qml/views/FileListView.qml src/qml/views/FileViewContainer.qml src/CMakeLists.txt
git commit -m "feat: add FileListView with simple rows"
```

---

## Task 16: File Detailed View

**Files:**
- Create: `src/qml/views/FileDetailedView.qml`
- Modify: `src/qml/views/FileViewContainer.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create FileDetailedView.qml**

Header row with sortable columns (Name, Size, Modified, Type, Permissions). Click header to sort. Arrow indicator shows sort direction. ListView below with alternating row colors. Same selection patterns.

- [ ] **Step 2: Wire into FileViewContainer.qml**

Connect sort signals to fsModel.

- [ ] **Step 3: Build and verify**

Expected: Detailed view with sortable columns.

- [ ] **Step 4: Commit**

```bash
git add src/qml/views/FileDetailedView.qml src/qml/views/FileViewContainer.qml src/CMakeLists.txt
git commit -m "feat: add FileDetailedView with sortable columns"
```

---

## Task 17: Context Menu

**Files:**
- Create: `src/qml/components/ContextMenu.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create ContextMenu.qml**

Qt Quick Controls Menu with items: Open, Cut, Copy, Paste, Copy Path, Rename, Trash, Delete, Open in Terminal, New Folder, New File, Properties. Styled with Catppuccin theme. Includes Instantiator for custom actions from config. Context-aware visibility based on file/folder/empty space.

- [ ] **Step 2: Wire into main.qml**

Connect view contextMenuRequested signals to open the menu. Connect menu action signals to file operations.

- [ ] **Step 3: Build and verify**

Expected: Right-click shows context menu. Actions work.

- [ ] **Step 4: Commit**

```bash
git add src/qml/components/ContextMenu.qml src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add context menu with custom actions support"
```

---

## Task 18: Keyboard Shortcuts

**Files:**
- Modify: `src/qml/main.qml`

- [ ] **Step 1: Add Shortcut items for all configured shortcuts**

Shortcut items for: new_tab, close_tab, reopen_tab, back, forward, parent, toggle_hidden, path_bar, toggle_sidebar, grid_view, list_view, detailed_view, copy, cut, paste, trash, select_all. Each reads its key sequence from `config.shortcut()`.

- [ ] **Step 2: Build and verify**

Expected: All keyboard shortcuts work.

- [ ] **Step 3: Commit**

```bash
git add src/qml/main.qml
git commit -m "feat: add configurable keyboard shortcuts"
```

---

## Task 19: Quick Preview Overlay

**Files:**
- Create: `src/qml/components/QuickPreview.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create QuickPreview.qml**

Full-window overlay with semi-transparent backdrop. Centered card with: header (filename + close button), content area (Image for images, scrollable Text for text files, "not available" for others). Fade-in animation. Close on Escape or backdrop click.

- [ ] **Step 2: Add spacebar shortcut**

Toggle preview on selected file.

- [ ] **Step 3: Build and verify**

Expected: Select file, Space opens preview. Space/Escape closes.

- [ ] **Step 4: Commit**

```bash
git add src/qml/components/QuickPreview.qml src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add spacebar quick preview overlay for images and text"
```

---

## Task 20: Thumbnail Provider

**Files:**
- Create: `src/providers/thumbnailprovider.h`
- Create: `src/providers/thumbnailprovider.cpp`
- Modify: `src/main.cpp`
- Modify: `src/qml/views/FileGridView.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Implement ThumbnailProvider**

QQuickAsyncImageProvider that loads images in a thread pool, scales to requested size, returns via QQuickImageResponse.

- [ ] **Step 2: Register in main.cpp**

```cpp
engine.addImageProvider("thumbnail", new ThumbnailProvider);
```

- [ ] **Step 3: Update FileGridView to show thumbnails**

For image files, use `source: "image://thumbnail/" + model.filePath` instead of emoji icon.

- [ ] **Step 4: Build and verify**

Expected: Image files show thumbnails in grid view.

- [ ] **Step 5: Commit**

```bash
git add src/providers/ src/main.cpp src/qml/views/FileGridView.qml src/CMakeLists.txt
git commit -m "feat: add async ThumbnailProvider for image thumbnails"
```

---

## Task 21: Toast Notifications

**Files:**
- Create: `src/qml/components/Toast.qml`
- Modify: `src/qml/main.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create Toast.qml**

Anchored bottom-right. `show(message, type)` function creates a toast rectangle with auto-dismiss timer (3s) and fade-out animation.

- [ ] **Step 2: Wire into main.qml**

Connect `fileOps.operationFinished` to show success/error toasts.

- [ ] **Step 3: Build and verify**

Expected: Toasts appear and auto-dismiss.

- [ ] **Step 4: Commit**

```bash
git add src/qml/components/Toast.qml src/qml/main.qml src/CMakeLists.txt
git commit -m "feat: add toast notification system"
```

---

## Task 22: Drag and Drop

**Files:**
- Modify: `src/qml/views/FileGridView.qml`
- Modify: `src/qml/views/FileListView.qml`
- Modify: `src/qml/views/FileDetailedView.qml`
- Modify: `src/qml/components/FileTabBar.qml`
- Modify: `src/qml/main.qml`

- [ ] **Step 1: Add Drag support to view delegates**

Add Drag properties and DragHandler to file item delegates in all three views.

- [ ] **Step 2: Add DropArea to file views**

Accept drops, determine copy vs move, invoke fileOps.

- [ ] **Step 3: Add DropArea to tab headers**

Drop files onto a tab header to copy/move to that tab's directory.

- [ ] **Step 4: Build and verify**

Expected: Drag files between folders and tabs.

- [ ] **Step 5: Commit**

```bash
git add src/qml/views/ src/qml/components/FileTabBar.qml src/qml/main.qml
git commit -m "feat: add drag and drop between views and tabs"
```

---

## Task 23: Rubber-Band Selection

**Files:**
- Create: `src/qml/components/RubberBand.qml`
- Modify: `src/qml/views/FileGridView.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Create RubberBand.qml**

Semi-transparent accent-colored rectangle with border. Methods: `begin(pos)`, `update(pos)`, `end()`. Tracks selection rectangle.

- [ ] **Step 2: Integrate into FileGridView**

MouseArea on empty space triggers rubber-band. Items whose bounds intersect the rectangle are selected.

- [ ] **Step 3: Build and verify**

Expected: Click-drag on empty space shows selection rectangle, selects intersecting items.

- [ ] **Step 4: Commit**

```bash
git add src/qml/components/RubberBand.qml src/qml/views/FileGridView.qml src/CMakeLists.txt
git commit -m "feat: add rubber-band selection in grid view"
```

---

## Task 24: Device Monitor (UDisks2)

**Files:**
- Create: `src/models/devicemodel.h`
- Create: `src/models/devicemodel.cpp`
- Modify: `src/main.cpp`
- Modify: `src/qml/components/Sidebar.qml`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Implement DeviceModel**

Uses QStorageInfo for mounted volumes. Filters out virtual filesystems (tmpfs, proc, sysfs, etc.). Each device exposes: name, mountPoint, totalSize, freeSpace, usagePercent, removable, mounted. Connects to UDisks2 DBus signals for auto-refresh on plug/unplug. Mount/unmount via `udisksctl`.

- [ ] **Step 2: Register in main.cpp**

- [ ] **Step 3: Update Sidebar.qml devices section**

Replace placeholder with Repeater over devices model. Each device shows: icon, name, eject button (removable only), storage bar (blue < 75%, yellow 75-89%, red 90%+), "X free of Y" text.

- [ ] **Step 4: Build and verify**

Expected: Sidebar shows mounted devices with storage bars.

- [ ] **Step 5: Commit**

```bash
git add src/models/devicemodel.h src/models/devicemodel.cpp \
    src/main.cpp src/qml/components/Sidebar.qml src/CMakeLists.txt
git commit -m "feat: add DeviceModel with UDisks2 monitoring and storage bars"
```

---

## Task 25: Final Integration and Polish

**Files:**
- Modify: `src/qml/main.qml`
- Modify: various QML files

- [ ] **Step 1: Wire tab switching to filesystem model**

When active tab changes, update fsModel.rootPath. When navigating within a tab, update fsModel.rootPath.

- [ ] **Step 2: Connect selection state to StatusBar**

Pass selected count and total size.

- [ ] **Step 3: Handle window close on last tab**

Connect `tabModel.lastTabClosed` to `Qt.quit()`.

- [ ] **Step 4: Add missing CLI tool check on startup**

Check for rsync, gio, xdg-open, wl-copy. Log warnings for missing tools.

- [ ] **Step 5: Build full app, run all tests**

```bash
cmake --build build && cd build && ctest -V
./build/src/hyprfm
```

Expected: All tests pass. Full functionality.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: final integration -- wire all components together"
```

---

## Task 26: Manual Verification Checklist

- [ ] Grid view, list view, detailed view toggle
- [ ] Tab create, close, switch, reopen
- [ ] Sidebar bookmarks navigate correctly
- [ ] Sidebar devices show with storage bars
- [ ] Breadcrumb navigation (click segments, double-click to edit)
- [ ] Back/forward navigation
- [ ] Copy, cut, paste files
- [ ] Rename with F2
- [ ] Trash with Delete key
- [ ] Right-click context menu with all items
- [ ] Custom context menu actions from config
- [ ] Keyboard shortcuts (Ctrl+T, Ctrl+W, Ctrl+H, Ctrl+L, Ctrl+1/2/3, etc.)
- [ ] Spacebar quick preview (image, text)
- [ ] Image thumbnails in grid view
- [ ] Drag and drop between directories
- [ ] Elastic scrolling with touchpad
- [ ] Status bar item count
- [ ] Operations progress bar in sidebar
- [ ] Config hot-reload
- [ ] Custom theme loads correctly

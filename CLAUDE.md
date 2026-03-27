# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Build
cmake -B build && cmake --build build

# Run
./build/src/hyprfm

# Run all tests
ctest --test-dir build

# Run a single test
./build/tests/tst_configmanager
```

Tests use Qt6::Test (QCOMPARE, QSignalSpy). Test sources are in `tests/tst_*.cpp` — one per backend class.

## Architecture

HyprFM is a Qt6/QML file manager with three layers:

**QML Frontend** (`src/qml/`) — All rendering. `Main.qml` is the root that wires tab state, selection, and keyboard shortcuts. `FileViewContainer.qml` switches between grid/list/detailed views. `Theme.qml` is a QML singleton providing colors from the active TOML theme.

**C++ Backend** (`src/models/`, `src/services/`, `src/providers/`) — Exposed to QML as context properties set in `main.cpp`. Models (FileSystemModel, TabListModel, BookmarkModel, DeviceModel) are all QAbstractListModel subclasses with custom roles. Services (ConfigManager, ThemeLoader, FileOperations, ClipboardManager) manage state and async operations.

**System Layer** — FileOperations spawns rsync/gio/xdg-open via QProcess. DeviceModel monitors UDisks2 over DBus. Assumes Wayland (wl-copy for clipboard).

### Data flow

QML action → Q_INVOKABLE C++ method → model property change → QML property binding re-renders view. FileSystemModel watches directories via QFileSystemWatcher for automatic reload.

### Key conventions

- Models expose data via `roleNames()` mapping enums to QML-accessible names (e.g., `FileNameRole` → `"fileName"`)
- QML components communicate upward via signals (fileActivated, contextMenuRequested), downward via property bindings
- Config lives at `~/.config/hyprfm/config.toml` (TOML format); theme files in `themes/`
- All async file I/O through QProcess to avoid blocking the GUI thread

## Commit Rules

Never add Co-Authored-By lines to commits.

## Shared Submodules

- `src/qml/icons/` → [quill-icons](https://github.com/soyeb-jim285/quill-icons) — 60 PathSvg icons (Lucide-derived, ISC/MIT)
- `src/qml/Quill/` → [quill](https://github.com/soyeb-jim285/quill) — Themed QML component library (Button, TextField, Card, Tabs, Dropdown, etc.)

Quill's `Theme.qml` singleton is bridged from HyprFM's theme in `Main.qml` `Component.onCompleted`. The directory must be uppercase `Quill/` to match the QML module name.

## Packaging & Distribution

- **AUR**: `PKGBUILD` in repo root, also maintained at `~/hyprfm-aur/` (ssh://aur@aur.archlinux.org/hyprfm-git.git)
- **AppImage**: GitHub Actions builds on `v*` tags (`.github/workflows/build.yml`)
- **Desktop entry + icon**: `dist/hyprfm.desktop`, `dist/hyprfm.svg`

### AUR vs GitHub repo

The AUR repo (`~/hyprfm-aur/`) only contains `PKGBUILD` + `.SRCINFO` — build instructions, not source code. The PKGBUILD clones from GitHub at build time, so `yay -S hyprfm-git` always gets the latest `main`. Only update the AUR repo when dependencies, build steps, or install paths change — not for code changes.

### HYPRFM_DATA_DIR

`HYPRFM_DATA_DIR` (CMake cache var) controls where the binary finds themes and QML at runtime. Defaults to `CMAKE_SOURCE_DIR` for dev. PKGBUILD sets it to `/usr/share/hyprfm`. Separate from `HYPRFM_SOURCE_DIR` which is always the build source dir (needed for `loadFromModule`).

## Dependencies

Qt6 modules: Core, Gui, Qml, Quick, QuickControls2, DBus, Widgets, Svg, SvgWidgets. TOML parsing via header-only `third_party/toml.hpp`. Runtime CLI tools: rsync, gio, xdg-open, wl-copy (optional; warns if missing).

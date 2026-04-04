# HyprFM

A lightweight Qt6/QML file manager built for Hyprland.
![License](https://img.shields.io/github/license/soyeb-jim285/hyprfm)
![Release](https://img.shields.io/github/v/release/soyeb-jim285/hyprfm)

## Features

- Grid, list, and detailed views with Ctrl+scroll zoom
- Tabs with independent history
- Quick preview (spacebar)
- Drag & drop file operations with progress tracking
- Properties dialog with permissions editor and Open With
- Context menu with app icons
- TOML-based theming (Catppuccin Mocha default)
- Configurable keyboard shortcuts
- Sidebar with bookmarks
- Wayland-native clipboard (wl-copy)

## Installation

### AUR (Arch Linux)

```bash
yay -S hyprfm-git
```

### .deb (Ubuntu 24.04)

Download the `.deb` from [Releases](https://github.com/soyeb-jim285/hyprfm/releases) and install:

```bash
sudo apt install ./hyprfm_*.deb
```

### AppImage

Download from [Releases](https://github.com/soyeb-jim285/hyprfm/releases).

```bash
chmod +x HyprFM-*.AppImage
./HyprFM-*.AppImage
```

### Build from source

```bash
git clone --recursive https://github.com/soyeb-jim285/hyprfm.git
cd hyprfm
cmake -B build
cmake --build build
./build/src/hyprfm
```

#### Dependencies

| | Packages |
|---|---|
| **Build** | cmake, qt6-base, qt6-declarative, qt6-svg |
| **Runtime** | qt6-base, qt6-declarative, qt6-svg, qt6-wayland, fd, rsync, xdg-utils |
| **Optional** | wl-clipboard (clipboard via wl-copy/wl-paste), glib2 (gio file ops) |

## Configuration

Config file: `~/.config/hyprfm/config.toml`

```toml
[general]
theme = "catppuccin-mocha"
icon_theme = "Adwaita"
builtin_icons = true
default_view = "grid"          # grid, list, detailed
show_hidden = false
sort_by = "name"
sort_ascending = true

[sidebar]
position = "left"
width = 200
visible = true

[appearance]
radius_small = 4
radius_medium = 8
radius_large = 12

[bookmarks]
paths = ["~/Documents", "~/Downloads", "~/Pictures", "~/Projects"]

[shortcuts]
# Override any shortcut (see table below)
# rename = "F2"
```

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Return` | Open |
| `Space` | Quick preview |
| `F2` | Rename |
| `Delete` | Move to trash |
| `Shift+Delete` | Permanent delete |
| `Ctrl+C` | Copy |
| `Ctrl+X` | Cut |
| `Ctrl+V` | Paste |
| `Ctrl+A` | Select all |
| `Ctrl+H` | Toggle hidden files |
| `Ctrl+L` | Focus path bar |
| `Ctrl+T` | New tab |
| `Ctrl+W` | Close tab |
| `Ctrl+Shift+T` | Reopen closed tab |
| `Ctrl+Shift+N` | New folder |
| `Ctrl+Alt+N` | New file |
| `Ctrl+1/2/3` | Grid / List / Detailed view |
| `Ctrl+Scroll` | Zoom grid icons |
| `Alt+Left` | Back |
| `Alt+Right` | Forward |
| `Alt+Up` | Parent directory |
| `F9` | Toggle sidebar |

## Theming

Themes are TOML files in `themes/`. Create your own:

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
```

Set it in config: `theme = "my-theme"` (filename without `.toml`).

## License

MIT

# HyprFM — Design Specification

## Overview

HyprFM is a lightweight, keyboard-driven file manager built with Qt6/QML for the Hyprland/Wayland ecosystem. It aims to combine Thunar's speed, Nautilus's polished UI, and Dolphin's feature depth — without pulling in heavy desktop environment dependencies.

## Architecture

Pure Qt6 QML frontend with a thin C++ backend. Three layers:

### QML Frontend
All UI components rendered in QML:
- TabBar, Sidebar, Breadcrumb, GridView, ListView, DetailedView
- QuickPreview overlay, ContextMenu, OperationsBar, ThemeEngine

### C++ Backend
Thin layer exposing models and services to QML via Q_PROPERTY and Q_INVOKABLE:
- **FileSystemModel** — wraps QFileSystemModel for directory listing and filesystem watching
- **TabManager** — manages per-tab state (directory, history, view mode, selection, scroll position)
- **BookmarkManager** — reads/writes user bookmarks from config
- **FileOperations** — async copy/move/delete via QProcess, emits progress signals
- **DeviceMonitor** — monitors mounted devices via UDisks2 DBus interface
- **ThemeLoader** — parses TOML theme files, exposes color properties to QML
- **ThumbnailProvider** — async image thumbnail generation via thread pool
- **PreviewProvider** — generates previews for quick preview overlay (images, text, PDF)

### System Layer
CLI tools invoked via QProcess:

| Task | Tool |
|------|------|
| Copy with progress | rsync --progress |
| Trash | gio trash |
| Mount/unmount | udisksctl |
| Open files | xdg-open |
| Search | fd |
| File type detection | file --mime-type |
| Archives | 7z, tar, unzip |
| Clipboard | wl-copy / wl-paste |

Communication:
- QML ↔ C++: Q_PROPERTY bindings and Q_INVOKABLE calls
- C++ ↔ System: QProcess for CLI tools, DBus for UDisks2

## UI Layout

### Visual Direction
Polished modern style — Nautilus-inspired warmth with Catppuccin theming. Rounded corners, layered surfaces, subtle depth. A proper desktop app with personality.

### Window Structure (top to bottom)

1. **Tab Bar** — Browser-style tabs at the top. Each tab shows the current folder name and a close button. Active tab blends into the content area. `+` button for new tabs.

2. **Toolbar** — Back/forward navigation buttons, breadcrumb path bar (clickable segments, Ctrl+L to type a path), view mode toggle (grid/list/detailed), search button.

3. **Main Area** — Split into:
   - **Sidebar** (~200px, configurable left/right position): Bookmarks section + Devices section with storage usage bars and eject buttons. Operations progress bar anchored at the bottom.
   - **Content Area** (fills remaining): File view — grid, list, or detailed mode.

4. **Status Bar** — Bottom strip showing item count and selection info.

### Sidebar

**Bookmarks section:**
- Default entries: Home, Downloads, Documents, Pictures
- Users add custom bookmarks via config or drag-and-drop
- Active bookmark highlighted with accent color background

**Devices section:**
- Lists mounted devices (internal drives, USB, external disks)
- Each device shows: icon, name, eject button (for removable)
- Storage usage bar below each device with color coding:
  - Blue (accent): 0–74% usage
  - Yellow (warning): 75–89% usage
  - Red (error): 90%+ usage
- Text below bar: "X free of Y"
- Devices detected via UDisks2 DBus, auto-updates on plug/unplug

**Operations progress:**
- Anchored at sidebar bottom
- Shows active file operations with progress bar, transfer speed, sizes
- Visible regardless of which tab is active

**Sidebar configuration:**
```toml
[sidebar]
position = "left"    # "left" or "right"
width = 200          # pixels, resizable by dragging edge
visible = true       # toggle with keyboard shortcut
```

### View Modes

**Grid view:** Icon tiles in a responsive grid. Folders and files with icons/thumbnails. Selected items highlighted with accent border.

**List view:** Simple rows — icon, name, size, date. Clean and dense like Thunar.

**Detailed view:** Sortable columns — name, size, date modified, type, permissions. Click column headers to sort. Like Dolphin's detail view.

All views support:
- Rubber-band (drag-to-select) multi-selection
- Ctrl+click and Shift+click selection
- Hover effects

## Tab System

Each tab is an independent browsing session with its own state:
- Current directory path
- Navigation history (back/forward stack)
- View mode (grid/list/detailed)
- Sort order (column + ascending/descending)
- Selection state
- Scroll position

**Tab interactions:**
- Ctrl+T: New tab at home directory
- Ctrl+W: Close current tab (last tab closes window)
- Ctrl+Shift+T: Reopen last closed tab
- Middle-click folder: Open in new tab
- Drag files onto tab header: Copy/move to that tab's directory

## Scrolling

Kinetic scrolling with elastic overscroll:
- Touchpad swipe continues with momentum, gradually decelerates
- Hitting top/bottom edge: content stretches slightly and bounces back (iOS/macOS style)
- Implemented in QML using Flickable with custom bounce behavior

## File Operations

- **Copy/Paste:** Ctrl+C sets clipboard with source paths. Ctrl+V triggers rsync to current directory. Progress shown in sidebar.
- **Cut/Paste:** Ctrl+X marks files for move. Ctrl+V moves them. Source files shown dimmed until move completes.
- **Delete/Trash:** Delete key sends to trash via `gio trash`. Shift+Delete permanently deletes (with confirmation dialog).
- **Rename:** F2 triggers inline rename in the file view.
- **Drag and drop:** Within app (between panes/tabs) and Wayland inter-app via wl_data_device.

## Quick Preview

- Spacebar opens a centered overlay showing a preview of the selected file
- Supported types: images (rendered), text files (syntax highlighted), PDFs (first page)
- Arrow keys cycle through files while preview is open
- Escape or Spacebar closes the overlay
- Overlay has a semi-transparent backdrop, rounded corners, and fade-in animation

## Context Menu

Right-click opens a context-aware menu:

**Default items:**
- Open / Open With
- Cut / Copy / Paste
- Copy Path
- Rename
- Trash / Delete
- Open in Terminal
- Compress / Extract (for archives)
- New Folder / New File
- Properties

**Custom actions** defined in config:
```toml
[[context_menu.actions]]
name = "Open in Neovim"
command = "foot nvim {file}"
types = ["file"]

[[context_menu.actions]]
name = "Set as Wallpaper"
command = "hyprctl hyprpaper wallpaper ',{file}'"
types = ["image"]
```

Context menu adapts based on:
- File vs folder vs empty space
- File type (image, archive, text, etc.)
- Single vs multi-selection

## Keyboard Shortcuts

Default shortcuts (all configurable via config):

| Action | Shortcut |
|--------|----------|
| Open | Enter |
| Back | Alt+Left |
| Forward | Alt+Right |
| Parent directory | Alt+Up |
| New tab | Ctrl+T |
| Close tab | Ctrl+W |
| Reopen tab | Ctrl+Shift+T |
| Copy | Ctrl+C |
| Cut | Ctrl+X |
| Paste | Ctrl+V |
| Rename | F2 |
| Trash | Delete |
| Permanent delete | Shift+Delete |
| Toggle hidden | Ctrl+H |
| Quick preview | Space |
| Search | Ctrl+F |
| Path bar focus | Ctrl+L |
| Toggle sidebar | F9 |
| Grid view | Ctrl+1 |
| List view | Ctrl+2 |
| Detailed view | Ctrl+3 |
| Select all | Ctrl+A |

## Theming

Catppuccin Mocha as default theme. Users can create custom themes via TOML files.

**Theme file format:**
```toml
# ~/.config/hyprfm/themes/custom.toml
[colors]
base = "#1e1e2e"
surface = "#313244"
overlay = "#45475a"
text = "#cdd6f4"
subtext = "#6c7086"
accent = "#89b4fa"
success = "#a6e3a1"
warning = "#f9e2af"
error = "#f38ba8"
```

**Main config references theme:**
```toml
[general]
theme = "catppuccin-mocha"    # built-in name or path to custom .toml
```

Theme is loaded at startup by ThemeLoader (C++) and exposed to QML as a singleton. The app watches the config file and hot-reloads on change — no restart required.

## Configuration

All config lives at `~/.config/hyprfm/config.toml`:

```toml
[general]
theme = "catppuccin-mocha"
default_view = "grid"
show_hidden = false
sort_by = "name"
sort_ascending = true

[sidebar]
position = "left"
width = 200
visible = true

[bookmarks]
paths = ["~/Documents", "~/Downloads", "~/Pictures", "~/Projects"]

[shortcuts]
open = "Return"
back = "Alt+Left"
forward = "Alt+Right"
new_tab = "Ctrl+T"
close_tab = "Ctrl+W"
toggle_hidden = "Ctrl+H"
quick_preview = "Space"
search = "Ctrl+F"
path_bar = "Ctrl+L"

[[context_menu.actions]]
name = "Open in Neovim"
command = "foot nvim {file}"
types = ["file"]
```

## Error Handling

- **Permission denied:** Inline toast notification (bottom-right, auto-dismiss). "Open as root" option in context menu for folders.
- **Device removed mid-operation:** Abort rsync, show error toast, keep partial files.
- **Large directories (10k+ items):** QFileSystemModel lazy-loads. Thumbnails generated asynchronously via thread pool.
- **Missing CLI tools:** First-launch check. Toast listing missing tools and affected features. Graceful degradation (e.g., cp fallback if rsync missing).
- **Config parse errors:** Fall back to defaults, show toast with error line number.
- **Filesystem watcher limits:** Toast suggesting inotify increase, fall back to polling.
- **Wayland DnD failure:** Fall back to copy-path-to-clipboard with toast notification.

## Build System

CMake with the following dependencies:
- Qt6 (Core, Gui, Qml, Quick, QuickControls2, DBus)
- TOML parser (toml++ or similar, header-only)

No GNOME, KDE, or XFCE libraries required.

## MVP Scope

**Included in v0.1:**
- Three view modes (grid, list, detailed with sortable columns)
- Tab support
- Sidebar with bookmarks + devices (storage bars, configurable position)
- Breadcrumb navigation with back/forward
- File operations (copy, cut, paste, rename, delete, trash)
- Operations progress in sidebar
- Image thumbnails (async)
- Spacebar quick preview (images, text, PDF)
- Drag and drop (within app + Wayland inter-app)
- Mouse support (click, double-click, right-click, hover, rubber-band select)
- Context menu with custom actions
- Keyboard shortcuts (configurable)
- Theming (Catppuccin default + custom TOML themes)
- Elastic/kinetic scrolling with overscroll bounce
- Status bar
- Config hot-reload

**Deferred to post-MVP:**
- Split pane view
- Vim-style keybindings (hjkl, command mode)
- Built-in terminal panel
- Archive preview/extraction
- Bulk rename
- File search (fd integration)
- Network shares / remote filesystems

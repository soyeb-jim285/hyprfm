# HyprFM — File Manager for Hyprland

## Vision
A lightweight, keyboard-driven file manager built with Qt6/QML. No GNOME, KDE, or XFCE dependencies. Designed for Hyprland and the Wayland ecosystem.

## Why
Every file manager available today pulls in a full desktop environment's libraries. Hyprland users deserve a native option that fits the ecosystem.

## Tech Stack
- Qt6 QML frontend
- Thin C++ backend (QFileSystemModel, async process runner, filesystem watcher)
- CLI tools for heavy lifting (rsync, gio trash, udisksctl, xdg-open, fd)
- CMake build system

## MVP Features
- [ ] Grid and list view with toggle
- [ ] Breadcrumb navigation + back/forward
- [ ] Bookmarks sidebar
- [ ] Basic file ops (copy, cut, paste, rename, delete, trash)
- [ ] Image thumbnails
- [ ] Drag and drop
- [ ] Keyboard navigation
- [ ] Themeable (Catppuccin default)
- [ ] Context menu (right-click)
- [ ] Open files with xdg-open

## Future Ideas
- Split pane / tabs
- Vim-style keybindings
- Built-in terminal panel
- Archive preview/extract
- Bulk rename
- USB/drive mount/unmount sidebar
- File search (fd integration)
- Preview panel (images, text, PDF)
- Wayland-native drag to other apps

## CLI Backend
| Task | Tool |
|------|------|
| Listing | QFileSystemModel / eza |
| Copy with progress | rsync --progress |
| Trash | gio trash |
| Mount/unmount | udisksctl |
| Search | fd |
| File type detection | file --mime-type |
| Open files | xdg-open |
| Archives | 7z, tar, unzip |
| Clipboard | wl-copy / wl-paste |

## Discussion
<!-- Start a new conversation in ~/hyprfm to brainstorm further -->

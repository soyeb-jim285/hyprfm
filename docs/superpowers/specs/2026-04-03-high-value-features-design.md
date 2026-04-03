# High-Value Features Design Spec

Five features to fill the most impactful gaps in HyprFM. Implementation order: symlink creation, bulk rename, directory size calculation, per-directory preferences, split pane.

---

## 1. Create Symlink

Add a "Create Link" context menu action that creates symbolic links to selected files/folders.

### Flow

- Select one or more files -> right-click -> "Create Link" (icon: `Link`)
- Creates `<filename> (link)` in the same directory
- For multiple selections, creates one symlink per file
- Uses `QFile::link()` -- synchronous, no progress needed

### Undo/Redo

- New `UndoRecord::CreateSymlink` type
- Undo: deletes the symlink(s)
- Redo: recreates them

### Files

| File | Change |
|------|--------|
| `src/services/fileoperations.h/cpp` | New `createSymlink(target, linkPath)` method |
| `src/services/undomanager.h/cpp` | New record type + undo/redo cases |
| `src/qml/components/ContextMenu.qml` | Add "Create Link" item after "Rename" |
| `src/qml/Main.qml` | Handle `createLinkRequested` signal |

---

## 2. Bulk Rename

A dialog for renaming multiple selected files at once with two modes: Find & Replace and Template.

### Dialog UI

- Opens when F2 / context menu "Rename..." is triggered with multiple files selected (single file keeps current inline dialog)
- `Q.Card` modal, larger than the rename dialog
- Two tabs: "Find & Replace" | "Template"
- Live preview list: `old name -> new name` for all selected files
- "Rename" and "Cancel" buttons

### Find & Replace Tab

- "Find" text field + "Replace with" text field
- Checkboxes: "Use regex", "Case sensitive", "Include extension"
- Operates on filenames only by default (not extensions)

### Template Tab

- Template text field with placeholders: `{name}`, `{ext}`, `{n}` (counter), `{n:03}` (zero-padded), `{date}`, `{date:YYYY-MM-DD}`
- "Start number" spinner for the counter
- Example: `Photo_{n:03}.{ext}` -> `Photo_001.jpg`, `Photo_002.jpg`, ...

### Backend

- `FileOperations::previewBulkRename(...)` -- returns list of `{oldPath, newName}` pairs without executing
- `FileOperations::bulkRename(QVariantList renames)` -- executes all renames
- Single `UndoRecord::BulkRename` with list of old/new names; one undo reverts all

### Files

| File | Change |
|------|--------|
| `src/services/fileoperations.h/cpp` | `previewBulkRename()`, `bulkRename()` methods |
| `src/services/undomanager.h/cpp` | New `BulkRename` record type |
| `src/qml/components/BulkRenameDialog.qml` | New file -- dialog UI |
| `src/qml/Main.qml` | F2 branches on selection count; context menu wiring |

---

## 3. Directory Size Calculation

Show immediate child count for folders by default, with on-demand recursive size calculation and caching.

### Default Display

- Folders show "{N} items" in size column (detailed view) and tooltip (grid/list)
- Computed in `FileSystemModel::data()` for `FileSizeTextRole` when `isDir`
- Uses `QDir::entryList(AllEntries | NoDotAndDotDot).count()` -- single syscall, fast

### On-Demand Recursive Size

- Right-click folder -> "Calculate Size" context menu action
- Also available in Properties dialog via a "Calculate" button
- Worker thread (`DirectorySizeWorker` QObject moved to QThread) walks tree via `QDirIterator`
- Folder shows spinner then final formatted size
- Result cached in `QHash<QString, qint64>` on FileSystemModel
- Cache invalidated when QFileSystemWatcher fires for that directory
- Cache is session-only (not persisted to disk)

### Backend

- New `DirectorySizeWorker` class with signal `sizeReady(QString path, qint64 size, int fileCount, int dirCount)`
- `FileSystemModel::calculateDirSize(const QString &path)` -- Q_INVOKABLE, spawns worker
- `dirSizeCalculated` signal for QML to react
- New `DirItemCountRole` for quick item count

### Files

| File | Change |
|------|--------|
| `src/models/filesystemmodel.h/cpp` | New role, cache, `calculateDirSize()` |
| `src/services/directorysizeworker.h/cpp` | New file -- worker class |
| `src/qml/components/ContextMenu.qml` | "Calculate Size" for directories |
| `src/qml/views/FileDetailedView.qml` | Show item count / calculated size |
| `CMakeLists.txt` | Add new source file |

---

## 4. Per-Directory View/Sort Memory

Remember view mode and sort settings per directory in config.toml. Apply automatically when navigating.

### Storage Format

```toml
[directory_prefs."~/Downloads"]
view = "grid"
sort_by = "modified"
sort_ascending = false

[directory_prefs."~/Projects"]
view = "detailed"
sort_by = "name"
sort_ascending = true
```

### Behavior

- Changing view mode or sort saves preference for the current directory
- Navigating to a directory with saved prefs overrides the tab's current settings
- Directories without prefs inherit the tab's current settings
- Max 500 entries, LRU eviction when exceeded
- Debounce saves by 1 second to avoid config thrashing

### Backend

- `ConfigManager::dirPrefs(path)` -- returns `{view, sortBy, sortAscending}` or empty map
- `ConfigManager::saveDirPrefs(path, view, sortBy, ascending)` -- saves to internal map + debounced file write
- Internal `QHash<QString, DirPref>` loaded from TOML on startup

### QML Integration

- Path changes trigger `config.dirPrefs(path)` lookup, apply if non-empty
- View/sort changes call `config.saveDirPrefs(...)` to persist

### Files

| File | Change |
|------|--------|
| `src/services/configmanager.h/cpp` | New methods, storage, debounced save |
| `src/qml/Main.qml` | Wire path changes and view/sort changes |

---

## 5. Split Pane

Split the current view vertically or horizontally to show two independent directory panes.

### UI

- Each pane is a complete `FileViewContainer` with its own path, view mode, sort, and selection
- Default: single pane (unchanged from current)
- Shortcuts: `Ctrl+\` (vertical split), `Ctrl+-` (horizontal split), `Ctrl+Shift+\` (close split)
- Active pane indicated by accent border; clicks/keyboard route to active pane
- Each pane has a mini breadcrumb header + close button

### Architecture

- `SplitView.qml` -- wraps Qt `SplitView` with two `PaneContainer` children
- `PaneContainer.qml` -- wraps `FileViewContainer` + mini breadcrumb + close button
- Each pane gets its own `FileSystemModel` instance
- `Main.qml` manages `activePaneIndex` (0 or 1) to route shortcuts and actions
- Tab bar stays shared; split state is per-tab

### State

- `TabModel` gains: `isSplit` (bool), `splitOrientation` (Qt.Vertical/Horizontal), `secondPanePath` (string)
- Session save/restore includes split state
- Drag-drop between panes supported for copy/move

### Files

| File | Change |
|------|--------|
| `src/qml/components/SplitView.qml` | New -- split container |
| `src/qml/components/PaneContainer.qml` | New -- pane wrapper |
| `src/qml/Main.qml` | Integrate split, route actions, add shortcuts |
| `src/models/tabmodel.h/cpp` | New split properties |
| `src/models/tablistmodel.h/cpp` | Save/restore split state |
| `src/qml/FileViewContainer.qml` | Minor refactor for PaneContainer compatibility |

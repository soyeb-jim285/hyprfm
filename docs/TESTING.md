# HyprFM MVP — Manual Testing Checklist (Round 2)

Test each feature below. Mark with `[x]` when passing, `[!]` for bugs, `[ ]` for untested.
Add comments inline after each item.

**Fixes applied since Round 1:**
- clipboard.take() now Q_INVOKABLE
- All rootIndex() references removed
- Ctrl+A selectAll added to all views
- Open in Terminal uses $TERMINAL/foot
- Right-click empty space emits context menu
- Drag no longer moves icons randomly (DragHandler removed)
- Ctrl+L calls toolbar.startEditing()
- Breadcrumb: home icon, double-click empty space enters edit
- Backspace + mouse back/forward buttons for navigation
- Tab bar shows folder icon + name
- Arrow key navigation in all views
- FileTabBar signal handler deprecation warnings fixed

---

## Navigation

- [ ] Double-click folder to navigate into it
  - Comments:
- [ ] Breadcrumb shows correct path (home icon instead of `/`)
  - Comments:
- [ ] Click breadcrumb segment to navigate to that directory
  - Comments:
- [ ] Double-click breadcrumb (or empty space) to enter edit mode
  - Comments:
- [ ] Press Enter in breadcrumb edit to navigate, Escape to cancel
  - Comments:
- [ ] Back button works (Alt+Left)
  - Comments:
- [ ] Backspace goes back
  - Comments:
- [ ] Mouse back button goes back
  - Comments:
- [ ] Mouse forward button goes forward
  - Comments:
- [ ] Forward button works (Alt+Right)
  - Comments:
- [ ] Up button works (Alt+Up)
  - Comments:
- [ ] Sidebar bookmark click navigates to that directory
  - Comments:
- [ ] Arrow keys navigate between files in grid/list/detailed view
  - Comments:

## Views

- [ ] Grid view shows files with icons
  - Comments:
- [ ] List view shows files in rows (icon, name, size, date)
  - Comments:
- [ ] Detailed view shows sortable columns (name, size, date, type, permissions)
  - Comments:
- [ ] Click column header in detailed view to sort
  - Comments:
- [ ] View toggle buttons switch between grid/list/detailed (Ctrl+1/2/3)
  - Comments:

## Tabs

- [ ] Click + to add new tab
  - Comments:
- [ ] Ctrl+T opens new tab
  - Comments:
- [ ] Click x to close tab
  - Comments:
- [ ] Ctrl+W closes current tab
  - Comments:
- [ ] Click tab to switch to it
  - Comments:
- [ ] Ctrl+Shift+T reopens last closed tab
  - Comments:
- [ ] Last tab close quits the app
  - Comments:
- [ ] Each tab maintains its own directory/view state
  - Comments:
- [ ] Tab shows folder icon and folder name
  - Comments:

## Selection

- [ ] Single click selects a file
  - Comments:
- [ ] Ctrl+click toggles selection
  - Comments:
- [ ] Shift+click range selects
  - Comments:
- [ ] Ctrl+A selects all
  - Comments:
- [ ] Click empty space clears selection
  - Comments:
- [ ] Rubber-band selection in grid view (drag on empty space)
  - Comments:
- [ ] Status bar shows selected count
  - Comments:

## File Operations

- [ ] Ctrl+C copies selected files
  - Comments:
- [ ] Ctrl+X cuts selected files
  - Comments:
- [ ] Ctrl+V pastes (copies or moves depending on cut/copy)
  - Comments:
- [ ] F2 renames selected file
  - Comments:
- [ ] Delete key trashes selected files
  - Comments:
- [ ] Double-click file opens it with xdg-open
  - Comments:
- [ ] Progress bar shows in sidebar during copy/move
  - Comments:
- [ ] Toast notification appears after operation completes
  - Comments:

## Context Menu

- [ ] Right-click file shows context menu (readable text colors)
  - Comments:
- [ ] Right-click empty space shows context menu (New Folder, New File, Paste)
  - Comments:
- [ ] Open works
  - Comments:
- [ ] Cut/Copy/Paste works from menu
  - Comments:
- [ ] Copy Path works
  - Comments:
- [ ] Rename works from menu
  - Comments:
- [ ] Trash works from menu
  - Comments:
- [ ] Open in Terminal works (uses $TERMINAL or foot)
  - Comments:
- [ ] New Folder creates a folder
  - Comments:
- [ ] New File creates a file
  - Comments:
- [ ] Custom actions appear (if configured in config.toml)
  - Comments:

## Sidebar

- [ ] Bookmarks section shows with correct icons
  - Comments:
- [ ] Devices section shows mounted drives
  - Comments:
- [ ] Device storage bars show with correct colors (blue/yellow/red)
  - Comments:
- [ ] Eject button appears on removable devices
  - Comments:
- [ ] F9 toggles sidebar visibility
  - Comments:
- [ ] Operations progress bar appears during file operations
  - Comments:

## Icons and Thumbnails

- [ ] Folder icons render (Adwaita theme)
  - Comments:
- [ ] File icons render by type
  - Comments:
- [ ] Image thumbnails show in grid view (png, jpg, etc.)
  - Comments:
- [ ] Sidebar bookmark icons render
  - Comments:
- [ ] Device icons render
  - Comments:

## Scrolling

- [ ] Smooth scroll with touchpad
  - Comments:
- [ ] Elastic overscroll bounce at top/bottom
  - Comments:
- [ ] Kinetic scrolling (momentum after swipe)
  - Comments:

## Drag and Drop

- [ ] Drag files from grid view (icon should NOT move randomly)
  - Comments:
- [ ] Drop files onto a folder to copy/move
  - Comments:
- [ ] Drop files onto a tab header
  - Comments:

## Quick Preview

- [ ] Spacebar opens preview on selected file
  - Comments:
- [ ] Image preview displays correctly
  - Comments:
- [ ] Spacebar or Escape closes preview
  - Comments:
- [ ] Arrow keys cycle through files in preview
  - Comments:

## Keyboard Shortcuts

- [ ] Ctrl+T — new tab
  - Comments:
- [ ] Ctrl+W — close tab
  - Comments:
- [ ] Ctrl+Shift+T — reopen tab
  - Comments:
- [ ] Alt+Left — back
  - Comments:
- [ ] Alt+Right — forward
  - Comments:
- [ ] Alt+Up — parent directory
  - Comments:
- [ ] Backspace — back
  - Comments:
- [ ] Ctrl+H — toggle hidden files
  - Comments:
- [ ] Ctrl+L — focus path bar (breadcrumb edit)
  - Comments:
- [ ] F9 — toggle sidebar
  - Comments:
- [ ] Ctrl+1/2/3 — switch view mode
  - Comments:
- [ ] Ctrl+C/X/V — copy/cut/paste
  - Comments:
- [ ] Delete — trash
  - Comments:
- [ ] Ctrl+A — select all
  - Comments:
- [ ] Space — quick preview
  - Comments:
- [ ] Arrow keys — navigate between files
  - Comments:

## Theming

- [ ] App loads with Catppuccin Mocha colors
  - Comments:
- [ ] Custom theme loads from config.toml
  - Comments:
- [ ] Config changes apply without restart (hot-reload)
  - Comments:

---

## Bugs Found

List any bugs found during testing:

1.
2.
3.

## Notes

Add any general observations here:

- the range box select using mouse should be using left click, not the right. 
- the dragging does not work. I can not drag and drop. nothing works!
- `ctrl + A` still does not work
- open in terminal does not work. use kitty terminal by default.  
- the `x` button in the tabs does not work 
- arrow button does not navigate. (if a file/folder is selected, I can not slected using the arrow keys) 

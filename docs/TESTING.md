# HyprFM MVP — Manual Testing Checklist

Test each feature below. Mark with `[x]` when passing, `[!]` for bugs, `[ ]` for untested.
Add comments inline after each item.

---

## Navigation

- [x] Double-click folder to navigate into it
  - Comments:
- [x] Breadcrumb shows correct path
  - Comments: before the breadcrumb there is a button `/` remove that, use home button instead. 
- [x] Click breadcrumb segment to navigate to that directory
  - Comments:
- [x] Double-click breadcrumb to enter edit mode (type a path)
  - Comments: I need to double click to the exact breadcrumb but it should work on the full space(the empty space right of the breadcrumb, where if the breadcrumb is large would show)
- [x] Press Enter in breadcrumb edit to navigate, Escape to cancel
  - Comments:
- [x] Back button works (Alt+Left)
  - Comments: also add the beakspace button for the back button. also my mouse back button should work.(this button works in dolphin, notulas also in browser)
- [x] Forward button works (Alt+Right)
  - Comments:
- [x] Up button works (Alt+Up)
  - Comments:
- [x] Sidebar bookmark click navigates to that directory
  - Comments:

## Views

- [x] Grid view shows files with icons
  - Comments:
- [x] List view shows files in rows (icon, name, size, date)
  - Comments:
- [x] Detailed view shows sortable columns (name, size, date, type, permissions)
  - Comments:
- [x] Click column header in detailed view to sort
  - Comments:
- [x] View toggle buttons switch between grid/list/detailed (Ctrl+1/2/3)
  - Comments:

## Tabs

- [x] Click + to add new tab
  - Comments: what icons are being used in the tabs? the folder icon? also the text should show the parent folder name in the tab.  
- [x] Ctrl+T opens new tab
  - Comments: add behaviour options for the new tab in settings, i.e open new tab in cwd or root or home etc. 
- [x] Click x to close tab
  - Comments:
- [x] Ctrl+W closes current tab
  - Comments:
- [x] Click tab to switch to it
  - Comments:
- [x] Ctrl+Shift+T reopens last closed tab
  - Comments:
- [x] Last tab close quits the app
  - Comments: in the last tab there is no x button and also the ctrl w does not work. this is probably the best. no need to chnage this behaviour. 
- [x] Each tab maintains its own directory/view state
  - Comments:

## Selection

- [x] Single click selects a file
  - Comments:
- [x] Ctrl+click toggles selection
  - Comments:
- [x] Shift+click range selects
  - Comments:
- [x] Ctrl+A selects all
  - Comments: does not work. [BUG]
- [x] Click empty space clears selection
  - Comments: does not work. [BUG]
- [x] Rubber-band selection in grid view (drag on empty space)
  - Comments:
- [x] Status bar shows selected count
  - Comments:

## File Operations

- [x] Ctrl+C copies selected files
  - Comments: does not work
- [x] Ctrl+X cuts selected files
  - Comments: does not work probably as the peast does not work
- [x] Ctrl+V pastes (copies or moves depending on cut/copy)
  - Comments: does not work
```
qrc:/HyprFM/qml/Main.qml:399: TypeError: Property 'take' of object ClipboardManager(0x55c511a12aa0) is not a function
qrc:/HyprFM/qml/Main.qml:399: TypeError: Property 'take' of object ClipboardManager(0x55c511a12aa0) is not a function
```
- [x] F2 renames selected file
  - Comments: does not work
- [x] Delete key trashes selected files
  - Comments:
- [x] Double-click file opens it with xdg-open
  - Comments: probably. not sure.
- [ ] Progress bar shows in sidebar during copy/move
  - Comments:
- [x] Toast notification appears after operation completes
  - Comments:

## Context Menu

- [x] Right-click file shows context menu
  - Comments: the text are not readale, fix the colors of the context menu.
- [x] Right-click empty space shows context menu (New Folder, New File, Paste)
  - Comments: deos not work
- [x] Open works
  - Comments:
- [ ] Cut/Copy/Paste works from menu
  - Comments:
- [x] Copy Path works
  - Comments:
- [x] Rename works from menu
  - Comments:
- [x] Trash works from menu
  - Comments:
- [x] Open in Terminal works
  - Comments: it does not: logs: 
  /usr/bin/xdg-open: line 1045: x-www-browser: command not found
/usr/bin/xdg-open: line 1045: firefox: command not found
/usr/bin/xdg-open: line 1045: iceweasel: command not found
/usr/bin/xdg-open: line 1045: seamonkey: command not found
/usr/bin/xdg-open: line 1045: mozilla: command not found
/usr/bin/xdg-open: line 1045: epiphany: command not found
/usr/bin/xdg-open: line 1045: konqueror: command not found
/usr/bin/xdg-open: line 1045: chromium: command not found
/usr/bin/xdg-open: line 1045: chromium-browser: command not found
/usr/bin/xdg-open: line 1045: google-chrome: command not found
/usr/bin/xdg-open: line 1045: www-browser: command not found
/usr/bin/xdg-open: line 1045: links2: command not found
/usr/bin/xdg-open: line 1045: elinks: command not found
/usr/bin/xdg-open: line 1045: links: command not found
/usr/bin/xdg-open: line 1045: lynx: command not found
/usr/bin/xdg-open: line 1045: w3m: command not found
xdg-open: no method available for opening 'exec:xterm%20-e%20bash%20-c%20'cd%20%22/home/jim/yay%22;%20exec%20bash''

- [x] New Folder creates a folder
  - Comments:
- [x] New File creates a file
  - Comments:
- [ ] Custom actions appear (if configured in config.toml)
  - Comments:

## Sidebar

- [x] Bookmarks section shows with correct icons
  - Comments:
- [x] Devices section shows mounted drives
  - Comments:
- [x] Device storage bars show with correct colors (blue/yellow/red)
  - Comments:
- [ ] Eject button appears on removable devices
  - Comments:
- [x] F9 toggles sidebar visibility
  - Comments:
- [ ] Operations progress bar appears during file operations
  - Comments:

## Icons and Thumbnails

- [x] Folder icons render (Adwaita theme)
  - Comments:
- [x] File icons render by type
  - Comments: maybe there is problem with zip or compressed files. the icons are not correct. Give reminding about this, there maybe a lot of of them, we will fix it later  
- [x] Image thumbnails show in grid view (png, jpg, etc.)
  - Comments:
- [x] Sidebar bookmark icons render
  - Comments:
- [x] Device icons render
  - Comments:

## Scrolling

- [x] Smooth scroll with touchpad
  - Comments:  
- [x] Elastic overscroll bounce at top/bottom
  - Comments:
- [x] Kinetic scrolling (momentum after swipe)
  - Comments: does not work

## Drag and Drop

- [x] Drag files from grid view
  - Comments: 
- [x] Drop files onto a folder to copy/move
  - Comments:
- [x] Drop files onto a tab header
  - Comments:
- None of them works as when tring to drag it moves the icon itself!!!
## Quick Preview

- [x] Spacebar opens preview on selected file
  - Comments: does not work.
  inotify_add_watch(/root) failed: (Permission denied)
qrc:/HyprFM/qml/Main.qml:101: TypeError: Property 'rootIndex' of object FileSystemModel(0x55a6deac3c50) is not a function
- [ ] Image preview displays correctly
  - Comments:
- [ ] Spacebar or Escape closes preview
  - Comments:
- [ ] Arrow keys cycle through files in preview
  - Comments:

none of them works

## Keyboard Shortcuts

- [x] Ctrl+T — new tab
  - Comments:
- [x] Ctrl+W — close tab
  - Comments:
- [x] Ctrl+Shift+T — reopen tab
  - Comments:
- [x] Alt+Left — back
  - Comments:
- [x] Alt+Right — forward
  - Comments:
- [x] Alt+Up — parent directory
  - Comments:
- [ ] Ctrl+H — toggle hidden files
  - Comments:
- [x] Ctrl+L — focus path bar (breadcrumb edit)
  - Comments: does not work
- [x] F9 — toggle sidebar
  - Comments:
- [x] Ctrl+1/2/3 — switch view mode
  - Comments:
- [ ] Ctrl+C/X/V — copy/cut/paste
  - Comments:
- [x] Delete — trash
  - Comments:
- [ ] Ctrl+A — select all
  - Comments:
- [ ] Space — quick preview
  - Comments:

## Theming

- [x] App loads with Catppuccin Mocha colors
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

-

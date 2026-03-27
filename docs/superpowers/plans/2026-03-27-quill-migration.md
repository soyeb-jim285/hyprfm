# Quill Component Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hand-rolled dialog UI in HyprFM with shared Quill components so both HyprFM and quickshell share the same design system.

**Architecture:** Quill lives at `src/qml/quill/` as a git submodule with its own `Theme.qml` singleton. HyprFM bridges its config-driven theme into Quill's Theme at startup so all Quill components match the user's chosen colors. Dialogs switch from raw Rectangle/Text/TextField to Quill's Card, Button, TextField, Tabs, Separator, Dropdown, Collapsible, and ProgressBar.

**Tech Stack:** Qt 6 / QML, Quill component library (submodule at `src/qml/quill/`)

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `src/qml/Main.qml` | Modify | Replace dialog markup with Quill components, add theme bridge |
| `src/qml/components/OperationsBar.qml` | Modify | Replace QtQuick.Controls ProgressBar with Quill ProgressBar |
| `src/CMakeLists.txt` | Modify | Add `QML_IMPORT_PATH` for quill module |

No new files needed — all changes are in-place replacements.

---

### Task 1: Register Quill QML import path in CMake

**Files:**
- Modify: `src/CMakeLists.txt`

The Quill module lives at `src/qml/quill/` with its own `qmldir`. Qt needs to know where to find the `Quill` module at runtime.

- [ ] **Step 1: Add QML import path to CMakeLists.txt**

Add after the `qt_add_qml_module(...)` block:

```cmake
# Make Quill module discoverable at runtime
set_target_properties(hyprfm PROPERTIES
    QT_QML_IMPORT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
```

This tells Qt to search `src/qml/` for QML modules, where it will find `quill/qmldir` defining the `Quill` module.

- [ ] **Step 2: Build to verify**

Run: `cmake -B build && cmake --build build`
Expected: Clean build, no errors.

- [ ] **Step 3: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "build: add Quill QML import path"
```

---

### Task 2: Bridge HyprFM Theme into Quill Theme

**Files:**
- Modify: `src/qml/Main.qml:1-10` (imports and Component.onCompleted)

HyprFM's `Theme` singleton (from `qml/theme/Theme.qml`) gets its colors from C++ config at runtime. Quill's `Theme` singleton has hardcoded Catppuccin defaults. We bridge them so Quill components render with HyprFM's active theme.

Property mapping:
| HyprFM Theme | Quill Theme |
|---|---|
| `base` | `background` |
| `mantle` | `backgroundAlt` |
| `crust` | `backgroundDeep` |
| `surface` | `surface0` |
| `text` | `textPrimary` |
| `subtext` | `textSecondary` |
| `muted` | `textTertiary` |
| `accent` | `primary` |
| `success` | `success` |
| `warning` | `warning` |
| `error` | `error` |
| `radiusSmall` | `radiusSm` |
| `radiusMedium` | `radius` |
| `radiusLarge` | `radiusLg` |
| `fontSmall` | `fontSizeSmall` |
| `fontNormal` | `fontSize` |
| `fontLarge` | `fontSizeLarge` |

- [ ] **Step 1: Add Quill import to Main.qml**

At the top of `Main.qml`, add:

```qml
import "quill" as Quill
```

- [ ] **Step 2: Add theme bridge in Component.onCompleted**

Inside the root `ApplicationWindow`, add a `Component.onCompleted` handler (or extend the existing one if present):

```qml
Component.onCompleted: {
    // Bridge HyprFM theme into Quill theme
    Quill.Theme.background = Qt.binding(() => Theme.base)
    Quill.Theme.backgroundAlt = Qt.binding(() => Theme.mantle)
    Quill.Theme.backgroundDeep = Qt.binding(() => Theme.crust)
    Quill.Theme.surface0 = Qt.binding(() => Theme.surface)
    Quill.Theme.surface1 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1))
    Quill.Theme.surface2 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15))
    Quill.Theme.textPrimary = Qt.binding(() => Theme.text)
    Quill.Theme.textSecondary = Qt.binding(() => Theme.subtext)
    Quill.Theme.textTertiary = Qt.binding(() => Theme.muted)
    Quill.Theme.primary = Qt.binding(() => Theme.accent)
    Quill.Theme.success = Qt.binding(() => Theme.success)
    Quill.Theme.warning = Qt.binding(() => Theme.warning)
    Quill.Theme.error = Qt.binding(() => Theme.error)
    Quill.Theme.radiusSm = Qt.binding(() => Theme.radiusSmall)
    Quill.Theme.radius = Qt.binding(() => Theme.radiusMedium)
    Quill.Theme.radiusLg = Qt.binding(() => Theme.radiusLarge)
    Quill.Theme.fontSizeSmall = Qt.binding(() => Theme.fontSmall)
    Quill.Theme.fontSize = Qt.binding(() => Theme.fontNormal)
    Quill.Theme.fontSizeLarge = Qt.binding(() => Theme.fontLarge)
}
```

Using `Qt.binding()` ensures Quill theme updates reactively when HyprFM theme changes (e.g., user switches theme in config).

- [ ] **Step 3: Build and run briefly to verify no errors**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: No QML errors, app launches normally.

- [ ] **Step 4: Commit**

```bash
git add src/qml/Main.qml
git commit -m "feat: bridge HyprFM theme into Quill theme singleton"
```

---

### Task 3: Migrate rename dialog to Quill components

**Files:**
- Modify: `src/qml/Main.qml:108-268` (rename dialog section)

Replace hand-rolled Rectangle + Column + Text + TextField + Row buttons with Quill Card + TextField + Button.

- [ ] **Step 1: Replace rename dialog content**

Find the rename dialog content (the `Column { id: renameContent ... }` block inside `renameBox`). Replace the entire `Rectangle { anchors.fill: parent ... }` and `Column { id: renameContent ... }` block with:

```qml
            Quill.Card {
                anchors.fill: parent
                title: "Rename"
                padding: 20

                Quill.TextField {
                    id: renameField
                    Layout.fillWidth: true
                    autoFocus: true
                    placeholder: "Enter new name"
                    Keys.onReturnPressed: renameDialog.accept()
                    Keys.onEscapePressed: renameDialog.reject()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Quill.Button {
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        onClicked: renameDialog.reject()
                    }

                    Quill.Button {
                        text: "Rename"
                        variant: "primary"
                        size: "small"
                        onClicked: renameDialog.accept()
                    }
                }
            }
```

Update the `accept()` function to read from `renameField.text` (still works — the id is preserved).

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: App launches. Test rename dialog by right-clicking a file → Rename. Dialog should show Quill-styled card with themed text field and buttons.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate rename dialog to Quill components"
```

---

### Task 4: Migrate new folder dialog to Quill components

**Files:**
- Modify: `src/qml/Main.qml:270-430` (new folder dialog section)

Same pattern as rename — replace the Rectangle + Column with Quill Card + TextField + Button.

- [ ] **Step 1: Replace new folder dialog content**

Replace the `Rectangle { anchors.fill: parent ... }` and `Column { id: folderContent ... }` block inside `folderBox` with:

```qml
            Quill.Card {
                anchors.fill: parent
                title: "New Folder"
                padding: 20

                Quill.TextField {
                    id: newFolderField
                    Layout.fillWidth: true
                    autoFocus: true
                    placeholder: "Folder name"
                    Keys.onReturnPressed: newFolderDialog.accept()
                    Keys.onEscapePressed: newFolderDialog.reject()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Quill.Button {
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        onClicked: newFolderDialog.reject()
                    }

                    Quill.Button {
                        text: "Create"
                        variant: "primary"
                        size: "small"
                        onClicked: newFolderDialog.accept()
                    }
                }
            }
```

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Right-click → New Folder shows Quill-styled dialog.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate new folder dialog to Quill components"
```

---

### Task 5: Migrate new file dialog to Quill components

**Files:**
- Modify: `src/qml/Main.qml:432-590` (new file dialog section)

- [ ] **Step 1: Replace new file dialog content**

Replace the `Rectangle { anchors.fill: parent ... }` and `Column { id: fileContent ... }` block inside `fileBox` with:

```qml
            Quill.Card {
                anchors.fill: parent
                title: "New File"
                padding: 20

                Quill.TextField {
                    id: newFileField
                    Layout.fillWidth: true
                    autoFocus: true
                    placeholder: "File name"
                    Keys.onReturnPressed: newFileDialog.accept()
                    Keys.onEscapePressed: newFileDialog.reject()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Quill.Button {
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        onClicked: newFileDialog.reject()
                    }

                    Quill.Button {
                        text: "Create"
                        variant: "primary"
                        size: "small"
                        onClicked: newFileDialog.accept()
                    }
                }
            }
```

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Right-click → New File shows Quill-styled dialog.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate new file dialog to Quill components"
```

---

### Task 6: Migrate properties dialog tabs to Quill Tabs

**Files:**
- Modify: `src/qml/Main.qml:718-746` (properties dialog tab bar)

Replace the hand-rolled tab bar (Repeater + underline Rectangle) with `Quill.Tabs`.

- [ ] **Step 1: Replace tab bar**

Replace the `Item { width: parent.width; height: 36 ... }` tab bar section with:

```qml
                Quill.Tabs {
                    model: ["General", "Permissions"]
                    currentIndex: propertiesDialog.currentTab
                    onTabChanged: (index) => propertiesDialog.currentTab = index
                }
```

Remove the manual `Rectangle` underline separator below the tab bar (Quill.Tabs has its own built-in underline).

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Properties dialog tabs render with Quill styling and animated underline.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate properties dialog tabs to Quill Tabs"
```

---

### Task 7: Migrate properties dialog separators to Quill Separator

**Files:**
- Modify: `src/qml/Main.qml` (all `Rectangle { width: parent.width - 48; height: 1; ... }` separator lines in properties dialog)

- [ ] **Step 1: Replace all separator lines in the General tab**

Find all instances of this pattern in the properties dialog General tab:

```qml
Rectangle { width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }
```

Replace each with:

```qml
Quill.Separator { Layout.leftMargin: 24; Layout.rightMargin: 24 }
```

Note: The General tab content uses a plain `Column`, not `ColumnLayout`. The Quill Separator uses `Layout.fillWidth` which only works inside layouts. If the parent is `Column`, wrap the separator in an `Item` or convert the parent to `ColumnLayout`. Check the actual parent element before replacing.

If the parent is `Column { id: generalTab }`, the simplest approach is to use:

```qml
Quill.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }
```

- [ ] **Step 2: Replace separator in Permissions tab**

Same pattern for any separators in the `permissionsTab` Column.

- [ ] **Step 3: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Properties dialog separators render identically (thin lines).

- [ ] **Step 4: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate properties dialog separators to Quill Separator"
```

---

### Task 8: Migrate properties dialog permissions dropdown to Quill Dropdown

**Files:**
- Modify: `src/qml/Main.qml:1003-1058` (PermGroup component)

Replace the hand-rolled access selector (Rectangle with click-to-cycle and `IconChevronDown`) with `Quill.Dropdown`.

- [ ] **Step 1: Replace dropdown in PermGroup component**

In the `PermGroup` component definition, replace the `Item { width: parent.width; height: 28 ... }` access selector with:

```qml
                        Quill.Dropdown {
                            model: propsBox.accessOptions
                            currentIndex: accessIdx
                            label: "Access"
                            onSelected: (index, value) => accessChanged(index)
                        }
```

Remove the `Text { text: "Access:" ... }` label and the entire dropdown `Rectangle` since Quill.Dropdown has its own label and chevron.

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Properties → Permissions tab shows Quill-styled dropdowns for Owner/Group/Others access. Clicking opens a list instead of cycling.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate permissions dropdown to Quill Dropdown"
```

---

### Task 9: Migrate Open With section to Quill Collapsible

**Files:**
- Modify: `src/qml/Main.qml:857-991` (Open With section in properties dialog)

Replace the hand-rolled expandable Open With list with `Quill.Collapsible`.

- [ ] **Step 1: Replace Open With section**

Replace the entire Open With `Item { ... property bool expanded ... }` block with:

```qml
                    Quill.Collapsible {
                        visible: !(propertiesDialog.props.isDir) && propertiesDialog.apps.length > 0
                        title: {
                            var apps = propertiesDialog.apps
                            for (var i = 0; i < apps.length; i++)
                                if (apps[i].isDefault) return "Open with: " + apps[i].name
                            return apps.length > 0 ? "Open with: " + apps[0].name : "Open with"
                        }
                        width: parent.width
                        anchors.leftMargin: 24; anchors.rightMargin: 24

                        Repeater {
                            model: propertiesDialog.apps
                            delegate: Rectangle {
                                width: parent ? parent.width : 0; height: 30; radius: 4
                                color: owItemMa.containsMouse
                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1)
                                    : "transparent"
                                Layout.fillWidth: true

                                Image {
                                    id: owAppIcon
                                    source: modelData.iconName ? ("image://icon/" + modelData.iconName) : ""
                                    sourceSize: Qt.size(18, 18)
                                    width: 18; height: 18
                                    anchors.left: parent.left; anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.iconName && status === Image.Ready
                                }

                                Text {
                                    text: modelData.name
                                    color: modelData.isDefault ? Theme.accent : Theme.text
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: modelData.isDefault ? Font.DemiBold : Font.Normal
                                    anchors.left: owAppIcon.visible ? owAppIcon.right : parent.left
                                    anchors.leftMargin: owAppIcon.visible ? 8 : 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: owItemBadge.left; anchors.rightMargin: 4
                                    elide: Text.ElideRight
                                }

                                IconCheck {
                                    id: owItemBadge
                                    visible: modelData.isDefault
                                    size: 14; color: Theme.accent
                                    anchors.right: parent.right; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                MouseArea {
                                    id: owItemMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!modelData.isDefault) {
                                            fsModel.setDefaultApp(propertiesDialog.props.mimeType, modelData.desktopFile)
                                            propertiesDialog.apps = fsModel.availableApps(propertiesDialog.props.mimeType)
                                        }
                                    }
                                }
                            }
                        }
                    }
```

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Properties dialog Open With section uses Quill-styled collapsible with animated expand/collapse and chevron rotation.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate Open With to Quill Collapsible"
```

---

### Task 10: Migrate properties dialog close button to Quill Button

**Files:**
- Modify: `src/qml/Main.qml:1100-1110` (close button at bottom of properties dialog)

- [ ] **Step 1: Replace close button**

Replace the hand-rolled `Rectangle { ... Text { id: propsCloseText ... } ... MouseArea { ... } }` with:

```qml
                Item {
                    width: parent.width; height: 48
                    Quill.Button {
                        text: "Close"
                        variant: "primary"
                        size: "small"
                        anchors.right: parent.right; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: propertiesDialog.close()
                    }
                }
```

- [ ] **Step 2: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Properties dialog close button renders with Quill styling.

- [ ] **Step 3: Commit**

```bash
git add src/qml/Main.qml
git commit -m "refactor: migrate properties close button to Quill Button"
```

---

### Task 11: Migrate OperationsBar to Quill ProgressBar

**Files:**
- Modify: `src/qml/components/OperationsBar.qml`

Replace the QtQuick.Controls ProgressBar (with custom background/contentItem) with Quill.ProgressBar.

- [ ] **Step 1: Replace ProgressBar**

Update imports — remove `import QtQuick.Controls`, add `import "../quill" as Quill`.

Replace the entire `ProgressBar { id: progressBar ... }` block with:

```qml
        Quill.ProgressBar {
            Layout.fillWidth: true
            value: fileOps.progress
        }
```

Also update the status text to use `Theme.subtext` (already correct) — no change needed there.

- [ ] **Step 2: Add OperationsBar.qml to CMakeLists if not already listed**

Check `src/CMakeLists.txt` — it should already be listed in QML_FILES. Verify.

- [ ] **Step 3: Build and test**

Run: `cmake --build build && timeout 3 ./build/src/hyprfm`
Expected: Operations bar shows Quill-styled progress bar during file copy/move.

- [ ] **Step 4: Commit**

```bash
git add src/qml/components/OperationsBar.qml
git commit -m "refactor: migrate OperationsBar to Quill ProgressBar"
```

---

### Task 12: Final verification and cleanup

**Files:**
- Review: `src/qml/Main.qml`, `src/qml/components/OperationsBar.qml`

- [ ] **Step 1: Run full build**

Run: `cmake -B build && cmake --build build`
Expected: Clean build, no warnings related to QML.

- [ ] **Step 2: Run tests**

Run: `ctest --test-dir build`
Expected: All tests pass (6/6).

- [ ] **Step 3: Manual test all migrated UI**

Test each dialog:
1. Right-click file → Rename → verify Quill Card/TextField/Button
2. Right-click → New Folder → verify dialog
3. Right-click → New File → verify dialog
4. Right-click → Properties → verify Quill Tabs, Separators, Close button
5. Properties → Permissions → verify Quill Dropdown for access
6. Properties → Open With → verify Quill Collapsible
7. Copy a large file → verify Quill ProgressBar in operations bar

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor: complete Quill component migration for dialogs and operations bar"
```

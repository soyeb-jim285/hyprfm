import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: 680
    title: "Bulk Rename"
    subtitle: sourceItems.length > 0
        ? sourceItems.length + (sourceItems.length === 1 ? " item selected" : " items selected")
        : ""
    initialFocusItem: replaceFindField

    property var sourceItems: []
    property var previewItems: []
    property string applyError: ""

    readonly property int changedCount: countChanged()
    readonly property int errorCount: countErrors()
    readonly property bool mixedParents: hasMultipleParents()

    signal renameApplied(var renamedPaths)

    function fileNameForPath(path) {
        var slashIndex = path.lastIndexOf("/")
        return slashIndex >= 0 ? path.substring(slashIndex + 1) : path
    }

    function parentDirForPath(path) {
        var slashIndex = path.lastIndexOf("/")
        return slashIndex > 0 ? path.substring(0, slashIndex) : "/"
    }

    function joinPath(dirPath, name) {
        return dirPath === "/" ? "/" + name : dirPath + "/" + name
    }

    function splitName(name) {
        if (!preserveExtensionToggle.checked)
            return { base: name, extension: "" }

        var dotIndex = name.lastIndexOf(".")
        if (dotIndex <= 0)
            return { base: name, extension: "" }

        return {
            base: name.substring(0, dotIndex),
            extension: name.substring(dotIndex)
        }
    }

    function sequencedNumber(index) {
        var startAt = parseInt(sequenceStartField.text, 10)
        if (isNaN(startAt))
            startAt = 1

        var padding = parseInt(sequencePaddingField.text, 10)
        if (isNaN(padding) || padding < 1)
            padding = 2

        var numberText = String(startAt + index)
        while (numberText.length < padding)
            numberText = "0" + numberText

        return numberText
    }

    function generatedName(item, index) {
        var parts = splitName(item.name)
        var baseName = parts.base
        var extension = parts.extension

        if (modeTabs.currentIndex === 0) {
            if (replaceFindField.text === "")
                return item.name
            baseName = baseName.split(replaceFindField.text).join(replaceWithField.text)
        } else if (modeTabs.currentIndex === 1) {
            if (addPrefixField.text === "" && addSuffixField.text === "")
                return item.name
            baseName = addPrefixField.text + baseName + addSuffixField.text
        } else {
            var baseTemplate = sequenceBaseField.text.trim()
            if (baseTemplate === "")
                return item.name
            baseName = baseTemplate + sequenceSeparatorField.text + sequencedNumber(index)
        }

        return baseName + extension
    }

    function validateName(name) {
        if (name.trim() === "")
            return "Invalid"
        if (name === "." || name === "..")
            return "Invalid"
        if (name.indexOf("/") >= 0)
            return "Invalid"
        return ""
    }

    function refreshPreview() {
        applyError = ""

        var previews = []
        var sourceLookup = ({})
        var targetCounts = ({})
        var existenceCache = ({})

        for (var i = 0; i < sourceItems.length; ++i)
            sourceLookup[sourceItems[i].path] = true

        for (var index = 0; index < sourceItems.length; ++index) {
            var item = sourceItems[index]
            var newName = generatedName(item, index)
            var targetPath = joinPath(item.parentDir, newName)
            var error = validateName(newName)
            var changed = targetPath !== item.path

            previews.push({
                path: item.path,
                parentDir: item.parentDir,
                oldName: item.name,
                newName: newName,
                targetPath: targetPath,
                changed: changed,
                error: error
            })

            targetCounts[targetPath] = (targetCounts[targetPath] || 0) + 1
        }

        for (var previewIndex = 0; previewIndex < previews.length; ++previewIndex) {
            var preview = previews[previewIndex]
            if (preview.error === "" && targetCounts[preview.targetPath] > 1) {
                preview.error = "Duplicate"
                continue
            }

            if (preview.error === "" && preview.changed && !sourceLookup[preview.targetPath]) {
                var exists = existenceCache[preview.targetPath]
                if (exists === undefined) {
                    exists = fileOps.pathExists(preview.targetPath)
                    existenceCache[preview.targetPath] = exists
                }

                if (exists)
                    preview.error = "Exists"
            }
        }

        previewItems = previews
    }

    function countChanged() {
        var count = 0
        for (var i = 0; i < previewItems.length; ++i) {
            if (previewItems[i].changed)
                count += 1
        }
        return count
    }

    function countErrors() {
        var count = 0
        for (var i = 0; i < previewItems.length; ++i) {
            if (previewItems[i].error !== "")
                count += 1
        }
        return count
    }

    function hasMultipleParents() {
        if (sourceItems.length < 2)
            return false

        var firstParent = sourceItems[0].parentDir
        for (var i = 1; i < sourceItems.length; ++i) {
            if (sourceItems[i].parentDir !== firstParent)
                return true
        }

        return false
    }

    function statusLabel(preview) {
        if (preview.error !== "")
            return preview.error
        if (!preview.changed)
            return ""
        return "Renamed"
    }

    function actionText() {
        if (changedCount <= 0)
            return "Rename Items"
        if (changedCount === 1)
            return "Rename Item"
        return "Rename " + changedCount + " Items"
    }

    function focusActiveField() {
        if (modeTabs.currentIndex === 0) {
            replaceFindField.inputItem.forceActiveFocus()
            replaceFindField.inputItem.selectAll()
            return
        }

        if (modeTabs.currentIndex === 1) {
            addPrefixField.inputItem.forceActiveFocus()
            addPrefixField.inputItem.selectAll()
            return
        }

        sequenceBaseField.inputItem.forceActiveFocus()
        sequenceBaseField.inputItem.selectAll()
    }

    function openForPaths(paths) {
        if (!paths || paths.length < 2)
            return

        var items = []
        var seen = ({})
        for (var i = 0; i < paths.length; ++i) {
            var path = paths[i]
            if (!path || seen[path])
                continue

            seen[path] = true
            items.push({
                path: path,
                name: fileNameForPath(path),
                parentDir: parentDirForPath(path)
            })
        }

        if (items.length < 2)
            return

        sourceItems = items
        previewItems = []
        applyError = ""
        modeTabs.currentIndex = 0
        replaceFindField.text = ""
        replaceWithField.text = ""
        addPrefixField.text = ""
        addSuffixField.text = ""
        sequenceBaseField.text = ""
        sequenceStartField.text = "1"
        sequencePaddingField.text = "2"
        sequenceSeparatorField.text = " "
        preserveExtensionToggle.checked = true
        refreshPreview()
        open()
    }

    function applyRenameRules() {
        applyError = ""

        if (errorCount > 0) {
            applyError = errorCount === 1
                ? "Resolve the remaining conflict before renaming."
                : "Resolve the remaining conflicts before renaming."
            return
        }

        var operations = []
        for (var i = 0; i < previewItems.length; ++i) {
            var preview = previewItems[i]
            if (!preview.changed)
                continue

            operations.push({
                sourcePath: preview.path,
                targetPath: preview.targetPath
            })
        }

        if (operations.length === 0) {
            applyError = "Adjust the rules to rename at least one item."
            return
        }

        var result = undoManager.renameResolvedItems(operations)
        if (!result.success) {
            applyError = result.error || "Couldn't rename the selected items."
            return
        }

        renameApplied(result.changedPaths || [])
        accept()
    }

    onOpened: {
        refreshPreview()
        Qt.callLater(function() {
            root.focusActiveField()
        })
    }

    onClosed: {
        previewItems = []
        sourceItems = []
        applyError = ""
    }

    // --- Tabs ---

    Q.Tabs {
        id: modeTabs
        Layout.fillWidth: true
        model: ["Replace", "Add Text", "Sequence"]
        onTabChanged: {
            root.refreshPreview()
            Qt.callLater(function() {
                root.focusActiveField()
            })
        }
    }

    // --- Mode controls ---

    Item {
        id: controlsContainer
        Layout.fillWidth: true
        clip: true
        property real contentHeight: modeTabs.currentIndex === 0
            ? replaceControls.implicitHeight
            : modeTabs.currentIndex === 1
                ? addControls.implicitHeight
                : sequenceControls.implicitHeight
        implicitHeight: contentHeight

        Behavior on contentHeight {
                NumberAnimation {
                    duration: Theme.animDurationSlow
                    easing.type: Theme.animEasingEnter
                }
            }

        ColumnLayout {
            id: replaceControls
            anchors.left: parent.left
            anchors.right: parent.right
            visible: modeTabs.currentIndex === 0
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Find"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: replaceFindField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "Text to find..."
                        onTextChanged: root.refreshPreview()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Replace with"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: replaceWithField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "Replacement..."
                        onTextChanged: root.refreshPreview()
                    }
                }
            }
        }

        ColumnLayout {
            id: addControls
            anchors.left: parent.left
            anchors.right: parent.right
            visible: modeTabs.currentIndex === 1
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Prefix"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: addPrefixField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "Before name..."
                        onTextChanged: root.refreshPreview()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Suffix"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: addSuffixField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "After name..."
                        onTextChanged: root.refreshPreview()
                    }
                }
            }
        }

        ColumnLayout {
            id: sequenceControls
            anchors.left: parent.left
            anchors.right: parent.right
            visible: modeTabs.currentIndex === 2
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Base name"
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }

                Q.TextField {
                    id: sequenceBaseField
                    Layout.fillWidth: true
                    variant: "filled"
                    placeholder: "e.g. photo, document..."
                    onTextChanged: root.refreshPreview()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Start at"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: sequenceStartField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "1"
                        inputItem.validator: IntValidator { bottom: 0 }
                        onTextChanged: root.refreshPreview()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Padding"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: sequencePaddingField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "2"
                        inputItem.validator: IntValidator { bottom: 1 }
                        onTextChanged: root.refreshPreview()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Separator"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: sequenceSeparatorField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "_ or -"
                        onTextChanged: root.refreshPreview()
                    }
                }
            }
        }
    }

    // --- Options row ---

    Q.Toggle {
        id: preserveExtensionToggle
        label: "Preserve file extensions"
        checked: true
        onToggled: root.refreshPreview()
    }

    // --- Separator ---

    Q.Separator { }

    // --- Preview header ---

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: {
                if (changedCount === 0)
                    return "Preview"
                return "Preview — " + changedCount + (changedCount === 1 ? " change" : " changes")
            }
            color: Theme.text
            font.pointSize: Theme.fontNormal
            font.bold: true
        }

        Q.Badge {
            visible: errorCount > 0
            text: errorCount + (errorCount === 1 ? " conflict" : " conflicts")
            variant: "error"
        }
    }

    // --- Preview list ---

    ListView {
        id: previewList
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(previewItems.length * (root.mixedParents ? 60 : 44) + (previewItems.length - 1) * 4, 240)
        clip: true
        spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        model: root.previewItems

        delegate: Rectangle {
            required property var modelData
            required property int index
            width: previewList.width
            height: root.mixedParents ? 60 : 44
            radius: Theme.radiusSmall
            color: modelData.error !== ""
                ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.06)
                : modelData.changed
                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.06)
                    : "transparent"
            opacity: modelData.changed || modelData.error !== "" ? 1.0 : 0.45

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 0

                // Old name — fixed 42% width
                Item {
                    width: parent.width * 0.42
                    height: parent.height

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 2

                        Text {
                            width: parent.width
                            text: modelData.oldName
                            color: Theme.subtext
                            font.pointSize: Theme.fontNormal
                            elide: Text.ElideMiddle
                        }

                        Text {
                            visible: root.mixedParents
                            width: parent.width
                            text: modelData.parentDir
                            color: Qt.rgba(Theme.subtext.r, Theme.subtext.g, Theme.subtext.b, 0.6)
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                        }
                    }
                }

                // Arrow — fixed 8% width, centered
                Item {
                    width: parent.width * 0.08
                    height: parent.height

                    Text {
                        anchors.centerIn: parent
                        text: "\u2192"
                        color: modelData.changed ? Theme.accent : Theme.subtext
                        font.pointSize: Theme.fontNormal
                        opacity: modelData.changed ? 1.0 : 0.5
                    }
                }

                // New name — remaining space
                Item {
                    width: parent.width * 0.50 - (errorBadge.visible ? errorBadge.width + 8 : 0)
                    height: parent.height

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 2

                        Text {
                            width: parent.width
                            text: modelData.newName
                            color: modelData.error !== ""
                                ? Theme.error
                                : modelData.changed ? Theme.text : Theme.subtext
                            font.pointSize: Theme.fontNormal
                            font.weight: modelData.changed && modelData.error === "" ? Font.DemiBold : Font.Normal
                            elide: Text.ElideMiddle
                        }

                        Text {
                            visible: root.mixedParents
                            width: parent.width
                            text: modelData.parentDir
                            color: Qt.rgba(Theme.subtext.r, Theme.subtext.g, Theme.subtext.b, 0.6)
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                        }
                    }
                }

                // Status badge — only for errors
                Q.Badge {
                    id: errorBadge
                    anchors.verticalCenter: parent.verticalCenter
                    visible: modelData.error !== ""
                    text: root.statusLabel(modelData)
                    variant: "error"
                }
            }
        }
    }

    Q.Label {
        visible: previewItems.length === 0
        Layout.alignment: Qt.AlignHCenter
        text: "Select at least two items to start a batch rename."
        variant: "body"
    }

    // --- Error message ---

    Text {
        Layout.fillWidth: true
        visible: applyError !== ""
        text: applyError
        color: Theme.error
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    // --- Action buttons ---

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12

        Q.Button {
            text: "Cancel"
            variant: "ghost"
            size: "small"
            onClicked: root.reject()
        }

        Q.Button {
            text: actionText()
            variant: "primary"
            size: "small"
            enabled: changedCount > 0 && errorCount === 0
            onClicked: root.applyRenameRules()
        }
    }
}

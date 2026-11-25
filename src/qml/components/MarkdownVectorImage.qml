import QtQuick
import QtQuick.VectorImage

VectorImage {
    id: root
    objectName: "markdownVectorImage"
    source: parent ? parent.svgSource : ""
    preferredRendererType: VectorImage.CurveRenderer
    fillMode: VectorImage.PreserveAspectFit

    // VectorImage applies the curve preference to shapes. Its SVG labels
    // are separate Text items and otherwise use Qt's distance-field glyphs.
    function useCurveText(item) {
        if (item instanceof Text)
            item.renderType = Text.CurveRendering
        for (let i = 0; i < item.children.length; ++i)
            useCurveText(item.children[i])
    }

    onChildrenChanged: Qt.callLater(useCurveText, root)
    Component.onCompleted: Qt.callLater(useCurveText, root)
}

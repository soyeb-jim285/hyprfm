import QtQuick
import QtQuick.VectorImage

VectorImage {
    objectName: "markdownVectorImage"
    source: parent ? parent.svgSource : ""
    preferredRendererType: VectorImage.CurveRenderer
    fillMode: VectorImage.PreserveAspectFit
}

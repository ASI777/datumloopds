import QtQuick
import DatumLoop.App 1.0

DatumEditor {
    id: host
    property string editorName: ""
    objectName: editorName + "EditorHost"

    Loader {
        anchors.fill: parent
        sourceComponent: host.editorType === DatumEditorType.Outliner ? outlinerComponent
                       : host.editorType === DatumEditorType.Viewport ? viewportComponent
                       : propertiesComponent
    }

    Component {
        id: outlinerComponent
        OutlinerEditor { theme: host.theme }
    }
    Component {
        id: viewportComponent
        ViewportEditor { theme: host.theme }
    }
    Component {
        id: propertiesComponent
        PropertiesEditor { theme: host.theme }
    }
}

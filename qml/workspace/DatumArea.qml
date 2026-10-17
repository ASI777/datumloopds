import QtQuick
import QtQuick.Controls
import DatumLoop.App 1.0

SplitView {
    id: area
    objectName: "datumArea"
    property var theme
    orientation: Qt.Horizontal
    handle: Rectangle {
        implicitWidth: area.theme ? area.theme.splitterWidth : 5
        implicitHeight: area.theme ? area.theme.splitterWidth : 5
        color: area.theme ? area.theme.surfaceRoot : "#11171d"
        Rectangle {
            anchors.centerIn: parent
            width: area.theme ? area.theme.borderWidth : 1
            height: parent.height
            color: area.theme ? area.theme.borderSubtle : "#33434e"
        }
    }

    EditorHost {
        theme: area.theme
        editorName: "outliner"
        editorTitle: "Engineering Outliner"
        editorType: DatumEditorType.Outliner
        SplitView.preferredWidth: 248
        SplitView.minimumWidth: 192
    }
    EditorHost {
        theme: area.theme
        editorName: "viewport"
        editorTitle: "Viewport"
        editorType: DatumEditorType.Viewport
        SplitView.fillWidth: true
        SplitView.minimumWidth: 450
    }
    EditorHost {
        theme: area.theme
        editorName: "properties"
        editorTitle: "Properties"
        editorType: DatumEditorType.Properties
        SplitView.preferredWidth: 302
        SplitView.minimumWidth: 235
    }
}

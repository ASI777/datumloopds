import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: workspace
    objectName: "datumWorkspace"
    property var theme
    spacing: 0

    WorkspaceHeader {
        Layout.fillWidth: true
        theme: workspace.theme
    }
    DatumArea {
        Layout.fillWidth: true
        Layout.fillHeight: true
        theme: workspace.theme
    }
    StatusBar {
        Layout.fillWidth: true
        theme: workspace.theme
    }
}

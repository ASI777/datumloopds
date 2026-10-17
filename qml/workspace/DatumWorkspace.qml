import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: workspace
    objectName: "datumWorkspace"
    signal openProjectRequested()
    signal saveProjectRequested()
    property var theme
    spacing: 0

    WorkspaceHeader {
        Layout.fillWidth: true
        theme: workspace.theme
        onOpenProjectRequested: workspace.openProjectRequested()
        onSaveProjectRequested: workspace.saveProjectRequested()
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

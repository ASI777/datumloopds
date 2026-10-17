import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import DatumLoop.App 1.0

ApplicationWindow {
    id: window
    objectName: "datumLoopWindow"
    visible: true
    width: 1480
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    title: "DatumLoop DS — " + documentModel.projectName + (documentModel.dirty ? " •" : "")
    color: designSystem.surfaceRoot

    DesignSystem { id: designSystem }

    FileDialog {
        id: openProjectDialog
        title: "Open DatumLoop Project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["DatumLoop projects (*.dld)"]
        onAccepted: {
            if (documentModel.dirty) {
                unsavedOpenDialog.requestedFile = selectedFile
                unsavedOpenDialog.open()
            } else {
                documentModel.openProject(selectedFile)
            }
        }
    }

    Dialog {
        id: unsavedOpenDialog
        property url requestedFile: ""
        title: "Unsaved changes"
        modal: true
        width: 390
        contentItem: ColumnLayout {
            spacing: designSystem.space3
            Text {
                Layout.fillWidth: true
                text: "Opening another project will discard the current unsaved changes."
                color: designSystem.textPrimary
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: "Cancel"
                    onClicked: unsavedOpenDialog.close()
                }
                Button {
                    text: "Discard and Open"
                    onClicked: {
                        const fileUrl = unsavedOpenDialog.requestedFile
                        unsavedOpenDialog.close()
                        documentModel.openProject(fileUrl)
                    }
                }
            }
        }
    }

    FileDialog {
        id: saveProjectDialog
        title: "Save DatumLoop Project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "dld"
        nameFilters: ["DatumLoop projects (*.dld)"]
        onAccepted: documentModel.saveProjectAs(selectedFile)
    }

    Connections {
        target: documentModel
        function onSaveAsRequested() { saveProjectDialog.open() }
    }

    Shortcut {
        sequence: "G"
        enabled: !documentModel.moveActive
        onActivated: documentModel.beginMove()
    }
    Shortcut {
        sequence: "Escape"
        enabled: documentModel.moveActive
        onActivated: documentModel.cancelMove()
    }
    Shortcut {
        sequence: "Return"
        enabled: documentModel.moveActive
        onActivated: documentModel.confirmMove()
    }
    Shortcut {
        sequence: "X"
        enabled: documentModel.moveActive
        onActivated: documentModel.setMoveAxis("X")
    }
    Shortcut {
        sequence: "Y"
        enabled: documentModel.moveActive
        onActivated: documentModel.setMoveAxis("Y")
    }
    Shortcut {
        sequence: "Z"
        enabled: documentModel.moveActive
        onActivated: documentModel.setMoveAxis("Z")
    }
    Shortcut {
        sequence: "Ctrl+Z"
        enabled: !documentModel.moveActive
        onActivated: documentModel.undo()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Z"
        enabled: !documentModel.moveActive
        onActivated: documentModel.redo()
    }
    Shortcut {
        sequence: "F"
        onActivated: documentModel.frameSelected()
    }

    DatumWorkspace {
        anchors.fill: parent
        theme: designSystem
        onOpenProjectRequested: openProjectDialog.open()
        onSaveProjectRequested: documentModel.saveProject()
    }
}

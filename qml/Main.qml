import QtQuick
import QtQuick.Controls
import DatumLoop.App 1.0

ApplicationWindow {
    id: window
    objectName: "datumLoopWindow"
    visible: true
    width: 1480
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    title: "DatumLoop DS"
    color: designSystem.surfaceRoot

    DesignSystem { id: designSystem }

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
    }
}

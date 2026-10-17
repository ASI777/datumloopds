import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import DatumLoop.Native 1.0

Item {
    id: editor
    objectName: "viewportEditor"
    property var theme

    View3D {
        id: viewport
        objectName: "cadViewport"
        anchors.fill: parent
        camera: camera
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: editor.theme.viewportBackground
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        PerspectiveCamera {
            id: camera
            objectName: "datumLoopCamera"
            clipNear: 0.1
            clipFar: 50000
            fieldOfView: 42
            property real orbitDistance: 112
            property real orbitYaw: 35
            property real orbitPitch: 26
            property vector3d orbitTarget: Qt.vector3d(0, 0, 0)
            position: Qt.vector3d(
                orbitTarget.x + orbitDistance * Math.sin(orbitYaw * Math.PI / 180) * Math.cos(orbitPitch * Math.PI / 180),
                orbitTarget.y + orbitDistance * Math.sin(orbitPitch * Math.PI / 180),
                orbitTarget.z + orbitDistance * Math.cos(orbitYaw * Math.PI / 180) * Math.cos(orbitPitch * Math.PI / 180))
            eulerRotation: Qt.vector3d(-orbitPitch, orbitYaw, 0)
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-42, -34, 0)
            brightness: 1.35
            ambientColor: editor.theme.textSecondary
        }

        Model {
            id: solidModel
            objectName: "datumLoopExactSolid"
            property string datumObjectId: documentModel.displayObjectId
            geometry: ExactShapeGeometry {
                id: cadGeometry
                objectName: "cadGeometry"
            }
            position: documentModel.displayPosition
            materials: PrincipledMaterial {
                baseColor: documentModel.selectedRow >= 0 ? editor.theme.accent : editor.theme.surfaceRaised
                metalness: 0.28
                roughness: 0.32
            }
            castsShadows: true
            pickable: true
        }
    }

    MouseArea {
        id: viewportInput
        objectName: "viewportInput"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        property real previousX: 0
        property real previousY: 0
        onPressed: function(mouse) {
            previousX = mouse.x
            previousY = mouse.y
        }
        onPositionChanged: function(mouse) {
            const dx = mouse.x - previousX
            const dy = mouse.y - previousY
            if (mouse.buttons & Qt.MiddleButton) {
                if (mouse.modifiers & Qt.ShiftModifier) {
                    const panScale = camera.orbitDistance * 0.0022
                    camera.orbitTarget = Qt.vector3d(
                        camera.orbitTarget.x - dx * panScale,
                        camera.orbitTarget.y + dy * panScale,
                        camera.orbitTarget.z)
                } else {
                    camera.orbitYaw -= dx * 0.42
                    camera.orbitPitch = Math.max(-82, Math.min(82, camera.orbitPitch + dy * 0.38))
                }
            } else if (mouse.buttons & Qt.RightButton) {
                const panScale = camera.orbitDistance * 0.0022
                camera.orbitTarget = Qt.vector3d(
                    camera.orbitTarget.x - dx * panScale,
                    camera.orbitTarget.y + dy * panScale,
                    camera.orbitTarget.z)
            }
            previousX = mouse.x
            previousY = mouse.y
        }
        onClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton) {
                const result = viewport.pick(mouse.x, mouse.y)
                if (result && result.objectHit === solidModel)
                    pickingService.resolveObjectHit(solidModel.datumObjectId)
            }
        }
        onWheel: function(wheel) {
            camera.orbitDistance = Math.max(52, Math.min(1600,
                camera.orbitDistance * Math.exp(-wheel.angleDelta.y / 120 * 0.12)))
            wheel.accepted = true
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: editor.theme.space3
        width: 164
        height: 33
        radius: editor.theme.panelRadius
        color: editor.theme.surfacePrimary
        border.width: editor.theme.borderWidth
        border.color: editor.theme.borderSubtle
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: editor.theme.space2
            anchors.rightMargin: editor.theme.space2
            Text { text: "DESIGN / PART"; color: editor.theme.textSecondary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Text { text: "●"; color: editor.theme.engineeringNominal; font.pixelSize: editor.theme.fontSizeSmall }
        }
    }

    ComboBox {
        id: viewSelector
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: editor.theme.space3
        width: 136
        height: editor.theme.controlHeight
        model: ["Perspective", "Front", "Right", "Top"]
        font.family: editor.theme.fontFamily
        font.pixelSize: editor.theme.fontSizeSmall
        onActivated: function(index) {
            if (index === 0) {
                camera.orbitYaw = 35
                camera.orbitPitch = 26
            } else if (index === 1) {
                camera.orbitYaw = 0
                camera.orbitPitch = 0
            } else if (index === 2) {
                camera.orbitYaw = 90
                camera.orbitPitch = 0
            } else {
                camera.orbitYaw = 0
                camera.orbitPitch = 88
            }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: editor.theme.space3
        spacing: editor.theme.space1
        DlButton {
            theme: editor.theme
            objectName: "moveAxisXButton"
            text: "X"
            width: 36
            height: 30
            contentItem: Text { text: parent.text; color: editor.theme.axisX; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; font.weight: Font.Bold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            ToolTip.visible: hovered
            ToolTip.text: "Move along X"
            onClicked: { if (!documentModel.moveActive) documentModel.beginMove(); documentModel.setMoveAxis("X") }
        }
        DlButton {
            theme: editor.theme
            objectName: "moveAxisYButton"
            text: "Y"
            width: 36
            height: 30
            contentItem: Text { text: parent.text; color: editor.theme.axisY; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; font.weight: Font.Bold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            ToolTip.visible: hovered
            ToolTip.text: "Move along Y"
            onClicked: { if (!documentModel.moveActive) documentModel.beginMove(); documentModel.setMoveAxis("Y") }
        }
        DlButton {
            theme: editor.theme
            objectName: "moveAxisZButton"
            text: "Z"
            width: 36
            height: 30
            contentItem: Text { text: parent.text; color: editor.theme.axisZ; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; font.weight: Font.Bold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            ToolTip.visible: hovered
            ToolTip.text: "Move along Z"
            onClicked: { if (!documentModel.moveActive) documentModel.beginMove(); documentModel.setMoveAxis("Z") }
        }
    }

    Rectangle {
        id: moveCommandPanel
        objectName: "moveCommandPanel"
        visible: documentModel.moveActive
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: editor.theme.space4
        width: Math.min(parent.width - editor.theme.space4 * 2, 500)
        height: 62
        radius: editor.theme.panelRadius
        color: editor.theme.surfacePrimary
        border.width: editor.theme.borderWidth
        border.color: editor.theme.accent

        RowLayout {
            anchors.fill: parent
            anchors.margins: editor.theme.space2
            spacing: editor.theme.space2
            Text {
                text: "MOVE"
                color: editor.theme.accent
                font.family: editor.theme.fontFamily
                font.pixelSize: editor.theme.fontSizeSmall
                font.weight: Font.Bold
            }
            Repeater {
                model: ["X", "Y", "Z"]
                delegate: DlButton {
                    required property string modelData
                    theme: editor.theme
                    objectName: "movePanelAxis" + modelData + "Button"
                    text: modelData
                    Layout.preferredWidth: 34
                    highlighted: documentModel.moveAxis === modelData
                    onClicked: documentModel.setMoveAxis(modelData)
                }
            }
            TextField {
                id: moveDistance
                objectName: "moveDistanceField"
                Layout.fillWidth: true
                implicitHeight: editor.theme.controlHeight
                text: documentModel.moveValue
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                color: editor.theme.textPrimary
                font.family: editor.theme.fontFamily
                font.pixelSize: editor.theme.fontSizeBody
                placeholderText: "Distance"
                onTextEdited: documentModel.setMoveValue(text)
                background: Rectangle {
                    radius: editor.theme.panelRadius
                    color: editor.theme.surfaceRoot
                    border.color: moveDistance.activeFocus ? editor.theme.accent : editor.theme.borderSubtle
                    border.width: editor.theme.borderWidth
                }
            }
            Text { text: "mm"; color: editor.theme.textSecondary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
            DlButton { theme: editor.theme; objectName: "cancelMoveButton"; text: "Cancel"; Layout.preferredWidth: 70; onClicked: documentModel.cancelMove() }
            DlButton {
                theme: editor.theme
                objectName: "confirmMoveButton"
                text: "Confirm"
                Layout.preferredWidth: 82
                background: Rectangle { radius: editor.theme.panelRadius; color: editor.theme.selectionPrimary; border.color: editor.theme.accent; border.width: editor.theme.borderWidth }
                onClicked: documentModel.confirmMove()
            }
        }
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: editor.theme.space3
        width: 160
        height: 28
        radius: editor.theme.panelRadius
        color: editor.theme.surfacePrimary
        border.color: editor.theme.borderSubtle
        border.width: editor.theme.borderWidth
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: editor.theme.space2
            anchors.rightMargin: editor.theme.space2
            Text { text: "X"; color: editor.theme.axisX; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall; font.weight: Font.Bold }
            Text { text: "Y"; color: editor.theme.axisY; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall; font.weight: Font.Bold }
            Text { text: "Z"; color: editor.theme.axisZ; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall; font.weight: Font.Bold }
            Item { Layout.fillWidth: true }
            Text { text: "mm"; color: editor.theme.textMuted; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
        }
    }

    Connections {
        target: documentModel
        function onFrameSelectedRequested(position) {
            camera.orbitTarget = position
        }
    }
}

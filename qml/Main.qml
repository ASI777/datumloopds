import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import DatumLoop.Native 1.0

ApplicationWindow {
    id: window
    objectName: "datumLoopWindow"
    visible: true
    width: 1480
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    title: "DatumLoop DS"
    color: theme.surfaceRoot

    DesignSystem { id: theme }

    component DlButton: Button {
        id: control
        implicitHeight: theme.controlHeight
        font.family: theme.fontFamily
        font.pixelSize: theme.fontSizeBody
        contentItem: Text {
            text: control.text
            color: control.enabled ? theme.textPrimary : theme.textMuted
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: theme.panelRadius
            color: !control.enabled ? theme.surfacePrimary
                  : control.down ? theme.selectionPrimary
                  : control.hovered ? theme.surfaceHover : theme.surfaceSecondary
            border.width: theme.borderWidth
            border.color: control.down ? theme.accent : theme.borderSubtle
        }
    }

    component EditorHeading: Text {
        font.family: theme.fontFamily
        font.pixelSize: theme.fontSizeSmall
        font.weight: Font.DemiBold
        color: theme.textMuted
        font.letterSpacing: 1.0
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.headerHeight
            color: theme.surfacePrimary
            border.color: theme.borderSubtle
            border.width: theme.borderWidth

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: theme.space4
                anchors.rightMargin: theme.space4
                spacing: theme.space3

                RowLayout {
                    spacing: theme.space2
                    Text {
                        text: "DATUMLOOP"
                        color: theme.textPrimary
                        font.family: theme.fontFamily
                        font.pixelSize: theme.fontSizeTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "DS"
                        color: theme.accent
                        font.family: theme.fontFamily
                        font.pixelSize: theme.fontSizeTitle
                        font.weight: Font.Bold
                    }
                    Rectangle {
                        Layout.preferredWidth: theme.borderWidth
                        Layout.preferredHeight: theme.rowHeight
                        color: theme.borderSubtle
                        Layout.leftMargin: theme.space2
                        Layout.rightMargin: theme.space2
                    }
                }

                RowLayout {
                    spacing: theme.space1
                    DlButton { text: "Design"; Layout.preferredWidth: 82; background: Rectangle { radius: theme.panelRadius; color: theme.selectionPrimary; border.color: theme.accent; border.width: theme.borderWidth } }
                    DlButton { text: "Assembly"; enabled: false; Layout.preferredWidth: 88 }
                    DlButton { text: "Simulation"; enabled: false; Layout.preferredWidth: 92 }
                    DlButton { text: "Manufacturing"; enabled: false; Layout.preferredWidth: 112 }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: "Untitled Mechanism"
                    color: theme.textSecondary
                    font.family: theme.fontFamily
                    font.pixelSize: theme.fontSizeBody
                    elide: Text.ElideRight
                }
                Rectangle {
                    Layout.preferredWidth: theme.borderWidth
                    Layout.preferredHeight: theme.rowHeight
                    color: theme.borderSubtle
                }
                DlButton {
                    text: "Save"
                    Layout.preferredWidth: 72
                    ToolTip.visible: hovered
                    ToolTip.text: "Native project persistence is being added during migration"
                }
            }
        }

        SplitView {
            id: editors
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: theme.splitterWidth
                implicitHeight: theme.splitterWidth
                color: theme.surfaceRoot
                Rectangle {
                    anchors.centerIn: parent
                    width: theme.borderWidth
                    height: parent.height
                    color: theme.borderSubtle
                }
            }

            Rectangle {
                SplitView.preferredWidth: 248
                SplitView.minimumWidth: 192
                color: theme.surfacePrimary

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.panelPadding
                    spacing: theme.space3

                    RowLayout {
                        Layout.fillWidth: true
                        EditorHeading { text: "ENGINEERING OUTLINER"; Layout.fillWidth: true }
                        DlButton { text: "⋯"; Layout.preferredWidth: 34; ToolTip.text: "Outliner options"; ToolTip.visible: hovered }
                    }

                    TextField {
                        id: outlinerSearch
                        Layout.fillWidth: true
                        implicitHeight: theme.controlHeight
                        placeholderText: "Filter components"
                        color: theme.textPrimary
                        placeholderTextColor: theme.textMuted
                        font.family: theme.fontFamily
                        font.pixelSize: theme.fontSizeBody
                        background: Rectangle {
                            radius: theme.panelRadius
                            color: theme.surfaceRoot
                            border.color: outlinerSearch.activeFocus ? theme.accent : theme.borderSubtle
                            border.width: theme.borderWidth
                        }
                    }

                    ListView {
                        id: objectList
                        objectName: "engineeringOutliner"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: documentModel
                        currentIndex: documentModel.selectedRow
                        spacing: theme.space1

                        delegate: Rectangle {
                            required property string name
                            required property string objectId
                            required property bool isSelected
                            required property int index
                            width: objectList.width
                            height: theme.rowHeight
                            radius: theme.panelRadius
                            color: isSelected ? theme.selectionPrimary : theme.surfaceTransparent
                            border.width: isSelected ? theme.borderWidth : 0
                            border.color: isSelected ? theme.accent : theme.surfaceTransparent

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: theme.space2
                                anchors.rightMargin: theme.space2
                                spacing: theme.space2
                                Text {
                                    text: "◈"
                                    color: theme.accent
                                    font.pixelSize: theme.fontSizeBody
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        text: name
                                        color: theme.textPrimary
                                        font.family: theme.fontFamily
                                        font.pixelSize: theme.fontSizeBody
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: "Solid  ·  OCCT"
                                        color: theme.textMuted
                                        font.family: theme.fontFamily
                                        font.pixelSize: theme.fontSizeSmall
                                    }
                                }
                                Text {
                                    text: "●"
                                    color: theme.engineeringNominal
                                    font.pixelSize: theme.fontSizeSmall
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: documentModel.selectRow(index)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: objectList.count === 0
                            text: "No components"
                            color: theme.textMuted
                            font.family: theme.fontFamily
                            font.pixelSize: theme.fontSizeBody
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        radius: theme.panelRadius
                        color: theme.surfaceSecondary
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: theme.space2
                            anchors.rightMargin: theme.space2
                            Text { text: "1 component"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                            Item { Layout.fillWidth: true }
                            Text { text: "Design"; color: theme.accent; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                        }
                    }
                }
            }

            Rectangle {
                id: viewportEditor
                objectName: "cadViewportEditor"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 450
                color: theme.viewportBackground

                View3D {
                    id: viewport
                    objectName: "cadViewport"
                    anchors.fill: parent
                    camera: camera
                    environment: SceneEnvironment {
                        backgroundMode: SceneEnvironment.Color
                        clearColor: theme.viewportBackground
                        antialiasingMode: SceneEnvironment.MSAA
                        antialiasingQuality: SceneEnvironment.High
                    }

                    PerspectiveCamera {
                        id: camera
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
                        ambientColor: theme.textSecondary
                    }

                    Model {
                        id: solidModel
                        objectName: "datumLoopExactSolid"
                        geometry: ExactShapeGeometry {
                            id: cadGeometry
                            objectName: "cadGeometry"
                        }
                        position: documentModel.displayPosition
                        materials: PrincipledMaterial {
                            baseColor: documentModel.selectedRow >= 0 ? theme.accent : theme.surfaceRaised
                            metalness: 0.28
                            roughness: 0.32
                        }
                        castsShadows: true
                        pickable: true
                    }
                }

                MouseArea {
                    id: viewportInput
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
                                documentModel.selectRow(0)
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
                    anchors.margins: theme.space3
                    width: 164
                    height: 33
                    radius: theme.panelRadius
                    color: theme.surfacePrimary
                    border.width: theme.borderWidth
                    border.color: theme.borderSubtle
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.space2
                        anchors.rightMargin: theme.space2
                        Text { text: "DESIGN / PART"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Text { text: "●"; color: theme.engineeringNominal; font.pixelSize: theme.fontSizeSmall }
                    }
                }

                ComboBox {
                    id: viewSelector
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: theme.space3
                    width: 136
                    height: theme.controlHeight
                    model: ["Perspective", "Front", "Right", "Top"]
                    font.family: theme.fontFamily
                    font.pixelSize: theme.fontSizeSmall
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
                    anchors.leftMargin: theme.space3
                    spacing: theme.space1
                    Repeater {
                        model: [
                            { axis: "X", color: theme.axisX },
                            { axis: "Y", color: theme.axisY },
                            { axis: "Z", color: theme.axisZ }
                        ]
                        delegate: DlButton {
                            required property var modelData
                            text: modelData.axis
                            width: 36
                            height: 30
                            contentItem: Text {
                                text: parent.text
                                color: modelData.color
                                font.family: theme.fontFamily
                                font.pixelSize: theme.fontSizeBody
                                font.weight: Font.Bold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "Move along " + modelData.axis
                            onClicked: {
                                if (!documentModel.moveActive)
                                    documentModel.beginMove()
                                documentModel.setMoveAxis(modelData.axis)
                            }
                        }
                    }
                }

                Rectangle {
                    visible: documentModel.moveActive
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: theme.space4
                    width: Math.min(parent.width - theme.space4 * 2, 500)
                    height: 62
                    radius: theme.panelRadius
                    color: theme.surfacePrimary
                    border.width: theme.borderWidth
                    border.color: theme.accent

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: theme.space2
                        spacing: theme.space2
                        Text {
                            text: "MOVE"
                            color: theme.accent
                            font.family: theme.fontFamily
                            font.pixelSize: theme.fontSizeSmall
                            font.weight: Font.Bold
                        }
                        Repeater {
                            model: ["X", "Y", "Z"]
                            delegate: DlButton {
                                required property string modelData
                                text: modelData
                                Layout.preferredWidth: 34
                                highlighted: documentModel.moveAxis === modelData
                                onClicked: documentModel.setMoveAxis(modelData)
                            }
                        }
                        TextField {
                            id: moveDistance
                            Layout.fillWidth: true
                            implicitHeight: theme.controlHeight
                            text: documentModel.moveValue
                            selectByMouse: true
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            color: theme.textPrimary
                            font.family: theme.fontFamily
                            font.pixelSize: theme.fontSizeBody
                            placeholderText: "Distance"
                            onTextEdited: documentModel.setMoveValue(text)
                            background: Rectangle {
                                radius: theme.panelRadius
                                color: theme.surfaceRoot
                                border.color: moveDistance.activeFocus ? theme.accent : theme.borderSubtle
                                border.width: theme.borderWidth
                            }
                        }
                        Text { text: "mm"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                        DlButton { text: "Cancel"; Layout.preferredWidth: 70; onClicked: documentModel.cancelMove() }
                        DlButton {
                            text: "Confirm"
                            Layout.preferredWidth: 82
                            background: Rectangle { radius: theme.panelRadius; color: theme.selectionPrimary; border.color: theme.accent; border.width: theme.borderWidth }
                            onClicked: documentModel.confirmMove()
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: theme.space3
                    width: 160
                    height: 28
                    radius: theme.panelRadius
                    color: theme.surfacePrimary
                    border.color: theme.borderSubtle
                    border.width: theme.borderWidth
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.space2
                        anchors.rightMargin: theme.space2
                        Text { text: "X"; color: theme.axisX; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall; font.weight: Font.Bold }
                        Text { text: "Y"; color: theme.axisY; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall; font.weight: Font.Bold }
                        Text { text: "Z"; color: theme.axisZ; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall; font.weight: Font.Bold }
                        Item { Layout.fillWidth: true }
                        Text { text: "mm"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                    }
                }

                Connections {
                    target: documentModel
                    function onFrameSelectedRequested(position) {
                        camera.orbitTarget = position
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: 302
                SplitView.minimumWidth: 235
                color: theme.surfacePrimary

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: theme.panelPadding
                    spacing: theme.space3

                    RowLayout {
                        Layout.fillWidth: true
                        EditorHeading { text: "PROPERTIES"; Layout.fillWidth: true }
                        DlButton { text: "⋯"; Layout.preferredWidth: 34; ToolTip.text: "Property options"; ToolTip.visible: hovered }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        radius: theme.panelRadius
                        color: theme.surfaceSecondary
                        border.color: theme.borderSubtle
                        border.width: theme.borderWidth
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space2
                            spacing: theme.space1
                            Text { text: documentModel.selectedName; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeHeading; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: documentModel.selectedType; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                        }
                    }

                    EditorHeading { text: "PLACEMENT" }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: theme.panelRadius
                        color: theme.surfaceSecondary
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space2
                            spacing: theme.space2
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Position"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeBody; Layout.fillWidth: true }
                                Text { text: "X   Y   Z"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                            }
                            Text {
                                text: documentModel.selectedPositionText
                                color: documentModel.moveActive ? theme.accent : theme.textPrimary
                                font.family: theme.fontFamily
                                font.pixelSize: theme.fontSizeBody
                                Layout.fillWidth: true
                            }
                        }
                    }

                    EditorHeading { text: "EXACT GEOMETRY" }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        radius: theme.panelRadius
                        color: theme.surfaceSecondary
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space2
                            spacing: theme.space2
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Bounds"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeBody; Layout.fillWidth: true }
                                Text { text: documentModel.selectedBoundsText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Volume"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeBody; Layout.fillWidth: true }
                                Text { text: documentModel.selectedVolumeText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall; horizontalAlignment: Text.AlignRight }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Validity"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeBody; Layout.fillWidth: true }
                                Text { text: "Valid"; color: theme.engineeringNominal; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                            }
                        }
                    }

                    EditorHeading { text: "DATUMS" }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        radius: theme.panelRadius
                        color: theme.surfaceSecondary
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: theme.space2
                            spacing: theme.space1
                            Text { text: "Origin"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeBody }
                            Text { text: "Global coordinate frame"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true
                        DlButton { text: "Undo"; enabled: documentModel.canUndo; Layout.fillWidth: true; onClicked: documentModel.undo() }
                        DlButton { text: "Redo"; enabled: documentModel.canRedo; Layout.fillWidth: true; onClicked: documentModel.redo() }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.statusHeight
            color: theme.surfacePrimary
            border.color: theme.borderSubtle
            border.width: theme.borderWidth
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: theme.space3
                anchors.rightMargin: theme.space3
                spacing: theme.space2
                Rectangle { width: 6; height: 6; radius: 3; color: documentModel.moveActive ? theme.engineeringWarning : theme.engineeringNominal }
                Text {
                    Layout.fillWidth: true
                    text: documentModel.statusText
                    color: documentModel.moveActive ? theme.engineeringWarning : theme.textSecondary
                    font.family: theme.fontFamily
                    font.pixelSize: theme.fontSizeSmall
                    elide: Text.ElideRight
                }
                Text { text: "OCCT 7  ·  Exact B-Rep"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
                Rectangle { width: theme.borderWidth; height: theme.rowHeight / 2; color: theme.borderSubtle }
                Text { text: "mm"; color: theme.textSecondary; font.family: theme.fontFamily; font.pixelSize: theme.fontSizeSmall }
            }
        }
    }
}

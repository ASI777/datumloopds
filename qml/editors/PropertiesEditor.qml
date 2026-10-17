import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: editor
    objectName: "propertiesEditor"
    property var theme

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: editor.theme.panelPadding
        spacing: editor.theme.space3

        RowLayout {
            Layout.fillWidth: true
            EditorHeading { theme: editor.theme; text: "PROPERTIES"; Layout.fillWidth: true }
            DlButton { theme: editor.theme; text: "⋯"; Layout.preferredWidth: 34; ToolTip.text: "Property options"; ToolTip.visible: hovered }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            radius: editor.theme.panelRadius
            color: editor.theme.surfaceSecondary
            border.color: editor.theme.borderSubtle
            border.width: editor.theme.borderWidth
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: editor.theme.space2
                spacing: editor.theme.space1
                Text { text: documentModel.selectedName; color: editor.theme.textPrimary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeHeading; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: documentModel.selectedType; color: editor.theme.textMuted; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
            }
        }

        EditorHeading { theme: editor.theme; text: "PLACEMENT" }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 82
            radius: editor.theme.panelRadius
            color: editor.theme.surfaceSecondary
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: editor.theme.space2
                spacing: editor.theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Position"; color: editor.theme.textSecondary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; Layout.fillWidth: true }
                    Text { text: "X   Y   Z"; color: editor.theme.textMuted; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
                }
                Text {
                    text: documentModel.selectedPositionText
                    color: documentModel.moveActive ? editor.theme.accent : editor.theme.textPrimary
                    font.family: editor.theme.fontFamily
                    font.pixelSize: editor.theme.fontSizeBody
                    Layout.fillWidth: true
                }
            }
        }

        EditorHeading { theme: editor.theme; text: "EXACT GEOMETRY" }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            radius: editor.theme.panelRadius
            color: editor.theme.surfaceSecondary
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: editor.theme.space2
                spacing: editor.theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Bounds"; color: editor.theme.textSecondary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; Layout.fillWidth: true }
                    Text { text: documentModel.selectedBoundsText; color: editor.theme.textPrimary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall; horizontalAlignment: Text.AlignRight }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Volume"; color: editor.theme.textSecondary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; Layout.fillWidth: true }
                    Text { text: documentModel.selectedVolumeText; color: editor.theme.textPrimary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall; horizontalAlignment: Text.AlignRight }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Validity"; color: editor.theme.textSecondary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody; Layout.fillWidth: true }
                    Text { text: "Valid"; color: editor.theme.engineeringNominal; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
                }
            }
        }

        EditorHeading { theme: editor.theme; text: "DATUMS" }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            radius: editor.theme.panelRadius
            color: editor.theme.surfaceSecondary
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: editor.theme.space2
                spacing: editor.theme.space1
                Text { text: "Origin"; color: editor.theme.textPrimary; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeBody }
                Text { text: "Global coordinate frame"; color: editor.theme.textMuted; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            DlButton { theme: editor.theme; objectName: "undoMoveButton"; text: "Undo"; enabled: documentModel.canUndo; Layout.fillWidth: true; onClicked: documentModel.undo() }
            DlButton { theme: editor.theme; objectName: "redoMoveButton"; text: "Redo"; enabled: documentModel.canRedo; Layout.fillWidth: true; onClicked: documentModel.redo() }
        }
    }
}

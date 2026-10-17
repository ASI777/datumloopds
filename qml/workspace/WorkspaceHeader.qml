import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: header
    signal openProjectRequested()
    signal saveProjectRequested()
    property var theme
    Layout.preferredHeight: theme.headerHeight
    color: theme.surfacePrimary
    border.color: theme.borderSubtle
    border.width: theme.borderWidth

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: header.theme.space4
        anchors.rightMargin: header.theme.space4
        spacing: header.theme.space3

        RowLayout {
            spacing: header.theme.space2
            Text {
                text: "DATUMLOOP"
                color: header.theme.textPrimary
                font.family: header.theme.fontFamily
                font.pixelSize: header.theme.fontSizeTitle
                font.weight: Font.DemiBold
            }
            Text {
                text: "DS"
                color: header.theme.accent
                font.family: header.theme.fontFamily
                font.pixelSize: header.theme.fontSizeTitle
                font.weight: Font.Bold
            }
            Rectangle {
                Layout.preferredWidth: header.theme.borderWidth
                Layout.preferredHeight: header.theme.rowHeight
                color: header.theme.borderSubtle
                Layout.leftMargin: header.theme.space2
                Layout.rightMargin: header.theme.space2
            }
        }

        RowLayout {
            spacing: header.theme.space1
            DlButton { theme: header.theme; text: "Design"; Layout.preferredWidth: 82; background: Rectangle { radius: header.theme.panelRadius; color: header.theme.selectionPrimary; border.color: header.theme.accent; border.width: header.theme.borderWidth } }
            DlButton { theme: header.theme; text: "Assembly"; enabled: false; Layout.preferredWidth: 88 }
            DlButton { theme: header.theme; text: "Simulation"; enabled: false; Layout.preferredWidth: 92 }
            DlButton { theme: header.theme; text: "Manufacturing"; enabled: false; Layout.preferredWidth: 112 }
        }

        Item { Layout.fillWidth: true }

        Text {
            text: documentModel.projectName + (documentModel.dirty ? " •" : "")
            color: header.theme.textSecondary
            font.family: header.theme.fontFamily
            font.pixelSize: header.theme.fontSizeBody
            elide: Text.ElideRight
        }
        Rectangle {
            Layout.preferredWidth: header.theme.borderWidth
            Layout.preferredHeight: header.theme.rowHeight
            color: header.theme.borderSubtle
        }
        DlButton {
            theme: header.theme
            text: "Open"
            Layout.preferredWidth: 72
            onClicked: header.openProjectRequested()
            ToolTip.visible: hovered
            ToolTip.text: "Open a native DatumLoop .dld project"
        }
        DlButton {
            theme: header.theme
            text: "Save"
            Layout.preferredWidth: 72
            onClicked: header.saveProjectRequested()
            ToolTip.visible: hovered
            ToolTip.text: "Save the native DatumLoop .dld project"
        }
    }
}

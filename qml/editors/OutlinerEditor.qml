import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: editor
    objectName: "outlinerEditor"
    property var theme

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: editor.theme.panelPadding
        spacing: editor.theme.space3

        RowLayout {
            Layout.fillWidth: true
            EditorHeading { theme: editor.theme; text: "ENGINEERING OUTLINER"; Layout.fillWidth: true }
            DlButton { theme: editor.theme; text: "⋯"; Layout.preferredWidth: 34; ToolTip.text: "Outliner options"; ToolTip.visible: hovered }
        }

        TextField {
            id: outlinerSearch
            Layout.fillWidth: true
            implicitHeight: editor.theme.controlHeight
            placeholderText: "Filter components"
            color: editor.theme.textPrimary
            placeholderTextColor: editor.theme.textMuted
            font.family: editor.theme.fontFamily
            font.pixelSize: editor.theme.fontSizeBody
            background: Rectangle {
                radius: editor.theme.panelRadius
                color: editor.theme.surfaceRoot
                border.color: outlinerSearch.activeFocus ? editor.theme.accent : editor.theme.borderSubtle
                border.width: editor.theme.borderWidth
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
            spacing: editor.theme.space1

            delegate: Rectangle {
                required property string name
                required property string objectId
                required property bool isSelected
                required property int index
                width: objectList.width
                height: editor.theme.rowHeight
                radius: editor.theme.panelRadius
                color: isSelected ? editor.theme.selectionPrimary : editor.theme.surfaceTransparent
                border.width: isSelected ? editor.theme.borderWidth : 0
                border.color: isSelected ? editor.theme.accent : editor.theme.surfaceTransparent

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: editor.theme.space2
                    anchors.rightMargin: editor.theme.space2
                    spacing: editor.theme.space2
                    Text { text: "◈"; color: editor.theme.accent; font.pixelSize: editor.theme.fontSizeBody }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: name
                            color: editor.theme.textPrimary
                            font.family: editor.theme.fontFamily
                            font.pixelSize: editor.theme.fontSizeBody
                            elide: Text.ElideRight
                        }
                        Text {
                            text: "Solid  ·  OCCT"
                            color: editor.theme.textMuted
                            font.family: editor.theme.fontFamily
                            font.pixelSize: editor.theme.fontSizeSmall
                        }
                    }
                    Text { text: "●"; color: editor.theme.engineeringNominal; font.pixelSize: editor.theme.fontSizeSmall }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: documentModel.selectObjectId(objectId)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: objectList.count === 0
                text: "No components"
                color: editor.theme.textMuted
                font.family: editor.theme.fontFamily
                font.pixelSize: editor.theme.fontSizeBody
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            radius: editor.theme.panelRadius
            color: editor.theme.surfaceSecondary
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: editor.theme.space2
                anchors.rightMargin: editor.theme.space2
                Text {
                    text: objectList.count === 1 ? "1 component" : objectList.count + " components"
                    color: editor.theme.textSecondary
                    font.family: editor.theme.fontFamily
                    font.pixelSize: editor.theme.fontSizeSmall
                }
                Item { Layout.fillWidth: true }
                Text { text: "Design"; color: editor.theme.accent; font.family: editor.theme.fontFamily; font.pixelSize: editor.theme.fontSizeSmall }
            }
        }
    }
}

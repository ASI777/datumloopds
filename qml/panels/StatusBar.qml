import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: statusBar
    property var theme
    Layout.preferredHeight: theme.statusHeight
    color: theme.surfacePrimary
    border.color: theme.borderSubtle
    border.width: theme.borderWidth

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: statusBar.theme.space3
        anchors.rightMargin: statusBar.theme.space3
        spacing: statusBar.theme.space2
        Rectangle {
            width: 6
            height: 6
            radius: 3
            color: documentModel.moveActive ? statusBar.theme.engineeringWarning : statusBar.theme.engineeringNominal
        }
        Text {
            Layout.fillWidth: true
            text: documentModel.statusText
            color: documentModel.moveActive ? statusBar.theme.engineeringWarning : statusBar.theme.textSecondary
            font.family: statusBar.theme.fontFamily
            font.pixelSize: statusBar.theme.fontSizeSmall
            elide: Text.ElideRight
        }
        Text { text: "OCCT 7  ·  Exact B-Rep"; color: statusBar.theme.textMuted; font.family: statusBar.theme.fontFamily; font.pixelSize: statusBar.theme.fontSizeSmall }
        Rectangle { width: statusBar.theme.borderWidth; height: statusBar.theme.rowHeight / 2; color: statusBar.theme.borderSubtle }
        Text { text: "mm"; color: statusBar.theme.textSecondary; font.family: statusBar.theme.fontFamily; font.pixelSize: statusBar.theme.fontSizeSmall }
    }
}

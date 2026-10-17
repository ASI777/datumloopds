import QtQuick
import QtQuick.Controls

Button {
    id: control
    property var theme
    implicitHeight: theme.controlHeight
    font.family: theme.fontFamily
    font.pixelSize: theme.fontSizeBody
    contentItem: Text {
        text: control.text
        color: control.enabled ? control.theme.textPrimary : control.theme.textMuted
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: control.theme.panelRadius
        color: !control.enabled ? control.theme.surfacePrimary
              : control.down ? control.theme.selectionPrimary
              : control.hovered ? control.theme.surfaceHover : control.theme.surfaceSecondary
        border.width: control.theme.borderWidth
        border.color: control.down ? control.theme.accent : control.theme.borderSubtle
    }
}

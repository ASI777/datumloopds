import QtQuick

Text {
    property var theme
    font.family: theme.fontFamily
    font.pixelSize: theme.fontSizeSmall
    font.weight: Font.DemiBold
    color: theme.textMuted
    font.letterSpacing: 1.0
}

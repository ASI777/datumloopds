import QtQuick

QtObject {
    readonly property color surfaceRoot: "#11171d"
    readonly property color surfacePrimary: "#182129"
    readonly property color surfaceSecondary: "#202b35"
    readonly property color surfaceRaised: "#293742"
    readonly property color surfaceHover: "#334451"
    readonly property color surfaceTransparent: "transparent"
    readonly property color viewportBackground: "#121b22"
    readonly property color borderSubtle: "#33434e"
    readonly property color borderStrong: "#435764"

    readonly property color textPrimary: "#ecf2f5"
    readonly property color textSecondary: "#aebbc3"
    readonly property color textMuted: "#768791"
    readonly property color selectionPrimary: "#176b69"
    readonly property color selectionHover: "#224d54"
    readonly property color accent: "#42d3c4"
    readonly property color engineeringWarning: "#e9b85e"
    readonly property color engineeringCritical: "#f07670"
    readonly property color engineeringNominal: "#64c89a"
    readonly property color axisX: "#ee746e"
    readonly property color axisY: "#67c890"
    readonly property color axisZ: "#5fa9ee"

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int panelPadding: 12
    readonly property int rowHeight: 34
    readonly property int controlHeight: 30
    readonly property int headerHeight: 54
    readonly property int statusHeight: 28
    readonly property int panelRadius: 6
    readonly property int borderWidth: 1
    readonly property int splitterWidth: 5

    readonly property string fontFamily: "Inter, Noto Sans, Sans Serif"
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeBody: 13
    readonly property int fontSizeHeading: 15
    readonly property int fontSizeTitle: 18
    readonly property int animationFast: 90
    readonly property int animationNormal: 150
}

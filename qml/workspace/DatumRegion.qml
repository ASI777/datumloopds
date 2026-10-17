import QtQuick

Rectangle {
    id: region
    property var theme
    property string regionName: ""
    default property alias contentData: content.data
    color: theme.surfacePrimary

    Item {
        id: content
        anchors.fill: parent
    }
}

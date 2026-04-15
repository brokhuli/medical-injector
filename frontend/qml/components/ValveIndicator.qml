import QtQuick
import QtQuick.Layouts
import ".." as App

Item {
    id: root

    property string label: "Contrast"
    property bool open: false
    property color openColor: App.Theme.injecting

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    RowLayout {
        id: row
        spacing: 6

        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: root.open ? root.openColor : Qt.rgba(1, 1, 1, 0.15)
        }

        Text {
            text: root.label + (root.open ? " OPEN" : " closed")
            color: root.open ? App.Theme.text : App.Theme.textSecondary
            font.pixelSize: App.Theme.fontSizeSmall
            font.bold: root.open
        }
    }
}

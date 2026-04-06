import QtQuick
import ".." as App

Rectangle {
    id: root

    property string state: "Idle"

    implicitWidth: 200
    implicitHeight: 48
    radius: 8

    color: {
        switch (root.state) {
            case "Idle":      return App.Theme.idle
            case "Armed":     return App.Theme.armed
            case "Injecting": return App.Theme.injecting
            case "Paused":    return App.Theme.paused
            case "Fault":     return App.Theme.fault
            case "Completed": return App.Theme.completed
            default:          return App.Theme.surface
        }
    }

    Text {
        anchors.centerIn: parent
        text: root.state.toUpperCase()
        color: "#ffffff"
        font.pixelSize: App.Theme.fontSizeLarge
        font.bold: true
    }
}

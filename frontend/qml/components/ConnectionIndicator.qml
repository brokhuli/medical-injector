import QtQuick
import QtQuick.Layouts
import ".." as App

Item {
    id: root

    property string status: "disconnected"

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    RowLayout {
        id: row
        spacing: App.Theme.spacingSmall

        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: {
                switch (root.status) {
                    case "connected":    return App.Theme.connected
                    case "reconnecting":
                    case "connecting":   return App.Theme.reconnecting
                    default:             return App.Theme.disconnected
                }
            }

            SequentialAnimation on opacity {
                running: root.status === "reconnecting" || root.status === "connecting"
                loops: Animation.Infinite
                NumberAnimation { to: 0.3; duration: 600 }
                NumberAnimation { to: 1.0; duration: 600 }
            }
        }

        Text {
            text: {
                switch (root.status) {
                    case "connected":    return "Connected"
                    case "connecting":   return "Connecting..."
                    case "reconnecting": return "Reconnecting..."
                    default:             return "Disconnected"
                }
            }
            color: App.Theme.text
            font.pixelSize: App.Theme.fontSizeMedium
        }
    }
}

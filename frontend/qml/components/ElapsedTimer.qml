import QtQuick
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property double elapsed: 0.0

    implicitHeight: 32
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        spacing: App.Theme.spacingSmall

        Text {
            text: "Elapsed"
            color: App.Theme.textSecondary
            font.pixelSize: App.Theme.fontSizeSmall
            font.bold: true
        }

        Text {
            text: {
                var min = Math.floor(elapsed / 60)
                var sec = (elapsed % 60).toFixed(1)
                return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
            }
            color: App.Theme.text
            font.pixelSize: App.Theme.fontSizeLarge
            font.family: "Consolas, monospace"
            font.bold: true
        }
    }
}

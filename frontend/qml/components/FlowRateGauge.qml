import QtQuick
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property double targetFlow: 0.0
    property double actualFlow: 0.0

    readonly property double ratio: targetFlow > 0 ? Math.min(actualFlow / targetFlow, 1.5) : 0

    implicitHeight: 64
    color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: App.Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "Flow Rate"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: actualFlow.toFixed(2) + " / " + targetFlow.toFixed(2) + " mL/s"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeMedium
                font.family: "Consolas, monospace"
            }
        }

        // Bar background
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            color: Qt.rgba(1, 1, 1, 0.05)
            radius: 4

            // Target indicator line
            Rectangle {
                x: parent.width * Math.min(targetFlow / 10.0, 1.0) - 1
                width: 2
                height: parent.height
                color: App.Theme.textSecondary
                visible: targetFlow > 0
            }

            // Actual flow bar
            Rectangle {
                width: parent.width * Math.min(actualFlow / 10.0, 1.0)
                height: parent.height
                radius: 4
                color: {
                    if (targetFlow <= 0) return App.Theme.injecting
                    var diff = Math.abs(actualFlow - targetFlow) / targetFlow
                    if (diff < 0.05) return App.Theme.injecting
                    if (diff < 0.15) return App.Theme.armed
                    return App.Theme.fault
                }

                Behavior on width { NumberAnimation { duration: 100 } }
            }
        }
    }
}

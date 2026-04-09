import QtQuick
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property double pressure: 0.0
    property double pressureLimit: 250.0

    readonly property double maxScale: 400.0
    readonly property double ratio: Math.min(pressure / maxScale, 1.0)
    readonly property double limitRatio: Math.min(pressureLimit / maxScale, 1.0)

    implicitHeight: 64
    color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: App.Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "Pressure"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: pressure.toFixed(1) + " psi"
                color: {
                    if (pressure >= pressureLimit) return App.Theme.fault
                    if (pressure >= pressureLimit * 0.85) return App.Theme.armed
                    return App.Theme.text
                }
                font.pixelSize: App.Theme.fontSizeMedium
                font.family: "Consolas, monospace"
            }

            Text {
                text: "(limit: " + pressureLimit.toFixed(0) + ")"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
            }
        }

        // Bar background with color zones
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            color: Qt.rgba(1, 1, 1, 0.05)
            radius: 4

            // Green zone (0-70% of limit)
            Rectangle {
                width: parent.width * limitRatio * 0.7
                height: parent.height
                radius: 4
                color: Qt.rgba(App.Theme.idle.r, App.Theme.idle.g, App.Theme.idle.b, 0.15)
            }

            // Yellow zone (70-85% of limit)
            Rectangle {
                x: parent.width * limitRatio * 0.7
                width: parent.width * limitRatio * 0.15
                height: parent.height
                color: Qt.rgba(App.Theme.armed.r, App.Theme.armed.g, App.Theme.armed.b, 0.15)
            }

            // Red zone (85-100% of limit)
            Rectangle {
                x: parent.width * limitRatio * 0.85
                width: parent.width * limitRatio * 0.15
                height: parent.height
                color: Qt.rgba(App.Theme.fault.r, App.Theme.fault.g, App.Theme.fault.b, 0.15)
            }

            // Pressure limit line
            Rectangle {
                x: parent.width * limitRatio - 1
                width: 2
                height: parent.height
                color: App.Theme.fault
            }

            // Actual pressure bar
            Rectangle {
                width: parent.width * ratio
                height: parent.height
                radius: 4
                color: {
                    if (pressure >= pressureLimit) return App.Theme.fault
                    if (pressure >= pressureLimit * 0.85) return App.Theme.armed
                    return App.Theme.idle
                }
                opacity: 0.8

                Behavior on width { NumberAnimation { duration: 100 } }
            }
        }
    }
}

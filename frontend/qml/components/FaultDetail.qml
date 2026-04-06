import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge
    property bool acknowledged: false

    readonly property bool hasFaults: bridge.activeFaults.length > 0
    readonly property bool isFaultState: bridge.injectorState === "Fault"

    visible: isFaultState || hasFaults
    color: Qt.rgba(App.Theme.fault.r, App.Theme.fault.g, App.Theme.fault.b, 0.15)
    radius: 8
    border.color: App.Theme.fault
    border.width: 1

    implicitHeight: visible ? col.implicitHeight + 2 * App.Theme.spacingLarge : 0

    // Reset acknowledged state when faults clear
    onHasFaultsChanged: {
        if (!hasFaults) acknowledged = false
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: App.Theme.spacingLarge
        spacing: App.Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            Rectangle {
                width: 10
                height: 10
                radius: 5
                color: App.Theme.fault

                SequentialAnimation on opacity {
                    running: root.isFaultState && !root.acknowledged
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }

            Text {
                text: "FAULT DETECTED"
                color: App.Theme.fault
                font.pixelSize: App.Theme.fontSizeMedium
                font.bold: true
                Layout.fillWidth: true
            }
        }

        // Fault details list
        Repeater {
            model: bridge.activeFaults

            RowLayout {
                Layout.fillWidth: true
                spacing: App.Theme.spacingSmall

                Text {
                    text: "\u2022"
                    color: App.Theme.fault
                    font.pixelSize: App.Theme.fontSizeMedium
                }

                Text {
                    text: modelData.details || "Unknown fault"
                    color: App.Theme.text
                    font.pixelSize: App.Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    text: {
                        var ts = modelData.timestamp || 0
                        var min = Math.floor(ts / 60)
                        var sec = (ts % 60).toFixed(1)
                        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
                    }
                    color: App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                }
            }
        }

        // Action buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall
            visible: root.isFaultState

            Button {
                text: "Acknowledge"
                visible: !root.acknowledged
                enabled: root.isFaultState
                Layout.fillWidth: true
                Layout.preferredHeight: 36

                background: Rectangle {
                    color: parent.enabled
                           ? (parent.hovered ? Qt.lighter(App.Theme.armed, 1.2) : App.Theme.armed)
                           : Qt.rgba(1, 1, 1, 0.05)
                    radius: 6
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#1a1a1a" : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: root.acknowledged = true
            }

            Button {
                text: "Reset"
                visible: root.acknowledged
                enabled: root.isFaultState
                Layout.fillWidth: true
                Layout.preferredHeight: 36

                background: Rectangle {
                    color: parent.enabled
                           ? (parent.hovered ? Qt.lighter(App.Theme.idle, 1.2) : App.Theme.idle)
                           : Qt.rgba(1, 1, 1, 0.05)
                    radius: 6
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#ffffff" : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: bridge.reset()
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge
    readonly property string state: bridge.injectorState
    readonly property bool isConnected: bridge.connectionStatus === "connected"

    // Button enable/disable logic per spec 03b state table
    readonly property var buttonStates: ({
        "Idle":      { arm: true,  disarm: false, start: false, pause: false, resume: false, reset: false },
        "Armed":     { arm: false, disarm: true,  start: true,  pause: false, resume: false, reset: false },
        "Injecting": { arm: false, disarm: false, start: false, pause: true,  resume: false, reset: false },
        "Paused":    { arm: false, disarm: false, start: false, pause: false, resume: true,  reset: false },
        "Fault":     { arm: false, disarm: false, start: false, pause: false, resume: false, reset: true  },
        "Completed": { arm: false, disarm: false, start: false, pause: false, resume: false, reset: false }
    })

    readonly property var currentButtons: buttonStates[state] || buttonStates["Idle"]

    color: App.Theme.surface
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: App.Theme.spacingLarge
        spacing: App.Theme.spacingMedium

        // State indicator + label row
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingLarge

            StateIndicator {
                state: root.state
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: "Injection Control"
                    color: App.Theme.text
                    font.pixelSize: App.Theme.fontSizeMedium
                    font.bold: true
                }

                Text {
                    text: {
                        if (!root.isConnected) return "Disconnected"
                        switch (root.state) {
                            case "Idle":      return "Ready \u2014 load protocol and arm"
                            case "Armed":     return "Armed \u2014 ready to inject"
                            case "Injecting": return "Phase " + (bridge.phaseIndex + 1) + " in progress"
                            case "Paused":    return "Injection paused"
                            case "Fault":     return "Fault detected \u2014 acknowledge and reset"
                            case "Completed": return "Injection complete"
                            default:          return ""
                        }
                    }
                    color: App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                }
            }
        }

        // Control buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            // Arm / Disarm
            Button {
                id: armBtn
                text: root.state === "Armed" ? "Disarm" : "Arm"
                enabled: root.isConnected && (currentButtons.arm || currentButtons.disarm)
                Layout.fillWidth: true
                Layout.preferredHeight: 40

                background: Rectangle {
                    color: parent.enabled
                           ? (parent.hovered ? Qt.lighter(App.Theme.armed, 1.2) : App.Theme.armed)
                           : Qt.rgba(1, 1, 1, 0.05)
                    radius: 6
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#1a1a1a" : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeMedium
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    if (root.state === "Armed") bridge.disarm()
                    else bridge.arm()
                }
            }

            // Start / Pause / Resume
            Button {
                id: actionBtn
                text: {
                    if (root.state === "Injecting") return "Pause"
                    if (root.state === "Paused") return "Resume"
                    return "Start"
                }
                enabled: root.isConnected && (currentButtons.start || currentButtons.pause || currentButtons.resume)
                Layout.fillWidth: true
                Layout.preferredHeight: 40

                background: Rectangle {
                    color: {
                        if (!parent.enabled) return Qt.rgba(1, 1, 1, 0.05)
                        var base = root.state === "Injecting" ? App.Theme.paused : App.Theme.injecting
                        return parent.hovered ? Qt.lighter(base, 1.2) : base
                    }
                    radius: 6
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#ffffff" : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeMedium
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: {
                    if (root.state === "Injecting") bridge.pause()
                    else if (root.state === "Paused") bridge.resume()
                    else bridge.start()
                }
            }

            // Reset (visible in Fault/Completed)
            Button {
                id: resetBtn
                text: "Reset"
                visible: root.state === "Fault" || root.state === "Completed"
                enabled: root.isConnected && currentButtons.reset
                Layout.fillWidth: true
                Layout.preferredHeight: 40

                background: Rectangle {
                    color: parent.enabled
                           ? (parent.hovered ? Qt.lighter(App.Theme.textSecondary, 1.3) : App.Theme.completed)
                           : Qt.rgba(1, 1, 1, 0.05)
                    radius: 6
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#ffffff" : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeMedium
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: bridge.reset()
            }
        }

        // Emergency Stop — always enabled when connected
        Button {
            id: estopBtn
            text: "\u25A0  EMERGENCY STOP"
            enabled: root.isConnected
            Layout.fillWidth: true
            Layout.preferredHeight: 48

            background: Rectangle {
                color: parent.enabled
                       ? (parent.hovered ? Qt.lighter(App.Theme.fault, 1.15) : App.Theme.fault)
                       : Qt.rgba(1, 1, 1, 0.05)
                radius: 6
                border.color: parent.enabled ? Qt.darker(App.Theme.fault, 1.3) : "transparent"
                border.width: 2
            }

            contentItem: Text {
                text: parent.text
                color: parent.enabled ? "#ffffff" : App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeMedium
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            onClicked: bridge.emergencyStop()
        }
    }
}

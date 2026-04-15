import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge
    property bool acknowledged: false

    signal reportRequested()

    readonly property bool hasFaults: bridge.activeFaults.length > 0
    readonly property bool isFaultState: bridge.injectorState === "Fault"

    visible: isFaultState || hasFaults
    color: App.Theme.surface
    radius: 8
    border.color: App.Theme.fault
    border.width: 1

    clip: true

    // Reset acknowledged state when faults clear
    onHasFaultsChanged: {
        if (!hasFaults) acknowledged = false
    }

    function formatFaultDetails(raw) {
        if (!raw) return "Unknown fault"
        var d
        try { d = JSON.parse(raw) } catch(e) { return raw }
        var ft  = d.fault_type  || ""
        var val = (d.value     !== undefined) ? d.value     : null
        var thr = (d.threshold !== undefined) ? d.threshold : null
        switch (ft) {
            case "OVERPRESSURE":
                return "Overpressure \u2014 " +
                       (val !== null ? val.toFixed(1) : "?") + " psi exceeded limit of " +
                       (thr !== null ? thr.toFixed(0) : "?") + " psi"
            case "AIR_BUBBLE":
                return "Air bubble detected in fluid line \u2014 injection halted"
            case "MOTOR_STALL":
                return "Motor stall \u2014 RPM divergence " +
                       (val !== null ? val.toFixed(0) : "?") + " RPM" +
                       (thr !== null ? " (threshold: " + thr.toFixed(0) + " RPM)" : "")
            case "TIMING_DELAY":
                return "Control loop overrun \u2014 " +
                       (val !== null ? val.toFixed(1) : "?") + " ms" +
                       (thr !== null ? " (limit: " + thr.toFixed(0) + " ms)" : "")
            default:
                return ft ? ft.replace(/_/g, " ") : raw
        }
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
                    text: root.formatFaultDetails(modelData.details || "")
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

        // Fallback when fault state has no specific fault events (e.g. manual emergency stop)
        RowLayout {
            visible: root.isFaultState && !root.hasFaults
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            Text {
                text: "\u2022"
                color: App.Theme.fault
                font.pixelSize: App.Theme.fontSizeMedium
            }

            Text {
                text: "Emergency stop activated \u2014 injection halted by operator"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
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

            Button {
                id: reportBtn
                text: "Show Fault Report"
                Layout.fillWidth: true
                Layout.preferredHeight: 36

                background: Rectangle {
                    color: reportBtn.hovered ? Qt.rgba(1, 1, 1, 0.14)
                                             : Qt.rgba(1, 1, 1, 0.08)
                    radius: 6
                    border.color: Qt.rgba(1, 1, 1, 0.18)
                    border.width: 1
                }

                contentItem: Text {
                    text: reportBtn.text
                    color: App.Theme.text
                    font.pixelSize: App.Theme.fontSizeSmall
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: root.reportRequested()
            }
        }
    }
}

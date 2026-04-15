import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge

    color: App.Theme.surface
    radius: 8

    function formatDetails(typeStr, rawDetails) {
        if (!rawDetails) return ""
        var d
        try { d = JSON.parse(rawDetails) } catch(e) { return rawDetails }
        switch(typeStr) {
            case "state_transition":
                return (d.from||"?") + " \u2192 " + (d.to||"?") +
                       (d.trigger ? "  [" + d.trigger + "]" : "")
            case "fault_detected":
                return (d.fault_type||"FAULT") + "  " +
                       (d.value !== undefined ? d.value.toFixed(1) : "?") + " / " +
                       (d.threshold !== undefined ? d.threshold.toFixed(0) : "?") + " psi"
            case "phase_transition":
                return "P" + ((d.from_phase||0)+1) + " \u2192 P" + ((d.to_phase||0)+1) +
                       "  " + (d.volume_delivered !== undefined ? d.volume_delivered.toFixed(1) : "?") +
                       "/" + (d.volume_programmed !== undefined ? d.volume_programmed.toFixed(0) : "?") + " mL"
            case "protocol_loaded":
                return (d.phase_count||"?") + " phases  " +
                       (d.total_volume !== undefined ? d.total_volume.toFixed(0) : "?") + " mL total"
            case "command_rejected":
                return "Rejected: " + (d.command||"?") + "  \u2014 " + (d.reason||"")
            default:
                return rawDetails
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: App.Theme.spacingMedium
        spacing: App.Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "Event Log"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: bridge.eventLog.length + " events"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
            }
        }

        ListView {
            id: eventList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: bridge.eventLog

            // Auto-scroll to bottom
            onCountChanged: {
                if (count > 0) positionViewAtEnd()
            }

            delegate: Rectangle {
                width: eventList.width
                height: 22
                color: index % 2 === 0 ? "transparent" : Qt.rgba(1, 1, 1, 0.02)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: App.Theme.spacingSmall
                    anchors.rightMargin: App.Theme.spacingSmall
                    spacing: App.Theme.spacingMedium

                    // Timestamp
                    Text {
                        text: {
                            var ts = modelData.timestamp || 0
                            var min = Math.floor(ts / 60)
                            var sec = (ts % 60).toFixed(1)
                            return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
                        }
                        color: App.Theme.textSecondary
                        font.pixelSize: 11
                        font.family: "Consolas, monospace"
                        Layout.preferredWidth: 56
                    }

                    // Event type indicator
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        color: {
                            var ts = modelData.typeStr || ""
                            if (ts === "fault_detected")   return App.Theme.fault
                            if (ts === "state_transition") return App.Theme.injecting
                            if (ts === "phase_transition") return "#22c55e"
                            if (ts === "command_rejected") return App.Theme.paused
                            return App.Theme.textSecondary
                        }
                    }

                    // Details
                    Text {
                        text: root.formatDetails(modelData.typeStr || "", modelData.details || "")
                        color: (modelData.typeStr === "fault_detected") ? App.Theme.fault : App.Theme.text
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                active: true
                policy: ScrollBar.AsNeeded
            }
        }

        // Empty state
        Text {
            text: "No events yet"
            color: App.Theme.textSecondary
            font.pixelSize: App.Theme.fontSizeSmall
            Layout.alignment: Qt.AlignHCenter
            visible: bridge.eventLog.length === 0
        }
    }
}

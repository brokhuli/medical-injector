import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import ".." as App

Dialog {
    id: root
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(900, Overlay.overlay.width - 80)
    height: Overlay.overlay.height - 80
    title: "Fault Report"

    property var bridge: injectorBridge

    // Snapshots captured on open so values don't drift while the dialog is visible.
    property var faultsSnapshot: []
    property var stateSnapshot: ({})
    property var telemetrySnapshot: []
    property var eventsSnapshot: []

    readonly property int maxTelemetryRows: 100
    readonly property int maxEventRows: 50

    onOpened: {
        faultsSnapshot = bridge.activeFaults.slice()

        // Prefer the snapshot captured by the bridge at fault-event time
        // (the backend halts the motor immediately, so live values lie).
        // Fall back to live values only if no fault has a snapshot yet.
        var faultSnap = null
        for (var i = 0; i < faultsSnapshot.length; ++i) {
            if (faultsSnapshot[i].stateAtFault) {
                faultSnap = faultsSnapshot[i].stateAtFault
                break
            }
        }

        if (faultSnap) {
            stateSnapshot = faultSnap
        } else {
            stateSnapshot = {
                injectorState:     bridge.injectorState,
                targetFlowRate:    bridge.targetFlowRate,
                actualFlowRate:    bridge.actualFlowRate,
                pressure:          bridge.pressure,
                motorRpm:          bridge.motorRpm,
                phaseIndex:        bridge.phaseIndex,
                elapsedTime:       bridge.elapsedTime,
                contrastRemaining: bridge.contrastRemaining,
                salineRemaining:   bridge.salineRemaining,
                contrastValve:     bridge.contrastValve,
                salineValve:       bridge.salineValve,
                meanTickMs:        bridge.meanTickMs,
                maxTickMs:         bridge.maxTickMs,
                overrunCount:      bridge.overrunCount
            }
        }

        var hist = bridge.telemetryHistory
        var histStart = Math.max(0, hist.length - maxTelemetryRows)
        telemetrySnapshot = hist.slice(histStart)

        var ev = bridge.eventLog
        var evStart = Math.max(0, ev.length - maxEventRows)
        eventsSnapshot = ev.slice(evStart)
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

    function formatTimestamp(ts) {
        var t = ts || 0
        var min = Math.floor(t / 60)
        var sec = (t % 60).toFixed(2)
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function prettyDetails(raw) {
        if (!raw) return ""
        try {
            var d = JSON.parse(raw)
            var parts = []
            for (var k in d) parts.push(k + "=" + d[k])
            return parts.join("  ")
        } catch(e) {
            return raw
        }
    }

    background: Rectangle {
        color: App.Theme.surface
        radius: 6
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
    }

    header: Rectangle {
        color: "transparent"
        implicitHeight: 44

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: App.Theme.spacingLarge
            anchors.rightMargin: App.Theme.spacingLarge
            spacing: App.Theme.spacingSmall

            Rectangle {
                width: 10; height: 10; radius: 5
                color: App.Theme.fault
            }
            Text {
                text: "Fault Report"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeLarge
                font.bold: true
                Layout.fillWidth: true
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }
    }

    footer: Rectangle {
        color: "transparent"
        implicitHeight: 52

        Rectangle {
            anchors.top: parent.top
            width: parent.width; height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }

        Button {
            id: closeBtn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: App.Theme.spacingLarge
            text: "Close"
            implicitWidth: 100
            implicitHeight: 32

            background: Rectangle {
                color: closeBtn.hovered ? Qt.rgba(1, 1, 1, 0.12)
                                        : Qt.rgba(1, 1, 1, 0.06)
                radius: 6
                border.color: Qt.rgba(1, 1, 1, 0.14)
                border.width: 1
            }
            contentItem: Text {
                text: closeBtn.text
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeMedium
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: root.close()
        }
    }

    contentItem: ScrollView {
        id: scrollView
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: root.availableWidth - 24
            spacing: App.Theme.spacingLarge

            // ── Section: Fault Summary ──────────────────────────
            Text {
                text: "Fault Summary"
                color: App.Theme.fault
                font.pixelSize: App.Theme.fontSizeLarge
                font.bold: true
            }

            Repeater {
                model: root.faultsSnapshot

                Rectangle {
                    Layout.fillWidth: true
                    color: Qt.rgba(239/255, 68/255, 68/255, 0.08)
                    border.color: Qt.rgba(239/255, 68/255, 68/255, 0.35)
                    border.width: 1
                    radius: 6
                    implicitHeight: faultCol.implicitHeight + 2 * App.Theme.spacingMedium

                    ColumnLayout {
                        id: faultCol
                        anchors.fill: parent
                        anchors.margins: App.Theme.spacingMedium
                        spacing: App.Theme.spacingSmall

                        Text {
                            text: root.formatFaultDetails(modelData.details || "")
                            color: App.Theme.text
                            font.pixelSize: App.Theme.fontSizeMedium
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Text {
                            text: {
                                var t = modelData.stateAtFault
                                       ? (modelData.stateAtFault.elapsedTime || 0)
                                       : (modelData.timestamp || 0)
                                return "Raised at t = " + root.formatTimestamp(t)
                            }
                            color: App.Theme.textSecondary
                            font.pixelSize: App.Theme.fontSizeSmall
                        }
                        Text {
                            visible: text.length > 0
                            text: root.prettyDetails(modelData.details || "")
                            color: App.Theme.textSecondary
                            font.family: "Consolas, Courier New, monospace"
                            font.pixelSize: App.Theme.fontSizeSmall
                            wrapMode: Text.WrapAnywhere
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            Text {
                visible: root.faultsSnapshot.length === 0
                text: "No specific fault events recorded (e.g. manual emergency stop)."
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                font.italic: true
            }

            // ── Section: System State at Fault ──────────────────
            Text {
                text: "System State at Fault"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeLarge
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: App.Theme.spacingLarge
                rowSpacing: App.Theme.spacingSmall

                component Metric: ColumnLayout {
                    property string label: ""
                    property string value: ""
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: label
                        color: App.Theme.textSecondary
                        font.pixelSize: App.Theme.fontSizeSmall
                    }
                    Text {
                        text: value
                        color: App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        font.bold: true
                    }
                }

                Metric { label: "State";            value: String(root.stateSnapshot.injectorState || "") }
                Metric { label: "Phase index";      value: String(root.stateSnapshot.phaseIndex + 1) }
                Metric { label: "Elapsed";          value: root.formatTimestamp(root.stateSnapshot.elapsedTime) }
                Metric { label: "Target flow";      value: (root.stateSnapshot.targetFlowRate || 0).toFixed(2) + " mL/s" }
                Metric { label: "Actual flow";      value: (root.stateSnapshot.actualFlowRate || 0).toFixed(2) + " mL/s" }
                Metric { label: "Flow deviation";   value: ((root.stateSnapshot.actualFlowRate || 0) - (root.stateSnapshot.targetFlowRate || 0)).toFixed(2) + " mL/s" }
                Metric { label: "Pressure";         value: (root.stateSnapshot.pressure || 0).toFixed(1) + " psi" }
                Metric { label: "Motor RPM";        value: (root.stateSnapshot.motorRpm || 0).toFixed(0) }
                Metric { label: "Contrast remain";  value: (root.stateSnapshot.contrastRemaining || 0).toFixed(1) + " mL" }
                Metric { label: "Saline remain";    value: (root.stateSnapshot.salineRemaining || 0).toFixed(1) + " mL" }
                Metric { label: "Contrast valve";   value: root.stateSnapshot.contrastValve ? "OPEN" : "closed" }
                Metric { label: "Saline valve";     value: root.stateSnapshot.salineValve ? "OPEN" : "closed" }
            }

            // ── Section: Control-Loop Health ────────────────────
            Text {
                text: "Control-Loop Health"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeLarge
                font.bold: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: App.Theme.spacingLarge
                rowSpacing: App.Theme.spacingSmall

                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text { text: "Mean tick"; color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall }
                    Text {
                        text: (root.stateSnapshot.meanTickMs || 0).toFixed(3) + " ms"
                        color: App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        font.bold: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text { text: "Max tick"; color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall }
                    Text {
                        text: (root.stateSnapshot.maxTickMs || 0).toFixed(3) + " ms"
                        color: (root.stateSnapshot.maxTickMs || 0) > 5.0 ? App.Theme.fault : App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        font.bold: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text { text: "Overruns"; color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall }
                    Text {
                        text: String(root.stateSnapshot.overrunCount || 0)
                        color: (root.stateSnapshot.overrunCount || 0) > 0 ? App.Theme.fault : App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        font.bold: true
                    }
                }
            }

            // ── Section: Pre-Fault Telemetry Trace ──────────────
            Text {
                text: "Pre-Fault Telemetry (last " + root.telemetrySnapshot.length + " frames)"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeLarge
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 220
                color: Qt.rgba(0, 0, 0, 0.25)
                border.color: Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                radius: 4

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: App.Theme.spacingSmall
                    spacing: 2

                    // Header row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { text: "time";     Layout.preferredWidth: 80;  color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                        Text { text: "target";   Layout.preferredWidth: 80;  color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                        Text { text: "actual";   Layout.preferredWidth: 80;  color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                        Text { text: "pressure"; Layout.preferredWidth: 90;  color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                        Text { text: "rpm";      Layout.preferredWidth: 70;  color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                        Text { text: "phase";    Layout.preferredWidth: 50;  color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                        Text { text: "valves";   Layout.fillWidth: true;     color: App.Theme.textSecondary; font.pixelSize: App.Theme.fontSizeSmall; font.bold: true; font.family: "Consolas, Courier New, monospace" }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.telemetrySnapshot
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { }

                        delegate: RowLayout {
                            width: ListView.view.width
                            spacing: 0
                            Text { text: root.formatTimestamp(modelData.timestamp || 0); Layout.preferredWidth: 80; color: App.Theme.text; font.pixelSize: App.Theme.fontSizeSmall; font.family: "Consolas, Courier New, monospace" }
                            Text { text: (modelData.targetFlowRate || 0).toFixed(2);     Layout.preferredWidth: 80; color: App.Theme.text; font.pixelSize: App.Theme.fontSizeSmall; font.family: "Consolas, Courier New, monospace" }
                            Text { text: (modelData.actualFlowRate || 0).toFixed(2);     Layout.preferredWidth: 80; color: App.Theme.text; font.pixelSize: App.Theme.fontSizeSmall; font.family: "Consolas, Courier New, monospace" }
                            Text { text: (modelData.pressure || 0).toFixed(1);           Layout.preferredWidth: 90; color: App.Theme.text; font.pixelSize: App.Theme.fontSizeSmall; font.family: "Consolas, Courier New, monospace" }
                            Text { text: (modelData.motorRpm || 0).toFixed(0);           Layout.preferredWidth: 70; color: App.Theme.text; font.pixelSize: App.Theme.fontSizeSmall; font.family: "Consolas, Courier New, monospace" }
                            Text { text: String((modelData.phaseIndex || 0) + 1);        Layout.preferredWidth: 50; color: App.Theme.text; font.pixelSize: App.Theme.fontSizeSmall; font.family: "Consolas, Courier New, monospace" }
                            Text {
                                text: (modelData.contrastValve ? "C" : "-") + (modelData.salineValve ? "S" : "-")
                                Layout.fillWidth: true
                                color: App.Theme.text
                                font.pixelSize: App.Theme.fontSizeSmall
                                font.family: "Consolas, Courier New, monospace"
                            }
                        }
                    }

                    Text {
                        visible: root.telemetrySnapshot.length === 0
                        text: "No telemetry frames captured."
                        color: App.Theme.textSecondary
                        font.pixelSize: App.Theme.fontSizeSmall
                        font.italic: true
                        Layout.alignment: Qt.AlignCenter
                    }
                }
            }

            // ── Section: Recent Events ──────────────────────────
            Text {
                text: "Recent Events (last " + root.eventsSnapshot.length + ")"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeLarge
                font.bold: true
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 220
                color: Qt.rgba(0, 0, 0, 0.25)
                border.color: Qt.rgba(1, 1, 1, 0.08)
                border.width: 1
                radius: 4

                ListView {
                    anchors.fill: parent
                    anchors.margins: App.Theme.spacingSmall
                    clip: true
                    model: root.eventsSnapshot
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }

                    delegate: RowLayout {
                        width: ListView.view.width
                        spacing: App.Theme.spacingSmall
                        readonly property bool isFault: (modelData.typeStr || "") === "fault_detected"

                        Text {
                            text: root.formatTimestamp(modelData.timestamp || 0)
                            Layout.preferredWidth: 80
                            color: parent.isFault ? App.Theme.fault : App.Theme.textSecondary
                            font.pixelSize: App.Theme.fontSizeSmall
                            font.family: "Consolas, Courier New, monospace"
                        }
                        Text {
                            text: modelData.typeStr || "event"
                            Layout.preferredWidth: 130
                            color: parent.isFault ? App.Theme.fault : App.Theme.text
                            font.pixelSize: App.Theme.fontSizeSmall
                            font.bold: parent.isFault
                            font.family: "Consolas, Courier New, monospace"
                        }
                        Text {
                            text: root.prettyDetails(modelData.details || "")
                            Layout.fillWidth: true
                            color: parent.isFault ? App.Theme.fault : App.Theme.text
                            font.pixelSize: App.Theme.fontSizeSmall
                            font.family: "Consolas, Courier New, monospace"
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}

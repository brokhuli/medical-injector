import QtQuick
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge
    property double pressureLimit: {
        // Get pressure limit from current phase of loaded protocol
        var phases = bridge.loadedProtocol
        var idx = bridge.phaseIndex
        if (phases && idx >= 0 && idx < phases.length) {
            return phases[idx].pressureLimit || 250.0
        }
        return 250.0
    }

    // Freeze elapsed time 2s after injection stops
    property string _state: bridge.injectorState
    property bool _timerFrozen: false
    property double _frozenElapsed: 0.0

    on_StateChanged: {
        if (_state === "Injecting") {
            _timerFrozen = false
            elapsedFreezeTimer.stop()
        } else if (!_timerFrozen) {
            elapsedFreezeTimer.restart()
        }
    }

    Timer {
        id: elapsedFreezeTimer
        interval: 2000
        repeat: false
        onTriggered: {
            root._frozenElapsed = bridge.elapsedTime
            root._timerFrozen = true
        }
    }

    color: App.Theme.surface
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: App.Theme.spacingLarge
        spacing: App.Theme.spacingMedium

        // Gauges row
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingLarge

            FlowRateGauge {
                targetFlow: bridge.targetFlowRate
                actualFlow: bridge.actualFlowRate
                Layout.fillWidth: true
            }

            PressureGauge {
                pressure: bridge.pressure
                pressureLimit: root.pressureLimit
                Layout.fillWidth: true
            }

            ElapsedTimer {
                elapsed: root._timerFrozen ? root._frozenElapsed : bridge.elapsedTime
                Layout.preferredWidth: 160
            }
        }

        // Valve state indicators
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingLarge

            ValveIndicator {
                label: "Contrast"
                open: bridge.contrastValve
                openColor: App.Theme.injecting
            }

            ValveIndicator {
                label: "Saline"
                open: bridge.salineValve
                openColor: "#06b6d4"
            }

            Item { Layout.fillWidth: true }
        }

        // Volume progress
        VolumeProgress {
            Layout.fillWidth: true
        }

        // Timeline chart (fills remaining space)
        TimelineChart {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            loadedProtocol: injectorBridge.loadedProtocol
        }
    }
}

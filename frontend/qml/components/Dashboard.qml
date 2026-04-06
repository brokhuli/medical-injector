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
            return phases[idx].pressureLimit || 325.0
        }
        return 325.0
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
                elapsed: bridge.elapsedTime
                Layout.preferredWidth: 160
            }
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
        }
    }
}

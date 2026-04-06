import QtQuick
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge

    readonly property double totalDelivered: bridge.totalVolumeDelivered
    readonly property double totalProgrammed: bridge.totalProgrammedVolume
    readonly property int currentPhase: bridge.phaseIndex
    readonly property var phases: bridge.loadedProtocol

    implicitHeight: col.implicitHeight
    color: "transparent"

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: App.Theme.spacingSmall

        Text {
            text: "Volume"
            color: App.Theme.textSecondary
            font.pixelSize: App.Theme.fontSizeSmall
            font.bold: true
        }

        // Per-phase progress bars
        Repeater {
            model: phases

            RowLayout {
                Layout.fillWidth: true
                spacing: App.Theme.spacingSmall

                readonly property bool isActive: index === currentPhase
                readonly property var phase: modelData
                readonly property double phaseVol: phase ? (phase.volume || 0) : 0

                Text {
                    text: "P" + (index + 1) + " " + (phase ? (phase.fluidType || "") : "")
                    color: isActive ? App.Theme.text : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                    font.bold: isActive
                    Layout.preferredWidth: 80
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 12
                    color: Qt.rgba(1, 1, 1, 0.05)
                    radius: 3

                    Rectangle {
                        width: {
                            if (phaseVol <= 0) return 0
                            // Approximate: before current phase = full, current = partial, after = 0
                            if (index < currentPhase) return parent.width
                            if (index > currentPhase) return 0
                            // Current phase: estimate from total delivered minus prior phases
                            var prior = 0
                            for (var i = 0; i < index; i++) {
                                var p = phases[i]
                                if (p) prior += (p.volume || 0)
                            }
                            var phaseDelivered = Math.max(0, totalDelivered - prior)
                            return parent.width * Math.min(phaseDelivered / phaseVol, 1.0)
                        }
                        height: parent.height
                        radius: 3
                        color: isActive ? App.Theme.injecting : App.Theme.completed
                        opacity: isActive ? 1.0 : 0.6

                        Behavior on width { NumberAnimation { duration: 100 } }
                    }
                }

                Text {
                    text: phaseVol.toFixed(0) + " mL"
                    color: App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                    Layout.preferredWidth: 50
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // Total
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall
            visible: totalProgrammed > 0

            Text {
                text: "Total"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                font.bold: true
                Layout.preferredWidth: 80
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 14
                color: Qt.rgba(1, 1, 1, 0.05)
                radius: 3

                Rectangle {
                    width: totalProgrammed > 0
                           ? parent.width * Math.min(totalDelivered / totalProgrammed, 1.0)
                           : 0
                    height: parent.height
                    radius: 3
                    color: App.Theme.injecting

                    Behavior on width { NumberAnimation { duration: 100 } }
                }
            }

            Text {
                text: totalDelivered.toFixed(1) + " / " + totalProgrammed.toFixed(1) + " mL"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeSmall
                Layout.preferredWidth: 110
                horizontalAlignment: Text.AlignRight
            }
        }

        // Empty state
        Text {
            text: "No protocol loaded"
            color: App.Theme.textSecondary
            font.pixelSize: App.Theme.fontSizeSmall
            visible: !phases || phases.length === 0
        }
    }
}

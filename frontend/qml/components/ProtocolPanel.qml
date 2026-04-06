import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge
    readonly property bool isIdle: bridge.injectorState === "Idle"
    readonly property int maxPhases: 20

    color: App.Theme.surface
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: App.Theme.spacingLarge
        spacing: App.Theme.spacingMedium

        // Title
        Text {
            text: "Protocol Configuration"
            color: App.Theme.text
            font.pixelSize: App.Theme.fontSizeLarge
            font.bold: true
            Layout.fillWidth: true
        }

        // Phase list
        ListView {
            id: phaseList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: App.Theme.spacingSmall
            model: bridge.protocol

            delegate: PhaseRow {
                width: phaseList.width
                phaseIndex: index
                fluidType: modelData.fluidType || "contrast"
                flowRate: modelData.flowRate || 4.0
                volume: modelData.volume || 80.0
                pressureLimit: modelData.pressureLimit || 325.0
                editable: root.isIdle

                onRemoveRequested: function(idx) {
                    bridge.removePhase(idx)
                }

                onValuesChanged: function(idx, ft, fr, vol, pl) {
                    // Remove and re-add with updated values
                    bridge.removePhase(idx)
                    // Insert at the same position by adding then reordering
                    bridge.addPhase(ft, fr, vol, pl)
                    var lastIdx = bridge.protocol.length - 1
                    if (lastIdx > idx) {
                        bridge.reorderPhases(lastIdx, idx)
                    }
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: phaseList.count === 0
                text: "No phases configured.\nClick \u201C+ Add Phase\u201D to begin."
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeMedium
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // Add Phase button
        Button {
            text: "+ Add Phase"
            enabled: root.isIdle && bridge.protocol.length < root.maxPhases
            Layout.fillWidth: true
            Layout.preferredHeight: 36

            background: Rectangle {
                color: parent.enabled
                       ? (parent.hovered ? Qt.lighter(App.Theme.injecting, 1.2) : App.Theme.injecting)
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

            onClicked: bridge.addPhase("contrast", 4.0, 80.0, 325.0)
        }

        // Separator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // Syringe levels
        SyringeIndicator {
            label: "Contrast"
            remaining: bridge.contrastRemaining
            capacity: 100.0
            barColor: App.Theme.injecting
            Layout.fillWidth: true
        }

        SyringeIndicator {
            label: "Saline"
            remaining: bridge.salineRemaining
            capacity: 50.0
            barColor: "#06b6d4"
            Layout.fillWidth: true
        }

        // Separator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // Action buttons row
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            Button {
                text: "Clear"
                enabled: root.isIdle && bridge.protocol.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 34

                background: Rectangle {
                    color: parent.enabled
                           ? (parent.hovered ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06))
                           : Qt.rgba(1, 1, 1, 0.03)
                    radius: 6
                    border.color: Qt.rgba(1, 1, 1, 0.15)
                    border.width: 1
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? App.Theme.text : App.Theme.textSecondary
                    font.pixelSize: App.Theme.fontSizeSmall
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: bridge.clearProtocol()
            }

            Button {
                text: "Load Protocol"
                enabled: root.isIdle && bridge.protocol.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 34

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

                onClicked: bridge.loadProtocol(bridge.protocol)
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as App
import "components" as Components

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 600
    title: "Medical Injector Simulator"
    color: App.Theme.background

    // Disconnect overlay when not connected
    Rectangle {
        id: disconnectOverlay
        anchors.fill: parent
        z: 100
        visible: injectorBridge.connectionStatus !== "connected"
        color: Qt.rgba(0, 0, 0, 0.6)

        Column {
            anchors.centerIn: parent
            spacing: App.Theme.spacingLarge

            Components.ConnectionIndicator {
                status: injectorBridge.connectionStatus
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: "Waiting for backend connection..."
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeLarge
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    // Header bar
    header: ToolBar {
        background: Rectangle { color: App.Theme.surface }
        height: 48

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: App.Theme.spacingLarge
            anchors.rightMargin: App.Theme.spacingLarge

            Components.ConnectionIndicator {
                status: injectorBridge.connectionStatus
            }

            Text {
                text: "Medical Injector Simulator"
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeTitle
                font.bold: true
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            // Spacer to balance the layout
            Item { Layout.preferredWidth: 150 }
        }
    }

    // Main content area
    RowLayout {
        anchors.fill: parent
        anchors.margins: App.Theme.spacingLarge
        spacing: App.Theme.spacingLarge

        // Left panel: Protocol configuration (placeholder for S12)
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: App.Theme.surface
            radius: 8

            Text {
                anchors.centerIn: parent
                text: "Protocol Panel\n(S12)"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeMedium
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // Right panel: Control + Dashboard
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: App.Theme.spacingLarge

            // Control panel with state indicator
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 80
                color: App.Theme.surface
                radius: 8

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: App.Theme.spacingLarge
                    spacing: App.Theme.spacingLarge

                    Components.StateIndicator {
                        state: injectorBridge.injectorState
                    }

                    // Placeholder for control buttons (S12)
                    Text {
                        text: "Control Buttons (S12)"
                        color: App.Theme.textSecondary
                        font.pixelSize: App.Theme.fontSizeMedium
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // Dashboard area (placeholder for S13)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: App.Theme.surface
                radius: 8

                Column {
                    anchors.centerIn: parent
                    spacing: App.Theme.spacingMedium

                    Text {
                        text: "Dashboard (S13)"
                        color: App.Theme.textSecondary
                        font.pixelSize: App.Theme.fontSizeLarge
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    // Show basic telemetry to prove the gRPC connection works
                    Text {
                        text: "Flow: " + injectorBridge.actualFlowRate.toFixed(2) + " / "
                              + injectorBridge.targetFlowRate.toFixed(2) + " mL/s"
                        color: App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: injectorBridge.connectionStatus === "connected"
                    }

                    Text {
                        text: "Pressure: " + injectorBridge.pressure.toFixed(1) + " psi"
                        color: App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: injectorBridge.connectionStatus === "connected"
                    }

                    Text {
                        text: "Volume: " + injectorBridge.totalVolumeDelivered.toFixed(1) + " / "
                              + injectorBridge.totalProgrammedVolume.toFixed(1) + " mL"
                        color: App.Theme.text
                        font.pixelSize: App.Theme.fontSizeMedium
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: injectorBridge.connectionStatus === "connected"
                    }
                }
            }
        }
    }

    // Bottom: Event log placeholder (S13)
    footer: ToolBar {
        background: Rectangle { color: App.Theme.surface }
        height: 60

        Text {
            anchors.centerIn: parent
            text: "Event Log (S13)"
            color: App.Theme.textSecondary
            font.pixelSize: App.Theme.fontSizeMedium
        }
    }
}

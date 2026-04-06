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

        // Left panel: Protocol configuration
        Components.ProtocolPanel {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
        }

        // Right panel: Control + Dashboard
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: App.Theme.spacingLarge

            // Control panel with state indicator and buttons
            Components.ControlPanel {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                Layout.minimumHeight: 180
            }

            // Fault detail (only visible when faults exist)
            Components.FaultDetail {
                Layout.fillWidth: true
            }

            // Dashboard: gauges + timeline chart
            Components.Dashboard {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    // Bottom: Event log
    footer: ToolBar {
        background: Rectangle { color: "transparent" }
        height: 150

        Components.EventLog {
            anchors.fill: parent
        }
    }
}

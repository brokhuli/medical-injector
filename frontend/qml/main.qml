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

            // About menu
            ToolButton {
                id: aboutBtn
                text: "About \u25BE"
                Layout.preferredWidth: 150
                Layout.preferredHeight: 36

                background: Rectangle {
                    color: aboutBtn.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                    radius: 6
                    border.color: Qt.rgba(1, 1, 1, 0.12)
                    border.width: 1
                }
                contentItem: Text {
                    text: aboutBtn.text
                    color: App.Theme.text
                    font.pixelSize: App.Theme.fontSizeMedium
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: aboutMenu.open()

                Menu {
                    id: aboutMenu
                    y: aboutBtn.height

                    background: Rectangle {
                        color: App.Theme.surface
                        radius: 6
                        border.color: Qt.rgba(1, 1, 1, 0.12)
                        border.width: 1
                        implicitWidth: 220
                    }

                    component ThemedMenuItem: MenuItem {
                        id: themedItem
                        implicitHeight: 36
                        background: Rectangle {
                            color: themedItem.highlighted
                                   ? Qt.rgba(1, 1, 1, 0.08)
                                   : "transparent"
                            radius: 4
                        }
                        contentItem: Text {
                            text: themedItem.text
                            color: App.Theme.text
                            font.pixelSize: App.Theme.fontSizeMedium
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: App.Theme.spacingMedium
                        }
                    }

                    ThemedMenuItem {
                        text: "About"
                        onTriggered: aboutDialog.show("qrc:/readme/about.md", "About")
                    }
                    ThemedMenuItem {
                        text: "Architecture Summary"
                        onTriggered: aboutDialog.show("qrc:/readme/architecture-summary.md", "Architecture Summary")
                    }
                    ThemedMenuItem {
                        text: "Design Decisions"
                        onTriggered: aboutDialog.show("qrc:/readme/architecture-decision-record.md", "Design Decisions")
                    }
                }
            }
        }
    }

    Components.AboutDialog {
        id: aboutDialog
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

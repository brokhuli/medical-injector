import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge

    color: App.Theme.surface
    radius: 8

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
                            var type = modelData.type || ""
                            if (type === "fault" || type === "emergency_stop")
                                return App.Theme.fault
                            if (type === "state_change")
                                return App.Theme.injecting
                            return App.Theme.textSecondary
                        }
                    }

                    // Details
                    Text {
                        text: modelData.details || ""
                        color: App.Theme.text
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

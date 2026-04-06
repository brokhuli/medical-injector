import QtQuick
import QtQuick.Layouts
import ".." as App

Item {
    id: root

    property string label: "Contrast"
    property double remaining: 100.0
    property double capacity: 100.0
    property color barColor: App.Theme.injecting

    implicitWidth: 260
    implicitHeight: col.implicitHeight

    readonly property double fraction: capacity > 0 ? Math.max(0, Math.min(1, remaining / capacity)) : 0

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: App.Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: root.label
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeSmall
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.remaining.toFixed(1) + " / " + root.capacity.toFixed(0) + " mL"
                color: root.fraction < 0.15 ? App.Theme.fault : App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
            }
        }

        // Progress bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 10
            radius: 5
            color: Qt.rgba(1, 1, 1, 0.08)

            Rectangle {
                width: parent.width * root.fraction
                height: parent.height
                radius: parent.radius
                color: root.fraction < 0.15 ? App.Theme.fault : root.barColor

                Behavior on width { NumberAnimation { duration: 150 } }
            }
        }
    }
}

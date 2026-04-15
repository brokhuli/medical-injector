import QtQuick
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property double meanTickMs:  0.0
    property double maxTickMs:   0.0
    property int    overrunCount: 0

    readonly property double targetMs: 2.0

    implicitWidth: 120
    color: overrunCount > 0 ? Qt.rgba(1, 0.5, 0, 0.12) : Qt.rgba(1, 1, 1, 0.04)
    radius: 4
    border.color: overrunCount > 0 ? App.Theme.paused : Qt.rgba(1, 1, 1, 0.08)
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: App.Theme.spacingMedium
        spacing: 4

        Text {
            text: "Loop Health"
            color: App.Theme.textSecondary
            font.pixelSize: 11
            font.bold: true
        }

        Text {
            text: "avg " + root.meanTickMs.toFixed(2) + " ms"
            color: root.meanTickMs > root.targetMs * 1.5 ? App.Theme.paused : App.Theme.textSecondary
            font.pixelSize: 11
            font.family: "Consolas, monospace"
        }

        Text {
            text: "max " + root.maxTickMs.toFixed(2) + " ms"
            color: root.maxTickMs > root.targetMs * 2.0 ? App.Theme.fault : App.Theme.textSecondary
            font.pixelSize: 11
            font.family: "Consolas, monospace"
        }

        Text {
            text: root.overrunCount > 0 ? root.overrunCount + " overruns" : "no overruns"
            color: root.overrunCount > 0 ? App.Theme.paused : App.Theme.textSecondary
            font.pixelSize: 11
            font.family: "Consolas, monospace"
        }

        Item { Layout.fillHeight: true }
    }
}

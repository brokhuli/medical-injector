import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property var bridge: injectorBridge
    readonly property bool isIdle: bridge.injectorState === "Idle"
    readonly property bool isPreInjection: bridge.injectorState === "Idle" || bridge.injectorState === "Armed"
    readonly property int maxPhases: 20

    // Track volume delivered at injection start so we use the delta
    readonly property var _loaded: bridge.loadedProtocol
    readonly property double _volDelivered: bridge.totalVolumeDelivered
    property double _volDeliveredAtStart: 0

    onIsPreInjectionChanged: {
        if (!isPreInjection) {
            // Capture baseline when injection begins
            _volDeliveredAtStart = _volDelivered
        }
    }

    readonly property double _volThisRun: Math.max(0, _volDelivered - _volDeliveredAtStart)

    readonly property double contrastDisplay: {
        if (isPreInjection) return totalContrast
        var contrastDel = 0
        var remaining = _volThisRun
        for (var i = 0; i < _loaded.length && remaining > 0; i++) {
            var phase = _loaded[i]
            var phaseVol = phase.volume || 0
            var used = Math.min(phaseVol, remaining)
            if ((phase.fluidType || "contrast").toLowerCase() === "contrast")
                contrastDel += used
            remaining -= used
        }
        return Math.max(0, totalContrast - contrastDel)
    }
    readonly property double salineDisplay: {
        if (isPreInjection) return totalSaline
        var salineDel = 0
        var remaining = _volThisRun
        for (var i = 0; i < _loaded.length && remaining > 0; i++) {
            var phase = _loaded[i]
            var phaseVol = phase.volume || 0
            var used = Math.min(phaseVol, remaining)
            if ((phase.fluidType || "").toLowerCase() === "saline")
                salineDel += used
            remaining -= used
        }
        return Math.max(0, totalSaline - salineDel)
    }

    // Recompute totals whenever protocol changes
    readonly property var _proto: bridge.protocol
    readonly property double totalContrast: {
        var total = 0
        for (var i = 0; i < _proto.length; i++) {
            if ((_proto[i].fluidType || "contrast").toLowerCase() === "contrast")
                total += (_proto[i].volume || 0)
        }
        return total
    }
    readonly property double totalSaline: {
        var total = 0
        for (var i = 0; i < _proto.length; i++) {
            if ((_proto[i].fluidType || "").toLowerCase() === "saline")
                total += (_proto[i].volume || 0)
        }
        return total
    }

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

            // Preserve scroll position across model resets
            property real savedContentY: 0
            onModelChanged: {
                contentY = savedContentY
            }

            delegate: PhaseRow {
                width: phaseList.width
                phaseIndex: index
                fluidType: modelData.fluidType || "contrast"
                flowRate: modelData.flowRate || 2.0
                volume: modelData.volume || 40.0
                pressureLimit: modelData.pressureLimit || 250.0
                editable: root.isIdle

                onRemoveRequested: function(idx) {
                    phaseList.savedContentY = phaseList.contentY
                    bridge.removePhase(idx)
                }

                onValuesChanged: function(idx, ft, fr, vol, pl) {
                    phaseList.savedContentY = phaseList.contentY
                    bridge.updatePhase(idx, ft, fr, vol, pl)
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

            onClicked: bridge.addPhase("contrast", 2.0, 40.0, 250.0)
        }

        // Separator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Qt.rgba(1, 1, 1, 0.1)
        }

        // Syringe levels — show protocol totals when idle, telemetry remaining otherwise
        SyringeIndicator {
            label: "Contrast"
            remaining: root.contrastDisplay
            capacity: Math.max(root.totalContrast, 1.0)
            barColor: App.Theme.injecting
            Layout.fillWidth: true
        }

        SyringeIndicator {
            label: "Saline"
            remaining: root.salineDisplay
            capacity: Math.max(root.totalSaline, 1.0)
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

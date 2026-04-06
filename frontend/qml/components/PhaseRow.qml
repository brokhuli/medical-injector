import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".." as App

Rectangle {
    id: root

    property int phaseIndex: 0
    property string fluidType: "contrast"
    property double flowRate: 4.0
    property double volume: 80.0
    property double pressureLimit: 325.0
    property bool editable: true

    signal removeRequested(int index)
    signal valuesChanged(int index, string fluidType, double flowRate, double volume, double pressureLimit)

    implicitHeight: col.implicitHeight + 2 * App.Theme.spacingMedium
    color: App.Theme.surface
    radius: 6
    border.color: Qt.rgba(1, 1, 1, 0.1)
    border.width: 1

    // Validation helpers
    readonly property bool flowRateValid: flowRateField.acceptableInput
    readonly property bool volumeValid: volumeField.acceptableInput
    readonly property bool pressureLimitValid: pressureLimitField.acceptableInput
    readonly property bool allValid: flowRateValid && volumeValid && pressureLimitValid

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: App.Theme.spacingMedium
        spacing: App.Theme.spacingSmall

        // Header: phase number + fluid type + remove button
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingMedium

            Text {
                text: "Phase " + (root.phaseIndex + 1)
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeMedium
                font.bold: true
            }

            ComboBox {
                id: fluidTypeCombo
                model: ["Contrast", "Saline"]
                currentIndex: root.fluidType.toLowerCase() === "saline" ? 1 : 0
                enabled: root.editable
                Layout.preferredWidth: 110

                onCurrentIndexChanged: {
                    root.fluidType = currentIndex === 1 ? "saline" : "contrast"
                    root.emitValues()
                }

                background: Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.08)
                    radius: 4
                    border.color: fluidTypeCombo.activeFocus ? App.Theme.injecting : Qt.rgba(1, 1, 1, 0.15)
                    border.width: 1
                }

                contentItem: Text {
                    text: fluidTypeCombo.displayText
                    color: App.Theme.text
                    font.pixelSize: App.Theme.fontSizeSmall
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: App.Theme.spacingSmall
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "\u00D7"
                enabled: root.editable
                visible: root.editable
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28

                background: Rectangle {
                    color: parent.hovered ? App.Theme.fault : "transparent"
                    radius: 4
                }

                contentItem: Text {
                    text: parent.text
                    color: App.Theme.text
                    font.pixelSize: App.Theme.fontSizeLarge
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: root.removeRequested(root.phaseIndex)
            }
        }

        // Flow rate
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            Text {
                text: "Flow Rate"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                Layout.preferredWidth: 90
            }

            TextField {
                id: flowRateField
                text: root.flowRate.toFixed(1)
                enabled: root.editable
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                validator: DoubleValidator { bottom: 0.1; top: 10.0; decimals: 1 }
                horizontalAlignment: Text.AlignRight

                color: acceptableInput ? App.Theme.text : App.Theme.fault
                font.pixelSize: App.Theme.fontSizeSmall

                background: Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.06)
                    radius: 4
                    border.color: flowRateField.acceptableInput
                                  ? (flowRateField.activeFocus ? App.Theme.injecting : Qt.rgba(1, 1, 1, 0.1))
                                  : App.Theme.fault
                    border.width: 1
                }

                onEditingFinished: {
                    var val = parseFloat(text)
                    if (!isNaN(val)) {
                        root.flowRate = val
                        root.emitValues()
                    }
                }
            }

            Text {
                text: "mL/s"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
            }
        }

        // Volume
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            Text {
                text: "Volume"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                Layout.preferredWidth: 90
            }

            TextField {
                id: volumeField
                text: root.volume.toFixed(1)
                enabled: root.editable
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                validator: DoubleValidator { bottom: 1.0; top: 200.0; decimals: 1 }
                horizontalAlignment: Text.AlignRight

                color: acceptableInput ? App.Theme.text : App.Theme.fault
                font.pixelSize: App.Theme.fontSizeSmall

                background: Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.06)
                    radius: 4
                    border.color: volumeField.acceptableInput
                                  ? (volumeField.activeFocus ? App.Theme.injecting : Qt.rgba(1, 1, 1, 0.1))
                                  : App.Theme.fault
                    border.width: 1
                }

                onEditingFinished: {
                    var val = parseFloat(text)
                    if (!isNaN(val)) {
                        root.volume = val
                        root.emitValues()
                    }
                }
            }

            Text {
                text: "mL"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
            }
        }

        // Pressure limit
        RowLayout {
            Layout.fillWidth: true
            spacing: App.Theme.spacingSmall

            Text {
                text: "Pressure Limit"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
                Layout.preferredWidth: 90
            }

            TextField {
                id: pressureLimitField
                text: root.pressureLimit.toFixed(0)
                enabled: root.editable
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                validator: DoubleValidator { bottom: 50.0; top: 400.0; decimals: 0 }
                horizontalAlignment: Text.AlignRight

                color: acceptableInput ? App.Theme.text : App.Theme.fault
                font.pixelSize: App.Theme.fontSizeSmall

                background: Rectangle {
                    color: Qt.rgba(1, 1, 1, 0.06)
                    radius: 4
                    border.color: pressureLimitField.acceptableInput
                                  ? (pressureLimitField.activeFocus ? App.Theme.injecting : Qt.rgba(1, 1, 1, 0.1))
                                  : App.Theme.fault
                    border.width: 1
                }

                onEditingFinished: {
                    var val = parseFloat(text)
                    if (!isNaN(val)) {
                        root.pressureLimit = val
                        root.emitValues()
                    }
                }
            }

            Text {
                text: "psi"
                color: App.Theme.textSecondary
                font.pixelSize: App.Theme.fontSizeSmall
            }
        }

        // Validation message
        Text {
            visible: !root.allValid
            text: {
                if (!root.flowRateValid) return "Flow rate: 0.1 \u2013 10.0 mL/s"
                if (!root.volumeValid) return "Volume: 1.0 \u2013 200.0 mL"
                if (!root.pressureLimitValid) return "Pressure: 50 \u2013 400 psi"
                return ""
            }
            color: App.Theme.fault
            font.pixelSize: App.Theme.fontSizeSmall
            Layout.fillWidth: true
        }
    }

    function emitValues() {
        root.valuesChanged(root.phaseIndex, root.fluidType, root.flowRate, root.volume, root.pressureLimit)
    }
}

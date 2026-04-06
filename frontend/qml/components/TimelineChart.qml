import QtQuick
import ".." as App

Rectangle {
    id: root

    property var history: injectorBridge.telemetryHistory
    property double windowSeconds: 30.0

    readonly property double maxFlowRate: 10.0
    readonly property double maxPressure: 400.0

    color: Qt.rgba(0, 0, 0, 0.2)
    radius: 6

    // 10 Hz repaint timer
    Timer {
        id: repaintTimer
        interval: 100
        running: true
        repeat: true
        onTriggered: canvas.requestPaint()
    }

    // Axis labels
    Text {
        x: 4
        y: 4
        text: "mL/s"
        color: App.Theme.injecting
        font.pixelSize: 10
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 4
        y: 4
        text: "psi"
        color: App.Theme.paused
        font.pixelSize: 10
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 4

        readonly property int leftMargin: 36
        readonly property int rightMargin: 36
        readonly property int topMargin: 18
        readonly property int bottomMargin: 20

        readonly property double plotW: width - leftMargin - rightMargin
        readonly property double plotH: height - topMargin - bottomMargin

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            if (!history || history.length === 0) {
                ctx.fillStyle = App.Theme.textSecondary
                ctx.font = "12px sans-serif"
                ctx.textAlign = "center"
                ctx.fillText("No telemetry data", width / 2, height / 2)
                return
            }

            var data = history
            var len = data.length

            // Determine time range
            var latestTime = data[len - 1].elapsed || 0
            var timeStart = Math.max(0, latestTime - windowSeconds)
            var timeEnd = latestTime

            // Draw grid
            drawGrid(ctx, timeStart, timeEnd)

            // Draw flow rate line (left Y-axis, blue)
            drawSeries(ctx, data, "actualFlow", timeStart, timeEnd,
                       maxFlowRate, App.Theme.injecting.toString())

            // Draw pressure line (right Y-axis, orange)
            drawSeriesRight(ctx, data, "pressure", timeStart, timeEnd,
                           maxPressure, App.Theme.paused.toString())

            // Draw phase boundary markers
            drawPhaseMarkers(ctx, data, timeStart, timeEnd)
        }

        function drawGrid(ctx, timeStart, timeEnd) {
            var x0 = leftMargin
            var y0 = topMargin
            var x1 = leftMargin + plotW
            var y1 = topMargin + plotH

            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.1)
            ctx.lineWidth = 1

            // Horizontal gridlines (5 lines)
            for (var i = 0; i <= 4; i++) {
                var y = y0 + plotH * i / 4
                ctx.beginPath()
                ctx.moveTo(x0, y)
                ctx.lineTo(x1, y)
                ctx.stroke()
            }

            // Left Y-axis labels (flow rate)
            ctx.fillStyle = App.Theme.injecting.toString()
            ctx.font = "10px sans-serif"
            ctx.textAlign = "right"
            for (var j = 0; j <= 4; j++) {
                var flowVal = maxFlowRate * (4 - j) / 4
                var yLabel = y0 + plotH * j / 4
                ctx.fillText(flowVal.toFixed(1), x0 - 4, yLabel + 3)
            }

            // Right Y-axis labels (pressure)
            ctx.fillStyle = App.Theme.paused.toString()
            ctx.textAlign = "left"
            for (var k = 0; k <= 4; k++) {
                var presVal = maxPressure * (4 - k) / 4
                var yPres = y0 + plotH * k / 4
                ctx.fillText(presVal.toFixed(0), x1 + 4, yPres + 3)
            }

            // X-axis time labels (5 labels)
            ctx.fillStyle = App.Theme.textSecondary.toString()
            ctx.textAlign = "center"
            var timeRange = timeEnd - timeStart
            for (var t = 0; t <= 4; t++) {
                var time = timeStart + timeRange * t / 4
                var xTime = x0 + plotW * t / 4
                ctx.fillText(time.toFixed(0) + "s", xTime, y1 + 14)
            }
        }

        function drawSeries(ctx, data, field, timeStart, timeEnd, maxVal, color) {
            var timeRange = timeEnd - timeStart
            if (timeRange <= 0) return

            ctx.strokeStyle = color
            ctx.lineWidth = 1.5
            ctx.beginPath()

            var started = false
            for (var i = 0; i < data.length; i++) {
                var d = data[i]
                var t = d.elapsed || 0
                if (t < timeStart) continue

                var x = leftMargin + plotW * (t - timeStart) / timeRange
                var val = d[field] || 0
                var y = topMargin + plotH * (1 - Math.min(val / maxVal, 1.0))

                if (!started) {
                    ctx.moveTo(x, y)
                    started = true
                } else {
                    ctx.lineTo(x, y)
                }
            }
            ctx.stroke()
        }

        function drawSeriesRight(ctx, data, field, timeStart, timeEnd, maxVal, color) {
            // Same as drawSeries but using right Y-axis scale
            drawSeries(ctx, data, field, timeStart, timeEnd, maxVal, color)
        }

        function drawPhaseMarkers(ctx, data, timeStart, timeEnd) {
            var timeRange = timeEnd - timeStart
            if (timeRange <= 0 || data.length < 2) return

            ctx.strokeStyle = Qt.rgba(1, 1, 1, 0.3)
            ctx.lineWidth = 1
            ctx.setLineDash([4, 4])

            var prevPhase = data[0].phaseIndex || 0
            for (var i = 1; i < data.length; i++) {
                var phase = data[i].phaseIndex || 0
                if (phase !== prevPhase) {
                    var t = data[i].elapsed || 0
                    if (t >= timeStart && t <= timeEnd) {
                        var x = leftMargin + plotW * (t - timeStart) / timeRange
                        ctx.beginPath()
                        ctx.moveTo(x, topMargin)
                        ctx.lineTo(x, topMargin + plotH)
                        ctx.stroke()

                        // Phase label
                        ctx.fillStyle = App.Theme.textSecondary.toString()
                        ctx.font = "9px sans-serif"
                        ctx.textAlign = "center"
                        ctx.fillText("P" + (phase + 1), x, topMargin - 4)
                    }
                    prevPhase = phase
                }
            }
            ctx.setLineDash([])
        }
    }
}

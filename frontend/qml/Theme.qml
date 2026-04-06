pragma Singleton
import QtQuick

QtObject {
    // State colors
    readonly property color idle: "#22c55e"
    readonly property color armed: "#eab308"
    readonly property color injecting: "#3b82f6"
    readonly property color paused: "#f97316"
    readonly property color fault: "#ef4444"
    readonly property color completed: "#6b7280"

    // Connection status colors
    readonly property color connected: "#22c55e"
    readonly property color disconnected: "#ef4444"
    readonly property color reconnecting: "#eab308"

    // UI colors
    readonly property color background: "#1e1e2e"
    readonly property color surface: "#2a2a3e"
    readonly property color text: "#e0e0e0"
    readonly property color textSecondary: "#a0a0b0"

    // Spacing
    readonly property int spacingSmall: 4
    readonly property int spacingMedium: 8
    readonly property int spacingLarge: 16

    // Fonts
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeMedium: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeTitle: 24
}

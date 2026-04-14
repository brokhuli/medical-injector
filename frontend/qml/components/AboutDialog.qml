import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import ".." as App

Dialog {
    id: root
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, Overlay.overlay.width - 80)
    height: Overlay.overlay.height - 80
    title: titleText

    property string sourcePath: ""
    property string titleText: ""
    property string _content: ""

    function show(path, title) {
        titleText = title
        sourcePath = path
        open()
    }

    onSourcePathChanged: _load()

    function _load() {
        _content = sourcePath ? injectorBridge.loadTextResource(sourcePath) : ""
        if (scrollView.contentItem)
            scrollView.contentItem.contentY = 0
    }

    background: Rectangle {
        color: App.Theme.surface
        radius: 6
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
    }

    header: Rectangle {
        color: "transparent"
        implicitHeight: 44
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: App.Theme.spacingLarge
            text: root.titleText
            color: App.Theme.text
            font.pixelSize: App.Theme.fontSizeLarge
            font.bold: true
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }
    }

    footer: Rectangle {
        color: "transparent"
        implicitHeight: 52

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.08)
        }

        Button {
            id: closeBtn
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: App.Theme.spacingLarge
            text: "Close"
            implicitWidth: 100
            implicitHeight: 32

            background: Rectangle {
                color: closeBtn.hovered ? Qt.rgba(1, 1, 1, 0.12)
                                        : Qt.rgba(1, 1, 1, 0.06)
                radius: 6
                border.color: Qt.rgba(1, 1, 1, 0.14)
                border.width: 1
            }
            contentItem: Text {
                text: closeBtn.text
                color: App.Theme.text
                font.pixelSize: App.Theme.fontSizeMedium
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: root.close()
        }
    }

    contentItem: ScrollView {
        id: scrollView
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Text {
            width: root.availableWidth - 24
            text: root._content
            textFormat: Text.MarkdownText
            wrapMode: Text.WordWrap
            color: App.Theme.text
            font.pixelSize: App.Theme.fontSizeMedium
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }
}

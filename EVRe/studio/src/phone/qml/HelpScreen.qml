// SPDX-License-Identifier: Apache-2.0
// The Help screen (part B.5), over the whole window: the desktop's Help topics (PhoneHelp, help_topics.cpp) as a list,
// a tap opens a topic's page. The back button at the header's start, and Android's back gesture, go from a page to the
// list and from the list out of the Help. In Arabic the list and the header are mirrored and the pages run right to
// left (their code blocks left to right). Opened from the drawer's Display section.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Popup {
    id: help
    property bool rtl: false
    // the topic shown; -1: the list
    property int topic: -1

    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 412
    height: parent ? parent.height : 915
    modal: true
    padding: 0
    // back closes by steps (back()), not at once; a tap outside there is none: it covers the window
    closePolicy: Popup.NoAutoClose
    background: Rectangle { color: PhoneTheme.bg }

    function back() {
        if (help.topic >= 0) help.topic = -1
        else help.close()
    }
    function show(index) {
        help.topic = index
        pageFlick.contentY = 0
    }
    onOpened: {
        help.topic = -1
        content.forceActiveFocus()
    }

    contentItem: Item {
        id: content
        focus: true
        // a popup is not a child of the window's items: it mirrors by its own
        LayoutMirroring.enabled: help.rtl
        LayoutMirroring.childrenInherit: true
        // Esc as Android's back (Main.qml's back() has that one)
        Keys.onPressed: event => {
            if (help.opened && event.key === Qt.Key_Escape) {
                help.back()
                event.accepted = true
            }
        }

        Rectangle {
            id: bar
            objectName: "helpHeader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 56
            color: PhoneTheme.surface
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: PhoneTheme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 16
                spacing: 4
                // a chevron drawn, pointing to the start edge
                ToolButton {
                    objectName: "helpBack"
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Back")
                    onClicked: help.back()
                    contentItem: Item {
                        PhoneIcon {
                            anchors.centerIn: parent
                            kind: help.rtl ? "next" : "back"
                            color: PhoneTheme.text
                        }
                    }
                }
                Label {
                    objectName: "helpTitle"
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    horizontalAlignment: Text.AlignLeft
                    text: help.topic < 0 ? qsTr("Help") : PhoneHelp.titles[help.topic]
                    color: PhoneTheme.text
                    font.pixelSize: 20
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
        }

        // the topics, a row each
        ListView {
            id: list
            objectName: "helpList"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: bar.bottom
            anchors.bottom: parent.bottom
            visible: help.topic < 0
            clip: true
            model: PhoneHelp.titles
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            delegate: Rectangle {
                id: row
                required property int index
                required property string modelData
                objectName: "helpTopic_" + index
                width: ListView.view.width
                height: 56
                color: rowTap.pressed ? PhoneTheme.surface2 : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: modelData
                Label {
                    objectName: "helpTopicText"
                    anchors.left: parent.left
                    anchors.right: chevron.left
                    anchors.leftMargin: 20
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: row.modelData
                    color: PhoneTheme.text
                    font.pixelSize: 16
                    elide: Text.ElideRight
                }
                PhoneIcon {
                    id: chevron
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    kind: help.rtl ? "back" : "next"
                    size: 20
                    color: PhoneTheme.muted
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    anchors.bottom: parent.bottom
                    height: 1
                    color: PhoneTheme.border
                }
                TapHandler {
                    id: rowTap
                    onTapped: help.show(row.index)
                }
            }
        }

        // a topic's page: the desktop's, as rich text
        Flickable {
            id: pageFlick
            objectName: "helpPage"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: bar.bottom
            anchors.bottom: parent.bottom
            visible: help.topic >= 0
            clip: true
            contentWidth: Math.max(width, pageText.contentWidth + 40)
            contentHeight: pageText.contentHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            Text {
                id: pageText
                objectName: "helpText"
                x: 20
                y: 20
                width: pageFlick.width - 40
                textFormat: Text.RichText
                wrapMode: Text.Wrap
                text: help.topic >= 0 ? PhoneHelp.page(help.topic, PhoneTheme.dark) : ""
                color: PhoneTheme.text
                linkColor: PhoneTheme.accent
                font.pixelSize: 15
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
    }
}

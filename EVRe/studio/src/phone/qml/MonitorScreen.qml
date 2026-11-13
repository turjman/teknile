// SPDX-License-Identifier: Apache-2.0
// The Monitor screen (STUDIO.md 25.6): the frames on the link (PhoneSession.frames), as the desktop's Monitor tab
// shows them, newest last: each frame's time and direction (TX, RX: the protocol's words) over its bytes in a
// monospaced font, left to right in Arabic too (bytes and time read that way); a note the engine adds (a function's
// name) beside them. The engine writes the frames only while this screen shows and Pause is off. Pause holds what is
// there, Clear empties it; the list follows the newest frame while it is at its end. Empty, it says what comes here
// (and, with no link, offers the Connect drawer).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    readonly property int lines: PhoneSession.frames.count
    readonly property bool paused: PhoneSession.frames.paused

    signal connectAsked()
    // a monospaced font: Android's "monospace"; Windows has no font by that name
    readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : "monospace"

    // the engine writes the frames while they are seen
    Binding {
        target: PhoneSession
        property: "monitorShown"
        value: screen.visible
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 8
            Layout.preferredHeight: 52
            spacing: 4

            Label {
                objectName: "monitorCount"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: screen.paused ? qsTr("paused · frames: %1").arg(screen.lines) : qsTr("frames: %1").arg(screen.lines)
                color: screen.paused ? PhoneTheme.warn : PhoneTheme.muted
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Button {
                objectName: "monitorPause"
                text: screen.paused ? qsTr("Resume") : qsTr("Pause")
                flat: true
                highlighted: screen.paused
                Material.foreground: PhoneTheme.accent
                onClicked: PhoneSession.frames.paused = !screen.paused
            }
            Button {
                objectName: "monitorClear"
                text: qsTr("Clear")
                flat: true
                Material.foreground: PhoneTheme.accent
                enabled: screen.lines > 0
                onClicked: PhoneSession.frames.clear()
            }
        }
        // the buttons apart from the frames
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: PhoneTheme.border
        }

        ListView {
            id: list
            objectName: "monitorList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: PhoneSession.frames
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            // bytes and times read left to right in every language
            LayoutMirroring.enabled: false
            LayoutMirroring.childrenInherit: true
            property bool following: true
            onMovementEnded: following = atYEnd
            onCountChanged: if (following) Qt.callLater(list.positionViewAtEnd)

            delegate: Item {
                id: frame
                required property string time
                required property string direction
                required property string bytes
                required property string note
                readonly property color tone: direction === "TX" || direction === "->DEV" ? PhoneTheme.accent
                        : direction.length > 0 ? PhoneTheme.good : PhoneTheme.warn
                width: ListView.view.width
                height: frameColumn.implicitHeight + 12

                ColumnLayout {
                    id: frameColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 6
                    spacing: 1
                    Row {
                        spacing: 10
                        Text {
                            objectName: "frameTime"
                            text: frame.time
                            visible: text.length > 0
                            color: PhoneTheme.muted
                            font.family: screen.mono
                            font.pixelSize: 12
                        }
                        Text {
                            objectName: "frameDirection"
                            text: frame.direction
                            visible: text.length > 0
                            color: frame.tone
                            font.family: screen.mono
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Text {
                            objectName: "frameNote"
                            text: frame.note
                            visible: text.length > 0 && frame.bytes.length > 0
                            color: PhoneTheme.muted
                            font.pixelSize: 12
                        }
                    }
                    Text {
                        objectName: "frameBytes"
                        Layout.fillWidth: true
                        // a note alone (frames not shown) in its place
                        text: frame.bytes.length > 0 ? frame.bytes : frame.note
                        color: frame.bytes.length > 0 ? PhoneTheme.text : PhoneTheme.warn
                        font.family: screen.mono
                        font.pixelSize: 13
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                }
                // the frames apart
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 16
                    anchors.bottom: parent.bottom
                    height: 1
                    color: PhoneTheme.border
                    opacity: 0.6
                }
            }
        }
    }

    ColumnLayout {
        objectName: "monitorEmptyState"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        visible: screen.lines === 0
        spacing: 12
        PhoneIcon {
            Layout.alignment: Qt.AlignHCenter
            kind: "monitor"
            size: 48
            color: PhoneTheme.muted
        }
        Label {
            objectName: "monitorEmpty"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: !PhoneSession.connected ? qsTr("Not connected: the frames on the link show here once it is up.")
                : screen.paused ? qsTr("Paused: Resume to see the frames again.")
                : qsTr("Waiting for frames…")
            color: PhoneTheme.muted
            font.pixelSize: 15
        }
        Button {
            objectName: "monitorConnect"
            Layout.alignment: Qt.AlignHCenter
            visible: !PhoneSession.connected
            text: qsTr("Connect…")
            highlighted: true
            onClicked: screen.connectAsked()
        }
    }
}

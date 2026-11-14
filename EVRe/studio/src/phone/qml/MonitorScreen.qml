// SPDX-License-Identifier: Apache-2.0
// The Monitor screen (STUDIO.md 25.6): the frames on the link (PhoneSession.frames), as the desktop's Monitor tab
// shows them, newest last: each frame's time and direction (TX, RX: the protocol's words), its function's name as a
// label coloured by its kind (a read asked in the accent, a write in amber, an answer in green, an error answer in red:
// FrameLog's type and kind), then its bytes in a monospaced font, left to right in Arabic too (bytes and time read that
// way); a note the engine adds (a fast stream's name) beside them. A tap on a frame opens its sheet: the frame field by
// field as the desktop's Dump spells it (FrameSheet). The engine writes the frames only while this screen shows and
// Pause is off. TX and RX are filter chips with their counts (a RowFilter over the side, kept in phone/monitorSides;
// a note shows either way); Pause holds what is there, Clear empties it; the list follows the newest frame while it is
// at its end. Empty, it says what comes here (and, with no link, offers the Connect drawer).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    readonly property int lines: PhoneSession.frames.count
    readonly property bool paused: PhoneSession.frames.paused
    readonly property alias sheet: frameSheet

    signal connectAsked()
    // a fixed-pitch family the phone has (PhoneTheme.monoFamily: "monospace" was drawn in Roboto on a Samsung phone,
    // the bytes out of line)
    readonly property string mono: PhoneTheme.monoFamily

    // a kind's colour and its soft tone behind the label
    function kindTone(kind) {
        return kind === "read" ? PhoneTheme.accent : kind === "write" ? PhoneTheme.warn
             : kind === "answer" ? PhoneTheme.good : kind === "error" ? PhoneTheme.bad : PhoneTheme.muted
    }
    function kindSoft(kind) {
        return kind === "read" ? PhoneTheme.accentSoft : kind === "write" ? PhoneTheme.warnSoft
             : kind === "answer" ? PhoneTheme.goodSoft : kind === "error" ? PhoneTheme.badSoft : PhoneTheme.surface2
    }

    // the engine writes the frames while they are seen
    Binding {
        target: PhoneSession
        property: "monitorShown"
        value: screen.visible
    }

    // the sides shown: sent, received, and the notes always
    RowFilter {
        id: sides
        objectName: "monitorFilter"
        sourceModel: PhoneSession.frames
        roleName: "side"
        shown: [0, 1, 2]
        settingsKey: "phone/monitorSides"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 4

            Repeater {
                model: 2
                FilterChip {
                    required property int index
                    objectName: index === 0 ? "monitorTx" : "monitorRx"
                    // the protocol's words, as the frames write them
                    label: index === 0 ? "TX" : "RX"
                    count: index === 0 ? PhoneSession.frames.sentCount : PhoneSession.frames.receivedCount
                    checked: sides.shown.indexOf(index) >= 0
                    onToggled: sides.setValueShown(index, checked)
                }
            }
            Label {
                objectName: "monitorCount"
                Layout.fillWidth: true
                Layout.leftMargin: 4
                horizontalAlignment: Text.AlignLeft
                visible: screen.paused
                text: qsTr("paused")
                color: PhoneTheme.warn
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Item {
                Layout.fillWidth: true
                visible: !screen.paused
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
            model: sides
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            // bytes and times read left to right in every language
            LayoutMirroring.enabled: false
            LayoutMirroring.childrenInherit: true
            property bool following: true
            onMovementEnded: following = atYEnd
            onCountChanged: if (following) Qt.callLater(list.positionViewAtEnd)

            delegate: ItemDelegate {
                id: frame
                required property string time
                required property string direction
                required property string bytes
                required property string note
                required property string type
                required property string kind
                objectName: "frameRow"
                width: ListView.view.width
                height: frameColumn.implicitHeight + 12
                padding: 0
                // a frame opens its sheet; a note alone (frames not shown) has nothing to spell out
                enabled: bytes.length > 0
                Accessible.name: qsTr("Frame %1 %2: show its fields").arg(direction).arg(type)
                onClicked: frameSheet.openFor(frame.time, frame.direction, frame.bytes, frame.note, frame.type, frame.kind)
                background: Rectangle {
                    color: frame.pressed ? PhoneTheme.surface2 : "transparent"
                }
                contentItem: Item {}

                ColumnLayout {
                    id: frameColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 6
                    spacing: 2
                    Row {
                        spacing: 8
                        Text {
                            objectName: "frameTime"
                            anchors.verticalCenter: parent.verticalCenter
                            text: frame.time
                            visible: text.length > 0
                            color: PhoneTheme.muted
                            font.family: screen.mono
                            font.pixelSize: 12
                        }
                        Text {
                            objectName: "frameDirection"
                            anchors.verticalCenter: parent.verticalCenter
                            text: frame.direction
                            visible: text.length > 0
                            color: PhoneTheme.text
                            font.family: screen.mono
                            font.pixelSize: 12
                            font.bold: true
                        }
                        // the function's name on its kind's soft tone, in the kind's colour
                        Rectangle {
                            objectName: "frameTypeBox"
                            anchors.verticalCenter: parent.verticalCenter
                            visible: frame.type.length > 0
                            width: typeText.implicitWidth + 12
                            height: typeText.implicitHeight + 4
                            radius: 4
                            color: screen.kindSoft(frame.kind)
                            Text {
                                id: typeText
                                objectName: "frameType"
                                anchors.centerIn: parent
                                text: frame.type
                                color: screen.kindTone(frame.kind)
                                font.family: screen.mono
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                        Text {
                            objectName: "frameNote"
                            anchors.verticalCenter: parent.verticalCenter
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
                // a frame opens: a chevron at its end says so
                PhoneIcon {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: frame.bytes.length > 0
                    kind: "next"
                    size: 16
                    color: PhoneTheme.muted
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
        visible: list.count === 0
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
                : screen.lines > 0 ? qsTr("TX and RX both off: turn one on to see its frames.")
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

    FrameSheet {
        id: frameSheet
        objectName: "frameSheet"
        rtl: Qt.application.layoutDirection === Qt.RightToLeft
        mono: screen.mono
        tone: screen.kindTone(frameSheet.kind)
        softTone: screen.kindSoft(frameSheet.kind)
        width: screen.Window.window ? screen.Window.window.width : screen.width
    }
}

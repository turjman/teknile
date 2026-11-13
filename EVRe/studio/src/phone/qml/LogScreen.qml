// SPDX-License-Identifier: Apache-2.0
// The Log screen: what happened on the link (PhoneSession.log), newest last, each line's time muted over its text,
// the text coloured by its level (the text's colour, amber for a warning, red for an error, with a bar of that colour
// at the line's start edge). Copy puts it all on the clipboard, Clear empties it. The list follows the newest line
// while it is at its end; scrolled up, it stays where it is. Empty, it shows the Log's icon and says what comes here.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    readonly property int lines: PhoneSession.log.count

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
                objectName: "logCount"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: qsTr("lines: %1").arg(screen.lines)
                color: PhoneTheme.muted
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Button {
                objectName: "logCopy"
                text: qsTr("Copy")
                flat: true
                Material.foreground: PhoneTheme.accent
                enabled: screen.lines > 0
                onClicked: PhoneSession.log.copy()
            }
            Button {
                objectName: "logClear"
                text: qsTr("Clear")
                flat: true
                Material.foreground: PhoneTheme.accent
                enabled: screen.lines > 0
                onClicked: PhoneSession.log.clear()
            }
        }
        // the buttons apart from the lines
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: PhoneTheme.border
        }

        ListView {
            id: list
            objectName: "logList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: PhoneSession.log
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            property bool following: true
            onMovementEnded: following = atYEnd
            onCountChanged: if (following) Qt.callLater(list.positionViewAtEnd)

            delegate: Item {
                id: line
                required property string time
                required property string text
                required property int level
                readonly property color tone: level === 2 ? PhoneTheme.bad : level === 1 ? PhoneTheme.warn : PhoneTheme.text
                width: ListView.view.width
                height: lineColumn.implicitHeight + 20

                // a warning's or an error's bar at the start edge
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    width: 3
                    radius: 1.5
                    visible: line.level > 0
                    color: line.tone
                }
                ColumnLayout {
                    id: lineColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 10
                    spacing: 2
                    Label {
                        objectName: "logTime"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: line.time
                        color: PhoneTheme.muted
                        font.pixelSize: 12
                    }
                    Label {
                        objectName: "logText"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: line.text
                        color: line.tone
                        font.pixelSize: 14
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                }
                // the lines apart
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
        objectName: "logEmptyState"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        visible: screen.lines === 0
        spacing: 12
        PhoneIcon {
            objectName: "logEmptyIcon"
            Layout.alignment: Qt.AlignHCenter
            kind: "log"
            size: 48
            color: PhoneTheme.muted
        }
        Label {
            objectName: "logEmpty"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Nothing yet: the map, the link and the recordings are told here.")
            color: PhoneTheme.muted
            font.pixelSize: 15
        }
    }
}

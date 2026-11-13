// SPDX-License-Identifier: Apache-2.0
// The Log screen: what happened on the link (PhoneSession.log), newest last, each line's time and its text coloured by
// its level (the text's colour, amber for a warning, red for an error). Copy puts it all on the clipboard, Clear
// empties it. The list follows the newest line while it is at its end; scrolled up, it stays where it is.
import QtQuick
import QtQuick.Controls
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
            spacing: 4

            Label {
                objectName: "logCount"
                Layout.fillWidth: true
                text: qsTr("lines: %1").arg(screen.lines)
                color: PhoneTheme.muted
                font.pixelSize: 14
                elide: Text.ElideRight
            }
            Button {
                objectName: "logCopy"
                text: qsTr("Copy")
                flat: true
                enabled: screen.lines > 0
                onClicked: PhoneSession.log.copy()
            }
            Button {
                objectName: "logClear"
                text: qsTr("Clear")
                flat: true
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
                width: ListView.view.width
                height: lineText.implicitHeight + 12

                Label {
                    id: timeText
                    objectName: "logTime"
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    text: model.time
                    color: PhoneTheme.muted
                    font.pixelSize: 13
                }
                Label {
                    id: lineText
                    objectName: "logText"
                    anchors.left: timeText.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    text: model.text
                    color: model.level === 2 ? PhoneTheme.bad : model.level === 1 ? PhoneTheme.warn : PhoneTheme.text
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
                // the lines apart
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: PhoneTheme.grid
                }
            }
        }
    }

    Label {
        objectName: "logEmpty"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        visible: screen.lines === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Nothing yet: the map, the link and the recordings are told here.")
        color: PhoneTheme.muted
        font.pixelSize: 16
    }
}

// SPDX-License-Identifier: Apache-2.0
// The proof P-9 (p9_proof.h): the example map's registers as plain rows, every value changing 20 times a second, to
// be flicked on the phone while P9Meter writes the frames to logcat. The bar at the top keeps running, so the window
// draws every frame and the meter times frames, not pauses.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import EVRe.Phone

Rectangle {
    color: PhoneTheme.bg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: runnerTrack
            objectName: "p9Runner"
            Layout.fillWidth: true
            implicitHeight: 4
            clip: true

            Rectangle {
                width: 64
                height: 4
                color: PhoneTheme.accent
                NumberAnimation on x {
                    from: -64
                    to: runnerTrack.width
                    duration: 1500
                    loops: Animation.Infinite
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: 12
            text: qsTr("Proof P-9: the values change 20 times a second. Flick the list.")
            color: PhoneTheme.muted
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
        ListView {
            id: list
            objectName: "p9List"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: P9Registers { objectName: "p9Model" }

            delegate: Item {
                required property string name
                required property string address
                required property string value
                required property string unit
                width: ListView.view.width
                height: 56

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: valueText.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter

                    Label {
                        width: parent.width
                        text: name
                        color: PhoneTheme.text
                        font.pixelSize: 16
                        elide: Text.ElideRight
                    }
                    Label {
                        text: address
                        color: PhoneTheme.muted
                        font.pixelSize: 12
                    }
                }
                Label {
                    id: valueText
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: unit.length > 0 ? value + " " + unit : value
                    color: PhoneTheme.text
                    font.pixelSize: 18
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: PhoneTheme.border
                }
            }
        }
    }
}

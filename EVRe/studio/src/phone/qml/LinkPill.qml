// SPDX-License-Identifier: Apache-2.0
// The link's state in the header, as a pill: a dot and a short text (B.0: always "Not connected"; B.1 gives it the
// link's states and their colours).
import QtQuick
import QtQuick.Controls
import EVRe.Phone

Rectangle {
    property alias text: label.text
    property color dotColor: PhoneTheme.muted

    implicitWidth: row.implicitWidth + 24
    implicitHeight: 30
    radius: height / 2
    color: PhoneTheme.surface2
    border.color: PhoneTheme.border

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: dotColor
        }
        Label {
            id: label
            objectName: "linkPillText"
            anchors.verticalCenter: parent.verticalCenter
            color: PhoneTheme.muted
            font.pixelSize: 13
        }
    }
}

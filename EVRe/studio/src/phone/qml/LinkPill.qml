// SPDX-License-Identifier: Apache-2.0
// The link's state in the header, as a pill: a dot in the state's colour (PhoneSession: grey off, amber on the way,
// green connected, red closed with a reason) and a short text. A tap opens the Connect drawer, where the whole story is.
import QtQuick
import QtQuick.Controls
import EVRe.Phone

Rectangle {
    id: pill
    property alias text: label.text
    property color dotColor: PhoneTheme.muted
    signal clicked()

    implicitWidth: row.implicitWidth + 24
    implicitHeight: 30
    radius: height / 2
    color: tap.pressed ? PhoneTheme.border : PhoneTheme.surface2
    border.color: PhoneTheme.border
    Accessible.role: Accessible.Button
    Accessible.name: label.text
    Accessible.onPressAction: pill.clicked()

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            objectName: "linkPillDot"
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: pill.dotColor
        }
        Label {
            id: label
            objectName: "linkPillText"
            anchors.verticalCenter: parent.verticalCenter
            color: PhoneTheme.muted
            font.pixelSize: 13
        }
    }

    TapHandler {
        id: tap
        onTapped: pill.clicked()
    }
}

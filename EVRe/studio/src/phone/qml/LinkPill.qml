// SPDX-License-Identifier: Apache-2.0
// The link's state in the header, as a pill: a dot in the state's colour (PhoneSession: grey off, amber on the way,
// green connected, red closed with a reason) and a short text in the text colour, on the state's soft tone (PhoneTheme),
// so it reads in both themes. The pill is 32 high in a 48 high target. A tap opens the Connect drawer, where the
// whole story is.
import QtQuick
import QtQuick.Controls
import EVRe.Phone

Item {
    id: pill
    property alias text: label.text
    // "good", "busy", "bad" or "off"
    property string tone: "off"
    readonly property color dotColor: tone === "good" ? PhoneTheme.good : tone === "bad" ? PhoneTheme.bad
                                      : tone === "busy" ? PhoneTheme.warn : PhoneTheme.muted
    readonly property color softColor: tone === "good" ? PhoneTheme.goodSoft : tone === "bad" ? PhoneTheme.badSoft
                                       : tone === "busy" ? PhoneTheme.warnSoft : PhoneTheme.surface2
    signal clicked()

    // the dot, the gap and the text, inside the pill's paddings; the text cut at its end when the header is narrow
    implicitWidth: label.implicitWidth + 8 + 8 + 28
    implicitHeight: 48
    Accessible.role: Accessible.Button
    Accessible.name: label.text
    Accessible.onPressAction: pill.clicked()

    Rectangle {
        objectName: "linkPillBox"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 32
        radius: height / 2
        color: tap.pressed ? PhoneTheme.border : pill.softColor
        border.color: pill.tone === "off" ? PhoneTheme.border : "transparent"

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8

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
                width: Math.max(0, Math.min(implicitWidth, pill.width - 28 - 16))
                color: PhoneTheme.text
                font.pixelSize: 13
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }
    }

    TapHandler {
        id: tap
        onTapped: pill.clicked()
    }
}

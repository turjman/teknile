// SPDX-License-Identifier: Apache-2.0
// A screen with nothing to show yet (the Chart with no line, the Registers with no map, an empty Log): the screen's
// icon large and muted, a short title, a line saying what comes here, and one button to where it comes from. Centred,
// at most 320 wide; the texts wrap, never cut.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

ColumnLayout {
    id: box
    property alias icon: picture.kind
    property alias title: titleText.text
    property alias text: bodyText.text
    // the button's text; empty: no button
    property string action: ""
    property string prefix: "empty"
    // smaller where the room is low (the Chart across)
    property real iconSize: 48

    signal acted()

    spacing: 8

    PhoneIcon {
        id: picture
        objectName: box.prefix + "Icon"
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: 4
        size: box.iconSize
        color: PhoneTheme.muted
    }
    Label {
        id: titleText
        objectName: box.prefix + "Title"
        Layout.fillWidth: true
        visible: text.length > 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: PhoneTheme.text
        font.pixelSize: 17
        font.weight: Font.Medium
    }
    Label {
        id: bodyText
        objectName: box.prefix + "Text"
        Layout.fillWidth: true
        visible: text.length > 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: PhoneTheme.muted
        font.pixelSize: 14
    }
    Button {
        objectName: box.prefix + "Button"
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 8
        Layout.preferredHeight: 48
        visible: box.action.length > 0
        text: box.action
        highlighted: true
        Material.accent: PhoneTheme.accentFill
        onClicked: box.acted()
    }
}

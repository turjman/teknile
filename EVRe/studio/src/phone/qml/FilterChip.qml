// SPDX-License-Identifier: Apache-2.0
// A filter chip (the Log's levels, the Monitor's TX and RX): an icon, a name and a count on a 32 dp pill in a 48 dp touch
// target. On: the pill in its tone's soft colour (PhoneTheme), its edge and icon in the tone; off: a plain edge, the icon
// and the text muted, the name struck through, as a hidden line's chip in the Chart's legend. A tap turns it on or off
// (the screen keeps the choice in its RowFilter); a screen reader hears it as a check box.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
import EVRe.Phone

AbstractButton {
    id: chip
    property string iconKind: ""
    property color tone: PhoneTheme.accent
    property color softTone: PhoneTheme.accentSoft
    property string label: ""
    property int count: -1

    checkable: true
    implicitHeight: 48
    implicitWidth: row.implicitWidth + 20
    Accessible.role: Accessible.CheckBox
    Accessible.name: count >= 0 ? qsTr("%1: %2").arg(label).arg(count) : label
    Accessible.checked: checked

    background: Item {
        Rectangle {
            id: face
            objectName: "filterChipFace"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 32
            radius: 16
            color: chip.checked ? chip.softTone : "transparent"
            border.width: 1
            border.color: chip.checked ? chip.tone : PhoneTheme.border
            Ripple {
                anchors.fill: parent
                clipRadius: face.radius
                pressed: chip.pressed
                anchor: chip
                active: chip.pressed
                color: chip.Material.rippleColor
            }
        }
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            PhoneIcon {
                objectName: "filterChipIcon"
                anchors.verticalCenter: parent.verticalCenter
                visible: chip.iconKind.length > 0
                kind: chip.iconKind
                size: 16
                color: chip.checked ? chip.tone : PhoneTheme.muted
                markColor: chip.checked ? chip.softTone : PhoneTheme.bg
            }
            Text {
                objectName: "filterChipName"
                anchors.verticalCenter: parent.verticalCenter
                text: chip.label
                color: chip.checked ? PhoneTheme.text : PhoneTheme.muted
                font.pixelSize: 13
                font.strikeout: !chip.checked
            }
            Text {
                objectName: "filterChipCount"
                anchors.verticalCenter: parent.verticalCenter
                visible: chip.count >= 0
                text: String(chip.count)
                color: chip.checked ? PhoneTheme.text : PhoneTheme.muted
                font.pixelSize: 13
                font.bold: true
            }
        }
    }
}

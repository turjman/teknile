// SPDX-License-Identifier: Apache-2.0
// A menu of the phone's (STUDIO.md 25.6): a card's "..." menu, a legend chip's, Fast lines. Kept `edge` dp inside the
// window (Material's menu has no margin: a card's menu touched the screen's right edge), and raised off the page by
// more than a shadow: Material's shadow is lost on the dark page (the owner's phone: the menu's dark face on the dark
// page had no edge), so its face is the raised surface and its edge the control colour, 3:1 or more to the page in
// both looks. openUnder() hangs it under the button that asks, toward the window's middle: a button in the right half
// (a card's in English) has the menu's end at its end, one in the left half (a card's in Arabic) its start.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
import EVRe.Phone

Menu {
    id: menu
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property real edge: 8

    margins: edge

    function openUnder(from) {
        const middle = from.mapToItem(null, from.width / 2, 0).x
        menu.popup(from, middle > from.Window.width / 2 ? from.width - menu.width : 0, from.height)
    }

    background: Rectangle {
        objectName: "menuFace"
        implicitWidth: 200
        implicitHeight: menu.Material.menuItemHeight
        radius: 4
        color: PhoneTheme.surface2
        border.width: 1
        border.color: PhoneTheme.control
        layer.enabled: menu.Material.elevation > 0
        layer.effect: ElevationEffect {
            elevation: menu.Material.elevation
        }
    }
}

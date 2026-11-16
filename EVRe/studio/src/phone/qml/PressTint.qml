// SPDX-License-Identifier: Apache-2.0
// A press in the item's own shape (the owner: no circles): a flat tint over the item's rounded rectangle, in at once
// when a finger comes down and fading out when it lifts; never a ripple's circle growing from the finger, a long press
// included. Material's own controls show the same through their Ripple with its wave turned off (phone_app.cpp). It
// fills its parent with the parent's corners unless told otherwise; its colour is Material's ripple colour, the
// strength a held ripple reaches.
import QtQuick
import QtQuick.Controls.Material
import EVRe.Phone

Rectangle {
    id: tint
    property bool pressed: false
    objectName: "pressTint"
    anchors.fill: parent
    radius: parent && parent.radius !== undefined ? parent.radius : 0
    color: Material.rippleColor
    opacity: 0
    onPressedChanged: {
        fade.stop()
        if (pressed || !PhoneSession.motion) opacity = pressed ? 1 : 0
        else fade.start()
    }
    NumberAnimation {
        id: fade
        target: tint
        property: "opacity"
        to: 0
        duration: 250
        easing.type: Easing.OutCubic
    }
}

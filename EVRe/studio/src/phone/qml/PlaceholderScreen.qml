// SPDX-License-Identifier: Apache-2.0
// A screen still to come (B.0): its note, centred and muted, saying what it will show.
import QtQuick
import QtQuick.Controls
import EVRe.Phone

Item {
    property alias note: noteText.text

    Label {
        id: noteText
        objectName: "placeholderNote"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: PhoneTheme.muted
        font.pixelSize: 16
    }
}

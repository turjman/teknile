// SPDX-License-Identifier: Apache-2.0
// One register on the Registers screen, as a card: its name, its value large with its unit, the decoded text under it
// (an enum's name, the bit fields), its address muted. A writable register has a Write button; every card has the
// "⋯" button (Plot, Copy). A tap on a writable card repeats Write, a long press repeats "⋯".
// A changed value glows: the card tinted toward the accent and a bar at its start edge, fading over the desktop's
// GLOW_MS (RegisterModel), with an arrow up or down for a plain number. A value not refreshed for a while is greyed
// and says so; a register the device refused, or a read that failed, shows its error in red.
// The value, the unit and the address run left to right in Arabic too (10.5), at the card's start edge; the map's
// names (Latin) are aligned to the start edge by hand: a text left to its own letters would sit at the left.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: card
    required property string name
    required property string addr
    required property string value
    required property string unit
    required property string decoded
    required property real glow
    required property int direction
    required property bool writable
    required property bool plotted
    required property string error
    required property bool stale
    required property int source

    signal writeAsked(int source)
    signal moreAsked(int source, Item from)

    objectName: "card_" + name
    implicitHeight: panel.implicitHeight + 8

    // the glow's colour at full strength: the desktop's tint (ValueDelegate::GLOW_TINT) and the accent
    readonly property real tintAlpha: 0.12
    readonly property color accent: PhoneTheme.accent

    // the glow from its strength now (1 just changed .. 0) to nothing, eased as RegisterModel::glowStrength fades:
    // (1 - k)^2 over GLOW_MS, so a card that comes into view mid-glow takes up the rest of it
    function startGlow() {
        fade.stop()
        if (card.glow <= 0) {
            tint.color = Qt.rgba(accent.r, accent.g, accent.b, 0)
            bar.opacity = 0
            return
        }
        const left = Math.sqrt(card.glow)
        tintFade.from = Qt.rgba(accent.r, accent.g, accent.b, card.tintAlpha * card.glow)
        tintFade.to = Qt.rgba(accent.r, accent.g, accent.b, 0)
        barFade.from = card.glow
        fade.duration = PhoneSession.registerList.glowMs * left
        fade.restart()
    }
    onGlowChanged: startGlow()
    Component.onCompleted: startGlow()
    ListView.onReused: startGlow()

    ParallelAnimation {
        id: fade
        property int duration: 1500
        ColorAnimation {
            id: tintFade
            target: tint
            property: "color"
            duration: fade.duration
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            id: barFade
            target: bar
            property: "opacity"
            to: 0
            duration: fade.duration
            easing.type: Easing.OutQuad
        }
    }

    Rectangle {
        id: panel
        objectName: "cardPanel"
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        implicitHeight: body.implicitHeight + 12
        radius: 8
        color: tap.pressed ? PhoneTheme.surface2 : PhoneTheme.surface
        border.color: PhoneTheme.border

        Rectangle {
            id: tint
            objectName: "cardTint"
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
        }
        Rectangle {
            id: bar
            objectName: "cardGlowBar"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 3
            width: 4
            radius: 2
            color: PhoneTheme.accent
            opacity: 0
        }

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 16
            anchors.rightMargin: 4
            anchors.topMargin: 2
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    objectName: "regName"
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                    text: card.name
                    color: PhoneTheme.text
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "writeButton"
                    visible: card.writable
                    text: qsTr("Write")
                    flat: true
                    Layout.preferredHeight: 48
                    onClicked: card.writeAsked(card.source)
                }
                // three dots drawn, not a character: a phone's font may have no "⋯"
                ToolButton {
                    id: more
                    objectName: "moreButton"
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    Accessible.name: qsTr("More: Plot, Copy")
                    onClicked: card.moreAsked(card.source, more)
                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 3
                            Repeater {
                                model: 3
                                Rectangle {
                                    objectName: "moreDot"
                                    width: 4
                                    height: 4
                                    radius: 2
                                    color: PhoneTheme.text
                                }
                            }
                        }
                    }
                }
            }

            // the value and its unit, left to right in every language, at the start edge
            Row {
                objectName: "valueRow"
                Layout.alignment: Qt.AlignLeft
                Layout.maximumWidth: body.width - 8
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                spacing: 6

                Label {
                    id: valueText
                    objectName: "regValue"
                    text: card.value
                    color: card.error.length > 0 ? PhoneTheme.bad
                           : card.stale ? PhoneTheme.muted : PhoneTheme.text
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                    width: Math.min(implicitWidth, body.width - 8 - unitText.width - arrow.width - 12)
                    elide: Text.ElideRight
                }
                Label {
                    id: unitText
                    objectName: "regUnit"
                    anchors.baseline: valueText.baseline
                    visible: text.length > 0
                    text: card.unit
                    color: PhoneTheme.muted
                    font.pixelSize: 16
                }
                // the glowing change's way: a triangle up or down (a square turned, half of it shown)
                Item {
                    id: arrow
                    objectName: "glowArrow"
                    anchors.verticalCenter: valueText.verticalCenter
                    width: 12
                    height: 7
                    clip: true
                    visible: card.direction !== 0
                    opacity: bar.opacity
                    rotation: card.direction < 0 ? 180 : 0
                    Rectangle {
                        width: 8.5
                        height: 8.5
                        x: 1.75
                        y: 2.75
                        rotation: 45
                        color: PhoneTheme.accent
                    }
                }
            }
            Label {
                objectName: "regDecoded"
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                Layout.rightMargin: 12
                visible: text.length > 0
                text: card.decoded
                color: PhoneTheme.muted
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            Label {
                objectName: "regError"
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                Layout.rightMargin: 12
                visible: card.error.length > 0
                text: card.error
                color: PhoneTheme.bad
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: 12
                Layout.bottomMargin: 4
                spacing: 8
                Label {
                    objectName: "regAddr"
                    text: card.addr
                    color: PhoneTheme.muted
                    font.pixelSize: 12
                }
                Label {
                    objectName: "regStale"
                    Layout.fillWidth: true
                    visible: card.stale
                    text: qsTr("not refreshed")
                    color: PhoneTheme.warn
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Label {
                    objectName: "regPlotted"
                    Layout.fillWidth: !card.stale
                    horizontalAlignment: Text.AlignRight
                    visible: card.plotted
                    text: qsTr("on the chart")
                    color: PhoneTheme.accent
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }

    TapHandler {
        id: tap
        onTapped: if (card.writable) card.writeAsked(card.source)
        onLongPressed: card.moreAsked(card.source, more)
    }
}

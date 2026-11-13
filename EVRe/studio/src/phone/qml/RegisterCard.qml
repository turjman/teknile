// SPDX-License-Identifier: Apache-2.0
// One register on the Registers screen, as a card: its name with its address beside it, muted; its value large (the
// card's hero) with its unit beside it in a lighter weight; the decoded text under it (an enum's name, the bit fields).
// A writable register has a Write button; every card has the "⋯" button (Plot, Copy); both are 48 high and at the
// card's end. A tap on a writable card repeats Write, a long press repeats "⋯"; a press shows Material's ripple.
// A changed value glows: the card tinted toward the accent and a bar at its start edge, fading over the desktop's
// GLOW_MS (RegisterModel), with an arrow up or down for a plain number. A value not refreshed for a while is greyed
// and a chip says so; a register the device refused, or a read that failed, shows its error in red, the card's edge
// red too. A plotted register has a chip "on the chart".
// The value, the unit and the address run left to right in Arabic too (10.5), at the card's start edge; the map's
// names (Latin) are aligned to the start edge by hand: a text left to its own letters would sit at the left.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
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

    // a small rounded label: "not refreshed", "on the chart"
    component Chip: Rectangle {
        property alias text: chipText.text
        property alias textColor: chipText.color
        property color dot: "transparent"
        property alias textName: chipText.objectName
        implicitWidth: chipRow.implicitWidth + 16
        implicitHeight: 24
        radius: 12
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.parent.dot.a > 0
                width: 6
                height: 6
                radius: 3
                color: parent.parent.dot
            }
            Label {
                id: chipText
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12
                font.weight: Font.Medium
            }
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
        radius: 12
        color: PhoneTheme.surface
        border.color: card.error.length > 0 ? PhoneTheme.bad : PhoneTheme.border

        Ripple {
            anchors.fill: parent
            clipRadius: panel.radius
            pressed: tap.pressed
            anchor: panel
            active: tap.pressed
            color: panel.Material.rippleColor
        }
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
            anchors.topMargin: 10
            anchors.bottomMargin: 10
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

            // the name and its address; the buttons at the end
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    objectName: "regName"
                    horizontalAlignment: Text.AlignLeft
                    Layout.maximumWidth: Math.max(48, body.width - addrText.implicitWidth - writeButton.width - more.width - 24)
                    text: card.name
                    color: PhoneTheme.text
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Label {
                    id: addrText
                    objectName: "regAddr"
                    Layout.fillWidth: true
                    // left to right, beside the name at its start edge
                    horizontalAlignment: Text.AlignLeft
                    text: card.addr
                    color: PhoneTheme.muted
                    font.pixelSize: 12
                }
                Button {
                    id: writeButton
                    objectName: "writeButton"
                    visible: card.writable
                    text: qsTr("Write")
                    flat: true
                    Material.foreground: PhoneTheme.accent
                    // a tap leaves no focus mark behind: the card's buttons are touched, not tabbed to
                    focusPolicy: Qt.NoFocus
                    Layout.preferredHeight: 48
                    Layout.minimumWidth: 64
                    onClicked: card.writeAsked(card.source)
                }
                // three dots drawn, not a character: a phone's font may have no "⋯"
                ToolButton {
                    id: more
                    objectName: "moreButton"
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    focusPolicy: Qt.NoFocus
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
                                    color: PhoneTheme.muted
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
                Layout.maximumWidth: body.width - 12
                Layout.topMargin: -8
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                spacing: 6

                Label {
                    id: valueText
                    objectName: "regValue"
                    text: card.value
                    color: card.error.length > 0 ? PhoneTheme.bad
                           : card.stale ? PhoneTheme.muted : PhoneTheme.text
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                    width: Math.min(implicitWidth, body.width - 12 - unitText.width - arrow.width - 12)
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
                    font.weight: Font.Normal
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
                Layout.topMargin: 2
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
                Layout.topMargin: 2
                visible: card.error.length > 0
                text: card.error
                color: PhoneTheme.bad
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            // the card's states, quietly: not refreshed, on the chart
            Flow {
                Layout.fillWidth: true
                Layout.rightMargin: 12
                Layout.topMargin: 8
                visible: card.stale || card.plotted
                spacing: 8
                Chip {
                    objectName: "regStaleChip"
                    visible: card.stale
                    color: PhoneTheme.warnSoft
                    text: qsTr("not refreshed")
                    textColor: PhoneTheme.warn
                    textName: "regStale"
                }
                Chip {
                    objectName: "regPlottedChip"
                    visible: card.plotted
                    color: PhoneTheme.accentSoft
                    dot: PhoneTheme.accent
                    text: qsTr("on the chart")
                    textColor: PhoneTheme.text
                    textName: "regPlotted"
                }
            }
            Item {
                Layout.preferredHeight: 6
            }
        }
    }

    TapHandler {
        id: tap
        onTapped: if (card.writable) card.writeAsked(card.source)
        onLongPressed: card.moreAsked(card.source, more)
    }
}

// SPDX-License-Identifier: Apache-2.0
// One register on the Registers screen, as a row of the desktop's table: its name at the start edge with its address
// beside it, small and muted; its value at the end edge in tabular digits, right-aligned in a slot of its own so the
// values of the rows above and below line up, its unit after it in a slot of its own; a chevron at the end, since a
// tap opens the register's sheet (its value large, its trend, Plot, Copy and Write). A second, smaller line only when
// there is more to say: "not refreshed", "on the chart", the decoded text (an enum's name, the bit fields) or the
// error. A thin line under each row; the screen's sections give the groups. At least 48 high, about 52. Across
// (compact, the Registers screen in landscape) a row is one line 42 high: what the second line says goes between the
// address and the value, so seven rows show at 760 x 384 (a full-width target, a full-width target).
// A changed value glows as on the desktop: the row tinted toward the accent and a bar at its start edge, fading over
// GLOW_MS (RegisterModel), with an arrow up or down for a plain number before the value. A value not refreshed for a
// while is greyed and the second line says so; a register the device refused, or a read that failed, shows its value
// and its error in red with a red bar at its start edge. A press shows Material's ripple.
// The value and its unit run left to right in Arabic too (10.5): the row mirrors (the name at the right, the value at
// the left, by the chevron), and the values line up at their left edge there, the unit right after each. The map's
// names (Latin) are aligned to the start edge by hand: a text left to its own letters would sit at the left.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: row
    required property string name
    required property string addr
    required property string value
    required property string unit
    required property string decoded
    required property real glow
    required property int direction
    required property bool plotted
    required property string error
    required property bool stale
    required property int source
    // one line, 42 high (across)
    property bool compact: false

    signal opened(int source)

    objectName: "row_" + name
    implicitHeight: compact ? 42 : Math.max(52, body.implicitHeight + 14)

    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property bool failed: error.length > 0
    readonly property bool secondLine: failed || stale || plotted || decoded.length > 0

    // the glow's colour at full strength: the desktop's tint (ValueDelegate::GLOW_TINT) and the accent
    readonly property real tintAlpha: 0.12
    readonly property color accent: PhoneTheme.accent

    // the glow from its strength now (1 just changed .. 0) to nothing, eased as RegisterModel::glowStrength fades:
    // (1 - k)^2 over GLOW_MS, so a row that comes into view mid-glow takes up the rest of it
    function startGlow() {
        fade.stop()
        if (row.glow <= 0) {
            tint.color = Qt.rgba(accent.r, accent.g, accent.b, 0)
            bar.opacity = 0
            return
        }
        const left = Math.sqrt(row.glow)
        tintFade.from = Qt.rgba(accent.r, accent.g, accent.b, row.tintAlpha * row.glow)
        tintFade.to = Qt.rgba(accent.r, accent.g, accent.b, 0)
        barFade.from = row.glow
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

    // a small label on the second line: "not refreshed", "on the chart"
    component Tag: Rectangle {
        property alias text: tagText.text
        property alias textColor: tagText.color
        property color dot: "transparent"
        property alias textName: tagText.objectName
        implicitWidth: tagRow.implicitWidth + 12
        implicitHeight: 18
        radius: 9
        Row {
            id: tagRow
            anchors.centerIn: parent
            spacing: 5
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.parent.dot.a > 0
                width: 6
                height: 6
                radius: 3
                color: parent.parent.dot
            }
            Label {
                id: tagText
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 11
                font.weight: Font.Medium
            }
        }
    }

    // what the second line says: "not refreshed", "on the chart", the decoded text or the error. `suffix` names the
    // inline copy's parts apart from the second line's
    component SecondLine: RowLayout {
        property string suffix: ""
        spacing: 6
        Tag {
            objectName: "regStaleTag" + parent.suffix
            visible: row.stale
            color: PhoneTheme.warnSoft
            text: qsTr("not refreshed")
            textColor: PhoneTheme.warn
            textName: "regStale" + parent.suffix
        }
        Tag {
            objectName: "regPlottedTag" + parent.suffix
            visible: row.plotted
            color: PhoneTheme.accentSoft
            dot: PhoneTheme.accent
            text: qsTr("on the chart")
            textColor: PhoneTheme.text
            textName: "regPlotted" + parent.suffix
        }
        Label {
            objectName: "regDecoded" + parent.suffix
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
            visible: !row.failed && text.length > 0
            text: row.decoded
            color: PhoneTheme.muted
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Label {
            objectName: "regError" + parent.suffix
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
            visible: row.failed
            text: row.error
            color: PhoneTheme.bad
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        // the tags alone: they keep to the start edge
        Item {
            Layout.fillWidth: true
            visible: !row.failed && row.decoded.length === 0
        }
    }

    Rectangle {
        id: panel
        objectName: "rowPanel"
        anchors.fill: parent
        color: PhoneTheme.surface

        Ripple {
            anchors.fill: parent
            clipRadius: 0
            pressed: tap.pressed
            anchor: panel
            active: tap.pressed
            color: panel.Material.rippleColor
        }
        Rectangle {
            id: tint
            objectName: "rowTint"
            anchors.fill: parent
            color: "transparent"
        }
        // the glow's bar, and an error's, at the start edge
        Rectangle {
            id: bar
            objectName: "rowGlowBar"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            width: 3
            radius: 1.5
            color: PhoneTheme.accent
            opacity: 0
        }
        Rectangle {
            objectName: "rowErrorBar"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            width: 3
            radius: 1.5
            visible: row.failed
            color: PhoneTheme.bad
        }
        // the thin line under the row, from the text's start edge
        Rectangle {
            objectName: "rowSeparator"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            height: 1
            color: PhoneTheme.border
        }

        ColumnLayout {
            id: body
            anchors.left: parent.left
            anchors.right: chevron.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 16
            anchors.rightMargin: 4
            spacing: 1

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    objectName: "regName"
                    Layout.alignment: Qt.AlignBaseline
                    horizontalAlignment: Text.AlignLeft
                    Layout.maximumWidth: Math.max(48, body.width - addrText.implicitWidth - valueBox.width - 16)
                    text: row.name
                    color: PhoneTheme.text
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Label {
                    id: addrText
                    objectName: "regAddr"
                    Layout.fillWidth: !(row.compact && row.secondLine)
                    Layout.alignment: Qt.AlignBaseline
                    // left to right, beside the name at its start edge
                    horizontalAlignment: Text.AlignLeft
                    text: row.addr
                    color: PhoneTheme.muted
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                    elide: Text.ElideRight
                }
                // across: the second line's words here, between the address and the value
                SecondLine {
                    objectName: "rowSecondInline"
                    suffix: "Inline"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    visible: row.compact && row.secondLine
                }

                // the value's slots, left to right in every language: the arrow, the value right-aligned (left-aligned
                // in Arabic, by the chevron), the unit
                Item {
                    id: valueBox
                    objectName: "valueBox"
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: true
                    readonly property real unitSlot: Math.max(30, unitText.implicitWidth)
                    readonly property real valueSlot: Math.min(Math.max(108, valueText.implicitWidth), body.width * 0.6)
                    Layout.preferredWidth: 12 + valueSlot + 4 + unitSlot
                    Layout.preferredHeight: valueText.implicitHeight

                    // the glowing change's way: a triangle up or down (a square turned, half of it shown), before
                    // the number so the values stay lined up
                    Item {
                        id: arrow
                        objectName: "glowArrow"
                        x: row.rtl ? 0 : valueText.x + valueText.width - valueText.contentWidth - 12
                        anchors.verticalCenter: valueText.verticalCenter
                        width: 10
                        height: 6
                        clip: true
                        visible: row.direction !== 0
                        opacity: bar.opacity
                        rotation: row.direction < 0 ? 180 : 0
                        Rectangle {
                            width: 7
                            height: 7
                            x: 1.5
                            y: 2.5
                            rotation: 45
                            color: PhoneTheme.accent
                        }
                    }
                    Label {
                        id: valueText
                        objectName: "regValue"
                        x: 12
                        width: valueBox.valueSlot
                        horizontalAlignment: row.rtl ? Text.AlignLeft : Text.AlignRight
                        text: row.value
                        color: row.failed ? PhoneTheme.bad : row.stale ? PhoneTheme.muted : PhoneTheme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        // tabular digits: a column of values reads as one, digit under digit
                        font.features: { "tnum": 1 }
                        elide: Text.ElideRight
                    }
                    Label {
                        id: unitText
                        objectName: "regUnit"
                        x: row.rtl ? valueText.x + Math.min(valueText.contentWidth, valueText.width) + 4
                                   : valueText.x + valueText.width + 4
                        width: valueBox.unitSlot
                        anchors.baseline: valueText.baseline
                        horizontalAlignment: Text.AlignLeft
                        text: row.unit
                        color: PhoneTheme.muted
                        font.pixelSize: 13
                    }
                }
            }

            // the second line, only when there is more to say (upright)
            SecondLine {
                objectName: "rowSecond"
                Layout.fillWidth: true
                visible: row.secondLine && !row.compact
            }
        }

        // the way into the sheet
        PhoneIcon {
            id: chevron
            objectName: "rowChevron"
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            kind: row.rtl ? "back" : "next"
            size: 18
            color: PhoneTheme.muted
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: row.name + " " + row.value + (row.unit.length > 0 ? " " + row.unit : "")
    Accessible.onPressAction: row.opened(row.source)

    TapHandler {
        id: tap
        onTapped: row.opened(row.source)
    }
}

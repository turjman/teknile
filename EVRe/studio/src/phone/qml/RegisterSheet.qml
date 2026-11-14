// SPDX-License-Identifier: Apache-2.0
// A register's sheet, from the screen's foot, opened by a tap on its row: everything about it in one place. Under its
// drag handle and beside its close cross: the register's name, its address and group; its value large with its unit
// (left to right in every language), "not refreshed" and "on the chart" when so, the decoded text (the bit fields, an
// enum's name) or the error in red; a small line of its last values (RegisterList::trend, at most TREND_POINTS) with
// their lowest and highest; Plot (or Take off the chart) and Copy, what they did said under them.
// A writable register then has the desktop's quick write: a field that starts with the value now (the number pad for
// a number; an enum: the list of its values, one chosen), its unit and Write beside it; Enter writes too
// (PhoneSession.write, the desktop's path). A value past the map's limits, or a register that moves something
// (danger), is asked first in a warning box: Write anyway or Cancel. The answer shows under it in its colour: written,
// then what the device holds, read back; or the refusal's text. With no link, or with Allow writes off, the sheet says
// so and offers the drawer.
// The soft keyboard: keyboardLift (Main.qml) is what it covers of the window; the sheet grows by it and keeps its
// content above it, scrolled so that the field and Write show while the field is typed in.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: sheet
    property bool rtl: false
    property real keyboardLift: 0
    property int source: -1
    property var entry: ({})
    property var trend: ({})
    property var choices: []
    property string chosen: ""
    property string question: ""
    property string pending: ""
    property string answer: ""
    property bool answerOk: false
    property bool waiting: false
    // after a write the device answered: the next value read is what it holds
    property bool readBackDue: false
    property bool readBackSeen: false
    // what Plot or Copy did, or why not
    property string done: ""
    property bool doneOk: true
    readonly property bool canWrite: PhoneSession.connected && PhoneSession.writesEnabled
    readonly property bool writable: sheet.entry.writable === true
    readonly property bool failed: (sheet.entry.error || "").length > 0
    // a number typed with digits: the number pad (not hex or binary, which need letters)
    property bool numberPad: false

    signal connectAsked()
    property bool connectAfter: false
    onClosed: if (connectAfter) {
        connectAfter = false
        connectAsked()
    }

    edge: Qt.BottomEdge
    height: Math.min(column.implicitHeight + 44 + keyboardLift, parent ? parent.height - 24 : 600)
    background: Rectangle {
        color: PhoneTheme.surface
        topLeftRadius: 16
        topRightRadius: 16
        border.color: PhoneTheme.border
        // the drag handle: the sheet can be pulled down
        Rectangle {
            objectName: "sheetHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            width: 32
            height: 4
            radius: 2
            color: PhoneTheme.control
        }
    }

    function openFor(source) {
        sheet.source = source
        sheet.refresh()
        sheet.choices = PhoneSession.choices(source)
        const now = PhoneSession.editText(source)
        valueField.text = now
        sheet.chosen = now
        sheet.numberPad = sheet.entry.numeric === true && sheet.choices.length === 0 && !/^\s*0[xXbB]/.test(now)
        sheet.question = ""
        sheet.answer = ""
        sheet.waiting = false
        sheet.readBackDue = false
        sheet.readBackSeen = false
        sheet.done = ""
        flick.contentY = 0
        sheet.open()
    }
    function refresh() {
        sheet.entry = PhoneSession.registerList.entry(sheet.source)
        sheet.trend = PhoneSession.registerList.trend(sheet.source)
        trendCanvas.requestPaint()
    }
    function typed() { return sheet.choices.length > 0 ? sheet.chosen : valueField.text }
    function send(text) {
        sheet.question = ""
        sheet.answer = qsTr("Writing…")
        sheet.waiting = true
        sheet.readBackDue = false
        sheet.readBackSeen = false
        PhoneSession.write(sheet.source, text)
    }
    function apply() {
        const text = sheet.typed()
        const asked = PhoneSession.writeQuestion(sheet.source, text)
        if (asked.length > 0) {
            sheet.pending = text
            sheet.question = asked
            sheet.answer = ""
        } else {
            sheet.send(text)
        }
    }
    function togglePlot() {
        const on = !sheet.entry.plotted
        if (PhoneSession.setPlot(sheet.source, on)) {
            sheet.done = on ? qsTr("%1 on the chart").arg(sheet.entry.name) : qsTr("%1 off the chart").arg(sheet.entry.name)
            sheet.doneOk = true
        }
        sheet.refresh()
    }
    function copy() {
        const copied = PhoneSession.registerList.copy(sheet.source)
        if (copied.length > 0) {
            sheet.done = qsTr("Copied: %1").arg(copied)
            sheet.doneOk = true
        }
    }
    // the field being typed in and Write in the view, above the keyboard: the flick scrolled to them
    function revealField() {
        if (!valueField.activeFocus) return
        const top = column.y + writeRow.mapToItem(column, 0, 0).y - 8
        const bottom = column.y + writeRow.mapToItem(column, 0, writeRow.height).y + 8
        const most = Math.max(0, flick.contentHeight - flick.height)
        let y = flick.contentY
        if (bottom - y > flick.height) y = bottom - flick.height
        if (top < y) y = top
        flick.contentY = Math.max(0, Math.min(y, most))
    }
    onKeyboardLiftChanged: Qt.callLater(revealField)
    onHeightChanged: Qt.callLater(revealField)

    // the value now, as the device sends it; after a written value, the first one read is what the device holds
    Connections {
        target: PhoneSession.registerList
        enabled: sheet.visible
        function onDataChanged(topLeft, bottomRight, roles) {
            const at = PhoneSession.registerList.rowOf(sheet.source)
            if (at < topLeft.row || at > bottomRight.row) return
            sheet.refresh()
            if (sheet.readBackDue && roles.indexOf(RegisterList.ValueRole) >= 0) {
                sheet.readBackDue = false
                sheet.readBackSeen = true
            }
        }
    }
    Connections {
        target: PhoneSession
        function onWriteAnswered(row, ok, text) {
            if (row !== sheet.source) return
            sheet.answer = text
            sheet.answerOk = ok
            sheet.waiting = false
            sheet.readBackDue = ok
        }
        // the chart's limit of lines: why Plot did nothing
        function onNotice(text) {
            if (!sheet.visible) return
            sheet.done = text
            sheet.doneOk = false
        }
    }

    // a small rounded label: "not refreshed", "on the chart"
    component Tag: Rectangle {
        property alias text: tagText.text
        property alias textColor: tagText.color
        property color dot: "transparent"
        property alias textName: tagText.objectName
        implicitWidth: tagRow.implicitWidth + 16
        implicitHeight: 24
        radius: 12
        Row {
            id: tagRow
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
                id: tagText
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }

    // Plot and Copy: an outlined button, its drawn icon before its text, 48 high
    component SheetAction: AbstractButton {
        id: action
        property string glyph: "chart"
        implicitHeight: 48
        implicitWidth: actionRow.implicitWidth + 32
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            y: 4
            width: parent.width
            height: parent.height - 8
            radius: height / 2
            color: action.pressed ? PhoneTheme.surface2 : "transparent"
            border.color: PhoneTheme.control
            Ripple {
                anchors.fill: parent
                clipRadius: parent.radius
                pressed: action.pressed
                anchor: action
                active: action.pressed
                color: action.Material.rippleColor
            }
        }
        contentItem: Item {
            Row {
                id: actionRow
                anchors.centerIn: parent
                spacing: 8
                PhoneIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    kind: action.glyph
                    size: 18
                    color: PhoneTheme.accent
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, action.width - 32 - 26)
                    text: action.text
                    color: PhoneTheme.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }
        }
    }

    Flickable {
        id: flick
        objectName: "sheetFlick"
        anchors.fill: parent
        // the keyboard's part of the sheet stays empty
        anchors.bottomMargin: sheet.keyboardLift
        contentWidth: width
        contentHeight: column.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // a popup is not a child of the window's items: it mirrors by its own
        LayoutMirroring.enabled: sheet.rtl
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            id: column
            // a phone's width in landscape too, in the middle: a sheet 900 wide reads as a stretched line
            width: Math.min(flick.width - 40, 600)
            x: (flick.width - width) / 2
            y: 24
            spacing: 10

            // the name, its address and group; the close cross
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        objectName: "sheetName"
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                        text: sheet.entry.name || ""
                        color: PhoneTheme.text
                        font.pixelSize: 20
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    // the address left to right, then the group
                    Label {
                        objectName: "sheetAddr"
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                        text: (sheet.entry.addr || "") + (sheet.entry.group ? "  ·  " + sheet.entry.group : "")
                        color: PhoneTheme.muted
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }
                ToolButton {
                    objectName: "sheetClose"
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    Layout.alignment: Qt.AlignTop
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Close")
                    onClicked: sheet.close()
                    contentItem: Item {
                        PhoneIcon {
                            anchors.centerIn: parent
                            kind: "clear"
                            size: 20
                            color: PhoneTheme.muted
                        }
                    }
                }
            }

            // the value large and its unit, left to right in every language, at the start edge
            Item {
                Layout.fillWidth: true
                Layout.topMargin: -6
                implicitHeight: valueText.implicitHeight
                Row {
                    objectName: "sheetValueRow"
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: true
                    x: sheet.rtl ? parent.width - width : 0
                    spacing: 8
                    Label {
                        id: valueText
                        objectName: "sheetValue"
                        width: Math.min(implicitWidth, column.width - unitBig.width - 8)
                        text: sheet.entry.value || ""
                        color: sheet.failed ? PhoneTheme.bad : sheet.entry.stale ? PhoneTheme.muted : PhoneTheme.text
                        font.pixelSize: 34
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                        elide: Text.ElideRight
                    }
                    Label {
                        id: unitBig
                        objectName: "sheetValueUnit"
                        anchors.baseline: valueText.baseline
                        visible: text.length > 0
                        text: sheet.entry.unit || ""
                        color: PhoneTheme.muted
                        font.pixelSize: 18
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                visible: sheet.entry.stale === true || sheet.entry.plotted === true
                spacing: 8
                Tag {
                    objectName: "sheetStaleTag"
                    visible: sheet.entry.stale === true
                    color: PhoneTheme.warnSoft
                    text: qsTr("not refreshed")
                    textColor: PhoneTheme.warn
                    textName: "sheetStale"
                }
                Tag {
                    objectName: "sheetPlottedTag"
                    visible: sheet.entry.plotted === true
                    color: PhoneTheme.accentSoft
                    dot: PhoneTheme.accent
                    text: qsTr("on the chart")
                    textColor: PhoneTheme.text
                    textName: "sheetPlotted"
                }
            }
            Label {
                objectName: "sheetDecoded"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                visible: !sheet.failed && text.length > 0
                text: sheet.entry.decoded || ""
                color: PhoneTheme.muted
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "sheetError"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                visible: sheet.failed
                text: sheet.entry.error || ""
                color: PhoneTheme.bad
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            // the last values: a line over the span kept, its highest and lowest at its top and foot at the end, the
            // span under its start. Left to right in every language (time goes that way on the chart too); no text
            // lies over the line
            Rectangle {
                objectName: "sheetTrend"
                Layout.fillWidth: true
                Layout.topMargin: 2
                implicitHeight: 84
                visible: sheet.trend.points !== undefined
                radius: 10
                color: PhoneTheme.surface2
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                readonly property bool flat: sheet.trend.min === sheet.trend.max
                // the line, drawn (no new module): the newest value at the end edge, a dot on it
                Canvas {
                    id: trendCanvas
                    objectName: "sheetTrendLine"
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: Math.max(trendMax.implicitWidth, trendMin.implicitWidth) + 24
                    anchors.topMargin: 14
                    anchors.bottomMargin: 28
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const points = sheet.trend.points
                        if (!points || points.length < 2) return
                        const span = Math.max(sheet.trend.seconds, 1e-6)
                        const low = sheet.trend.min, high = sheet.trend.max
                        const flat = high - low <= 0
                        const xOf = p => (1 + p.x / span) * (width - 4) + 1
                        const yOf = p => flat ? height / 2 : height - (p.y - low) / (high - low) * height
                        ctx.strokeStyle = PhoneTheme.accent
                        ctx.lineWidth = 2
                        ctx.lineJoin = "round"
                        ctx.beginPath()
                        ctx.moveTo(xOf(points[0]), yOf(points[0]))
                        for (let i = 1; i < points.length; ++i) ctx.lineTo(xOf(points[i]), yOf(points[i]))
                        ctx.stroke()
                        const last = points[points.length - 1]
                        ctx.fillStyle = PhoneTheme.accent
                        ctx.beginPath()
                        ctx.arc(xOf(last), yOf(last), 3, 0, 2 * Math.PI)
                        ctx.fill()
                    }
                }
                // the highest at the line's top, the lowest at its foot; one value alone at its middle
                Label {
                    id: trendMax
                    objectName: "sheetTrendMax"
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.flat ? trendCanvas.verticalCenter : trendCanvas.top
                    text: sheet.trend.maxText || ""
                    color: PhoneTheme.muted
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                }
                Label {
                    id: trendMin
                    objectName: "sheetTrendMin"
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: trendCanvas.bottom
                    visible: !parent.flat
                    text: sheet.trend.minText || ""
                    color: PhoneTheme.muted
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                }
                Label {
                    objectName: "sheetTrendSpan"
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 12
                    anchors.bottomMargin: 5
                    text: sheet.trend.seconds !== undefined ? qsTr("last %1 s").arg(Math.max(1, Math.round(sheet.trend.seconds))) : ""
                    color: PhoneTheme.muted
                    font.pixelSize: 11
                }
            }

            // Plot and Copy, side by side
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                SheetAction {
                    objectName: "sheetPlot"
                    Layout.fillWidth: true
                    visible: sheet.entry.plottable === true
                    glyph: "chart"
                    text: sheet.entry.plotted === true ? qsTr("Take off the chart") : qsTr("Plot")
                    onClicked: sheet.togglePlot()
                }
                SheetAction {
                    objectName: "sheetCopy"
                    Layout.fillWidth: true
                    glyph: "copy"
                    text: qsTr("Copy")
                    onClicked: sheet.copy()
                }
            }
            Label {
                objectName: "sheetDone"
                Layout.fillWidth: true
                Layout.topMargin: -4
                horizontalAlignment: Text.AlignLeft
                visible: text.length > 0
                text: sheet.done
                color: sheet.doneOk ? PhoneTheme.muted : PhoneTheme.warn
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            // the write: a line above it, its title and the map's limits
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: sheet.writable
                implicitHeight: 1
                color: PhoneTheme.border
            }
            RowLayout {
                Layout.fillWidth: true
                visible: sheet.writable
                spacing: 8
                Label {
                    objectName: "sheetWriteTitle"
                    horizontalAlignment: Text.AlignLeft
                    text: qsTr("Write")
                    color: PhoneTheme.accent
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "sheetLimits"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    visible: text.length > 0
                    text: {
                        const limits = sheet.source >= 0 ? PhoneSession.limitsText(sheet.source) : ""
                        return limits.length > 0 ? qsTr("The map's limits: %1").arg(limits) : ""
                    }
                    color: PhoneTheme.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }

            // no link, or writes off: why, and the way to the drawer
            Label {
                objectName: "sheetBlocked"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                visible: sheet.writable && !sheet.canWrite
                text: !PhoneSession.connected ? qsTr("Not connected: connect in the Connect drawer first.")
                                              : qsTr("Writes are off: turn on Allow writes in the Connect drawer.")
                color: PhoneTheme.warn
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
            Button {
                objectName: "sheetConnect"
                Layout.fillWidth: true
                visible: sheet.writable && !sheet.canWrite
                text: qsTr("Open the Connect drawer")
                highlighted: true
                Material.accent: PhoneTheme.accentFill
                // the drawer once the sheet is gone: one popup at a time
                onClicked: {
                    sheet.connectAfter = true
                    sheet.close()
                }
            }

            // an enum: its values as a list, one chosen
            Rectangle {
                objectName: "sheetChoices"
                Layout.fillWidth: true
                visible: sheet.writable && sheet.canWrite && sheet.choices.length > 0
                implicitHeight: choiceColumn.implicitHeight + 2
                radius: 12
                color: "transparent"
                border.color: PhoneTheme.border
                clip: true
                Column {
                    id: choiceColumn
                    x: 1
                    y: 1
                    width: parent.width - 2
                    Repeater {
                        model: sheet.choices
                        Rectangle {
                            id: choice
                            required property var modelData
                            required property int index
                            readonly property bool picked: sheet.chosen === modelData.value
                            objectName: "sheetChoice_" + modelData.value
                            width: choiceColumn.width
                            height: 48
                            // the first and the last row rounded as the list's frame: a chosen row's tint stays inside it
                            topLeftRadius: index === 0 ? 11 : 0
                            topRightRadius: index === 0 ? 11 : 0
                            bottomLeftRadius: index === sheet.choices.length - 1 ? 11 : 0
                            bottomRightRadius: index === sheet.choices.length - 1 ? 11 : 0
                            color: choiceTap.pressed ? PhoneTheme.surface2 : picked ? PhoneTheme.accentSoft : "transparent"
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.text
                            Accessible.checked: picked
                            // a radio drawn: a ring, its dot when chosen
                            Rectangle {
                                id: ring
                                anchors.left: parent.left
                                anchors.leftMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                width: 20
                                height: 20
                                radius: 10
                                color: "transparent"
                                border.width: 2
                                border.color: choice.picked ? PhoneTheme.accent : PhoneTheme.control
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10
                                    height: 10
                                    radius: 5
                                    visible: choice.picked
                                    color: PhoneTheme.accent
                                }
                            }
                            Label {
                                anchors.left: ring.right
                                anchors.right: parent.right
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignLeft
                                text: choice.modelData.text
                                color: PhoneTheme.text
                                font.pixelSize: 15
                                font.weight: choice.picked ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                visible: choice.index < sheet.choices.length - 1
                                color: PhoneTheme.border
                            }
                            TapHandler {
                                id: choiceTap
                                onTapped: sheet.chosen = choice.modelData.value
                            }
                        }
                    }
                }
            }
            // the quick write: a number or a text in the field, its unit after it (left to right in every language),
            // Write at the end (the left in Arabic); an enum's Write under its list
            RowLayout {
                id: writeRow
                objectName: "sheetWriteRow"
                Layout.fillWidth: true
                visible: sheet.writable && sheet.canWrite
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    visible: sheet.choices.length === 0
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: true
                    spacing: 10
                    TextField {
                        id: valueField
                        objectName: "sheetField"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        font.pixelSize: 18
                        font.features: { "tnum": 1 }
                        inputMethodHints: sheet.numberPad ? Qt.ImhFormattedNumbersOnly | Qt.ImhNoPredictiveText
                                                          : Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        EnterKey.type: Qt.EnterKeySend
                        onAccepted: if (sheet.question.length === 0) sheet.apply()
                        onActiveFocusChanged: if (activeFocus) Qt.callLater(sheet.revealField)
                    }
                    Label {
                        objectName: "sheetUnit"
                        visible: text.length > 0
                        text: sheet.entry.unit || ""
                        color: PhoneTheme.muted
                        font.pixelSize: 18
                    }
                }
                Item {
                    Layout.fillWidth: true
                    visible: sheet.choices.length > 0
                }
                Button {
                    objectName: "sheetApply"
                    Layout.preferredHeight: 56
                    Layout.minimumWidth: 96
                    enabled: sheet.question.length === 0 && !sheet.waiting
                    Material.accent: PhoneTheme.accentFill
                    highlighted: true
                    text: qsTr("Write")
                    onClicked: sheet.apply()
                }
            }

            // asked first: past the limits, or a register that moves something; a warning, in its colours
            Rectangle {
                objectName: "sheetQuestion"
                Layout.fillWidth: true
                visible: sheet.question.length > 0
                implicitHeight: questionColumn.implicitHeight + 28
                radius: 12
                color: PhoneTheme.warnSoft
                border.color: PhoneTheme.warn
                PhoneIcon {
                    id: warningIcon
                    objectName: "sheetQuestionIcon"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.leftMargin: 14
                    anchors.topMargin: 14
                    kind: "warning"
                    color: PhoneTheme.warn
                    markColor: PhoneTheme.surface
                }
                ColumnLayout {
                    id: questionColumn
                    anchors.left: warningIcon.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 12
                    anchors.rightMargin: 14
                    anchors.topMargin: 14
                    spacing: 6
                    Label {
                        objectName: "sheetQuestionTitle"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Check before writing")
                        color: PhoneTheme.warn
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "sheetQuestionText"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: sheet.question
                        color: PhoneTheme.text
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        spacing: 8
                        Button {
                            objectName: "sheetWriteAnyway"
                            Material.accent: PhoneTheme.accentFill
                            highlighted: true
                            text: qsTr("Write anyway")
                            onClicked: sheet.send(sheet.pending)
                        }
                        Button {
                            objectName: "sheetCancel"
                            flat: true
                            Material.foreground: PhoneTheme.text
                            text: qsTr("Cancel")
                            onClicked: {
                                sheet.question = ""
                                sheet.answer = qsTr("Not written")
                                sheet.answerOk = false
                                sheet.waiting = false
                            }
                        }
                    }
                }
            }

            // the answer, in its colour on its soft tone; once written, what the device holds (read back)
            Rectangle {
                objectName: "sheetAnswerBox"
                Layout.fillWidth: true
                visible: sheet.answer.length > 0
                implicitHeight: answerColumn.implicitHeight + 20
                radius: 10
                color: sheet.waiting ? PhoneTheme.surface2 : sheet.answerOk ? PhoneTheme.goodSoft : PhoneTheme.badSoft
                Rectangle {
                    id: answerDot
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 10 + (answerText.height - height) / 2
                    width: 8
                    height: 8
                    radius: 4
                    color: answerText.color
                }
                ColumnLayout {
                    id: answerColumn
                    anchors.left: answerDot.right
                    anchors.right: parent.right
                    anchors.leftMargin: 10
                    anchors.rightMargin: 14
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    spacing: 2
                    Label {
                        id: answerText
                        objectName: "sheetAnswer"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: sheet.answer
                        color: sheet.waiting ? PhoneTheme.muted : sheet.answerOk ? PhoneTheme.good : PhoneTheme.bad
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                    }
                    Label {
                        objectName: "sheetReadBack"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        visible: sheet.answerOk && sheet.readBackSeen
                        text: qsTr("Read back: %1").arg((sheet.entry.value || "") + (sheet.entry.unit ? " " + sheet.entry.unit : ""))
                        color: PhoneTheme.text
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}

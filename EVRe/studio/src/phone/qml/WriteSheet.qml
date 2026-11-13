// SPDX-License-Identifier: Apache-2.0
// The write sheet, from the screen's foot: one register's value written (PhoneSession.write, the desktop's path). Under
// its drag handle it shows the register and its address, its value now and the map's limits; then a field for the
// value with its unit (an enum: the list of its values, one chosen), Apply (the primary button) and Close. A value
// past the map's limits, or a register that moves something (danger), is asked first in a warning box: Write anyway
// or Cancel. The answer shows under it in its colour: written (the card then shows what the device holds, read back),
// or the refusal's text. With no link, or with Allow writes off, the sheet says so and offers the drawer.
// The soft keyboard: keyboardLift (Main.qml) is what it covers of the window; the sheet grows by it and keeps its
// content above it, scrolled so that the field and Apply show while the field is typed in.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: sheet
    property bool rtl: false
    property real keyboardLift: 0
    property int source: -1
    property var entry: ({})
    property var choices: []
    property string chosen: ""
    property string question: ""
    property string pending: ""
    property string answer: ""
    property bool answerOk: false
    property bool waiting: false
    readonly property bool canWrite: PhoneSession.connected && PhoneSession.writesEnabled

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
        sheet.question = ""
        sheet.answer = ""
        sheet.waiting = false
        flick.contentY = 0
        sheet.open()
    }
    function refresh() { sheet.entry = PhoneSession.registerList.entry(sheet.source) }
    function typed() { return sheet.choices.length > 0 ? sheet.chosen : valueField.text }
    function send(text) {
        sheet.question = ""
        sheet.answer = qsTr("Writing…")
        sheet.waiting = true
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
    // the field being typed in and Apply in the view, above the keyboard: the flick scrolled to them
    function revealField() {
        if (!valueField.activeFocus) return
        const top = column.y + valueRow.mapToItem(column, 0, 0).y - 8
        const bottom = column.y + buttons.mapToItem(column, 0, buttons.height).y + 8
        const most = Math.max(0, flick.contentHeight - flick.height)
        let y = flick.contentY
        if (bottom - y > flick.height) y = bottom - flick.height
        if (top < y) y = top
        flick.contentY = Math.max(0, Math.min(y, most))
    }
    onKeyboardLiftChanged: Qt.callLater(revealField)
    onHeightChanged: Qt.callLater(revealField)

    // the value now, as the device sends it
    Connections {
        target: PhoneSession.registerList
        enabled: sheet.visible
        function onDataChanged() { sheet.refresh() }
    }
    Connections {
        target: PhoneSession
        function onWriteAnswered(row, ok, text) {
            if (row !== sheet.source) return
            sheet.answer = text
            sheet.answerOk = ok
            sheet.waiting = false
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
            x: 20
            y: 28
            width: flick.width - 40
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
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
                Label {
                    objectName: "sheetAddr"
                    text: sheet.entry.addr || ""
                    color: PhoneTheme.muted
                    font.pixelSize: 13
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: -4
                spacing: 8
                Label {
                    objectName: "sheetNowLabel"
                    text: qsTr("Now")
                    color: PhoneTheme.muted
                    font.pixelSize: 14
                }
                // the value and its unit, left to right in every language
                Label {
                    objectName: "sheetNow"
                    Layout.fillWidth: true
                    LayoutMirroring.enabled: false
                    horizontalAlignment: sheet.rtl ? Text.AlignRight : Text.AlignLeft
                    text: (sheet.entry.value || "") + (sheet.entry.unit ? " " + sheet.entry.unit : "")
                    color: sheet.entry.stale ? PhoneTheme.muted : PhoneTheme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }
            Label {
                objectName: "sheetLimits"
                Layout.fillWidth: true
                Layout.topMargin: -6
                horizontalAlignment: Text.AlignLeft
                visible: text.length > 0
                text: {
                    const limits = sheet.source >= 0 ? PhoneSession.limitsText(sheet.source) : ""
                    return limits.length > 0 ? qsTr("The map's limits: %1").arg(limits) : ""
                }
                color: PhoneTheme.muted
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            // no link, or writes off: why, and the way to the drawer
            Label {
                objectName: "sheetBlocked"
                Layout.fillWidth: true
                Layout.topMargin: 4
                horizontalAlignment: Text.AlignLeft
                visible: !sheet.canWrite
                text: !PhoneSession.connected ? qsTr("Not connected: connect in the Connect drawer first.")
                                              : qsTr("Writes are off: turn on Allow writes in the Connect drawer.")
                color: PhoneTheme.warn
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
            Button {
                objectName: "sheetConnect"
                Layout.fillWidth: true
                visible: !sheet.canWrite
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
                Layout.topMargin: 4
                visible: sheet.canWrite && sheet.choices.length > 0
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
            // a number or a text: the field, its unit after it (left to right in every language)
            RowLayout {
                id: valueRow
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: sheet.canWrite && sheet.choices.length === 0
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                spacing: 12
                TextField {
                    id: valueField
                    objectName: "sheetField"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 18
                    inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    onAccepted: sheet.apply()
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

            RowLayout {
                id: buttons
                Layout.fillWidth: true
                spacing: 8
                Button {
                    objectName: "sheetApply"
                    Layout.fillWidth: true
                    visible: sheet.canWrite
                    enabled: sheet.question.length === 0
                    Material.accent: PhoneTheme.accentFill
                    highlighted: true
                    text: qsTr("Apply")
                    onClicked: sheet.apply()
                }
                Button {
                    objectName: "sheetClose"
                    Layout.fillWidth: !sheet.canWrite
                    Layout.minimumWidth: 88
                    flat: true
                    Material.foreground: PhoneTheme.accent
                    text: qsTr("Close")
                    onClicked: sheet.close()
                }
            }
            // the answer, in its colour on its soft tone
            Rectangle {
                objectName: "sheetAnswerBox"
                Layout.fillWidth: true
                visible: sheet.answer.length > 0
                implicitHeight: answerText.implicitHeight + 20
                radius: 10
                color: sheet.waiting ? PhoneTheme.surface2 : sheet.answerOk ? PhoneTheme.goodSoft : PhoneTheme.badSoft
                Rectangle {
                    id: answerDot
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: answerText.color
                }
                Label {
                    id: answerText
                    objectName: "sheetAnswer"
                    anchors.left: answerDot.right
                    anchors.right: parent.right
                    anchors.leftMargin: 10
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: sheet.answer
                    color: sheet.waiting ? PhoneTheme.muted : sheet.answerOk ? PhoneTheme.good : PhoneTheme.bad
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}

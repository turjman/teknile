// SPDX-License-Identifier: Apache-2.0
// The write sheet, from the screen's foot: one register's value written (PhoneSession.write, the desktop's path). It
// shows the register, its value now and the map's limits; then a field for the value (an enum: the list of its
// values), Apply and Close. A value past the map's limits, or a register that moves something (danger), is asked
// first: Write anyway or Cancel. The answer shows under it: written (the card then shows what the device holds, read
// back), or the refusal's text. With no link, or with Allow writes off, the sheet says so and offers the drawer.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: sheet
    property bool rtl: false
    property int source: -1
    property var entry: ({})
    property var choices: []
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
    height: Math.min(column.implicitHeight + 48, parent ? parent.height * 0.9 : 600)
    background: Rectangle {
        color: PhoneTheme.surface
        // the sheet apart from the screen under it
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: PhoneTheme.border
        }
    }

    function openFor(source) {
        sheet.source = source
        sheet.refresh()
        sheet.choices = PhoneSession.choices(source)
        const now = PhoneSession.editText(source)
        valueField.text = now
        choiceBox.currentIndex = Math.max(0, choiceBox.indexOfValue(now))
        sheet.question = ""
        sheet.answer = ""
        sheet.waiting = false
        sheet.open()
    }
    function refresh() { sheet.entry = PhoneSession.registerList.entry(sheet.source) }
    function typed() { return sheet.choices.length > 0 ? String(choiceBox.currentValue) : valueField.text }
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
        contentWidth: width
        contentHeight: column.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // a popup is not a child of the window's items: it mirrors by its own
        LayoutMirroring.enabled: sheet.rtl
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            id: column
            x: 16
            y: 16
            width: flick.width - 32
            spacing: 10

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
                spacing: 8
                Label {
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
                visible: !sheet.canWrite
                text: !PhoneSession.connected ? qsTr("Not connected: connect in the Connect drawer first.")
                                              : qsTr("Writes are off: turn on Allow writes in the Connect drawer.")
                color: PhoneTheme.warn
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
            Button {
                objectName: "sheetConnect"
                visible: !sheet.canWrite
                text: qsTr("Open the Connect drawer")
                // the drawer once the sheet is gone: one popup at a time
                onClicked: {
                    sheet.connectAfter = true
                    sheet.close()
                }
            }

            // the value: an enum's list, or a field
            ComboBox {
                id: choiceBox
                objectName: "sheetChoices"
                Layout.fillWidth: true
                visible: sheet.canWrite && sheet.choices.length > 0
                model: sheet.choices
                textRole: "text"
                valueRole: "value"
            }
            RowLayout {
                Layout.fillWidth: true
                visible: sheet.canWrite && sheet.choices.length === 0
                spacing: 8
                TextField {
                    id: valueField
                    objectName: "sheetField"
                    Layout.fillWidth: true
                    LayoutMirroring.enabled: false
                    horizontalAlignment: Text.AlignLeft
                    inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    onAccepted: sheet.apply()
                }
                Label {
                    objectName: "sheetUnit"
                    visible: text.length > 0
                    text: sheet.entry.unit || ""
                    color: PhoneTheme.muted
                    font.pixelSize: 16
                }
            }

            // asked first: past the limits, or a register that moves something
            Rectangle {
                objectName: "sheetQuestion"
                Layout.fillWidth: true
                visible: sheet.question.length > 0
                implicitHeight: questionColumn.implicitHeight + 24
                radius: 8
                color: PhoneTheme.surface2
                border.color: PhoneTheme.warn
                ColumnLayout {
                    id: questionColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 8
                    Label {
                        objectName: "sheetQuestionText"
                        Layout.fillWidth: true
                        text: sheet.question
                        color: PhoneTheme.text
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
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
                    flat: true
                    text: qsTr("Close")
                    onClicked: sheet.close()
                }
            }
            Label {
                objectName: "sheetAnswer"
                Layout.fillWidth: true
                visible: sheet.answer.length > 0
                text: sheet.answer
                color: sheet.waiting ? PhoneTheme.muted : sheet.answerOk ? PhoneTheme.good : PhoneTheme.bad
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
        }
    }
}

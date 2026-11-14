// SPDX-License-Identifier: Apache-2.0
// A frame's sheet, from the screen's foot, opened by a tap on a frame of the Monitor: the frame field by field as the
// desktop's Dump spells it (FrameLog.decode, through the engine's parser). Under its drag handle and beside its close
// cross: the function's name in its kind's colour, the direction and the time; then a row per field in the wire's order
// (START, SLAVE, FUNCTION, OFFSET, COUNT, DATA, CRC, END: the protocol's words), each its bytes in the monospaced font
// and what they mean (the registers its offset covers named from the map; the CRC checked), and under it the Dump's
// words for that field. The rows read left to right in every language, as the frames do; the words under them in the
// language's direction. Scrolled when the window is low (the Monitor across); Copy puts the bytes on the clipboard.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: sheet
    property bool rtl: false
    property string mono: "monospace"
    property color tone: PhoneTheme.accent
    property color softTone: PhoneTheme.accentSoft
    property string time: ""
    property string direction: ""
    property string bytes: ""
    property string note: ""
    property string type: ""
    property string kind: ""
    property var fields: []
    property bool copied: false

    function openFor(time, direction, bytes, note, type, kind) {
        sheet.time = time
        sheet.direction = direction
        sheet.bytes = bytes
        sheet.note = note
        sheet.type = type
        sheet.kind = kind
        sheet.fields = PhoneSession.frames.decode(bytes, direction)
        sheet.copied = false
        flick.contentY = 0
        sheet.open()
    }

    edge: Qt.BottomEdge
    interactive: opened
    height: Math.min(column.implicitHeight + 24 + topPadding + bottomPadding, parent ? parent.height - 24 : 600)
    // its slide (Material's takes about as long); at once when Android's animations are removed
    enter: Transition {
        NumberAnimation { duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
    }
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }
    background: Rectangle {
        color: PhoneTheme.surface
        topLeftRadius: 16
        topRightRadius: 16
        border.color: PhoneTheme.border
        // the drag handle: the sheet can be pulled down
        Rectangle {
            objectName: "frameSheetHandle"
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            width: 32
            height: 4
            radius: 2
            color: PhoneTheme.control
        }
    }

    Flickable {
        id: flick
        objectName: "frameSheetFlick"
        anchors.fill: parent
        anchors.topMargin: 24
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: column
            width: flick.width
            spacing: 0
            LayoutMirroring.enabled: sheet.rtl
            LayoutMirroring.childrenInherit: true

            // the frame's function, direction and time, and the close cross
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 4
                spacing: 8
                Rectangle {
                    objectName: "frameSheetTypeBox"
                    visible: sheet.type.length > 0
                    implicitWidth: sheetType.implicitWidth + 16
                    implicitHeight: sheetType.implicitHeight + 6
                    radius: 4
                    color: sheet.softTone
                    Text {
                        id: sheetType
                        objectName: "frameSheetType"
                        anchors.centerIn: parent
                        text: sheet.type
                        color: sheet.tone
                        font.family: sheet.mono
                        font.pixelSize: 15
                        font.bold: true
                    }
                }
                Text {
                    objectName: "frameSheetWhen"
                    Layout.fillWidth: true
                    text: sheet.direction + "  " + sheet.time
                    color: PhoneTheme.muted
                    font.family: sheet.mono
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    // the protocol's words and the time, left to right in every language
                    horizontalAlignment: Text.AlignLeft
                    LayoutMirroring.enabled: false
                }
                ToolButton {
                    objectName: "frameSheetClose"
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    Accessible.name: qsTr("Close")
                    onClicked: sheet.close()
                    contentItem: Item {
                        PhoneIcon {
                            anchors.centerIn: parent
                            kind: "clear"
                            color: PhoneTheme.text
                        }
                    }
                }
            }
            Label {
                objectName: "frameSheetNote"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                visible: sheet.note.length > 0
                text: sheet.note
                color: PhoneTheme.muted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            // not a whole EVRe frame: said, the bytes as they came
            Label {
                objectName: "frameSheetNotEvre"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                visible: sheet.fields.length === 0
                text: qsTr("Not one whole EVRe frame: its bytes as they came.")
                color: PhoneTheme.warn
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }

            // a row per field: its name, its bytes and what they mean, then the Dump's words for it
            Repeater {
                model: sheet.fields
                ColumnLayout {
                    id: field
                    required property var modelData
                    required property int index
                    objectName: "frameField"
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    spacing: 2
                    // the fields apart
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        implicitHeight: 1
                        color: PhoneTheme.border
                        opacity: 0.6
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 10
                        LayoutMirroring.enabled: false
                        Text {
                            objectName: "frameFieldName"
                            Layout.preferredWidth: 76
                            Layout.alignment: Qt.AlignTop
                            text: field.modelData.name
                            color: PhoneTheme.muted
                            font.family: sheet.mono
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Text {
                            objectName: "frameFieldBytes"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            text: field.modelData.bytes.length > 0 ? field.modelData.bytes : "–"
                            color: PhoneTheme.text
                            font.family: sheet.mono
                            font.pixelSize: 13
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                        Text {
                            objectName: "frameFieldMeaning"
                            Layout.maximumWidth: column.width * 0.45
                            Layout.alignment: Qt.AlignTop
                            visible: text.length > 0
                            text: field.modelData.meaning
                            color: field.modelData.name === "FUNCTION" ? sheet.tone : PhoneTheme.text
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                    }
                    Label {
                        objectName: "frameFieldWords"
                        Layout.fillWidth: true
                        text: field.modelData.words
                        color: PhoneTheme.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // the bytes to the clipboard, and what that did
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 8
                Button {
                    objectName: "frameSheetCopy"
                    flat: true
                    text: qsTr("Copy the bytes")
                    Material.foreground: PhoneTheme.accent
                    onClicked: {
                        PhoneSession.frames.copyText(sheet.bytes)
                        sheet.copied = true
                    }
                }
                Label {
                    objectName: "frameSheetCopied"
                    Layout.fillWidth: true
                    visible: sheet.copied
                    text: qsTr("Copied")
                    color: PhoneTheme.good
                    font.pixelSize: 13
                }
            }
        }
    }
}

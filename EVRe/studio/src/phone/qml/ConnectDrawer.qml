// SPDX-License-Identifier: Apache-2.0
// The Connect drawer, opened by the header's menu button: the link (B.0: a note of what comes) and, at the bottom,
// the theme and the language. It slides in from the start edge: the left, the right in Arabic.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: drawer
    property bool rtl: false

    edge: rtl ? Qt.RightEdge : Qt.LeftEdge
    background: Rectangle { color: PhoneTheme.surface }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        // a popup is not a child of the window's items: it mirrors by its own
        LayoutMirroring.enabled: drawer.rtl
        LayoutMirroring.childrenInherit: true

        Label {
            objectName: "drawerTitle"
            Layout.fillWidth: true
            text: qsTr("Connect")
            color: PhoneTheme.text
            font.pixelSize: 20
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Label {
            objectName: "drawerNote"
            Layout.fillWidth: true
            text: qsTr("Coming soon: a USB port or a TCP address, the map, and Connect.")
            color: PhoneTheme.muted
            font.pixelSize: 15
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }

        // the settings apart from the link above
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: PhoneTheme.border
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("Dark theme")
                color: PhoneTheme.text
                font.pixelSize: 16
                elide: Text.ElideRight
            }
            Switch {
                objectName: "themeSwitch"
                checked: PhoneTheme.dark
                Accessible.name: qsTr("Dark theme")
                onToggled: PhoneTheme.dark = checked
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Language")
                color: PhoneTheme.text
                font.pixelSize: 16
            }
            ComboBox {
                id: languageBox
                objectName: "languageBox"
                Layout.fillWidth: true
                // each language in its own words, as the desktop's sidebar lists them
                model: [qsTr("System"), "English", "العربية"]
                currentIndex: Math.max(0, PhoneLanguage.codes.indexOf(PhoneLanguage.choice))
                onActivated: index => PhoneLanguage.choice = PhoneLanguage.codes[index]
                // a plain label in place of Material's text field: in Arabic that one's text ran over the box's
                // right edge
                contentItem: Label {
                    objectName: "languageText"
                    leftPadding: 12
                    rightPadding: 12
                    text: languageBox.displayText
                    color: PhoneTheme.text
                    font.pixelSize: 16
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
        Label {
            objectName: "languageNote"
            Layout.fillWidth: true
            visible: PhoneLanguage.pending
            text: qsTr("The language changes when the app is opened again.")
            color: PhoneTheme.warn
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }
    }
}

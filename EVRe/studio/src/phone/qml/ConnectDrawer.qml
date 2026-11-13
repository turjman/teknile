// SPDX-License-Identifier: Apache-2.0
// The Connect drawer, opened by the header's menu button or its pill: the link (a USB device, or a TCP host, port and
// token; the baud rate), the map, Connect / Disconnect with the link's state and the device's ID under it, Reconnect by
// itself, polling and its interval, Allow writes, Record; at the bottom the theme and the language. It slides in from
// the start edge (the left; the right in Arabic) and scrolls when it is taller than the screen (landscape).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: drawer
    property bool rtl: false

    edge: rtl ? Qt.RightEdge : Qt.LeftEdge
    background: Rectangle { color: PhoneTheme.surface }
    onAboutToShow: PhoneSession.refreshPorts()

    // a board plugged in or out shows in the list while the drawer is open on USB
    Timer {
        interval: 2000
        repeat: true
        running: drawer.visible && !PhoneSession.tcp
        onTriggered: PhoneSession.refreshPorts()
    }

    // the state's colour, as the header's pill has it
    readonly property color stateColor: {
        switch (PhoneSession.state) {
        case PhoneSession.Connected: return PhoneTheme.good
        case PhoneSession.Closed: return PhoneTheme.bad
        case PhoneSession.Off: return PhoneTheme.text
        default: return PhoneTheme.warn
        }
    }
    readonly property bool linkBusy: PhoneSession.wantConnected

    // a title over a control: muted, small
    component Caption: Label {
        Layout.fillWidth: true
        color: PhoneTheme.muted
        font.pixelSize: 13
        elide: Text.ElideRight
    }
    // a line of text that wraps
    component Note: Label {
        Layout.fillWidth: true
        font.pixelSize: 14
        wrapMode: Text.Wrap
    }
    // a setting's name and its switch
    component SwitchRow: RowLayout {
        property alias text: name.text
        property alias checked: toggle.checked
        property alias switchName: toggle.objectName
        signal toggled(bool on)
        Layout.fillWidth: true
        spacing: 8
        Label {
            id: name
            Layout.fillWidth: true
            color: PhoneTheme.text
            font.pixelSize: 16
            elide: Text.ElideRight
        }
        Switch {
            id: toggle
            Accessible.name: name.text
            onToggled: parent.toggled(checked)
        }
    }
    // a line between groups
    component Separator: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: PhoneTheme.border
    }

    FileDialog {
        id: mapDialog
        title: qsTr("Open map")
        nameFilters: [qsTr("Maps (*.json)")]
        onAccepted: PhoneSession.loadMap(selectedFile.toString())
    }

    Flickable {
        id: flick
        objectName: "drawerFlick"
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollIndicator.vertical: ScrollIndicator {}
        // a popup is not a child of the window's items: it mirrors by its own
        LayoutMirroring.enabled: drawer.rtl
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            id: column
            x: 16
            y: 16
            width: flick.width - 32
            // at least the drawer's height: the theme and the language stay at its bottom
            height: Math.max(implicitHeight, flick.height - 32)
            spacing: 10

            Label {
                objectName: "drawerTitle"
                Layout.fillWidth: true
                text: qsTr("Connect")
                color: PhoneTheme.text
                font.pixelSize: 20
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            // USB or TCP: two buttons, one of them chosen; while a link is wanted only the chosen one stays lit
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    objectName: "linkUsb"
                    Layout.fillWidth: true
                    // white on the accent's fill (5.2:1); on the lighter accent it read 3:1
                    Material.accent: PhoneTheme.accentFill
                    text: qsTr("USB")
                    // lit, not checkable: Material's checked look is a lighter fill, too light under white
                    highlighted: !PhoneSession.tcp
                    enabled: highlighted || !drawer.linkBusy
                    onClicked: PhoneSession.tcp = false
                }
                Button {
                    objectName: "linkTcp"
                    Layout.fillWidth: true
                    Material.accent: PhoneTheme.accentFill
                    text: qsTr("TCP")
                    highlighted: PhoneSession.tcp
                    enabled: highlighted || !drawer.linkBusy
                    onClicked: PhoneSession.tcp = true
                }
            }

            // USB: the devices plugged in, the baud rate
            ColumnLayout {
                objectName: "usbPanel"
                Layout.fillWidth: true
                visible: !PhoneSession.tcp
                enabled: !drawer.linkBusy
                spacing: 6

                Caption { text: qsTr("USB device") }
                ComboBox {
                    id: portBox
                    objectName: "portBox"
                    Layout.fillWidth: true
                    visible: PhoneSession.ports.count > 0
                    model: PhoneSession.ports
                    textRole: "label"
                    valueRole: "name"
                    // again when the list changes (its count read for that): the device chosen last, else the first
                    currentIndex: PhoneSession.ports.count > 0
                                  ? Math.max(0, PhoneSession.ports.indexOf(PhoneSession.usbDevice)) : -1
                    // a plain label: the device's name and IDs, left to right, cut at the end
                    contentItem: Label {
                        objectName: "portText"
                        leftPadding: 12
                        rightPadding: 12
                        text: portBox.displayText
                        color: PhoneTheme.text
                        font.pixelSize: 15
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                Note {
                    objectName: "noPorts"
                    visible: PhoneSession.ports.count === 0
                    text: qsTr("No USB device found: plug the board in (through a USB OTG adapter).")
                    color: PhoneTheme.muted
                }
                Caption { text: qsTr("Baud rate") }
                ComboBox {
                    id: baudBox
                    objectName: "baudBox"
                    Layout.fillWidth: true
                    model: PhoneSession.baudRates
                    currentIndex: Math.max(0, PhoneSession.baudRates.indexOf(PhoneSession.baud))
                    contentItem: Label {
                        leftPadding: 12
                        rightPadding: 12
                        text: baudBox.displayText
                        color: PhoneTheme.text
                        font.pixelSize: 15
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Note {
                    text: qsTr("A USB CDC board ignores the baud rate.")
                    color: PhoneTheme.muted
                    font.pixelSize: 13
                }
            }

            // TCP: the host and its port, the token
            ColumnLayout {
                objectName: "tcpPanel"
                Layout.fillWidth: true
                visible: PhoneSession.tcp
                enabled: !drawer.linkBusy
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Caption { text: qsTr("Host") }
                        TextField {
                            id: hostField
                            objectName: "hostField"
                            Layout.fillWidth: true
                            text: PhoneSession.host
                            inputMethodHints: Qt.ImhUrlCharacters | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 96
                        spacing: 6
                        Caption { text: qsTr("Port") }
                        TextField {
                            id: portField
                            objectName: "portField"
                            Layout.fillWidth: true
                            text: PhoneSession.port
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                    }
                }
                Caption { text: qsTr("Token (not stored)") }
                TextField {
                    id: tokenField
                    objectName: "tokenField"
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                }
            }

            // the map: one of the app's, or a file
            Caption { text: qsTr("Map") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ComboBox {
                    id: mapBox
                    objectName: "mapBox"
                    Layout.fillWidth: true
                    model: PhoneSession.mapNames
                    currentIndex: PhoneSession.mapFiles.indexOf(PhoneSession.mapFile)
                    displayText: PhoneSession.mapName
                    onActivated: index => PhoneSession.loadMap(PhoneSession.mapFiles[index])
                    contentItem: Label {
                        objectName: "mapText"
                        leftPadding: 12
                        rightPadding: 12
                        text: mapBox.displayText
                        color: PhoneTheme.text
                        font.pixelSize: 15
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
                Button {
                    objectName: "openMap"
                    text: qsTr("Open…")
                    flat: true
                    onClicked: mapDialog.open()
                }
            }
            Note {
                objectName: "mapInfo"
                text: PhoneSession.mapText
                color: PhoneTheme.muted
                font.pixelSize: 13
            }
            Note {
                objectName: "mapError"
                visible: PhoneSession.mapError.length > 0
                text: PhoneSession.mapError
                color: PhoneTheme.bad
                font.pixelSize: 13
            }

            Button {
                id: connectButton
                objectName: "connectButton"
                Layout.fillWidth: true
                highlighted: true
                Material.accent: PhoneTheme.accentFill
                text: PhoneSession.connectText
                enabled: drawer.linkBusy || PhoneSession.state === PhoneSession.Connected || PhoneSession.tcp
                         || PhoneSession.ports.count > 0
                onClicked: {
                    if (PhoneSession.wantConnected || PhoneSession.state === PhoneSession.Connected)
                        PhoneSession.disconnectLink()
                    else if (PhoneSession.tcp)
                        PhoneSession.connectTcp(hostField.text, parseInt(portField.text), tokenField.text)
                    else
                        PhoneSession.connectUsb(portBox.currentValue, PhoneSession.baudRates[baudBox.currentIndex])
                }
            }
            // what the link is, or why it is not; then the device's ID
            Note {
                objectName: "linkText"
                text: PhoneSession.linkText
                color: drawer.stateColor
            }
            Note {
                objectName: "linkDetail"
                visible: text.length > 0
                text: PhoneSession.linkDetail
                color: PhoneTheme.muted
                font.pixelSize: 13
            }
            Note {
                objectName: "deviceText"
                text: PhoneSession.deviceText
                color: PhoneTheme.muted
            }
            Note {
                objectName: "deviceWarning"
                visible: text.length > 0
                text: PhoneSession.deviceWarning
                color: PhoneTheme.warn
                font.pixelSize: 13
            }
            SwitchRow {
                text: qsTr("Reconnect by itself")
                switchName: "reconnectSwitch"
                checked: PhoneSession.autoReconnect
                onToggled: on => PhoneSession.autoReconnect = on
            }

            Separator {}
            SwitchRow {
                text: qsTr("Polling")
                switchName: "pollSwitch"
                checked: PhoneSession.polling
                onToggled: on => PhoneSession.polling = on
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Interval (ms)")
                    color: PhoneTheme.text
                    font.pixelSize: 16
                    elide: Text.ElideRight
                }
                SpinBox {
                    id: intervalBox
                    objectName: "intervalBox"
                    from: 0
                    to: 60000
                    stepSize: 10
                    editable: true
                    value: PhoneSession.intervalMs
                    Accessible.name: qsTr("Interval (ms)")
                    onValueModified: PhoneSession.intervalMs = value
                    // milliseconds stay left to right, as every number
                    LayoutMirroring.enabled: false
                }
            }
            Note {
                objectName: "pollText"
                text: PhoneSession.pollText.length > 0 ? PhoneSession.pollText : qsTr("0 ms: as fast as the device answers")
                color: PhoneTheme.muted
                font.pixelSize: 13
            }
            SwitchRow {
                text: qsTr("Allow writes")
                switchName: "writesSwitch"
                checked: PhoneSession.writesEnabled
                onToggled: on => PhoneSession.writesEnabled = on
            }

            Separator {}
            Button {
                objectName: "recordButton"
                Layout.fillWidth: true
                text: PhoneSession.recording ? qsTr("Stop recording") : qsTr("Record")
                onClicked: PhoneSession.recording ? PhoneSession.stopRecord() : PhoneSession.startRecord()
            }
            Note {
                objectName: "recordText"
                text: PhoneSession.recordText.length > 0 ? PhoneSession.recordText
                                                         : qsTr("A CSV of the logged registers, in the app's folder.")
                color: PhoneTheme.muted
                font.pixelSize: 13
            }

            Item { Layout.fillHeight: true }

            // the settings apart from the link above
            Separator {}
            SwitchRow {
                text: qsTr("Dark theme")
                switchName: "themeSwitch"
                checked: PhoneTheme.dark
                onToggled: on => PhoneTheme.dark = on
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
            Note {
                objectName: "languageNote"
                visible: PhoneLanguage.pending
                text: qsTr("The language changes when the app is opened again.")
                color: PhoneTheme.warn
            }
        }
    }
}

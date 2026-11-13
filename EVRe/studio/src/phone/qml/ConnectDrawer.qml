// SPDX-License-Identifier: Apache-2.0
// The Connect drawer, opened by the header's menu button or its pill, in sections under headers. Link: USB or TCP (a
// USB device and the baud rate, or a host, port and token), Connect / Disconnect (the primary button), the link's
// state by a dot in its colour, Reconnect by itself. Device: the map, the device's ID, Allow writes. Polling: on or
// off and its interval. Recording: Record. Display: the theme and the language, at the bottom. It slides in from the
// start edge (the left; the right in Arabic) and scrolls when it is taller than the screen (landscape); a field typed
// in is scrolled above the soft keyboard (keyboardLift, Main.qml).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import EVRe.Phone

Drawer {
    id: drawer
    property bool rtl: false
    property real keyboardLift: 0

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
    readonly property bool linkUp: PhoneSession.wantConnected || PhoneSession.state === PhoneSession.Connected

    // the field typed in, scrolled into the view above the keyboard
    property Item typing: null
    function reveal() {
        const item = drawer.typing
        if (!item || !item.activeFocus) return
        const top = item.mapToItem(flick.contentItem, 0, 0).y - 32
        const bottom = item.mapToItem(flick.contentItem, 0, item.height).y + 16
        const most = Math.max(0, flick.contentHeight - flick.height)
        let y = flick.contentY
        if (bottom - y > flick.height) y = bottom - flick.height
        if (top < y) y = top
        flick.contentY = Math.max(0, Math.min(y, most))
    }
    onKeyboardLiftChanged: Qt.callLater(reveal)

    // a section's header: its name in the accent, a line over it (none over the first)
    component Section: ColumnLayout {
        property alias text: sectionName.text
        property bool first: false
        Layout.fillWidth: true
        Layout.topMargin: first ? 0 : 12
        spacing: 12
        Rectangle {
            Layout.fillWidth: true
            visible: !parent.first
            implicitHeight: 1
            color: PhoneTheme.border
        }
        Label {
            id: sectionName
            objectName: "drawerSection"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            color: PhoneTheme.accent
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 0.4
            elide: Text.ElideRight
        }
    }
    // a title over a control: muted, small
    component Caption: Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        color: PhoneTheme.muted
        font.pixelSize: 13
        elide: Text.ElideRight
    }
    // a line of text that wraps
    component Note: Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        font.pixelSize: 14
        wrapMode: Text.Wrap
    }
    // a setting's name and its switch at the end edge, the name a target too (48 high)
    component SwitchRow: RowLayout {
        id: switchRow
        property alias text: name.text
        property alias checked: toggle.checked
        property alias switchName: toggle.objectName
        signal toggled(bool on)
        Layout.fillWidth: true
        spacing: 8
        Label {
            id: name
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            color: PhoneTheme.text
            font.pixelSize: 16
            elide: Text.ElideRight
            TapHandler {
                onTapped: {
                    toggle.toggle()
                    switchRow.toggled(toggle.checked)
                }
            }
        }
        Switch {
            id: toggle
            Layout.rightMargin: -6
            Accessible.name: name.text
            onToggled: switchRow.toggled(checked)
        }
    }
    // a half of the USB / TCP choice: the chosen one lit (not checkable: Material's checked look is a lighter fill, too
    // light under white), its text white on the accent's fill (5.2:1) in both themes, drawn by a label of its own
    // (Material's highlighted text came out black on blue); while a link is wanted only the chosen half stays lit
    component SegmentButton: Button {
        id: segment
        Layout.fillWidth: true
        Layout.fillHeight: true
        topInset: 3
        bottomInset: 3
        leftInset: 3
        rightInset: 3
        Material.accent: PhoneTheme.accentFill
        flat: !highlighted
        enabled: highlighted || !drawer.linkBusy
        contentItem: Label {
            objectName: "segmentText"
            text: segment.text
            color: segment.highlighted ? "white" : segment.enabled ? PhoneTheme.text : PhoneTheme.muted
            font.pixelSize: 15
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    // a field's way into the view above the keyboard
    component Field: TextField {
        id: field
        Layout.fillWidth: true
        onActiveFocusChanged: if (activeFocus) {
            drawer.typing = field
            Qt.callLater(drawer.reveal)
        }
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
        // the keyboard's part of the drawer stays empty
        anchors.bottomMargin: drawer.keyboardLift
        contentWidth: width
        contentHeight: column.height + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollIndicator.vertical: ScrollIndicator {}
        // a popup is not a child of the window's items: it mirrors by its own
        LayoutMirroring.enabled: drawer.rtl
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            id: column
            x: 20
            y: 20
            width: flick.width - 40
            // at least the drawer's height: the display's settings stay at its bottom
            height: Math.max(implicitHeight, flick.height - 40)
            spacing: 8

            Label {
                objectName: "drawerTitle"
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                horizontalAlignment: Text.AlignLeft
                text: qsTr("Connect")
                color: PhoneTheme.text
                font.pixelSize: 22
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Section { text: qsTr("Link"); first: true }

            // USB or TCP: a segmented choice, the chosen half filled; while a link is wanted only that one stays lit
            Rectangle {
                objectName: "linkChoice"
                Layout.fillWidth: true
                implicitHeight: 48
                radius: 24
                color: "transparent"
                border.color: PhoneTheme.control
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    SegmentButton {
                        objectName: "linkUsb"
                        text: qsTr("USB")
                        highlighted: !PhoneSession.tcp
                        onClicked: PhoneSession.tcp = false
                    }
                    SegmentButton {
                        objectName: "linkTcp"
                        text: qsTr("TCP")
                        highlighted: PhoneSession.tcp
                        onClicked: PhoneSession.tcp = true
                    }
                }
            }

            // USB: the devices plugged in, the baud rate
            ColumnLayout {
                objectName: "usbPanel"
                Layout.fillWidth: true
                Layout.topMargin: 4
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
                        horizontalAlignment: Text.AlignLeft
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
                Caption {
                    Layout.topMargin: 4
                    text: qsTr("Baud rate")
                }
                ComboBox {
                    id: baudBox
                    objectName: "baudBox"
                    Layout.fillWidth: true
                    model: PhoneSession.baudRates
                    currentIndex: Math.max(0, PhoneSession.baudRates.indexOf(PhoneSession.baud))
                    contentItem: Label {
                        leftPadding: 12
                        rightPadding: 12
                        horizontalAlignment: Text.AlignLeft
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
                Layout.topMargin: 4
                visible: PhoneSession.tcp
                enabled: !drawer.linkBusy
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Caption { text: qsTr("Host") }
                        Field {
                            id: hostField
                            objectName: "hostField"
                            text: PhoneSession.host
                            inputMethodHints: Qt.ImhUrlCharacters | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: 96
                        spacing: 6
                        Caption { text: qsTr("Port") }
                        Field {
                            id: portField
                            objectName: "portField"
                            text: PhoneSession.port
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 65535 }
                        }
                    }
                }
                Caption {
                    Layout.topMargin: 4
                    text: qsTr("Token (not stored)")
                }
                Field {
                    id: tokenField
                    objectName: "tokenField"
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData
                }
            }

            // the primary button: Connect, or Disconnect (Cancel, Stop reconnecting) in the danger's fill
            Button {
                id: connectButton
                objectName: "connectButton"
                Layout.fillWidth: true
                Layout.topMargin: 8
                implicitHeight: 52
                highlighted: true
                Material.accent: drawer.linkUp ? PhoneTheme.badFill : PhoneTheme.accentFill
                text: PhoneSession.connectText
                font.pixelSize: 15
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
            // what the link is, or why it is not, by a dot in the state's colour
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 10
                Rectangle {
                    objectName: "linkDot"
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 6
                    width: 10
                    height: 10
                    radius: 5
                    color: PhoneSession.state === PhoneSession.Off ? PhoneTheme.muted : drawer.stateColor
                }
                Note {
                    objectName: "linkText"
                    text: PhoneSession.linkText
                    color: drawer.stateColor
                    font.weight: Font.Medium
                }
            }
            Note {
                objectName: "linkDetail"
                Layout.leftMargin: 20
                visible: text.length > 0
                text: PhoneSession.linkDetail
                color: PhoneTheme.muted
                font.pixelSize: 13
            }
            SwitchRow {
                text: qsTr("Reconnect by itself")
                switchName: "reconnectSwitch"
                checked: PhoneSession.autoReconnect
                onToggled: on => PhoneSession.autoReconnect = on
            }

            Section { text: qsTr("Device") }
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
                        horizontalAlignment: Text.AlignLeft
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
                    Material.foreground: PhoneTheme.accent
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
            Note {
                objectName: "deviceText"
                Layout.topMargin: 4
                text: PhoneSession.deviceText
                color: PhoneTheme.text
            }
            Note {
                objectName: "deviceWarning"
                visible: text.length > 0
                text: PhoneSession.deviceWarning
                color: PhoneTheme.warn
                font.pixelSize: 13
            }
            SwitchRow {
                text: qsTr("Allow writes")
                switchName: "writesSwitch"
                checked: PhoneSession.writesEnabled
                onToggled: on => PhoneSession.writesEnabled = on
            }

            Section { text: qsTr("Polling") }
            SwitchRow {
                Layout.topMargin: -8
                text: qsTr("Poll the registers")
                switchName: "pollSwitch"
                checked: PhoneSession.polling
                onToggled: on => PhoneSession.polling = on
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
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

            Section { text: qsTr("Recording") }
            Button {
                objectName: "recordButton"
                Layout.fillWidth: true
                text: PhoneSession.recording ? qsTr("Stop recording") : qsTr("Record")
                flat: true
                Material.foreground: PhoneSession.recording ? PhoneTheme.bad : PhoneTheme.accent
                // an outlined button: the second kind of action, under the primary Connect
                background: Rectangle {
                    implicitHeight: 40
                    y: (parent.height - height) / 2
                    height: 40
                    radius: 20
                    color: parent.down ? PhoneTheme.surface2 : "transparent"
                    border.color: PhoneTheme.control
                }
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

            Section { text: qsTr("Display") }
            SwitchRow {
                Layout.topMargin: -8
                text: qsTr("Dark theme")
                switchName: "themeSwitch"
                checked: PhoneTheme.dark
                onToggled: on => PhoneTheme.dark = on
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Label {
                    horizontalAlignment: Text.AlignLeft
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
                        horizontalAlignment: Text.AlignLeft
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

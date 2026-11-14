// SPDX-License-Identifier: Apache-2.0
// The Registers screen: every register of the map as a row of the desktop's table (RegisterRow: the name and address,
// the value lined up with the others, its unit), in sections by group (the group's header stays at the top while its
// rows scroll under it), the values live while the link is up (PhoneSession.registerList, RegisterModel's values copied
// once a frame). A tap on a row opens the register's sheet (Main.qml: its value, trend, Plot, Copy and Write). A search
// field at the top (the desktop's search: name, description, group, unit or address), its magnifier and its clear cross
// drawn; how many registers show, quietly at the list's foot. While no link is up a slim strip at the top says so, with
// a button to the Connect drawer; with no map, or a search that finds nothing, the screen says so with one button (the
// drawer, or clearing the search). What a sheet's Plot or Copy did shows a moment at the bottom too.
// With perf (phone_app.h) a bar runs at the top, so the window draws every frame and the speed line times frames.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    property bool perf: false
    readonly property var registers: PhoneSession.registerList
    // in landscape the cards keep a phone's width, in the middle: a card 900 wide reads as a stretched line
    readonly property real columnWidth: 640

    signal connectAsked()
    signal sheetAsked(int source)

    function showNotice(text) {
        noticeText.text = text
        notice.opacity = 1
        noticeTimer.restart()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // perf: frames drawn all the time, not only when something moves
        Item {
            id: runnerTrack
            objectName: "perfRunner"
            Layout.fillWidth: true
            visible: screen.perf
            implicitHeight: 3
            clip: true
            Rectangle {
                width: 48
                height: 3
                color: PhoneTheme.accent
                NumberAnimation on x {
                    running: screen.perf
                    from: -48
                    to: runnerTrack.width
                    duration: 1500
                    loops: Animation.Infinite
                }
            }
        }

        // the search field: a rounded bar, the magnifier at its start, the clear cross at its end while it holds text
        Rectangle {
            id: searchBox
            objectName: "searchBox"
            Layout.fillWidth: true
            Layout.maximumWidth: screen.columnWidth - 24
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            implicitHeight: 48
            radius: 24
            color: PhoneTheme.surface2
            border.color: searchField.activeFocus ? PhoneTheme.accent : PhoneTheme.border
            border.width: searchField.activeFocus ? 2 : 1

            PhoneIcon {
                objectName: "searchIcon"
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                kind: "search"
                size: 22
                color: PhoneTheme.muted
            }
            TextInput {
                id: searchField
                objectName: "registerSearch"
                anchors.left: parent.left
                anchors.right: clearButton.visible ? clearButton.left : parent.right
                anchors.leftMargin: 48
                anchors.rightMargin: clearButton.visible ? 0 : 16
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignLeft
                color: PhoneTheme.text
                selectionColor: PhoneTheme.accentFill
                selectedTextColor: "white"
                font.pixelSize: 16
                clip: true
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onTextChanged: screen.registers.search = text
                Accessible.role: Accessible.EditableText
                Accessible.name: placeholder.text
            }
            Label {
                id: placeholder
                objectName: "searchPlaceholder"
                anchors.left: searchField.left
                anchors.right: searchField.right
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignLeft
                visible: searchField.text.length === 0
                text: qsTr("Search: name, unit, address")
                color: PhoneTheme.muted
                font.pixelSize: 16
                elide: Text.ElideRight
            }
            // a cross drawn, not a character: a phone's font may not have one
            ToolButton {
                id: clearButton
                objectName: "searchClear"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                visible: searchField.text.length > 0
                Accessible.name: qsTr("Clear")
                onClicked: searchField.text = ""
                contentItem: Item {
                    PhoneIcon {
                        objectName: "clearIcon"
                        anchors.centerIn: parent
                        kind: "clear"
                        size: 20
                        color: PhoneTheme.muted
                    }
                }
            }
        }

        // no link: a slim strip over the list saying so, and the way to the drawer
        Rectangle {
            id: offline
            objectName: "registersOffline"
            Layout.fillWidth: true
            Layout.maximumWidth: screen.columnWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            implicitHeight: Math.max(56, offlineRow.implicitHeight + 12)
            visible: !PhoneSession.connected && screen.registers.total > 0
            color: PhoneTheme.surface
            // apart from the rows under it: a line at its top and its foot
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: PhoneTheme.border
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: PhoneTheme.border
            }
            RowLayout {
                id: offlineRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                spacing: 12
                Rectangle {
                    objectName: "offlineDot"
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: PhoneTheme.muted
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        objectName: "offlineTitle"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Not connected")
                        color: PhoneTheme.text
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "offlineText"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("The values show once the link is up.")
                        color: PhoneTheme.muted
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }
                Button {
                    objectName: "registersConnect"
                    Layout.preferredHeight: 48
                    text: qsTr("Connect…")
                    highlighted: true
                    Material.accent: PhoneTheme.accentFill
                    onClicked: screen.connectAsked()
                }
            }
        }

        ListView {
            id: list
            objectName: "registerList"
            Layout.fillWidth: true
            Layout.maximumWidth: screen.columnWidth
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            clip: true
            model: screen.registers
            reuseItems: true
            boundsBehavior: Flickable.DragOverBounds
            ScrollIndicator.vertical: ScrollIndicator {}

            section.property: "group"
            section.criteria: ViewSection.FullString
            // the group's name stays at the top while its rows pass under it: on the window's background, the rows
            // on the surface, so each group reads as a panel of its own
            section.labelPositioning: ViewSection.InlineLabels | ViewSection.CurrentLabelAtStart
            section.delegate: Rectangle {
                required property string section
                width: ListView.view.width
                height: section.length > 0 ? 34 : 4
                color: PhoneTheme.bg
                Label {
                    objectName: "regGroup"
                    horizontalAlignment: Text.AlignLeft
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.bottomMargin: 6
                    text: parent.section
                    color: PhoneTheme.accent
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.letterSpacing: 0.4
                    elide: Text.ElideRight
                }
            }

            delegate: RegisterRow {
                width: ListView.view.width
                // the list takes the focus from the search field first: a closed sheet gives it back to what had it,
                // and the field's keyboard came up again over the list
                onOpened: source => {
                    if (searchField.activeFocus) list.forceActiveFocus()
                    screen.sheetAsked(source)
                }
            }

            // how many registers show, quietly at the list's foot
            footer: Item {
                width: ListView.view.width
                height: screen.registers.count > 0 ? 56 : 0
                Label {
                    objectName: "registerCount"
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, parent.width - 32)
                    visible: screen.registers.count > 0
                    text: screen.registers.count === screen.registers.total
                          ? qsTr("registers: %1").arg(screen.registers.total)
                          : qsTr("registers: %1 of %2").arg(screen.registers.count).arg(screen.registers.total)
                    color: PhoneTheme.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }

    // nothing to show: no map (the way to the drawer), or a search that matches nothing (the way back to all)
    EmptyState {
        objectName: "registersEmptyState"
        prefix: "registersEmpty"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        visible: screen.registers.count === 0
        icon: screen.registers.total === 0 ? "registers" : "search"
        title: screen.registers.total === 0 ? qsTr("No registers") : qsTr("No match")
        text: screen.registers.total === 0 ? qsTr("Choose a map in the Connect drawer.")
                                           : qsTr("No register matches \"%1\".").arg(screen.registers.search)
        action: screen.registers.total === 0 ? qsTr("Open the Connect drawer") : qsTr("Clear the search")
        onActed: {
            if (screen.registers.total === 0) screen.connectAsked()
            else searchField.text = ""
        }
    }

    // what a sheet's Plot or Copy did, a moment over the list's foot (a snackbar: the text colour's panel, the surface's text)
    Rectangle {
        id: notice
        objectName: "registerNotice"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(parent.width - 32, noticeText.implicitWidth + 32)
        height: noticeText.implicitHeight + 28
        radius: 8
        color: PhoneTheme.text
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Label {
            id: noticeText
            objectName: "registerNoticeText"
            anchors.fill: parent
            anchors.margins: 14
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            color: PhoneTheme.surface
            font.pixelSize: 14
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }
        Timer {
            id: noticeTimer
            interval: 4000
            onTriggered: notice.opacity = 0
        }
    }
    Connections {
        target: PhoneSession
        function onNotice(text) { screen.showNotice(text) }
    }
}

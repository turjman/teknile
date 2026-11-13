// SPDX-License-Identifier: Apache-2.0
// The Registers screen: every register of the map as a card (RegisterCard), in sections by group (the group's header
// stays at the top while its cards scroll under it), the values live while the link is up (PhoneSession.registerList,
// RegisterModel's values copied once a frame). A search field at the top (the desktop's search: name, description,
// group, unit or address), its magnifier and its clear cross drawn; how many registers show, quietly at the list's
// foot. While no link is up a card at the top says how to connect, with a button to the Connect drawer; with no map,
// the screen says so with the same button. The "⋯" button's menu: Plot (or Take off the chart) and Copy; what they
// did, or why not (the chart's limit of lines), shows a moment at the bottom.
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
    signal writeAsked(int source)

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
            // at the top until a finger moves it: the card over the list comes and goes with the link, and the list
            // keeps showing it whole (ListView leaves its view where it was when the header's height changes)
            property bool keepTop: true
            onMovementEnded: keepTop = atYBeginning

            // no link: how to connect, and the way to the drawer
            header: Item {
                width: ListView.view.width
                height: offline.visible ? offline.height + 12 : 0
                onHeightChanged: if (list.keepTop) Qt.callLater(list.positionViewAtBeginning)
                Rectangle {
                    id: offline
                    objectName: "registersOffline"
                    x: 12
                    y: 8
                    width: parent.width - 24
                    height: offlineColumn.implicitHeight + 32
                    visible: !PhoneSession.connected && screen.registers.total > 0
                    radius: 12
                    color: PhoneTheme.surface
                    border.color: PhoneTheme.border
                    ColumnLayout {
                        id: offlineColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 16
                        spacing: 4
                        Label {
                            objectName: "offlineTitle"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Not connected")
                            color: PhoneTheme.text
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Label {
                            objectName: "offlineText"
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Connect over USB or TCP in the Connect drawer: the values show once the link is up.")
                            color: PhoneTheme.muted
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }
                        Button {
                            objectName: "registersConnect"
                            Layout.alignment: Qt.AlignRight
                            Layout.topMargin: 4
                            text: qsTr("Connect…")
                            highlighted: true
                            Material.accent: PhoneTheme.accentFill
                            onClicked: screen.connectAsked()
                        }
                    }
                }
            }

            section.property: "group"
            section.criteria: ViewSection.FullString
            // the group's name stays at the top while its cards pass under it: on the window's background
            section.labelPositioning: ViewSection.InlineLabels | ViewSection.CurrentLabelAtStart
            section.delegate: Rectangle {
                required property string section
                width: ListView.view.width
                height: section.length > 0 ? 40 : 4
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

            delegate: RegisterCard {
                width: ListView.view.width
                onWriteAsked: source => screen.writeAsked(source)
                onMoreAsked: (source, from) => moreMenu.openFor(source, from)
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

    // nothing to show: no map, or a search that matches nothing
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        visible: screen.registers.count === 0
        spacing: 16
        Label {
            objectName: "registersEmpty"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: screen.registers.total === 0 ? qsTr("No registers: choose a map in the Connect drawer.")
                                               : qsTr("No register matches \"%1\".").arg(screen.registers.search)
            color: PhoneTheme.muted
            font.pixelSize: 16
        }
        Button {
            objectName: "registersEmptyConnect"
            Layout.alignment: Qt.AlignHCenter
            visible: screen.registers.total === 0
            text: qsTr("Open the Connect drawer")
            highlighted: true
            Material.accent: PhoneTheme.accentFill
            onClicked: screen.connectAsked()
        }
    }

    // what Plot or Copy did, a moment over the list's foot (a snackbar: the text colour's panel, the surface's text)
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

    // the "⋯" button's menu, one for every card
    Menu {
        id: moreMenu
        objectName: "moreMenu"
        property int source: -1
        property var entry: ({})
        function openFor(source, from) {
            moreMenu.source = source
            moreMenu.entry = screen.registers.entry(source)
            moreMenu.popup(from, 0, from.height)
        }
        MenuItem {
            objectName: "menuPlot"
            text: moreMenu.entry.plotted ? qsTr("Take off the chart") : qsTr("Plot")
            enabled: moreMenu.entry.plottable === true
            onTriggered: {
                const on = !moreMenu.entry.plotted
                if (PhoneSession.setPlot(moreMenu.source, on))
                    screen.showNotice(on ? qsTr("%1 on the chart").arg(moreMenu.entry.name)
                                         : qsTr("%1 off the chart").arg(moreMenu.entry.name))
            }
        }
        MenuItem {
            objectName: "menuWrite"
            text: qsTr("Write…")
            enabled: moreMenu.entry.writable === true
            onTriggered: screen.writeAsked(moreMenu.source)
        }
        MenuItem {
            objectName: "menuCopy"
            text: qsTr("Copy")
            onTriggered: {
                const copied = screen.registers.copy(moreMenu.source)
                if (copied.length > 0) screen.showNotice(qsTr("Copied: %1").arg(copied))
            }
        }
    }
}

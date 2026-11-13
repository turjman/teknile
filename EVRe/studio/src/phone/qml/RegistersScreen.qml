// SPDX-License-Identifier: Apache-2.0
// The Registers screen: every register of the map as a card (RegisterCard), in sections by group, the values live
// while the link is up (PhoneSession.registerList, RegisterModel's values copied once a frame). A search field at the
// top (the desktop's search: name, description, group, unit or address) with Clear; while no link is up a line says
// so with a button to the Connect drawer. The "⋯" button's menu: Plot (or Take off the chart) and Copy; what they did,
// or why not (the chart's limit of lines), shows a moment at the bottom.
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

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 4
            Layout.topMargin: 4
            spacing: 4

            TextField {
                id: searchField
                objectName: "registerSearch"
                Layout.fillWidth: true
                // only while empty: Material's label would float over the field's edge, half out of it
                placeholderText: text.length === 0 ? qsTr("Search: name, unit, address") : ""
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onTextChanged: screen.registers.search = text
            }
            Button {
                objectName: "searchClear"
                text: qsTr("Clear")
                flat: true
                enabled: searchField.text.length > 0
                onClicked: searchField.text = ""
            }
        }
        Label {
            objectName: "registerCount"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 4
            text: screen.registers.count === screen.registers.total
                  ? qsTr("registers: %1").arg(screen.registers.total)
                  : qsTr("registers: %1 of %2").arg(screen.registers.count).arg(screen.registers.total)
            color: PhoneTheme.muted
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        // no link: the values are the last read, or none; the way to the drawer
        Rectangle {
            objectName: "registersOffline"
            Layout.fillWidth: true
            visible: !PhoneSession.connected
            implicitHeight: offlineRow.implicitHeight + 8
            color: PhoneTheme.surface2
            RowLayout {
                id: offlineRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 16
                anchors.rightMargin: 4
                spacing: 4
                Label {
                    objectName: "offlineText"
                    Layout.fillWidth: true
                    text: qsTr("Not connected: the values show once the link is up.")
                    color: PhoneTheme.text
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
                Button {
                    objectName: "registersConnect"
                    text: qsTr("Connect…")
                    flat: true
                    onClicked: screen.connectAsked()
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: PhoneTheme.border
        }

        ListView {
            id: list
            objectName: "registerList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: screen.registers
            reuseItems: true
            boundsBehavior: Flickable.DragOverBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            bottomMargin: 8

            section.property: "group"
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                required property string section
                width: ListView.view.width
                height: section.length > 0 ? 36 : 4
                Label {
                    objectName: "regGroup"
                    horizontalAlignment: Text.AlignLeft
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.bottomMargin: 4
                    text: parent.section
                    color: PhoneTheme.accent
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
            }

            delegate: RegisterCard {
                width: ListView.view.width
                onWriteAsked: source => screen.writeAsked(source)
                onMoreAsked: (source, from) => moreMenu.openFor(source, from)
            }
        }
    }

    Label {
        objectName: "registersEmpty"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        visible: screen.registers.count === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: screen.registers.total === 0 ? qsTr("No registers: choose a map in the Connect drawer.")
                                           : qsTr("No register matches \"%1\".").arg(screen.registers.search)
        color: PhoneTheme.muted
        font.pixelSize: 16
    }

    // what Plot or Copy did, a moment over the list's foot
    Rectangle {
        id: notice
        objectName: "registerNotice"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(parent.width - 32, noticeText.implicitWidth + 32)
        height: noticeText.implicitHeight + 20
        radius: 8
        color: PhoneTheme.surface2
        border.color: PhoneTheme.border
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Label {
            id: noticeText
            objectName: "registerNoticeText"
            anchors.fill: parent
            anchors.margins: 10
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            color: PhoneTheme.text
            font.pixelSize: 14
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
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

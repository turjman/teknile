// SPDX-License-Identifier: Apache-2.0
// The Registers screen: every register of the map as a row of the desktop's table (RegisterRow: the name and address,
// the value lined up with the others, its unit), in sections by group (each group's header in the list over its rows;
// none is kept over the rows: on a phone it covered the row under it, its value cut), the values live while the link is up (PhoneSession.registerList, RegisterModel's values copied
// once a frame). A tap on a row opens the register's sheet (Main.qml: its value, trend, Plot, Copy and Write). A search
// field at the top (the desktop's search: name, description, group, unit or address), its magnifier and its clear cross
// drawn; how many registers show, quietly at the list's foot. While no link is up a slim strip at the top says so, with
// a button to the Connect drawer; with no map, or a search that finds nothing, the screen says so with one button (the
// drawer, or clearing the search). What a sheet's Plot or Copy did shows a moment at the bottom too.
// Each group folds (the owner): its header, 48 dp, is a button with a drawn chevron at its end ("v" open, ">" folded,
// mirrored in Arabic); folded, its rows go (a short fold of their height) and the header says how many it holds
// ("Sensors · 2"). The folds are kept per map (PhoneSession.foldedGroups); a search shows its matches in every group,
// the folds left as they were for when it is cleared. A folded group's registers are polled as any.
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
    // across (760 x 384 on the owner's phone, beside the rail): the search 40 high, the rows one line 42 high
    // (RegisterRow's compact), so five rows show with their groups' headers (48 dp, they fold)
    readonly property bool wide: width > height
    readonly property var folds: PhoneSession.foldedGroups
    readonly property bool searching: registers.search.length > 0
    // a tap's fold moves the rows' height; rows the list reuses while it scrolls take theirs at once
    property bool foldMoving: false
    Timer {
        id: foldMotion
        interval: 300
        onTriggered: screen.foldMoving = false
    }
    function toggleFold(group) {
        screen.foldMoving = PhoneSession.motion
        foldMotion.restart()
        PhoneSession.setGroupFolded(group, screen.folds.indexOf(group) < 0)
    }

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
            Layout.topMargin: screen.wide ? 4 : 8
            Layout.bottomMargin: 4
            implicitHeight: screen.wide ? 40 : 48
            radius: 8
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
                // a press: a rounded square's flat tint, not Material's circle
                background: Item {
                    implicitWidth: 48
                    implicitHeight: 48
                    PressTint {
                        anchors.margins: 4
                        radius: 8
                        pressed: parent.parent ? parent.parent.down : false
                    }
                }
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
                    radius: 2
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
                    Material.roundedScale: Material.SmallScale
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
            // the group's name over its rows, on the window's background, the rows on the surface, so each group reads
            // as a panel of its own. In the list only, not kept at the view's top: a header kept there lay over the
            // rows passing under it and cut their values (the owner's phone). The whole header folds the group or
            // opens it (48 dp, a chevron at its end); while a search is typed it is a header alone
            section.labelPositioning: ViewSection.InlineLabels
            section.delegate: Rectangle {
                id: head
                required property string section
                readonly property bool named: section.length > 0
                readonly property bool folded: named && !screen.searching && screen.folds.indexOf(section) >= 0
                objectName: "groupHeader"
                width: ListView.view.width
                height: named ? 48 : 4
                color: PhoneTheme.bg
                Accessible.role: Accessible.Button
                Accessible.name: head.folded ? qsTr("Open %1").arg(head.section) : qsTr("Fold %1").arg(head.section)
                Accessible.onPressAction: if (head.named && !screen.searching) screen.toggleFold(head.section)
                PressTint {
                    pressed: headTap.pressed && head.named && !screen.searching
                }
                Row {
                    anchors.left: parent.left
                    anchors.right: chevron.left
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Label {
                        id: groupName
                        objectName: "regGroup"
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - (groupCount.visible ? groupCount.implicitWidth + 8 : 0))
                        horizontalAlignment: Text.AlignLeft
                        text: head.section
                        color: PhoneTheme.accent
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.4
                        elide: Text.ElideRight
                    }
                    // folded: how many registers it holds, so it still says what is in it
                    Label {
                        id: groupCount
                        objectName: "regGroupCount"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: head.folded
                        // the dot between the name and the number in either direction
                        text: Qt.application.layoutDirection === Qt.RightToLeft
                              ? PhoneSession.groupSize(head.section) + " \u00b7" : "\u00b7 " + PhoneSession.groupSize(head.section)
                        color: PhoneTheme.muted
                        font.pixelSize: 13
                    }
                }
                // "v" open, ">" folded (in Arabic "<": the start is at the right)
                PhoneIcon {
                    id: chevron
                    objectName: "groupChevron"
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: head.named && !screen.searching
                    kind: !head.folded ? "down" : Qt.application.layoutDirection === Qt.RightToLeft ? "back" : "next"
                    size: 18
                    color: PhoneTheme.muted
                }
                TapHandler {
                    id: headTap
                    enabled: head.named && !screen.searching
                    onTapped: screen.toggleFold(head.section)
                }
            }

            delegate: RegisterRow {
                width: ListView.view.width
                compact: screen.wide
                folded: !screen.searching && screen.folds.indexOf(group) >= 0
                foldMotion: screen.foldMoving
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

// SPDX-License-Identifier: Apache-2.0
// The phone's window (STUDIO.md 25.6): a header (the menu button, the screen's name, the link's state as a pill, which
// opens the drawer too), the Connect drawer, a tab bar at the bottom (Registers, Chart, Monitor, Log) and the screens
// above it. Material, coloured from PhoneTheme; right to left in Arabic (the header, the tab bar, the screens and the
// drawer mirrored).
// With p9 (phone_app.cpp) the window shows the proof P-9 alone; with chartDemo the Chart screen, fed by the chart's test
// feed (chart_demo.h).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

ApplicationWindow {
    id: root
    objectName: "phoneWindow"
    width: 412
    height: 915
    visible: true
    title: qsTr("EVRe Studio")

    property bool p9: false
    property bool chartDemo: false
    // the direction comes from the Arabic translation (language.cpp), as on the desktop
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property var screenNames: [qsTr("Registers"), qsTr("Chart"), qsTr("Monitor"), qsTr("Log")]
    readonly property var screenKeys: ["Registers", "Chart", "Monitor", "Log"]

    Material.theme: PhoneTheme.dark ? Material.Dark : Material.Light
    Material.accent: PhoneTheme.accent
    Material.background: PhoneTheme.bg
    Material.foreground: PhoneTheme.text
    color: PhoneTheme.bg

    header: ToolBar {
        id: header
        objectName: "header"
        visible: !root.p9
        LayoutMirroring.enabled: root.rtl
        LayoutMirroring.childrenInherit: true
        Material.foreground: PhoneTheme.text
        // the bar a panel of its own, a line under it: the screen below never reads as part of it
        background: Rectangle {
            color: PhoneTheme.surface
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: PhoneTheme.border
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 12
            spacing: 8

            ToolButton {
                id: menuButton
                objectName: "menuButton"
                text: "☰"
                font.pixelSize: 22
                Accessible.name: qsTr("Connect and settings")
                onClicked: drawer.open()
            }
            Label {
                id: screenTitle
                objectName: "screenTitle"
                Layout.fillWidth: true
                text: root.screenNames[tabBar.currentIndex]
                color: PhoneTheme.text
                font.pixelSize: 20
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            LinkPill {
                objectName: "linkPill"
                text: PhoneSession.pillText
                dotColor: {
                    switch (PhoneSession.state) {
                    case PhoneSession.Connected: return PhoneTheme.good
                    case PhoneSession.Closed: return PhoneTheme.bad
                    case PhoneSession.Off: return PhoneTheme.muted
                    default: return PhoneTheme.warn
                    }
                }
                onClicked: drawer.open()
            }
        }
    }

    footer: TabBar {
        id: tabBar
        objectName: "tabBar"
        visible: !root.p9
        LayoutMirroring.enabled: root.rtl
        LayoutMirroring.childrenInherit: true
        Material.background: PhoneTheme.surface
        Material.accent: PhoneTheme.accent
        // a line over the bar: the tabs apart from the screen
        background: Rectangle {
            color: PhoneTheme.surface
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: PhoneTheme.border
            }
        }

        Repeater {
            model: root.screenNames
            TabButton {
                objectName: "tab" + root.screenKeys[index]
                text: modelData
            }
        }
    }

    StackLayout {
        id: screens
        objectName: "screens"
        anchors.fill: parent
        visible: !root.p9
        currentIndex: tabBar.currentIndex
        LayoutMirroring.enabled: root.rtl
        LayoutMirroring.childrenInherit: true

        PlaceholderScreen {
            objectName: "screenRegisters"
            note: qsTr("Coming soon: every register of the map, its value live.")
        }
        ChartScreen {
            objectName: "screenChart"
            demo: root.chartDemo
        }
        PlaceholderScreen {
            objectName: "screenMonitor"
            note: qsTr("Coming soon: the frames on the link.")
        }
        LogScreen {
            objectName: "screenLog"
        }
    }

    Component.onCompleted: if (chartDemo) tabBar.currentIndex = 1

    ConnectDrawer {
        id: drawer
        objectName: "connectDrawer"
        rtl: root.rtl
        interactive: !root.p9
        width: Math.min(root.width * 0.85, 360)
        height: root.height
    }

    Loader {
        objectName: "p9Loader"
        anchors.fill: parent
        active: root.p9
        sourceComponent: P9Screen {}
    }
}

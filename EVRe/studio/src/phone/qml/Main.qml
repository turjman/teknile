// SPDX-License-Identifier: Apache-2.0
// The phone's window (STUDIO.md 25.6): a header (the menu button, the screen's name, the link's state as a pill, which
// opens the drawer too), the Connect drawer, a tab bar at the bottom (Registers, Chart, Monitor, Log, each with a drawn
// icon) and the screens above it. Material, coloured from PhoneTheme; right to left in Arabic (the header, the tab
// bar, the screens and the drawer mirrored).
// With p9 (phone_app.cpp) the window shows the proof P-9 alone; with perf the Registers screen keeps a bar running
// and registersShown tells the speed line (EVRE_PERF) when to write; with chartDemo the Chart screen, fed by the chart's
// test feed (chart_demo.h), chartShown telling the same line when to write.
// The soft keyboard: the manifest asks Android to make the window smaller over it (adjustResize); keyboardLift is what
// the keyboard still covers of the window when it does not (an Android that draws the app under it). The write sheet
// and the drawer keep the field being typed in, and the sheet's Apply, above it.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Controls.Material.impl
import QtQuick.Layouts
import QtQuick.Window
import EVRe.Phone

ApplicationWindow {
    id: root
    objectName: "phoneWindow"
    width: 412
    height: 915
    visible: true
    title: qsTr("EVRe Studio")

    property bool p9: false
    property bool perf: false
    // the Registers screen in view: the speed line's seconds (phone_app.cpp)
    readonly property bool registersShown: !p9 && tabBar.currentIndex === 0 && !drawer.opened && !writeSheet.opened
    property bool chartDemo: false
    readonly property bool chartShown: !p9 && tabBar.currentIndex === 1 && !drawer.opened
    // the direction comes from the Arabic translation (language.cpp), as on the desktop
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property var screenNames: [qsTr("Registers"), qsTr("Chart"), qsTr("Monitor"), qsTr("Log")]
    readonly property var screenKeys: ["Registers", "Chart", "Monitor", "Log"]
    readonly property var screenIcons: ["registers", "chart", "monitor", "log"]
    // landscape: the tabs' icons beside their names, the bar lower
    readonly property bool wide: width > height

    // the soft keyboard's top in the window's logical pixels (the window's height while it is hidden). Qt 6.8 gives
    // Android's keyboard rectangle in device pixels, hence the division. A test sets testKeyboardHeight to stand for
    // a keyboard: a desktop has none.
    property real testKeyboardHeight: 0
    readonly property real keyboardTop: {
        if (testKeyboardHeight > 0) return height - testKeyboardHeight
        const rect = Qt.inputMethod.keyboardRectangle
        if (!Qt.inputMethod.visible || rect.height <= 0) return height
        const dpr = Qt.platform.os === "android" ? Screen.devicePixelRatio : 1
        return rect.y / dpr
    }
    // what the keyboard covers of the window: 0 when the window was made smaller for it
    readonly property real keyboardLift: Math.min(Math.max(0, height - keyboardTop), height * 0.75)

    Material.theme: PhoneTheme.dark ? Material.Dark : Material.Light
    Material.accent: PhoneTheme.accent
    Material.background: PhoneTheme.bg
    Material.foreground: PhoneTheme.text
    color: PhoneTheme.bg

    // Behind a modal (the drawer, the write sheet) the screen darkens in both themes: Material's own dimming is a grey
    // wash, which lightened the dark theme's screen instead. A Drawer of Material's sets its own, so they set this too
    readonly property Component scrim: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }
    Overlay.modal: scrim
    Overlay.modeless: scrim

    header: ToolBar {
        id: header
        objectName: "header"
        visible: !root.p9
        implicitHeight: 56
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
            anchors.rightMargin: 8
            spacing: 4

            // three bars drawn, not a character: a phone's font may have no "☰" (a Samsung phone's had none)
            ToolButton {
                id: menuButton
                objectName: "menuButton"
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Accessible.name: qsTr("Connect and settings")
                onClicked: drawer.open()
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Repeater {
                            model: 3
                            Rectangle {
                                objectName: "menuBar"
                                width: 20
                                height: 2
                                radius: 1
                                color: PhoneTheme.text
                            }
                        }
                    }
                }
            }
            Label {
                id: screenTitle
                objectName: "screenTitle"
                Layout.fillWidth: true
                Layout.leftMargin: 4
                horizontalAlignment: Text.AlignLeft
                text: root.screenNames[tabBar.currentIndex]
                color: PhoneTheme.text
                font.pixelSize: 20
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            LinkPill {
                objectName: "linkPill"
                Layout.maximumWidth: root.width * 0.55
                text: PhoneSession.pillText
                tone: PhoneSession.state === PhoneSession.Connected ? "good"
                      : PhoneSession.state === PhoneSession.Closed ? "bad"
                      : PhoneSession.state === PhoneSession.Off ? "off" : "busy"
                onClicked: drawer.open()
            }
        }
    }

    // the tabs: an icon over each name (beside it in landscape); the chosen one in the accent, its icon on a pill
    footer: TabBar {
        id: tabBar
        objectName: "tabBar"
        visible: !root.p9
        implicitHeight: root.wide ? 52 : 68
        LayoutMirroring.enabled: root.rtl
        LayoutMirroring.childrenInherit: true
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
        // Material's line under the chosen tab goes: the pill marks it
        contentItem: ListView {
            model: tabBar.contentModel
            currentIndex: tabBar.currentIndex
            spacing: tabBar.spacing
            orientation: ListView.Horizontal
            boundsBehavior: Flickable.StopAtBounds
            interactive: false
        }

        Repeater {
            model: root.screenNames
            TabButton {
                id: tab
                required property int index
                required property string modelData
                objectName: "tab" + root.screenKeys[index]
                text: modelData
                implicitHeight: tabBar.implicitHeight
                padding: 0
                readonly property color tone: checked ? PhoneTheme.accent : PhoneTheme.muted
                background: Item {}
                contentItem: Item {
                    // the icon's pill, then the name: under it, or beside it in landscape
                    Rectangle {
                        id: pill
                        objectName: "tabPill"
                        x: root.wide ? (parent.width - width - 8 - name.width) / 2 : (parent.width - width) / 2
                        y: root.wide ? (parent.height - height) / 2 : 9
                        width: 56
                        height: 30
                        radius: height / 2
                        color: tab.checked ? PhoneTheme.accentSoft : "transparent"
                        Ripple {
                            anchors.fill: parent
                            clipRadius: pill.radius
                            pressed: tab.pressed
                            anchor: tab
                            active: tab.pressed
                            color: tab.Material.rippleColor
                        }
                        PhoneIcon {
                            objectName: "tabIcon"
                            anchors.centerIn: parent
                            kind: root.screenIcons[tab.index]
                            color: tab.tone
                        }
                    }
                    Label {
                        id: name
                        objectName: "tabName"
                        x: root.wide ? pill.x + pill.width + 8 : (parent.width - width) / 2
                        y: root.wide ? (parent.height - height) / 2 : pill.y + pill.height + 4
                        width: Math.min(implicitWidth, parent.width - (root.wide ? pill.width + 16 : 8))
                        text: tab.text
                        color: tab.tone
                        font.pixelSize: 12
                        font.weight: tab.checked ? Font.DemiBold : Font.Medium
                        elide: Text.ElideRight
                    }
                }
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

        RegistersScreen {
            objectName: "screenRegisters"
            perf: root.perf
            onConnectAsked: drawer.open()
            onWriteAsked: source => writeSheet.openFor(source)
        }
        ChartScreen {
            objectName: "screenChart"
            demo: root.chartDemo
            onRegistersAsked: tabBar.currentIndex = 0
        }
        MonitorScreen {
            objectName: "screenMonitor"
            onConnectAsked: drawer.open()
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
        keyboardLift: root.keyboardLift
        Overlay.modal: root.scrim
        Overlay.modeless: root.scrim
    }

    WriteSheet {
        id: writeSheet
        objectName: "writeSheet"
        rtl: root.rtl
        width: root.width
        interactive: opened
        keyboardLift: root.keyboardLift
        onConnectAsked: drawer.open()
        Overlay.modal: root.scrim
        Overlay.modeless: root.scrim
    }

    Loader {
        objectName: "p9Loader"
        anchors.fill: parent
        active: root.p9
        sourceComponent: P9Screen {}
    }
}

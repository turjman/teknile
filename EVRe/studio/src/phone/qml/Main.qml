// SPDX-License-Identifier: Apache-2.0
// The phone's window (STUDIO.md 25.6): a slim header (the menu button, the screen's name, the link's state as a pill
// with the poll rate while connected and a chevron, which opens the drawer too), the Connect drawer, a tab bar at the
// bottom (Registers, Chart, Monitor, Log, each with a drawn icon) and the screens above it. Material, coloured from PhoneTheme; right to left in Arabic (the header, the tab
// bar, the screens and the drawer mirrored).
// With p9 (phone_app.cpp) the window shows the proof P-9 alone; with perf the Registers screen keeps a bar running
// and registersShown tells the speed line (EVRE_PERF) when to write; with chartDemo the Chart screen, fed by the chart's
// test feed (chart_demo.h), chartShown telling the same line when to write.
// Across (landscape) the header and the bottom tab bar give way to a navigation rail at the window's start edge (the
// right in Arabic): the menu button, the four tabs (an icon over each name) and the link's state (its dot, the poll
// rate or its word: a tap opens the drawer), so the screens keep the whole height (seven register rows at 760 x 384).
// The Chart across is the whole window (chartFull): the rail hidden, the chart's bars button (or Android's back) brings
// it back until another tab is chosen. A tab's screen comes in with a short fade and slide from
// the side of the tab chosen (none when Android's animations are removed: PhoneSession.motion).
// The soft keyboard: the manifest asks Android to make the window smaller over it (adjustResize); keyboardLift is what
// the keyboard still covers of the window when it does not (an Android that draws the app under it). A register's
// sheet and the drawer keep the field being typed in, and the sheet's Write, above it.
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
    readonly property bool registersShown: !p9 && tabBar.currentIndex === 0 && !drawer.opened && !registerSheet.opened
    property bool chartDemo: false
    readonly property bool chartShown: !p9 && tabBar.currentIndex === 1 && !drawer.opened
    // the direction comes from the Arabic translation (language.cpp), as on the desktop
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    readonly property var screenNames: [qsTr("Registers"), qsTr("Chart"), qsTr("Monitor"), qsTr("Log")]
    readonly property var screenKeys: ["Registers", "Chart", "Monitor", "Log"]
    readonly property var screenIcons: ["registers", "chart", "monitor", "log"]
    // landscape: the rail in place of the header and the tab bar (they show upright only)
    readonly property bool wide: width > height
    // the Chart across on the whole window, unless its bars button (or a back) asked for the bars; another tab chosen
    // forgets that
    property bool chartBars: false
    readonly property bool chartFull: !p9 && wide && tabBar.currentIndex === 1 && !chartBars
    // the rail across (not on the Chart's whole window), the header and the tab bar upright
    readonly property bool railShown: !p9 && wide && !chartFull
    readonly property real railWidth: 80
    // the link's state as a tone, for the header's pill and the rail's dot
    readonly property string linkTone: PhoneSession.state === PhoneSession.Connected ? "good"
                                       : PhoneSession.state === PhoneSession.Closed ? "bad"
                                       : PhoneSession.state === PhoneSession.Off ? "off" : "busy"
    readonly property bool showRate: PhoneSession.state === PhoneSession.Connected && PhoneSession.polling
                                     && PhoneSession.pollHz > 0

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

    // Behind a modal (the drawer, a register's sheet) the screen darkens in both themes: Material's own dimming is a grey
    // wash, which lightened the dark theme's screen instead. A Drawer of Material's sets its own, so they set this too
    readonly property Component scrim: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }
    Overlay.modal: scrim
    Overlay.modeless: scrim

    header: ToolBar {
        id: header
        objectName: "header"
        visible: !root.p9 && !root.wide
        // 48 high, the touch targets' height: the room goes to the screens (the Chart's plot in landscape)
        implicitHeight: 48
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
                // the rail's is the menu button across
                objectName: header.visible ? "menuButton" : "headerMenuButton"
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
                font.pixelSize: 18
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            LinkPill {
                objectName: header.visible ? "linkPill" : "headerLinkPill"
                Layout.maximumWidth: root.width * 0.6
                // the polls a second beside "Connected", as the desktop's status bar has them (units stay in Latin)
                text: root.showRate ? qsTr("%1 · %2 Hz").arg(PhoneSession.pillText).arg(root.rateText(PhoneSession.pollHz))
                      : PhoneSession.pillText
                tone: root.linkTone
                onClicked: drawer.open()
            }
        }
    }

    // the tabs: an icon over each name (beside it in landscape); the chosen one in the accent, its icon on a pill
    footer: TabBar {
        id: tabBar
        objectName: "tabBar"
        visible: !root.p9 && !root.wide
        onCurrentIndexChanged: root.chartBars = false
        implicitHeight: root.wide ? 48 : 68
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
                // the rail's tab has the name across
                objectName: (tabBar.visible ? "tab" : "barTab") + root.screenKeys[index]
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

    // the rail across: the menu button, the tabs, the link's state; a line at its inner edge sets it apart
    Rectangle {
        id: rail
        objectName: "navRail"
        visible: root.railShown
        width: root.railWidth
        height: parent.height
        x: root.rtl ? parent.width - width : 0
        color: PhoneTheme.surface
        Rectangle {
            x: root.rtl ? 0 : parent.width - 1
            width: 1
            height: parent.height
            color: PhoneTheme.border
        }
        ToolButton {
            id: railMenu
            objectName: rail.visible ? "menuButton" : "railMenuButton"
            x: (parent.width - width) / 2
            y: 4
            width: 48
            height: 48
            Accessible.name: qsTr("Connect and settings")
            onClicked: drawer.open()
            contentItem: Item {
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Repeater {
                        model: 3
                        Rectangle {
                            width: 20
                            height: 2
                            radius: 1
                            color: PhoneTheme.text
                        }
                    }
                }
            }
        }
        Column {
            y: railMenu.y + railMenu.height + 4
            width: parent.width
            Repeater {
                model: root.screenNames
                AbstractButton {
                    id: railTab
                    required property int index
                    required property string modelData
                    objectName: (rail.visible ? "tab" : "railTab") + root.screenKeys[index]
                    width: rail.width
                    height: 60
                    text: modelData
                    checked: tabBar.currentIndex === index
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData
                    onClicked: tabBar.currentIndex = index
                    readonly property color tone: checked ? PhoneTheme.accent : PhoneTheme.muted
                    contentItem: Item {
                        Rectangle {
                            id: railPill
                            objectName: "tabPill"
                            x: (parent.width - width) / 2
                            y: 6
                            width: 56
                            height: 30
                            radius: height / 2
                            color: railTab.checked ? PhoneTheme.accentSoft : "transparent"
                            Ripple {
                                anchors.fill: parent
                                clipRadius: railPill.radius
                                pressed: railTab.pressed
                                anchor: railTab
                                active: railTab.pressed
                                color: railTab.Material.rippleColor
                            }
                            PhoneIcon {
                                objectName: "tabIcon"
                                anchors.centerIn: parent
                                kind: root.screenIcons[railTab.index]
                                color: railTab.tone
                            }
                        }
                        Label {
                            objectName: "tabName"
                            x: (parent.width - width) / 2
                            y: railPill.y + railPill.height + 3
                            width: Math.min(implicitWidth, parent.width - 4)
                            text: railTab.text
                            color: railTab.tone
                            font.pixelSize: 12
                            font.weight: railTab.checked ? Font.DemiBold : Font.Medium
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        // the link's state at the foot: its dot, then the poll rate (connected) or its word, two lines at most; a tap
        // opens the drawer, as the header's pill does
        AbstractButton {
            id: railLink
            objectName: rail.visible ? "linkPill" : "railLinkPill"
            x: 4
            width: parent.width - 8
            height: 56
            y: parent.height - height - 4
            Accessible.role: Accessible.Button
            Accessible.name: PhoneSession.pillText
            onClicked: drawer.open()
            background: Rectangle {
                radius: 12
                color: railLink.pressed ? PhoneTheme.border
                       : root.linkTone === "good" ? PhoneTheme.goodSoft : root.linkTone === "bad" ? PhoneTheme.badSoft
                       : root.linkTone === "busy" ? PhoneTheme.warnSoft : PhoneTheme.surface2
            }
            contentItem: Item {
                Rectangle {
                    objectName: "railLinkDot"
                    x: (parent.width - width) / 2
                    y: 8
                    width: 10
                    height: 10
                    radius: 5
                    color: root.linkTone === "good" ? PhoneTheme.good : root.linkTone === "bad" ? PhoneTheme.bad
                           : root.linkTone === "busy" ? PhoneTheme.warn : PhoneTheme.muted
                }
                Label {
                    objectName: "railLinkText"
                    x: 2
                    y: 22
                    width: parent.width - 4
                    horizontalAlignment: Text.AlignHCenter
                    // the rate a number and its unit, one piece left to right in every language
                    text: root.showRate ? "\u2066" + root.rateText(PhoneSession.pollHz) + " Hz\u2069" : PhoneSession.pillText
                    color: PhoneTheme.text
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
            }
        }
    }

    StackLayout {
        id: screens
        objectName: "screens"
        anchors.fill: parent
        // the rail's room at the start edge (the screens' mirroring takes it to the right in Arabic)
        anchors.leftMargin: rail.visible ? rail.width : 0
        visible: !root.p9
        currentIndex: tabBar.currentIndex
        LayoutMirroring.enabled: root.rtl
        LayoutMirroring.childrenInherit: true

        // the screen coming in: from the side of the tab chosen (a tab further on comes from the end), faded in
        property int lastIndex: currentIndex
        property real shift: 0
        readonly property real shiftBy: 32
        onCurrentIndexChanged: {
            const item = children[currentIndex]
            const towardEnd = currentIndex > lastIndex
            lastIndex = currentIndex
            tabMotion.stop()
            if (!item || !PhoneSession.motion) {
                shift = 0
                if (item) item.opacity = 1
                return
            }
            tabMotion.target = item
            // the end is at the right, at the left in Arabic
            shift = (towardEnd ? 1 : -1) * (root.rtl ? -1 : 1) * shiftBy
            item.opacity = 0
            tabMotion.start()
        }
        ParallelAnimation {
            id: tabMotion
            objectName: "tabMotion"
            property Item target
            NumberAnimation { target: screens; property: "shift"; to: 0; duration: 220; easing.type: Easing.OutCubic }
            NumberAnimation { target: tabMotion.target; property: "opacity"; to: 1; duration: 180; easing.type: Easing.OutCubic }
        }

        RegistersScreen {
            transform: Translate { x: screens.currentIndex === 0 ? screens.shift : 0 }
            objectName: "screenRegisters"
            perf: root.perf
            onConnectAsked: drawer.open()
            onSheetAsked: source => registerSheet.openFor(source)
        }
        ChartScreen {
            transform: Translate { x: screens.currentIndex === 1 ? screens.shift : 0 }
            objectName: "screenChart"
            demo: root.chartDemo
            fullScreen: root.chartFull
            onRegistersAsked: tabBar.currentIndex = 0
            onBarsAsked: shown => root.chartBars = shown
        }
        MonitorScreen {
            id: monitorScreen
            transform: Translate { x: screens.currentIndex === 2 ? screens.shift : 0 }
            objectName: "screenMonitor"
            onConnectAsked: drawer.open()
        }
        LogScreen {
            transform: Translate { x: screens.currentIndex === 3 ? screens.shift : 0 }
            objectName: "screenLog"
            onConnectAsked: drawer.open()
        }
    }

    Component.onCompleted: if (chartDemo) tabBar.currentIndex = 1

    // the poll rate as the pill writes it: whole numbers (a tenth that came and went made the pill flicker), a tenth
    // under 2 (a slow poll's 0.5 Hz)
    function rateText(hz) {
        return hz >= 2 ? Math.round(hz).toString() : (Math.round(hz * 10) / 10).toString()
    }

    // Android's back (phone_app.cpp hands it here first): the keyboard goes, else what is open closes, the topmost
    // first; with nothing open the app goes to the background while a link or a recording runs; false: Android has the
    // back (it closes the app)
    function back() {
        // the soft keyboard first, as Android does: a back while it is up only hides it and takes the focus off the
        // field (Qt hands that back to the app: from the search field one back quit the app, link and all)
        if (Qt.inputMethod.visible || testKeyboardHeight > 0) {
            Qt.inputMethod.hide()
            // the field's own focus off (the window's content taking it would hand it back down to the field)
            if (root.activeFocusItem) root.activeFocusItem.focus = false
            return true
        }
        if (showing(helpScreen)) {
            helpScreen.back()
            return true
        }
        if (showing(registerSheet)) {
            registerSheet.close()
            return true
        }
        if (showing(monitorScreen.sheet)) {
            monitorScreen.sheet.close()
            return true
        }
        if (showing(drawer)) {
            drawer.close()
            return true
        }
        // the Chart on the whole window: a back brings the rail back first
        if (root.chartFull) {
            root.chartBars = true
            return true
        }
        // nothing open: while a link is wanted or a recording runs the app goes to the background as with Home
        // (finishing the activity ended the process, and the link and the recording with it); else Android closes it
        return root.p9 ? false : PhoneSession.backToBackground()
    }
    // a popup on the screen and not on its way out: also while it is still coming in (`opened` is true only once its
    // transition has ended, so a back in the drawer's slide, or a finger's drag of it, went to Android and the app
    // went to the background with the drawer half open)
    function showing(popup) {
        return popup.visible && !(popup.exit && popup.exit.running)
    }

    // each screen's orientation (A-16): the Chart and the Monitor across; the Registers, the Log and the proof as the
    // phone is held ("": Android's unspecified), so going back from the Chart no longer turns the phone upright (the
    // owner's phone: held across, a tap on Registers flipped it). Android turns the activity (PhoneSession, SystemBridge)
    readonly property string wantedOrientation: !p9 && (tabBar.currentIndex === 1 || tabBar.currentIndex === 2)
                                                ? "landscape" : ""
    Binding {
        target: PhoneSession
        property: "orientation"
        value: root.wantedOrientation
    }

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
        onHelpAsked: helpScreen.open()
    }

    HelpScreen {
        id: helpScreen
        objectName: "helpScreen"
        rtl: root.rtl
    }

    RegisterSheet {
        id: registerSheet
        objectName: "registerSheet"
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

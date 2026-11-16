// SPDX-License-Identifier: Apache-2.0
// The Chart screen (STUDIO.md 25.6): the plotted lines over time, drawn by the GPU from nodes of the chart's own
// (ChartItem, chart_item.h), the time and value labels as Text over it. Above it: Live / Hold, the window (a button
// with the window shown, its menu 10 s, 1 min, 5 min), Y (a button with the range shown: Auto, or a manual range from a
// small sheet), Cursors, Measure, Clear, and Fast lines when the map has streams, as the desktop's combo boxes;
// a line saying what a finger does. The legend over the plot's top, as the desktop's: a compact chip per line (its
// colour, name and latest value in view with its unit, a "▾" for its menu: Hide / Show and Take off the chart; a tap
// on the chip hides or shows the line), whole chips only: when they do not all fit, the first ones and a "+N" chip
// that opens the Lines sheet, every line with its value, a switch to show it and its menu. Measure opens a sheet of
// the desktop's measurements per line (min, max, mean, RMS, std dev, peak-peak) over the view, or over A -> B with
// the cursors placed, at most four times a second; at the screen's foot upright, at its end across, the chart
// usable beside it; its columns scroll sideways under the names. The time axis and the cursors say clock times
// (17:50:50), as the desktop's. A pinch zooms the time, a drag looks back (and holds), a double tap goes live again.
// Time runs left to right in Arabic too: the plot, its labels and the legend are never mirrored. The lines come from PhoneSession (the registers plotted on the
// Registers screen, the fast channels chosen here); with none, the plot says how to plot one, with a button to the
// Registers screen. Across (the phone's way for the Chart) the screen is the whole window (fullScreen, Main.qml hides
// its header and tab bar): one 48 dp row of the controls and the legend over the plot, a button at its start (four
// corners) brings the bars back (barsAsked), a dot on it in the link's colour; with the bars back the same button hides
// them again. The controls are alone on their row: the legend is over the plot. Cursors (B.4): the Cursors button puts A and B where the lines have samples in view (a quarter and
// three quarters of that span) and holds the view; each is dragged by its round handle (48 dp; A's at the plot's top,
// B's at its foot); the readouts beside the plot (under it in portrait; scrolled, with a scroll bar and a fade at an
// edge with more) give A's and B's times and B - A, and per line its values at A and B, B - A, and min, max and mean
// over A..B (ChartItem's readouts: the desktop's statsOf; an em dash where a line has no value, "No samples between
// A and B" where it has none at all). With demo (Main.qml's chartDemo) the chart's test feed draws 8 lines at 1,000 Hz (chart_demo.h).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    property bool demo: false
    readonly property alias chart: chart

    // the empty state's button: Main.qml shows the Registers screen
    signal registersAsked()

    // the window choice's lengths, seconds, and their names (a unit stays as it is in every language)
    readonly property var windows: [10, 60, 300]
    readonly property var windowNames: ["10 s", "1 min", "5 min"]

    // a number for the Y range's button and fields: four significant digits, Western digits in every language
    function shortNumber(v) { return String(Number(v.toPrecision(4))) }

    // a cursor's handle is being dragged: the view stays where it is
    property bool cursorDragging: false

    // across: the rail hidden (Main.qml); the bars button at the controls' start asks for it back (or away again)
    property bool fullScreen: false
    signal barsAsked(bool shown)

    // the window's length as its button writes it: "10 s", "1 min", or a pinch's "27.3 s" (units in Latin)
    function windowText(seconds) {
        if (seconds >= 60) return String(Number((seconds / 60).toPrecision(3))) + " min"
        return String(Number(seconds.toPrecision(3))) + " s"
    }

    // landscape: the plot keeps three quarters of the height or more (the owner's phone, 760 x 384 dp beside its
    // navigation buttons). The gesture line goes (it stays upright and in Help), the buttons are as narrow as their
    // text (48 dp or more), the window and Y are one button each with their menu
    readonly property bool wide: width > height
    readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
    // the legend's row over the plot: the chips drawn 28 high in it (their touch 48, into the plot's top)
    readonly property real legendHeight: 34
    readonly property alias measureSheet: measureSheet
    readonly property alias linesSheet: linesSheet

    // a button of the controls: flat, in its row's middle; across, as narrow as its text allows (Material's are 64 dp
    // at least) and 48 dp high (Material's 52: its face 40 and 6 above and under), 48 dp to the touch either way
    component BarButton: Button {
        Material.roundedScale: Material.SmallScale
        flat: true
        anchors.verticalCenter: parent.verticalCenter
        topInset: screen.wide ? 4 : 6
        bottomInset: screen.wide ? 4 : 6
        implicitWidth: Math.max(screen.wide ? 48 : implicitBackgroundWidth + leftInset + rightInset,
                                implicitContentWidth + leftPadding + rightPadding)
    }

    // a button that opens a menu or a sheet: its text and a drawn chevron (a phone's font may have no "▾")
    component MenuButton: BarButton {
        id: menuButton
        property string label
        text: label
        rightPadding: screen.wide ? 6 : 8
        contentItem: Item {
            implicitWidth: menuLabel.implicitWidth + 2 + 16
            implicitHeight: menuLabel.implicitHeight
            Label {
                id: menuLabel
                objectName: "menuButtonLabel"
                anchors.verticalCenter: parent.verticalCenter
                x: menuButton.mirrored ? 18 : 0
                text: menuButton.label
                font: menuButton.font
                color: PhoneTheme.text
            }
            PhoneIcon {
                anchors.verticalCenter: parent.verticalCenter
                x: menuButton.mirrored ? 0 : menuLabel.implicitWidth + 2
                kind: "down"
                size: 16
                color: PhoneTheme.muted
            }
        }
    }

    function showNotice(text) {
        noticeText.text = text
        notice.opacity = 1
        noticeTimer.restart()
    }

    // a readout: its label (translated, muted) and its value (a number and unit, left to right), one piece read as its
    // text would be: a symbol's (A, B, B - A) left to right in every language ("B 5.11 V"), a word's in the
    // language's direction (in Arabic the word at the right). The gap to the next pair (12) is on the side the pairs
    // run to and far wider than the label's to its value (4), so a value is never read with the next pair's label.
    component Pair: Item {
        id: pair
        property string label
        property string value
        property string valueName: "readoutValue"
        property string labelName: "readoutLabel"
        property bool symbol: false
        readonly property bool rtl: Qt.application.layoutDirection === Qt.RightToLeft
        objectName: "readoutPair"
        implicitWidth: pairRow.implicitWidth + 12
        implicitHeight: pairRow.implicitHeight
        Row {
            id: pairRow
            objectName: "readoutPairRow"
            x: pair.rtl ? 12 : 0
            spacing: 4
            LayoutMirroring.enabled: pair.rtl && !pair.symbol
            Text {
                objectName: pair.labelName
                text: pair.label
                color: PhoneTheme.muted
                font.pixelSize: 12
                anchors.baseline: valueText.baseline
            }
            Text {
                id: valueText
                objectName: pair.valueName
                text: pair.value
                color: PhoneTheme.text
                font.pixelSize: 13
            }
        }
    }

    // the session feeds this chart (not the demo's: it has its own clock and lines)
    Component.onCompleted: if (!demo) PhoneSession.chart = chart

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // the controls and the gesture line, one under the other (the legend is over the plot)
        ColumnLayout {
            id: topRows
            objectName: "chartTop"
            Layout.fillWidth: true
            spacing: 0

            // the controls: one row in landscape; in portrait the view's (Live / Hold, the window) over the lines'
            // (Y, Clear, Fast lines), each group whole
            Flow {
                id: controls
                objectName: "chartControls"
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                spacing: 0

                Row {
                    id: viewGroup
                    spacing: 2
                    // across: the rail back, or away again (four corners in, or out); the link's
                    // state a dot on it, as the header's pill would say it
                    ToolButton {
                        id: barsButton
                        objectName: "barsButton"
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
                        visible: screen.wide
                        anchors.verticalCenter: parent.verticalCenter
                        width: 48
                        height: 48
                        Accessible.name: screen.fullScreen ? qsTr("Show the tabs") : qsTr("Hide the tabs")
                        // said on a long press (a phone has no hover), and to a screen reader
                        readonly property string tip: screen.fullScreen ? qsTr("Show the tabs and the link")
                                                                        : qsTr("Hide the tabs: the chart on the whole screen")
                        ToolTip.visible: pressed
                        ToolTip.delay: 500
                        ToolTip.text: tip
                        onClicked: screen.barsAsked(screen.fullScreen)
                        contentItem: Item {
                            PhoneIcon {
                                objectName: "barsIcon"
                                anchors.centerIn: parent
                                kind: screen.fullScreen ? "shrink" : "expand"
                                color: PhoneTheme.text
                            }
                            Rectangle {
                                objectName: "barsLinkDot"
                                visible: screen.fullScreen
                                x: parent.width / 2 + 7
                                y: parent.height / 2 - 13
                                width: 9
                                height: 9
                                radius: 2.25
                                border.width: 1.5
                                border.color: PhoneTheme.bg
                                color: PhoneSession.state === PhoneSession.Connected
                                       ? (PhoneSession.linkTroubled ? PhoneTheme.warn : PhoneTheme.good)
                                       : PhoneSession.state === PhoneSession.Closed ? PhoneTheme.bad
                                       : PhoneSession.state === PhoneSession.Off ? PhoneTheme.muted : PhoneTheme.warn
                            }
                        }
                    }
                    // the buttons show what is chosen (checked, highlighted) and are no toggles: a tap on the one
                    // chosen keeps it chosen
                    // Live / Hold: the view follows now, or stays where it is
                    BarButton {
                        objectName: "liveButton"
                        text: qsTr("Live")
                        checked: chart.live
                        highlighted: chart.live
                        onClicked: chart.live = true
                    }
                    BarButton {
                        objectName: "holdButton"
                        text: qsTr("Hold")
                        checked: !chart.live
                        highlighted: !chart.live
                        onClicked: chart.live = false
                    }
                    // the groups apart
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 9
                        height: 24
                        color: "transparent"
                        Rectangle {
                            anchors.centerIn: parent
                            width: 1
                            height: 24
                            color: PhoneTheme.border
                        }
                    }
                    // the window: its length now (a pinch's too), its menu the three lengths
                    MenuButton {
                        id: windowButton
                        objectName: "windowButton"
                        label: screen.windowText(chart.windowSeconds)
                        Accessible.name: qsTr("Window: %1").arg(label)
                        onClicked: windowMenu.openUnder(windowButton)
                    }
                }
                Row {
                    id: linesGroup
                    spacing: 2
                    height: viewGroup.height
                    // apart from the view's group when beside it (on a row of its own, nothing to set apart)
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 13
                        height: 24
                        color: "transparent"
                        opacity: linesGroup.y === viewGroup.y ? 1 : 0
                        Rectangle {
                            anchors.centerIn: parent
                            width: 1
                            height: 24
                            color: PhoneTheme.border
                        }
                    }
                    // Y: Auto (the values in view) or the manual range, shown here; its sheet chooses (the desktop's
                    // Y combo box)
                    MenuButton {
                        id: yButton
                        objectName: "yButton"
                        label: chart.yAuto ? qsTr("Y auto")
                                           : qsTr("Y %1 to %2").arg(screen.shortNumber(chart.yLow))
                                                               .arg(screen.shortNumber(chart.yHigh))
                        onClicked: ySheet.openFor()
                    }
                }
                Row {
                    id: toolsGroup
                    spacing: 2
                    height: viewGroup.height
                    // A and B on the view, or off it
                    BarButton {
                        objectName: "cursorsButton"
                        text: qsTr("Cursors")
                        checked: chart.cursorsOn
                        highlighted: chart.cursorsOn
                        onClicked: chart.cursorsOn ? chart.clearCursors() : chart.placeCursors()
                    }
                    // the desktop's measurements in a sheet (over the view, or A -> B with the cursors); a tap
                    // again closes it
                    BarButton {
                        objectName: "measureButton"
                        text: qsTr("Measure")
                        visible: chart.lineCount > 0
                        checked: measureSheet.visible
                        highlighted: measureSheet.visible
                        onClicked: measureSheet.visible ? measureSheet.close() : measureSheet.open()
                    }
                    BarButton {
                        objectName: "chartClear"
                        text: qsTr("Clear")
                        visible: chart.lineCount > 0
                        onClicked: chart.clear()
                    }
                    BarButton {
                        id: fastButton
                        objectName: "fastLinesButton"
                        visible: PhoneSession.fastChannels.length > 0 && !screen.demo
                        text: qsTr("Fast lines")
                        onClicked: fastMenu.openUnder(fastButton)
                    }
                }
            }
            // what a finger does on the plot (no hover on a phone: said where it is seen); upright only, the
            // plot's height goes first across (Help says it too)
            Label {
                objectName: "chartHint"
                visible: !screen.wide
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                text: qsTr("Pinch: zoom the time · Drag: look back · Double tap: live")
                color: PhoneTheme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        GridLayout {
            id: plotAndReadouts
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: screen.width > screen.height ? 2 : 1
            rowSpacing: 0
            columnSpacing: 0

        Item {
            id: plotArea
            objectName: "chartArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // time runs left to right in any language (as the desktop's chart, chart_widget.cpp)
            LayoutMirroring.enabled: false
            LayoutMirroring.childrenInherit: true

            ChartItem {
                id: chart
                objectName: "chartItem"
                anchors.fill: parent
                gridColor: PhoneTheme.grid
                frameColor: PhoneTheme.border
                colours: PhoneTheme.series
                // the legend's row over the plot, as the desktop's (its chips never over a line)
                topPad: chart.lineCount > 0 ? screen.legendHeight : 8
            }

            // the legend over the plot's top, as the desktop's: a chip per line (its colour, name, latest value in view
            // and a "▾" for its menu), left to right in every language, whole chips only. When they do not all fit:
            // the first ones that do, and a "+N" chip that opens the Lines sheet (every line): a chip is never cut
            Item {
                id: legendStrip
                objectName: "legendStrip"
                visible: chart.lineCount > 0
                // across the chart's whole width: over the value labels' column too (none is that high), so a narrow
                // phone has room for one more chip. Not under the Measure sheet across (at the window's end: the right,
                // the left in Arabic): the legend keeps to the plot beside it, so no chip lies under its edge cut
                // mid-word ("SURE 0.620 bar"), and the "+N" counts the ones it leaves out
                readonly property real sheetCover: measureSheet.side && measureSheet.visible
                                                   ? measureSheet.width * measureSheet.position : 0
                readonly property real chartLeft: chart.width >= 0 && screen.width >= 0 ? chart.mapToItem(null, 0, 0).x : 0
                readonly property real coverStart: screen.rtl ? Math.max(0, sheetCover - chartLeft) : 0
                readonly property real coverEnd: screen.rtl ? 0
                    : Math.max(0, chartLeft + chart.width
                                  - ((measureSheet.parent ? measureSheet.parent.width : chart.width) - sheetCover))
                x: 8 + (coverStart > 0 ? coverStart + 8 : 0)
                width: Math.max(0, chart.width - 16 - (coverStart > 0 ? coverStart + 8 : 0) - (coverEnd > 0 ? coverEnd + 8 : 0))
                height: screen.legendHeight
                // the chips shown (the first `shown`), set by fit()
                property int shown: 0
                readonly property int hidden: chipRepeater.count - shown
                // the "+N" chip's width for the most it could say: the same whatever N is, so fit() knows it first
                readonly property real moreWidth: Math.max(48, moreMetrics.advanceWidth + 24)
                function fit() {
                    const n = chipRepeater.count
                    let all = 0
                    for (let i = 0; i < n; ++i) {
                        const item = chipRepeater.itemAt(i)
                        all += (item ? item.implicitWidth : 0) + (i > 0 ? chipRow.spacing : 0)
                    }
                    if (all <= width + 0.5) {
                        shown = n
                        return
                    }
                    const room = width - moreWidth - chipRow.spacing
                    let used = 0, k = 0
                    while (k < n) {
                        const item = chipRepeater.itemAt(k)
                        const next = used + (item ? item.implicitWidth : 0) + (k > 0 ? chipRow.spacing : 0)
                        if (next > room + 0.5) break
                        used = next
                        ++k
                    }
                    shown = k
                }
                onWidthChanged: Qt.callLater(fit)
                TextMetrics {
                    id: moreMetrics
                    font.pixelSize: 13
                    font.bold: true
                    text: "+" + chipRepeater.count
                }

                Row {
                    id: chipRow
                    objectName: "legendRow"
                    spacing: 6
                    LayoutMirroring.enabled: false
                    LayoutMirroring.childrenInherit: true
                    Repeater {
                        id: chipRepeater
                        model: chart.legend
                        onCountChanged: Qt.callLater(legendStrip.fit)
                        // a chip: drawn 28 dp high (its face), 48 dp to the touch; the "▾" at its end a button of
                        // its own (48 x 48), set apart by a thin line as a split button
                        delegate: Item {
                            id: chip
                            required property int index
                            required property int lineId
                            required property string name
                            required property string value
                            required property string unit
                            required property color colour
                            required property bool shown
                            objectName: "legendChip"
                            visible: index < legendStrip.shown
                            // the value's room: from the start the widest the legend writes but its rare exponents
                            // (ChartData::number: five figures and a sign, "-12345", "-0.123", in figures of one
                            // width) with the unit, and it only grows: a live value that gets longer does not widen
                            // the chip, so the chips shown change only with the lines or the width (the owner's
                            // phone: four chips, then three and "+1" as a value grew, and back)
                            property real valueRoom: 0
                            readonly property real valueWidth: Math.max(valueRoom, Math.ceil(valueMetrics.advanceWidth))
                            TextMetrics {
                                id: valueMetrics
                                font: chipValue.font
                                text: "-00000" + (chip.unit.length > 0 ? " " + chip.unit : "")
                            }
                            implicitWidth: 10 + 10 + 6 + chipName.implicitWidth + 6 + valueWidth
                                           + 6 + chipArrowRoom
                            readonly property real chipArrowRoom: 28
                            width: implicitWidth
                            height: 48
                            onImplicitWidthChanged: Qt.callLater(legendStrip.fit)

                            Rectangle {
                                objectName: "chipFace"
                                y: 3
                                width: parent.width
                                height: 28
                                radius: 8
                                color: chipToggle.pressed ? PhoneTheme.surface2 : PhoneTheme.surface
                                // a hidden line's chip: its border and text muted, its dot hollow
                                border.width: 1
                                border.color: chip.shown ? chip.colour : PhoneTheme.border
                                // the menu's part apart
                                Rectangle {
                                    objectName: "chipSplit"
                                    x: parent.width - chip.chipArrowRoom
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 1
                                    height: 16
                                    color: PhoneTheme.border
                                }
                            }
                            // a tap on the chip (its "▾" apart, over it) hides or shows its line: a button, so every
                            // tap counts (two quick taps are two, not a double tap)
                            AbstractButton {
                                id: chipToggle
                                objectName: "chipToggle"
                                anchors.fill: parent
                                Accessible.name: chip.shown ? qsTr("Hide %1").arg(chip.name)
                                                            : qsTr("Show %1").arg(chip.name)
                                onClicked: chart.setLineShown(chip.lineId, !chip.shown)
                            }
                            Rectangle {
                                objectName: "chipDot"
                                x: 10
                                y: 3 + 14 - 5
                                width: 10
                                height: 10
                                radius: 2.5
                                color: chip.shown ? chip.colour : "transparent"
                                border.width: 2
                                border.color: chip.colour
                            }
                            Text {
                                id: chipName
                                objectName: "chipName"
                                x: 26
                                y: 3 + 14 - height / 2
                                text: chip.name
                                color: chip.shown ? PhoneTheme.text : PhoneTheme.muted
                                font.pixelSize: 13
                                font.strikeout: !chip.shown
                            }
                            Text {
                                id: chipValue
                                objectName: "chipValue"
                                x: chipName.x + chipName.implicitWidth + 6
                                y: 3 + 14 - height / 2
                                width: chip.valueWidth
                                horizontalAlignment: Text.AlignRight
                                text: chip.value
                                visible: text.length > 0
                                color: chip.shown ? PhoneTheme.text : PhoneTheme.muted
                                font.pixelSize: 13
                                font.bold: true
                                font.features: { "tnum": 1 }
                                onImplicitWidthChanged: if (implicitWidth > chip.valueRoom) chip.valueRoom = Math.ceil(implicitWidth)
                                Component.onCompleted: chip.valueRoom = Math.ceil(implicitWidth)
                            }
                            // the chip's menu: a drawn chevron at its end, 48 x 48 to the touch, highlighted pressed
                            AbstractButton {
                                id: chipMore
                                objectName: "chipMore"
                                x: parent.width - 48
                                width: 48
                                height: 48
                                Accessible.name: qsTr("More: Hide, Take off the chart")
                                // said on a long press (a phone has no hover), and to a screen reader
                                ToolTip.visible: pressed
                                ToolTip.delay: 500
                                ToolTip.text: Accessible.name
                                onClicked: chipMenu.openFor(chip.lineId, chip.name, chip.shown, chipMore)
                                Rectangle {
                                    x: 48 - chip.chipArrowRoom
                                    y: 4
                                    width: chip.chipArrowRoom - 1
                                    height: 26
                                    topRightRadius: 7
                                    bottomRightRadius: 7
                                    color: chipMore.pressed ? PhoneTheme.border : "transparent"
                                }
                                PhoneIcon {
                                    objectName: "chipArrow"
                                    x: 48 - chip.chipArrowRoom + (chip.chipArrowRoom - 14) / 2 - 1
                                    y: 3 + 14 - 7
                                    kind: "down"
                                    size: 14
                                    color: PhoneTheme.text
                                }
                            }
                        }
                    }
                    // more lines than room: "+N", every line in the Lines sheet
                    AbstractButton {
                        id: legendMore
                        objectName: "legendMore"
                        visible: legendStrip.hidden > 0
                        width: legendStrip.moreWidth
                        height: 48
                        Accessible.name: qsTr("All lines: %1 more").arg(legendStrip.hidden)
                        // said on a long press (a phone has no hover), and to a screen reader
                        ToolTip.visible: pressed
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("All the lines, with their values")
                        onClicked: linesSheet.open()
                        Rectangle {
                            objectName: "legendMoreFace"
                            y: 3
                            width: parent.width
                            height: 28
                            radius: 8
                            color: legendMore.pressed ? PhoneTheme.border : PhoneTheme.surface2
                            border.width: 1
                            border.color: PhoneTheme.control
                            Text {
                                objectName: "legendMoreText"
                                anchors.centerIn: parent
                                text: "+" + legendStrip.hidden
                                color: PhoneTheme.text
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }
                    }
                }
            }
            // the value labels left of the plot, the time labels under it: Text over the item, kept inside the area
            Repeater {
                model: chart.valueTicks
                Text {
                    required property real at
                    required property string label
                    objectName: "valueLabel"
                    text: label
                    x: chart.plotRect.x - width - 6
                    y: at - height / 2
                    // under the legend's chips (their faces end 3 dp above the plot), never over one
                    visible: x >= 0 && y >= (legendStrip.visible ? legendStrip.height - 3 : 0) && y + height <= plotArea.height
                    color: PhoneTheme.muted
                    font.pixelSize: 11
                }
            }
            Repeater {
                model: chart.timeTicks
                Text {
                    required property real at
                    required property string label
                    objectName: "timeLabel"
                    text: label
                    x: at - width / 2
                    y: chart.plotRect.y + chart.plotRect.height + 4
                    visible: x >= 0 && x + width <= plotArea.width
                    color: PhoneTheme.muted
                    font.pixelSize: 11
                }
            }

            // two fingers: the time zoomed by their spread, around the view's end
            PinchHandler {
                id: pinch
                objectName: "chartPinch"
                target: null
                property real lastScale: 1
                onActiveChanged: lastScale = 1
                onActiveScaleChanged: {
                    if (!active) return
                    chart.zoomTime(activeScale / lastScale)
                    lastScale = activeScale
                }
            }
            // one finger: the time dragged along (to the right: older), the view held
            DragHandler {
                id: drag
                objectName: "chartDrag"
                target: null
                minimumPointCount: 1
                maximumPointCount: 1
                yAxis.enabled: false
                property real lastX: 0
                // a cursor's handle keeps its finger: the view is not taken from it, nor moved under it. While the
                // Measure sheet is open it takes nothing from an item either: the sheet lies over the plot, and the
                // drag took the finger from its columns (they never scrolled, the chart panned and held)
                grabPermissions: measureSheet.opened ? PointerHandler.ApprovesTakeOverByAnything
                                                     : PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything
                onActiveChanged: lastX = 0
                onActiveTranslationChanged: {
                    if (!active || screen.cursorDragging) return
                    chart.panPixels(activeTranslation.x - lastX)
                    lastX = activeTranslation.x
                }
            }
            TapHandler {
                objectName: "chartTap"
                onDoubleTapped: chart.live = true
            }

            // the cursors: a line over the plot and a handle to drag it, a rounded square with the cursor's letter (A's at
            // the top, B's at the foot), 48 dp to the touch
            Repeater {
                id: cursorRepeater
                model: 2
                Item {
                    id: cursor
                    readonly property Item handle: cursorHandle
                    required property int index
                    readonly property real cx: index === 0 ? chart.cursorAX : chart.cursorBX
                    objectName: index === 0 ? "cursorA" : "cursorB"
                    visible: chart.cursorsOn && !isNaN(cx)
                    x: (isNaN(cx) ? 0 : cx) - 24
                    width: 48
                    height: plotArea.height
                    Rectangle {
                        x: 24 - 0.75
                        y: chart.plotRect.y
                        width: 1.5
                        height: chart.plotRect.height
                        color: PhoneTheme.accent
                    }
                    Item {
                        id: cursorHandle
                        objectName: cursor.index === 0 ? "cursorHandleA" : "cursorHandleB"
                        width: 48
                        height: 48
                        y: cursor.index === 0 ? chart.plotRect.y : chart.plotRect.y + chart.plotRect.height - 48
                        Rectangle {
                            anchors.centerIn: parent
                            objectName: "cursorFace"
                            width: 28
                            height: 28
                            radius: 7
                            color: PhoneTheme.accent
                            border.width: 2
                            border.color: PhoneTheme.bg
                            Text {
                                objectName: "cursorName"
                                anchors.centerIn: parent
                                text: cursor.index === 0 ? "A" : "B"
                                color: PhoneTheme.bg
                                font.pixelSize: 14
                                font.bold: true
                            }
                        }
                        // a drag moves the cursor along the time, inside the plot (not the view: the chart's own drag);
                        // the finger's place in the chart, less where it took the handle: the cursor does not jump
                        DragHandler {
                            id: cursorDrag
                            objectName: cursor.index === 0 ? "cursorDragA" : "cursorDragB"
                            target: null
                            yAxis.enabled: false
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            property real offset: 0
                            function fingerX() {
                                return chart.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y).x
                            }
                            onActiveChanged: {
                                screen.cursorDragging = active
                                // from where the finger came down: the drag's threshold is not lost
                                if (active) offset = chart.mapFromItem(null, centroid.scenePressPosition.x,
                                                                       centroid.scenePressPosition.y).x - cursor.cx
                            }
                            onCentroidChanged: if (active) chart.moveCursor(cursor.index, fingerX() - offset)
                        }
                    }
                }
            }

            // no line: the chart's icon, how to plot one, and a button to the Registers screen; on a panel inside the
            // plot, so no text lies over the grid and the axes' labels stay whole
            Rectangle {
                id: empty
                objectName: "chartEmpty"
                visible: chart.lineCount === 0 && !screen.demo
                // the text reads in the screen's language (the plot under it does not mirror)
                LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
                LayoutMirroring.childrenInherit: true
                width: Math.min(chart.plotRect.width - 24, 340)
                // across, the plot is low: less padding and a smaller icon
                readonly property real pad: chart.plotRect.height >= 300 ? 20 : 12
                height: Math.min(emptyState.implicitHeight + 2 * pad, chart.plotRect.height - 16)
                x: chart.plotRect.x + (chart.plotRect.width - width) / 2
                y: chart.plotRect.y + (chart.plotRect.height - height) / 2
                radius: 12
                color: PhoneTheme.surface
                border.color: PhoneTheme.border
                EmptyState {
                    id: emptyState
                    prefix: "chartEmpty"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: empty.pad
                    icon: "chart"
                    iconSize: chart.plotRect.height >= 300 ? 48 : 32
                    title: qsTr("No lines yet")
                    text: PhoneSession.fastChannels.length > 0
                          ? qsTr("Tap a register on the Registers screen, then Plot. Or choose a stream's channel under Fast lines.")
                          : qsTr("Tap a register on the Registers screen, then Plot.")
                    action: qsTr("Go to Registers")
                    onActed: screen.registersAsked()
                }
            }
        }

        // the cursors' readouts: the times, then per line its values at A and B and over A..B
        Rectangle {
            id: readoutPanel
            objectName: "readoutPanel"
            visible: chart.cursorsOn
            readonly property bool beside: plotAndReadouts.columns === 2
            Layout.fillWidth: !beside
            Layout.fillHeight: beside
            Layout.preferredWidth: beside ? 290 : -1
            Layout.preferredHeight: beside ? -1 : Math.min(readoutColumn.implicitHeight + 16, screen.height * 0.4)
            color: PhoneTheme.surface
            // the panel apart from the plot
            Rectangle {
                anchors.left: parent.left
                anchors.right: readoutPanel.beside ? undefined : parent.right
                anchors.top: parent.top
                anchors.bottom: readoutPanel.beside ? parent.bottom : undefined
                width: 1
                height: 1
                color: PhoneTheme.border
            }
            Flickable {
                id: readoutFlick
                objectName: "readoutFlick"
                anchors.fill: parent
                anchors.margins: 8
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                clip: true
                contentHeight: readoutColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                // more rows than room: a scroll bar that stays (not only while moving), and a fade over each edge
                // that has more, so a row cut by the edge reads as more to scroll, not as a cut text
                readonly property bool overflows: contentHeight > height + 0.5
                ScrollBar.vertical: readoutBar
                ColumnLayout {
                    id: readoutColumn
                    width: readoutFlick.width
                    spacing: 6
                    Flow {
                        objectName: "readoutTimes"
                        Layout.fillWidth: true
                        spacing: 2
                        Pair { label: "A"; symbol: true; labelName: "cursorName"; valueName: "readoutTimeA"; value: chart.cursorAText }
                        Pair { label: "B"; symbol: true; labelName: "cursorName"; valueName: "readoutTimeB"; value: chart.cursorBText }
                        Pair { label: "B \u2212 A"; symbol: true; labelName: "cursorName"; valueName: "readoutSpan"; value: chart.cursorSpanText }
                    }
                    Repeater {
                        model: chart.readouts
                        ColumnLayout {
                            id: readout
                            required property int lineId
                            required property string name
                            required property color colour
                            required property string atA
                            required property string atB
                            required property string delta
                            required property string min
                            required property string max
                            required property string mean
                            // no sample of the line at A, at B or between them (the cursors left where Clear took the
                            // samples away): one muted line says so, not six dashes in a row
                            readonly property bool empty: [atA, atB, delta, min, max, mean].every(t => t === "—")
                            objectName: "readoutRow"
                            Layout.fillWidth: true
                            spacing: 2
                            // the lines apart
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 1
                                color: PhoneTheme.border
                                opacity: 0.6
                            }
                            Row {
                                spacing: 6
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 10
                                    radius: 2.5
                                    color: readout.colour
                                }
                                Text {
                                    objectName: "readoutName"
                                    text: readout.name
                                    color: PhoneTheme.text
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }
                            Text {
                                objectName: "readoutEmpty"
                                Layout.fillWidth: true
                                visible: readout.empty
                                text: qsTr("No samples between A and B")
                                color: PhoneTheme.muted
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                            }
                            Flow {
                                Layout.fillWidth: true
                                visible: !readout.empty
                                spacing: 2
                                Pair { label: "A"; symbol: true; labelName: "cursorName"; valueName: "readoutA"; value: readout.atA }
                                Pair { label: "B"; symbol: true; labelName: "cursorName"; valueName: "readoutB"; value: readout.atB }
                                Pair { label: "B \u2212 A"; symbol: true; labelName: "cursorName"; valueName: "readoutDelta"; value: readout.delta }
                                Pair { label: qsTr("min"); valueName: "readoutMin"; value: readout.min }
                                Pair { label: qsTr("max"); valueName: "readoutMax"; value: readout.max }
                                Pair { label: qsTr("mean"); valueName: "readoutMean"; value: readout.mean }
                            }
                        }
                    }
                }
            }
            ScrollBar {
                id: readoutBar
                objectName: "readoutBar"
                anchors.top: readoutFlick.top
                anchors.bottom: readoutFlick.bottom
                anchors.right: parent.right
                anchors.rightMargin: 2
                policy: readoutFlick.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            Repeater {
                model: 2 // the top's fade (more above) and the foot's (more below)
                Rectangle {
                    id: fade
                    required property int index
                    readonly property bool foot: index === 1
                    readonly property color clear: Qt.rgba(PhoneTheme.surface.r, PhoneTheme.surface.g,
                                                           PhoneTheme.surface.b, 0)
                    objectName: foot ? "readoutFadeFoot" : "readoutFadeTop"
                    anchors.left: readoutFlick.left
                    anchors.right: readoutFlick.right
                    y: foot ? readoutFlick.y + readoutFlick.height - height : readoutFlick.y
                    height: 22
                    visible: readoutFlick.overflows && (foot ? !readoutFlick.atYEnd : !readoutFlick.atYBeginning)
                    gradient: Gradient {
                        GradientStop { position: 0; color: fade.foot ? fade.clear : PhoneTheme.surface }
                        GradientStop { position: 1; color: fade.foot ? PhoneTheme.surface : fade.clear }
                    }
                }
            }
        }
        }
    }

    // a chip's "..." menu (opened from the legend, which is never mirrored: its items read in the screen's language)
    PhoneMenu {
        id: chipMenu
        objectName: "chipMenu"
        property int lineId: -1
        property string name: ""
        property bool shown: true
        function openFor(id, name, shown, from) {
            chipMenu.lineId = id
            chipMenu.name = name
            chipMenu.shown = shown
            chipMenu.openUnder(from)
        }
        MenuItem {
            objectName: "chipMenuShow"
            LayoutMirroring.enabled: chipMenu.rtl
            text: chipMenu.shown ? qsTr("Hide") : qsTr("Show")
            onTriggered: chart.setLineShown(chipMenu.lineId, !chipMenu.shown)
        }
        MenuItem {
            objectName: "chipMenuUnplot"
            LayoutMirroring.enabled: chipMenu.rtl
            text: qsTr("Take off the chart")
            onTriggered: {
                PhoneSession.unplotLine(chipMenu.lineId)
                screen.showNotice(qsTr("%1 off the chart").arg(chipMenu.name))
            }
        }
    }

    // a sheet's face, as the frame's sheet (FrameSheet.qml): rounded at the side it comes from, a handle
    component SheetFace: Rectangle {
        property bool side: false
        color: PhoneTheme.surface
        topLeftRadius: side ? 0 : 16
        topRightRadius: side ? 0 : 16
        border.color: PhoneTheme.border
        Rectangle {
            visible: !parent.side
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
            width: 32
            height: 4
            radius: 1
            color: PhoneTheme.control
        }
    }
    // a sheet's title and its close cross (the words in the language's direction)
    component SheetTitle: RowLayout {
        id: sheetTitle
        property alias text: titleLabel.text
        property string prefix
        signal closeAsked()
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 4
        spacing: 8
        Label {
            id: titleLabel
            objectName: sheetTitle.prefix + "Title"
            Layout.fillWidth: true
            color: PhoneTheme.text
            font.pixelSize: 17
            font.bold: true
            wrapMode: Text.Wrap
        }
        ToolButton {
            objectName: sheetTitle.prefix + "Close"
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
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            Accessible.name: qsTr("Close")
            onClicked: sheetTitle.closeAsked()
            contentItem: Item {
                PhoneIcon {
                    anchors.centerIn: parent
                    kind: "clear"
                    color: PhoneTheme.text
                }
            }
        }
    }

    // every line, from the legend's "+N": its colour, name and value, a switch to show it, and its menu (the chip's)
    Drawer {
        id: linesSheet
        objectName: "linesSheet"
        edge: Qt.BottomEdge
        interactive: opened
        width: parent ? parent.width : screen.width
        height: Math.min(linesColumn.implicitHeight + 24 + 8, parent ? parent.height - 24 : 600)
        enter: Transition {
            NumberAnimation { duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
        }
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }
        background: SheetFace {}
        Flickable {
            id: linesFlick
            objectName: "linesFlick"
            anchors.fill: parent
            anchors.topMargin: 24
            contentHeight: linesColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {
                policy: linesFlick.contentHeight > linesFlick.height + 0.5 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            ColumnLayout {
                id: linesColumn
                width: linesFlick.width
                spacing: 0
                LayoutMirroring.enabled: screen.rtl
                LayoutMirroring.childrenInherit: true
                SheetTitle {
                    prefix: "linesSheet"
                    text: qsTr("Lines: %1").arg(chart.lineCount)
                    onCloseAsked: linesSheet.close()
                }
                Repeater {
                    model: chart.legend
                    delegate: RowLayout {
                        id: lineRow
                        required property int lineId
                        required property string name
                        required property string value
                        required property color colour
                        required property bool shown
                        objectName: "linesSheetRow"
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 4
                        Layout.preferredHeight: 48
                        spacing: 10
                        Rectangle {
                            objectName: "linesSheetDot"
                            width: 12
                            height: 12
                            radius: 3
                            color: lineRow.shown ? lineRow.colour : "transparent"
                            border.width: 2
                            border.color: lineRow.colour
                        }
                        // the line's name and value as the chip writes them (left to right in every language)
                        Text {
                            objectName: "linesSheetName"
                            Layout.fillWidth: true
                            // at the start edge, by the dot (a Latin name alone sat at the left of its room in Arabic,
                            // in the row's middle)
                            horizontalAlignment: Text.AlignLeft
                            text: lineRow.name
                            color: lineRow.shown ? PhoneTheme.text : PhoneTheme.muted
                            font.pixelSize: 15
                            font.strikeout: !lineRow.shown
                            elide: Text.ElideRight
                        }
                        Text {
                            objectName: "linesSheetValue"
                            text: lineRow.value
                            color: lineRow.shown ? PhoneTheme.text : PhoneTheme.muted
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Switch {
                            objectName: "linesSheetShown"
                            checked: lineRow.shown
                            Accessible.name: lineRow.shown ? qsTr("Hide %1").arg(lineRow.name) : qsTr("Show %1").arg(lineRow.name)
                            onToggled: chart.setLineShown(lineRow.lineId, checked)
                        }
                        ToolButton {
                            id: lineMore
                            objectName: "linesSheetMore"
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
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 48
                            Accessible.name: qsTr("More: Hide, Take off the chart")
                            onClicked: chipMenu.openFor(lineRow.lineId, lineRow.name, lineRow.shown, lineMore)
                            contentItem: Item {
                                PhoneIcon {
                                    anchors.centerIn: parent
                                    kind: "down"
                                    size: 16
                                    color: PhoneTheme.text
                                }
                            }
                        }
                    }
                }
                Item { implicitHeight: 8 }
            }
        }
    }

    // the desktop's measurements (its table under the chart): per line min, max, mean, RMS, std dev and peak-peak over
    // the view, or over A -> B with both cursors placed (the title says which), at most four times a second
    // (ChartItem::measureRows). Not modal: the chart stays live and its cursors can be dragged beside it. Upright at the
    // screen's foot, across at its end; the columns scroll sideways under the names, which stay
    Drawer {
        id: measureSheet
        objectName: "measureSheet"
        readonly property bool side: screen.wide
        edge: side ? (screen.rtl ? Qt.LeftEdge : Qt.RightEdge) : Qt.BottomEdge
        modal: false
        dim: false
        // upright it is pulled down to close; across a sideways drag is the columns' (an interactive side drawer took it
        // to drag itself, so the columns never scrolled and the chart beside panned and held): there its cross closes it
        interactive: opened && !side
        closePolicy: Popup.CloseOnEscape
        width: side ? Math.min(460, (parent ? parent.width : 760) * 0.62) : (parent ? parent.width : 400)
        // across, under the controls' row: Cursors, Hold and the rest stay in reach beside it
        readonly property real underControls: topRows.mapToItem(null, 0, topRows.height).y
        height: side ? (parent ? parent.height : 384) - underControls
                     : Math.min(measureColumn.implicitHeight + 24 + 8, (parent ? parent.height : 800) * 0.5)
        Binding on y {
            when: measureSheet.side
            value: measureSheet.underControls
        }
        onAboutToShow: {
            measureTable.widths = [0, 0, 0, 0, 0, 0]
            chart.measuring = true
        }
        onClosed: chart.measuring = false
        enter: Transition {
            NumberAnimation { duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
        }
        background: SheetFace { side: measureSheet.side }
        Flickable {
            id: measureFlick
            objectName: "measureFlick"
            anchors.fill: parent
            anchors.topMargin: measureSheet.side ? 8 : 24
            contentHeight: measureColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {
                policy: measureFlick.contentHeight > measureFlick.height + 0.5 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            ColumnLayout {
                id: measureColumn
                width: measureFlick.width
                spacing: 4
                LayoutMirroring.enabled: screen.rtl
                LayoutMirroring.childrenInherit: true
                SheetTitle {
                    prefix: "measure"
                    // the range's length a number and its unit: one piece left to right in every language
                    text: chart.measureOverCursors
                          ? qsTr("Measure: A to B (%1)").arg("\u2066" + screen.windowText(chart.measureSeconds) + "\u2069")
                          : qsTr("Measure: the view (%1)").arg("\u2066" + screen.windowText(chart.measureSeconds) + "\u2069")
                    onCloseAsked: measureSheet.close()
                }
                Label {
                    objectName: "measureNote"
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    text: chart.measureOverCursors ? qsTr("Between the cursors: drag A or B to change the range.")
                                                   : qsTr("Over the time in view. Place the cursors to measure between them.")
                    color: PhoneTheme.muted
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
                // the table: the names' column, then the values' columns in a view of their own that scrolls sideways
                Item {
                    id: measureTable
                    objectName: "measureTable"
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    // clear of the sheet's scroll bar, which stays when the rows are taller than the sheet
                    Layout.rightMargin: 16
                    Layout.topMargin: 4
                    Layout.bottomMargin: 12
                    implicitHeight: headerHeight + measureRepeater.count * rowHeight + 10
                    readonly property real headerHeight: 30
                    readonly property real rowHeight: 36
                    readonly property var heads: [qsTr("Min"), qsTr("Max"), qsTr("Mean"), qsTr("RMS"), qsTr("Std dev"),
                                                  qsTr("Peak-peak")]
                    // each column's width: its widest text (head or value) and a gap, grown only while the sheet is
                    // open, so the columns do not shift as the values change
                    property var widths: [0, 0, 0, 0, 0, 0]
                    property real nameWidth: 0
                    function measureWidths() {
                        let names = nameHead.implicitWidth + 8
                        for (let r = 0; r < nameRepeater.count; ++r) {
                            const cell = nameRepeater.itemAt(r)
                            if (cell) names = Math.max(names, cell.implicitWidth)
                        }
                        if (names !== nameWidth) nameWidth = names
                        const w = widths.slice()
                        for (let c = 0; c < 6; ++c) {
                            const head = headRepeater.itemAt(c)
                            let most = head ? head.implicitWidth : 0
                            for (let r = 0; r < measureRepeater.count; ++r) {
                                const row = measureRepeater.itemAt(r)
                                const cell = row ? row.cell(c) : null
                                if (cell) most = Math.max(most, cell.implicitWidth)
                            }
                            w[c] = Math.max(w[c], Math.ceil(most) + 18)
                        }
                        let changed = false
                        for (let c = 0; c < 6; ++c) changed = changed || w[c] !== widths[c]
                        if (changed) widths = w
                    }
                    Connections {
                        target: chart
                        function onMeasured() { Qt.callLater(measureTable.measureWidths) }
                    }
                    // the names: the column that stays
                    Column {
                        id: nameColumn
                        objectName: "measureNames"
                        anchors.left: parent.left
                        width: Math.min(Math.max(measureTable.nameWidth, 64), measureTable.width * 0.42)
                        Text {
                            id: nameHead
                            objectName: "measureHeadName"
                            // over the names, at their start (the right in Arabic)
                            width: nameColumn.width
                            horizontalAlignment: Text.AlignLeft
                            height: measureTable.headerHeight
                            verticalAlignment: Text.AlignVCenter
                            text: qsTr("Line")
                            color: PhoneTheme.muted
                            font.pixelSize: 12
                            font.bold: true
                        }
                        Repeater {
                            id: nameRepeater
                            model: chart.measureRows
                            delegate: Item {
                                id: nameCell
                                required property string name
                                required property color colour
                                objectName: "measureNameCell"
                                width: nameColumn.width
                                implicitWidth: 16 + measureName.implicitWidth + 12
                                height: measureTable.rowHeight
                                Rectangle {
                                    id: nameDot
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 10
                                    radius: 2.5
                                    color: nameCell.colour
                                }
                                Text {
                                    id: measureName
                                    objectName: "measureName"
                                    anchors.left: nameDot.right
                                    anchors.leftMargin: 6
                                    width: parent.width - 16 - 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: nameCell.name
                                    color: PhoneTheme.text
                                    font.pixelSize: 13
                                    font.bold: true
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignLeft
                                }
                            }
                        }
                    }
                    Flickable {
                        id: valuesFlick
                        objectName: "measureValues"
                        anchors.left: nameColumn.right
                        anchors.right: parent.right
                        height: parent.height
                        contentWidth: valuesColumn.width
                        contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true
                        readonly property bool overflows: contentWidth > width + 0.5
                        // in Arabic the first column is at the right: the view starts there
                        onContentWidthChanged: if (screen.rtl && !moving) contentX = Math.max(0, contentWidth - width)
                        ScrollBar.horizontal: ScrollBar {
                            objectName: "measureScrollBar"
                            policy: valuesFlick.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        }
                        Column {
                            id: valuesColumn
                            Row {
                                objectName: "measureHeads"
                                Repeater {
                                    id: headRepeater
                                    model: 6
                                    Text {
                                        required property int index
                                        objectName: "measureHead"
                                        width: measureTable.widths[index]
                                        height: measureTable.headerHeight
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: Text.AlignRight
                                        text: measureTable.heads[index]
                                        color: PhoneTheme.muted
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                }
                            }
                            Repeater {
                                id: measureRepeater
                                model: chart.measureRows
                                delegate: Row {
                                    id: valueRow
                                    required property string min
                                    required property string max
                                    required property string mean
                                    required property string rms
                                    required property string std
                                    required property string p2p
                                    objectName: "measureRow"
                                    readonly property var texts: [min, max, mean, rms, std, p2p]
                                    function cell(c) { return cellRepeater.itemAt(c) }
                                    Repeater {
                                        id: cellRepeater
                                        model: 6
                                        Text {
                                            required property int index
                                            objectName: ["measureMin", "measureMax", "measureMean", "measureRms",
                                                         "measureStd", "measureP2p"][index]
                                            width: measureTable.widths[index]
                                            height: measureTable.rowHeight
                                            verticalAlignment: Text.AlignVCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: valueRow.texts[index]
                                            color: PhoneTheme.text
                                            font.pixelSize: 13
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // the rows apart, across both parts
                    Repeater {
                        model: measureRepeater.count
                        Rectangle {
                            required property int index
                            x: 0
                            y: measureTable.headerHeight + index * measureTable.rowHeight
                            width: measureTable.width
                            height: 1
                            color: PhoneTheme.border
                            opacity: 0.6
                        }
                    }
                    // more columns than room: a fade over the values' edge with more
                    Repeater {
                        model: 2 // the start's fade (scrolled on) and the end's (more to come)
                        Rectangle {
                            id: columnFade
                            required property int index
                            readonly property bool end: index === 1
                            readonly property bool onRight: end !== screen.rtl
                            readonly property color clear: Qt.rgba(PhoneTheme.surface.r, PhoneTheme.surface.g,
                                                                   PhoneTheme.surface.b, 0)
                            objectName: end ? "measureFadeEnd" : "measureFadeStart"
                            x: onRight ? valuesFlick.x + valuesFlick.width - width : valuesFlick.x
                            width: 24
                            height: measureTable.headerHeight + measureRepeater.count * measureTable.rowHeight
                            visible: valuesFlick.overflows && (end ? (screen.rtl ? !valuesFlick.atXBeginning : !valuesFlick.atXEnd)
                                                                   : (screen.rtl ? !valuesFlick.atXEnd : !valuesFlick.atXBeginning))
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: columnFade.onRight ? columnFade.clear : PhoneTheme.surface }
                                GradientStop { position: 1; color: columnFade.onRight ? PhoneTheme.surface : columnFade.clear }
                            }
                        }
                    }
                }
            }
        }
    }

    // the window's lengths, the one shown checked (none after a pinch to another)
    PhoneMenu {
        id: windowMenu
        objectName: "windowMenu"
        Instantiator {
            model: screen.windows.length
            delegate: MenuItem {
                required property int index
                objectName: "window" + screen.windows[index]
                LayoutMirroring.enabled: windowMenu.rtl
                text: screen.windowNames[index]
                checkable: true
                checked: Math.abs(chart.windowSeconds - screen.windows[index]) < 1e-6
                onTriggered: {
                    chart.windowSeconds = screen.windows[index]
                    checked = Qt.binding(() => Math.abs(chart.windowSeconds - screen.windows[index]) < 1e-6)
                }
            }
            onObjectAdded: (index, object) => windowMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => windowMenu.removeItem(object)
        }
    }

    // the map's streams' channels, each on the chart or not (its stream switched on with its first channel)
    PhoneMenu {
        id: fastMenu
        objectName: "fastMenu"
        Instantiator {
            model: PhoneSession.fastChannels
            delegate: MenuItem {
                required property var modelData
                objectName: "fastChannel"
                LayoutMirroring.enabled: fastMenu.rtl
                text: modelData.unit.length > 0 ? modelData.name + " (" + modelData.unit + ")" : modelData.name
                checkable: true
                checked: modelData.plotted
                onTriggered: PhoneSession.setFastPlot(modelData.stream, modelData.channel, !modelData.plotted)
            }
            onObjectAdded: (index, object) => fastMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => fastMenu.removeItem(object)
        }
    }

    // the manual Y range: two fields, near the top (the soft keyboard comes up from the foot)
    Popup {
        id: ySheet
        objectName: "ySheet"
        modal: true
        // the screen behind darkened as behind the drawer (Material's grey wash lightened the dark theme)
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }
        x: (screen.width - width) / 2
        y: 8
        width: Math.min(screen.width - 32, 360)
        padding: 16
        // down from the top edge, where it stays; none when Android's animations are removed
        enter: Transition {
            NumberAnimation { property: "y"; from: -ySheet.height; to: 8; duration: PhoneSession.motion ? 220 : 0; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: PhoneSession.motion ? 150 : 0 }
        }
        exit: Transition {
            NumberAnimation { property: "y"; from: 8; to: -ySheet.height; duration: PhoneSession.motion ? 180 : 0; easing.type: Easing.InCubic }
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: PhoneSession.motion ? 180 : 0 }
        }
        property string error: ""
        function openFor() {
            minField.text = screen.shortNumber(chart.yLow)
            maxField.text = screen.shortNumber(chart.yHigh)
            ySheet.error = ""
            ySheet.open()
        }
        function apply() {
            const loText = minField.text.trim().replace(",", "."), hiText = maxField.text.trim().replace(",", ".")
            const lo = Number(loText), hi = Number(hiText)
            if (loText.length === 0 || hiText.length === 0 || !isFinite(lo) || !isFinite(hi)) {
                ySheet.error = qsTr("Type a number in both fields.")
                return
            }
            if (!chart.setYRange(lo, hi)) {
                ySheet.error = qsTr("Max must be above min.")
                return
            }
            ySheet.close()
        }
        background: Rectangle {
            color: PhoneTheme.surface
            border.color: PhoneTheme.border
            radius: 12
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8
            Label {
                objectName: "ySheetTitle"
                text: qsTr("Y range")
                color: PhoneTheme.text
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Auto follows the values in view. Or type a range: the value axis stays between these.")
                color: PhoneTheme.muted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                Label {
                    text: qsTr("Min")
                    color: PhoneTheme.text
                    font.pixelSize: 15
                }
                TextField {
                    id: minField
                    objectName: "yMinField"
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    LayoutMirroring.enabled: false
                    onAccepted: maxField.forceActiveFocus()
                }
                Label {
                    text: qsTr("Max")
                    color: PhoneTheme.text
                    font.pixelSize: 15
                }
                TextField {
                    id: maxField
                    objectName: "yMaxField"
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    LayoutMirroring.enabled: false
                    onAccepted: ySheet.apply()
                }
            }
            Label {
                objectName: "ySheetError"
                Layout.fillWidth: true
                visible: ySheet.error.length > 0
                text: ySheet.error
                color: PhoneTheme.bad
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                // Auto: the values in view again, at once
                Button {
                    objectName: "ySheetAuto"
                    Material.roundedScale: Material.SmallScale
                    text: qsTr("Auto")
                    flat: true
                    highlighted: chart.yAuto
                    onClicked: {
                        chart.yAuto = true
                        ySheet.close()
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "ySheetCancel"
                    Material.roundedScale: Material.SmallScale
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: ySheet.close()
                }
                Button {
                    objectName: "ySheetApply"
                    Material.roundedScale: Material.SmallScale
                    text: qsTr("Apply")
                    highlighted: true
                    onClicked: ySheet.apply()
                }
            }
        }
    }

    // what Take off the chart or a refused fast line did, a moment over the plot's foot
    Rectangle {
        id: notice
        objectName: "chartNotice"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
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
            objectName: "chartNoticeText"
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
        enabled: screen.visible
        function onNotice(text) { screen.showNotice(text) }
    }

    ChartDemo {
        objectName: "chartDemo"
        chart: chart
        running: screen.demo
        colours: PhoneTheme.series
    }
}

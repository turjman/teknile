// SPDX-License-Identifier: Apache-2.0
// The Chart screen (STUDIO.md 25.6): the plotted lines over time, drawn by the GPU from nodes of the chart's own
// (ChartItem, chart_item.h), the time and value labels as Text over it. Above it: Live / Hold, the window (10 s,
// 1 min, 5 min), Y auto (off: a manual range from a small sheet), Clear, and Fast lines when the map has streams;
// a line saying what a finger does; the legend, a chip per line (its colour, name and latest value in view with its
// unit: a tap hides or shows the line, its "..." button offers Hide / Show and Take off the chart). A pinch zooms the
// time, a drag looks back (and holds), a double tap goes live again. Time runs left to right in Arabic too: the plot,
// its labels and the legend are never mirrored. The lines come from PhoneSession (the registers plotted on the
// Registers screen, the fast channels chosen here); with none, the plot says how to plot one, with a button to the
// Registers screen. Cursors (B.4): the Cursors button puts A and B where the lines have samples in view (a quarter and
// three quarters of that span) and holds the view; each is dragged by its round handle (48 dp; A's at the plot's top,
// B's at its foot); the readouts beside the plot (under it in portrait; scrolled, with a scroll bar and a fade at an
// edge with more) give A's and B's times and B - A, and per line its values at A and B, B - A, and min, max and mean
// over A..B (ChartItem's readouts: the desktop's statsOf; an em dash where a line has no value). With demo (Main.qml's chartDemo) the chart's test feed draws 8 lines at 1,000 Hz (chart_demo.h).
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

    // landscape: the plot keeps about half the height (the owner's phone, 384 dp across: it had 260 of 1080 px under
    // the controls, the gesture line and the legend). The gesture line goes (it stays upright and in Help), the
    // buttons are as narrow as their text (48 dp or more), and the legend shares the controls' row when about a chip
    // fits after them (legendBesideWidth), else it has a row of its own
    readonly property bool wide: width > height
    readonly property real legendBesideWidth: 180
    readonly property real controlsWidth: viewGroup.width + linesGroup.width + toolsGroup.width
    readonly property bool legendBeside: wide && chart.lineCount > 0
                                         && width - 16 - controlsWidth - legendGap.width >= legendBesideWidth

    // a button of the controls: flat, in its row's middle; across, as narrow as its text allows (Material's are 64 dp
    // at least) and 48 dp high (Material's 52: its face 40 and 6 above and under), 48 dp to the touch either way
    component BarButton: Button {
        flat: true
        anchors.verticalCenter: parent.verticalCenter
        topInset: screen.wide ? 4 : 6
        bottomInset: screen.wide ? 4 : 6
        implicitWidth: Math.max(screen.wide ? 48 : implicitBackgroundWidth + leftInset + rightInset,
                                implicitContentWidth + leftPadding + rightPadding)
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

        // the controls, the gesture line and the legend: one under the other, or in landscape with room the controls
        // and the legend side by side on one row (mirrored in Arabic: the controls at the right)
        GridLayout {
            id: topRows
            objectName: "chartTop"
            Layout.fillWidth: true
            columns: screen.legendBeside ? 2 : 1
            rowSpacing: 0
            columnSpacing: 0

            // the controls: one row in landscape; in portrait the view's (Live / Hold, the window) over the lines'
            // (Y, Clear, Fast lines), each group whole
            Flow {
                id: controls
                objectName: "chartControls"
                Layout.fillWidth: !screen.legendBeside
                Layout.preferredWidth: screen.legendBeside ? screen.controlsWidth : -1
                Layout.leftMargin: 8
                Layout.rightMargin: screen.legendBeside ? 0 : 8
                spacing: 0

                Row {
                    id: viewGroup
                    spacing: 2
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
                    Repeater {
                        model: screen.windows.length
                        BarButton {
                            required property int index
                            objectName: "window" + screen.windows[index]
                            text: screen.windowNames[index]
                            checked: Math.abs(chart.windowSeconds - screen.windows[index]) < 1e-6
                            highlighted: checked
                            onClicked: chart.windowSeconds = screen.windows[index]
                        }
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
                    // Y: Auto spans the values in view; off asks for a manual range first
                    Switch {
                        id: yAutoSwitch
                        objectName: "yAutoSwitch"
                        anchors.verticalCenter: parent.verticalCenter
                        // 48 dp to the touch, as the buttons (Material's switch is lower)
                        implicitHeight: 48
                        text: qsTr("Y auto")
                        checked: chart.yAuto
                        onToggled: {
                            if (checked)
                                chart.yAuto = true
                            else
                                ySheet.openFor()
                            checked = Qt.binding(() => chart.yAuto)
                        }
                    }
                    // the manual range, shown and changed here while Auto is off
                    BarButton {
                        objectName: "yRangeButton"
                        visible: !chart.yAuto
                        text: qsTr("Y %1 to %2").arg(screen.shortNumber(chart.yLow))
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

            // the legend: a chip per line, scrolled sideways when they do not fit; left to right in every
            // language. Beside the controls a line sets it apart from them, as between their groups
            Item {
                id: legendBox
                objectName: "legendBox"
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.topMargin: screen.wide ? 0 : 4
                Layout.leftMargin: screen.legendBeside ? 0 : 8
                Layout.rightMargin: 8
                Layout.alignment: Qt.AlignVCenter
                visible: chart.lineCount > 0

                Item {
                    id: legendGap
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 13
                    height: 24
                    visible: screen.legendBeside
                    Rectangle {
                        objectName: "legendGap"
                        anchors.centerIn: parent
                        width: 1
                        height: 24
                        color: PhoneTheme.border
                    }
                }
                // the chips' room (it mirrors with the screen; the list in it does not)
                Item {
                    id: legendRoom
                    anchors.fill: parent
                    anchors.leftMargin: screen.legendBeside ? legendGap.width : 0
                    ListView {
                        id: legend
                        objectName: "legendView"
                        anchors.fill: parent
                        orientation: ListView.Horizontal
                        spacing: 6
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        LayoutMirroring.enabled: false
                        LayoutMirroring.childrenInherit: true
                        model: chart.legend
                        readonly property bool overflows: contentWidth > width + 0.5

                        // a chip: 40 dp drawn, its touch 48 dp high (the "..." button 48 x 48)
                        delegate: Item {
                            id: chip
                            required property int lineId
                            required property string name
                            required property string value
                            required property color colour
                            required property bool shown
                            objectName: "legendChip"
                            width: chipRow.x + chipRow.implicitWidth + 2
                            height: 48

                            Rectangle {
                                objectName: "chipFace"
                                anchors.fill: parent
                                anchors.topMargin: 4
                                anchors.bottomMargin: 4
                                radius: 20
                                color: PhoneTheme.surface
                                border.width: 1
                                // a hidden line's chip: its border and text muted, its dot hollow
                                border.color: chip.shown ? chip.colour : PhoneTheme.border
                            }

                            // a tap on the chip (its "..." button apart, over it) hides or shows its line: a
                            // button, so every tap counts (two quick taps are two, not a double tap)
                            AbstractButton {
                                objectName: "chipToggle"
                                anchors.fill: parent
                                Accessible.name: chip.shown ? qsTr("Hide %1").arg(chip.name)
                                                            : qsTr("Show %1").arg(chip.name)
                                onClicked: chart.setLineShown(chip.lineId, !chip.shown)
                            }

                            Row {
                                id: chipRow
                                anchors.verticalCenter: parent.verticalCenter
                                x: 12
                                spacing: 6
                                // the line's colour: filled while drawn, a ring while hidden
                                Rectangle {
                                    objectName: "chipDot"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 12
                                    height: 12
                                    radius: 6
                                    color: chip.shown ? chip.colour : "transparent"
                                    border.width: 2
                                    border.color: chip.colour
                                }
                                Text {
                                    objectName: "chipName"
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: chip.name
                                    color: chip.shown ? PhoneTheme.text : PhoneTheme.muted
                                    font.pixelSize: 13
                                    font.strikeout: !chip.shown
                                }
                                Text {
                                    objectName: "chipValue"
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: chip.value
                                    visible: text.length > 0
                                    color: chip.shown ? PhoneTheme.text : PhoneTheme.muted
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                // three dots drawn, not a character: a phone's font may have no "⋯"
                                ToolButton {
                                    id: chipMore
                                    objectName: "chipMore"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 48
                                    height: 48
                                    Accessible.name: qsTr("More: Hide, Take off the chart")
                                    onClicked: chipMenu.openFor(chip.lineId, chip.name, chip.shown, chipMore)
                                    contentItem: Item {
                                        Row {
                                            anchors.centerIn: parent
                                            spacing: 3
                                            Repeater {
                                                model: 3
                                                Rectangle {
                                                    objectName: "chipMoreDot"
                                                    width: 4
                                                    height: 4
                                                    radius: 2
                                                    color: PhoneTheme.text
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // more chips than room: a fade over each edge with more, so a chip cut by the edge reads as
                    // more to scroll, not as a cut name (the owner's phone: "SETPOIN" at the screen's edge). The
                    // list runs left to right in every language, so its end is at the right; set from its
                    // direction all the same
                    Repeater {
                        model: 2 // the start's fade (scrolled on) and the end's (more to come)
                        Rectangle {
                            id: chipFade
                            required property int index
                            readonly property bool end: index === 1
                            readonly property bool onRight: end !== (legend.effectiveLayoutDirection === Qt.RightToLeft)
                            readonly property color clear: Qt.rgba(PhoneTheme.bg.r, PhoneTheme.bg.g, PhoneTheme.bg.b, 0)
                            objectName: end ? "legendFadeEnd" : "legendFadeStart"
                            x: onRight ? legendRoom.width - width : 0
                            width: 32
                            height: legendRoom.height
                            visible: legend.overflows && (end ? !legend.atXEnd : !legend.atXBeginning)
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: chipFade.onRight ? chipFade.clear : PhoneTheme.bg }
                                GradientStop { position: 1; color: chipFade.onRight ? PhoneTheme.bg : chipFade.clear }
                            }
                        }
                    }
                }
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
                    visible: x >= 0 && y >= 0 && y + height <= plotArea.height
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
                // a cursor's handle keeps its finger: the view is not taken from it, nor moved under it
                grabPermissions: PointerHandler.CanTakeOverFromItems | PointerHandler.ApprovesTakeOverByAnything
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

            // the cursors: a line over the plot and a round handle to drag it (A's at the top, B's at the foot)
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
                            width: 28
                            height: 28
                            radius: 14
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
                                    radius: 5
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
                            Flow {
                                Layout.fillWidth: true
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

    // the map's streams' channels, each on the chart or not (its stream switched on with its first channel)
    PhoneMenu {
        id: fastMenu
        objectName: "fastMenu"
        Instantiator {
            model: PhoneSession.fastChannels
            delegate: MenuItem {
                required property var modelData
                objectName: "fastChannel"
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
                text: qsTr("Auto off: the value axis stays between these.")
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
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    objectName: "ySheetCancel"
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: ySheet.close()
                }
                Button {
                    objectName: "ySheetApply"
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

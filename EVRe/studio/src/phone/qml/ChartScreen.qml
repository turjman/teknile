// SPDX-License-Identifier: Apache-2.0
// The Chart screen (STUDIO.md 25.6): the plotted lines over time, drawn by the GPU from nodes of the chart's own
// (ChartItem, chart_item.h), the time and value labels as Text over it; Live / Hold and the window (10 s, 1 min,
// 5 min) above it, and a line saying what a finger does. A pinch zooms the time, a drag looks back (and holds), a
// double tap goes live again. Time runs left to right in Arabic too: the plot and its labels are never mirrored.
// With demo (Main.qml's chartDemo) the chart's test feed draws 8 lines at 1,000 Hz (chart_demo.h).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    property bool demo: false
    readonly property alias chart: chart

    // the window choice's lengths, seconds, and their names (a unit stays as it is in every language)
    readonly property var windows: [10, 60, 300]
    readonly property var windowNames: ["10 s", "1 min", "5 min"]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            id: controls
            objectName: "chartControls"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 2

            // Live / Hold: the view follows now, or stays where it is
            Button {
                objectName: "liveButton"
                text: qsTr("Live")
                flat: true
                checkable: true
                checked: chart.live
                highlighted: chart.live
                onClicked: chart.live = true
            }
            Button {
                objectName: "holdButton"
                text: qsTr("Hold")
                flat: true
                checkable: true
                checked: !chart.live
                highlighted: !chart.live
                onClicked: chart.live = false
            }
            // the two groups apart
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 24
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                color: PhoneTheme.border
            }
            Repeater {
                model: screen.windows.length
                Button {
                    required property int index
                    objectName: "window" + screen.windows[index]
                    text: screen.windowNames[index]
                    flat: true
                    checkable: true
                    checked: Math.abs(chart.windowSeconds - screen.windows[index]) < 1e-6
                    highlighted: checked
                    onClicked: chart.windowSeconds = screen.windows[index]
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
        // what a finger does on the plot (no hover on a phone: said where it is seen)
        Label {
            objectName: "chartHint"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            text: qsTr("Pinch: zoom the time · Drag: look back · Double tap: live")
            color: PhoneTheme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

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
            Label {
                objectName: "chartEmpty"
                visible: chart.lineCount === 0
                x: chart.plotRect.x + (chart.plotRect.width - width) / 2
                y: chart.plotRect.y + (chart.plotRect.height - height) / 2
                width: Math.min(chart.plotRect.width - 32, 320)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("No lines yet: plot a register on the Registers screen to see it here.")
                color: PhoneTheme.muted
                font.pixelSize: 14
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
                onActiveChanged: lastX = 0
                onActiveTranslationChanged: {
                    if (!active) return
                    chart.panPixels(activeTranslation.x - lastX)
                    lastX = activeTranslation.x
                }
            }
            TapHandler {
                objectName: "chartTap"
                onDoubleTapped: chart.live = true
            }
        }
    }

    ChartDemo {
        objectName: "chartDemo"
        chart: chart
        running: screen.demo
        colours: PhoneTheme.series
    }
}

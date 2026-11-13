// SPDX-License-Identifier: Apache-2.0
// The phone's small icons, drawn from rectangles on a grid of 24 (scaled to `size`): no icon font and no symbol
// character, which a phone's font may not have ("☰" showed as a box on a Samsung phone). The tab bar's four
// (registers, chart, monitor, log), the search field's magnifier and its clear cross, the write sheet's warning, and
// the Help's chevrons (back, next).
// The strokes are 2 units wide with round ends; a stroke at an angle is a turned rectangle, smoothed.
import QtQuick

Item {
    id: icon
    property string kind: "registers"
    property color color: "black"
    // the warning's mark, on its filled circle
    property color markColor: "white"
    property real size: 24
    readonly property real u: size / 24

    implicitWidth: size
    implicitHeight: size

    // a straight stroke from (x1, y1) to (x2, y2), in grid units
    component Stroke: Rectangle {
        property real x1
        property real y1
        property real x2
        property real y2
        property real weight: 2
        readonly property real length: Math.sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))
        width: (length + weight) * icon.u
        height: weight * icon.u
        radius: height / 2
        x: ((x1 + x2) / 2) * icon.u - width / 2
        y: ((y1 + y2) / 2) * icon.u - height / 2
        rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
        antialiasing: true
        color: icon.color
    }
    // an outlined box, in grid units
    component Box: Rectangle {
        property real x1
        property real y1
        property real x2
        property real y2
        x: x1 * icon.u
        y: y1 * icon.u
        width: (x2 - x1) * icon.u
        height: (y2 - y1) * icon.u
        radius: 2.5 * icon.u
        color: "transparent"
        border.width: 2 * icon.u
        border.color: icon.color
        antialiasing: true
    }

    // registers: a list, a dot and a line per row
    Item {
        anchors.fill: parent
        visible: icon.kind === "registers"
        Repeater {
            model: [6, 12, 18]
            Item {
                anchors.fill: parent
                Rectangle {
                    x: 3 * icon.u
                    y: (modelData - 1.75) * icon.u
                    width: 3.5 * icon.u
                    height: 3.5 * icon.u
                    radius: 1 * icon.u
                    color: icon.color
                    antialiasing: true
                }
                Stroke { x1: 10; y1: modelData; x2: 20; y2: modelData }
            }
        }
    }
    // chart: two axes and a line over them
    Item {
        anchors.fill: parent
        visible: icon.kind === "chart"
        Stroke { x1: 4; y1: 3; x2: 4; y2: 20 }
        Stroke { x1: 4; y1: 20; x2: 21; y2: 20 }
        Stroke { x1: 8; y1: 15; x2: 12; y2: 10 }
        Stroke { x1: 12; y1: 10; x2: 15; y2: 13 }
        Stroke { x1: 15; y1: 13; x2: 20; y2: 6 }
    }
    // monitor: a screen with a prompt and a line of text
    Item {
        anchors.fill: parent
        visible: icon.kind === "monitor"
        Box { x1: 2; y1: 4; x2: 22; y2: 20 }
        Stroke { x1: 6.5; y1: 9; x2: 9.5; y2: 12 }
        Stroke { x1: 9.5; y1: 12; x2: 6.5; y2: 15 }
        Stroke { x1: 12; y1: 15; x2: 17; y2: 15 }
    }
    // log: a page of lines
    Item {
        anchors.fill: parent
        visible: icon.kind === "log"
        Box { x1: 4.5; y1: 2.5; x2: 19.5; y2: 21.5 }
        Stroke { x1: 8.5; y1: 8; x2: 15.5; y2: 8 }
        Stroke { x1: 8.5; y1: 12; x2: 15.5; y2: 12 }
        Stroke { x1: 8.5; y1: 16; x2: 12.5; y2: 16 }
    }
    // search: a magnifier
    Item {
        anchors.fill: parent
        visible: icon.kind === "search"
        Rectangle {
            x: 3.5 * icon.u
            y: 3.5 * icon.u
            width: 13 * icon.u
            height: 13 * icon.u
            radius: width / 2
            color: "transparent"
            border.width: 2 * icon.u
            border.color: icon.color
            antialiasing: true
        }
        Stroke { x1: 15; y1: 15; x2: 20; y2: 20 }
    }
    // clear: a cross
    Item {
        anchors.fill: parent
        visible: icon.kind === "clear"
        Stroke { x1: 6.5; y1: 6.5; x2: 17.5; y2: 17.5 }
        Stroke { x1: 17.5; y1: 6.5; x2: 6.5; y2: 17.5 }
    }
    // back and next: a chevron pointing left or right
    Item {
        anchors.fill: parent
        visible: icon.kind === "back" || icon.kind === "next"
        rotation: icon.kind === "next" ? 180 : 0
        Stroke { x1: 15; y1: 5.5; x2: 8.5; y2: 12 }
        Stroke { x1: 8.5; y1: 12; x2: 15; y2: 18.5 }
    }
    // warning: a filled circle with an exclamation mark
    Item {
        anchors.fill: parent
        visible: icon.kind === "warning"
        Rectangle {
            x: 2 * icon.u
            y: 2 * icon.u
            width: 20 * icon.u
            height: 20 * icon.u
            radius: width / 2
            color: icon.color
            antialiasing: true
        }
        Rectangle {
            x: 10.75 * icon.u
            y: 6.5 * icon.u
            width: 2.5 * icon.u
            height: 7.5 * icon.u
            radius: width / 2
            color: icon.markColor
            antialiasing: true
        }
        Rectangle {
            x: 10.75 * icon.u
            y: 15.25 * icon.u
            width: 2.5 * icon.u
            height: 2.5 * icon.u
            radius: width / 2
            color: icon.markColor
            antialiasing: true
        }
    }
}

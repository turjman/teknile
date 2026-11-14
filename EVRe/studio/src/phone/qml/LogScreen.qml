// SPDX-License-Identifier: Apache-2.0
// The Log screen: what happened on the link (PhoneSession.log), newest last. Each line: its level's icon in the level's
// colour (info: an i in the accent; warning: a triangle in amber; error: a cross in red: three shapes, so a level reads
// without its colour too), its time in the monospaced font, then its text, the text coloured as the desktop's Log
// colours it (the text colour, amber, red). Above: a filter chip per level with its count, on or off (RowFilter over the
// level; kept in phone/logLevels), then Copy (the lines shown, each with its level as the desktop's log file writes it)
// and Clear. The list follows the newest line while it is at its end; scrolled up, it stays where it is. Empty, it shows
// the Log's icon, says what comes here and has a button to the Connect drawer (a link is what fills it); every line
// filtered out, it says so and offers to show them all.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import EVRe.Phone

Item {
    id: screen
    readonly property int lines: PhoneSession.log.count
    readonly property int shownLines: levels.count
    // the levels' looks: icon, tone, soft tone, name
    readonly property var levelIcons: ["info", "warn", "error"]
    readonly property var levelTones: [PhoneTheme.accent, PhoneTheme.warn, PhoneTheme.bad]
    readonly property var levelSoftTones: [PhoneTheme.accentSoft, PhoneTheme.warnSoft, PhoneTheme.badSoft]
    readonly property var levelNames: [qsTr("Info"), qsTr("Warnings"), qsTr("Errors")]
    readonly property var levelCounts: [PhoneSession.log.infoCount, PhoneSession.log.warningCount, PhoneSession.log.errorCount]
    // a fixed-pitch family the phone has, for the times (PhoneTheme.monoFamily)
    readonly property string mono: PhoneTheme.monoFamily

    signal connectAsked()

    // the levels shown (all at first; the choice kept)
    RowFilter {
        id: levels
        objectName: "logFilter"
        sourceModel: PhoneSession.log
        roleName: "level"
        shown: [0, 1, 2]
        settingsKey: "phone/logLevels"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 4

            // the levels' chips: wrap onto a second row when the screen is narrow
            Flow {
                id: chips
                objectName: "logLevelChips"
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: 3
                    FilterChip {
                        required property int index
                        objectName: "logLevel" + index
                        iconKind: screen.levelIcons[index]
                        tone: screen.levelTones[index]
                        softTone: screen.levelSoftTones[index]
                        label: screen.levelNames[index]
                        count: screen.levelCounts[index]
                        checked: levels.shown.indexOf(index) >= 0
                        onToggled: levels.setValueShown(index, checked)
                    }
                }
            }
            Button {
                objectName: "logCopy"
                Layout.alignment: Qt.AlignTop
                // as narrow as its word (Material's are 64 dp at least): the chips and both buttons on one row upright
                leftPadding: 8
                rightPadding: 8
                implicitWidth: Math.max(48, implicitContentWidth + leftPadding + rightPadding)
                text: qsTr("Copy")
                flat: true
                Material.foreground: PhoneTheme.accent
                enabled: screen.shownLines > 0
                onClicked: PhoneSession.log.copy(levels.shown)
            }
            Button {
                objectName: "logClear"
                Layout.alignment: Qt.AlignTop
                // as narrow as its word (Material's are 64 dp at least): the chips and both buttons on one row upright
                leftPadding: 8
                rightPadding: 8
                implicitWidth: Math.max(48, implicitContentWidth + leftPadding + rightPadding)
                text: qsTr("Clear")
                flat: true
                Material.foreground: PhoneTheme.accent
                enabled: screen.lines > 0
                onClicked: PhoneSession.log.clear()
            }
        }
        // the buttons apart from the lines
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: PhoneTheme.border
        }

        ListView {
            id: list
            objectName: "logList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: levels
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
            property bool following: true
            onMovementEnded: following = atYEnd
            onCountChanged: if (following) Qt.callLater(list.positionViewAtEnd)

            delegate: Item {
                id: line
                required property string time
                required property string text
                required property int level
                readonly property color tone: level === 2 ? PhoneTheme.bad : level === 1 ? PhoneTheme.warn : PhoneTheme.text
                width: ListView.view.width
                height: Math.max(lineText.implicitHeight, timeText.implicitHeight) + 16

                // the level's icon, the time and the text side by side, the text wrapping in its own column
                PhoneIcon {
                    id: levelIcon
                    objectName: "logIcon"
                    x: line.LayoutMirroring.enabled ? line.width - 12 - width : 12
                    y: 8 + (timeText.implicitHeight - height) / 2
                    kind: screen.levelIcons[line.level] || "info"
                    size: 16
                    color: screen.levelTones[line.level] || PhoneTheme.accent
                    markColor: PhoneTheme.bg
                }
                Text {
                    id: timeText
                    objectName: "logTime"
                    x: line.LayoutMirroring.enabled ? levelIcon.x - 8 - width : levelIcon.x + levelIcon.width + 8
                    y: 8 + (lineText.lineHeightPx - implicitHeight) / 2
                    text: line.time
                    color: PhoneTheme.muted
                    font.family: screen.mono
                    font.pixelSize: 12
                }
                Label {
                    id: lineText
                    objectName: "logText"
                    // the first line's height, so the time and the icon sit on it
                    readonly property real lineHeightPx: lineMetrics.height
                    x: line.LayoutMirroring.enabled ? 12 : timeText.x + timeText.width + 10
                    y: 8
                    width: line.LayoutMirroring.enabled ? timeText.x - 10 - 12 : line.width - x - 12
                    horizontalAlignment: Text.AlignLeft
                    text: line.text
                    color: line.tone
                    font.pixelSize: 14
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    FontMetrics {
                        id: lineMetrics
                        font: lineText.font
                    }
                }
                // the lines apart
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 12
                    anchors.bottom: parent.bottom
                    height: 1
                    color: PhoneTheme.border
                    opacity: 0.6
                }
            }
        }
    }

    EmptyState {
        objectName: "logEmptyState"
        prefix: "logEmpty"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        visible: screen.lines === 0
        icon: "log"
        title: qsTr("Nothing logged yet")
        text: qsTr("The map, the link and the recordings are told here.")
        action: PhoneSession.connected ? "" : qsTr("Open the Connect drawer")
        onActed: screen.connectAsked()
    }
    // lines there, every one filtered out
    EmptyState {
        objectName: "logFilteredState"
        prefix: "logFiltered"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        visible: screen.lines > 0 && screen.shownLines === 0
        icon: "log"
        title: qsTr("No line of the levels chosen")
        text: qsTr("%n line(s) hidden by the filter above.", "", screen.lines)
        action: qsTr("Show all levels")
        onActed: levels.shown = [0, 1, 2]
    }
}

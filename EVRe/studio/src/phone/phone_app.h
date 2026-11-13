/* SPDX-License-Identifier: Apache-2.0 */
/* What the phone program does before and at loading its window, shared with its test (tests/phone_test.cpp), so the
 * test loads the window exactly as the program does. */
#pragma once

#include <QSize>
#include <QString>

class QQmlApplicationEngine;
class QQuickWindow;

namespace phone {

/* the maps that come with the program: the APK's assets on Android, maps/ beside the program elsewhere */
QString mapsFolder();
/* the Material style (Q-3): before any QML is loaded */
void setStyle();
/* the proof P-9 asked for: EVRE_P9=1, or on Android the start intent's extra (am start ... --ez p9 true) */
bool p9Asked();
/* the chart's test feed asked for (chart_demo.h): EVRE_CHARTDEMO=1, or the extra chartdemo (--ez chartdemo true) */
bool chartDemoAsked();
/* Main.qml of the module EVRe.Phone into `engine`; `size` empty: Main.qml's own (412 x 915, a phone's portrait).
 * p9: the proof's screen alone, and its meter on the window (p9_proof.h). chartDemo: the Chart screen fed by the
 * chart's test feed, the meter's lines under EVRE_PERF (chart_demo.h). nullptr: it did not load. */
QQuickWindow *loadMain(QQmlApplicationEngine &engine, bool p9 = false, QSize size = QSize(), bool chartDemo = false);

} // namespace phone

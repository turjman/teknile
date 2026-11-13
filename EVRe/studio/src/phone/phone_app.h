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
/* the speed line asked for: EVRE_PERF=1, or on Android the start intent's extra (am start ... --ez perf true) */
bool perfAsked();
/* Main.qml of the module EVRe.Phone into `engine`; `size` empty: Main.qml's own (412 x 915, a phone's portrait).
 * p9: the proof's screen alone, and its meter on the window (p9_proof.h). perf: a line a second to logcat under the
 * tag EVRE_PERF while the Registers screen shows (fps, the longest frame, frames over 20 ms), and a bar running at
 * its top so every frame is drawn. The window's frames run PhoneSession::sync(). nullptr: it did not load. */
QQuickWindow *loadMain(QQmlApplicationEngine &engine, bool p9 = false, QSize size = QSize(), bool perf = false);

} // namespace phone

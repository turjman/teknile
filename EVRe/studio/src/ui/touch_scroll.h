/* SPDX-License-Identifier: Apache-2.0 */
/* Touch scrolling everywhere: a finger dragged over any scrolling area (a table, a list, the sidebar, the Monitor, the
 * Help, a dialog's list, the Chart tab's rows on a phone) scrolls it, with momentum, as a phone's lists do. Qt's widgets
 * scroll by touch only where a QScroller is grabbed on their viewport, and the Studio had none: on a touch screen, a
 * Windows one as an Android one, only the scroll bars moved them. One filter on the application grabs it on every
 * QAbstractScrollArea as it is first polished, so the windows, dialogs and popups made later have it too. The mouse is
 * unchanged (TouchGesture: a mouse drag still selects). The chart's plot is not a scrolling area: its own touch comes
 * with the touch part of the Android plan. */
#pragma once

#include <QObject>

class QApplication;

class TouchScroll : public QObject {
public:
	/* the filter on the application, once (a second call does nothing) */
	static void install(QApplication &app);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	explicit TouchScroll(QObject *parent) : QObject(parent) {}
};

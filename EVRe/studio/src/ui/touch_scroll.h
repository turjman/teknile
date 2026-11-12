/* SPDX-License-Identifier: Apache-2.0 */
/* Touch scrolling everywhere: a finger dragged over any scrolling area (a table, a list, the sidebar, the Monitor, the
 * Help, a dialog's list, the Chart tab's rows on a phone) scrolls it, with momentum, as a phone's lists do; a tap (down
 * and up without moving) is a click where the finger was, a double tap a double click. Qt's widgets scroll by touch only
 * where a QScroller is fed, and the Studio fed none: on a touch screen, a Windows one as an Android one, only the scroll
 * bars moved them, and a drag over a list selected its rows.
 *
 * One filter on the application: the mouse events Qt makes from a finger (their device a touch screen) over a
 * QAbstractScrollArea's viewport, or over any widget inside it (the sidebar's buttons and boxes), are held back and fed
 * to that area's QScroller instead; at the release, one that never moved is sent on as the click it was. A grabbed touch
 * gesture was not enough: it never started over a child widget (the sidebar), and the mouse events made from the same
 * finger still reached the list and selected. The mouse is unchanged (a mouse drag selects, the scroll bars drag), and
 * so are a finger on a scroll bar and on the chart's plot (its own touch comes with the touch part of the Android plan). */
#pragma once

#include <QObject>
#include <QPersistentModelIndex>
#include <QPointF>
#include <QPointer>

class QApplication;
class QMouseEvent;
class QWidget;

class TouchScroll : public QObject {
public:
	/* the filter on the application, once (a second call does nothing) */
	static void install(QApplication &app);
	/* how far a finger may move and still be a tap, in logical px */
	static constexpr int TAP_SLOP = 10;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	explicit TouchScroll(QObject *parent) : QObject(parent) {}
	/* the scrolling area's viewport a finger on this widget scrolls, or nullptr */
	static QWidget *scrollViewport(QWidget *widget);
	/* the press (or double click) held back, sent on with its release: a tap */
	void sendTap(const QMouseEvent *release);
	/* the finger at this screen point: the area scrolled, or the rows from selectFrom_ to it selected */
	void moveTo(const QPointF &global, quint64 timestamp);

	QPointer<QWidget> target_;   /* the widget the finger came down on */
	QPointer<QWidget> viewport_; /* the area it scrolls */
	QPointF pressGlobal_;
	bool doubleTap_ = false;
	QPersistentModelIndex selectFrom_; /* down on a selected row: the drag selects from it, it does not scroll */
	bool moved_ = false;
	bool sending_ = false;       /* the tap's own events pass the filter */
};

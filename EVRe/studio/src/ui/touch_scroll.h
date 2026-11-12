/* SPDX-License-Identifier: Apache-2.0 */
/* Touch scrolling everywhere: a finger dragged over any scrolling area (a table, a list, the sidebar, the Monitor, the
 * Help, a dialog's list, the Chart tab's rows on a phone) scrolls it, and glides on a little when let go fast, as a
 * phone's lists do; a tap (down and up without moving) is a click where the finger was, a double tap a double click; a
 * drag that starts on a selected row of a list or table selects the rows it passes. At an end the list's frame lights
 * in the accent colour while the finger pushes on, and a moment after: the list goes no further (a scroll bar is not
 * always in sight: on a phone the window is wider than the screen). Qt's widgets scroll by touch only where something moves them, and the
 * Studio had nothing: on a touch screen, a Windows one as an Android one, only the scroll bars moved them, and a drag
 * over a list selected its rows.
 *
 * One filter on the application: the mouse events Qt makes from a finger (their device a touch screen) over a
 * QAbstractScrollArea's viewport, or over any widget inside it (the sidebar's buttons and boxes), are held back; the
 * finger's way (the touch updates, or the mouse moves where there are no touch updates) sets the area's scroll bar
 * itself, along one axis a drag; at the release one that never moved is sent on as the click it was. Not QScroller: on
 * Android (Qt 6.8) a scroller-driven area tore and left old strips on the screen (QTBUG-135011; the rows above the
 * scroll stayed, a row's text split in two), and a grabbed touch gesture never started over a child widget. The mouse is
 * unchanged (a mouse drag selects, the scroll bars drag), and so are a finger on a scroll bar and on the chart's plot
 * (its own touch comes with the touch part of the Android plan). */
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QVector>

class QAbstractScrollArea;
class QApplication;
class QMouseEvent;
class QScrollBar;
class QWidget;

class TouchScroll : public QObject {
public:
	/* the filter on the application, once (a second call does nothing) */
	static void install(QApplication &app);
	/* how far a finger may move and still be a tap, in logical px */
	static constexpr int TAP_SLOP = 10;
	/* the glide after a fast release: its speed falls by e every FLING_TAU ms, and it stops under FLING_STOP px/ms */
	static constexpr double FLING_TAU = 325, FLING_START = 0.25, FLING_STOP = 0.02;
	/* how long the frame stays lit after the finger stops pushing at an end, ms */
	static constexpr int END_FLASH_MS = 400;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	explicit TouchScroll(QObject *parent);
	/* the scrolling area's viewport a finger on this widget scrolls, or nullptr */
	static QWidget *scrollViewport(QWidget *widget);
	/* the press (or double click) held back, sent on with its release: a tap */
	void sendTap(const QMouseEvent *release);
	/* the finger at this screen point: the area scrolled along its axis, or the rows from selectFrom_ to it selected */
	void moveTo(const QPointF &global, quint64 timestamp);
	/* the drag's axis's scroll bar, or nullptr */
	QScrollBar *axisBar() const;
	/* the scroll bar to this place (fractions kept in place_); false at an end it could not pass */
	bool scrollTo(QScrollBar *bar, double place);
	/* the list's frame lit (the property touchEnd, the theme's accent) until END_FLASH_MS after the last push */
	void flashEnd(QScrollBar *bar);
	void flingStep();
	void stopFling();

	QPointer<QWidget> target_;   /* the widget the finger came down on */
	QPointer<QWidget> viewport_; /* the area it scrolls */
	QPointF pressGlobal_, lastGlobal_;
	int axis_ = 0;               /* 0 not yet, 1 across, 2 up and down: one axis a drag (a finger is never straight) */
	double place_ = 0;           /* the scroll bar's place with its fraction */
	bool doubleTap_ = false;
	QPersistentModelIndex selectFrom_; /* down on a selected row: the drag selects from it, it does not scroll */
	bool moved_ = false;
	bool stoppedGlide_ = false;  /* the finger came down on a gliding list: it stops it, it is no tap */
	bool touchSeen_ = false;     /* this finger's way comes as touch updates: its mouse moves are left out */
	quint64 lastMove_ = 0;       /* the time of the last step */
	struct Sample {
		quint64 ms;
		double along;
	};
	QVector<Sample> samples_;    /* the finger's last steps along the axis, for the glide's speed */
	bool sending_ = false;       /* the tap's own events pass the filter */
	QTimer flingTimer_;
	QElapsedTimer flingClock_;
	QPointer<QScrollBar> flingBar_;
	double flingSpeed_ = 0;      /* px/ms, in the scroll bar's values */
};

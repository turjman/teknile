/* SPDX-License-Identifier: Apache-2.0 */
#include "ui/touch_scroll.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QInputDevice>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QTouchEvent>
#include <algorithm>

void TouchScroll::install(QApplication &app) {
	static bool installed = false;
	if (installed) return;
	installed = true;
	app.installEventFilter(new TouchScroll(&app));
}

QWidget *TouchScroll::scrollViewport(QWidget *widget) {
	for (QWidget *w = widget; w; w = w->parentWidget()) {
		if (qobject_cast<QScrollBar *>(w)) return nullptr;       /* its handle drags as with the mouse */
		if (w->inherits("ChartView")) return nullptr;            /* the plot: cursors, notes, the trigger's level */
		auto *area = qobject_cast<QAbstractScrollArea *>(w->parentWidget());
		if (area && area->viewport() == w) return w;
	}
	return nullptr;
}

void TouchScroll::sendTap(const QMouseEvent *release) {
	QWidget *target = target_;
	if (!target) return;
	const QPointF local = target->mapFromGlobal(pressGlobal_);
	sending_ = true;
	QMouseEvent press(doubleTap_ ? QEvent::MouseButtonDblClick : QEvent::MouseButtonPress, local, pressGlobal_,
			Qt::LeftButton, Qt::LeftButton, release->modifiers());
	QApplication::sendEvent(target, &press);
	if (target_) { /* the press may have closed what it was on */
		QMouseEvent up(QEvent::MouseButtonRelease, local, pressGlobal_, Qt::LeftButton, Qt::NoButton, release->modifiers());
		QApplication::sendEvent(target_, &up);
	}
	sending_ = false;
}

bool TouchScroll::eventFilter(QObject *watched, QEvent *event) {
	const QEvent::Type type = event->type();
	/* the finger's way while it is held: on Windows Qt makes a mouse press and a release of a touch but no mouse moves,
	 * the touch updates (to the window) are all there is of the drag; they go on to Qt as they came */
	if ((type == QEvent::TouchUpdate || type == QEvent::TouchEnd) && viewport_ && !sending_) {
		const auto *touch = static_cast<QTouchEvent *>(event);
		/* one source a finger: on Android the drag comes as touch updates and as mouse moves both, a little apart in
		 * place and time, and the list fed both shook; the touch updates once they come, the mouse moves no more */
		touchSeen_ = true;
		if (!touch->points().isEmpty()) moveTo(touch->points().first().globalPosition(), touch->timestamp());
		return false;
	}
	if (sending_ || (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick && type != QEvent::MouseMove
			&& type != QEvent::MouseButtonRelease))
		return false;
	auto *mouse = static_cast<QMouseEvent *>(event);
	if (!mouse->pointingDevice() || mouse->pointingDevice()->type() != QInputDevice::DeviceType::TouchScreen) return false;
	auto *widget = qobject_cast<QWidget *>(watched);
	if (!widget) return false;
	if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
		if (mouse->button() != Qt::LeftButton) return false;
		QWidget *viewport = scrollViewport(widget);
		if (!viewport) return false;
		target_ = widget;
		viewport_ = viewport;
		pressGlobal_ = mouse->globalPosition();
		doubleTap_ = type == QEvent::MouseButtonDblClick;
		moved_ = false;
		touchSeen_ = false;
		lastMove_ = 0;
		/* down on a selected row of a list or table: the drag selects the rows it passes, as the mouse's does */
		selectFrom_ = QPersistentModelIndex();
		if (auto *view = qobject_cast<QAbstractItemView *>(viewport->parentWidget())) {
			/* by pixels once a finger is on it: by whole rows (Qt's default) a drag moved the table a row at a time and
			 * it shook under the finger (the phone's tall rows the most); a mouse-only list keeps its rows */
			view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
			view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
			const QModelIndex at = view->indexAt(viewport->mapFromGlobal(pressGlobal_).toPoint());
			if (at.isValid() && view->selectionModel() && view->selectionModel()->isSelected(at)) selectFrom_ = at;
		}
		QScroller *scroller = QScroller::scroller(viewport);
		QScrollerProperties properties = scroller->scrollerProperties();
		/* no bounce past the ends: the views draw nothing there, and a table that springs back reads as a fault */
		const QVariant off = QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff);
		properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, off);
		properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, off);
		/* every frame of the display, not every other (the default moved a phone's table in 9 px steps every 33 ms) */
		properties.setScrollMetric(QScrollerProperties::FrameRate, QVariant::fromValue(QScrollerProperties::Fps60));
		/* a drag from 2 mm, not 5: it began with a jump of what the finger had gone meanwhile (31 px on the phone) */
		properties.setScrollMetric(QScrollerProperties::DragStartDistance, 0.002);
		/* one axis a drag: a finger is never quite straight, and a table dragged up the phone slid sideways as well
		 * (the owner's "moves away"); a drag mostly up or down moves only up or down, one mostly across only across */
		properties.setScrollMetric(QScrollerProperties::AxisLockThreshold, 0.5);
		scroller->setScrollerProperties(properties);
		scroller->handleInput(QScroller::InputPress, viewport->mapFromGlobal(pressGlobal_), mouse->timestamp());
		return true;
	}
	if (watched != target_ || !viewport_) return false; /* not a finger we hold */
	if (type == QEvent::MouseMove) {
		if (!touchSeen_) moveTo(mouse->globalPosition(), mouse->timestamp());
		return true;
	}
	QScroller::scroller(viewport_)->handleInput(QScroller::InputRelease, viewport_->mapFromGlobal(mouse->globalPosition()),
			mouse->timestamp());
	if (!moved_) sendTap(mouse);
	target_ = nullptr;
	viewport_ = nullptr;
	selectFrom_ = QPersistentModelIndex();
	return true;
}

void TouchScroll::moveTo(const QPointF &global, quint64 timestamp) {
	/* the same update twice (the window's and a widget's) or one older than the last: the scroller's speed is made of
	 * the steps between them, a step back in time made it shake */
	if (timestamp != 0 && timestamp <= lastMove_) return;
	lastMove_ = timestamp;
	if ((global - pressGlobal_).manhattanLength() > TAP_SLOP) moved_ = true;
	auto *view = qobject_cast<QAbstractItemView *>(viewport_->parentWidget());
	if (selectFrom_.isValid() && view && view->selectionModel()) {
		/* from the selected row to the one under the finger, the rest of the selection let go */
		if (!moved_) return;
		const QModelIndex at = view->indexAt(viewport_->mapFromGlobal(global).toPoint());
		if (!at.isValid()) return;
		const QAbstractItemModel *model = view->model();
		const int top = std::min(selectFrom_.row(), at.row()), bottom = std::max(selectFrom_.row(), at.row());
		const QItemSelection rows(model->index(top, 0, at.parent()),
				model->index(bottom, std::max(0, model->columnCount(at.parent()) - 1), at.parent()));
		view->selectionModel()->select(rows, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		view->selectionModel()->setCurrentIndex(at, QItemSelectionModel::NoUpdate);
		return;
	}
	QScroller::scroller(viewport_)->handleInput(QScroller::InputMove, viewport_->mapFromGlobal(global), timestamp);
}

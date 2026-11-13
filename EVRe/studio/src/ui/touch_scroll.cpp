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
#include <QStyle>
#include <QTouchEvent>
#include <algorithm>
#include <cmath>

void TouchScroll::install(QApplication &app) {
	static bool installed = false;
	if (installed) return;
	installed = true;
	app.installEventFilter(new TouchScroll(&app));
}

TouchScroll::TouchScroll(QObject *parent) : QObject(parent) {
	flingTimer_.setInterval(16);
	connect(&flingTimer_, &QTimer::timeout, this, &TouchScroll::flingStep);
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

QScrollBar *TouchScroll::axisBar() const {
	auto *area = viewport_ ? qobject_cast<QAbstractScrollArea *>(viewport_->parentWidget()) : nullptr;
	if (!area || axis_ == 0) return nullptr;
	return axis_ == 2 ? area->verticalScrollBar() : area->horizontalScrollBar();
}

bool TouchScroll::scrollTo(QScrollBar *bar, double place) {
	const double clamped = std::clamp(place, double(bar->minimum()), double(bar->maximum()));
	place_ = clamped;
	bar->setValue(int(std::lround(clamped)));
	return clamped == place;
}

void TouchScroll::flashEnd(QScrollBar *bar) {
	auto *area = bar ? qobject_cast<QAbstractScrollArea *>(bar->parentWidget() ? bar->parentWidget()->parentWidget() : nullptr)
					 : nullptr;
	if (!area) return;
	/* lit while the finger keeps pushing against the end: a frame already lit waits END_FLASH_MS again */
	if (auto *timer = area->findChild<QTimer *>(QStringLiteral("touchEndTimer"), Qt::FindDirectChildrenOnly)) {
		timer->start();
		return;
	}
	/* the frame, not a strip over the rows: on Android (Qt 6.8) a small widget shown over the rows was painted and never
	 * reached the screen; the frame restyled repaints the whole list, which does */
	const auto light = [](QAbstractScrollArea *list, bool on) {
		list->setProperty("touchEnd", on);
		list->style()->unpolish(list);
		list->style()->polish(list);
		list->update();
	};
	light(area, true);
	auto *timer = new QTimer(area);
	timer->setObjectName(QStringLiteral("touchEndTimer"));
	timer->setSingleShot(true);
	timer->setInterval(END_FLASH_MS);
	QPointer<QAbstractScrollArea> guard(area);
	QObject::connect(timer, &QTimer::timeout, area, [guard, timer, light] {
		if (guard) light(guard, false);
		timer->deleteLater();
	});
	timer->start();
}

void TouchScroll::stopFling() {
	flingTimer_.stop();
	flingBar_ = nullptr;
	flingSpeed_ = 0;
}

void TouchScroll::flingStep() {
	QScrollBar *bar = flingBar_;
	if (!bar) {
		stopFling();
		return;
	}
	const double ms = std::max<qint64>(1, flingClock_.restart());
	if (!scrollTo(bar, place_ + flingSpeed_ * ms)) { /* an end: the glide stops there */
		flashEnd(bar);
		stopFling();
		return;
	}
	flingSpeed_ *= std::exp(-ms / FLING_TAU);
	if (std::abs(flingSpeed_) < FLING_STOP) stopFling();
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
		/* a finger on a gliding list stops it, and is no tap (as a phone's list) */
		stoppedGlide_ = flingTimer_.isActive() && flingBar_ && flingBar_->parentWidget() == viewport->parentWidget();
		stopFling();
		target_ = widget;
		viewport_ = viewport;
		pressGlobal_ = lastGlobal_ = mouse->globalPosition();
		doubleTap_ = type == QEvent::MouseButtonDblClick;
		moved_ = false;
		axis_ = 0;
		touchSeen_ = false;
		lastMove_ = 0;
		samples_.clear();
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
		return true;
	}
	if (watched != target_ || !viewport_) return false; /* not a finger we hold */
	if (type == QEvent::MouseMove) {
		if (!touchSeen_) moveTo(mouse->globalPosition(), mouse->timestamp());
		return true;
	}
	/* the release: a tap sent on, or a fast drag glides on */
	if (!moved_ && !stoppedGlide_) {
		sendTap(mouse);
	} else if (QScrollBar *bar = axisBar(); bar && !selectFrom_.isValid() && samples_.size() >= 2) {
		/* the speed of the last 100 ms of the drag (an older step would slow a fast flick) */
		const Sample last = samples_.last();
		Sample first = samples_.first();
		for (const Sample &s : std::as_const(samples_))
			if (last.ms - s.ms <= 100) {
				first = s;
				break;
			}
		if (last.ms > first.ms) {
			const double speed = -(last.along - first.along) / double(last.ms - first.ms); /* the list moves against the finger */
			if (std::abs(speed) >= FLING_START) {
				flingBar_ = bar;
				flingSpeed_ = speed;
				flingClock_.start();
				flingTimer_.start();
			}
		}
	}
	target_ = nullptr;
	viewport_ = nullptr;
	selectFrom_ = QPersistentModelIndex();
	return true;
}

void TouchScroll::moveTo(const QPointF &global, quint64 timestamp) {
	/* the same update twice (the window's and a widget's) or one older than the last: a step back in time made it shake */
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
	if (axis_ == 0) {
		/* the axis once the finger has gone past a tap: the one it went most along; the list follows from here */
		if (!moved_) return;
		const QPointF d = global - pressGlobal_;
		axis_ = std::abs(d.x()) > std::abs(d.y()) ? 1 : 2;
		lastGlobal_ = global;
		if (QScrollBar *bar = axisBar()) place_ = bar->value();
	}
	QScrollBar *bar = axisBar();
	if (!bar) return;
	const double along = axis_ == 2 ? global.y() : global.x();
	const double step = along - (axis_ == 2 ? lastGlobal_.y() : lastGlobal_.x());
	lastGlobal_ = global;
	samples_.append({ timestamp, along });
	if (samples_.size() > 16) samples_.removeFirst();
	/* the list moves with the finger: its content up as the finger goes up, so the scroll bar's value goes down */
	if (step != 0 && !scrollTo(bar, place_ - step)) flashEnd(bar);
}

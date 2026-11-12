/* SPDX-License-Identifier: Apache-2.0 */
#include "ui/touch_scroll.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QScroller>
#include <QScrollerProperties>

void TouchScroll::install(QApplication &app) {
	static bool installed = false;
	if (installed) return;
	installed = true;
	app.installEventFilter(new TouchScroll(&app));
}

bool TouchScroll::eventFilter(QObject *watched, QEvent *event) {
	if (event->type() != QEvent::Polish) return false;
	auto *area = qobject_cast<QAbstractScrollArea *>(watched);
	if (!area || !area->viewport()) return false;
	QWidget *viewport = area->viewport();
	if (QScroller::grabbedGesture(viewport) != Qt::GestureType(0)) return false; /* polished again: grabbed already */
	QScroller::grabGesture(viewport, QScroller::TouchGesture);
	/* no bounce past the ends: the views draw nothing there, and a table that springs back reads as a fault */
	QScrollerProperties properties = QScroller::scroller(viewport)->scrollerProperties();
	const QVariant off = QVariant::fromValue(QScrollerProperties::OvershootAlwaysOff);
	properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, off);
	properties.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, off);
	QScroller::scroller(viewport)->setScrollerProperties(properties);
	return false;
}

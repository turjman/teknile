/* SPDX-License-Identifier: Apache-2.0 */
/* The fold button: see fold_button.h. */
#include "ui/fold_button.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>

#include "ui/theme.h"

namespace {

/* a colour a share of the way toward another: the lit fill, a step from the card's */
QColor toward(const QColor &from, const QColor &to, qreal share) {
	return QColor::fromRgbF(float(from.redF() + (to.redF() - from.redF()) * share),
			float(from.greenF() + (to.greenF() - from.greenF()) * share),
			float(from.blueF() + (to.blueF() - from.blueF()) * share));
}

} // namespace

FoldButton::FoldButton(int side, QWidget *parent) : QAbstractButton(parent), side_(side) {
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_Hover);
	setFixedSize(side, side);
}

void FoldButton::setPoints(Points points) {
	if (points == points_) return;
	points_ = points;
	update();
}

void FoldButton::setOpacity(qreal opacity) {
	opacity = std::clamp(opacity, 0.0, 1.0);
	if (opacity == opacity_) return;
	opacity_ = opacity;
	update();
}

bool FoldButton::pointsLeft() const {
	if (points_ == Points::Down || points_ == Points::Up) return false;
	return (points_ == Points::Start) != isRightToLeft();
}

QSize FoldButton::sizeHint() const { return QSize(side_, side_); }

void FoldButton::enterEvent(QEnterEvent *event) {
	QAbstractButton::enterEvent(event);
	update();
}

void FoldButton::leaveEvent(QEvent *event) {
	QAbstractButton::leaveEvent(event);
	update();
}

void FoldButton::paintEvent(QPaintEvent *) {
	const ThemeColors &c = Theme::colors();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setOpacity(opacity_);
	const bool pressed = isDown(), lit = underMouse() && isEnabled() && !pressed;
	const bool ring = property("keyFocus").toBool() && hasFocus();
	/* the frame: a button's at rest, the accent's edge under the mouse, the accent's fill while pressed */
	const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = side_ >= 28 ? 8.0 : side_ / 4.0;
	const QColor fill = pressed ? c.accentFill : lit ? toward(c.surface2, c.text, 0.08) : c.surface2;
	const QColor edge = ring ? c.text : pressed ? c.accentFill : lit ? c.accent : c.border;
	p.setPen(QPen(edge, ring ? 1.5 : 1.0));
	p.setBrush(fill);
	p.drawRoundedRect(box.adjusted(ring ? 0.25 : 0, ring ? 0.25 : 0, ring ? -0.25 : 0, ring ? -0.25 : 0), radius, radius);
	/* the chevron, in a 24-unit square as an icon is drawn, the square 5/8 of the button */
	const QColor ink = !isEnabled() ? c.muted : pressed ? QColor(Qt::white) : lit ? c.text : c.muted;
	const qreal icon = side_ * 0.625, scale = icon / 24.0;
	const QPointF origin((width() - icon) / 2.0, (height() - icon) / 2.0);
	QPolygonF chevron;
	switch (points_) {
	case Points::Down: chevron << QPointF(6, 9.5) << QPointF(12, 15) << QPointF(18, 9.5); break;
	case Points::Up: chevron << QPointF(6, 14.5) << QPointF(12, 9) << QPointF(18, 14.5); break;
	default:
		if (pointsLeft()) chevron << QPointF(14.5, 6) << QPointF(9, 12) << QPointF(14.5, 18);
		else chevron << QPointF(9.5, 6) << QPointF(15, 12) << QPointF(9.5, 18);
		break;
	}
	for (QPointF &point : chevron) point = origin + point * scale;
	p.setPen(QPen(ink, std::max(1.5, 2.0 * scale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	p.drawPolyline(chevron);
}

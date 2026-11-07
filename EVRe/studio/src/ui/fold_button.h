/* SPDX-License-Identifier: Apache-2.0 */
/* A fold button: a chevron drawn with lines on a small rounded square, one control for the sidebar's fold, the thin
 * bar's open and the quick-write panel's title, so the three read as one thing. Drawn rather than a text glyph ("‹",
 * "▾"): each font draws a glyph at its own size and height, and it never sat centred in its box.
 *
 * It looks like a button at rest (a frame), lit under the mouse (the accent's edge, as the theme's buttons), filled
 * with the accent while pressed, and ringed while it has the focus from the keyboard ([keyFocus], theme.h); the
 * mouse is a pointing hand over it. The chevron points toward the row's start or end (mirrored right to left, as the
 * row is), down or up. setOpacity fades it in: the sidebar's, once the sidebar has arrived. */
#pragma once

#include <QAbstractButton>

class FoldButton : public QAbstractButton {
	Q_OBJECT
public:
	enum class Points { Start, End, Down, Up };
	/* side: its width and height, logical px (32 in the sidebar; the quick-write panel's, its title's height) */
	explicit FoldButton(int side, QWidget *parent = nullptr);

	void setPoints(Points points);
	Points points() const { return points_; }
	void setOpacity(qreal opacity); /* 0 .. 1: how much of it is drawn */
	qreal opacity() const { return opacity_; }
	/* the chevron's direction on screen, after the mirroring: true for one that points left */
	bool pointsLeft() const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	int side_;
	Points points_ = Points::Start;
	qreal opacity_ = 1.0;
};

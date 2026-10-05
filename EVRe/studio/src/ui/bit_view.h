/* SPDX-License-Identifier: Apache-2.0 */
/* A register drawn as in a datasheet: the bit numbers, the fields across the
 * bits they take (name and value), and a cell per bit showing 0 / 1. Click a
 * bit to flip it, click a wider field for its values; the quick-write panel
 * writes it (read-modify-write) on writeField. 16 bits a line, the highest first.
 *
 * In edit mode (the Map editor) nothing is written: a drag across bit cells
 * chooses bits for a new field (bitsChosen), a click on a field picks it
 * (fieldPicked), and the field picked is drawn in the accent colour. */
#pragma once

#include <QWidget>

#include "model/device_map.h"

class QPainter;

class BitView : public QWidget {
	Q_OBJECT
public:
	explicit BitView(QWidget *parent = nullptr);
	void setRegister(const RegDef &definition);
	void setValue(quint64 value, bool valid);
	void setEditMode(bool on);
	void setPickedField(int index); /* the field drawn as picked (edit mode), -1 = none */

	/* where things are drawn (for tests and tooltips) */
	QRect bitCell(int bit) const;
	QRect fieldCell(const QString &name) const; /* its first part */

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	/* the user asks for `width` bits from `lsb` up to become `value`; the other bits stay */
	void writeField(int lsb, int width, quint64 value);
	/* edit mode: bits dragged across (a new field on them), and a field clicked (its index) */
	void bitsChosen(int lsb, int width);
	void fieldPicked(int index);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEvent(QEvent *) override;
	bool event(QEvent *e) override; /* tooltips */

private:
	/* each line, top to bottom: the bit numbers, the fields, the bit cells, a gap */
	static constexpr int NUMBER_HEIGHT = 16, FIELD_HEIGHT = 26, BIT_HEIGHT = 26, LINE_GAP = 8;
	static constexpr int LINE_HEIGHT = NUMBER_HEIGHT + FIELD_HEIGHT + BIT_HEIGHT + LINE_GAP;

	/* layout */
	int bitCount() const;
	int bitsPerLine() const;
	int lineCount() const;
	int lineTop(int line) const;
	int lineOfBit(int bit) const;
	int highestBitOfLine(int line) const;
	int lowestBitOfLine(int line) const;
	double cellWidth() const;
	int bitAtX(double x, int line) const;

	/* fields */
	const BitField *fieldAt(int bit) const;
	quint64 fieldValue(const BitField &f) const;
	QString fieldText(const BitField &f) const;

	/* drawing, one line at a time */
	void drawBitNumbers(QPainter &p, int line) const;
	void drawFields(QPainter &p, int line) const;
	void drawBitCells(QPainter &p, int line) const;

	/* the mouse */
	void updateHover(const QPoint &pos);
	QString toolTipText() const;
	void clickField(const BitField &f, const QPoint &globalPos);
	void askFieldValue(const BitField &f, quint64 current);

	RegDef def_;
	quint64 value_ = 0;
	bool valid_ = false;
	int hoverBit_ = -1;       /* the bit under the mouse, -1 = none */
	bool hoverField_ = false; /* the mouse is on that bit's field, not on its cell */
	bool editMode_ = false;
	int dragFrom_ = -1, dragTo_ = -1; /* edit mode: the bits being dragged across, -1 = none */
	int pickedField_ = -1;
};

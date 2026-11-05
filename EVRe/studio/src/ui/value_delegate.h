/* SPDX-License-Identifier: Apache-2.0 */
/* The Value column of the register table: how a value is drawn and edited.
 *
 * A changed value glows (RegisterModel::GlowRole): a faint accent tint, a
 * 3 px accent bar on the cell's leading edge (the right one, right to left),
 * the text in a readable accent while the glow is strong, and for a plain
 * number a triangle, up or down, the way it moved. The triangle has a slot of
 * its own at the right end, kept on every row, so nothing moves when it shows.
 *
 * A value with decoded fields or an enum name gets an (i) at its right;
 * hovering the (i) shows them, one per line, while the rest of the cell keeps
 * the register's own tooltip (description, raw bytes, age, errors).
 *
 * The editor remembers the raw value the edit started from, so a change by
 * the device or another client meanwhile is caught before writing, and a
 * refresh while typing does not overwrite the text. The edit reaches the
 * model as RegisterModel::WriteRole { text, base }. */
#pragma once

#include <QStyledItemDelegate>

struct ThemeColors;

class ValueDelegate : public QStyledItemDelegate {
	Q_OBJECT
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	/* a selected row keeps the cell's own text colour (ForegroundRole: grey when stale, red on an error), as
	 * HighlightedText; the Decoded column's delegate does the same */
	static void keepColourWhenSelected(QStyleOptionViewItem *option, const QModelIndex &index);
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QString displayText(const QVariant &value, const QLocale &locale) const override;
	bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
			const QModelIndex &index) override;

	QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	void setEditorData(QWidget *editor, const QModelIndex &index) const override;
	void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;

protected:
	void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

public:
	/* where a Value cell's parts go (tests too): the value's text, and the glow's triangle and bar */
	static QRect valueRect(const QRect &cell, const QFont &font);
	static QRectF arrowRect(const QRect &cell, const QFont &font);
	static QRect barRect(const QRect &cell, Qt::LayoutDirection direction);
	/* a glowing value's text: the accent's blue made readable, 4.5:1 on the glow's tint in both themes */
	static QColor glowText(const ThemeColors &c);
	static constexpr double GLOW_TINT = 0.12; /* the tint's alpha at full glow */
	static constexpr double GLOW_TEXT = 0.35; /* the text is in glowText while the glow is above this */
	static constexpr int BAR_WIDTH = 3;

private:
	static constexpr int INFO_WIDTH = 20; /* room for the (i) at the right of every value */
	static constexpr int ARROW_GAP = 6;   /* between the triangle and the (i)'s room (the cell's padding is before it) */
	static double arrowSide(const QFont &font); /* the triangle: about the digits' height, 12 px at 100 % */
	static int arrowSlot(const QFont &font);    /* its room, kept on every row: the side and the gap */
	static void paintArrow(QPainter *painter, const QRectF &box, bool up, const QColor &colour);

	static QString decodedText(const QModelIndex &index); /* the Decoded column's text: empty = no (i) */
	static void paintInfoMark(QPainter *painter, const QRect &cell, const QFont &font);
	static QString fieldsToolTip(const QModelIndex &index, const QString &decoded);
};

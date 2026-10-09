/* SPDX-License-Identifier: Apache-2.0 */
/* The Value column of the register table: how a value is drawn and edited.
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

class ValueDelegate : public QStyledItemDelegate {
	Q_OBJECT
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QString displayText(const QVariant &value, const QLocale &locale) const override;
	bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
			const QModelIndex &index) override;

	QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	void setEditorData(QWidget *editor, const QModelIndex &index) const override;
	void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;

private:
	static constexpr int INFO_WIDTH = 20; /* room for the (i) at the right of every value */

	static QString decodedText(const QModelIndex &index); /* the Decoded column's text: empty = no (i) */
	static void paintInfoMark(QPainter *painter, const QRect &cell, const QFont &font);
	static QString fieldsToolTip(const QModelIndex &index, const QString &decoded);
};

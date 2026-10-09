/* SPDX-License-Identifier: Apache-2.0 */
/* The Value column's drawing and editor: see value_delegate.h. */
#include "ui/value_delegate.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QHelpEvent>
#include <QLineEdit>
#include <QPainter>
#include <QToolTip>

#include "model/register_model.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

/* the whole cell's background (glow, selection), the value left of the (i), then the (i) */
void ValueDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QStyleOptionViewItem cell(option);
	initStyleOption(&cell, index);
	const QWidget *widget = option.widget;
	QStyle *style = widget ? widget->style() : QApplication::style();
	style->drawPrimitive(QStyle::PE_PanelItemViewItem, &cell, painter, widget);
	QStyleOptionViewItem value(cell);
	value.rect.adjust(0, 0, -INFO_WIDTH, 0);
	value.backgroundBrush = Qt::NoBrush;
	style->drawControl(QStyle::CE_ItemViewItem, &value, painter, widget);
	if (!decodedText(index).isEmpty()) paintInfoMark(painter, cell.rect, cell.font);
}

/* a small circled "i" in the accent colour, centred in the room at the cell's right */
void ValueDelegate::paintInfoMark(QPainter *painter, const QRect &cell, const QFont &font) {
	const QRectF circle(cell.right() - INFO_WIDTH + 3.5, cell.center().y() - 7, 14, 14);
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(QPen(Theme::colors().accent, 1.3));
	painter->setBrush(Qt::NoBrush);
	painter->drawEllipse(circle);
	QFont letter = font;
	letter.setFamily(QStringLiteral("Segoe UI"));
	letter.setBold(true);
	letter.setPixelSize(10);
	painter->setFont(letter);
	painter->drawText(circle, Qt::AlignCenter, QStringLiteral("i"));
	painter->restore();
}

/* "-1500" in Arabic too, not "1500-": drawn as one left-to-right piece, the data plain (Ctrl+C copies it so) */
QString ValueDelegate::displayText(const QVariant &value, const QLocale &locale) const {
	return ltrPiece(QStyledItemDelegate::displayText(value, locale));
}

QSize ValueDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QSize size = QStyledItemDelegate::sizeHint(option, index);
	size.rwidth() += INFO_WIDTH;
	return size;
}

/* hovering the (i): the decoded fields; the rest of the cell: the model's tooltip */
bool ValueDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
		const QModelIndex &index) {
	const QString decoded = decodedText(index);
	const QRect cell = view->visualRect(index);
	const bool onInfo = event->pos().x() >= cell.right() - INFO_WIDTH;
	if (event->type() != QEvent::ToolTip || decoded.isEmpty() || !onInfo)
		return QStyledItemDelegate::helpEvent(event, view, option, index);
	QToolTip::showText(event->globalPos(), fieldsToolTip(index, decoded), view,
			QRect(cell.right() - INFO_WIDTH, cell.top(), INFO_WIDTH + 1, cell.height()));
	return true;
}

/* the register's name, value and unit over a line, then the decoded fields one per line */
QString ValueDelegate::fieldsToolTip(const QModelIndex &index, const QString &decoded) {
	const QString name = index.siblingAtColumn(RegisterModel::ColName).data().toString();
	const QString unit = index.siblingAtColumn(RegisterModel::ColUnit).data().toString();
	QString html = QStringLiteral("<b>%1</b> &nbsp;%2 %3<hr>").arg(name.toHtmlEscaped(),
			index.data(Qt::DisplayRole).toString().toHtmlEscaped(), unit.toHtmlEscaped());
	html += decoded.toHtmlEscaped().replace(QLatin1String("  "), QLatin1String("<br>"));
	return html;
}

QString ValueDelegate::decodedText(const QModelIndex &index) {
	return index.siblingAtColumn(RegisterModel::ColDecoded).data(Qt::DisplayRole).toString();
}

QWidget *ValueDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
		const QModelIndex &index) const {
	QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
	editor->setProperty("evreBase", index.data(RegisterModel::RawRole));
	return editor;
}

/* only once: the refreshes while the editor is open leave the typed text alone */
void ValueDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
	if (editor->property("evreInit").toBool()) return;
	editor->setProperty("evreInit", true);
	QStyledItemDelegate::setEditorData(editor, index);
}

void ValueDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
	auto *line = qobject_cast<QLineEdit *>(editor);
	if (!line) return;
	QVariantMap edit;
	edit.insert(QStringLiteral("text"), line->text());
	edit.insert(QStringLiteral("base"), editor->property("evreBase"));
	model->setData(index, edit, RegisterModel::WriteRole);
}

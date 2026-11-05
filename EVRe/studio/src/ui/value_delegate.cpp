/* SPDX-License-Identifier: Apache-2.0 */
/* The Value column's drawing and editor: see value_delegate.h. */
#include "ui/value_delegate.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QHelpEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

#include "model/register_model.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

QColor withAlpha(QColor colour, double alpha) {
	colour.setAlphaF(float(std::clamp(alpha, 0.0, 1.0)));
	return colour;
}

} // namespace

/* the whole cell's panel (selection), the glow's tint and bar, the value left of the triangle's slot, the triangle,
 * then the (i). The glow is painted here: the style sheet's item rule (QTableView::item) draws the cell's panel itself
 * and would leave a model's background out */
void ValueDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QStyleOptionViewItem cell(option);
	initStyleOption(&cell, index);
	const QWidget *widget = option.widget;
	QStyle *style = widget ? widget->style() : QApplication::style();
	cell.backgroundBrush = Qt::NoBrush;
	style->drawPrimitive(QStyle::PE_PanelItemViewItem, &cell, painter, widget);
	const ThemeColors &colors = Theme::colors();
	const double glow = index.data(RegisterModel::GlowRole).toDouble();
	if (glow > 0) {
		painter->fillRect(cell.rect, withAlpha(colors.accent, GLOW_TINT * glow));
		painter->fillRect(barRect(cell.rect, cell.direction), withAlpha(colors.accent, glow));
	}
	QStyleOptionViewItem value(cell);
	value.rect = valueRect(cell.rect, cell.font);
	/* a selected row keeps its text colour: on the selection's tint the accent would fall under 4.5:1 */
	if (glow > GLOW_TEXT && !(cell.state & QStyle::State_Selected)) value.palette.setColor(QPalette::Text, glowText(colors));
	style->drawControl(QStyle::CE_ItemViewItem, &value, painter, widget);
	const int direction = glow > 0 ? index.data(RegisterModel::GlowDirectionRole).toInt() : 0;
	if (direction != 0) paintArrow(painter, arrowRect(cell.rect, cell.font), direction > 0, withAlpha(colors.accent, glow));
	if (!decodedText(index).isEmpty()) paintInfoMark(painter, cell.rect, cell.font);
}

/* about the digits' height: 12 px at 100 % with the table's 10 pt font, growing with the font (the system's scaling
 * grows the painter's pixels: the triangle stays as sharp) */
double ValueDelegate::arrowSide(const QFont &font) {
	return std::max(8.0, std::round(QFontMetricsF(font).capHeight() * 1.3));
}

int ValueDelegate::arrowSlot(const QFont &font) { return int(std::ceil(arrowSide(font))) + ARROW_GAP; }

/* the value's room: the cell less the (i)'s and the triangle's, on every row and at every moment, so a number never
 * moves when its triangle shows and right-aligned numbers stay lined up */
QRect ValueDelegate::valueRect(const QRect &cell, const QFont &font) {
	return cell.adjusted(0, 0, -(INFO_WIDTH + arrowSlot(font)), 0);
}

/* right after the number (the cell's padding is the gap before it), centred on the digits: the text is centred in the
 * cell, its digits a cap height over its baseline. At the right in Arabic too: the values read left to right there. */
QRectF ValueDelegate::arrowRect(const QRect &cell, const QFont &font) {
	const QFontMetricsF metrics(font);
	const double side = arrowSide(font);
	const double baseline = cell.top() + (cell.height() - metrics.height()) / 2.0 + metrics.ascent();
	const double middle = baseline - metrics.capHeight() / 2.0;
	const double right = cell.right() + 1 - INFO_WIDTH - ARROW_GAP;
	return QRectF(right - side, middle - side / 2.0, side, side);
}

/* the cell's leading edge: the left, the right one right to left (Arabic) */
QRect ValueDelegate::barRect(const QRect &cell, Qt::LayoutDirection direction) {
	return direction == Qt::RightToLeft ? QRect(cell.right() + 1 - BAR_WIDTH, cell.top(), BAR_WIDTH, cell.height())
			: QRect(cell.left(), cell.top(), BAR_WIDTH, cell.height());
}

/* Listen's blue: the accent made readable as text on its own tint (4.5:1 there in both themes); the accent itself is
 * 4.4:1 on the glow's tint of a light table's alternate row */
QColor ValueDelegate::glowText(const ThemeColors &c) { return c.listen; }

/* a filled triangle, a path (a font's ▲ differs from font to font and sits on the baseline): its tip up or down,
 * 10/12 of the box wide and 8/12 high, as the preview's */
void ValueDelegate::paintArrow(QPainter *painter, const QRectF &box, bool up, const QColor &colour) {
	const double unit = box.width() / 12.0;
	const double tip = up ? 2 * unit : 10 * unit, base = up ? 10 * unit : 2 * unit;
	QPainterPath path;
	path.moveTo(box.left() + 6 * unit, box.top() + tip);
	path.lineTo(box.left() + 11 * unit, box.top() + base);
	path.lineTo(box.left() + unit, box.top() + base);
	path.closeSubpath();
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->fillPath(path, colour);
	painter->restore();
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

void ValueDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const {
	QStyledItemDelegate::initStyleOption(option, index);
	keepColourWhenSelected(option, index);
}

/* a selected row keeps the cell's own colour (a stale value's grey, an error's red): the selection's text colour
 * (selection-color in the style sheet) would put every cell of the row in the plain text colour */
void ValueDelegate::keepColourWhenSelected(QStyleOptionViewItem *option, const QModelIndex &index) {
	const QVariant foreground = index.data(Qt::ForegroundRole);
	if (foreground.canConvert<QBrush>()) option->palette.setBrush(QPalette::HighlightedText, qvariant_cast<QBrush>(foreground));
}

/* "-1500" in Arabic too, not "1500-": drawn as one left-to-right piece, the data plain (Ctrl+C copies it so) */
QString ValueDelegate::displayText(const QVariant &value, const QLocale &locale) const {
	return ltrPiece(QStyledItemDelegate::displayText(value, locale));
}

QSize ValueDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
	QSize size = QStyledItemDelegate::sizeHint(option, index);
	const QVariant font = index.data(Qt::FontRole);
	size.rwidth() += INFO_WIDTH + arrowSlot(font.canConvert<QFont>() ? qvariant_cast<QFont>(font) : option.font);
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

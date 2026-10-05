/* SPDX-License-Identifier: Apache-2.0 */
/* The register drawn bit by bit: see bit_view.h. */
#include "ui/bit_view.h"

#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>

#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* a single bit with no value names: shown by its name, a click flips it */
bool isFlag(const BitField &f) { return f.width == 1 && f.values.isEmpty(); }

} // namespace

BitView::BitView(QWidget *parent) : QWidget(parent) {
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BitView::setRegister(const RegDef &definition) {
	def_ = definition;
	hoverBit_ = -1;
	updateGeometry();
	update();
}

void BitView::setEditMode(bool on) {
	editMode_ = on;
	update();
}

void BitView::setPickedField(int index) {
	if (index == pickedField_) return;
	pickedField_ = index;
	update();
}

void BitView::setValue(quint64 value, bool valid) {
	if (value == value_ && valid == valid_) return;
	value_ = value;
	valid_ = valid;
	update();
}

QSize BitView::sizeHint() const { return { 16 * 46, std::max(0, lineCount() * LINE_HEIGHT - LINE_GAP) }; }
QSize BitView::minimumSizeHint() const { return { bitsPerLine() * 30, sizeHint().height() }; }

/* ------------------------------------------------------------------- layout */

/* a number's bits, 64 at most (its value is a quint64); a bytes register has none: 255 bytes would be 128 lines,
 * a page taller than any screen (a tab widget is as tall as its tallest page, the window with it) */
int BitView::bitCount() const { return def_.isNumeric() ? 8 * std::clamp(def_.size, 0, 8) : 0; }
int BitView::bitsPerLine() const { return std::min(16, bitCount()); }
int BitView::lineCount() const { return bitCount() > 0 ? (bitCount() + bitsPerLine() - 1) / bitsPerLine() : 0; }
int BitView::lineTop(int line) const { return line * LINE_HEIGHT; }
int BitView::lineOfBit(int bit) const { return (bitCount() - 1 - bit) / bitsPerLine(); }
int BitView::highestBitOfLine(int line) const { return bitCount() - 1 - line * bitsPerLine(); }
int BitView::lowestBitOfLine(int line) const { return std::max(0, highestBitOfLine(line) - bitsPerLine() + 1); }
double BitView::cellWidth() const { return double(width() - 1) / bitsPerLine(); }

int BitView::bitAtX(double x, int line) const {
	const int column = std::clamp(int(x / cellWidth()), 0, bitsPerLine() - 1);
	return highestBitOfLine(line) - column;
}

QRect BitView::bitCell(int bit) const {
	const int column = (bitCount() - 1 - bit) % bitsPerLine();
	const double w = cellWidth();
	const int left = int(column * w);
	const int top = lineTop(lineOfBit(bit)) + NUMBER_HEIGHT + FIELD_HEIGHT;
	return QRect(left, top, int((column + 1) * w) - left, BIT_HEIGHT);
}

QRect BitView::fieldCell(const QString &name) const {
	for (const BitField &f : def_.fields) {
		if (f.name != name) continue;
		const int msb = f.lsb + f.width - 1;
		const int line = lineOfBit(msb);
		const QRect first = bitCell(msb), last = bitCell(std::max(f.lsb, lowestBitOfLine(line)));
		return QRect(first.left(), lineTop(line) + NUMBER_HEIGHT, last.right() - first.left() + 1, FIELD_HEIGHT);
	}
	return {};
}

/* ------------------------------------------------------------------- fields */

const BitField *BitView::fieldAt(int bit) const {
	for (const BitField &f : def_.fields)
		if (bit >= f.lsb && bit < f.lsb + f.width) return &f;
	return nullptr;
}

quint64 BitView::fieldValue(const BitField &f) const { return (value_ >> f.lsb) & bitMask(f.width); }

/* a flag, or any field while the value is unknown: its name; otherwise "NAME = value" (its name if it has one) */
QString BitView::fieldText(const BitField &f) const {
	if (isFlag(f) || !valid_) return f.name;
	const quint64 value = fieldValue(f);
	return QStringLiteral("%1 = %2").arg(f.name, f.values.value(qint64(value), QString::number(value)));
}

/* ------------------------------------------------------------------ drawing */

void BitView::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	for (int line = 0; line < lineCount(); line++) {
		drawBitNumbers(p, line);
		drawFields(p, line);
		drawBitCells(p, line);
	}
}

void BitView::drawBitNumbers(QPainter &p, int line) const {
	QFont small = font();
	small.setPointSizeF(font().pointSizeF() * 0.8);
	p.setFont(small);
	p.setPen(Theme::colors().muted);
	for (int bit = highestBitOfLine(line); bit >= lowestBitOfLine(line); bit--) {
		const QRect cell = bitCell(bit);
		const QRect number(cell.left(), lineTop(line), cell.width(), NUMBER_HEIGHT);
		p.drawText(number, Qt::AlignCenter, QString::number(bit));
	}
}

/* One box per field across its bits (a field that goes on to the next line is cut at the end of
 * this one); a bit in no field gets a box of its own with a dash. */
void BitView::drawFields(QPainter &p, int line) const {
	const ThemeColors &c = Theme::colors();
	const bool enabled = isEnabled();
	const int top = lineTop(line) + NUMBER_HEIGHT;
	const int lowest = lowestBitOfLine(line);
	p.setFont(font());
	for (int high = highestBitOfLine(line); high >= lowest;) {
		const BitField *f = fieldAt(high);
		const int low = f ? std::max(lowest, f->lsb) : high;
		const QRect first = bitCell(high), last = bitCell(low);
		const QRectF box(first.left() + 1.5, top + 1.5, last.right() - first.left() - 2, FIELD_HEIGHT - 5);
		const bool hovered = f && hoverField_ && hoverBit_ <= high && hoverBit_ >= low;
		const bool picked = f && editMode_ && int(f - def_.fields.data()) == pickedField_;
		p.setPen(QPen(hovered || picked ? c.accent : c.border, picked ? 2 : 1));
		p.setBrush(f ? c.surface2 : Qt::transparent);
		p.drawRoundedRect(box, 5, 5);
		p.setPen(f && enabled ? c.text : c.muted);
		const QString text = f ? fieldText(*f) : QStringLiteral("—");
		p.drawText(box.adjusted(4, 0, -4, 0), Qt::AlignCenter,
				p.fontMetrics().elidedText(text, Qt::ElideRight, int(box.width()) - 8));
		high = low - 1;
	}
}

/* A set bit is filled with the accent colour; "·" for every bit while the value is unknown. */
void BitView::drawBitCells(QPainter &p, int line) const {
	const ThemeColors &c = Theme::colors();
	const bool enabled = isEnabled();
	QFont mono(QStringLiteral("Cascadia Mono"));
	mono.setStyleHint(QFont::Monospace);
	mono.setBold(true);
	p.setFont(mono);
	for (int bit = highestBitOfLine(line); bit >= lowestBitOfLine(line); bit--) {
		const QRect cell = bitCell(bit);
		const bool set = valid_ && ((value_ >> bit) & 1);
		const bool hovered = !hoverField_ && hoverBit_ == bit;
		const bool dragged = dragFrom_ >= 0 && bit >= std::min(dragFrom_, dragTo_) && bit <= std::max(dragFrom_, dragTo_);
		const QRectF box(cell.left() + 1.5, cell.top() + 1.5, cell.width() - 3, cell.height() - 3);
		p.setPen(QPen((hovered || dragged) && enabled ? c.accent : c.border, hovered || dragged ? 1.5 : 1));
		QColor fill = set ? (enabled ? c.accentFill : c.muted) : c.surface; /* a 1 is white text: on accentFill */
		if (dragged) {
			fill = c.accent;
			fill.setAlpha(set ? 255 : 90);
		}
		p.setBrush(fill);
		p.drawRoundedRect(box, 5, 5);
		p.setPen(set ? QColor(Qt::white) : (enabled ? c.text : c.muted));
		p.drawText(box, Qt::AlignCenter, valid_ ? QString::number(set ? 1 : 0) : QStringLiteral("·"));
	}
}

/* ---------------------------------------------------------------- the mouse */

/* Finds the bit under pos, and whether pos is on its field (only a bit that is in a field
 * counts there) or on its cell. */
void BitView::updateHover(const QPoint &pos) {
	int bit = -1;
	bool onField = false;
	for (int line = 0; line < lineCount(); line++) {
		const int fieldTop = lineTop(line) + NUMBER_HEIGHT, cellTop = fieldTop + FIELD_HEIGHT;
		if (pos.y() >= fieldTop && pos.y() < cellTop) {
			bit = bitAtX(pos.x(), line);
			onField = true;
			break;
		}
		if (pos.y() >= cellTop && pos.y() < cellTop + BIT_HEIGHT) {
			bit = bitAtX(pos.x(), line);
			break;
		}
	}
	if (onField && !fieldAt(bit)) bit = -1;
	if (bit == hoverBit_ && onField == hoverField_) return;
	hoverBit_ = bit;
	hoverField_ = onField;
	setCursor(bit >= 0 && isEnabled() ? Qt::PointingHandCursor : Qt::ArrowCursor);
	update();
}

void BitView::mouseMoveEvent(QMouseEvent *e) {
	updateHover(e->position().toPoint());
	if (dragFrom_ >= 0 && hoverBit_ >= 0 && !hoverField_ && hoverBit_ != dragTo_) {
		dragTo_ = hoverBit_;
		update();
	}
}

void BitView::mouseReleaseEvent(QMouseEvent *e) {
	if (dragFrom_ < 0 || e->button() != Qt::LeftButton) return;
	const int lsb = std::min(dragFrom_, dragTo_), width = std::abs(dragFrom_ - dragTo_) + 1;
	dragFrom_ = dragTo_ = -1;
	update();
	emit bitsChosen(lsb, width);
}

void BitView::leaveEvent(QEvent *) {
	hoverBit_ = -1;
	update();
}

void BitView::mousePressEvent(QMouseEvent *e) {
	updateHover(e->position().toPoint());
	if (editMode_) {
		if (!isEnabled() || hoverBit_ < 0 || e->button() != Qt::LeftButton) return;
		if (hoverField_) {
			if (const BitField *f = fieldAt(hoverBit_)) emit fieldPicked(int(f - def_.fields.data()));
		} else {
			dragFrom_ = dragTo_ = hoverBit_;
			update();
		}
		return;
	}
	if (!isEnabled() || !valid_ || hoverBit_ < 0 || e->button() != Qt::LeftButton) return;
	if (!hoverField_) emit writeField(hoverBit_, 1, ((value_ >> hoverBit_) & 1) ? 0 : 1);
	else if (const BitField *f = fieldAt(hoverBit_)) clickField(*f, e->globalPosition().toPoint());
}

/* A flag flips at once; any other field gets a menu of its named values and "Value…". */
void BitView::clickField(const BitField &f, const QPoint &globalPos) {
	const quint64 current = fieldValue(f);
	if (isFlag(f)) {
		emit writeField(f.lsb, 1, current ? 0 : 1);
		return;
	}
	QMenu menu(this);
	for (auto it = f.values.begin(); it != f.values.end(); ++it) {
		const quint64 value = quint64(it.key());
		QAction *action = menu.addAction(noMnemonic(QStringLiteral("%1  (%2)").arg(it.value()).arg(it.key())));
		action->setCheckable(true);
		action->setChecked(value == current);
		connect(action, &QAction::triggered, this, [this, lsb = f.lsb, width = f.width, value] {
			emit writeField(lsb, width, value);
		});
	}
	if (!f.values.isEmpty()) menu.addSeparator();
	menu.addAction(tr("Value…"), this, [this, f, current] { askFieldValue(f, current); });
	menu.exec(globalPos);
}

void BitView::askFieldValue(const BitField &f, quint64 current) {
	const quint64 max = bitMask(f.width);
	bool ok = false;
	const int value = QInputDialog::getInt(this, f.name,
			tr("%1 (bits %2:%3), 0 … %4").arg(f.name).arg(f.lsb + f.width - 1).arg(f.lsb).arg(max),
			int(current), 0, int(std::min<quint64>(max, 0x7FFFFFFF)), 1, &ok);
	if (ok) emit writeField(f.lsb, f.width, quint64(value));
}

bool BitView::event(QEvent *e) {
	if (e->type() != QEvent::ToolTip) return QWidget::event(e);
	const auto *help = static_cast<QHelpEvent *>(e);
	updateHover(help->pos());
	if (hoverBit_ < 0) QToolTip::hideText();
	else QToolTip::showText(help->globalPos(), toolTipText(), this);
	return true;
}

/* The field under the mouse: its bits, its value names, and what a click does. */
QString BitView::toolTipText() const {
	const BitField *f = fieldAt(hoverBit_);
	if (editMode_ && (!f || !hoverField_))
		return tr("bit %1: drag across bits to make a field of them").arg(hoverBit_);
	if (!f) return tr("in no field");
	QString text = hoverField_ ? QString() : tr("bit %1: click to flip<br>").arg(hoverBit_);
	text += QStringLiteral("<b>%1</b> &nbsp;").arg(f->name.toHtmlEscaped());
	text += f->width == 1 ? tr("bit %1").arg(f->lsb) : tr("bits %1:%2").arg(f->lsb + f->width - 1).arg(f->lsb);
	for (auto it = f->values.begin(); it != f->values.end(); ++it)
		text += QStringLiteral("<br>%1 = %2").arg(it.key()).arg(it.value().toHtmlEscaped());
	if (editMode_) text += tr("<br><i>click: edit this field</i>");
	else if (hoverField_) text += isFlag(*f) ? tr("<br><i>click to flip</i>") : tr("<br><i>click: choose a value</i>");
	return text;
}

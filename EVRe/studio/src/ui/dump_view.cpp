/* SPDX-License-Identifier: Apache-2.0 */
/* The Dump tab's picture: see dump_view.h. */
#include "ui/dump_view.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTime>
#include <QTimer>
#include <QToolTip>
#include <algorithm>

#include "evre/frame.h"
#include "model/register_model.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* the bytes of a READ and of its answer, as on the wire: one definition with the master's (evre::build) */
QByteArray requestFrame(const DumpBlock &b) { return evre::build(b.wireSlave, evre::READ, b.addr, b.size); }
QByteArray replyFrame(const DumpBlock &b) { return evre::build(b.wireSlave, evre::READ_RESP, b.addr, b.size, b.data); }
constexpr int REPLY_HEADER = 7; /* 7B SLAVE FN OFF(2) CNT(2): the CRC and the end byte follow the payload */
constexpr int FRAME_TAIL = 3;   /* CRC(2) 7D */
/* a text at the left of its room whatever its direction (the painter takes each text's own: paintEvent) */
constexpr Qt::Alignment LEFT = Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignVCenter;

/* a row's base address as the grid writes it: four digits, no 0x (the tooltip gives 0x) */
QString rowAddress(int base) { return QStringLiteral("%1").arg(base, 4, 16, QLatin1Char('0')).toUpper(); }

QString byteText(char byte) { return QStringLiteral("%1").arg(uchar(byte), 2, 16, QLatin1Char('0')).toUpper(); }

/* how old a reply is: "12 ms", "2.1 s" (units are not translated) */
QString ageText(double seconds) {
	seconds = std::max(0.0, seconds);
	if (seconds < 1.0) return QStringLiteral("%1 ms").arg(qRound(seconds * 1000));
	return QStringLiteral("%1 s").arg(seconds, 0, 'f', seconds < 100 ? 1 : 0);
}

/* a read's place in the glow's memory: the same read again (its kind, device and address) is compared with the last */
quint32 readId(const DumpBlock &b) { return (quint32(b.kind) << 24) | (quint32(b.slave) << 16) | b.addr; }

constexpr qint64 NEVER_CHANGED = -1000000; /* a byte that has not changed since it was first shown */
constexpr int GLOW_TICK_MS = 50;           /* the glow fades in steps this long, only while a byte glows */

QColor blend(const QColor &over, const QColor &under, double alpha) {
	return QColor::fromRgbF(float(over.redF() * alpha + under.redF() * (1 - alpha)),
			float(over.greenF() * alpha + under.greenF() * (1 - alpha)),
			float(over.blueF() * alpha + under.blueF() * (1 - alpha)));
}

} // namespace

DumpView::DumpView(QWidget *parent) : QAbstractScrollArea(parent) {
	/* addresses, hex, names and values in their own order in any language (the grid's, not the window's) */
	setLayoutDirection(Qt::LeftToRight);
	viewport()->setLayoutDirection(Qt::LeftToRight); /* it is the one painted on */
	setFrameShape(QFrame::NoFrame);
	viewport()->setMouseTracking(true);
	viewport()->setAutoFillBackground(false);
	setFont(monospaceFont());
	drawnDark_ = Theme::isDark();
	glowClock_.start();
	glowTimer_ = new QTimer(this);
	glowTimer_->setInterval(GLOW_TICK_MS);
	connect(glowTimer_, &QTimer::timeout, this, [this] {
		if (!glowing()) glowTimer_->stop(); /* one more paint takes the last rings away */
		viewport()->update();
	});
}

/* ------------------------------------------------------------------- inputs */

void DumpView::setBlocks(const QVector<DumpBlock> &blocks) {
	const QVector<DumpBlock> was = blocks_;
	blocks_ = blocks;
	noteChanges(was);
	relayout();
	scrollToPending();
}

/* The bytes of a read that differ from its last answer start glowing; a read shown for the first time does not glow
 * (nothing changed, it only came), and reads gone are forgotten. */
void DumpView::noteChanges(const QVector<DumpBlock> &was) {
	QHash<quint32, const DumpBlock *> before;
	for (const DumpBlock &b : was) before.insert(readId(b), &b);
	QHash<quint32, QVector<qint64>> kept;
	const qint64 now = glowClock_.elapsed();
	bool changed = false;
	for (const DumpBlock &b : blocks_) {
		const quint32 id = readId(b);
		QVector<qint64> times = changedMs_.value(id);
		if (times.size() != b.size) times = QVector<qint64>(b.size, NEVER_CHANGED);
		const DumpBlock *old = before.value(id);
		if (old && old->size == b.size && old->data.size() >= b.size && b.data.size() >= b.size) {
			for (int i = 0; i < b.size; i++) {
				if (old->data[i] == b.data[i]) continue;
				times[i] = now;
				changed = true;
			}
		}
		kept.insert(id, times);
	}
	changedMs_ = kept;
	if (changed && !glowTimer_->isActive()) glowTimer_->start();
}

double DumpView::glow(int card, int offset) const {
	if (card < 0 || card >= blocks_.size()) return 0;
	const QVector<qint64> times = changedMs_.value(readId(blocks_[card]));
	if (offset < 0 || offset >= times.size()) return 0;
	const qint64 since = glowClock_.elapsed() - times[offset];
	return since >= RegisterModel::GLOW_MS ? 0.0 : 1.0 - double(since) / double(RegisterModel::GLOW_MS);
}

bool DumpView::glowing() const {
	const qint64 now = glowClock_.elapsed();
	for (const QVector<qint64> &times : changedMs_)
		for (qint64 t : times)
			if (now - t < RegisterModel::GLOW_MS) return true;
	return false;
}

void DumpView::setRegisters(const QVector<RegDef> &defs) {
	defs_ = defs;
	relayout();
}

void DumpView::setColours(const QHash<RegKey, QColor> &plotted) {
	if (plotted == plotted_) return;
	plotted_ = plotted;
	relayout();
}

void DumpView::setColourMode(ColourMode mode) {
	if (mode == colourMode_) return;
	colourMode_ = mode;
	relayout();
}

void DumpView::setHold(bool on) {
	if (on == hold_) return;
	hold_ = on;
	heldAt_ = on ? QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) : QString();
	relayout(); /* the reply lines' words, and with them the labels' width */
}

void DumpView::setConnected(bool on) {
	if (on == connected_) return;
	connected_ = on;
	relayout();
}

void DumpView::setClock(std::function<double()> now) { now_ = std::move(now); }
void DumpView::setStaleAfterMs(qint64 ms) { staleAfterMs_ = ms; }

void DumpView::select(RegKey key) {
	if (key == selected_) return;
	selected_ = key;
	viewport()->update();
}

void DumpView::setEmptyText(const QString &text) {
	emptyText_ = text;
	viewport()->update();
}

void DumpView::scrollToRegister(RegKey key) {
	scrollTo_ = key;
	scrollToPending();
}

/* The register's row in view, with its card's top when both fit: the request and reply lines say what it is. */
void DumpView::scrollToPending() {
	const int card = cardOf(scrollTo_);
	if (card < 0) return;
	const Card &c = cards_[card];
	const int addr = regKeyAddr(scrollTo_);
	int rowTop = c.rows.isEmpty() ? 0 : c.rows.first().top;
	for (const Row &row : c.rows)
		if (addr >= row.base && addr < row.base + 16) rowTop = row.top;
	const int top = c.top + rowTop; /* in the content */
	int value = c.top - CARD_GAP;
	if (top + 2 * lineHeight() - value > viewport()->height()) value = top - 2 * lineHeight();
	verticalScrollBar()->setValue(value);
	scrollTo_ = NO_REGISTER;
}

void DumpView::setHovered(RegKey key) {
	if (key == hovered_) return;
	hovered_ = key;
	/* a hand over what a click picks */
	if (key == NO_REGISTER) viewport()->unsetCursor();
	else viewport()->setCursor(Qt::PointingHandCursor);
	viewport()->update();
}

/* ------------------------------------------------------------------- layout */

int DumpView::lineHeight() const { return fontMetrics().height() + 6; }
/* a byte's cell: its two digits and room for a chip's edge and the 2 px between two chips */
int DumpView::cellWidth() const { return fontMetrics().horizontalAdvance(QStringLiteral("00")) + 10; }
int DumpView::gridLeft() const { return CARD_GAP + PAD + fontMetrics().horizontalAdvance(QStringLiteral("D000")) + 12; }
int DumpView::decodeLeft() const { return gridLeft() + 16 * cellWidth() + DECODE_GAP; }

int DumpView::contentWidth() const {
	const QFontMetrics fm = fontMetrics();
	const int decodeRight = decodeLeft() + 14 + fm.horizontalAdvance(QLatin1Char('M')) * (NAME_CHARS + VALUE_CHARS) + 10;
	const int frameRight = CARD_GAP + PAD + labelWidth_ + 10 * fm.horizontalAdvance(QStringLiteral("00 "));
	return std::max(decodeRight, frameRight) + PAD + CARD_GAP;
}

QPoint DumpView::origin() const { return { -horizontalScrollBar()->value(), -verticalScrollBar()->value() }; }

/* A plotted register takes the chart's colour (the chip, the dot and the legend agree); every other one the palette's
 * colour of its map row, the next one when a neighbour in the block has that colour already, so two chips side by
 * side never read as one. With Colour: plotted, the others are neutral. */
void DumpView::assignColours(Card &card) const {
	const QVector<QColor> &series = Theme::colors().series;
	const int n = int(card.regs.size());
	card.colours = QVector<QColor>(n);
	for (int i = 0; i < n; i++) card.colours[i] = plotted_.value(regKey(defs_[card.regs[i]]));
	if (colourMode_ == PlottedOnly || series.isEmpty()) return;
	for (int i = 0; i < n; i++) {
		if (card.colours[i].isValid()) continue;
		const int first = card.regs[i] % int(series.size());
		for (int t = 0; t < series.size(); t++) {
			const QColor c = series[(first + t) % series.size()];
			if ((i > 0 && card.colours[i - 1] == c) || (i + 1 < n && card.colours[i + 1] == c)) continue;
			card.colours[i] = c;
			break;
		}
	}
}

void DumpView::relayout() {
	const QFontMetrics fm = fontMetrics();
	const int line = lineHeight();
	cards_.clear();
	labelWidth_ = 0;
	int y = CARD_GAP;
	for (int i = 0; i < blocks_.size(); i++) {
		const DumpBlock &b = blocks_[i];
		Card card;
		card.top = y;
		int at = PAD;
		if (b.kind == DumpBlock::FastStream) {
			card.noteTop = at;
			at += line;
		} else {
			/* the registers wholly inside the block, in address order */
			for (int r = 0; r < defs_.size(); r++) {
				const RegDef &d = defs_[r];
				if (d.slave == b.slave && d.readable && d.size > 0 && d.addr >= b.addr
						&& int(d.addr) + d.size <= int(b.addr) + b.size)
					card.regs << r;
			}
			std::sort(card.regs.begin(), card.regs.end(), [this](int x, int z) { return defs_[x].addr < defs_[z].addr; });
			assignColours(card);
			if (b.kind == DumpBlock::Manual) {
				card.titleTop = at;
				at += line;
			}
			if (b.kind != DumpBlock::AutoSend) {
				card.requestTop = at;
				at += line;
				labelWidth_ = std::max(labelWidth_, fm.horizontalAdvance(requestLabel(b)));
			}
			card.replyTop = at;
			at += line + GRID_GAP;
			/* the reply's label with room for its usual state ("999 ms ago"); a longer one (an error, held) goes
			 * after the header bytes */
			labelWidth_ = std::max(labelWidth_, fm.horizontalAdvance(replyLabel(b) + QLatin1Char(' ')
					+ tr("%1 ago").arg(QStringLiteral("999 ms"))));
			/* the rows from the block's first to its last, aligned to 16; each as tall as its decode lines */
			card.gridTop = at;
			const int first = b.addr & ~0xF, last = (int(b.addr) + std::max(1, int(b.size)) - 1) & ~0xF;
			int reg = 0;
			for (int base = first; base <= last; base += 16) {
				Row row;
				row.base = base;
				row.top = at;
				while (reg < card.regs.size() && defs_[card.regs[reg]].addr < base + 16) row.starts << reg++;
				row.height = std::max(1, int(row.starts.size())) * line;
				at += row.height;
				card.rows << row;
			}
			if (b.data.size() >= b.size) {
				card.crcTop = at;
				at += line;
			}
		}
		card.height = at + PAD;
		y += card.height + CARD_GAP;
		cards_ << card;
	}
	labelWidth_ += 16;
	contentHeight_ = y;
	const QSize view = viewport()->size();
	verticalScrollBar()->setRange(0, std::max(0, contentHeight_ - view.height()));
	verticalScrollBar()->setPageStep(view.height());
	verticalScrollBar()->setSingleStep(line);
	horizontalScrollBar()->setRange(0, std::max(0, contentWidth() - view.width()));
	horizontalScrollBar()->setPageStep(view.width());
	horizontalScrollBar()->setSingleStep(cellWidth());
	viewport()->update();
}

int DumpView::regAt(int card, int offset) const {
	const DumpBlock &b = blocks_[card];
	const int addr = int(b.addr) + offset;
	const Card &c = cards_[card];
	for (int i = 0; i < c.regs.size(); i++) {
		const RegDef &d = defs_[c.regs[i]];
		if (addr >= d.addr && addr < int(d.addr) + d.size) return i;
	}
	return -1;
}

/* ------------------------------------------------------------- where things are */

QRect DumpView::cardRect(int card) const {
	if (card < 0 || card >= cards_.size()) return {};
	const QPoint o = origin();
	const int width = std::max(contentWidth(), viewport()->width()) - 2 * CARD_GAP;
	return QRect(o.x() + CARD_GAP, o.y() + cards_[card].top, width, cards_[card].height);
}

QRect DumpView::byteRect(int card, int offset) const {
	if (card < 0 || card >= cards_.size() || cards_[card].rows.isEmpty()) return {};
	const Card &c = cards_[card];
	const int addr = int(blocks_[card].addr) + offset;
	const int index = (addr - c.rows.first().base) / 16;
	if (addr < c.rows.first().base || index >= c.rows.size()) return {};
	const QPoint o = origin();
	return QRect(o.x() + gridLeft() + (addr & 0xF) * cellWidth(), o.y() + c.top + c.rows[index].top, cellWidth(),
			lineHeight());
}

/* at the column after the payload's last byte, all three on the line */
QRect DumpView::tailRect(int card) const {
	if (card < 0 || card >= cards_.size() || cards_[card].crcTop < 0) return {};
	const DumpBlock &b = blocks_[card];
	const int first = std::min((int(b.addr) + b.size) & 0xF, 16 - FRAME_TAIL);
	const QPoint o = origin();
	return QRect(o.x() + gridLeft() + first * cellWidth(), o.y() + cards_[card].top + cards_[card].crcTop,
			FRAME_TAIL * cellWidth(), lineHeight());
}

int DumpView::decodeLines(int card) const { return card < 0 || card >= cards_.size() ? 0 : int(cards_[card].regs.size()); }

QRect DumpView::decodeRect(int card, int line) const {
	if (line < 0 || line >= decodeLines(card)) return {};
	const Card &c = cards_[card];
	for (const Row &row : c.rows) {
		const int at = int(row.starts.indexOf(line));
		if (at < 0) continue;
		const QPoint o = origin();
		const int width = 14 + fontMetrics().horizontalAdvance(QLatin1Char('M')) * (NAME_CHARS + VALUE_CHARS) + 10;
		return QRect(o.x() + decodeLeft(), o.y() + c.top + row.top + at * lineHeight(), width, lineHeight());
	}
	return {};
}

QString DumpView::decodeText(int card, int line) const {
	if (line < 0 || line >= decodeLines(card)) return {};
	const RegDef &d = defs_[cards_[card].regs[line]];
	const DumpBlock &b = blocks_[card];
	const QByteArray raw = b.data.size() >= b.size ? b.data.mid(d.addr - b.addr, d.size) : QByteArray();
	return d.name + QStringLiteral("  ") + decodeValue(d, raw);
}

bool DumpView::byteAt(const QPoint &pos, int &card, int &offset) const {
	const QPoint o = origin();
	const int column = pos.x() - (o.x() + gridLeft());
	if (column < 0 || column >= 16 * cellWidth()) return false;
	for (int i = 0; i < cards_.size(); i++) {
		if (!cardRect(i).contains(pos)) continue;
		for (const Row &row : cards_[i].rows) {
			const int top = o.y() + cards_[i].top + row.top; /* the bytes are on a row's first line */
			if (pos.y() < top || pos.y() >= top + lineHeight()) continue;
			card = i;
			offset = row.base + column / cellWidth() - int(blocks_[i].addr);
			return true;
		}
		return false;
	}
	return false;
}

RegKey DumpView::registerAt(const QPoint &pos) const {
	for (int i = 0; i < cards_.size(); i++) {
		if (!cardRect(i).contains(pos)) continue;
		for (int line = 0; line < decodeLines(i); line++)
			if (decodeRect(i, line).contains(pos)) return regKey(defs_[cards_[i].regs[line]]);
		break;
	}
	int card = -1, offset = 0;
	if (!byteAt(pos, card, offset) || offset < 0 || offset >= blocks_[card].size) return NO_REGISTER;
	const int reg = regAt(card, offset);
	return reg >= 0 ? regKey(defs_[cards_[card].regs[reg]]) : NO_REGISTER;
}

int DumpView::cardOf(RegKey key) const {
	if (key == NO_REGISTER) return -1;
	for (int i = 0; i < cards_.size(); i++)
		for (int r : cards_[i].regs)
			if (regKey(defs_[r]) == key) return i;
	return -1;
}

int DumpView::currentCard() const {
	const int card = cardOf(selected_);
	if (card >= 0) return card;
	for (int i = 0; i < cards_.size(); i++)
		if (blocks_[i].kind != DumpBlock::FastStream && cardRect(i).bottom() >= 0) return i;
	return -1;
}

QColor DumpView::chipColour(int card, const RegKey &key) const {
	if (card < 0 || card >= cards_.size()) return {};
	const Card &c = cards_[card];
	for (int i = 0; i < c.regs.size(); i++)
		if (regKey(defs_[c.regs[i]]) == key) return c.colours[i].isValid() ? c.colours[i] : Theme::colors().surface2;
	return {};
}

QColor DumpView::chipFill(const QColor &colour, const QColor &card, bool dark) {
	return blend(colour, card, dark ? CHIP_ALPHA_DARK : CHIP_ALPHA_LIGHT);
}

/* ------------------------------------------------------------------- words */

QString DumpView::requestLabel(const DumpBlock &b) const {
	QString label = tr("→ READ %n bytes @ %1", nullptr, b.size).arg(addrText(b.addr));
	if (!b.device.isEmpty()) label = tr("%1, slave %2").arg(b.device).arg(b.wireSlave) + QStringLiteral("  ") + label;
	return label;
}

QString DumpView::replyLabel(const DumpBlock &b) const {
	return b.kind == DumpBlock::AutoSend ? tr("← AUTO_SEND") : tr("← REPLY");
}

/* The reply line's state, in its colour; the reasons a card is not live come first, so the line says why its frame
 * is amber. */
QString DumpView::replyState(int card, QColor &colour) const {
	const ThemeColors &c = Theme::colors();
	const DumpBlock &b = blocks_[card];
	const double age = now_ ? now_() - b.time : 0.0;
	colour = c.warn;
	if (hold_) return tr("held %1").arg(heldAt_);
	if (!connected_) return tr("the link is closed");
	if (b.data.size() < b.size && b.error.isEmpty()) {
		colour = c.muted;
		return tr("no reply yet");
	}
	if (!b.error.isEmpty()) {
		colour = c.bad;
		return tr("error: %1").arg(b.error);
	}
	if (b.kind == DumpBlock::Manual) return tr("read once, %1 ago").arg(ageText(age));
	if (age * 1000.0 > double(staleAfterMs_)) return tr("stale %1").arg(ageText(age));
	colour = c.muted;
	if (b.kind == DumpBlock::AutoSend) return QStringLiteral("%1 /s").arg(b.rate, 0, 'f', 0);
	return tr("%1 ago").arg(ageText(age));
}

QString DumpView::notLiveReason(int card) const {
	if (card < 0 || card >= blocks_.size()) return {};
	const DumpBlock &b = blocks_[card];
	if (b.kind == DumpBlock::FastStream) return {};
	if (hold_) return tr("Held at %1: these bytes are not refreshed (Follow shows the replies again)").arg(heldAt_);
	if (!connected_) return tr("The link is closed: these are the last bytes read");
	if (b.kind == DumpBlock::Manual) return tr("A manual read: read once, not refreshed");
	if (!b.error.isEmpty()) return tr("The last read failed (%1): these are the last bytes read").arg(b.error);
	if (b.data.size() < b.size) return tr("No reply yet");
	const double age = now_ ? now_() - b.time : 0.0;
	if (age * 1000.0 > double(staleAfterMs_)) return tr("Not refreshed for %1").arg(ageText(age));
	return {};
}

QString DumpView::requestText(int card) const {
	if (card < 0 || card >= cards_.size() || cards_[card].requestTop < 0) return {};
	const DumpBlock &b = blocks_[card];
	QString text = requestLabel(b) + QStringLiteral("  ") + evre::hex(requestFrame(b));
	if (b.single) text += QStringLiteral("  ") + tr("split: the device refused the merged read");
	return text;
}

QString DumpView::replyText(int card) const {
	if (card < 0 || card >= cards_.size() || cards_[card].replyTop < 0) return {};
	const DumpBlock &b = blocks_[card];
	QColor colour;
	QString text = replyLabel(b) + QLatin1Char(' ') + replyState(card, colour);
	if (b.data.size() >= b.size) text += QStringLiteral("  ") + evre::hex(replyFrame(b).left(REPLY_HEADER));
	return text;
}

/* "0xD006 · SUPPLY_V · byte 2 of 4 · 0x40 (64) · raw FA 7E 40 41 · 12.031 V"; a gap byte "0xD016 · not mapped · 0x83";
 * a byte of the row that no read covers "0xD088 · not read: no register here" */
QString DumpView::byteTip(int card, int offset) const {
	if (card < 0 || card >= blocks_.size()) return {};
	const DumpBlock &b = blocks_[card];
	const int addr = int(b.addr) + offset;
	const QString dot = QStringLiteral(" · ");
	QStringList parts { ltrPiece(addrText(uint16_t(addr))) };
	const RegDef *def = nullptr;
	for (const RegDef &d : defs_)
		if (d.slave == b.slave && d.readable && addr >= d.addr && addr < int(d.addr) + d.size) def = &d;
	if (offset < 0 || offset >= b.size) {
		parts << (def ? ltrPiece(def->name) + dot + tr("not in this read") : tr("not read: no register here"));
		return parts.join(dot);
	}
	const int reg = regAt(card, offset);
	if (reg < 0) {
		parts << tr("not mapped");
	} else {
		const RegDef &d = defs_[cards_[card].regs[reg]];
		parts << ltrPiece(d.name) << tr("byte %1 of %2").arg(addr - d.addr + 1).arg(d.size);
	}
	if (b.data.size() < b.size) {
		parts << tr("no reply yet");
		return parts.join(dot);
	}
	const uchar byte = uchar(b.data[offset]);
	parts << ltrPiece(QStringLiteral("0x%1 (%2)").arg(byteText(char(byte))).arg(byte));
	if (reg >= 0) {
		const RegDef &d = defs_[cards_[card].regs[reg]];
		const QByteArray raw = b.data.mid(d.addr - b.addr, d.size);
		parts << tr("raw %1").arg(ltrPiece(evre::hex(raw))) << ltrPiece(decodeValue(d, raw));
	}
	return parts.join(dot);
}

/* The clipboard's forms of a card: a register's bytes "FA 7E 40 41"; the block as hex rows "D000  10 27 00 00 ..."
 * (aligned as the grid, a byte the read does not cover left blank); the block as a C array with each register's
 * address and name before its bytes; the decoded lines "0xD004  SUPPLY_V  12.031 V". Empty: nothing answered yet. */
QString DumpView::copyText(CopyKind kind, int card, RegKey key) const {
	if (card < 0 || card >= blocks_.size()) return {};
	const DumpBlock &b = blocks_[card];
	const Card &c = cards_[card];
	if (b.kind == DumpBlock::FastStream || b.data.size() < b.size) return {};
	auto bytesOf = [&b](int from, int count, const QString &prefix, const QString &separator) {
		QStringList out;
		for (int i = from; i < from + count; i++) out << prefix + byteText(b.data[i]);
		return out.join(separator);
	};
	QStringList lines;
	switch (kind) {
	case CopyRegisterBytes:
		for (int r : c.regs)
			if (regKey(defs_[r]) == key) return bytesOf(defs_[r].addr - b.addr, defs_[r].size, QString(), QStringLiteral(" "));
		return {};
	case CopyBlockHex:
		for (const Row &row : c.rows) {
			QStringList cells;
			for (int column = 0; column < 16; column++) {
				const int offset = row.base + column - int(b.addr);
				cells << (offset >= 0 && offset < b.size ? byteText(b.data[offset]) : QStringLiteral("  "));
			}
			lines << (rowAddress(row.base) + QStringLiteral("  ") + cells.join(QLatin1Char(' '))).trimmed();
		}
		return lines.join(QLatin1Char('\n'));
	case CopyBlockC: {
		lines << QStringLiteral("static const uint8_t block_%1[%2] = {")
						 .arg(QStringLiteral("%1").arg(b.addr, 4, 16, QLatin1Char('0')))
						 .arg(b.size);
		/* one line per register, its address and name first; the bytes no register maps on lines of their own */
		int offset = 0;
		while (offset < b.size) {
			const int reg = regAt(card, offset);
			int count = 1;
			QString comment = QStringLiteral("/* %1 */ ").arg(addrText(uint16_t(b.addr + offset)));
			if (reg >= 0) {
				const RegDef &d = defs_[c.regs[reg]];
				count = int(d.addr) + d.size - (int(b.addr) + offset);
				comment = QStringLiteral("/* %1 %2 */ ").arg(addrText(d.addr), d.name);
			} else {
				while (offset + count < b.size && regAt(card, offset + count) < 0) count++;
			}
			lines << QStringLiteral("\t") + comment + bytesOf(offset, count, QStringLiteral("0x"), QStringLiteral(", "))
							+ QLatin1Char(',');
			offset += count;
		}
		lines << QStringLiteral("};");
		return lines.join(QLatin1Char('\n'));
	}
	case CopyDecoded:
		for (int r : c.regs) {
			const RegDef &d = defs_[r];
			lines << addrText(d.addr) + QStringLiteral("  ") + d.name + QStringLiteral("  ")
							+ decodeValue(d, b.data.mid(d.addr - b.addr, d.size));
		}
		return lines.join(QLatin1Char('\n'));
	}
	return {};
}

/* the Registers tab's Value and Unit, or its Decoded text (an enum's name, the fields) with " · " between the parts */
QString DumpView::decodeValue(const RegDef &def, const QByteArray &raw) const {
	if (raw.size() != def.size) return QStringLiteral("—");
	const QString decoded = formatDecoded(def, raw);
	if (!decoded.isEmpty()) return QString(decoded).replace(QLatin1String("  "), QStringLiteral(" · "));
	const QString value = formatValue(def, raw);
	return def.unit.isEmpty() ? value : value + QLatin1Char(' ') + def.unit;
}

/* ------------------------------------------------------------------ drawing */

void DumpView::resizeEvent(QResizeEvent *e) {
	QAbstractScrollArea::resizeEvent(e);
	relayout();
}

void DumpView::changeEvent(QEvent *e) {
	QAbstractScrollArea::changeEvent(e);
	if (Theme::switched(e, drawnDark_) || e->type() == QEvent::FontChange) relayout();
}

void DumpView::paintEvent(QPaintEvent *) {
	paints_++;
	QPainter p(viewport());
	/* a painter starts in the application's direction: in Arabic "8634 ms" read "ms 8634". Each text in its own direction
	 * instead (hex, names and values left to right, an Arabic label right to left: forced left to right its words came
	 * in the wrong order), every one at the left of its room (AlignAbsolute) */
	p.setLayoutDirection(Qt::LayoutDirectionAuto);
	const ThemeColors &c = Theme::colors();
	p.fillRect(viewport()->rect(), c.bg);
	p.setFont(font());
	const QRect view = viewport()->rect();
	if (cards_.isEmpty() && !emptyText_.isEmpty()) {
		p.setPen(c.muted);
		p.drawText(view.adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, emptyText_);
		return;
	}
	for (int i = 0; i < cards_.size(); i++) {
		const QRect rect = cardRect(i);
		if (rect.bottom() < view.top() || rect.top() > view.bottom()) continue;
		drawCard(p, i, rect.topLeft());
	}
}

/* the frame's bytes spaced as the Monitor writes them; those from mutedFirst to mutedLast (the start, the CRC and the
 * end byte) in the muted colour, the header in the text colour */
void DumpView::drawBytes(QPainter &p, int x, int y, const QByteArray &bytes, int mutedFirst, int mutedLast) const {
	const ThemeColors &c = Theme::colors();
	const int step = fontMetrics().horizontalAdvance(QStringLiteral("00 "));
	const int line = lineHeight();
	for (int i = 0; i < bytes.size(); i++) {
		const bool muted = i < mutedFirst || i > mutedLast;
		p.setPen(muted ? c.muted : c.text);
		p.drawText(QRect(x + i * step, y, step, line), LEFT, byteText(bytes[i]));
	}
}

void DumpView::drawCard(QPainter &p, int i, const QPoint &at) const {
	const ThemeColors &c = Theme::colors();
	const DumpBlock &b = blocks_[i];
	const Card &card = cards_[i];
	const QRect rect = cardRect(i);
	const int line = lineHeight();
	const int textLeft = at.x() + PAD;
	const int bytesLeft = textLeft + labelWidth_;
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(c.border, 1));
	p.setBrush(c.surface);
	p.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
	p.setRenderHint(QPainter::Antialiasing, false);

	if (b.kind == DumpBlock::FastStream) {
		p.setPen(c.muted);
		p.drawText(QRect(textLeft, at.y() + card.noteTop, rect.width() - 2 * PAD, line), LEFT,
				tr("fast stream %1: see the Chart").arg(b.stream));
		return;
	}
	if (card.titleTop >= 0) {
		QFont bold = font();
		bold.setBold(true);
		p.setFont(bold);
		p.setPen(c.text);
		p.drawText(QRect(textLeft, at.y() + card.titleTop, rect.width() - 2 * PAD, line), LEFT,
				tr("Manual read %1 · %n bytes", nullptr, b.size).arg(addrText(b.addr)));
		p.setFont(font());
	}
	if (card.requestTop >= 0) {
		const int y = at.y() + card.requestTop;
		p.setPen(c.text);
		p.drawText(QRect(textLeft, y, labelWidth_, line), LEFT, requestLabel(b));
		const QByteArray request = requestFrame(b);
		drawBytes(p, bytesLeft, y, request, 1, int(request.size()) - 1 - FRAME_TAIL);
		if (b.single) {
			const int x = bytesLeft + int(request.size()) * fontMetrics().horizontalAdvance(QStringLiteral("00 ")) + 8;
			p.setPen(c.muted);
			p.drawText(QRect(x, y, rect.right() - PAD - x, line), LEFT,
					fontMetrics().elidedText(tr("split: the device refused the merged read"), Qt::ElideRight,
							std::max(0, rect.right() - PAD - x)));
		}
	}
	/* the reply line: its label, its state in its colour, then the reply's header bytes */
	{
		const int y = at.y() + card.replyTop;
		p.setPen(c.text);
		const QString label = replyLabel(b);
		p.drawText(QRect(textLeft, y, labelWidth_, line), LEFT, label);
		QColor stateColour;
		const QString state = replyState(i, stateColour);
		const int stateLeft = textLeft + fontMetrics().horizontalAdvance(label) + fontMetrics().horizontalAdvance(QLatin1Char(' '));
		const bool hasBytes = b.data.size() >= b.size;
		p.setPen(stateColour);
		if (hasBytes && stateLeft + fontMetrics().horizontalAdvance(state) <= bytesLeft - 8) {
			p.drawText(QRect(stateLeft, y, bytesLeft - stateLeft, line), LEFT, state);
		} else {
			/* longer than the label's column (a long error): after the bytes, where there is room */
			const int x = hasBytes ? bytesLeft + REPLY_HEADER * fontMetrics().horizontalAdvance(QStringLiteral("00 ")) + 8
								   : stateLeft;
			p.drawText(QRect(x, y, rect.right() - PAD - x, line), LEFT,
					fontMetrics().elidedText(state, Qt::ElideRight, std::max(0, rect.right() - PAD - x)));
		}
		if (hasBytes) drawBytes(p, bytesLeft, y, replyFrame(b).left(REPLY_HEADER), 1, REPLY_HEADER);
	}
	drawGrid(p, i, at);
	drawGlow(p, i);
	drawDecode(p, i, at);
	/* the reply's CRC and end byte, closing it on a line of their own, at the column after the payload's last byte */
	if (card.crcTop >= 0) {
		const QByteArray reply = replyFrame(b);
		const QRect tail = tailRect(i);
		p.setPen(c.muted);
		for (int t = 0; t < FRAME_TAIL; t++)
			p.drawText(QRect(tail.left() + t * cellWidth(), tail.top(), cellWidth(), line), Qt::AlignCenter,
					byteText(reply[reply.size() - FRAME_TAIL + t]));
	}
	/* not live: a thin amber frame over the card's edge */
	if (!notLiveReason(i).isEmpty()) {
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(QPen(c.warn, 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(rect).adjusted(0.75, 0.75, -0.75, -0.75), 6, 6);
		p.setRenderHint(QPainter::Antialiasing, false);
	}
}

/* The rows: the address, then 16 cells. The chips first (a register's bytes in one row are one chip, 1 px inside
 * its cells, so two neighbours have 2 px between them), then each byte's text over them. */
void DumpView::drawGrid(QPainter &p, int i, const QPoint &at) const {
	const ThemeColors &c = Theme::colors();
	const bool dark = Theme::isDark();
	const DumpBlock &b = blocks_[i];
	const Card &card = cards_[i];
	const int line = lineHeight(), cell = cellWidth();
	const int left = at.x() - CARD_GAP + gridLeft();
	const bool answered = b.data.size() >= b.size;
	const int viewHeight = viewport()->height();
	for (const Row &row : card.rows) {
		const int y = at.y() + row.top;
		if (y + row.height < 0 || y > viewHeight) continue; /* a long manual read: only the rows in view */
		p.setPen(c.muted);
		p.drawText(QRect(at.x() + PAD, y, left - at.x() - PAD, line), LEFT, rowAddress(row.base));
		/* the chips of the registers with bytes in this row; the selected one's frame over them all */
		p.setRenderHint(QPainter::Antialiasing, true);
		QRectF selectedChip;
		QVector<QRectF> hoveredChips;
		for (int r = 0; r < card.regs.size(); r++) {
			const RegDef &d = defs_[card.regs[r]];
			const int from = std::max(int(d.addr), row.base), to = std::min(int(d.addr) + d.size, row.base + 16) - 1;
			if (from > to) continue;
			const QColor colour = card.colours[r];
			const QRectF chip(left + (from - row.base) * cell + 1.5, y + 2.5, (to - from + 1) * cell - 3.0, line - 5.0);
			p.setPen(QPen(colour.isValid() ? colour : c.border, 1));
			p.setBrush(colour.isValid() ? chipFill(colour, c.surface, dark) : c.surface2);
			p.drawRoundedRect(chip, 4, 4);
			if (regKey(d) == selected_) selectedChip = chip;
			if (regKey(d) == hovered_) hoveredChips << chip;
		}
		/* the register under the mouse: all its bytes ringed (its decode line lit too) */
		for (const QRectF &chip : hoveredChips) {
			p.setPen(QPen(c.accent, 1));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(chip.adjusted(-1, -1, 1, 1), 5, 5);
		}
		if (!selectedChip.isNull()) {
			p.setPen(QPen(c.accent, 2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(selectedChip.adjusted(-1, -1, 1, 1), 5, 5);
		}
		p.setRenderHint(QPainter::Antialiasing, false);
		for (int column = 0; column < 16; column++) {
			const int offset = row.base + column - int(b.addr);
			const QRect rect(left + column * cell, y, cell, line);
			if (offset < 0 || offset >= b.size || !answered) {
				p.setPen(c.muted);
				p.drawText(rect, Qt::AlignCenter, QStringLiteral("··"));
				continue;
			}
			p.setPen(regAt(i, offset) >= 0 ? c.text : c.muted); /* a gap byte: read, but no register's */
			p.drawText(rect, Qt::AlignCenter, byteText(b.data[offset]));
		}
	}
}

/* One line per register, in the row it starts in: its dot, its name, its value; each elided past its room (the whole
 * line is the tooltip). The selected register's line is bold. */
void DumpView::drawDecode(QPainter &p, int i, const QPoint &at) const {
	Q_UNUSED(at);
	const ThemeColors &c = Theme::colors();
	const QFontMetrics fm = fontMetrics();
	const int nameWidth = fm.horizontalAdvance(QLatin1Char('M')) * NAME_CHARS;
	const int valueWidth = fm.horizontalAdvance(QLatin1Char('M')) * VALUE_CHARS;
	const DumpBlock &b = blocks_[i];
	const Card &card = cards_[i];
	for (int r = 0; r < card.regs.size(); r++) {
		const QRect rect = decodeRect(i, r);
		if (rect.isEmpty()) continue;
		const RegDef &d = defs_[card.regs[r]];
		const QColor colour = card.colours[r].isValid() ? card.colours[r] : c.muted;
		if (regKey(d) == hovered_) p.fillRect(rect, blend(c.accent, c.surface, 0.14));
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(colour);
		p.drawEllipse(QPointF(rect.left() + 4.5, rect.center().y() + 0.5), 4, 4);
		p.setRenderHint(QPainter::Antialiasing, false);
		QFont f = font();
		f.setBold(regKey(d) == selected_);
		p.setFont(f);
		const QFontMetrics lineFm(f);
		const QByteArray raw = b.data.size() >= b.size ? b.data.mid(d.addr - b.addr, d.size) : QByteArray();
		p.setPen(c.text);
		p.drawText(QRect(rect.left() + 14, rect.top(), nameWidth, rect.height()), LEFT,
				lineFm.elidedText(d.name, Qt::ElideRight, nameWidth - 8));
		p.drawText(QRect(rect.left() + 14 + nameWidth, rect.top(), valueWidth, rect.height()), LEFT,
				lineFm.elidedText(decodeValue(d, raw), Qt::ElideRight, valueWidth));
		p.setFont(font());
	}
}

/* A byte that changed: a thin ring in the accent colour, fading over GLOW_MS as the Registers tab's value does. */
void DumpView::drawGlow(QPainter &p, int i) const {
	const QVector<qint64> times = changedMs_.value(readId(blocks_[i]));
	if (times.isEmpty()) return;
	const qint64 now = glowClock_.elapsed();
	const QColor accent = Theme::colors().accent;
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setBrush(Qt::NoBrush);
	for (int offset = 0; offset < times.size(); offset++) {
		if (now - times[offset] >= RegisterModel::GLOW_MS) continue;
		QColor ring = accent;
		ring.setAlphaF(float(glow(i, offset)));
		p.setPen(QPen(ring, 1.5));
		p.drawRoundedRect(QRectF(byteRect(i, offset)).adjusted(2.5, 3.5, -2.5, -3.5), 3, 3);
	}
	p.setRenderHint(QPainter::Antialiasing, false);
}

/* ------------------------------------------------------------------ the mouse */

void DumpView::mouseMoveEvent(QMouseEvent *e) {
	setHovered(registerAt(e->position().toPoint()));
	QAbstractScrollArea::mouseMoveEvent(e);
}

/* a click picks the register under it (here and in the Registers tab); the menu is the right-click's */
void DumpView::mousePressEvent(QMouseEvent *e) {
	const RegKey key = e->button() == Qt::LeftButton ? registerAt(e->position().toPoint()) : NO_REGISTER;
	if (key == NO_REGISTER) {
		QAbstractScrollArea::mousePressEvent(e);
		return;
	}
	select(key);
	emit registerPicked(key, false);
}

void DumpView::mouseDoubleClickEvent(QMouseEvent *e) {
	const RegKey key = e->button() == Qt::LeftButton ? registerAt(e->position().toPoint()) : NO_REGISTER;
	if (key == NO_REGISTER) {
		QAbstractScrollArea::mouseDoubleClickEvent(e);
		return;
	}
	select(key);
	emit registerPicked(key, true);
}

/* the register under the mouse picked first, so the menu and the Registers tab speak of the same one */
void DumpView::contextMenuEvent(QContextMenuEvent *e) {
	const RegKey key = registerAt(e->pos());
	int card = -1;
	for (int i = 0; i < cards_.size() && card < 0; i++)
		if (cardRect(i).contains(e->pos())) card = i;
	if (key != NO_REGISTER) {
		select(key);
		emit registerPicked(key, false);
	}
	emit menuRequested(key, card >= 0 ? card : currentCard(), e->globalPos());
}

/* ------------------------------------------------------------------ tooltips */

bool DumpView::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::Leave) setHovered(NO_REGISTER);
	if (e->type() != QEvent::ToolTip) return QAbstractScrollArea::viewportEvent(e);
	const auto *help = static_cast<QHelpEvent *>(e);
	const QString tip = toolTipAt(help->pos());
	if (tip.isEmpty()) {
		QToolTip::hideText();
		e->ignore();
	} else {
		QToolTip::showText(help->globalPos(), tip, viewport());
	}
	return true;
}

/* a decode line's whole text; a byte's words (and why its card is not live); a row's address; else the card's reason */
QString DumpView::toolTipAt(const QPoint &pos) const {
	QString tip;
	for (int i = 0; i < cards_.size() && tip.isEmpty(); i++) {
		if (!cardRect(i).contains(pos)) continue;
		for (int line = 0; line < decodeLines(i) && tip.isEmpty(); line++)
			if (decodeRect(i, line).contains(pos)) tip = decodeText(i, line);
		int card = -1, offset = 0;
		if (tip.isEmpty() && byteAt(pos, card, offset) && card == i) {
			/* a byte's own words; a card not live says why under them */
			tip = byteTip(i, offset);
			const QString reason = notLiveReason(i);
			if (!reason.isEmpty()) tip += QLatin1Char('\n') + reason;
		}
		for (const Row &row : cards_[i].rows) {
			const QRect address(cardRect(i).left() + PAD, cardRect(i).top() + row.top,
					gridLeft() - CARD_GAP - PAD, lineHeight());
			if (tip.isEmpty() && address.contains(pos)) tip = addrText(uint16_t(row.base));
		}
		if (tip.isEmpty()) tip = notLiveReason(i);
	}
	return tip;
}

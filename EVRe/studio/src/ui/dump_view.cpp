/* SPDX-License-Identifier: Apache-2.0 */
/* The Dump tab's picture: see dump_view.h. */
#include "ui/dump_view.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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

/* a text at the left of its room whatever its direction (the painter takes each text's own: paintEvent) */
constexpr Qt::Alignment LEFT = Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignVCenter;
constexpr int FRAME_HEAD = 7; /* 7B SLAVE FN OFF(2) CNT(2): the data, the CRC and the end byte follow */
/* a tooltip's own room around its text: the style sheet's padding (6) and border (1), the label's margin (2) */
constexpr int TIP_CHROME = 2 * (6 + 1 + 2);

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

/* where a field's bytes are in a frame: the header's fixed places, the data between it and the CRC */
void fieldSlice(int frameSize, DumpView::Field field, int &from, int &count) {
	switch (field) {
	case DumpView::Start: from = 0, count = 1; return;
	case DumpView::Slave: from = 1, count = 1; return;
	case DumpView::Function: from = 2, count = 1; return;
	case DumpView::Offset: from = 3, count = 2; return;
	case DumpView::Count: from = 5, count = 2; return;
	case DumpView::Data: from = FRAME_HEAD, count = std::max(0, frameSize - FRAME_HEAD - 3); return;
	case DumpView::Crc: from = frameSize - 3, count = 2; return;
	default: from = frameSize - 1, count = 1; return;
	}
}

/* The window's font, `smaller` points smaller, its family and style hint marked as set: a painter resolves a font
 * against the widget's, and the view's is the monospace one, whose Monospace hint alone made the labels Courier. */
QFont uiFont(double smaller = 0) {
	const QFont app = QApplication::font();
	QFont f(app.families(), -1, app.weight(), app.italic());
	if (app.pointSizeF() > 0) f.setPointSizeF(app.pointSizeF() - smaller);
	else f.setPixelSize(std::max(8, app.pixelSize() - int(smaller)));
	f.setStyleHint(app.styleHint(), app.styleStrategy());
	return f;
}

} // namespace

DumpView::DumpView(QWidget *parent) : QAbstractScrollArea(parent) {
	/* addresses, hex, names and values in their own order in any language (the grid's, not the window's) */
	setLayoutDirection(Qt::LeftToRight);
	viewport()->setLayoutDirection(Qt::LeftToRight); /* it is the one painted on */
	setFrameShape(QFrame::NoFrame);
	/* the arrows, Enter and the Menu key work the cards: the Tab key reaches the view after the toolbar */
	setFocusPolicy(Qt::StrongFocus);
	viewport()->setMouseTracking(true);
	viewport()->setAutoFillBackground(false);
	setFont(monospaceFont());
	drawnDark_ = Theme::isDark();
	glowClock_.start();
	glowTimer_ = new QTimer(this);
	glowTimer_->setInterval(GLOW_TICK_MS);
	connect(glowTimer_, &QTimer::timeout, this, [this] {
		if (!glowing()) glowTimer_->stop(); /* one more paint draws the last bytes in their plain colour */
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

/* A changed byte's text is drawn in the glow's colour, fading back to its plain colour: a ring round the cell would
 * read as a bordered byte, and a live grid would be a field of them. While a register is hovered nothing glows, so the
 * hover is the one mark on the grid; a lit register's bytes (hovered or selected) are in the lit text, on its stronger
 * fill. */
QColor DumpView::byteColour(int card, int offset) const {
	const ThemeColors &c = Theme::colors();
	const int reg = regAt(card, offset);
	if (reg >= 0) {
		const RegKey key = regKey(defs_[cards_[card].regs[reg]]);
		if (key == hovered_ || key == selected_) return litText(c, Theme::isDark());
	}
	const QColor plain = reg >= 0 ? c.text : c.muted; /* a gap byte: read, but no register's */
	const double g = hovered_ != NO_REGISTER ? 0.0 : glow(card, offset);
	return g > 0 ? blend(glowText(c, Theme::isDark()), plain, g) : plain;
}

/* The accent itself is 2.0 to 3.2:1 on the dark theme's chips and 3.5 to 4.0:1 on the light one's, and a byte that
 * changes at every poll glows all the time: so the accent made lighter (dark) or deeper (light), as far as keeps
 * 4.5:1 on every chip's tint, as the byte's plain text does. */
QColor DumpView::glowText(const ThemeColors &c, bool dark) {
	return dark ? blend(QColor(Qt::white), c.accent, 0.7) : blend(QColor(Qt::black), c.accent, 0.2);
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
	relayout();
}

void DumpView::setConnected(bool on) {
	if (on == connected_) return;
	connected_ = on;
	relayout();
}

void DumpView::setFields(bool on) {
	if (on == fields_) return;
	fields_ = on;
	relayout();
}

void DumpView::setClock(std::function<double()> now) { now_ = std::move(now); }
void DumpView::setStaleAfterMs(qint64 ms) { staleAfterMs_ = ms; }

void DumpView::select(RegKey key) {
	if (key == selected_) return;
	selected_ = key;
	/* a register in two cards (the manual read's and the polled one): the card it was picked in stays its card */
	if (selectedCard_ < 0 || selectedCard_ >= cards_.size() || cardOf(key) < 0
			|| std::none_of(cards_[selectedCard_].regs.begin(), cards_[selectedCard_].regs.end(),
					[&](int r) { return regKey(defs_[r]) == key; }))
		selectedCard_ = cardOf(key);
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

/* The register's row in view, with its card's top when both fit: the request and the reply say what it is. */
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

/* The keys' selection in view: its first chip and its decode line, scrolled by as little as it takes. */
void DumpView::ensureVisible(int card, RegKey key) {
	if (card < 0 || card >= cards_.size()) return;
	const Card &c = cards_[card];
	for (int r = 0; r < c.regs.size(); r++) {
		if (regKey(defs_[c.regs[r]]) != key) continue;
		const QRect area = byteRect(card, defs_[c.regs[r]].addr - blocks_[card].addr).united(decodeRect(card, r));
		const int height = viewport()->height();
		if (area.top() < 0) verticalScrollBar()->setValue(verticalScrollBar()->value() + area.top() - lineHeight());
		else if (area.bottom() > height)
			verticalScrollBar()->setValue(verticalScrollBar()->value() + area.bottom() - height + lineHeight());
		return;
	}
}

void DumpView::setHovered(RegKey key) {
	if (key == hovered_) return;
	hovered_ = key;
	/* a hand over what a click picks: a chip or a decode line */
	if (key == NO_REGISTER) viewport()->unsetCursor();
	else viewport()->setCursor(Qt::PointingHandCursor);
	viewport()->update();
}

/* ------------------------------------------------------------------- layout */

QFont DumpView::labelFont() const { return uiFont(1); }

int DumpView::lineHeight() const { return fontMetrics().height() + 6; }
int DumpView::labelLineHeight() const { return QFontMetrics(labelFont()).height() + 2; }
int DumpView::headLineHeight() const { return QFontMetrics(uiFont()).height() + 6; }
int DumpView::stripHeight() const { return fields_ ? lineHeight() + LABEL_GAP + labelLineHeight() : lineHeight(); }
/* a byte's cell: its two digits and room for the 3 px of card between two chips */
int DumpView::cellWidth() const { return fontMetrics().horizontalAdvance(QStringLiteral("00")) + 10; }
int DumpView::gridLeft() const { return CARD_GAP + PAD + fontMetrics().horizontalAdvance(QStringLiteral("D000")) + 12; }
int DumpView::decodeLeft() const { return origin().x() + gridLeft() + usedColumns_ * cellWidth() + DECODE_GAP; }
int DumpView::nameWidth() const { return fontMetrics().horizontalAdvance(QLatin1Char('M')) * nameChars_; }
int DumpView::valueWidth() const { return fontMetrics().horizontalAdvance(QLatin1Char('M')) * VALUE_CHARS; }

int DumpView::stripLeft(Field field) const {
	int x = PAD;
	for (int f = 0; f < field; f++) x += fieldW_[f] + FIELD_GAP;
	return x;
}

int DumpView::contentWidth() const {
	const int decodeRight = gridLeft() + usedColumns_ * cellWidth() + DECODE_GAP + 14 + nameWidth() + valueWidth() + 10;
	const int stripRight = CARD_GAP + stripLeft(End) + fieldW_[End];
	return std::max(decodeRight, stripRight) + PAD + CARD_GAP;
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

/* Each card from the top: a title (a manual read), the request's label and strip, the reply's label and strip (or
 * the one line of a reply that never came), then the grid of one-line rows with the decode list beside it. The list
 * flows: a line sits level with its register's row, or under the line before when that one is lower, so a sparse
 * block reads row by row and a dense one stacks its lines past the grid; the card is as tall as the taller. The
 * strips' columns and the grid's width are the whole tab's, so the cards line up under each other. */
void DumpView::relayout() {
	const QFontMetrics mono = fontMetrics(), labels(labelFont());
	const int line = lineHeight(), head = headLineHeight();
	cards_.clear();
	std::fill(std::begin(fieldW_), std::end(fieldW_), 0);
	headLabelW_ = std::max({ labels.horizontalAdvance(requestLabel()), labels.horizontalAdvance(tr("← REPLY")),
						 labels.horizontalAdvance(tr("← AUTO_SEND")) }) + 12;
	/* the tab's widths first: the grid's columns in use and the longest name */
	usedColumns_ = 0;
	int longestName = 4;
	for (const DumpBlock &b : blocks_) {
		if (b.kind == DumpBlock::FastStream) continue;
		const int first = b.addr & ~0xF, last = (int(b.addr) + std::max(1, int(b.size)) - 1);
		usedColumns_ = std::max(usedColumns_, (last & ~0xF) > first ? 16 : (last & 0xF) + 1);
		for (const RegDef &d : defs_)
			if (holds(b, d)) longestName = std::max(longestName, int(d.name.size()));
	}
	if (usedColumns_ == 0) usedColumns_ = 16;
	nameChars_ = std::min(int(NAME_CHARS), longestName) + 2;

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
			for (int r = 0; r < defs_.size(); r++)
				if (holds(b, defs_[r])) card.regs << r;
			std::sort(card.regs.begin(), card.regs.end(), [this](int x, int z) { return defs_[x].addr < defs_[z].addr; });
			assignColours(card);
			if (b.kind == DumpBlock::Manual || b.kind == DumpBlock::Write) {
				card.titleTop = at;
				at += head;
			}
			if (b.kind != DumpBlock::AutoSend) {
				card.requestTop = at;
				at += head;
				card.requestStripTop = at;
				at += stripHeight() + STRIP_GAP;
			}
			card.replyTop = at;
			at += head;
			if (!frame(i, Reply).isEmpty()) {
				card.replyStripTop = at;
				at += stripHeight();
			} else if (!b.error.isEmpty()) {
				card.noReplyTop = at;
				at += line;
			}
			at += GRID_GAP;
			/* the rows from the block's first to its last, aligned to 16, one line each */
			card.gridTop = at;
			const int first = b.addr & ~0xF, last = (int(b.addr) + std::max(1, int(b.size)) - 1) & ~0xF;
			for (int base = first; base <= last; base += 16) {
				card.rows << Row { base, at };
				at += line;
			}
			int listBottom = card.gridTop;
			for (const int r : card.regs) {
				const int top = std::max(card.gridTop + ((defs_[r].addr & ~0xF) - first) / 16 * line, listBottom);
				card.lineTop << top;
				listBottom = top + line;
			}
			at = std::max(at, listBottom);
		}
		card.height = at + PAD;
		y += card.height + CARD_GAP;
		cards_ << card;
	}
	/* the strips' columns: each as wide as the widest bytes or label it holds in any card */
	for (int i = 0; i < blocks_.size(); i++)
		for (const Side side : { Request, Reply }) {
			if (!hasStrip(i, side)) continue;
			for (int f = 0; f < N_FIELDS; f++) {
				const QString pill = f == Data ? payloadText(i, side) : QString();
				fieldW_[f] = std::max({ fieldW_[f],
						pill.isEmpty() ? mono.horizontalAdvance(fieldBytesText(i, side, Field(f))) : payloadWidth(pill),
						labels.horizontalAdvance(fieldLabel(i, side, Field(f))) });
			}
		}
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

bool DumpView::holds(const DumpBlock &b, const RegDef &d) {
	return d.slave == b.slave && (d.readable || b.kind == DumpBlock::Write) && d.size > 0 && d.addr >= b.addr
			&& int(d.addr) + d.size <= int(b.addr) + b.size;
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

/* ------------------------------------------------------------------- frames */

/* The bytes of a READ and of its answer (or of a WRITE_ACK and its WRITE_ACK_RESP), as on the wire: one definition
 * with the master's (evre::build). An error answer is rebuilt as the device sent it (its code the one data byte, the
 * count echoed); a read with no answer, and a WRITE (never answered), have no reply frame. */
QByteArray DumpView::frame(int card, Side side) const {
	if (card < 0 || card >= blocks_.size()) return {};
	const DumpBlock &b = blocks_[card];
	if (b.kind == DumpBlock::FastStream) return {};
	const bool write = b.kind == DumpBlock::Write;
	if (side == Request) {
		if (write) return evre::build(b.wireSlave, b.ack ? evre::WRITE_ACK : evre::WRITE, b.addr, b.size, b.data);
		return b.kind == DumpBlock::AutoSend ? QByteArray() : evre::build(b.wireSlave, evre::READ, b.addr, b.size);
	}
	if (b.errorCode != 0)
		return evre::build(b.wireSlave, evre::ERROR_RESP, b.addr, b.size, QByteArray(1, char(b.errorCode)));
	if (!b.error.isEmpty() || b.data.size() < b.size) return {};
	if (write) return b.ack ? evre::build(b.wireSlave, evre::WRITE_ACK_RESP, b.addr, b.size) : QByteArray();
	return evre::build(b.wireSlave, evre::READ_RESP, b.addr, b.size, b.data.left(b.size));
}

bool DumpView::hasStrip(int card, Side side) const { return !frame(card, side).isEmpty(); }

QByteArray DumpView::fieldBytes(int card, Side side, Field field) const {
	const QByteArray bytes = frame(card, side);
	if (bytes.isEmpty()) return {};
	int from = 0, count = 0;
	fieldSlice(int(bytes.size()), field, from, count);
	return bytes.mid(from, count);
}

/* the payload is in the grid: its column says so with the pill's words (an error answer's one code byte is written) */
QString DumpView::fieldBytesText(int card, Side side, Field field) const {
	if (field == Data) {
		const QString pill = payloadText(card, side);
		if (!pill.isEmpty()) return pill;
	}
	return evre::hex(fieldBytes(card, side, field));
}

/* The payload in a strip: a grey pill saying how many bytes and that they are the grid below ("22 bytes ↓"), as a
 * protocol analyser writes a data field it does not spell out ("Data (22 bytes)") and a packet diagram boxes it; an
 * ellipsis said only that something was left out. */
QString DumpView::payloadText(int card, Side side) const {
	const QByteArray bytes = frame(card, side);
	const int data = int(bytes.size()) - FRAME_HEAD - 3;
	if (data <= 0 || uchar(bytes.at(2)) == evre::ERROR_RESP) return {};
	return tr("%n bytes ↓", nullptr, data);
}

int DumpView::payloadWidth(const QString &text) const {
	return QFontMetrics(labelFont()).horizontalAdvance(text) + 2 * PILL_PAD;
}

QString DumpView::fieldName(Field field) {
	/* the protocol's own words, as READ_RESP and AUTO_SEND are: not translated */
	static const char *const names[N_FIELDS] = { "START", "SLAVE", "FUNCTION", "OFFSET", "COUNT", "DATA", "CRC", "END" };
	return field >= 0 && field < N_FIELDS ? QLatin1String(names[field]) : QString();
}

QString DumpView::fieldMeaning(int card, Side side, Field field) const {
	const QByteArray bytes = frame(card, side);
	if (bytes.size() < FRAME_HEAD + 3) return {};
	const uint8_t fn = uint8_t(bytes.at(2));
	const uint16_t count = evre::littleEndian16(bytes, 5);
	switch (field) {
	case Slave:
		return uchar(bytes.at(1)) == evre::BROADCAST ? QStringLiteral("0 ") + tr("all") : QString::number(uchar(bytes.at(1)));
	case Function: return evre::fnName(fn);
	case Offset: return addrText(evre::littleEndian16(bytes, 3));
	case Count: return QString::number(count);
	case Data: {
		if (fn == evre::ERROR_RESP) {
			const uint8_t code = uint8_t(bytes.at(FRAME_HEAD));
			return tr("ERROR %1 %2").arg(code).arg(evre::errorName(code));
		}
		const int data = int(bytes.size()) - FRAME_HEAD - 3;
		return data == 0 ? tr("none") : tr("payload");
	}
	/* in capitals, as the field names beside it are */
	case Crc: return side == Reply ? tr("OK") : QString();
	default: return {};
	}
}

QString DumpView::fieldLabel(int card, Side side, Field field) const {
	const QString meaning = fieldMeaning(card, side, field);
	return meaning.isEmpty() ? fieldName(field) : fieldName(field) + QLatin1Char(' ') + meaning;
}

/* PROTOCOL.md's words for each field, with this frame's numbers */
QString DumpView::fieldTip(int card, Side side, Field field) const {
	const QByteArray bytes = frame(card, side);
	if (bytes.size() < FRAME_HEAD + 3) return {};
	const uint8_t fn = uint8_t(bytes.at(2));
	const int data = int(bytes.size()) - FRAME_HEAD - 3;
	const QString crc = QStringLiteral("0x")
			+ QStringLiteral("%1").arg(evre::littleEndian16(bytes, int(bytes.size()) - 3), 4, 16, QLatin1Char('0')).toUpper();
	switch (field) {
	case Start: return tr("Start byte 0x7B (\"{\"): a frame is recognisable in a terminal dump");
	case Slave: return tr("Slave ID %1; 0 is the broadcast, every device takes a WRITE to it").arg(uchar(bytes.at(1)));
	case Function: {
		QString words = evre::fnName(fn);
		if (fn == evre::READ) words = tr("READ, answered with READ_RESP");
		else if (fn == evre::READ_RESP) words = tr("READ_RESP, the answer to a READ: the bytes asked for");
		else if (fn == evre::ERROR_RESP) words = tr("ERROR_RESP, the answer to a request that failed: one error code");
		else if (fn == evre::WRITE_ACK) words = tr("WRITE_ACK, a write answered with WRITE_ACK_RESP");
		else if (fn == evre::WRITE) words = tr("WRITE, a write that is never answered");
		else if (fn == evre::WRITE_ACK_RESP) words = tr("WRITE_ACK_RESP, the answer to a WRITE_ACK: the write landed");
		return tr("Function code 0x%1: %2").arg(byteText(char(fn)), words);
	}
	case Offset:
		return tr("Register offset, little endian: %1 is %2")
				.arg(QStringLiteral("0x%1 0x%2").arg(byteText(bytes.at(3)), byteText(bytes.at(4))),
						addrText(evre::littleEndian16(bytes, 3)));
	case Count:
		return tr("Number of registers, little endian: %1 (a register is one byte: the count is in bytes)")
				.arg(evre::littleEndian16(bytes, 5));
	case Data:
		if (fn == evre::ERROR_RESP) {
			const uint8_t code = uint8_t(bytes.at(FRAME_HEAD));
			return tr("Data: the error code %1, %2").arg(code).arg(evre::errorName(code));
		}
		if (data == 0)
			return fn == evre::WRITE_ACK_RESP ? tr("Data: none, the answer carries no status")
											  : tr("Data: none, a READ carries no data");
		if (fn == evre::WRITE || fn == evre::WRITE_ACK) return tr("Data: the %n bytes written, in the grid below", nullptr, data);
		return tr("Data: the %n bytes of the grid below (the payload)", nullptr, data);
	case Crc:
		return side == Request
				? tr("CRC-16/X-25 over every byte before it, low byte first: %1; the Studio computed it").arg(crc)
				: tr("CRC-16/X-25 over every byte before it, low byte first: %1; checked before the reply "
						"was taken (a frame whose CRC fails is dropped and counted in the status bar's Bad "
						"frames)").arg(crc);
	default: return tr("End byte 0x7D (\"}\")");
	}
}

/* ------------------------------------------------------------- where things are */

QRect DumpView::cardRect(int card) const {
	if (card < 0 || card >= cards_.size()) return {};
	const QPoint o = origin();
	const int width = std::max(contentWidth(), viewport()->width()) - 2 * CARD_GAP;
	return QRect(o.x() + CARD_GAP, o.y() + cards_[card].top, width, cards_[card].height);
}

QRect DumpView::stripeRect(int card) const {
	if (notLiveReason(card).isEmpty()) return {};
	const QRect rect = cardRect(card);
	return QRect(rect.left(), rect.top(), STRIPE_W, rect.height());
}

QRect DumpView::stripRect(int card, Side side) const {
	if (!hasStrip(card, side)) return {};
	const Card &c = cards_[card];
	const int top = side == Request ? c.requestStripTop : c.replyStripTop;
	const QRect first = fieldRect(card, side, Start), last = fieldRect(card, side, End);
	return QRect(first.left(), cardRect(card).top() + top, last.right() - first.left() + 1, stripHeight());
}

/* Fields on: the column, both lines. Off: the field's bytes in the frame's one line (the payload one "…"). */
QRect DumpView::fieldRect(int card, Side side, Field field) const {
	if (!hasStrip(card, side)) return {};
	const Card &c = cards_[card];
	const QRect rect = cardRect(card);
	const int top = rect.top() + (side == Request ? c.requestStripTop : c.replyStripTop);
	if (fields_) return QRect(rect.left() + stripLeft(field), top, fieldW_[field], stripHeight());
	const int step = fontMetrics().horizontalAdvance(QStringLiteral("00 "));
	const int space = step - fontMetrics().horizontalAdvance(QStringLiteral("00"));
	const QString pill = payloadText(card, side);
	int x = rect.left() + PAD;
	for (int f = 0; f < field; f++) {
		if (f == Data && !pill.isEmpty()) {
			x += payloadWidth(pill) + space;
			continue;
		}
		const QString text = fieldBytesText(card, side, Field(f));
		if (!text.isEmpty()) x += (int(text.size()) + 1) / 3 * step;
	}
	if (field == Data && !pill.isEmpty()) return QRect(x, top, payloadWidth(pill), lineHeight());
	const int units = (int(fieldBytesText(card, side, field).size()) + 1) / 3;
	return QRect(x, top, units * step, lineHeight());
}

QRect DumpView::headRect(int card, Side side) const {
	if (card < 0 || card >= cards_.size()) return {};
	const int top = side == Request ? cards_[card].requestTop : cards_[card].replyTop;
	if (top < 0) return {};
	const QRect rect = cardRect(card);
	return QRect(rect.left() + PAD, rect.top() + top, headLabelW_, headLineHeight());
}

QRectF DumpView::arrowRect(int card, Side side) const {
	const QRect head = headRect(card, side);
	if (head.isEmpty()) return {};
	const QString label = side == Request ? requestLabel() : replyLabel(blocks_[card]);
	return headLabel(label, head.left(), head.top()).arrowInk;
}

QRect DumpView::payloadRect(int card, Side side) const {
	if (payloadText(card, side).isEmpty()) return {};
	const QRect field = fieldRect(card, side, Data);
	return QRect(field.left(), field.top(), payloadWidth(payloadText(card, side)), lineHeight());
}

/* the cells of a row no read covers: before a block that starts inside it, or all of a block with no reply yet */
QVector<DumpView::Run> DumpView::unreadRuns(int card, int row) const {
	QVector<Run> runs;
	if (card < 0 || card >= cards_.size() || row < 0 || row >= cards_[card].rows.size()) return runs;
	const DumpBlock &b = blocks_[card];
	const bool answered = b.data.size() >= b.size;
	const int base = cards_[card].rows[row].base;
	for (int column = 0; column < 16; column++) {
		const int offset = base + column - int(b.addr);
		if (offset >= b.size) break;
		if (offset >= 0 && answered) continue;
		const bool waiting = offset >= 0; /* in the block, no reply yet; else before it */
		if (!runs.isEmpty() && runs.last().to == column - 1 && runs.last().waiting == waiting) runs.last().to = column;
		else runs << Run { column, column, waiting };
	}
	return runs;
}

bool DumpView::unreadRun(int card, int offset, int &row, Run &run) const {
	if (card < 0 || card >= cards_.size() || cards_[card].rows.isEmpty()) return false;
	const int addr = int(blocks_[card].addr) + offset;
	row = (addr - cards_[card].rows.first().base) / 16;
	if (addr < cards_[card].rows.first().base || row >= cards_[card].rows.size()) return false;
	for (const Run &r : unreadRuns(card, row))
		if ((addr & 0xF) >= r.from && (addr & 0xF) <= r.to) {
			run = r;
			return true;
		}
	return false;
}

QRect DumpView::unreadRect(int card, int offset) const {
	int row = 0;
	Run run;
	if (!unreadRun(card, offset, row, run)) return {};
	const QRect first = byteRect(card, offset - ((int(blocks_[card].addr) + offset) & 0xF) + run.from);
	return QRect(first.left() + 1, first.top() + 2, (run.to - run.from + 1) * cellWidth() - 2, lineHeight() - 4);
}

/* the longest of the run's words that fits: "4 bytes not read", "not read" (a block with no reply yet: "no reply yet");
 * none in a run too short, whose cells' tooltips say it */
QString DumpView::unreadWords(int card, int offset, int width) const {
	int row = 0;
	Run run;
	if (!unreadRun(card, offset, row, run)) return {};
	const QStringList words = run.waiting ? QStringList { tr("no reply yet") }
									  : QStringList { tr("%n bytes not read", nullptr, run.to - run.from + 1), tr("not read") };
	const QFontMetrics fm(labelFont());
	for (const QString &w : words)
		if (fm.horizontalAdvance(w) <= width - 8) return w;
	return {};
}

QString DumpView::unreadText(int card, int offset) const { return unreadWords(card, offset, unreadRect(card, offset).width()); }

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

int DumpView::decodeLines(int card) const { return card < 0 || card >= cards_.size() ? 0 : int(cards_[card].regs.size()); }

QRect DumpView::decodeRect(int card, int line) const {
	if (line < 0 || line >= decodeLines(card) || line >= cards_[card].lineTop.size()) return {};
	const Card &c = cards_[card];
	return QRect(decodeLeft(), origin().y() + c.top + c.lineTop[line], 14 + nameWidth() + valueWidth() + 10, lineHeight());
}

QString DumpView::decodeText(int card, int line) const {
	if (line < 0 || line >= decodeLines(card)) return {};
	const RegDef &d = defs_[cards_[card].regs[line]];
	const DumpBlock &b = blocks_[card];
	const QByteArray raw = b.data.size() >= b.size ? b.data.mid(d.addr - b.addr, d.size) : QByteArray();
	return d.name + QStringLiteral("  ") + decodeValue(d, raw);
}

/* a cell the row draws: from the row's first cell to the block's last byte (the cells before the block, framed as not read) */
bool DumpView::byteAt(const QPoint &pos, int &card, int &offset) const {
	const QPoint o = origin();
	const int column = pos.x() - (o.x() + gridLeft());
	if (column < 0 || column >= 16 * cellWidth()) return false;
	for (int i = 0; i < cards_.size(); i++) {
		if (!cardRect(i).contains(pos)) continue;
		for (const Row &row : cards_[i].rows) {
			const int top = o.y() + cards_[i].top + row.top;
			if (pos.y() < top || pos.y() >= top + lineHeight()) continue;
			const int at = row.base + column / cellWidth() - int(blocks_[i].addr);
			if (at >= blocks_[i].size) return false; /* after the block: the card's surface */
			card = i;
			offset = at;
			return true;
		}
		return false;
	}
	return false;
}

bool DumpView::fieldAt(const QPoint &pos, int &card, Side &side, Field &field) const {
	for (int i = 0; i < cards_.size(); i++) {
		if (!cardRect(i).contains(pos)) continue;
		for (const Side s : { Request, Reply })
			for (int f = 0; f < N_FIELDS; f++)
				if (fieldRect(i, s, Field(f)).contains(pos)) {
					card = i;
					side = s;
					field = Field(f);
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
	const int card = selectedCard_ >= 0 && selectedCard_ < cards_.size() && cardOf(selected_) >= 0 ? selectedCard_
																									 : cardOf(selected_);
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

double DumpView::chipAlpha(bool dark, bool hovered) {
	if (hovered) return dark ? CHIP_ALPHA_HOVER_DARK : CHIP_ALPHA_HOVER_LIGHT;
	return dark ? CHIP_ALPHA_DARK : CHIP_ALPHA_LIGHT;
}

QColor DumpView::chipFill(const QColor &colour, const QColor &card, double alpha) { return blend(colour, card, alpha); }

QColor DumpView::litText(const ThemeColors &c, bool dark) { return dark ? QColor(Qt::white) : c.text; }

/* a grey a step off the card in both themes (the text on it 11:1 dark, 14:1 light) */
QColor DumpView::payloadFill(const ThemeColors &c) { return blend(c.muted, c.surface, 0.18); }

/* the controls' edge colour: 3:1 or more to the card, as a shape needs (the label in it is muted, as text needs) */
QColor DumpView::unreadEdge(const ThemeColors &c) { return c.control; }

/* ------------------------------------------------------------------- words */

QString DumpView::requestLabel() const { return tr("→ REQUEST"); }

QString DumpView::requestSentence(const DumpBlock &b) const {
	QString sentence = b.kind != DumpBlock::Write ? tr("READ %n bytes @ %1", nullptr, b.size).arg(addrText(b.addr))
			: b.ack ? tr("WRITE_ACK %n bytes @ %1", nullptr, b.size).arg(addrText(b.addr))
					: tr("WRITE %n bytes @ %1", nullptr, b.size).arg(addrText(b.addr));
	if (!b.device.isEmpty())
		sentence = tr("%1, slave %2").arg(b.device).arg(b.wireSlave) + QStringLiteral(" · ") + sentence;
	return sentence;
}

QString DumpView::replyLabel(const DumpBlock &b) const {
	return b.kind == DumpBlock::AutoSend ? tr("← AUTO_SEND") : tr("← REPLY");
}

/* The reply label's state, in its colour; the reasons a card is not live come first, so the label says why its
 * stripe is amber. */
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
	if (b.kind == DumpBlock::Write) {
		if (b.ack) return tr("acknowledged, %1 ago").arg(ageText(age));
		colour = c.muted;
		return tr("none: WRITE is not answered");
	}
	if (age * 1000.0 > double(staleAfterMs_)) return tr("stale %1").arg(ageText(age));
	colour = c.muted;
	if (b.kind == DumpBlock::AutoSend) return QStringLiteral("%1 /s").arg(b.rate, 0, 'f', 0);
	return tr("%1 ago").arg(ageText(age));
}

/* a manual read's "Manual read 0xD080 · 8 bytes"; a write's "Write SETPOINT @ 0xD080 · 4 bytes" (the register it
 * starts at, when one does) */
QString DumpView::titleText(int card) const {
	if (card < 0 || card >= cards_.size() || cards_[card].titleTop < 0) return {};
	const DumpBlock &b = blocks_[card];
	if (b.kind != DumpBlock::Write) return tr("Manual read %1 · %n bytes", nullptr, b.size).arg(addrText(b.addr));
	for (const int r : cards_[card].regs)
		if (defs_[r].addr == b.addr)
			return tr("Write %1 @ %2 · %n bytes", nullptr, b.size).arg(ltrPiece(defs_[r].name), addrText(b.addr));
	return tr("Write %1 · %n bytes", nullptr, b.size).arg(addrText(b.addr));
}

/* a read whose last answer never came: no frame to draw, one line instead */
QString DumpView::noReplyText(int card) const {
	const DumpBlock &b = blocks_[card];
	return b.timedOut ? tr("no reply: timed out") : tr("no reply: %1").arg(b.error);
}

QString DumpView::notLiveReason(int card) const {
	if (card < 0 || card >= blocks_.size()) return {};
	const DumpBlock &b = blocks_[card];
	if (b.kind == DumpBlock::FastStream) return {};
	if (hold_) return tr("Held at %1: these bytes are not refreshed (Follow shows the replies again)").arg(heldAt_);
	if (!connected_) return tr("The link is closed: these are the last bytes read");
	if (b.kind == DumpBlock::Manual) return tr("A manual read: read once, not refreshed");
	if (b.kind == DumpBlock::Write) return tr("A write: sent once, not refreshed");
	if (!b.error.isEmpty()) return tr("The last read failed (%1): these are the last bytes read").arg(b.error);
	if (b.data.size() < b.size) return tr("No reply yet");
	const double age = now_ ? now_() - b.time : 0.0;
	if (age * 1000.0 > double(staleAfterMs_)) return tr("Not refreshed for %1").arg(ageText(age));
	return {};
}

QString DumpView::requestText(int card) const {
	if (card < 0 || card >= cards_.size() || cards_[card].requestTop < 0) return {};
	const DumpBlock &b = blocks_[card];
	QString text = requestLabel() + QStringLiteral("  ") + requestSentence(b) + QStringLiteral("  ")
			+ evre::hex(frame(card, Request));
	if (b.single) text += QStringLiteral("  ") + tr("split: the device refused the merged read");
	return text;
}

QString DumpView::replyText(int card) const {
	if (card < 0 || card >= cards_.size() || cards_[card].replyTop < 0) return {};
	const DumpBlock &b = blocks_[card];
	QColor colour;
	QString text = replyLabel(b) + QLatin1Char(' ') + replyState(card, colour);
	if (hasStrip(card, Reply)) {
		QStringList bytes;
		for (int f = 0; f < N_FIELDS; f++) {
			const QString field = fieldBytesText(card, Reply, Field(f));
			if (!field.isEmpty()) bytes << field;
		}
		text += QStringLiteral("  ") + bytes.join(QLatin1Char(' '));
	} else if (cards_[card].noReplyTop >= 0) {
		text += QStringLiteral("  ") + noReplyText(card);
	}
	return text;
}

/* Two lines: "SUPPLY_V · byte 3 of 4 · 0xD006" and "0x40 (64) · raw FA 7E 40 41 · 12.031 V"; a gap byte "not mapped ·
 * 0xD016" and "0x83 (131)"; a byte before the block "0xD000" and "not read: no register here" */
QString DumpView::byteTip(int card, int offset) const {
	if (card < 0 || card >= blocks_.size()) return {};
	const DumpBlock &b = blocks_[card];
	const int addr = int(b.addr) + offset;
	const QString dot = QStringLiteral(" · ");
	const QString address = ltrPiece(addrText(uint16_t(addr)));
	const RegDef *def = nullptr;
	for (const RegDef &d : defs_)
		if (d.slave == b.slave && (d.readable || b.kind == DumpBlock::Write) && addr >= d.addr
				&& addr < int(d.addr) + d.size)
			def = &d;
	if (offset < 0 || offset >= b.size)
		return address + QLatin1Char('\n') + (def ? ltrPiece(def->name) + dot + tr("not in this read")
												  : tr("not read: no register here"));
	const int reg = regAt(card, offset);
	QString first;
	if (reg < 0) {
		first = tr("not mapped") + dot + address;
	} else {
		const RegDef &d = defs_[cards_[card].regs[reg]];
		first = ltrPiece(d.name) + dot + tr("byte %1 of %2").arg(addr - d.addr + 1).arg(d.size) + dot + address;
	}
	if (b.data.size() < b.size) return first + QLatin1Char('\n') + tr("no reply yet");
	const uchar byte = uchar(b.data[offset]);
	QStringList second { ltrPiece(QStringLiteral("0x%1 (%2)").arg(byteText(char(byte))).arg(byte)) };
	if (reg >= 0) {
		const RegDef &d = defs_[cards_[card].regs[reg]];
		const QByteArray raw = b.data.mid(d.addr - b.addr, d.size);
		second << tr("raw %1").arg(ltrPiece(evre::hex(raw))) << ltrPiece(decodeValue(d, raw));
	}
	return first + QLatin1Char('\n') + second.join(dot);
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

/* The label's arrow and its word apart, where the label drawn whole would put them (the arrow first in the word's
 * direction: at the left of an English word, at the right of an Arabic one), the arrow's baseline moved so its ink is
 * centred on the word's capitals: a font draws an arrow on its maths axis, lower, and "→ REQUEST" read as an arrow
 * at the bottom of the line. */
DumpView::HeadLabel DumpView::headLabel(const QString &label, int x, int y) const {
	HeadLabel h;
	const QFontMetricsF fm(labelFont());
	const double baseline = qRound(y + (headLineHeight() - fm.height()) / 2.0 + fm.ascent());
	const bool arrow = !label.isEmpty() && (label.at(0) == QChar(0x2192) || label.at(0) == QChar(0x2190));
	h.arrow = arrow ? label.left(1) : QString();
	h.word = arrow ? label.mid(1) : label;
	const bool rtl = h.word.isRightToLeft();
	h.wordAt = QPointF(rtl || !arrow ? x : x + fm.horizontalAdvance(h.arrow), baseline);
	if (arrow) {
		const QRectF ink = fm.tightBoundingRect(h.arrow);
		h.arrowAt = QPointF(rtl ? x + fm.horizontalAdvance(h.word) : x,
				qRound(baseline - fm.capHeight() / 2.0 - ink.center().y()));
		h.arrowInk = ink.translated(h.arrowAt);
	}
	return h;
}

/* A label line: the small muted label ("→ REQUEST"), then its sentence in the window's font, cut to its room. */
void DumpView::drawHead(QPainter &p, int x, int y, int width, const QString &label, const QString &sentence,
		const QColor &sentenceColour) const {
	const ThemeColors &c = Theme::colors();
	const int head = headLineHeight();
	const QFont labels = labelFont();
	const QFontMetricsF fm(labels);
	const HeadLabel h = headLabel(label, x, y);
	p.setFont(labels);
	p.setPen(c.muted);
	/* each at its baseline: a rect's top at the baseline less the ascent, aligned to its top */
	const auto at = [&](const QPointF &baseline, const QString &text) {
		p.drawText(QRectF(baseline.x(), baseline.y() - fm.ascent(), fm.horizontalAdvance(text) + 2, fm.height()),
				Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignTop, text);
	};
	at(h.wordAt, h.word);
	if (!h.arrow.isEmpty()) at(h.arrowAt, h.arrow);
	const QFont font = uiFont();
	p.setFont(font);
	p.setPen(sentenceColour);
	const int room = std::max(0, width - headLabelW_);
	p.drawText(QRect(x + headLabelW_, y, room, head), LEFT, QFontMetrics(font).elidedText(sentence, Qt::ElideRight, room));
	p.setFont(this->font());
}

/* A frame. Fields on: a column per field, its bytes (START, CRC and END muted; the payload a grey pill, its bytes are
 * the grid) over its name (muted) and meaning ("OK" in the good colour, an error in the bad). Off: the bytes on one
 * line, the pill among them. */
void DumpView::drawStrip(QPainter &p, int i, Side side, int x, int y) const {
	const ThemeColors &c = Theme::colors();
	const QByteArray bytes = frame(i, side);
	const bool error = uchar(bytes.at(2)) == evre::ERROR_RESP;
	const int line = lineHeight();
	const QFont labels = labelFont();
	const QFontMetrics labelFm(labels);
	Q_UNUSED(x);
	for (int f = 0; f < N_FIELDS; f++) {
		const Field field = Field(f);
		const QString text = fieldBytesText(i, side, field);
		const QRect rect = fieldRect(i, side, field); /* drawn where the tests and the tooltips find it */
		const QRect pill = field == Data ? payloadRect(i, side) : QRect();
		if (!pill.isEmpty()) {
			/* the payload: a grey pill the size of a chip, its words in the labels' font */
			p.setRenderHint(QPainter::Antialiasing, true);
			p.setPen(Qt::NoPen);
			p.setBrush(payloadFill(c));
			p.drawRoundedRect(QRectF(pill).adjusted(0, 2.5, 0, -2.5), 4, 4);
			p.setRenderHint(QPainter::Antialiasing, false);
			p.setFont(labels);
			p.setPen(c.text);
			p.drawText(pill, Qt::AlignCenter, text);
		} else {
			const bool muted = field == Start || field == Crc || field == End;
			p.setFont(font());
			p.setPen(field == Data && error ? c.bad : muted ? c.muted : c.text);
			p.drawText(QRect(rect.left(), y, rect.width() + FIELD_GAP, line), LEFT, text);
		}
		if (!fields_) continue;
		const int labelTop = y + line + LABEL_GAP;
		const QString name = fieldName(field), meaning = fieldMeaning(i, side, field);
		p.setFont(labels);
		p.setPen(c.muted);
		p.drawText(QRect(rect.left(), labelTop, rect.width(), labelLineHeight()), LEFT, name);
		if (meaning.isEmpty()) continue;
		const int at = rect.left() + labelFm.horizontalAdvance(name + QLatin1Char(' '));
		p.setPen(field == Crc ? c.good : (error && (field == Function || field == Data)) ? c.bad : c.text);
		p.drawText(QRect(at, labelTop, rect.right() + 1 - at, labelLineHeight()), LEFT, meaning);
	}
	p.setFont(font());
}

/* not live: a 3 px amber stripe down the card's left edge, inside its rounded corners */
void DumpView::drawStripe(QPainter &p, int i) const {
	const QRect stripe = stripeRect(i);
	if (stripe.isEmpty()) return;
	QPainterPath card, edge;
	card.addRoundedRect(QRectF(cardRect(i)).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
	edge.addRect(QRectF(stripe));
	p.setRenderHint(QPainter::Antialiasing, true);
	p.fillPath(card.intersected(edge), Theme::colors().warn);
	p.setRenderHint(QPainter::Antialiasing, false);
}

void DumpView::drawCard(QPainter &p, int i, const QPoint &at) const {
	const ThemeColors &c = Theme::colors();
	const DumpBlock &b = blocks_[i];
	const Card &card = cards_[i];
	const QRect rect = cardRect(i);
	const int line = lineHeight();
	const int textLeft = at.x() + PAD;
	const int textWidth = rect.width() - 2 * PAD;
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(QPen(c.border, 1));
	p.setBrush(c.surface);
	p.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
	p.setRenderHint(QPainter::Antialiasing, false);
	drawStripe(p, i);

	if (b.kind == DumpBlock::FastStream) {
		p.setPen(c.muted);
		p.drawText(QRect(textLeft, at.y() + card.noteTop, textWidth, line), LEFT,
				tr("fast stream %1: see the Chart").arg(b.stream));
		return;
	}
	if (card.titleTop >= 0) {
		QFont bold = uiFont();
		bold.setBold(true);
		p.setFont(bold);
		p.setPen(c.text);
		p.drawText(QRect(textLeft, at.y() + card.titleTop, textWidth, headLineHeight()), LEFT, titleText(i));
		p.setFont(font());
	}
	if (card.requestTop >= 0) {
		const int y = at.y() + card.requestTop;
		const QString sentence = requestSentence(b);
		drawHead(p, textLeft, y, textWidth, requestLabel(), sentence, c.text);
		if (b.single) {
			/* the split's reason after the sentence, muted */
			const int x = textLeft + headLabelW_ + QFontMetrics(uiFont()).horizontalAdvance(sentence) + 12;
			p.setFont(uiFont());
			p.setPen(c.muted);
			p.drawText(QRect(x, y, std::max(0, rect.right() - PAD - x), headLineHeight()), LEFT,
					QFontMetrics(uiFont()).elidedText(QStringLiteral("· ") + tr("split: the device refused the merged read"),
							Qt::ElideRight, std::max(0, rect.right() - PAD - x)));
			p.setFont(font());
		}
		drawStrip(p, i, Request, textLeft, at.y() + card.requestStripTop);
	}
	QColor stateColour;
	const QString state = replyState(i, stateColour);
	drawHead(p, textLeft, at.y() + card.replyTop, textWidth, replyLabel(b), state, stateColour);
	if (card.replyStripTop >= 0) drawStrip(p, i, Reply, textLeft, at.y() + card.replyStripTop);
	if (card.noReplyTop >= 0) {
		p.setFont(uiFont());
		p.setPen(c.muted);
		p.drawText(QRect(textLeft, at.y() + card.noReplyTop, textWidth, line), LEFT, noReplyText(i));
		p.setFont(font());
	}
	drawGrid(p, i, at);
	drawDecode(p, i, at);
}

/* The rows: the address, then the cells from the row's first to the block's last byte. The chips first (a register's
 * bytes in one row are one fill, 1.5 px inside its cells, so two neighbours have 3 px of card between them), then
 * each byte's text over them. A hovered register's chips are filled stronger and outlined in its colour, a selected
 * one's filled stronger and framed in the accent; the other chips keep their tint (dimming them greyed the whole tab,
 * the other cards too). Cells no read covers are an empty run in a dashed frame with its words, not dots that say
 * nothing: dashed, as an empty slot is drawn, and empty, as a debugger leaves memory it could not read. */
void DumpView::drawGrid(QPainter &p, int i, const QPoint &at) const {
	const ThemeColors &c = Theme::colors();
	const bool dark = Theme::isDark();
	const DumpBlock &b = blocks_[i];
	const Card &card = cards_[i];
	const int line = lineHeight(), cell = cellWidth();
	const int left = at.x() - CARD_GAP + gridLeft();
	const bool answered = b.data.size() >= b.size;
	const int viewHeight = viewport()->height();
	const double rest = chipAlpha(dark), hot = chipAlpha(dark, true);
	const QFont labels = labelFont();
	for (int rowIndex = 0; rowIndex < card.rows.size(); rowIndex++) {
		const Row &row = card.rows[rowIndex];
		const int y = at.y() + row.top;
		if (y + line < 0 || y > viewHeight) continue; /* a long manual read: only the rows in view */
		p.setPen(c.muted);
		p.drawText(QRect(at.x() + PAD, y, left - at.x() - PAD, line), LEFT, rowAddress(row.base));
		p.setRenderHint(QPainter::Antialiasing, true);
		QRectF selectedChip;
		QVector<QPair<QRectF, QColor>> hoveredChips;
		for (int r = 0; r < card.regs.size() && answered; r++) {
			const RegDef &d = defs_[card.regs[r]];
			const int from = std::max(int(d.addr), row.base), to = std::min(int(d.addr) + d.size, row.base + 16) - 1;
			if (from > to) continue;
			const QColor colour = card.colours[r];
			const bool isHovered = regKey(d) == hovered_, lit = isHovered || regKey(d) == selected_;
			const QRectF chip(left + (from - row.base) * cell + 1.5, y + 2.5, (to - from + 1) * cell - 3.0, line - 5.0);
			/* a neutral chip (Colour: plotted) lit: the muted grey at the lit alpha */
			const QColor fill = colour.isValid() ? chipFill(colour, c.surface, lit ? hot : rest)
												 : lit ? chipFill(c.muted, c.surface, hot) : c.surface2;
			p.setPen(Qt::NoPen);
			p.setBrush(fill);
			p.drawRoundedRect(chip, 4, 4);
			if (regKey(d) == selected_) selectedChip = chip;
			if (isHovered) hoveredChips << qMakePair(chip, colour.isValid() ? colour : c.muted);
		}
		for (const auto &chip : hoveredChips) {
			p.setPen(QPen(chip.second, 1.5));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(chip.first.adjusted(-1, -1, 1, 1), 5, 5);
		}
		if (!selectedChip.isNull()) {
			p.setPen(QPen(c.accent, 2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(selectedChip.adjusted(-1, -1, 1, 1), 5, 5);
		}
		/* the runs no read covers: a dashed frame, its words in it when they fit */
		for (const Run &run : unreadRuns(i, rowIndex)) {
			const int offset = row.base + run.from - int(b.addr);
			const QRect frame = unreadRect(i, offset);
			QPen dashed(unreadEdge(c), 1);
			dashed.setDashPattern({ 3, 3 });
			p.setPen(dashed);
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(QRectF(frame).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
			const QString words = unreadWords(i, offset, frame.width());
			if (words.isEmpty()) continue;
			p.setFont(labels);
			p.setPen(c.muted);
			p.drawText(frame, Qt::AlignCenter, words);
			p.setFont(font());
		}
		p.setRenderHint(QPainter::Antialiasing, false);
		for (int column = 0; column < 16; column++) {
			const int offset = row.base + column - int(b.addr);
			if (offset >= b.size) break; /* after the block: the card's surface, nothing invented */
			if (offset < 0 || !answered) continue; /* in a run, framed above */
			const QRect rect(left + column * cell, y, cell, line);
			p.setPen(byteColour(i, offset));
			p.drawText(rect, Qt::AlignCenter, byteText(b.data[offset]));
		}
	}
}

/* One line per register, placed by the flow (relayout): its dot, its name, its value; each elided past its room (the
 * whole line is the tooltip). The hovered and the selected register's lines are filled with its colour's tint (the
 * chip's own), so the chip and its line light together; the hovered one's dot larger and its name bold, the selected
 * one's name and value bold. */
void DumpView::drawDecode(QPainter &p, int i, const QPoint &at) const {
	Q_UNUSED(at);
	const ThemeColors &c = Theme::colors();
	const bool dark = Theme::isDark();
	const int names = nameWidth(), values = valueWidth();
	const DumpBlock &b = blocks_[i];
	const Card &card = cards_[i];
	for (int r = 0; r < card.regs.size(); r++) {
		const QRect rect = decodeRect(i, r);
		if (rect.isEmpty()) continue;
		const RegDef &d = defs_[card.regs[r]];
		const bool isHovered = regKey(d) == hovered_, isSelected = regKey(d) == selected_;
		const QColor colour = card.colours[r].isValid() ? card.colours[r] : c.muted;
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		if (isHovered || isSelected) {
			p.setBrush(card.colours[r].isValid() ? chipFill(colour, c.surface, chipAlpha(dark)) : c.surface2);
			p.drawRoundedRect(QRectF(rect).adjusted(-4, 1, 0, -1), 4, 4);
		}
		p.setBrush(colour);
		const double radius = isHovered ? 5 : 4;
		p.drawEllipse(QPointF(rect.left() + 4.5, rect.center().y() + 0.5), radius, radius);
		p.setRenderHint(QPainter::Antialiasing, false);
		QFont nameFont = font();
		nameFont.setBold(isSelected || isHovered);
		QFont valueFont = font();
		valueFont.setBold(isSelected);
		const QByteArray raw = b.data.size() >= b.size ? b.data.mid(d.addr - b.addr, d.size) : QByteArray();
		p.setPen(c.text);
		p.setFont(nameFont);
		p.drawText(QRect(rect.left() + 14, rect.top(), names, rect.height()), LEFT,
				QFontMetrics(nameFont).elidedText(d.name, Qt::ElideRight, names - 8));
		p.setFont(valueFont);
		p.drawText(QRect(rect.left() + 14 + names, rect.top(), values, rect.height()), LEFT,
				QFontMetrics(valueFont).elidedText(decodeValue(d, raw), Qt::ElideRight, values));
		p.setFont(font());
	}
}

/* ------------------------------------------------------------------ the mouse and the keys */

void DumpView::mouseMoveEvent(QMouseEvent *e) {
	setHovered(registerAt(e->position().toPoint()));
	QAbstractScrollArea::mouseMoveEvent(e);
}

/* a click picks the register under it (here and in the Registers tab); the menu is the right-click's */
void DumpView::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	const RegKey key = e->button() == Qt::LeftButton ? registerAt(pos) : NO_REGISTER;
	if (key == NO_REGISTER) {
		QAbstractScrollArea::mousePressEvent(e);
		return;
	}
	for (int i = 0; i < cards_.size(); i++)
		if (cardRect(i).contains(pos)) selectedCard_ = i;
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
	if (e->reason() == QContextMenuEvent::Keyboard) {
		/* the Menu key or Shift+F10: the selected register's menu, at its chip. The key press opens it (keyPressEvent);
		 * the system's own context menu event for the same key, if one follows, is not a second menu */
		if (keyMenu_.isValid() && keyMenu_.elapsed() < 500) return;
		keyMenu_.start();
		const int card = currentCard();
		QPoint at = viewport()->rect().center();
		if (card >= 0 && selected_ != NO_REGISTER)
			at = byteRect(card, regKeyAddr(selected_) - blocks_[card].addr).bottomLeft();
		emit menuRequested(selected_, card, viewport()->mapToGlobal(at));
		return;
	}
	const RegKey key = registerAt(e->pos());
	int card = -1;
	for (int i = 0; i < cards_.size() && card < 0; i++)
		if (cardRect(i).contains(e->pos())) card = i;
	if (key != NO_REGISTER) {
		selectedCard_ = card;
		select(key);
		emit registerPicked(key, false);
	}
	emit menuRequested(key, card >= 0 ? card : currentCard(), e->globalPos());
}

/* Up and Left the register before, Down and Right the next, across the cards (the first key without a selection
 * picks the first register); each move is a click's (the Registers tab follows), scrolled into view. Enter goes to
 * the Registers tab on it. The Menu key and Shift+F10 come as a context menu event (contextMenuEvent). */
void DumpView::keyPressEvent(QKeyEvent *e) {
	const int key = e->key();
	if (key == Qt::Key_Menu || (key == Qt::Key_F10 && e->modifiers() == Qt::ShiftModifier)) {
		const QPoint centre = viewport()->rect().center();
		QContextMenuEvent menu(QContextMenuEvent::Keyboard, centre, viewport()->mapToGlobal(centre), e->modifiers());
		contextMenuEvent(&menu);
		return;
	}
	if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		if (selected_ != NO_REGISTER) emit registerPicked(selected_, true);
		return;
	}
	const bool back = key == Qt::Key_Up || key == Qt::Key_Left, forth = key == Qt::Key_Down || key == Qt::Key_Right;
	if (!back && !forth) {
		QAbstractScrollArea::keyPressEvent(e);
		return;
	}
	QVector<QPair<int, int>> order; /* every register of every card, in the cards' order */
	for (int i = 0; i < cards_.size(); i++)
		for (int r = 0; r < cards_[i].regs.size(); r++) order << qMakePair(i, r);
	if (order.isEmpty()) return;
	int at = -1;
	const int card = currentCard();
	for (int k = 0; k < order.size() && at < 0; k++)
		if (order[k].first == card && regKey(defs_[cards_[card].regs[order[k].second]]) == selected_) at = k;
	const int next = at < 0 ? 0 : std::clamp(at + (forth ? 1 : -1), 0, int(order.size()) - 1);
	const RegKey picked = regKey(defs_[cards_[order[next].first].regs[order[next].second]]);
	selectedCard_ = order[next].first;
	select(picked);
	ensureVisible(selectedCard_, picked);
	emit registerPicked(picked, false);
}

/* ------------------------------------------------------------------ tooltips */

/* A text's lines broken at their words so none is wider than width (a tooltip shows its text's lines as they come). */
QString DumpView::wrapTip(const QString &text, int width) const {
	const QFontMetrics fm(QToolTip::font());
	QStringList out;
	for (const QString &line : text.split(QLatin1Char('\n'))) {
		if (fm.horizontalAdvance(line) <= width) {
			out << line;
			continue;
		}
		QString current;
		for (const QString &word : line.split(QLatin1Char(' '))) {
			const QString longer = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
			if (!current.isEmpty() && fm.horizontalAdvance(longer) > width) {
				out << current;
				current = word;
			} else {
				current = longer;
			}
		}
		out << current;
	}
	return out.join(QLatin1Char('\n'));
}

QRect DumpView::tipRoom(int card) const {
	const QRect rect = cardRect(card);
	const int left = rect.left() + PAD, right = decodeLeft() - TIP_GAP;
	return QRect(left, rect.top(), std::max(0, right - left), rect.height());
}

/* The chip's tooltip goes under its row, from the chip's left, moved left so that it ends before the decode list;
 * a decode line's under the line; a field's under its column. Its size from the tooltip's font and chrome. */
QRect DumpView::tipPlace(const QPoint &pos) const {
	const QString text = toolTipAt(pos);
	if (text.isEmpty()) return {};
	const QFontMetrics fm(QToolTip::font());
	const QSize size = fm.boundingRect(QRect(0, 0, 4000, 4000), Qt::AlignLeft | Qt::AlignTop, text).size()
			+ QSize(TIP_CHROME, TIP_CHROME);
	for (int i = 0; i < cards_.size(); i++) {
		if (!cardRect(i).contains(pos)) continue;
		for (int line = 0; line < decodeLines(i); line++) {
			const QRect rect = decodeRect(i, line);
			if (rect.contains(pos)) return QRect(QPoint(rect.left(), rect.bottom() + 4), size);
		}
		break;
	}
	int card = -1, offset = 0;
	if (byteAt(pos, card, offset)) {
		const QRect cell = byteRect(card, offset);
		int chipLeft = cell.left();
		const int reg = offset >= 0 ? regAt(card, offset) : -1;
		if (reg >= 0) {
			const RegDef &d = defs_[cards_[card].regs[reg]];
			const int rowBase = (int(blocks_[card].addr) + offset) & ~0xF;
			chipLeft = byteRect(card, std::max(int(d.addr), rowBase) - blocks_[card].addr).left();
		}
		const QRect room = tipRoom(card);
		const int left = std::max(room.left(), std::min(chipLeft, room.right() + 1 - size.width()));
		return QRect(QPoint(left, cell.bottom() + 4), size);
	}
	Side side = Request;
	Field field = Start;
	if (fieldAt(pos, card, side, field)) {
		const QRect rect = fieldRect(card, side, field);
		return QRect(QPoint(rect.left(), rect.bottom() + 4), size);
	}
	return {};
}

bool DumpView::viewportEvent(QEvent *e) {
	if (e->type() == QEvent::Leave) setHovered(NO_REGISTER);
	if (e->type() != QEvent::ToolTip) return QAbstractScrollArea::viewportEvent(e);
	const auto *help = static_cast<QHelpEvent *>(e);
	const QPoint pos = help->pos();
	const QString tip = toolTipAt(pos);
	const QRect place = tipPlace(pos);
	if (tip.isEmpty()) {
		QToolTip::hideText();
		e->ignore();
		return true;
	}
	if (place.isNull()) {
		QToolTip::showText(help->globalPos(), tip, viewport());
		return true;
	}
	/* shown with the thing's rect, so it goes when the mouse leaves it: the chip's cells in this row, the line, the
	 * column */
	QRect hot;
	int card = -1, offset = 0;
	Side side = Request;
	Field field = Start;
	for (int i = 0; i < cards_.size() && hot.isNull(); i++)
		for (int line = 0; line < decodeLines(i) && hot.isNull(); line++)
			if (decodeRect(i, line).contains(pos)) hot = decodeRect(i, line);
	if (hot.isNull() && byteAt(pos, card, offset)) {
		hot = byteRect(card, offset);
		const int reg = offset >= 0 ? regAt(card, offset) : -1;
		if (reg >= 0) {
			const RegDef &d = defs_[cards_[card].regs[reg]];
			const int rowBase = (int(blocks_[card].addr) + offset) & ~0xF;
			hot = byteRect(card, std::max(int(d.addr), rowBase) - blocks_[card].addr)
						  .united(byteRect(card, std::min(int(d.addr) + d.size, rowBase + 16) - 1 - blocks_[card].addr));
		}
	} else if (hot.isNull() && fieldAt(pos, card, side, field)) {
		hot = fieldRect(card, side, field);
	}
	const QPoint global = viewport()->mapToGlobal(place.topLeft());
	QToolTip::showText(global, tip, viewport(), hot);
	/* Qt puts a tooltip a cursor's height under the point it is given: moved to its place, and a chip's kept left of
	 * the decode list by its real width */
	int rightEdge = -1;
	if (byteAt(pos, card, offset)) rightEdge = viewport()->mapToGlobal(QPoint(tipRoom(card).right(), 0)).x();
	for (QWidget *top : QApplication::topLevelWidgets()) {
		if (!top->isVisible() || !top->inherits("QTipLabel")) continue;
		QPoint at = global;
		if (rightEdge >= 0 && at.x() + top->width() - 1 > rightEdge)
			at.setX(std::max(viewport()->mapToGlobal(QPoint(tipRoom(card).left(), 0)).x(), rightEdge + 1 - top->width()));
		top->move(at);
	}
	return true;
}

/* a decode line's whole text; a byte's words (and why its card is not live), wrapped to the room left of the decode
 * list; a field's name, meaning and the protocol's words; a row's address; else the card's reason */
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
			tip = wrapTip(tip, tipRoom(i).width() - TIP_CHROME);
		}
		Side side = Request;
		Field field = Start;
		if (tip.isEmpty() && fieldAt(pos, card, side, field) && card == i)
			tip = fieldLabel(i, side, field) + QLatin1Char('\n') + fieldTip(i, side, field);
		for (const Row &row : cards_[i].rows) {
			const QRect address(cardRect(i).left() + PAD, cardRect(i).top() + row.top,
					gridLeft() - CARD_GAP - PAD, lineHeight());
			if (tip.isEmpty() && address.contains(pos)) tip = addrText(uint16_t(row.base));
		}
		if (tip.isEmpty()) tip = notLiveReason(i);
	}
	return tip;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* The Dump tab's picture: the device's memory as the reads bring it. One card per block read, in address order:
 * the request the host sent and the reply's header, rebuilt with evre::build from the same bytes (equal to the
 * wire's); the payload as a hex grid, 16 bytes a row, the rows aligned to 16 with the row's base address at the
 * left; every mapped register's bytes one chip, tinted in its colour with a 1 px edge in it; and beside each row
 * one decode line per register that starts in it (its colour dot, its name, its value as the Registers tab shows
 * it), the row as tall as its lines. The reply's CRC and end byte close the card on a line of their own: the grid
 * is the payload only.
 *
 *   +--------------------------------------------------------------------------------+
 *   | -> READ 22 bytes @ 0xD000   7B 01 AA 00 D0 16 00 FC A5 7D                      |
 *   | <- REPLY 12 ms ago          7B 01 AB 00 D0 16 00                               |
 *   |  D000  [10 27 00 00] [FA 7E 40 41] ...           o UPTIME       10000 ms        |
 *   |                                                  o SUPPLY_V     12.031 V        |
 *   |  D010  [05 00] [2C 01] [B9 00] ..  ..  ..        o STATE        run . READY     |
 *   |                          AE 41 7D                                              |
 *   +--------------------------------------------------------------------------------+
 *
 * A byte of a row that no read covers is "··" (never an invented 00); a byte inside the block that no register
 * maps (the engine reads over small gaps) is plain hex, muted. Values not live (Hold, a stale or failed reply,
 * the link closed, the manual READ's card) get a thin amber frame around their card, the reason in the reply
 * line and the card's tooltip, so a frozen dump never reads as live.
 *
 * Painted as BitView paints (ThemeColors, a monospace font, no style sheet), left to right in every language: the
 * addresses, the bytes and the decode column belong to the grid, not to the window. Only the cards in view are
 * painted. */
#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QVector>
#include <functional>

#include "model/device_map.h"

/* One block as the Dump tab shows it: the engine's reply (IoEngine::DumpReply) and what kind of read it was. */
struct DumpBlock {
	enum Kind { Read, AutoSend, Manual, FastStream };
	Kind kind = Read;
	uint8_t slave = 0;        /* the device's, as the table has it (RegDef::slave) */
	uint8_t wireSlave = 0;    /* where the request went: the frames are rebuilt with it */
	uint16_t addr = 0, size = 0;
	QByteArray data;          /* the payload; shorter than size (empty): no answer yet */
	double time = 0;          /* when it came, on the view's clock (setClock) */
	quint64 version = 0;
	bool single = false;      /* one register of a block the device refused merged */
	QString error;            /* the last answer's error (the bytes the last good ones); empty: fine */
	QString device;           /* on a bus: the device's name, written before the request */
	QString stream;           /* FastStream: the stream's name */
	double rate = 0;          /* AutoSend: its frames a second */
};

class DumpView : public QAbstractScrollArea {
	Q_OBJECT
public:
	/* the tint of a chip: the register's colour at this alpha over the card, the byte text in the theme's text
	 * colour on it (4.5:1 in both themes, checked) */
	static constexpr double CHIP_ALPHA_DARK = 0.30, CHIP_ALPHA_LIGHT = 0.22;
	enum ColourMode { AllRegisters, PlottedOnly };
	static constexpr RegKey NO_REGISTER = ~RegKey(0); /* select() and registerAt(): none (0 is slave 0's 0x0000) */

	explicit DumpView(QWidget *parent = nullptr);

	void setBlocks(const QVector<DumpBlock> &blocks);
	const QVector<DumpBlock> &blocks() const { return blocks_; }
	void setRegisters(const QVector<RegDef> &defs); /* the map (on a bus: every device's) */
	/* the chart's colour of each plotted register: its chip and dot take it; the others the palette's */
	void setColours(const QHash<RegKey, QColor> &plotted);
	void setColourMode(ColourMode mode);
	ColourMode colourMode() const { return colourMode_; }
	void setHold(bool on);                          /* the picture frozen: said in every reply line */
	bool hold() const { return hold_; }
	void setConnected(bool on);                     /* the link closed: the bytes are the last ones */
	void setClock(std::function<double()> now);     /* the seconds DumpBlock::time is on */
	void setStaleAfterMs(qint64 ms);                /* a reply older than this is stale (the Registers tab's) */
	void select(RegKey key);                        /* its chips framed, its decode line bold */
	RegKey selected() const { return selected_; }

	/* what is not live in card i, and why (empty: live): its amber frame and the reply line's words */
	QString notLiveReason(int card) const;
	/* the colour a register's chip takes in card i (invalid: no chip of it there); the tint over the card */
	QColor chipColour(int card, const RegKey &key) const;
	static QColor chipFill(const QColor &colour, const QColor &card, bool dark);

	/* where things are drawn, in the viewport's coordinates (the tests, the tooltips) */
	QRect cardRect(int card) const;
	QRect byteRect(int card, int offset) const;      /* the cell of the block's byte at offset (may be outside) */
	/* the card's decode line `line`, top to bottom (one per register of the block, in address order) */
	QRect decodeRect(int card, int line) const;
	QString decodeText(int card, int line) const;    /* its name and value, whole (the line may elide them) */
	int decodeLines(int card) const;
	RegKey registerAt(const QPoint &pos) const;      /* under a chip's byte or a decode line; NO_REGISTER: none */
	int paintCount() const { return paints_; }

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void changeEvent(QEvent *e) override;
	bool viewportEvent(QEvent *e) override; /* tooltips */

private:
	static constexpr int PAD = 12;          /* inside a card */
	static constexpr int CARD_GAP = 10;     /* between cards, and around them */
	static constexpr int GRID_GAP = 6;      /* between the reply line and the grid */
	static constexpr int DECODE_GAP = 18;   /* between the grid and the decode column */
	static constexpr int NAME_CHARS = 14;   /* a decode line's name, at most (elided past it) */
	static constexpr int VALUE_CHARS = 22;  /* and its value */

	/* one row of the grid: its base address, its top in the card, its height, the registers starting in it */
	struct Row {
		int base = 0;
		int top = 0, height = 0;
		QVector<int> starts;               /* indices into Card::regs */
	};
	/* a card laid out: its registers in address order with their colours, its rows, its lines */
	struct Card {
		int top = 0, height = 0;           /* in the content (scrolled) */
		QVector<int> regs;                 /* indices into defs_ */
		QVector<QColor> colours;           /* each one's colour; invalid: neutral (Colour: plotted) */
		QVector<Row> rows;
		int titleTop = -1, requestTop = -1, replyTop = -1, gridTop = -1, crcTop = -1, noteTop = -1;
	};

	void relayout();
	void assignColours(Card &card) const;
	int lineHeight() const;
	int cellWidth() const;
	int gridLeft() const;
	int decodeLeft() const;
	int contentWidth() const;
	QPoint origin() const; /* the content's top left in the viewport (the scroll) */
	int regAt(int card, int offset) const; /* the index into Card::regs of the register holding this byte; -1 none */

	QString requestLabel(const DumpBlock &block) const;
	QString replyLabel(const DumpBlock &block) const;
	QString replyState(int card, QColor &colour) const;
	QString decodeValue(const RegDef &def, const QByteArray &raw) const;

	void drawCard(QPainter &p, int card, const QPoint &at) const;
	void drawBytes(QPainter &p, int x, int y, const QByteArray &bytes, int mutedFirst, int mutedLast) const;
	void drawGrid(QPainter &p, int card, const QPoint &at) const;
	void drawDecode(QPainter &p, int card, const QPoint &at) const;

	QVector<DumpBlock> blocks_;
	QVector<Card> cards_;
	QVector<RegDef> defs_;
	QHash<RegKey, QColor> plotted_;
	ColourMode colourMode_ = AllRegisters;
	bool hold_ = false;
	QString heldAt_;
	bool connected_ = true;
	std::function<double()> now_;
	qint64 staleAfterMs_ = 1000;
	RegKey selected_ = NO_REGISTER;
	int contentHeight_ = 0;
	int labelWidth_ = 0;  /* the widest request or reply label of the cards: their frame bytes line up */
	int paints_ = 0;
	bool drawnDark_ = true;
};

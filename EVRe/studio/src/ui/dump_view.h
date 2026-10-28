/* SPDX-License-Identifier: Apache-2.0 */
/* The Dump tab's picture: the device's memory as the reads bring it. One card per block read, in address order:
 * the request the host sent and the reply, each a strip of the frame's named fields in the wire's order (START,
 * SLAVE, FUNCTION, OFFSET, COUNT, DATA, CRC, END: their bytes on one line, the field's name and meaning under them),
 * rebuilt with evre::build from the same bytes (equal to the wire's); the payload as a hex grid, 16 bytes a row, the
 * rows aligned to 16 with the row's base address at the left, one line high, the columns' offsets 00 .. 0F over them
 * (so a byte's address is its row's plus its column's, read, not counted); every mapped register's bytes one chip
 * (a fill in its colour, no edge); and beside the grid the decode list, one line per register (its colour dot, its
 * name, its value as the Registers tab shows it), each level with its register's row when there is room, else under
 * the line before it.
 *
 *   +------------------------------------------------------------------------------------------------+
 *   |  -> REQUEST  READ 22 bytes @ 0xD000                                                            |
 *   |  7B     01       AA                 00 D0          16 00                    FC A5   7D         |
 *   |  START  SLAVE 1  FUNCTION READ      OFFSET 0xD000  COUNT 22   DATA none     CRC     END        |
 *   |                                                                                                |
 *   |  <- REPLY    12 ms ago                                                                         |
 *   |  7B     01       AB                 00 D0          16 00      (22 bytes v)  AE 41   7D         |
 *   |  START  SLAVE 1  FUNCTION READ_RESP OFFSET 0xD000  COUNT 22   DATA payload  CRC OK  END        |
 *   |                                                                                                |
 *   |         00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F                                        |
 *   |  D000  [10 27 00 00][FA 7E 40 41][2D B2 BD 3F][66 66 26 42]    o UPTIME       10000 ms         |
 *   |  D010  [05 00][2C 01][B9 00]                                   o SUPPLY_V     12.031 V         |
 *   |                                                                o SUPPLY_I     1.482 A          |
 *   |                                                                o STATE        run . READY      |
 *   +------------------------------------------------------------------------------------------------+
 *
 * The strips share their columns over the whole tab (each as wide as the widest text it holds), so the request's
 * bytes stand over the reply's, field under field, in every card. With Fields off (setFields) each frame is one line
 * of bytes, the fields named in their tooltips. The payload's place in a strip is a grey pill "22 bytes v" (the bytes
 * are the grid under it), not an ellipsis that says nothing. A row starts at its first cell and ends at the block's
 * last byte: the cells before a block that starts inside a row, and a block with no reply yet, are an empty run
 * framed by a dashed line with its words in it ("4 bytes not read", "no reply yet") when they fit (never an invented
 * 00); a byte inside the block that no register maps is plain hex, muted. Values not live (Hold, a stale or failed
 * reply, the link closed, the manual READ's card, the last write's) get a 3 px amber stripe at their card's left edge
 * and the reason in the reply's label and the card's tooltip, so a frozen dump never reads as live. The last write is
 * a card at the top: its WRITE_ACK (or WRITE) as the request, the written bytes as the grid, the WRITE_ACK_RESP (or
 * the refusal) as the reply; a WRITE is never answered, and the card says so. A fast stream's newest block is a card of
 * its own: the READ_RESP nobody asked for as the reply's strip (no request), the block's 8-byte header as a strip of
 * its four members (FIRST, COUNT, FLAGS, SPARE), a line for the records lost, then the records one row each: the
 * number, each channel's bytes a chip in its colour and its value; folded the first three and the last, with Show all
 * every record (as many rows as the stream's largest block, so the card never jumps between blocks).
 *
 * A byte whose value changed glows a moment (its text in a readable accent, fading over the Registers tab's
 * GLOW_MS), so the dump and the table pulse together; not while a register is hovered. Hovering a byte or a decode
 * line lights its register in its own colour (the chips filled stronger and outlined, the decode line filled), and
 * nothing else changes: the other chips keep their tint. A hovered byte also lights its column's offset and its row's
 * address, a crosshair in the register's tint, so its place reads at the edges. A fast card's records have no column
 * offsets: a row there is a record, not 16 bytes of memory. A click selects it (the same light, framed in the accent; the
 * window makes it the Registers tab's current row), a double-click goes there, a right-click asks for the menu (the
 * tab's, repeated in its Copy button); a click on empty room (no chip, decode line or fast card's part) clears the
 * selection, here and in the Registers tab. The keys do the same: the arrows move the
 * selection, Enter goes there, Escape clears it, the Menu key or Shift+F10 opens the menu.
 *
 * Painted as BitView paints (ThemeColors, no style sheet): the bytes, addresses, names and values in a monospace
 * font, the labels and field names in the window's font a point smaller; left to right in every language, the
 * addresses, the bytes and the decode list belong to the grid, not to the window. Only the cards in view are
 * painted. */
#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QVector>
#include <functional>

#include "model/device_map.h"

class QTimer;
struct ThemeColors;

/* One block as the Dump tab shows it: the engine's reply (IoEngine::DumpReply) and what kind of read it was (or the
 * last write: its bytes, WRITE_ACK's answer or the device's refusal). */
struct DumpBlock {
	enum Kind { Read, AutoSend, Manual, FastStream, Write };
	Kind kind = Read;
	uint8_t slave = 0;        /* the device's, as the table has it (RegDef::slave) */
	uint8_t wireSlave = 0;    /* where the request went: the frames are rebuilt with it */
	uint16_t addr = 0, size = 0;
	QByteArray data;          /* the payload; shorter than size (empty): no answer yet */
	double time = 0;          /* when it came, on the view's clock (setClock) */
	quint64 version = 0;
	bool single = false;      /* one register of a block the device refused merged */
	QString error;            /* the last answer's error (the bytes the last good ones); empty: fine */
	uint8_t errorCode = 0;    /* the last answer's ERROR_RESP code (its frame is shown); 0: none came */
	bool timedOut = false;    /* the last read had no answer in time: no frame to show */
	QString device;           /* on a bus: the device's name, written before the request */
	QString stream;           /* FastStream: the stream's name */
	double rate = 0;          /* AutoSend: its frames a second */
	bool ack = false;         /* Write: a WRITE_ACK (answered), not a WRITE (never answered) */
	/* FastStream: the newest block (IoEngine::FastDump; data empty: switched on, no block yet), version its serial */
	int streamIndex = -1;     /* the stream's index in the map's streams (setStreams) */
	quint64 first = 0;        /* its first record in 64 bits; a bad block: the wire's 32 */
	int count = 0;            /* the header's count of records */
	uint8_t flags = 0, spare = 0;
	quint64 lost = 0;         /* records lost before it */
	quint64 lostTotal = 0;    /* the stream's records lost so far, counted from its start number `start` */
	quint64 start = 0;
	/* the records lost since the block the tab showed before it (DumpTab::applyFastDumps): the tab takes the newest
	 * block ten times a second, so the losses may be in blocks never shown; 0 (none shown before): `lost` */
	quint64 lostSince = 0;
	bool newStart = false;    /* a new start of the stream */
	int check = 0;            /* 0 a good block, 1 a bad count, 2 a newer kind (fast::BlockCheck) */
	bool stopped = false;     /* the stream is switched off: this was its last block */
	qint64 wallMs = 0;        /* when it came, on the wall clock (ms since 1970): "last block 14:03:12" */
};

class DumpView : public QAbstractScrollArea {
	Q_OBJECT
public:
	/* the tint of a chip: the register's colour at this alpha over the card, the byte text in the theme's text
	 * colour on it (4.5:1 in both themes at rest and lit, checked). The fill carries the colour alone (no edge), so
	 * it is a step stronger than an edged chip's. Lit (hovered or selected) the fill is a clear step stronger again,
	 * and the other chips stay as they are: the dark theme's lit text is white (litText), which keeps the yellow chip
	 * over 4.5:1 at 0.46 where the theme's text fell under it at 0.40. */
	static constexpr double CHIP_ALPHA_DARK = 0.34, CHIP_ALPHA_LIGHT = 0.26;
	static constexpr double CHIP_ALPHA_HOVER_DARK = 0.46, CHIP_ALPHA_HOVER_LIGHT = 0.50;
	static constexpr int STRIPE_W = 3;            /* the amber stripe of a card not live */
	static constexpr int FIELD_GAP = 14;          /* between two columns of a strip */
	enum ColourMode { AllRegisters, PlottedOnly };
	/* what the Copy items put on the clipboard (copyText); the records as CSV only of a fast stream's block */
	enum CopyKind { CopyRegisterBytes, CopyBlockHex, CopyBlockC, CopyDecoded, CopyRecordsCsv };
	/* a fast stream's block header, its members in the wire's order (PROTOCOL.md's block table) */
	enum HeaderField { First, HeaderCount, Flags, Spare, N_HEADER };
	/* what is under the mouse on a fast card's records (fastAt) */
	enum FastPart { NoPart, ChannelChip, ChannelValue, ChannelHead, RecordNumber, ShowAllButton };
	static constexpr int RECORDS_SHOWN = 3; /* a fast card folded: the first three records, then the last */
	/* a frame's fields, in the wire's order (PROTOCOL.md's frame table), and which frame of a card */
	enum Field { Start, Slave, Function, Offset, Count, Data, Crc, End, N_FIELDS };
	enum Side { Request, Reply };
	static constexpr RegKey NO_REGISTER = ~RegKey(0); /* select() and registerAt(): none (0 is slave 0's 0x0000) */

	explicit DumpView(QWidget *parent = nullptr);

	void setBlocks(const QVector<DumpBlock> &blocks);
	const QVector<DumpBlock> &blocks() const { return blocks_; }
	void setRegisters(const QVector<RegDef> &defs); /* the map (on a bus: every device's) */
	/* the chart's colour of each plotted register: its chip and dot take it; the others the palette's */
	void setColours(const QHash<RegKey, QColor> &plotted);
	void setColourMode(ColourMode mode);
	ColourMode colourMode() const { return colourMode_; }
	void setHold(bool on);                          /* the picture frozen: said in every reply's label */
	bool hold() const { return hold_; }
	QString heldAt() const { return heldAt_; }      /* when Hold was pressed, "14:03:12"; empty: following */
	void setConnected(bool on);                     /* the link closed: the bytes are the last ones */
	bool connected() const { return connected_; }
	/* the frames' fields named under their bytes (on), or each frame one line of bytes (off) */
	void setFields(bool on);
	bool fields() const { return fields_; }
	void setClock(std::function<double()> now);     /* the seconds DumpBlock::time is on */
	void setStaleAfterMs(qint64 ms);                /* a reply older than this is stale (the Registers tab's) */
	/* one frame's repaint, the ages and the newest bytes drawn. Qt merges update() calls until its paint request is
	 * delivered, so two ticks the window's thread takes in one event pass share one paint (the newer bytes): the paint
	 * settles every tick asked before it, and the tests count the merged ones instead of a paint each */
	void tick();
	void select(RegKey key);                        /* its chips framed, its decode line bold */
	RegKey selected() const { return selected_; }
	/* nothing selected (a click on empty room, Escape): selectionCleared, so the Registers tab clears too */
	void clearSelection();
	/* scrolled so the register's first chip is in view; a register no card holds yet: when its card comes */
	void scrollToRegister(RegKey key);
	void setEmptyText(const QString &text);         /* said in the middle while there is no card */

	int cardOf(RegKey key) const;                   /* the card holding the register; -1: none */
	/* the card the block items of the menu act on: the selected register's, else the first one in view; -1: none */
	int currentCard() const;
	/* the clipboard's text: the register's bytes, the card's block as hex rows or a C array, or its decoded lines */
	QString copyText(CopyKind kind, int card, RegKey key = NO_REGISTER) const;
	/* a byte's tooltip: its register (or not mapped, or not read) and address, then its value */
	QString byteTip(int card, int offset) const;
	double glow(int card, int offset) const;        /* how strongly a changed byte glows now: 1 just changed .. 0 */
	bool glowing() const;                           /* the glow's timer runs (only while a byte glows) */
	QColor byteColour(int card, int offset) const;  /* a byte's text now: plain, or toward glowText while it glows */
	static QColor glowText(const ThemeColors &c, bool dark); /* a glowing byte's text: in the accent's hue, 4.5:1 on any chip */
	RegKey hovered() const { return hovered_; }

	/* what is not live in card i, and why (empty: live): its amber stripe and the reply label's words */
	QString notLiveReason(int card) const;
	/* the colour a register's chip takes in card i (invalid: no chip of it there); the tint over the card */
	QColor chipColour(int card, const RegKey &key) const;
	static double chipAlpha(bool dark, bool hovered = false);
	static QColor chipFill(const QColor &colour, const QColor &card, double alpha);
	static QColor litText(const ThemeColors &c, bool dark); /* a lit chip's byte text */
	static QColor payloadFill(const ThemeColors &c);        /* the strip's payload pill */
	static QColor unreadEdge(const ThemeColors &c);         /* the dashed frame of cells no read covers */

	/* the strips: a field's name ("START" .. "END", the protocol's words), its meaning in card i's frame ("READ",
	 * "0xD000", "22 bytes ↓", "ok"; empty: none), its tooltip, its bytes; whether the card has that frame */
	static QString fieldName(Field field);
	QString fieldMeaning(int card, Side side, Field field) const;
	QString fieldTip(int card, Side side, Field field) const;
	QByteArray fieldBytes(int card, Side side, Field field) const;
	bool hasStrip(int card, Side side) const;
	QFont labelFont() const;                        /* the labels and field names: the window's font, a point smaller */

	/* where things are drawn, in the viewport's coordinates (the tests, the tooltips) */
	QRect cardRect(int card) const;
	QRect stripeRect(int card) const;                /* the amber stripe at its left edge; empty: live */
	QRect stripRect(int card, Side side) const;      /* the frame's strip (both lines; Fields off: its one line) */
	/* a field's column, its bytes and its label (Fields off: its bytes in the frame's line); empty: no such frame */
	QRect fieldRect(int card, Side side, Field field) const;
	QRect byteRect(int card, int offset) const;      /* the cell of the block's byte at offset (may be outside) */
	/* the grid's column offsets "00" .. "0F" over the byte columns: how many the card has (as many as its widest row's
	 * cells; 0: no grid, a fast card), each one's rect (the width of its column) and its text */
	int columnLabels(int card) const;
	QRect columnLabelRect(int card, int column) const;
	static QString columnLabelText(int column);
	QRect rowAddressRect(int card, int row) const;   /* the row's base address at its left, "D010" */
	QFont columnFont() const;                        /* the offsets: the bytes' monospace font, a point smaller */
	/* the byte under the mouse whose column offset and row address are lit (the crosshair); false: none */
	bool crosshair(int &card, int &offset) const;
	/* the card's decode line `line`, top to bottom (one per register of the block, in address order) */
	QRect decodeRect(int card, int line) const;
	QString decodeText(int card, int line) const;    /* its name and value, whole (the line may elide them) */
	int decodeLines(int card) const;
	int decodeLeft() const;                          /* where the decode list starts, the same in every card */
	int lineHeight() const;                          /* a bytes line: a grid row, a decode line */
	RegKey registerAt(const QPoint &pos) const;      /* under a chip's byte or a decode line; NO_REGISTER: none */
	QString toolTipAt(const QPoint &pos) const;      /* what the tooltip says there (empty: none) */
	/* where the tooltip at pos goes (in the viewport): a chip's under its row and left of the decode list, a decode
	 * line's under the line, a field's under its column; empty: at the mouse, as a tooltip usually is */
	QRect tipPlace(const QPoint &pos) const;
	/* the request's label and sentence with the frame's bytes (empty: no request, AUTO_SEND's or a note), the
	 * reply's label, its state and its frame's bytes ("…" for the payload) */
	QString requestText(int card) const;
	QString titleText(int card) const; /* a manual read's or a write's title; empty: none */
	QString replyText(int card) const;
	/* the request's or the reply's label ("→ REQUEST"): its line, and its arrow's ink as drawn (centred on the word's
	 * capitals, not on the font's maths axis, lower) */
	QRect headRect(int card, Side side) const;
	QRectF arrowRect(int card, Side side) const;
	/* the run of cells no read covers that holds this byte's cell (offset < 0: before the block; a block with no
	 * reply yet: all of it), as framed, and the words drawn in it (empty: they do not fit); empty rect: the byte is read */
	QRect unreadRect(int card, int offset) const;
	QString unreadText(int card, int offset) const;
	QRect payloadRect(int card, Side side) const;    /* the DATA field's pill; empty: no payload in that frame */
	QString payloadText(int card, Side side) const;  /* the pill's words, "22 bytes ↓"; empty: no payload pill */
	int paintCount() const { return paints_; }
	int glowRepaints() const { return glowRepaints_; } /* the glow's own repaints, while a byte fades (tests) */
	int ticksMerged() const { return ticksMerged_; }  /* ticks that shared an earlier tick's paint (tests) */
	/* ticks asked but not painted yet: at most the last one, awaiting its paint; more is a card left stale (tests) */
	int ticksUnpainted() const { return ticksAsked_ - ticksPainted_; }

	/* Fast streams: their definitions (the map's, by index: DumpBlock::streamIndex), and the chart's colour of each
	 * plotted channel by ChartView::fastKey (its chips and dot take it; the others the palette's) */
	void setStreams(const QVector<StreamDef> &streams);
	void setFastColours(const QHash<int, QColor> &plotted);
	/* a fast card: its block header's columns, their name ("FIRST" .. "SPARE", the protocol's), meaning ("record
	 * 1191259", "253 records", "LOST", "0"), tooltip and bytes */
	static QString headerFieldName(HeaderField field);
	QString headerFieldMeaning(int card, HeaderField field) const;
	QString headerFieldTip(int card, HeaderField field) const;
	QByteArray headerFieldBytes(int card, HeaderField field) const;
	QRect headerFieldRect(int card, HeaderField field) const; /* both lines; empty: no header there */
	QColor headerFieldColour(int card, HeaderField field) const; /* its meaning's ink: warn for LOST, a bad count */
	/* the line under the header: the records lost before this block, a new start, or a bad block's words (empty: none) */
	QString lossText(int card) const;
	QString badBlockText(int card) const;
	QRect lossRect(int card) const;
	/* the record rows as laid: folded the first three and the last (row 3), open every record (recordsPerBlock rows) */
	int fastRowsShown(int card) const;
	quint64 recordNumber(int card, int row) const;  /* its record's number; -1 (all ones): none in that row */
	QRect recordRect(int card, int row) const;      /* the whole row, in the viewport */
	QRect channelChipRect(int card, int row, int channel) const;
	QRect channelValueRect(int card, int row, int channel) const;
	QString channelValueText(int card, int row, int channel) const; /* "1.2000 A": a raw step shows (channelText) */
	QRect channelHeadRect(int card, int channel) const; /* the records head's dot, name and type of a channel */
	QColor channelColour(int card, int channel) const;  /* its chips' colour (invalid: neutral) */
	QString moreText(int card) const;               /* folded: "249 more records"; empty: none */
	QRect showAllRect(int card) const;              /* the Show all (Show fewer) button; empty: none */
	QString showAllText(int card) const;
	bool showAll(int card) const;
	void setShowAll(int card, bool on);             /* by the card's stream, kept while the view lives */
	bool fastAt(const QPoint &pos, int &card, int &row, int &channel, FastPart &part) const;
	int hoveredChannel() const { return hoverChannel_; } /* the channel lit on a fast card; -1: none */
	int hoveredFastCard() const { return hoverFastCard_; }

signals:
	/* a click on a chip or a decode line (switchTab false), a double-click or Enter (true) */
	void registerPicked(RegKey key, bool switchTab);
	/* the selection cleared here (clearSelection): the Registers tab has no current row either */
	void selectionCleared();
	/* a right-click: the menu for this register (NO_REGISTER: none under the mouse) of card (-1: none) */
	void menuRequested(RegKey key, int card, const QPoint &globalPos);

protected:
	void paintEvent(QPaintEvent *) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void resizeEvent(QResizeEvent *) override;
	void changeEvent(QEvent *e) override;
	bool viewportEvent(QEvent *e) override; /* tooltips, and the mouse leaving */

private:
	static constexpr int PAD = 14;          /* inside a card */
	static constexpr int CARD_GAP = 12;     /* between cards, and around them */
	static constexpr int LABEL_GAP = 2;     /* between a strip's bytes and its labels */
	static constexpr int STRIP_GAP = 8;     /* between the request's strip and the reply's label */
	static constexpr int GRID_GAP = 10;     /* between the reply's strip and the grid */
	static constexpr int DECODE_GAP = 18;   /* between the grid and the decode list */
	static constexpr int NAME_CHARS = 14;   /* a decode line's name, at most (elided past it) */
	static constexpr int VALUE_CHARS = 22;  /* and its value */
	static constexpr int TIP_GAP = 8;       /* a chip's tooltip ends this far before the decode list */
	static constexpr int PILL_PAD = 8;      /* the payload pill's room left and right of its words */
	/* a fast card's records: a channel's chip and its value close, the channels further apart, so each value reads
	 * with its own chip */
	static constexpr int RECORD_GAP = 8;
	static constexpr int GROUP_GAP = 28;

	/* one row of the grid: its base address and its top in the card (one line high) */
	struct Row {
		int base = 0;
		int top = 0;
	};
	/* a card laid out: its registers in address order with their colours, its rows, its lines */
	struct Card {
		int top = 0, height = 0;           /* in the content (scrolled) */
		QVector<int> regs;                 /* indices into defs_ */
		QVector<QColor> colours;           /* each one's colour; invalid: neutral (Colour: plotted) */
		QVector<Row> rows;
		QVector<int> lineTop;              /* each decode line's top in the card (the flow: level with its row, or
											* under the line before) */
		int titleTop = -1, requestTop = -1, requestStripTop = -1, replyTop = -1, replyStripTop = -1, noReplyTop = -1;
		int gridTop = -1, noteTop = -1;    /* the grid: its column offsets' line, then the rows */
		/* a fast stream's block: its header, the line under it, the records' head, rows and button */
		int headerLabelTop = -1, headerStripTop = -1, lossTop = -1, recordsHeadTop = -1, rowsTop = -1, showAllTop = -1;
		int recRows = 0;                   /* the record rows laid */
		bool folded = false;               /* the first three, a row "249 more records", the last */
		bool open = false;                 /* Show all: every record of the block */
		QVector<int> groupLeft, groupW;    /* each channel's column, from the card's left */
		QVector<QColor> channelColours;    /* invalid: neutral (Colour: plotted) */
	};

	void relayout();
	void assignColours(Card &card) const;
	int labelLineHeight() const;  /* a strip's label line */
	int columnLineHeight() const; /* the grid's column offsets */
	int headLineHeight() const;   /* the request's and the reply's label, a title */
	int stripHeight() const;
	int cellWidth() const;
	int gridLeft() const;
	int stripLeft(Field field) const; /* a strip's column, from the card's left */
	int contentWidth() const;
	int nameWidth() const;
	int valueWidth() const;
	QPoint origin() const; /* the content's top left in the viewport (the scroll) */
	int regAt(int card, int offset) const; /* the index into Card::regs of the register holding this byte; -1 none */
	/* a register of the map that card's block holds (a read's: the readable ones; a write's: any it wrote) */
	static bool holds(const DumpBlock &block, const RegDef &def);
	/* the grid's cell under pos: its card and its byte's offset in the block (before it: a cell of the row no read covers) */
	bool byteAt(const QPoint &pos, int &card, int &offset) const;
	bool fieldAt(const QPoint &pos, int &card, Side &side, Field &field) const;
	void setHovered(RegKey key);
	void setCrosshair(int card, int offset);
	void scrollToPending();
	void ensureVisible(int card, RegKey key);
	/* the bytes that changed since the last blocks of the same read start glowing */
	void noteChanges(const QVector<DumpBlock> &was);

	QByteArray frame(int card, Side side) const; /* the frame of that side as on the wire; empty: none */
	QString fieldBytesText(int card, Side side, Field field) const; /* as the strip writes them ("…" the payload) */
	QString fieldLabel(int card, Side side, Field field) const;     /* its name and meaning, as the strip writes them */
	QString requestSentence(const DumpBlock &block) const;
	QString requestLabel() const;
	QString replyLabel(const DumpBlock &block) const;
	QString replyState(int card, QColor &colour) const;
	QString noReplyText(int card) const;
	QString decodeValue(const RegDef &def, const QByteArray &raw) const;
	int payloadWidth(const QString &text) const;
	/* a head label's arrow and word apart: where each is drawn (the arrow's baseline moved to centre its ink) */
	struct HeadLabel {
		QString arrow, word;
		QPointF arrowAt, wordAt; /* baselines' left ends */
		QRectF arrowInk;
	};
	HeadLabel headLabel(const QString &label, int x, int y) const;
	/* a row's runs of cells no read covers: the first and last column of each */
	struct Run {
		int from = 0, to = 0;
		bool waiting = false; /* in a block with no reply yet; else before the block */
	};
	QVector<Run> unreadRuns(int card, int row) const;
	bool unreadRun(int card, int offset, int &row, Run &run) const;
	QString unreadWords(int card, int offset, int width) const;
	/* the text wrapped (at its words) to a tooltip's room: the tooltip of a chip never reaches the decode list */
	QString wrapTip(const QString &text, int width) const;
	QRect tipRoom(int card) const; /* where a chip's tooltip may go, across: from the card's padding to the list */

	void drawCard(QPainter &p, int card, const QPoint &at) const;
	void drawHead(QPainter &p, int x, int y, int width, const QString &label, const QString &sentence,
			const QColor &sentenceColour) const;
	void drawStrip(QPainter &p, int card, Side side, int x, int y) const;
	void drawStripe(QPainter &p, int card) const;
	void drawGrid(QPainter &p, int card, const QPoint &at) const;
	void drawDecode(QPainter &p, int card, const QPoint &at) const;

	/* fast stream cards */
	void layFastCard(int i, Card &card, int &at) const;
	const StreamDef *streamOf(int card) const;       /* the card's stream; null: not a fast card with a block */
	bool fastBlockShown(int card) const;             /* a fast card with a block (not the note of a stream switched on) */
	int recordIndex(int card, int row) const;        /* the block's record in that row; -1: none */
	int rowTop(int card, int row) const;             /* the row's top in the card */
	int recnoWidth() const;
	int headerLeft(HeaderField field) const;         /* a header column's left, from the card's left */
	QString headerBytesText(int card, HeaderField field) const;
	QString headerLabel(int card, HeaderField field) const;
	QString fastSentence(const DumpBlock &block) const;
	QString channelTip(int card, int row, int channel) const;
	static QString channelText(const StreamChannel &channel, double value); /* its decimals, else the scale's */
	static QString typeText(const StreamChannel &channel);                   /* "i16 × 0.0005 A" */
	static int channelOffset(const StreamDef &def, int channel);             /* its first byte in a record */
	void setHoveredFast(int card, int channel, int buttonCard);
	void drawFastCard(QPainter &p, int card, const QPoint &at) const;
	void drawHeaderStrip(QPainter &p, int card, const QPoint &at) const;
	void drawRecordsHead(QPainter &p, int card, const QPoint &at) const;
	void drawRecordRows(QPainter &p, int card, const QPoint &at) const;
	void drawShowAll(QPainter &p, int card) const;
	QString fastCopyText(CopyKind kind, int card) const;
	/* what the tooltip of a fast card's part stands for: a channel's chip and value in the row, its head, the number */
	QRect fastAnchor(int card, int row, int channel, FastPart part) const;

	QVector<DumpBlock> blocks_;
	QVector<Card> cards_;
	QVector<RegDef> defs_;
	QHash<RegKey, QColor> plotted_;
	ColourMode colourMode_ = AllRegisters;
	bool hold_ = false;
	QString heldAt_;
	bool connected_ = true;
	bool fields_ = true;
	std::function<double()> now_;
	qint64 staleAfterMs_ = 1000;
	RegKey selected_ = NO_REGISTER;
	int selectedCard_ = -1;           /* the card of the selection (a register in two cards: the one picked) */
	RegKey hovered_ = NO_REGISTER;
	int crossCard_ = -1, crossOffset_ = 0; /* the byte under the mouse: its column offset and row address lit */
	RegKey scrollTo_ = NO_REGISTER;   /* scrollToRegister's, waiting for its card */
	QString emptyText_;
	/* the glow: when each byte of a read last changed (ms on glowClock_), by the read (its kind, device, address) */
	QHash<quint32, QVector<qint64>> changedMs_;
	QElapsedTimer glowClock_;
	QElapsedTimer keyMenu_;      /* when the keys last opened the menu */
	QTimer *glowTimer_ = nullptr;
	int contentHeight_ = 0;
	int fieldW_[N_FIELDS] = {};  /* each strip column's width, the widest text it holds in any card */
	int headLabelW_ = 0;         /* the widest of "→ REQUEST", "← REPLY", "← AUTO_SEND": the sentences line up */
	int usedColumns_ = 16;       /* the grid's columns any card uses: the decode list starts after them */
	int nameChars_ = NAME_CHARS; /* the widest name of the tab, at most NAME_CHARS, plus two */
	int paints_ = 0;
	int glowRepaints_ = 0;
	int ticksAsked_ = 0;   /* tick() calls */
	int ticksPainted_ = 0; /* the ticks a paint has drawn (ticksAsked_ when it began) */
	int ticksMerged_ = 0;
	bool drawnDark_ = true;
	/* fast stream cards */
	QVector<StreamDef> streams_;
	QHash<int, QColor> fastColours_;  /* by ChartView::fastKey */
	QSet<QString> showAll_;           /* the streams whose cards show every record */
	int headerW_[N_HEADER] = {};      /* each header column's width, the widest in any fast card */
	int recnoW_ = 0;                  /* the records' number column, the widest number shown */
	int fastRight_ = 0;               /* the fast cards' widest content, from the content's left */
	int hoverFastCard_ = -1, hoverChannel_ = -1; /* the channel lit, in that card */
	int hoverButtonCard_ = -1;        /* the card whose Show all button is under the mouse */
};

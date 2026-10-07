/* SPDX-License-Identifier: Apache-2.0 */
/* Fast EVRe, the host's side (docs/MAP_FORMAT.md "streams", PROTOCOL.md "Fast EVRe"): a device sends its samples
 * in numbered blocks, each a READ_RESP nobody asked for at the first address of the stream's window.
 *
 *   the frame's data:  first (u32)  count (u16)  flags (u8)  spare (u8)  count records ...
 *
 * Here, without a window or a link, so the engine, evre, evre-sim and the tests share them:
 *
 *   fastBlock()    a block's header read and checked against its frame (the rules of the block)
 *   FastClock      a stream laid on the host's clock: the record's number and the rate, corrected slowly from
 *                  the blocks' arrival times (the earliest are the truth: a block can arrive late, never early)
 *   FastStream     one stream's running state: the 64-bit numbers, starts, losses and the counts
 *   FastSource     a stream as a device sends it, a wave per channel: for evre-sim and the fake devices
 *   FastSender     a device's streams on one connection, switched by their enable registers, with a host watchdog
 *   TriggerScan    the chart's trigger on a channel, looked for in each block as it comes (on the engine's thread)
 *
 * "Records" here, as in PROTOCOL.md; the window says "samples" (a record is one instant of all channels).
 */
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QVector>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "model/device_map.h"

class QObject;
class QTimer;

namespace fast {

constexpr int HEADER = 8;               /* the block's header: first, count, flags, spare */
constexpr uint8_t FLAG_START = 0x01;    /* the first block since the stream started */
constexpr uint8_t FLAG_LOST = 0x02;     /* records were dropped just before this block */
constexpr uint8_t KNOWN_FLAGS = FLAG_START | FLAG_LOST;

/* What a block's frame holds, read and checked. */
enum class BlockCheck {
	Ok,
	BadCount,   /* the frame's count is not 8 + count x record size (or shorter than the header): a bad block */
	NewerKind,  /* a flag bit nobody knows, or a spare that is not 0: a block of a newer kind, none of it used */
};

struct BlockHeader {
	uint32_t first = 0;
	uint16_t count = 0;
	uint8_t flags = 0;
	uint8_t spare = 0;
};

/* The header of the block in `data` (a frame's data bytes), checked against a record of recordSize bytes. Only Ok
 * gives records: `data` then holds exactly HEADER + count x recordSize bytes. */
BlockCheck fastBlock(const QByteArray &data, int recordSize, BlockHeader &header);

/* A stream on the host's clock (FAST_PLAN.md section 4). The time of record k is
 *   markTime + (k - markRecord) x period
 * from the newest time mark; a mark is made at the start and then about once a second, where the period is
 * corrected, so the time never steps: each mark lies on the line before it. The correction aims at the earliest
 * arrivals of each second (a block arrives late, never early): their trend gives the rate, the distance to them
 * the phase, pulled in over PULL_S seconds. The rate moves by at most MAX_SLEW_PPM a second, or more while the
 * clock is more than FAR_S off (a device whose clock is far from its map's rate). */
class FastClock {
public:
	static constexpr double WINDOW_S = 1.0;     /* the earliest arrival of each such span is taken */
	static constexpr double PULL_S = 10.0;      /* a phase error is pulled in over this time */
	static constexpr double MAX_SLEW_PPM = 10.0;/* the rate's change a second, at most, near the truth */
	static constexpr double FAR_S = 0.010;      /* further off than this, the change may be faster */
	static constexpr int HISTORY = 600;         /* the spans the trend is fitted over: ten minutes */

	struct Mark {
		quint64 record = 0;
		double time = 0;
	};

	void reset(double nominalRate);             /* a new start: the next block sets the first mark */
	bool started() const { return started_; }
	/* the records up to `end` (the number after the block's last) arrived at `arrival` seconds; true when a new
	 * time mark was made (the first one, then about one a second) */
	bool arrived(quint64 end, double arrival);
	double timeOf(quint64 record) const { return mark_.time + double(qint64(record - mark_.record)) * period_; }
	const Mark &mark() const { return mark_; }
	double period() const { return period_; }
	double rate() const { return period_ > 0 ? 1.0 / period_ : 0; } /* records a second, as fitted */
	double nominalRate() const { return nominal_; }
	double ppm() const { return nominal_ > 0 && period_ > 0 ? (1.0 / (period_ * nominal_) - 1.0) * 1e6 : 0; }

private:
	void closeWindow(quint64 end, double span);

	double nominal_ = 0;
	bool started_ = false;
	Mark mark_;
	double period_ = 0;
	double correction_ = 0;     /* the period's fraction above nominal: period = (1 + correction) / nominal */
	Mark origin_;               /* the first mark: the line the trend is measured against */
	double windowStart_ = 0;
	double windowMin_ = 0;      /* the earliest arrival of this span, against the current line */
	double windowMinOrigin_ = 0;/* the same arrival against the origin's nominal line */
	double windowMinAt_ = 0;    /* when it came */
	int windowBlocks_ = 0;
	QVector<double> trendAt_, trendResidual_;
};

/* What one block brought, after the rules. */
struct BlockTaken {
	quint64 first = 0;      /* its first record's number, in 64 bits, from the stream's (or this host's) start */
	int count = 0;
	quint64 lost = 0;       /* records missing between the block before and this one */
	bool newStart = false;  /* START, the first block seen, or a number that went back: a new pair of clock numbers */
	bool newMark = false;   /* the clock made a time mark (the first, or about one a second) */
};

/* One stream's running state: every block in order through take(). */
class FastStream {
public:
	/* switched (on): nothing seen yet. rate: the records a second the device was set to (its rate_reg); 0: the map's */
	void reset(const StreamDef &def, double rate = 0);
	const StreamDef &def() const { return def_; }
	/* one block's frame data, arrived at `arrival` seconds: Ok fills `taken`; anything else is counted, and none of
	 * its records is used */
	BlockCheck take(const QByteArray &data, double arrival, BlockTaken &taken);

	const FastClock &clock() const { return clock_; }
	quint64 blocks = 0;        /* good blocks */
	quint64 records = 0;       /* records in them */
	quint64 lost = 0;          /* records the numbers say are missing */
	quint64 badBlocks = 0;     /* a count that does not fit */
	quint64 newerBlocks = 0;   /* blocks of a newer kind */
	quint64 starts = 0;        /* START blocks and restarts seen */

private:
	StreamDef def_;
	int recordSize_ = 0;
	double nominal_ = 0;
	bool seen_ = false;        /* a good block came since reset */
	quint64 next_ = 0;         /* the number the next block should start at, in 64 bits */
	FastClock clock_;
};

/* The chart's trigger on one channel of a stream (ui/chart_widget.h), looked for on the engine's thread as each block
 * comes: the crossing is found in the block that holds it, at its record, not by the window a frame later. The window
 * says what to watch (TriggerWatch, handed to the engine when it changes); each block's crossings go to the window
 * with the block (IoEngine::FastBlock), which takes their times from its own store of the records. */
struct TriggerWatch {
	bool on = false;
	int channel = 0;
	double level = 0;       /* in the channel's shown value (scale and offset applied) */
	int edge = 0;           /* 0 rising (from below the level to it or above), 1 falling, 2 either */
	/* crossings after this time count (the arm), on the stream's clock: the window's store shifts a start that would
	 * begin before the one before ended (fast::Store), so the window takes that shift off */
	double from = 0;
	/* after a crossing at t, the next counts after t + rearm (the hold-off, the view's fill); < 0: none (Single: the
	 * window arms it again) */
	double rearm = -1;
	quint64 serial = 0;     /* the window's arm: a crossing found for an older one is not used */
};
struct Crossing {
	int record = 0;         /* the block's record at or past the level; 0: the record before it ended the block before */
	double fraction = 0;    /* where the level lies between the record before and this one, 0 to 1 */
	double time = 0;        /* on the stream's clock */
	quint64 serial = 0;     /* the watch's */
};
class TriggerScan {
public:
	void set(const TriggerWatch &watch) { watch_ = watch; }
	const TriggerWatch &watch() const { return watch_; }
	/* one block's records after FastStream::take, the crossings in it (the times from the clock's mark and period):
	 * each pair of records in one segment, the block's first with the last of the block before only when nothing was
	 * lost between them and the stream did not start again */
	void scan(const StreamDef &def, const BlockTaken &taken, const char *records, const FastClock::Mark &mark,
			double period, QVector<Crossing> &out);
	/* the record before the next block scanned, as it came (empty: none to pair with): the blocks still waiting for
	 * the window are scanned again for a new watch, the first of them paired with the record the window has before it */
	void pairWith(const StreamDef &def, const QByteArray &record);
	/* a crossing that went with a block the window never took (the engine's queue overflowed): the window did not
	 * hold on it, so the scan is armed again from it (Single had stopped there; Normal and Auto waited its re-arm) */
	void dropped(const Crossing &crossing);

private:
	TriggerWatch watch_;
	bool hasLast_ = false;
	int lastChannel_ = -1;
	double last_ = 0;       /* the channel's value in the last record of the block before */
	double counted_ = NAN;  /* the time of the last crossing counted */
};

/* A stream as a device sends it (evre-sim, the fake devices): blocks when full or BLOCK_AGE_MS old, a wave per
 * channel, and the test aids. Each record's value is a function of its number, so a reader can check it. */
class FastSource {
public:
	static constexpr int BLOCK_AGE_MS = 10;
	struct Options {
		int loseEvery = 0;     /* --fast-lose N: every N-th block is not sent (its records keep their numbers) */
		double ppm = 0;        /* --fast-ppm P: the sample clock runs P parts in a million fast (negative: slow) */
		quint32 first = 0;     /* --fast-first K: the first block starts at record K, with START */
		double rate = 0;       /* --fast-rate R: R records a second; 0: the map's */
	};
	FastSource(const StreamDef &def, uint8_t slave) : def_(def), slave_(slave) {}
	void start(const Options &options, qint64 nowNs); /* on: record `first` next, START */
	void stop() { running_ = false; }
	bool running() const { return running_; }
	double rate() const;       /* records a second as built (the map's or --fast-rate), before --fast-ppm */
	/* the frames due by nowNs: every full block, and the rest when its oldest record is BLOCK_AGE_MS old. A device
	 * far behind (a stall over a second) drops what it could not send, and the next block says LOST. */
	QByteArray due(qint64 nowNs);
	/* channel c's raw value in record k, as the source builds it */
	static double wave(const StreamDef &def, int channel, quint64 record, double rate);

private:
	QByteArray block(int n);

	StreamDef def_;
	uint8_t slave_;
	Options options_;
	bool running_ = false;
	qint64 startNs_ = 0;
	quint64 made_ = 0;         /* records made since the start (sent or dropped) */
	quint32 next_ = 0;         /* the device's number of the next record */
	quint64 blockNo_ = 0;
	uint8_t pending_ = 0;
};

/* A device's streams on one connection (evre-sim, the fake devices): each switched on and off by its enable
 * register, sent from a 1 ms timer, and stopped when the host is silent for WATCHDOG_MS (the device's host
 * watchdog: a host that streams reads CONFIG every 100 ms) or the connection closes. The timer is a child of
 * `owner` (the connection): it goes with it. */
class FastSender {
public:
	static constexpr int WATCHDOG_MS = 2000;
	using Send = std::function<void(const QByteArray &)>;
	using Stopped = std::function<void(int stream)>; /* stopped by the watchdog: the device clears its enable */
	FastSender(QObject *owner, const QVector<StreamDef> &streams, uint8_t slave, const FastSource::Options &options,
			Send send, Stopped stopped);
	/* a request from the host: the watchdog sees it */
	void heard() { sinceHeard_.restart(); }
	/* the enable register of stream i was written: on (not 0) starts it when it is off, 0 stops it */
	void enable(int stream, bool on);
	bool running(int stream) const { return sources_[size_t(stream)].running(); }

private:
	void tick();

	std::vector<FastSource> sources_;
	FastSource::Options options_;
	Send send_;
	Stopped stopped_;
	QTimer *timer_;
	QElapsedTimer clock_, sinceHeard_;
};

/* a channel's raw bytes in a record -> its shown value (scale and offset applied) */
double channelValue(const StreamChannel &channel, const char *bytes);

} // namespace fast

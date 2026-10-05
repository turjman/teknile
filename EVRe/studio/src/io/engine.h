/* SPDX-License-Identifier: Apache-2.0 */
/* The I/O engine: everything that talks to the device, on its own thread -
 * the link, the master, the poller, CSV recording and the API server - so
 * nothing the window draws can delay a poll or a CSV row.
 *
 * One device, or several on the link (a bus, model/bus_file.h): the table
 * then holds every device's registers, each with its device's slave
 * (RegDef::slave), and a poll reads them all, never one read across two
 * devices. A device of a bus that misses OFFLINE_AFTER_TIMEOUTS answers in a
 * row is offline: left out of the polls (its values go stale) and asked for
 * its DEVICE_ID until it answers again - every OFFLINE_RETRY_MS one offline
 * device, each in turn, so the others keep their rate. On a serial link a bus
 * gets In flight 1 whatever the user set: on RS-485 only one may talk at a
 * time, and requests to two devices must never overlap. With one device none
 * of this applies: it is polled as always.
 *
 * The window calls it only through post() (queued into the engine thread) and
 * reads it through table().snapshot() / stats() / takeSamples() / takeFrames()
 * (copies under a lock). Results come back as signals, queued into the
 * window's thread.
 *
 * AUTO_SEND (one device only, setAutoSend): the device sends its read-only
 * block (0xD000 on) by itself, as READ_RESP frames nobody asked for. Each
 * frame fills the table and is a sample tick (chart points, a CSV row); the
 * polls read only the registers the frames do not cover. A read of CONFIG
 * every HEARTBEAT_MS keeps the device's host watchdog fed whatever the polling
 * is, and tells when the device stopped sending (a reset).
 *
 * Fast EVRe (one device only, setFastStream): the map's "streams" are sample streams the device sends in numbered
 * blocks, READ_RESP frames nobody asked for at each stream's window (io/fast_stream.h). A block is recognised by its
 * slave and its window before the AUTO_SEND test, checked by the block's rules and counted; the clock's fit lays
 * it on now()'s time. A stream is switched by its map's enable register (1 on, 0 off, WRITE_ACK; its rate_reg read
 * first); one without an enable is only listened to. While one is on, CONFIG is read every HEARTBEAT_MS as for
 * AUTO_SEND. Disconnect switches every stream off first, the way it clears AUTO_SEND; a lost link keeps the wish,
 * and the stream is switched on again after the reconnect. No block FIRST_FRAME_MS after the enable was taken: off
 * again, and said. (5.1: the window shows the counts; the chart takes the blocks from 5.2.)
 *
 * Every QObject the engine uses is its child (or is made in its thread), so
 * moveToThread() takes them all along: a timer left in the window's thread
 * could not be started from the engine's.
 */
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "evre/master.h"
#include "evre/registers.h"
#include "io/fast_stream.h"
#include "io/reg_table.h"

class ApiServer;
class QFile;
namespace fast {
class RecordingWriter;
}
class QTimer;

/* The poll ticker woke at or after `deadline`: the periods due (that one and every later one already past, which a
 * timer coarser than the period passes; counted, not dropped), and `deadline` moved to the last of them. Far behind
 * (a long stall) it starts again from now. Public for the tests. */
int tickPeriodsDue(std::chrono::steady_clock::time_point &deadline, std::chrono::microseconds period);

/* A device on the link, as the engine needs it. */
struct EngineDevice {
	QString name;
	uint8_t slave = 0;       /* its address; 0: the one device of a map, at the link's slave */
	uint16_t loginAddr = 0;  /* its map's login register, where the token is written after connecting; 0: none */
	int loginSize = 0;
	bool poll = true;        /* false: read and written on request only */
	QString token;           /* its own token; empty: the one given to connectTcp */
	std::shared_ptr<const DeviceMap> map; /* its map: what a broadcast would mean to it (model/bus_file.h) */
};

class IoEngine : public QObject {
	Q_OBJECT
public:
	/* polls under way at once, at most, whatever In flight allows: a poll is `blocks` reads, so In flight past
	 * this x blocks gives nothing more (the sidebar's hint never asks for more) */
	static constexpr int MAX_POLLS_UNDER_WAY = 8;
	/* a device of a bus that misses this many answers in a row is offline, and asked again this often */
	static constexpr int OFFLINE_AFTER_TIMEOUTS = 3;
	static constexpr int OFFLINE_RETRY_MS = 2000;
	/* AUTO_SEND: the CONFIG read that feeds the device's host watchdog (2 s) while the polls may be off, and the
	 * share of a serial link the frames may take. The protocol's own constants are in evre/registers.h; these two
	 * names are kept for the window's code. */
	static constexpr uint16_t STATUS_CAP_AUTO_SEND = evre::CAP_AUTO_SEND;
	static constexpr int AUTO_SEND_BASE_HZ = evre::AUTO_SEND_BASE_HZ;
	static constexpr int HEARTBEAT_MS = 100;
	static constexpr int FIRST_FRAME_MS = 2000;  /* AUTO_SEND on and no frame by then: off again, polled */
	static constexpr double AUTO_SEND_LINK_SHARE = 0.7;
	/* the prescaler + 1 of every rate the device makes exactly (8000 Hz divided evenly), fastest first */
	static const QVector<int> &autoSendDividers();
	/* the prescaler to use on a serial link at baud (0: not serial) for frames of streamBytes data bytes: the one
	 * asked for, or the fastest of autoSendDividers() whose frames take at most AUTO_SEND_LINK_SHARE of the link
	 * (the slowest when none does). Public for the tests. */
	static int autoSendPrescalerFor(int prescaler, qint32 baud, int streamBytes);

	explicit IoEngine(QObject *parent = nullptr);
	~IoEngine() override;

	/* run f in the engine thread */
	void post(std::function<void()> f);

	/* from any thread */
	RegTable &table() { return table_; }
	double now() const { return double(clock_.nsecsElapsed()) / 1e9; } /* seconds: the chart and CSV time base */

	/* for the window's status line and sidebar, refreshed every 250 ms */
	struct Stats {
		evre::Master::Stats master;
		double pollHz = 0;
		int blocks = 0, pollable = 0;  /* the reads a poll is made of, and the registers they cover */
		bool recording = false;
		quint64 csvRows = 0;
		int csvCols = 0;
		QString csvFile;
		bool apiRunning = false;
		quint16 evrePort = 0, jsonPort = 0;
		int apiClients = 0;
		quint64 apiRequests = 0;
		bool autoSend = false;   /* the device sends by itself: pollHz is then the polls of the rest */
		double autoSendHz = 0;   /* its frames a second, measured as pollHz is */
		/* Fast EVRe: each stream of the map, in its order (none on a bus) */
		struct Fast {
			QString name;
			bool wanted = false;  /* asked for: switched on at every connect */
			bool on = false;      /* switched on in the device (or listened to): its blocks are taken */
			double rate = 0;      /* records a second as the clock's fit has it; 0 before the first block */
			double ppm = 0;       /* the fit's correction against the rate the device was set to */
			double recordsHz = 0; /* records that came a second, measured as pollHz is */
			quint64 records = 0, blocks = 0, lost = 0, badBlocks = 0, newerBlocks = 0, starts = 0;
			quint64 notShown = 0; /* records the window did not take in time (FAST_QUEUE_BYTES) */
		};
		QVector<Fast> fast;
	};
	Stats stats() const;
	/* the samples of the plotted registers since the last call, every poll, by register key */
	QHash<RegKey, QVector<QPointF>> takeSamples();
	/* Fast EVRe: the blocks taken since the last call, for the chart (the window takes them once a frame). Capped at
	 * FAST_QUEUE_BYTES: past it the oldest go, counted in Stats::Fast::notShown (a window that stalls loses them on
	 * the chart only). */
	static constexpr qint64 FAST_QUEUE_BYTES = 64ll * 1024 * 1024;
	struct FastBlock {
		int stream = 0;
		quint64 first = 0;       /* the number of its first record in its start (64 bits) */
		int count = 0;
		bool newStart = false;   /* a new start of the stream: a new epoch of times */
		quint64 lost = 0;        /* records lost before it */
		QByteArray records;      /* count records, as they came */
		bool marked = false;     /* a time mark came with it: */
		quint64 markRecord = 0;
		double markTime = 0, markPeriod = 0;
	};
	QVector<FastBlock> takeFastBlocks();
	/* the monitor's lines since the last call (when monitoring), and how many
	 * were dropped: at thousands of frames a second only the newest are kept */
	QStringList takeFrames(int &dropped);

	/* in the engine thread (through post) */
	/* generation: the window's number for this map. devices: the one device of a map (slave 0), or the devices
	 * of a bus, each with its slave; defs then name the device of each register (RegDef::slave). */
	void setMap(const QVector<RegDef> &defs, quint64 generation, const QString &deviceName,
			const QVector<EngineDevice> &devices);
	void connectTcp(const QString &host, quint16 port, const QString &token); /* token: empty = none */
	void connectSerial(const QString &port, qint32 baud);
	void disconnectLink();
	/* inFlight as the user set it; a bus on a serial link (RS-485: one transmitter at a time) always gets 1 */
	void setLinkOptions(int slave, int timeoutMs, int inFlight);
	void setPolling(bool on, double intervalMs); /* 0 = as fast as possible: the next poll when one ends */
	/* AUTO_SEND on or off at 8000 / (prescaler + 1) Hz, on the one device (never on a bus). Kept: switched on
	 * again after a reconnect, once the device's STATUS says it can. Answered by autoSendSet. */
	void setAutoSend(bool on, int prescaler);
	/* Fast EVRe: the map's stream (its index in DeviceMap::streams) on or off, on the one device (never on a bus).
	 * Kept: switched on again after a reconnect. Answered by fastStreamSet. */
	void setFastStream(int stream, bool on);
	void setPlotted(const QVector<RegKey> &keys);
	void startRecord(const QString &file, const QVector<RegKey> &cols);
	void stopRecord();
	/* slave: where the request goes, 1 to 255; evre::BROADCAST (0) for a broadcast WRITE (no acknowledge) */
	void write(quint64 id, uint8_t slave, uint16_t addr, const QByteArray &bytes, bool ack);
	void read(quint64 id, uint8_t slave, uint16_t addr, uint16_t count, bool intoTable);
	void setMonitor(bool on) { monitorOn_ = on; }
	void apiStart(quint16 evrePort, quint16 jsonPort, bool network);
	void apiStop();
	void apiSetWrites(bool writes, bool danger);
	/* why a broadcast WRITE of count bytes at addr must not go out, for the devices of the map or bus (in the
	 * engine thread; model/bus_file.h); empty: it may */
	QString broadcastRefusal(uint16_t addr, const QByteArray &bytes) const;
	void shutdown(); /* before the thread ends */

signals:
	/* `device` below: the device's slave as the table has it (RegDef::slave): 0 for the one device of a map */
	void opened(const QString &link);
	void closed(const QString &why);
	void deviceInfo(int device, bool ok, quint16 id, quint16 status, const QString &err);
	void loginRefused(int device, const QString &err); /* the device refused the token written to its login register */
	void loginSkipped();                   /* a token was given, but no map declares a login register */
	void deviceOnline(int device, bool online); /* a device of a bus stopped answering, or answers again */
	void writeDone(quint64 id, bool ok, const QString &msg);
	void readDone(quint64 id, bool ok, const QByteArray &data, const QString &msg, double latencyMs);
	void recordStarted(bool ok, const QString &err);
	void recordStopped(const QString &file, quint64 rows);
	void apiStarted(bool ok, const QString &err);
	/* AUTO_SEND switched on (at hz) or off; err not empty: it was not, and is no longer wanted */
	void autoSendSet(bool on, int hz, const QString &err);
	void autoSendSlowed(const QString &why); /* a serial link: a slower rate than asked, and why */
	void autoSendStopped();                  /* CONFIG shows AUTO_SEND cleared (the device reset?): not wanted any more */
	/* Fast EVRe: a stream switched on (the device took its enable; rate: the records a second it was set to) or off;
	 * err not empty: it was not, and is no longer wanted */
	void fastStreamSet(int stream, bool on, double rate, const QString &err);
	/* something to say of a stream: blocks of a newer kind (once), the device stopped it (no longer wanted) */
	void fastStreamNote(int stream, const QString &text, bool stopped);
	/* a stream's recording beside the CSV, closed: what it holds (the Log, as information) */
	void fastRecorded(const QString &text);

private:
	/* one read of a poll: registers of one device close together, read at once */
	struct Block {
		uint8_t slave = 0;     /* the device's, as the table has it (RegDef::slave) */
		uint16_t addr = 0;
		uint16_t size = 0;     /* bytes */
		QVector<int> rows;     /* the table rows it covers */
		bool single = false;   /* one register, split off a block the device refused */
	};

	/* the link */
	void attach(evre::Link *link, const QString &token);
	void onOpened();
	void onClosed(const QString &why);
	void sendLogin();
	void readDeviceInfo();
	uint8_t target(uint8_t tableSlave) const; /* where a request for a register of this device goes */
	uint8_t tableSlave(uint8_t slave) const;  /* the table's slave of the device at this address */
	bool isBus() const { return devices_.size() > 1 || (!devices_.isEmpty() && devices_[0].slave != 0); }
	void applyInFlight();                     /* the user's In flight, or 1 for a bus on a serial link */
	/* what a request to a device of a bus tells of it: an answer (ok or an error code), a miss (a timeout), or
	 * nothing (cancelled, the link gone) */
	void heardFrom(uint8_t tableSlave, const evre::Result &r);
	/* every OFFLINE_RETRY_MS: the DEVICE_ID of one offline device, each in turn, one request under way at most */
	void retryOffline();

	/* polling */
	void rebuildBlocks();
	void resumePolling(); /* the ticker, or the first of the back-to-back polls */
	void pollTick();
	/* polls allowed to overlap: the next is sent while the answers to the last
	 * are still coming (they come in order: they complete in order). From the
	 * link's In flight over the blocks a poll needs, 1 to 8. */
	int maxPollsUnderWay() const;
	void startPoll();
	void onBlockResult(quint64 layout, quint64 pollId, const Block &block, const evre::Result &r);
	void storeBlock(const Block &block, const QByteArray &data);
	void onBlockRefused(const Block &block, const evre::Result &r);
	void splitBlock(const Block &block);
	void pollDone();
	void appendSamples(double t);
	void writeCsvRow(double t);
	void readBack(uint8_t tableSlave, uint16_t addr, int writtenSize);

	/* AUTO_SEND */
	/* the state wanted to the device at slave: CONFIG read, then written */
	void applyAutoSend(uint8_t slave);
	void autoSendFailed(bool on, const QString &why);
	void stopStream();                            /* not streaming: every register polled again */
	void onUnsolicited(const evre::Frame &frame); /* a frame of the stream, or something else to ignore */
	void heartbeat();
	void noFrames(); /* FIRST_FRAME_MS after AUTO_SEND was taken, and no frame came */
	int streamBytes() const;                      /* the data bytes of a frame: the last one's, else the map's block */
	QByteArray configBytes(bool on, int prescaler) const;

	/* Fast EVRe */
	struct FastRun {
		StreamDef def;
		const RegDef *enable() const { return enableReg.name.isEmpty() ? nullptr : &enableReg; }
		const RegDef *rate() const { return rateReg.name.isEmpty() ? nullptr : &rateReg; }
		RegDef enableReg, rateReg;  /* the map's registers it names; no name: none */
		fast::FastStream state;
		bool wanted = false;
		bool on = false;            /* its blocks are taken (set before the enable's answer: the first may come first) */
		bool deviceMaySend = false; /* the enable written 1, and no 0 acknowledged since: Disconnect sends the 0 */
		quint64 request = 0;        /* +1 at every switch: answers to an older one are ignored */
		qint64 takenMs = -1;        /* when the device took the enable (clock_); -1: not yet */
		qint64 lastBlockMs = -1;    /* when the last good block came */
		bool newerSaid = false;     /* blocks of a newer kind: said once */
		bool silenceAsked = false;  /* no block for a while: its enable read once */
		quint64 recordsSinceRate = 0;
		double recordsHz = 0;
		quint64 notShown = 0;
		/* while the CSV records: its blocks as they came beside it (run.csv -> run.ADC.evrs), opened at the first */
		std::shared_ptr<fast::RecordingWriter> writer;
		bool writeFailed = false;
		qint64 recordedBlocks = 0;
	};
	void recordBlock(int stream, const fast::BlockTaken &taken, const QByteArray &data);
	void applyFast(int stream);                    /* the wanted state to the device: rate_reg read, enable written */
	void fastFailed(int stream, bool on, const QString &why);
	bool anyFastOn() const;
	int fastStreamOf(const evre::Frame &frame) const; /* the stream whose block this is; -1: none */
	int fastStreamOfRaw(const QByteArray &raw) const; /* the same from a frame's bytes (the Monitor) */
	void takeBlock(int stream, const evre::Frame &frame);
	void watchFast();                              /* every 500 ms: the first block, and a stream gone silent */
	void sendFastOffs();                           /* Disconnect: each stream that may send told 0, straight */

	/* the poll clock: a thread that sleeps to each deadline and wakes the
	 * engine. Qt's timers tick at ~15.6 ms on Windows outside the GUI thread;
	 * this one keeps a 1-5 ms interval on Windows and Linux alike. */
	void startTicker();
	void stopTicker();
	void runTicker(); /* the ticker thread's loop */
	void onTick(int ticks); /* in the ticker thread: this many more polls due */

	void monitorLine(const char *direction, const QByteArray &raw, const QString &note);
	void updateStats();

	/* the device side (children of the engine) */
	RegTable table_;
	evre::Master *master_;
	std::unique_ptr<evre::Link> link_;
	ApiServer *api_;
	QTimer *statsTimer_;
	bool connected_ = false;
	QString deviceName_;
	QHash<RegKey, int> rowOfKey_;

	/* the devices, the token from the window (the login registers are theirs), and which are offline */
	QVector<EngineDevice> devices_;
	QString loginToken_;
	QHash<uint8_t, int> missedAnswers_;          /* per device of a bus (table slave): timeouts in a row */
	QSet<uint8_t> offline_;                      /* devices of a bus left out of the polls */
	QTimer *offlineTimer_;
	bool retryPending_ = false;                  /* one offline device's DEVICE_ID asked, no answer yet */
	uint8_t lastRetried_ = 0;                    /* the offline device asked last: the next one is asked next */
	int inFlightWanted_ = 1;                     /* the user's In flight (setLinkOptions) */

	/* the ticker */
	std::thread ticker_;
	std::mutex tickerMutex_;
	std::condition_variable tickerWake_;
	bool tickerRunning_ = false;                 /* guarded by tickerMutex_ */
	void *tickerStopEvent_ = nullptr;            /* Windows: the event that ends the ticker's wait */
	std::atomic<qint64> tickIntervalUs_{ 100000 };
	std::atomic<bool> tickQueued_{ false };      /* one wake-up in the engine's queue at most */
	std::atomic<int> ticksDue_{ 0 };             /* ticks fallen due, not yet started as polls */

	/* polling */
	bool pollingOn_ = true;
	bool continuous_ = false;                    /* interval 0: the next poll as soon as one ends */
	int pollsBehind_ = 0;                        /* due polls waiting for a free slot (at most 20 ms of them) */
	QVector<Block> blocks_;
	quint64 layoutGeneration_ = 0;               /* +1 on every new block layout: older answers are ignored */
	quint64 nextPollId_ = 0;
	QHash<quint64, int> pollsUnderWay_;          /* poll id -> blocks still to answer */
	int pollsSinceRate_ = 0;                     /* polls done since the rate was last measured */
	QElapsedTimer clock_, rateClock_;
	QVector<RegKey> plottedKeys_;
	bool monitorOn_ = false;

	/* AUTO_SEND */
	qint32 serialBaud_ = 0;                      /* the link's baud rate; 0: not a serial link */
	bool autoSendWanted_ = false;                /* asked for: switched on at every connect the device allows */
	bool autoSendOn_ = false;                    /* switched on in the device: the frames are taken */
	bool statusKnown_ = false, canAutoSend_ = false; /* the device's STATUS, read at connect */
	int autoSendPrescaler_ = 79;                 /* as asked (100 Hz) */
	int autoSendUsed_ = 79;                      /* as written: slower on a busy serial link */
	/* AUTO_SEND written on, and no off acknowledged since: the device may be sending. Unlike autoSendOn_ (cleared at
	 * once when it is switched off, the frames ignored from then), it stays set until the device took the off, so a
	 * Disconnect in between still sends it */
	bool deviceMaySend_ = false;
	uint8_t autoSendSlave_ = 0;                  /* the device it was switched on in */
	uint16_t configKept_ = 0;                    /* CONFIG's MSG_ENABLE as read: written back unchanged */
	quint64 autoSendRequest_ = 0;                /* +1 at every switch: answers to an older one are ignored */
	uint16_t streamAddr_ = 0, streamCount_ = 0;  /* what the last frame covered; count 0: no frame yet */
	QVector<int> streamRows_;                    /* the table rows inside it */
	int framesSinceRate_ = 0;
	QTimer *heartbeatTimer_;
	QTimer *firstFrameTimer_;
	bool heartbeatPending_ = false;              /* one CONFIG read under way at most */

	/* Fast EVRe: the map's streams (none on a bus) */
	QVector<FastRun> fastRuns_;
	QTimer *fastWatch_;

	/* CSV recording */
	QFile *csvFile_;
	QTextStream csvStream_;
	QVector<RegKey> csvColumns_;                 /* by register key */
	quint64 csvRows_ = 0;

	/* what the other threads take, under crossThreadMutex_ */
	mutable QMutex crossThreadMutex_;
	Stats stats_;
	QHash<RegKey, QVector<QPointF>> samples_;
	QStringList monitorLines_;
	int monitorLinesDropped_ = 0;
	QVector<FastBlock> fastBlocks_;
	qint64 fastQueued_ = 0;                      /* the bytes in fastBlocks_ */
};

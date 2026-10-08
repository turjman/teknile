/* SPDX-License-Identifier: Apache-2.0 */
/* The I/O engine (see engine.h): the connect sequence, the poll clock, the
 * block reads a poll is made of, samples for the chart, CSV rows and the
 * monitor's lines. Everything here runs in the engine thread, except the
 * ticker's loop and the take*() copies. */
#include "io/engine.h"

#include <QDateTime>
#include <QFile>
#include <QTime>
#include <QTimer>
#include <algorithm>
#include <chrono>

#include "api/api_server.h"
#include "model/bus_file.h"
#include "model/fast_recording.h"
#include "evre/frame.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace {

constexpr qint64 MAX_BACKLOG_US = 20000; /* due polls kept waiting for a free slot: 20 ms of them */
constexpr int MAX_TICKS_AT_ONCE = 1 << 16; /* periods counted at one wake-up of the ticker, at most */
constexpr int MAX_SAMPLES = 100000;      /* per plotted register, if the window does not take them */
constexpr int MAX_MONITOR_LINES = 1500;  /* monitor lines kept between two frames of the window */

using Clock = std::chrono::steady_clock;

/* polled: a register the device has not refused, and no long byte array */
bool pollable(const RegValue &v) { return !v.unavailable && isPollable(v.def); }

/* EVRe refusals a retry will not change: permission denied (3), offset out
 * of range (4), count out of range (5) */
bool refusedForGood(uint8_t error) { return error == 3 || error == 4 || error == 5; }

/* a CSV column title: "NAME [unit]", a comma in it made a semicolon */
QString csvTitle(const RegDef &d) {
	QString title = d.name + (d.unit.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(d.unit));
	return title.replace(QLatin1Char(','), QLatin1Char(';'));
}

/* a CSV cell: the number as shown (scale and offset applied), or the bytes in hex */
QString csvCell(const RegValue &v) {
	return v.def.isNumeric() ? QString::number(decodeNumber(v.def, v.raw), 'g', 9) : QString::fromLatin1(v.raw.toHex());
}

} // namespace

/* The ticker woke at or after `deadline`: how many periods are due, that one and every later one already past, and
 * `deadline` moved to the last of them. A timer coarser than the period (Windows wakes some 0.3 ms late) passes
 * several at each wake-up; they are counted, not dropped: dropped, 0.25 ms gave 1750 polls/s, not 4000. pollTick
 * keeps no more of them waiting than MAX_BACKLOG_US (a stall is not made up in a burst); far behind (a long
 * stall) the count starts again from now. */
int tickPeriodsDue(std::chrono::steady_clock::time_point &deadline, std::chrono::microseconds period) {
	const auto now = std::chrono::steady_clock::now();
	if (now - deadline < period) return 1;
	const qint64 extra = (now - deadline) / period;
	if (extra > MAX_TICKS_AT_ONCE) {
		deadline = now;
		return MAX_TICKS_AT_ONCE;
	}
	deadline += extra * period;
	return 1 + int(extra);
}

IoEngine::IoEngine(QObject *parent) : QObject(parent) {
	master_ = new evre::Master(this);
	api_ = new ApiServer(master_, &table_, this);
	api_->isConnected = [this] { return connected_; };
	api_->linkName = [this] { return link_ ? link_->describe() : QString(); };
	api_->deviceName = [this] { return deviceName_; };
	api_->broadcastRefusal = [this](uint16_t addr, const QByteArray &bytes) { return broadcastRefusal(addr, bytes); };
	/* the streams for the API: asked on this thread, so the runs are read where they are written */
	api_->fastStreams = [this] {
		QVector<ApiServer::FastState> states;
		for (const FastRun &run : std::as_const(fastRuns_)) {
			ApiServer::FastState state;
			state.def = run.def;
			state.enable = run.enableReg;
			state.on = run.on;
			const fast::FastClock &clock = run.state.clock();
			state.rate = run.on && clock.started() && clock.nominalRate() > 0 ? clock.nominalRate() : run.def.rate;
			state.hasRecord = run.on && run.state.records > 0 && !run.lastRecord.isEmpty();
			state.newest = run.lastRecord;
			state.newestTime = double(clockEpochMs_) / 1000.0 + run.lastRecordTime;
			states.push_back(state);
		}
		return states;
	};
	statsTimer_ = new QTimer(this);
	statsTimer_->setInterval(250);
	connect(statsTimer_, &QTimer::timeout, this, &IoEngine::updateStats);
	offlineTimer_ = new QTimer(this);
	offlineTimer_->setInterval(OFFLINE_RETRY_MS);
	connect(offlineTimer_, &QTimer::timeout, this, &IoEngine::retryOffline);
	csvFile_ = new QFile(this);
	/* AUTO_SEND's CONFIG read: precise, so the device's watchdog sees one every 100 ms, not every 100 to 115 */
	heartbeatTimer_ = new QTimer(this);
	heartbeatTimer_->setTimerType(Qt::PreciseTimer);
	heartbeatTimer_->setInterval(HEARTBEAT_MS);
	connect(heartbeatTimer_, &QTimer::timeout, this, &IoEngine::heartbeat);
	firstFrameTimer_ = new QTimer(this);
	firstFrameTimer_->setSingleShot(true);
	firstFrameTimer_->setInterval(FIRST_FRAME_MS);
	connect(firstFrameTimer_, &QTimer::timeout, this, &IoEngine::noFrames);
	connect(master_, &evre::Master::unsolicited, this, &IoEngine::onUnsolicited);
	connect(master_, &evre::Master::frameSent, this, [this](const QByteArray &raw) {
		if (monitorOn_) monitorLine("TX", raw, QString());
	});
	connect(master_, &evre::Master::frameReceived, this, [this](const QByteArray &raw, const QString &what) {
		if (!monitorOn_) return;
		const int stream = fastStreamOfRaw(raw);
		monitorLine("RX", raw, stream >= 0 ? tr("READ_RESP (fast stream %1)").arg(fastRuns_[stream].def.name) : what);
	});
	fastWatch_ = new QTimer(this);
	fastWatch_->setInterval(500);
	connect(fastWatch_, &QTimer::timeout, this, &IoEngine::watchFast);
	clock_.start();
	clockEpochMs_ = QDateTime::currentMSecsSinceEpoch();
	rateClock_.start();
#ifdef Q_OS_WIN
	/* Windows ticks timers every 15.6 ms unless a program asks for better; a
	 * 5 ms poll needs 1 ms. Linux and macOS timers are fine-grained already. */
	timeBeginPeriod(1);
#endif
}

IoEngine::~IoEngine() {
	stopTicker();
#ifdef Q_OS_WIN
	timeEndPeriod(1);
#endif
}

void IoEngine::post(std::function<void()> f) {
	QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
}

/* ------------------------------------------------- copies for other threads */

IoEngine::Stats IoEngine::stats() const {
	QMutexLocker lock(&crossThreadMutex_);
	return stats_;
}

QHash<RegKey, QVector<QPointF>> IoEngine::takeSamples() {
	QMutexLocker lock(&crossThreadMutex_);
	QHash<RegKey, QVector<QPointF>> out;
	out.swap(samples_);
	return out;
}

void IoEngine::setFastTrigger(int stream, const fast::TriggerWatch &watch) {
	for (int i = 0; i < fastRuns_.size(); i++) fastRuns_[i].trigger.set(i == stream ? watch : fast::TriggerWatch());
	if (stream < 0 || stream >= fastRuns_.size()) return;
	FastRun &run = fastRuns_[stream];
	QMutexLocker lock(&crossThreadMutex_);
	rescanWaiting(run.trigger, run.def, stream, fastBlocks_, run.state.clock().mark(), run.state.clock().period());
}

fast::TriggerWatch IoEngine::fastTriggerWatch(int stream) const {
	return stream >= 0 && stream < fastRuns_.size() ? fastRuns_[stream].trigger.watch() : fast::TriggerWatch();
}

/* A frame's blocks wait for the window (more when its thread was held): scanned only for the watch before, a crossing
 * in them after the window's newest record was lost to the new one. The times come from the clock as it is now: they
 * only decide which crossings count, and the window takes each one's time from its own store. */
void IoEngine::rescanWaiting(fast::TriggerScan &scan, const StreamDef &def, int stream, QVector<FastBlock> &blocks,
		const fast::FastClock::Mark &mark, double period) {
	bool first = true;
	for (FastBlock &block : blocks) {
		if (block.stream != stream) continue;
		block.crossings.clear();
		/* a block taken under another layout of the stream (the map loaded again) is not read with this one */
		if (block.count <= 0 || block.records.size() < qsizetype(block.count) * def.recordSize()) continue;
		if (first) scan.pairWith(def, block.before); /* the record before it is the window's newest */
		first = false;
		fast::BlockTaken taken;
		taken.first = block.first;
		taken.count = block.count;
		taken.newStart = block.newStart;
		taken.lost = block.lost;
		scan.scan(def, taken, block.records.constData(), mark, period, block.crossings);
	}
}

QVector<IoEngine::FastBlock> IoEngine::takeFastBlocks() {
	QMutexLocker lock(&crossThreadMutex_);
	fastQueued_ = 0;
	return std::exchange(fastBlocks_, {});
}

QStringList IoEngine::takeFrames(int &dropped) {
	QMutexLocker lock(&crossThreadMutex_);
	dropped = monitorLinesDropped_;
	monitorLinesDropped_ = 0;
	QStringList out;
	out.swap(monitorLines_);
	return out;
}

/* the text is made here, off the window's thread */
void IoEngine::monitorLine(const char *direction, const QByteArray &raw, const QString &note) {
	QString line = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")) + QLatin1String("  ") +
			QLatin1String(direction) + QLatin1String("  ") + evre::hex(raw);
	if (!note.isEmpty()) line += QLatin1String("   ") + note;
	QMutexLocker lock(&crossThreadMutex_);
	if (monitorLines_.size() >= MAX_MONITOR_LINES) {
		monitorLines_.removeFirst();
		monitorLinesDropped_++;
	}
	monitorLines_.push_back(std::move(line));
}

/* ---------------------------------------------------------------------- map */

void IoEngine::setMap(const QVector<RegDef> &defs, quint64 generation, const QString &deviceName,
		const QVector<EngineDevice> &devices) {
	table_.setDefs(defs, generation);
	deviceName_ = deviceName;
	const uint8_t oneDevice = isBus() ? 0 : target(0); /* where the one device was, before the new devices */
	devices_ = devices;
	/* Several devices now: none may send by itself. The window switched it off already; this makes sure, and when
	 * the device was sending, it is told so (a WRITE of CONFIG, not only its frames ignored): left on, its frames
	 * would collide with the other devices' answers. */
	if (isBus() && (autoSendWanted_ || autoSendOn_)) {
		autoSendWanted_ = false;
		stopStream();
		if (connected_ && canAutoSend_ && oneDevice != 0) applyAutoSend(oneDevice);
	}
	/* Fast EVRe: the one device's streams, each wish and state kept by name; a stream gone from the map is switched
	 * off on the device first (on a bus there are none: the window switched them off before) */
	QVector<FastRun> runs;
	if (!isBus() && !devices_.isEmpty() && devices_[0].map) {
		const DeviceMap &map = *devices_[0].map;
		for (const StreamDef &def : map.streams) {
			FastRun run;
			for (const FastRun &old : std::as_const(fastRuns_))
				if (old.def.name == def.name) run = old;
			/* another layout: the trigger's watch and the record kept were for the one before (the window watches
			 * again once it has the new streams) */
			if (run.def.addr != def.addr || run.def.recordSize() != def.recordSize()) {
				run.state.reset(def);
				run.trigger = fast::TriggerScan();
				run.lastRecord.clear();
			}
			run.def = def;
			const RegDef *enable = map.registerNamed(def.enable), *rate = map.registerNamed(def.rateReg);
			run.enableReg = enable ? *enable : RegDef();
			run.rateReg = rate ? *rate : RegDef();
			runs.push_back(run);
		}
	}
	for (const FastRun &old : std::as_const(fastRuns_)) {
		const bool kept = std::any_of(runs.begin(), runs.end(), [&](const FastRun &r) { return r.def.name == old.def.name; });
		if (kept || !connected_ || !old.deviceMaySend || !old.enable() || oneDevice == 0) continue;
		QByteArray zero;
		QString why;
		if (encodeValue(old.enableReg, QStringLiteral("0"), zero, why)) master_->writeTo(oneDevice, old.enableReg.addr, zero);
	}
	/* the blocks still waiting for the window go with their stream's number: one whose number is now another stream's,
	 * or the same stream in another layout, is dropped (its crossings, read by the new run, would be another's) */
	{
		QMutexLocker lock(&crossThreadMutex_);
		const auto stale = [&](const FastBlock &block) {
			if (block.stream < 0 || block.stream >= runs.size() || block.stream >= fastRuns_.size()) return true;
			const StreamDef &was = fastRuns_[block.stream].def, &now = runs[block.stream].def;
			return was.name != now.name || was.addr != now.addr || was.recordSize() != now.recordSize();
		};
		for (const FastBlock &block : std::as_const(fastBlocks_))
			if (stale(block)) fastQueued_ -= block.records.size();
		fastBlocks_.removeIf(stale);
	}
	fastRuns_ = runs;
	if (!anyFastOn() && !autoSendOn_) heartbeatTimer_->stop();
	QStringList names;
	if (isBus())
		for (const EngineDevice &device : devices_) names << device.name;
	api_->setBus(isBus(), names);
	applyInFlight();
	/* a device that left the bus is no longer waited for, nor counted */
	auto gone = [this](uint8_t slave) {
		return std::none_of(devices_.begin(), devices_.end(), [slave](const EngineDevice &d) { return d.slave == slave; });
	};
	for (auto it = offline_.begin(); it != offline_.end();) {
		if (gone(*it)) it = offline_.erase(it);
		else ++it;
	}
	for (auto it = missedAnswers_.begin(); it != missedAnswers_.end();) {
		if (gone(it.key())) it = missedAnswers_.erase(it);
		else ++it;
	}
	if (offline_.isEmpty()) offlineTimer_->stop();
	rowOfKey_.clear();
	for (int i = 0; i < table_.size(); i++) rowOfKey_.insert(regKey(table_.rows()[i].def), i);
	rebuildBlocks();
	/* the polls under way were dropped with the old layout: at interval 0 nothing else would start the
	 * next one (the ticker, when there is one, goes on by itself) */
	resumePolling();
	if (!statsTimer_->isActive()) statsTimer_->start();
}

void IoEngine::setPlotted(const QVector<RegKey> &keys) {
	plottedKeys_ = keys;
	QMutexLocker lock(&crossThreadMutex_);
	for (auto it = samples_.begin(); it != samples_.end();) {
		if (!keys.contains(it.key())) it = samples_.erase(it);
		else ++it;
	}
}

QString IoEngine::broadcastRefusal(uint16_t addr, const QByteArray &bytes) const {
	QVector<const DeviceMap *> maps;
	for (const EngineDevice &device : devices_)
		if (device.map) maps << device.map.get();
	return ::broadcastRefusal(maps, addr, bytes);
}

/* the one device of a map (slave 0) is at the link's slave; a device of a bus at its own */
uint8_t IoEngine::target(uint8_t tableSlave) const { return requestSlave(tableSlave, master_->slave()); }

/* a request to an address: which device's registers it reads (the table's slave); with one device, only the
 * link's slave is that device */
uint8_t IoEngine::tableSlave(uint8_t slave) const { return isBus() ? slave : 0; }

/* ----------------------------------------------------------------- the link */

void IoEngine::setLinkOptions(int slave, int timeoutMs, int inFlight) {
	master_->setSlave(uint8_t(slave));
	master_->setTimeoutMs(timeoutMs);
	inFlightWanted_ = inFlight;
	applyInFlight();
}

/* RS-485 has one transmitter at a time: two requests under way to two devices would have both answer at once */
void IoEngine::applyInFlight() {
	master_->setInFlight(serialBaud_ > 0 && isBus() ? 1 : inFlightWanted_);
}

void IoEngine::connectTcp(const QString &host, quint16 port, const QString &token) {
	serialBaud_ = 0;
	applyInFlight();
	attach(new evre::TcpLink(host, port), token);
}

/* the baud rate before the link opens (it may say so at once): the AUTO_SEND rate must leave room for the polls */
void IoEngine::connectSerial(const QString &port, qint32 baud) {
	serialBaud_ = baud;
	applyInFlight();
	attach(new evre::SerialLink(port, baud), QString());
}

/* the new link replaces the old one; it is made here, in the engine thread */
void IoEngine::attach(evre::Link *link, const QString &token) {
	disconnectLink();
	link_.reset(link);
	loginToken_ = token;
	connect(link, &evre::Link::opened, this, &IoEngine::onOpened);
	connect(link, &evre::Link::closed, this, &IoEngine::onClosed);
	master_->setLink(link);
	link->open();
}

/* A device left sending by itself would keep a link busy (and a serial port full) after the Studio is gone: AUTO_SEND
 * is cleared first, in a WRITE sent straight on the link and flushed before it closes. A lost link cannot send it. */
void IoEngine::disconnectLink() {
	stopTicker();
	offlineTimer_->stop();
	layoutGeneration_++; /* the answers still due belong to the old link */
	autoSendRequest_++;  /* and the AUTO_SEND ones: cancelled below, that is no refusal */
	pollsUnderWay_.clear();
	master_->setLink(nullptr);
	if (link_) {
		link_->disconnect(this); /* its closed() is not news any more */
		/* the device may still be sending (on, or an off still under way): off now, the last frame before the close */
		if (link_->isOpen() && deviceMaySend_) {
			const QByteArray off = configBytes(false, autoSendUsed_);
			link_->send(evre::build(autoSendSlave_, evre::WRITE, evre::CONFIG, uint16_t(off.size()), off));
			link_->flush(200);
			link_->drain(100); /* its last frames read: closed with bytes unread, TCP dropped the off on the way */
			deviceMaySend_ = false;
		}
		if (link_->isOpen()) sendFastOffs();
		if (link_->isOpen()) link_->close();
		link_.release()->deleteLater();
	}
	connected_ = false;
	/* the wish stays (autoSendWanted_): the next connect switches it on again, once STATUS says the device can */
	autoSendOn_ = false;
	statusKnown_ = canAutoSend_ = false;
	heartbeatTimer_->stop();
	firstFrameTimer_->stop();
	heartbeatPending_ = false;
	retryPending_ = false; /* cancelled with the link */
	streamAddr_ = streamCount_ = 0;
	streamRows_.clear();
	/* the fast streams' wishes stay too: switched on again at the next connect */
	for (FastRun &run : fastRuns_) {
		run.on = run.deviceMaySend = false;
		run.request++;
		run.takenMs = -1;
	}
	fastWatch_->stop();
	updateStats();
}

/* Connected: the values start afresh, then the requests go out in this order -
 * the login first (a gateway may drop a client that does not log in soon),
 * the device's ID, and the polls. opened() is emitted before them, so the
 * window logs the connection before any login warning. */
void IoEngine::onOpened() {
	connected_ = true;
	table_.clearValues();
	/* a new link: every device is given its chance again */
	offline_.clear();
	missedAnswers_.clear();
	rebuildBlocks();
	emit opened(link_->describe());
	sendLogin();
	readDeviceInfo();
	/* the fast streams wanted (a reconnect): on again, after the login */
	for (int i = 0; i < fastRuns_.size(); i++)
		if (fastRuns_[i].wanted) applyFast(i);
	resumePolling();
	updateStats();
}

void IoEngine::onClosed(const QString &why) {
	disconnectLink();
	emit closed(why);
}

/* each device's token (its own, else the link's), UTF-8, cut or zero-padded to the size of its login register */
void IoEngine::sendLogin() {
	bool sent = false, given = !loginToken_.isEmpty();
	for (const EngineDevice &device : devices_) {
		const QString token = device.token.isEmpty() ? loginToken_ : device.token;
		if (token.isEmpty()) continue;
		given = true;
		if (device.loginAddr == 0 || device.loginSize <= 0) continue;
		sent = true;
		const uint8_t slave = device.slave;
		master_->writeTo(target(slave), device.loginAddr, encodeLoginToken(token, device.loginSize),
				[this, slave](const evre::Result &r) {
					if (!r.ok) emit loginRefused(slave, r.message);
				});
	}
	if (given && !sent) emit loginSkipped();
}

/* DEVICE_ID and STATUS of every device: every EVRe device has them */
void IoEngine::readDeviceInfo() {
	for (const EngineDevice &device : devices_) {
		const uint8_t slave = device.slave;
		master_->readFrom(target(slave), evre::DEVICE_ID, 4, [this, slave](const evre::Result &r) {
			heardFrom(slave, r);
			if (!r.ok || r.data.size() < 4) {
				emit deviceInfo(slave, false, 0, 0, r.message);
				return;
			}
			const uint16_t status = evre::littleEndian16(r.data, 2);
			emit deviceInfo(slave, true, evre::littleEndian16(r.data, 0), status, QString());
			/* the one device: AUTO_SEND wanted (a reconnect) goes on now that STATUS says whether it can */
			if (slave != 0 || isBus()) return;
			statusKnown_ = true;
			canAutoSend_ = status & evre::CAP_AUTO_SEND;
			if (!autoSendWanted_) return;
			if (canAutoSend_) applyAutoSend(target(0));
			else autoSendFailed(true, tr("the device does not offer it (no CAP_AUTO_SEND in its STATUS)"));
		});
	}
}

/* ------------------------------------------------- devices that stop answering */

/* Counts the answers a device of a bus missed in a row: offline after OFFLINE_AFTER_TIMEOUTS, online again at its
 * first answer. An error answer is an answer too: the device is there. A request cancelled, or lost with the link,
 * says nothing of the device. With one device nothing changes: it is polled as always. */
void IoEngine::heardFrom(uint8_t tableSlave, const evre::Result &r) {
	if (!isBus()) return;
	const bool answered = r.ok || r.error != 0;
	if (!answered && !r.timedOut) return;
	/* a late answer for a device that has left the bus since */
	if (std::none_of(devices_.begin(), devices_.end(), [tableSlave](const EngineDevice &d) { return d.slave == tableSlave; }))
		return;
	if (answered) {
		missedAnswers_.remove(tableSlave);
		if (offline_.remove(tableSlave)) {
			emit deviceOnline(tableSlave, true);
			if (offline_.isEmpty()) offlineTimer_->stop();
			resumePolling();
		}
		return;
	}
	if (offline_.contains(tableSlave) || ++missedAnswers_[tableSlave] < OFFLINE_AFTER_TIMEOUTS) return;
	offline_.insert(tableSlave);
	emit deviceOnline(tableSlave, false);
	if (!offlineTimer_->isActive()) offlineTimer_->start();
}

/* One device per tick, the next after the one asked last (by slave, wrapping round), and never a second request
 * while one is under way: on a serial bus every offline device costs a whole timeout, and asking them all at once
 * would stall the polls of the others for that long each time. */
void IoEngine::retryOffline() {
	if (!connected_ || retryPending_ || offline_.isEmpty()) return;
	QList<uint8_t> slaves(offline_.begin(), offline_.end());
	std::sort(slaves.begin(), slaves.end());
	const auto next = std::upper_bound(slaves.begin(), slaves.end(), lastRetried_);
	const uint8_t slave = next != slaves.end() ? *next : slaves.front();
	lastRetried_ = slave;
	retryPending_ = true;
	master_->readFrom(target(slave), evre::DEVICE_ID, 2, [this, slave](const evre::Result &r) {
		retryPending_ = false;
		heardFrom(slave, r);
	});
}

/* --------------------------------------------------------------- the ticker */

void IoEngine::startTicker() {
	{
		std::lock_guard<std::mutex> lock(tickerMutex_);
		if (tickerRunning_) return;
		tickerRunning_ = true;
	}
	ticksDue_ = 0;
	pollsBehind_ = 0;
#ifdef Q_OS_WIN
	tickerStopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
	ticker_ = std::thread([this] { runTicker(); });
}

void IoEngine::stopTicker() {
	{
		std::lock_guard<std::mutex> lock(tickerMutex_);
		if (!tickerRunning_ && !ticker_.joinable()) return;
		tickerRunning_ = false;
	}
	tickerWake_.notify_all();
#ifdef Q_OS_WIN
	if (tickerStopEvent_) SetEvent(static_cast<HANDLE>(tickerStopEvent_));
#endif
	if (ticker_.joinable()) ticker_.join();
#ifdef Q_OS_WIN
	if (tickerStopEvent_) CloseHandle(static_cast<HANDLE>(tickerStopEvent_));
	tickerStopEvent_ = nullptr;
#endif
}

/* The ticker thread: sleeps to each deadline, then onTick() with the periods due (tickPeriodsDue). The interval is
 * read again every period, so a new one needs no restart. */
void IoEngine::runTicker() {
	auto deadline = Clock::now();
#ifdef Q_OS_WIN
	/* a high-resolution waitable timer (Windows 10 1803+): the C++ library's
	 * timed waits round to the 15.6 ms system tick here */
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!timer) timer = CreateWaitableTimerW(nullptr, TRUE, nullptr); /* older Windows: the system tick */
	const HANDLE stop = static_cast<HANDLE>(tickerStopEvent_);
	const HANDLE handles[2] = { stop, timer };
	for (;;) {
		deadline += std::chrono::microseconds(tickIntervalUs_.load());
		const auto waitUs = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
		if (waitUs > 0) {
			LARGE_INTEGER due;
			due.QuadPart = -LONGLONG(waitUs) * 10; /* relative, in 100 ns units */
			SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
			if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) break;
		} else if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) {
			break;
		}
		onTick(tickPeriodsDue(deadline, std::chrono::microseconds(tickIntervalUs_.load())));
	}
	CloseHandle(timer);
#else
	/* Linux, macOS: the C++ library's timed wait is precise */
	std::unique_lock<std::mutex> lock(tickerMutex_);
	while (tickerRunning_) {
		deadline += std::chrono::microseconds(tickIntervalUs_.load());
		tickerWake_.wait_until(lock, deadline, [this] { return !tickerRunning_; });
		if (!tickerRunning_) break;
		onTick(tickPeriodsDue(deadline, std::chrono::microseconds(tickIntervalUs_.load())));
	}
#endif
}

/* polls due, and a wake-up of the engine unless one is queued
 * already: pollTick() takes every tick due at once */
void IoEngine::onTick(int ticks) {
	ticksDue_ += ticks;
	if (tickQueued_.exchange(true)) return;
	QMetaObject::invokeMethod(this, [this] {
		tickQueued_ = false;
		pollTick();
	}, Qt::QueuedConnection);
}

/* ------------------------------------------------------------------ polling */

void IoEngine::setPolling(bool on, double intervalMs) {
	pollingOn_ = on;
	continuous_ = intervalMs <= 0.0;
	tickIntervalUs_ = std::max<qint64>(50, qint64(intervalMs * 1000.0)); /* the ticker takes it at its next period */
	if (!on || continuous_) stopTicker();
	resumePolling();
}

void IoEngine::resumePolling() {
	if (!connected_ || !pollingOn_) return;
	if (!continuous_) startTicker();
	else if (pollsUnderWay_.isEmpty()) QTimer::singleShot(0, this, &IoEngine::pollTick);
}

/* A poll as block reads: the pollable registers by device and address, each
 * one joining the block before it while of the same device, in the same bank
 * and at most MAX_BLOCK_GAP bytes past its end (the rule in
 * model/device_map.h). A device that is not polled has none, nor do the
 * registers the AUTO_SEND frames bring. A new layout makes the answers of the
 * polls under way stale. */
void IoEngine::rebuildBlocks() {
	blocks_.clear();
	const auto &rows = table_.rows();
	QSet<uint8_t> notPolled;
	for (const EngineDevice &device : devices_)
		if (!device.poll) notPolled.insert(device.slave);
	/* the registers wholly inside the last frame of the stream: the frames fill them */
	streamRows_.clear();
	if (autoSendOn_ && streamCount_ > 0 && !isBus()) {
		for (int i = 0; i < rows.size(); i++) {
			const RegDef &d = rows[i].def;
			if (d.slave == 0 && d.addr >= streamAddr_ && int(d.addr) + d.size <= int(streamAddr_) + streamCount_)
				streamRows_ << i;
		}
	}
	QVector<int> byAddress;
	for (int i = 0; i < rows.size(); i++)
		if (pollable(rows[i]) && !notPolled.contains(rows[i].def.slave) && !streamRows_.contains(i)) byAddress << i;
	std::sort(byAddress.begin(), byAddress.end(),
			[&rows](int a, int b) { return regKey(rows[a].def) < regKey(rows[b].def); });
	for (int row : byAddress) {
		const RegDef &d = rows[row].def;
		const bool joins = !blocks_.isEmpty() && blocks_.last().slave == d.slave && sameBank(d.addr, blocks_.last().addr)
				&& int(d.addr) - (blocks_.last().addr + blocks_.last().size) <= MAX_BLOCK_GAP;
		if (!joins) {
			Block block;
			block.slave = d.slave;
			block.addr = d.addr;
			blocks_.push_back(block);
		}
		Block &block = blocks_.last();
		block.rows << row;
		block.size = uint16_t(std::max(int(block.size), int(d.addr) + d.size - int(block.addr)));
	}
	layoutGeneration_++;
	pollsUnderWay_.clear();
}

int IoEngine::maxPollsUnderWay() const {
	const int blocks = std::max(1, int(blocks_.size()));
	return std::clamp(master_->inFlight() / blocks, 1, MAX_POLLS_UNDER_WAY);
}

/* the blocks of every device that answers (an offline one is asked by retryOffline instead) */
void IoEngine::startPoll() {
	const quint64 layout = layoutGeneration_, pollId = nextPollId_++;
	QVector<Block> reads;
	for (const Block &block : std::as_const(blocks_))
		if (!offline_.contains(block.slave)) reads << block;
	if (reads.isEmpty()) return;
	pollsUnderWay_.insert(pollId, int(reads.size()));
	for (const Block &block : std::as_const(reads)) {
		master_->readFrom(target(block.slave), block.addr, block.size, [this, layout, pollId, block](const evre::Result &r) {
			onBlockResult(layout, pollId, block, r);
		});
	}
}

void IoEngine::pollTick() {
	if (!connected_ || !pollingOn_ || blocks_.isEmpty()) return;
	if (offline_.size() > 0 && std::all_of(blocks_.begin(), blocks_.end(),
			[this](const Block &b) { return offline_.contains(b.slave); }))
		return; /* nobody to ask: retryOffline brings them back */
	if (continuous_) {
		while (pollsUnderWay_.size() < maxPollsUnderWay()) startPoll();
		return;
	}
	/* every tick fallen due starts a poll, as slots free up; the rest start as
	 * polls end (pollDone). A backlog past MAX_BACKLOG_US (a stall) is not made
	 * up in a burst. */
	const int maxBehind = std::max(8, int(MAX_BACKLOG_US / std::max<qint64>(1, tickIntervalUs_.load())));
	pollsBehind_ = std::min(maxBehind, pollsBehind_ + ticksDue_.exchange(0));
	while (pollsBehind_ > 0 && pollsUnderWay_.size() < maxPollsUnderWay()) {
		pollsBehind_--;
		startPoll();
	}
}

void IoEngine::onBlockResult(quint64 layout, quint64 pollId, const Block &block, const evre::Result &r) {
	if (layout != layoutGeneration_) return; /* a poll of an older layout: its rows may be others now */
	heardFrom(block.slave, r);
	if (r.ok) {
		storeBlock(block, r.data);
	} else if (r.error != 0) {
		onBlockRefused(block, r);
	} else {
		for (int row : block.rows) table_.setError(row, r.message, false); /* a timeout or the link: next poll */
	}
	auto it = pollsUnderWay_.find(pollId);
	if (it != pollsUnderWay_.end() && --it.value() <= 0) {
		pollsUnderWay_.erase(it);
		pollDone();
	}
}

/* each register's bytes, out of the block's */
void IoEngine::storeBlock(const Block &block, const QByteArray &data) {
	const auto &rows = table_.rows();
	for (int row : block.rows) {
		const RegDef &d = rows[row].def;
		table_.setRaw(row, data.mid(d.addr - block.addr, d.size));
	}
}

/* The device refused a block: a merged read over a gap it does not have, or
 * a register it does not have. A merged block is split into one read per
 * register; a register refused for good is marked unavailable and no longer
 * polled. */
void IoEngine::onBlockRefused(const Block &block, const evre::Result &r) {
	if (!block.single) {
		splitBlock(block);
		return;
	}
	const bool gone = refusedForGood(r.error);
	for (int row : block.rows) table_.setError(row, r.message, gone);
	if (!gone) return;
	const auto it = std::find_if(blocks_.begin(), blocks_.end(),
			[&block](const Block &b) { return b.single && b.slave == block.slave && b.addr == block.addr; });
	if (it != blocks_.end()) blocks_.erase(it);
}

/* a merged block -> one block per register, in its place (unless the answer
 * to an earlier poll has split it already) */
void IoEngine::splitBlock(const Block &block) {
	const auto it = std::find_if(blocks_.begin(), blocks_.end(),
			[&block](const Block &b) { return !b.single && b.slave == block.slave && b.addr == block.addr; });
	if (it == blocks_.end()) return;
	const int at = int(it - blocks_.begin());
	blocks_.remove(at);
	const auto &rows = table_.rows();
	for (int i = 0; i < block.rows.size(); i++) {
		const RegDef &d = rows[block.rows[i]].def;
		Block single;
		single.slave = block.slave;
		single.addr = d.addr;
		single.size = uint16_t(d.size);
		single.rows << block.rows[i];
		single.single = true;
		blocks_.insert(at + i, single);
	}
}

/* every block of a poll answered: a point per plotted register, a CSV row (with AUTO_SEND on and its frames coming,
 * each frame is the tick instead: the polls of the rest only refresh the table; a device switched on that never
 * sends a frame keeps its polled samples) */
void IoEngine::pollDone() {
	pollsSinceRate_++;
	/* polls waiting for a slot, or as fast as possible: the next one now */
	if (pollsBehind_ > 0 || (continuous_ && pollingOn_)) QTimer::singleShot(0, this, &IoEngine::pollTick);
	if (autoSendOn_ && streamCount_ > 0) return;
	const double t = now();
	appendSamples(t);
	writeCsvRow(t);
}

/* an offline device's registers get no point: its last value is not news */
void IoEngine::appendSamples(double t) {
	if (plottedKeys_.isEmpty()) return;
	const auto &rows = table_.rows();
	QMutexLocker lock(&crossThreadMutex_);
	for (RegKey key : plottedKeys_) {
		const int row = rowOfKey_.value(key, -1);
		if (row < 0 || !rows[row].valid || offline_.contains(rows[row].def.slave)) continue;
		QVector<QPointF> &points = samples_[key];
		if (points.size() >= MAX_SAMPLES) points.remove(0, MAX_SAMPLES / 10);
		points.push_back(QPointF(t, decodeNumber(rows[row].def, rows[row].raw)));
	}
}

void IoEngine::updateStats() {
	/* the poll rate, over a second at least; until then the last one stands */
	const double elapsed = double(rateClock_.elapsed()) / 1000.0;
	double pollHz = -1, framesHz = -1;
	if (elapsed >= 1.0) {
		pollHz = pollsSinceRate_ / elapsed;
		framesHz = framesSinceRate_ / elapsed;
		pollsSinceRate_ = framesSinceRate_ = 0;
		for (FastRun &run : fastRuns_) {
			run.recordsHz = connected_ && run.on ? double(run.recordsSinceRate) / elapsed : 0;
			run.recordsSinceRate = 0;
		}
		rateClock_.restart();
	}
	if (csvFile_->isOpen()) csvStream_.flush();
	for (FastRun &run : fastRuns_)
		if (run.writer) run.writer->flush();
	const auto &rows = table_.rows();
	QMutexLocker lock(&crossThreadMutex_);
	stats_.master = master_->stats();
	if (pollHz >= 0) stats_.pollHz = connected_ ? pollHz : 0;
	if (framesHz >= 0) stats_.autoSendHz = connected_ && autoSendOn_ ? framesHz : 0;
	stats_.autoSend = connected_ && autoSendOn_;
	stats_.blocks = int(blocks_.size());
	stats_.pollable = int(std::count_if(rows.begin(), rows.end(), pollable));
	stats_.recording = csvFile_->isOpen();
	stats_.csvRows = csvRows_;
	stats_.csvCols = int(csvColumns_.size());
	stats_.csvFile = csvFile_->fileName();
	stats_.apiRunning = api_->running();
	stats_.evrePort = api_->evrePort();
	stats_.jsonPort = api_->jsonPort();
	stats_.apiClients = api_->clientCount();
	stats_.apiRequests = api_->requests();
	stats_.fast.resize(fastRuns_.size());
	for (int i = 0; i < fastRuns_.size(); i++) {
		const FastRun &run = fastRuns_[i];
		Stats::Fast &f = stats_.fast[i];
		f.name = run.def.name;
		f.wanted = run.wanted;
		f.on = connected_ && run.on;
		f.rate = run.state.clock().started() ? run.state.clock().rate() : 0;
		f.ppm = run.state.clock().started() ? run.state.clock().ppm() : 0;
		f.recordsHz = run.recordsHz;
		f.records = run.state.records;
		f.blocks = run.state.blocks;
		f.lost = run.state.lost;
		f.badBlocks = run.state.badBlocks;
		f.newerBlocks = run.state.newerBlocks;
		f.starts = run.state.starts;
		f.notShown = run.notShown;
	}
}

/* ---------------------------------------------------------------- AUTO_SEND */

const QVector<int> &IoEngine::autoSendDividers() {
	/* 8000 Hz / divider for the prescalers 1..199 that give whole rates: 4000 Hz down to 40 Hz */
	static const QVector<int> dividers = { 2, 4, 5, 8, 10, 16, 20, 25, 32, 40, 50, 80, 100, 125, 160, 200 };
	return dividers;
}

/* a frame is the data and 10 bytes around it (7B, slave, function, offset, count, CRC, 7D); a UART byte is 10 bits */
int IoEngine::autoSendPrescalerFor(int prescaler, qint32 baud, int streamBytes) {
	if (baud <= 0) return prescaler;
	const double room = AUTO_SEND_LINK_SHARE * baud / 10.0; /* bytes a second */
	const int frame = streamBytes + 10;
	auto fits = [&](int divider) { return double(evre::AUTO_SEND_BASE_HZ) / divider * frame <= room; };
	if (fits(prescaler + 1)) return prescaler;
	for (const int divider : autoSendDividers())
		if (divider > prescaler + 1 && fits(divider)) return divider - 1;
	return autoSendDividers().last() - 1;
}

/* the wish is kept whatever the link: connected, it goes to the device at once (on: once STATUS allows it) */
void IoEngine::setAutoSend(bool on, int prescaler) {
	autoSendWanted_ = on;
	autoSendPrescaler_ = std::clamp(prescaler, evre::AUTO_SEND_PRESCALER_MIN,
			evre::AUTO_SEND_BASE_HZ / evre::AUTO_SEND_MIN_HZ - 1);
	if (!connected_) return;
	if (on && isBus()) {
		autoSendFailed(true, tr("not with several devices on the link: their frames would collide"));
		return;
	}
	if (!on) stopStream(); /* at once: the frames still coming are ignored */
	if (!statusKnown_) return; /* readDeviceInfo goes on with it */
	if (on && !canAutoSend_) {
		autoSendFailed(true, tr("the device does not offer it (no CAP_AUTO_SEND in its STATUS)"));
		return;
	}
	if (canAutoSend_) applyAutoSend(target(0));
}

/* CONFIG as written: MSG_ENABLE as the device had it, AUTO_SEND, the prescaler; HEARTBEAT (the device sets it),
 * SYS_RESET and DFU always 0 */
QByteArray IoEngine::configBytes(bool on, int prescaler) const {
	const uint16_t value = uint16_t(configKept_ | (on ? evre::CONFIG_AUTO_SEND : 0)
			| (prescaler << evre::CONFIG_PRESCALER_SHIFT));
	QByteArray bytes(2, '\0');
	bytes[0] = char(value & 0xFF);
	bytes[1] = char(value >> 8);
	return bytes;
}

/* the wanted state to the device: CONFIG read (its MSG_ENABLE is kept), then written with WRITE_ACK. On a serial link
 * the rate is cut to what leaves room for the polls. */
void IoEngine::applyAutoSend(uint8_t slave) {
	const bool on = autoSendWanted_ && !isBus();
	const quint64 request = ++autoSendRequest_;
	int prescaler = autoSendPrescaler_;
	if (on && serialBaud_ > 0) {
		prescaler = autoSendPrescalerFor(autoSendPrescaler_, serialBaud_, streamBytes());
		if (prescaler != autoSendPrescaler_)
			emit autoSendSlowed(tr("auto send at %1 Hz, not %2 Hz: %2 frames of %3 bytes a second would take more than "
					"%4% of the serial link at %5 baud").arg(evre::AUTO_SEND_BASE_HZ / (prescaler + 1))
					.arg(evre::AUTO_SEND_BASE_HZ / (autoSendPrescaler_ + 1)).arg(streamBytes() + 10)
					.arg(int(AUTO_SEND_LINK_SHARE * 100)).arg(serialBaud_));
	}
	master_->readFrom(slave, evre::CONFIG, 2, [this, request, on, prescaler, slave](const evre::Result &r) {
		if (request != autoSendRequest_) return;
		if (!r.ok || r.data.size() < 2) {
			autoSendFailed(on, tr("CONFIG not read: %1").arg(r.message));
			return;
		}
		configKept_ = evre::littleEndian16(r.data, 0) & evre::CONFIG_MSG_ENABLE;
		autoSendUsed_ = prescaler;
		/* on before the answer: the first frames may come ahead of it */
		if (on) {
			autoSendOn_ = true;
			deviceMaySend_ = true;
			autoSendSlave_ = slave;
		}
		master_->writeTo(slave, evre::CONFIG, configBytes(on, prescaler), [this, request, on, prescaler](
				const evre::Result &w) {
			if (request != autoSendRequest_) return;
			if (!w.ok) {
				autoSendFailed(on, tr("CONFIG not written: %1").arg(w.message));
				return;
			}
			if (on) {
				heartbeatTimer_->start();
				if (streamCount_ == 0) firstFrameTimer_->start(); /* the first frame must come */
			} else {
				deviceMaySend_ = false; /* the device took the off */
			}
			emit autoSendSet(on, evre::AUTO_SEND_BASE_HZ / (prescaler + 1), QString());
		});
	});
}

void IoEngine::autoSendFailed(bool on, const QString &why) {
	if (on) {
		autoSendWanted_ = false;
		stopStream();
	}
	emit autoSendSet(on, 0, why);
}

/* frames are no longer taken; the registers they brought are polled again */
/* AUTO_SEND taken, and not one frame since: something between does not pass them on (a gateway that answers requests
 * from its own copy of the device, and streams nothing). Off again, on the device too; the polls go on. */
void IoEngine::noFrames() {
	if (!autoSendOn_ || streamCount_ > 0) return;
	const uint8_t slave = autoSendSlave_;
	autoSendFailed(true, tr("no frame came in %1 s: something between the Studio and the device (a gateway) does not "
			"pass them on; the registers are polled").arg(FIRST_FRAME_MS / 1000));
	applyAutoSend(slave); /* the off to the device */
}

void IoEngine::stopStream() {
	autoSendOn_ = false;
	if (!anyFastOn()) heartbeatTimer_->stop(); /* a fast stream still needs it */
	firstFrameTimer_->stop();
	if (streamCount_ == 0) return;
	streamAddr_ = streamCount_ = 0;
	rebuildBlocks();
	resumePolling(); /* the polls under way were dropped with the old layout */
}

/* A frame nobody asked for: with AUTO_SEND on, a READ_RESP of the device's read-only block. Its registers go into the
 * table and it is a sample tick (chart points, a CSV row). Anything else (a late answer) is ignored. When it covers
 * other registers than the last one, the polls are laid out again without them. */
void IoEngine::onUnsolicited(const evre::Frame &frame) {
	/* a fast stream's block, recognised first: by its slave and its window */
	const int stream = fastStreamOf(frame);
	if (stream >= 0) {
		api_->passBlock(fastRuns_[stream].def.name, frame); /* to the pass-through clients that asked, as it came */
		takeBlock(stream, frame);
		return;
	}
	if (!autoSendOn_ || isBus() || frame.fn != evre::READ_RESP || frame.slave != target(0)
			|| frame.addr != evre::READ_ONLY_BLOCK || frame.cnt == 0 || frame.data.size() != frame.cnt)
		return;
	if (frame.addr != streamAddr_ || frame.cnt != streamCount_) {
		streamAddr_ = frame.addr;
		streamCount_ = frame.cnt;
		rebuildBlocks();
		resumePolling();
	}
	const auto &rows = table_.rows();
	for (const int row : std::as_const(streamRows_)) {
		const RegDef &d = rows[row].def;
		table_.setRaw(row, frame.data.mid(d.addr - streamAddr_, d.size));
	}
	framesSinceRate_++;
	const double t = now();
	appendSamples(t);
	writeCsvRow(t);
}

/* Every HEARTBEAT_MS while AUTO_SEND or a fast stream is on: CONFIG read, whatever the polling is (Poll off, or every
 * register in the frames), so the device's host watchdog sees a request. AUTO_SEND found cleared (the device reset,
 * or someone wrote CONFIG): said once, and not switched on again by itself. */
void IoEngine::heartbeat() {
	if (!connected_ || (!autoSendOn_ && !anyFastOn()) || heartbeatPending_) return;
	heartbeatPending_ = true;
	const quint64 request = autoSendRequest_;
	master_->readFrom(target(0), evre::CONFIG, 2, [this, request](const evre::Result &r) {
		heartbeatPending_ = false;
		if (request != autoSendRequest_ || !autoSendOn_ || !r.ok || r.data.size() < 2) return;
		const int row = rowOfKey_.value(regKey(0, evre::CONFIG), -1);
		if (row >= 0 && table_.rows()[row].def.size == 2) table_.setRaw(row, r.data);
		if (evre::littleEndian16(r.data, 0) & evre::CONFIG_AUTO_SEND) return;
		autoSendWanted_ = false;
		stopStream();
		emit autoSendStopped();
	});
}

/* the data bytes of one frame: the last frame's; before the first, the map's read-only block from 0xD000 up to the
 * first writable register */
int IoEngine::streamBytes() const {
	if (streamCount_ > 0) return streamCount_;
	int firstWritable = 0x10000, end = evre::READ_ONLY_BLOCK;
	for (const RegValue &v : table_.rows())
		if (v.def.slave == 0 && v.def.rw && v.def.addr >= evre::READ_ONLY_BLOCK)
			firstWritable = std::min(firstWritable, int(v.def.addr));
	for (const RegValue &v : table_.rows())
		if (v.def.slave == 0 && !v.def.rw && v.def.addr >= evre::READ_ONLY_BLOCK && v.def.addr < firstWritable)
			end = std::max(end, int(v.def.addr) + v.def.size);
	return end - evre::READ_ONLY_BLOCK;
}

/* ---------------------------------------------------------------- Fast EVRe */

/* the wish is kept whatever the link: connected, it goes to the device at once */
void IoEngine::setFastStream(int stream, bool on) {
	if (stream < 0 || stream >= fastRuns_.size()) return;
	FastRun &run = fastRuns_[stream];
	run.wanted = on;
	if (!connected_) return;
	if (on && isBus()) {
		fastFailed(stream, true, tr("not with several devices on the link: their blocks would collide"));
		return;
	}
	applyFast(stream);
}

bool IoEngine::anyFastOn() const {
	return std::any_of(fastRuns_.begin(), fastRuns_.end(), [](const FastRun &run) { return run.on; });
}

/* On: its rate register read (the rate the device was set to; else the map's), then its enable written 1 with
 * WRITE_ACK. The blocks are taken from just before the write: the first may come ahead of its answer. A stream
 * without an enable is the device's own business: it is listened to at once. Off: the blocks ignored at once, the
 * enable written 0. */
void IoEngine::applyFast(int stream) {
	FastRun &run = fastRuns_[stream];
	const quint64 request = ++run.request;
	const bool on = run.wanted && !isBus();
	const uint8_t slave = target(0);
	const QString name = run.def.name;
	auto switchOn = [this, stream, name, request, slave](double rate) {
		FastRun *asked = fastRunAsked(stream, name, request);
		if (!asked) return;
		FastRun &run = *asked;
		run.state.reset(run.def, rate);
		run.notShown = 0;
		run.on = true;
		run.newerSaid = run.silenceAsked = false;
		run.lastBlockMs = run.takenMs = -1;
		heartbeatTimer_->start();
		fastWatch_->start();
		const RegDef *enable = run.enable();
		if (!enable) {
			run.takenMs = clock_.elapsed();
			emit fastStreamSet(stream, true, rate, QString());
			return;
		}
		QByteArray one;
		QString why;
		encodeValue(*enable, QStringLiteral("1"), one, why);
		run.deviceMaySend = true;
		master_->writeTo(slave, enable->addr, one, [this, stream, name, request, rate](const evre::Result &w) {
			FastRun *asked = fastRunAsked(stream, name, request);
			if (!asked) return;
			FastRun &run = *asked;
			if (!w.ok) {
				fastFailed(stream, true, tr("%1 not written: %2").arg(run.enableReg.name, w.message));
				return;
			}
			run.takenMs = clock_.elapsed(); /* the first block must come FIRST_FRAME_MS from now */
			emit fastStreamSet(stream, true, rate, QString());
		});
	};
	if (!on) {
		const bool wasOn = run.on;
		run.on = false;
		run.takenMs = -1;
		if (!anyFastOn()) fastWatch_->stop();
		if (!anyFastOn() && !autoSendOn_) heartbeatTimer_->stop();
		const RegDef *enable = run.enable();
		if (!enable || !(run.deviceMaySend || wasOn)) {
			emit fastStreamSet(stream, false, 0, QString());
			return;
		}
		QByteArray zero;
		QString why;
		encodeValue(*enable, QStringLiteral("0"), zero, why);
		master_->writeTo(slave, enable->addr, zero, [this, stream, name, request](const evre::Result &w) {
			FastRun *asked = fastRunAsked(stream, name, request);
			if (!asked) return;
			if (w.ok) asked->deviceMaySend = false; /* the device took the off */
			emit fastStreamSet(stream, false, 0, w.ok ? QString() : w.message);
		});
		return;
	}
	const RegDef *rate = run.rate();
	if (!rate) {
		switchOn(run.def.rate);
		return;
	}
	master_->readFrom(slave, rate->addr, uint16_t(rate->size), [this, stream, name, request, switchOn](const evre::Result &r) {
		const FastRun *asked = fastRunAsked(stream, name, request);
		if (!asked) return;
		const double hz = r.ok && r.data.size() == asked->rateReg.size ? decodeNumber(asked->rateReg, r.data) : 0;
		switchOn(hz > 0 ? hz : asked->def.rate);
	});
}

IoEngine::FastRun *IoEngine::fastRunAsked(int stream, const QString &name, quint64 request) {
	if (stream < 0 || stream >= fastRuns_.size()) return nullptr;
	FastRun &run = fastRuns_[stream];
	return run.def.name == name && run.request == request ? &run : nullptr;
}

void IoEngine::fastFailed(int stream, bool on, const QString &why) {
	FastRun &run = fastRuns_[stream];
	if (on) {
		run.wanted = run.on = false;
		run.takenMs = -1;
		run.request++;
		if (!anyFastOn() && !autoSendOn_) heartbeatTimer_->stop();
	}
	emit fastStreamSet(stream, on, 0, why);
}

/* an unasked READ_RESP from the one device at a stream's window */
int IoEngine::fastStreamOf(const evre::Frame &frame) const {
	if (fastRuns_.isEmpty() || isBus() || frame.fn != evre::READ_RESP || frame.slave != target(0)) return -1;
	for (int i = 0; i < fastRuns_.size(); i++)
		if (fastRuns_[i].def.addr == frame.addr) return i;
	return -1;
}

int IoEngine::fastStreamOfRaw(const QByteArray &raw) const {
	if (raw.size() < 7) return -1;
	evre::Frame frame;
	frame.slave = uint8_t(raw[1]);
	frame.fn = uint8_t(raw[2]);
	frame.addr = evre::littleEndian16(raw, 3);
	return fastStreamOf(frame);
}

/* A block, by the block's rules: counted, laid on the clock (now()'s seconds). A stream not on (switched off, its
 * last blocks still coming) takes none. */
void IoEngine::takeBlock(int stream, const evre::Frame &frame) {
	FastRun &run = fastRuns_[stream];
	if (!connected_ || !run.on || frame.data.size() != frame.cnt) return;
	fast::BlockTaken taken;
	const fast::BlockCheck check = run.state.take(frame.data, now(), taken);
	if (check == fast::BlockCheck::NewerKind && !run.newerSaid) {
		run.newerSaid = true;
		emit fastStreamNote(stream, tr("fast stream %1: blocks of a newer kind (a flag or a spare byte this Studio does "
				"not know): none of their samples is used").arg(run.def.name), false);
	}
	if (check != fast::BlockCheck::Ok) return;
	if (csvFile_->isOpen()) recordBlock(stream, taken, frame.data);
	FastBlock block;
	block.stream = stream;
	block.first = taken.first;
	block.count = taken.count;
	block.newStart = taken.newStart;
	block.lost = taken.lost;
	block.records = frame.data.mid(fast::HEADER);
	const int size = run.def.recordSize();
	if (taken.lost == 0 && !taken.newStart) block.before = run.lastRecord;
	if (size > 0 && block.records.size() >= qsizetype(taken.count) * size)
		run.lastRecord = block.records.mid(qsizetype(taken.count - 1) * size, size);
	if (taken.newMark) {
		block.marked = true;
		block.markRecord = run.state.clock().mark().record;
		block.markTime = run.state.clock().mark().time;
		block.markPeriod = run.state.clock().period();
	}
	/* the API: its newest record, and its JSON streams' periods */
	if (taken.count > 0) run.lastRecordTime = run.state.clock().timeOf(taken.first + quint64(taken.count) - 1);
	if (size > 0 && block.records.size() >= qsizetype(taken.count) * size)
		api_->fastRecords(run.def, taken.first, taken.count, block.records.constData());
	/* the chart's trigger: its crossing found here, as the block comes, so the window holds on it at the next frame */
	run.trigger.scan(run.def, taken, frame.data.constData() + fast::HEADER, run.state.clock().mark(),
			run.state.clock().period(), block.crossings);
	{
		QMutexLocker lock(&crossThreadMutex_);
		fastQueued_ += block.records.size();
		fastBlocks_.push_back(std::move(block));
		/* a window that does not take them: the oldest go, counted; a start or a mark they carried goes on with the
		 * next, so the chart still lays what follows on the clock */
		qsizetype gone = 0;
		while (fastQueued_ > FAST_QUEUE_BYTES && fastBlocks_.size() - gone > 1) {
			const FastBlock &old = fastBlocks_[gone];
			FastBlock &next = fastBlocks_[gone + 1];
			fastQueued_ -= old.records.size();
			if (old.stream < fastRuns_.size()) fastRuns_[old.stream].notShown += quint64(old.count);
			/* its trigger crossing never reaches the window: the scan waits for the next one instead */
			if (old.stream < fastRuns_.size() && !old.crossings.isEmpty())
				fastRuns_[old.stream].trigger.dropped(old.crossings.last());
			if (next.stream == old.stream) {
				next.newStart = next.newStart || old.newStart;
				next.before.clear(); /* the window never has the record before it */
				if (old.marked && !next.marked) {
					next.marked = true;
					next.markRecord = old.markRecord;
					next.markTime = old.markTime;
					next.markPeriod = old.markPeriod;
				}
			}
			gone++;
		}
		if (gone > 0) fastBlocks_.remove(0, gone);
	}
	run.lastBlockMs = clock_.elapsed();
	run.silenceAsked = false;
	run.recordsSinceRate += quint64(taken.count);
}

/* The CSV records: the block as it came into its stream's file beside the CSV, made at its first block with a time
 * mark before it (the clock's newest: the reader lays the records from there), then a mark whenever the clock makes
 * one. A file that cannot be written is said once and left. */
void IoEngine::recordBlock(int stream, const fast::BlockTaken &taken, const QByteArray &data) {
	FastRun &run = fastRuns_[stream];
	if (run.writeFailed) return;
	bool marked = taken.newMark;
	if (!run.writer) {
		run.writer = std::make_shared<fast::RecordingWriter>();
		run.recordedBlocks = 0;
		QString err;
		const QString file = fast::recordingFileFor(csvFile_->fileName(), run.def.name);
		if (!run.writer->open(file, deviceName_, run.def, QDateTime::currentDateTime(), err, now())) {
			run.writeFailed = true;
			run.writer.reset();
			emit fastStreamNote(stream, tr("fast stream %1 not recorded: %2: %3").arg(run.def.name, file, err), false);
			return;
		}
		marked = true;
	}
	bool ok = true;
	if (marked) ok = run.writer->mark(run.state.clock().mark().record, run.state.clock().mark().time);
	ok = ok && run.writer->block(data);
	if (!ok) {
		run.writeFailed = true;
		emit fastStreamNote(stream, tr("fast stream %1: its recording stopped: %2").arg(run.def.name, run.writer->errorString()), false);
		run.writer.reset();
		return;
	}
	run.recordedBlocks++;
}

/* Every 500 ms while a stream is on. No block FIRST_FRAME_MS after the device took the enable: something between does
 * not pass them on, or the device does not stream; off again, and said. A stream that sent and went silent as long:
 * its enable read once; 0 (a reset, another host) stops it here too, not switched on again by itself. */
void IoEngine::watchFast() {
	if (!connected_) return;
	const qint64 now = clock_.elapsed();
	for (int i = 0; i < fastRuns_.size(); i++) {
		FastRun &run = fastRuns_[i];
		if (!run.on || run.takenMs < 0) continue;
		if (run.lastBlockMs < 0 && now - run.takenMs >= FIRST_FRAME_MS) {
			const QString why = tr("no block came in %1 s: the device does not stream, or something between the Studio "
					"and it (a gateway) does not pass the blocks on").arg(FIRST_FRAME_MS / 1000);
			run.wanted = false;
			applyFast(i); /* the off to the device */
			emit fastStreamNote(i, tr("fast stream %1 switched off: %2").arg(run.def.name, why), true);
			continue;
		}
		if (run.lastBlockMs < 0 || now - run.lastBlockMs < FIRST_FRAME_MS || run.silenceAsked || !run.enable()) continue;
		run.silenceAsked = true;
		const quint64 request = run.request;
		const QString name = run.def.name;
		master_->readFrom(target(0), run.enableReg.addr, uint16_t(run.enableReg.size), [this, i, name, request](const evre::Result &r) {
			FastRun *asked = fastRunAsked(i, name, request);
			if (!asked) return;
			FastRun &run = *asked;
			if (!run.on || !r.ok || r.data.size() != run.enableReg.size) return;
			if (decodeNumber(run.enableReg, r.data) != 0) return; /* still on: a slow stream, or a pause */
			run.wanted = run.on = run.deviceMaySend = false;
			run.request++;
			if (!anyFastOn() && !autoSendOn_) heartbeatTimer_->stop();
			emit fastStreamNote(i, tr("the device stopped fast stream %1 (%2 reads 0: a reset?)").arg(run.def.name,
					run.enableReg.name), true);
		});
	}
}

/* Before the link closes: every stream that may be sending told 0, in a WRITE sent straight on the link and flushed,
 * the link drained, as AUTO_SEND's off */
void IoEngine::sendFastOffs() {
	bool sent = false;
	for (FastRun &run : fastRuns_) {
		if (!run.deviceMaySend || !run.enable()) continue;
		QByteArray zero;
		QString why;
		if (!encodeValue(run.enableReg, QStringLiteral("0"), zero, why)) continue;
		link_->send(evre::build(target(0), evre::WRITE, run.enableReg.addr, uint16_t(zero.size()), zero));
		run.deviceMaySend = false;
		sent = true;
	}
	if (!sent) return;
	link_->flush(200);
	link_->drain(100);
}

/* ---------------------------------------------------------------------- CSV */

/* the columns: time_s, datetime, then each register asked for that the map has */
void IoEngine::startRecord(const QString &file, const QVector<RegKey> &cols) {
	stopRecord();
	csvFile_->setFileName(file);
	if (!csvFile_->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		emit recordStarted(false, csvFile_->errorString());
		return;
	}
	csvStream_.setDevice(csvFile_);
	csvColumns_.clear();
	csvStream_ << "time_s,datetime";
	for (RegKey key : cols) {
		const int row = rowOfKey_.value(key, -1);
		if (row < 0) continue;
		csvColumns_ << key;
		csvStream_ << ',' << csvTitle(table_.rows()[row].def);
	}
	csvStream_ << '\n';
	csvRows_ = 0;
	updateStats();
	emit recordStarted(true, QString());
}

/* a register with no good value yet, or of an offline device, leaves its cell empty */
void IoEngine::writeCsvRow(double t) {
	if (!csvFile_->isOpen()) return;
	const auto &rows = table_.rows();
	csvStream_ << QString::number(t, 'f', 6) << ',' << QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
	for (RegKey key : csvColumns_) {
		csvStream_ << ',';
		const int row = rowOfKey_.value(key, -1);
		if (row >= 0 && rows[row].valid && !offline_.contains(rows[row].def.slave)) csvStream_ << csvCell(rows[row]);
	}
	csvStream_ << '\n';
	csvRows_++;
}

void IoEngine::stopRecord() {
	for (int i = 0; i < fastRuns_.size(); i++) { /* the streams' files beside it */
		FastRun &run = fastRuns_[i];
		run.writeFailed = false;
		if (!run.writer) continue;
		const qint64 bytes = run.writer->bytes();
		run.writer->close();
		run.writer.reset();
		emit fastRecorded(tr("fast stream %1 recorded: %2 (%3 blocks, %4 MB)").arg(run.def.name,
				fast::recordingFileFor(csvFile_->fileName(), run.def.name)).arg(run.recordedBlocks)
				.arg(double(bytes) / (1024 * 1024), 0, 'f', 1));
	}
	if (!csvFile_->isOpen()) return;
	csvStream_.flush();
	const QString file = csvFile_->fileName();
	csvFile_->close();
	updateStats();
	emit recordStopped(file, csvRows_);
}

/* ----------------------------------------------- writes and reads asked for */

void IoEngine::write(quint64 id, uint8_t slave, uint16_t addr, const QByteArray &bytes, bool ack) {
	if (!connected_) {
		emit writeDone(id, false, tr("not connected"));
		return;
	}
	if (!ack) {
		master_->writeNoAckTo(slave, addr, bytes, [this, id](const evre::Result &r) { emit writeDone(id, r.ok, r.message); });
		return;
	}
	/* with one device, a write to another slave (the Monitor) is not that device's: no read back into the table */
	const bool ours = isBus() || slave == master_->slave();
	master_->writeTo(slave, addr, bytes, [this, id, ours, table = tableSlave(slave), addr, size = int(bytes.size())](
			const evre::Result &r) {
		emit writeDone(id, r.ok, r.message);
		if (r.ok && ours) readBack(table, addr, size);
	});
}

/* after a write, what the device holds now (it may have kept another value) */
void IoEngine::readBack(uint8_t tableSlave, uint16_t addr, int writtenSize) {
	const RegKey key = regKey(tableSlave, addr);
	const int row = rowOfKey_.value(key, -1);
	const uint16_t count = uint16_t(row >= 0 ? table_.rows()[row].def.size : writtenSize);
	master_->readFrom(target(tableSlave), addr, count, [this, key](const evre::Result &r) {
		const int row = rowOfKey_.value(key, -1); /* the map may have changed meanwhile */
		if (row >= 0 && r.ok) table_.setRaw(row, r.data);
	});
}

void IoEngine::read(quint64 id, uint8_t slave, uint16_t addr, uint16_t count, bool intoTable) {
	if (!connected_) {
		emit readDone(id, false, {}, tr("not connected"), 0);
		return;
	}
	const bool ours = intoTable && (isBus() || slave == master_->slave());
	master_->readFrom(slave, addr, count, [this, id, key = regKey(tableSlave(slave), addr), ours](const evre::Result &r) {
		const int row = ours ? rowOfKey_.value(key, -1) : -1;
		if (row >= 0) {
			if (r.ok) table_.setRaw(row, r.data);
			else table_.setError(row, r.message, false);
		}
		emit readDone(id, r.ok, r.data, r.message, r.latencyMs);
	});
}

/* ---------------------------------------------------------------------- API */

void IoEngine::apiStart(quint16 evrePort, quint16 jsonPort, bool network) {
	QString err;
	const bool ok = api_->start(evrePort, jsonPort, network, err);
	updateStats();
	emit apiStarted(ok, err);
}

void IoEngine::apiStop() {
	api_->stop();
	updateStats();
}

void IoEngine::apiSetWrites(bool writes, bool danger) {
	api_->setAllowWrites(writes);
	api_->setAllowDanger(writes && danger);
}

void IoEngine::shutdown() {
	stopRecord();
	api_->stop();
	disconnectLink();
	stopTicker();
	statsTimer_->stop();
}

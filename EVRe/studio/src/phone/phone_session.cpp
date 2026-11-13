/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's session: see phone_session.h. What it repeats of MainWindow is named at each place. */
#include "phone/phone_session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVariantMap>
#include <cmath>
#include <memory>
#include <utility>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

#include "evre/frame.h"
#include "evre/link_android.h"
#include "evre/registers.h"
#include "io/engine.h"
#include "model/register_model.h"
#include "phone/phone_app.h"

namespace {

/* the phone's own keys: the phone program on a desktop shares the desktop's settings, whose link keys it must not
 * change (the theme and the language are shared on purpose) */
const QString KEY_TCP = QStringLiteral("phone/tcp");
const QString KEY_HOST = QStringLiteral("phone/host");
const QString KEY_PORT = QStringLiteral("phone/port");
const QString KEY_USB = QStringLiteral("phone/usbDevice");
const QString KEY_BAUD = QStringLiteral("phone/baud");
const QString KEY_RECONNECT = QStringLiteral("phone/reconnect");
const QString KEY_POLLING = QStringLiteral("phone/polling");
const QString KEY_INTERVAL = QStringLiteral("phone/interval");
const QString KEY_MAP = QStringLiteral("phone/map");

const QString FIRST_MAP = QStringLiteral("example_device.json");

evre::UsbBridge *testBridge = nullptr;

} // namespace

PhoneSession::PhoneSession(QObject *parent)
	: QObject(parent), model_(new RegisterModel(this)), ports_(new PortList(this)), log_(new LogModel(this)) {
	const QSettings settings;
	tcp_ = settings.value(KEY_TCP, true).toBool();
	host_ = settings.value(KEY_HOST, QStringLiteral("127.0.0.1")).toString();
	port_ = settings.value(KEY_PORT, 1210).toInt();
	usbDevice_ = settings.value(KEY_USB).toString();
	baud_ = settings.value(KEY_BAUD, 115200).toInt();
	autoReconnect_ = settings.value(KEY_RECONNECT, true).toBool();
	polling_ = settings.value(KEY_POLLING, true).toBool();
	intervalMs_ = settings.value(KEY_INTERVAL, 100).toInt();
	linkText_ = tr("Not connected");
	deviceText_ = tr("Device ID not read yet");
	list_ = new RegisterList(model_, this);
	frames_ = new FrameLog(this);
	connect(frames_, &FrameLog::pausedChanged, this, &PhoneSession::pushMonitor);
	valuePacer_.setPerSecond(ValuePace::saved());
	valueClock_.start();
	sinceSync_.start();
	syncTimer_.setInterval(SYNC_IDLE_MS);
	connect(&syncTimer_, &QTimer::timeout, this, [this] {
		if (sinceSync_.elapsed() >= SYNC_IDLE_MS) sync();
	});
	idTimer_.setSingleShot(true);
	idTimer_.setInterval(ID_RETRY_MS);
	connect(&idTimer_, &QTimer::timeout, this, &PhoneSession::retryDeviceId);
	/* MainWindow's words for a refused Plot, and for lines a lower limit took off */
	connect(model_, &RegisterModel::plotLimitReached, this, [this] {
		const long rate = std::lround(sampleRateHz()); /* 0: not polling, no rate to name */
		emit notice(rate > 0 ? tr("At most %1 registers on the chart at %2 samples a second: take one off first")
							.arg(model_->plotLimit()).arg(rate)
						: tr("At most %1 registers on the chart: take one off first").arg(model_->plotLimit()));
	});
	connect(model_, &RegisterModel::plotChanged, this, &PhoneSession::onPlotChanged);
	connect(model_, &RegisterModel::plotsTakenOff, this, [this](const QStringList &names) {
		addLog(LogModel::Warning, tr("%1 samples a second: at most %2 registers on the chart; taken off: %3")
				.arg(std::lround(sampleRateHz())).arg(model_->plotLimit()).arg(names.join(QStringLiteral(", "))));
	});

	startEngine();
	reconnectTimer_.setSingleShot(true);
	connect(&reconnectTimer_, &QTimer::timeout, this, &PhoneSession::retryConnect);
	statsTimer_.setInterval(STATS_MS);
	connect(&statsTimer_, &QTimer::timeout, this, &PhoneSession::refreshStats);
	statsTimer_.start();

	/* the map opened last; else (or when it is gone) the app's first */
	findMaps();
	const QString saved = settings.value(KEY_MAP).toString();
	if ((saved.isEmpty() || !loadMap(saved)) && !mapFiles_.isEmpty()) loadMap(mapFiles_.first());
	pushPolling();
	refreshPorts();
}

/* the engine closes the link and a recording on its thread, then the thread ends (MainWindow::~MainWindow) */
PhoneSession::~PhoneSession() {
	reconnectTimer_.stop();
	if (chart_) chart_->setClock({}); /* its clock is the engine's, which goes now */
	if (thread_ && thread_->isRunning()) {
		QMetaObject::invokeMethod(engine_, [engine = engine_] { engine->shutdown(); }, Qt::BlockingQueuedConnection);
		thread_->quit();
		thread_->wait(3000);
	}
}

/* MainWindow::startEngine: the phone's part of it (no API, no auto send, no fast streams yet) */
void PhoneSession::startEngine() {
	thread_ = new QThread(this);
	thread_->setObjectName(QStringLiteral("evre-io"));
	engine_ = new IoEngine;
	engine_->moveToThread(thread_);
	connect(thread_, &QThread::finished, engine_, &QObject::deleteLater);
	thread_->start(QThread::HighPriority);
	connect(engine_, &IoEngine::opened, this, &PhoneSession::onOpened);
	connect(engine_, &IoEngine::closed, this, &PhoneSession::onClosed);
	connect(engine_, &IoEngine::linkWaiting, this, [this] {
		if (wantConnected_ && !tcp_)
			setState(Waiting, tr("Waiting for permission…"),
					tr("Android asks whether EVRe Studio may use %1: answer its dialog.").arg(usbDevice_));
	});
	connect(engine_, &IoEngine::deviceInfo, this, &PhoneSession::onDeviceInfo);
	connect(engine_, &IoEngine::loginRefused, this, [this](int, const QString &err) {
		setDevice(-1, tr("token refused: %1").arg(err));
		addLog(LogModel::Error, tr("token refused: %1").arg(err));
		loginStop_ = tr("token refused: type the right token, then Connect");
	});
	connect(engine_, &IoEngine::loginNeeded, this, [this](int) {
		addLog(LogModel::Error, tr("the device wants a login: its map has a login register, and no token was given"));
		loginStop_ = tr("the device wants a token: type it in the Token box, then Connect");
	});
	connect(engine_, &IoEngine::loginSkipped, this, [this] {
		addLog(LogModel::Warning, tr("the map declares no login register: the token was not sent"));
	});
	connect(engine_, &IoEngine::recordStarted, this, &PhoneSession::onRecordStarted);
	connect(engine_, &IoEngine::readDone, this,
			[this](quint64 id, bool ok, const QByteArray &data, const QString &err, double) { onReadDone(id, ok, data, err); });
	connect(engine_, &IoEngine::writeDone, this, &PhoneSession::onWriteDone);
	connect(engine_, &IoEngine::recordStopped, this, &PhoneSession::onRecordStopped);
	/* Fast EVRe: what a stream did, in the Log (MainWindow::onFastStreamSet's words, shorter) */
	connect(engine_, &IoEngine::fastStreamSet, this, [this](int stream, bool on, double rate, const QString &err) {
		if (stream < 0 || stream >= map_.streams.size()) return;
		const QString name = map_.streams[stream].name;
		if (!err.isEmpty())
			addLog(LogModel::Error, (on ? tr("fast stream %1 not switched on: %2") : tr("fast stream %1 not switched off: %2"))
					.arg(name, err));
		else if (on)
			addLog(LogModel::Info, tr("fast stream %1 on: %2 records a second").arg(name).arg(std::lround(rate)));
		else
			addLog(LogModel::Info, tr("fast stream %1 off").arg(name));
	});
	connect(engine_, &IoEngine::fastStreamNote, this, [this](int, const QString &text, bool) {
		addLog(LogModel::Warning, text);
	});
}

/* ------------------------------------------------------------------- the map */

/* the maps that come with the app, by their devices' names; example_device.json first. A file that is no map (a bus)
 * is left out. */
void PhoneSession::findMaps() {
	const QDir folder(phone::mapsFolder());
	QStringList files = folder.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
	if (files.removeOne(FIRST_MAP)) files.prepend(FIRST_MAP);
	for (const QString &name : files) {
		DeviceMap map;
		QString err;
		const QString file = folder.filePath(name);
		if (!map.load(file, err)) continue;
		mapNames_ << map.device;
		mapFiles_ << file;
	}
}

bool PhoneSession::loadMap(const QString &file) {
	const QUrl url(file);
	const QString path = url.isLocalFile() ? url.toLocalFile() : file;
	DeviceMap loaded;
	QString err;
	if (!loaded.load(path, err)) {
		mapError_ = tr("map not loaded: %1: %2").arg(QDir::toNativeSeparators(path), err);
		addLog(LogModel::Error, mapError_);
		emit mapChanged();
		return false;
	}
	/* the streams of the map before whose lines are on the chart: off (the engine keeps a wish for a stream of the same
	 * name in the next map) */
	for (int i = 0; chart_ && i < map_.streams.size(); ++i)
		for (int c = 0; c < map_.streams[i].channels.size(); ++c)
			if (chart_->hasLine(fastLineId(i, c))) {
				engine_->post([engine = engine_, i] { engine->setFastStream(i, false); });
				break;
			}
	map_ = loaded;
	mapFile_ = path;
	mapError_.clear();
	QSettings().setValue(KEY_MAP, path);
	model_->setDefinitions({}); /* another map: nothing of the one before stays */
	if (chart_) chart_->removeAll(); /* its fast lines too */
	model_->setDefinitions(map_.regs);
	resetStreams();
	addLog(LogModel::Info, tr("map loaded: %1 (%2 registers)").arg(QDir::toNativeSeparators(path)).arg(map_.regs.size()));
	pushMap();
	pushLinkOptions();
	refreshPorts(); /* the map's USB IDs name its device in the list */
	if (deviceId_ >= 0) setDevice(deviceId_, deviceText_, mapWarning(quint16(deviceId_)));
	emit mapChanged();
	return true;
}

QString PhoneSession::mapText() const { return tr("registers: %1").arg(map_.regs.size()); }

/* MainWindow::pushMap for one device: its login register and its map */
void PhoneSession::pushMap() {
	const QVector<RegDef> defs = model_->definitions();
	const quint64 generation = ++mapGeneration_;
	EngineDevice device;
	device.name = map_.device;
	device.loginAddr = map_.loginAddr;
	device.loginSize = map_.loginSize;
	device.map = std::make_shared<const DeviceMap>(map_);
	const QVector<EngineDevice> devices{ device };
	const QString name = map_.device;
	copiedVersions_.fill(quint64(-1), defs.size()); /* every row copied again from the new table */
	engine_->post([engine = engine_, defs, generation, name, devices] { engine->setMap(defs, generation, name, devices); });
	pushPlotted();
}

/* the map's slave, the desktop's answer time and In flight (4 on TCP, 1 on a serial line) */
void PhoneSession::pushLinkOptions() {
	const int slave = map_.slave, inFlight = tcp_ ? 4 : 1;
	engine_->post([engine = engine_, slave, inFlight] { engine->setLinkOptions(slave, TIMEOUT_MS, inFlight); });
}

void PhoneSession::pushPolling() {
	const bool on = polling_;
	const double interval = intervalMs_;
	engine_->post([engine = engine_, on, interval] { engine->setPolling(on, interval); });
	updatePlotLimit();
	updateStaleAfter();
}

/* MainWindow::sampleRateHz without auto send: the polls a second asked for, or measured at "as fast as possible" */
double PhoneSession::sampleRateHz() const {
	if (!polling_) return 0;
	return intervalMs_ > 0 ? 1000.0 / intervalMs_ : pollHz_;
}

/* MainWindow::updatePlotLimit: the registers on the chart, as many as the rate allows */
void PhoneSession::updatePlotLimit() { model_->setPlotLimit(RegisterModel::plotLimitFor(sampleRateHz())); }

/* MainWindow::updateStaleAfter: two poll intervals, never less than a slow answer, plus the time between two copies */
void PhoneSession::updateStaleAfter() {
	const int perSecond = valuePacer_.perSecond();
	const qint64 copyGapMs = perSecond > 0 ? 1000 / perSecond : 0;
	model_->setStaleAfterMs(copyGapMs + std::max<qint64>(qint64(2 * intervalMs_), qint64(intervalMs_) + TIMEOUT_MS));
}

/* ------------------------------------------------------------------ settings */

void PhoneSession::setTcp(bool tcp) {
	if (tcp == tcp_) return;
	tcp_ = tcp;
	QSettings().setValue(KEY_TCP, tcp);
	if (!tcp) refreshPorts();
	emit settingsChanged();
}

QVariantList PhoneSession::baudRates() const {
	return { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000 };
}

void PhoneSession::setAutoReconnect(bool on) {
	if (on == autoReconnect_) return;
	autoReconnect_ = on;
	QSettings().setValue(KEY_RECONNECT, on);
	/* off while a try waits: no try comes, the link says why it is down */
	if (!on && state_ == Reconnecting) {
		reconnectTimer_.stop();
		wantConnected_ = false;
		setState(Closed, linkDetail_.isEmpty() ? tr("Not connected") : linkDetail_);
	}
	emit settingsChanged();
}

void PhoneSession::setPolling(bool on) {
	if (on == polling_) return;
	polling_ = on;
	QSettings().setValue(KEY_POLLING, on);
	pushPolling();
	if (!on) pollHz_ = 0;
	emit settingsChanged();
	emit statsChanged();
}

/* 0: as fast as possible, the next poll when one ends (the desktop's max) */
void PhoneSession::setIntervalMs(int ms) {
	ms = std::clamp(ms, 0, 60000);
	if (ms == intervalMs_) return;
	intervalMs_ = ms;
	QSettings().setValue(KEY_INTERVAL, ms);
	pushPolling();
	emit settingsChanged();
}

QString PhoneSession::pollText() const {
	if (!connected_ || !polling_) return QString();
	return tr("polls a second: %1").arg(pollHz_, 0, 'f', 1);
}

bool PhoneSession::writesEnabled() const { return model_->writesEnabled(); }

void PhoneSession::setWritesEnabled(bool on) {
	if (on == model_->writesEnabled()) return;
	model_->setWritesEnabled(on);
	addLog(LogModel::Info, on ? tr("writes allowed") : tr("writes off"));
	emit settingsChanged();
}

/* ------------------------------------------------------------------- the link */

evre::UsbBridge *PhoneSession::usbBridge() {
	if (testBridge) return testBridge;
#ifdef Q_OS_ANDROID
	return evre::UsbBridge::system();
#else
	return nullptr;
#endif
}

void PhoneSession::setUsbBridgeForTests(evre::UsbBridge *bridge) { testBridge = bridge; }

void PhoneSession::refreshPorts() { ports_->refresh(usbBridge(), map_); }

void PhoneSession::connectUsb(const QString &device, int baud) {
	if (device.isEmpty()) {
		setState(Closed, tr("No USB device: plug the board in, then choose it"));
		return;
	}
	tcp_ = false;
	usbDevice_ = device;
	baud_ = baud > 0 ? baud : 115200;
	QSettings settings;
	settings.setValue(KEY_TCP, false);
	settings.setValue(KEY_USB, usbDevice_);
	settings.setValue(KEY_BAUD, baud_);
	emit settingsChanged();
	wantConnected_ = true;
	connectLink();
}

void PhoneSession::connectTcp(const QString &host, int port, const QString &token) {
	const QString name = host.trimmed();
	if (name.isEmpty() || port < 1 || port > 65535) {
		setState(Closed, tr("Type the device's host and a port from 1 to 65535"));
		return;
	}
	tcp_ = true;
	host_ = name;
	port_ = port;
	token_ = token; /* never saved */
	QSettings settings;
	settings.setValue(KEY_TCP, true);
	settings.setValue(KEY_HOST, host_);
	settings.setValue(KEY_PORT, port_);
	emit settingsChanged();
	wantConnected_ = true;
	connectLink();
}

/* MainWindow::connectLink for TCP and the phone's USB; a try by itself is not said again in the Log */
void PhoneSession::connectLink() {
	reconnectTimer_.stop();
	loginStop_.clear(); /* a new try: its own login */
	connected_ = false;
	pushLinkOptions();
	pushPolling();
	if (tcp_) {
		const QString host = host_, token = token_;
		const quint16 port = quint16(port_);
		if (!retrying_)
			addLog(LogModel::Info, tr("connecting to %1:%2%3").arg(host).arg(port)
					.arg(token.isEmpty() ? QString() : tr(" (with a token)")));
		engine_->post([engine = engine_, host, port, token] { engine->connectTcp(host, port, token); });
		setState(Connecting, tr("Connecting to %1:%2…").arg(host).arg(port));
	} else {
		const QString device = usbDevice_;
		const qint32 baud = baud_;
		evre::UsbBridge *bridge = usbBridge();
		if (!retrying_) addLog(LogModel::Info, tr("opening %1 at %2").arg(device).arg(baud));
		setState(Connecting, tr("Connecting to %1…").arg(device));
		engine_->post([engine = engine_, device, baud, bridge] { engine->connectUsb(device, baud, bridge); });
	}
}

void PhoneSession::retryConnect() {
	if (!wantConnected_ || connected_) return;
	retrying_ = true;
	connectLink();
	retrying_ = false;
}

void PhoneSession::disconnectLink() {
	const bool wasUp = connected_;
	wantConnected_ = false;
	reconnectTimer_.stop();
	engine_->post([engine = engine_] { engine->disconnectLink(); });
	connected_ = false;
	linkDown();
	markDeviceLost();
	if (wasUp) addLog(LogModel::Info, tr("disconnected"));
	setState(Off, tr("Not connected"));
	emit statsChanged();
}

void PhoneSession::onOpened(const QString &link) {
	if (!wantConnected_) return; /* stopped meanwhile */
	connected_ = true;
	idTries_ = 0;
	model_->setLinkUp(true);
	syncTimer_.start();
	addLog(LogModel::Info, tr("connected: %1").arg(link));
	setDevice(-1, tr("Device ID not read yet"));
	setState(Connected, tr("Connected: %1").arg(link));
	emit statsChanged(); /* the polls' line shows from now */
}

/* MainWindow::onClosed: said in the Log, then tried again, or not. A refused token, a device that wants one and a
 * refused USB permission stop the tries: the same try would be refused again (and the permission asked again every
 * 2 s) */
void PhoneSession::onClosed(const QString &why, const QString &detail) {
	const bool wasUp = connected_;
	connected_ = false;
	linkDown();
	if (!why.isEmpty()) {
		const QString both = detail.isEmpty() ? why : QStringLiteral("%1 (%2)").arg(why, detail);
		addLog(wasUp ? LogModel::Error : LogModel::Warning,
				(wasUp ? tr("connection lost: %1") : tr("not connected: %1")).arg(both));
	} else if (wasUp) {
		addLog(LogModel::Info, tr("disconnected"));
	}
	markDeviceLost();
	emit statsChanged();
	const QString loginStop = std::exchange(loginStop_, QString());
	const bool permissionRefused = !tcp_ && why == evre::AndroidUsbLink::refusedText(usbDevice_);
	if (!loginStop.isEmpty() && !why.isEmpty()) {
		wantConnected_ = false;
		setState(Closed, loginStop, why);
		addLog(LogModel::Info, tr("not reconnecting: %1").arg(loginStop));
	} else if (permissionRefused) {
		wantConnected_ = false;
		setState(Closed, why, tr("Connect asks again"));
		addLog(LogModel::Info, tr("not reconnecting: %1").arg(why));
	} else if (wantConnected_ && autoReconnect_) {
		reconnectTimer_.start(wasUp ? RECONNECT_LOST_MS : RECONNECT_FAILED_MS);
		setState(Reconnecting, tr("Reconnecting…"), why);
	} else {
		wantConnected_ = false;
		if (why.isEmpty()) setState(Off, tr("Not connected"));
		else setState(Closed, why, detail);
	}
}

/* MainWindow::onDeviceInfo for one device: the drawer's line, and the Log */
void PhoneSession::onDeviceInfo(int, bool ok, quint16 id, quint16 status, const QString &err) {
	idRead_ = 0;
	/* no answer (a board still booting when the link came back): asked again, a refusal is the token's (loginNeeded) */
	if (!ok && connected_ && idTries_ < ID_RETRIES && !err.contains(evre::errorName(3))) {
		setDevice(-1, tr("reading the device ID…"));
		idTimer_.start();
		return;
	}
	if (!ok) {
		setDevice(-1, tr("DEVICE_ID not read: %1").arg(err));
		addLog(LogModel::Error, tr("DEVICE_ID not read: %1").arg(err));
		return;
	}
	revision_ = status & 0xFF;
	setDevice(id, tr("Device ID %1 · protocol rev %2").arg(addrText(id)).arg(revision_), mapWarning(id));
	addLog(LogModel::Info, tr("device ID %1, protocol revision %2").arg(addrText(id)).arg(revision_));
	if (map_.deviceId && id != map_.deviceId)
		addLog(LogModel::Warning, tr("the map is for device %1, this is %2").arg(addrText(map_.deviceId), addrText(id)));
}

/* the map's own device ID against the device's, so another map chosen shows at once */
QString PhoneSession::mapWarning(quint16 id) const {
	return map_.deviceId && id != map_.deviceId ? tr("the map is for %1").arg(addrText(map_.deviceId)) : QString();
}

/* a DEVICE_ID read again, as the engine reads it when the link opens; not once a token stopped the link */
void PhoneSession::retryDeviceId() {
	if (!connected_ || !loginStop_.isEmpty()) return;
	++idTries_;
	idRead_ = nextRequest_++;
	const quint64 id = idRead_;
	const uint8_t slave = requestSlave(0, uint8_t(map_.slave));
	engine_->post([engine = engine_, id, slave] { engine->read(id, slave, evre::DEVICE_ID, 4, false); });
}

void PhoneSession::onReadDone(quint64 id, bool ok, const QByteArray &data, const QString &err) {
	if (id == 0 || id != idRead_) return;
	if (ok && data.size() >= 4) onDeviceInfo(0, true, evre::littleEndian16(data, 0), evre::littleEndian16(data, 2), QString());
	else onDeviceInfo(0, false, 0, 0, ok ? tr("a short answer") : err);
}

/* the link went down: no value is new from here (RegisterModel greys them as time passes), no ID read is to come */
void PhoneSession::linkDown() {
	idTimer_.stop();
	idRead_ = 0;
	syncTimer_.stop();
	model_->setLinkUp(false);
}

/* the link down: the ID read is kept, said to be old (MainWindow::markDeviceInfoLost) */
void PhoneSession::markDeviceLost() {
	if (deviceId_ < 0) return;
	setDevice(-1, tr("last seen: Device ID %1 · protocol rev %2").arg(addrText(quint16(deviceId_))).arg(revision_));
}

void PhoneSession::setState(State state, const QString &text, const QString &detail) {
	if (state == state_ && text == linkText_ && detail == linkDetail_) return;
	state_ = state;
	linkText_ = text;
	linkDetail_ = detail;
	emit linkChanged();
}

void PhoneSession::setDevice(int id, const QString &text, const QString &warning) {
	if (id == deviceId_ && text == deviceText_ && warning == deviceWarning_) return;
	deviceId_ = id;
	deviceText_ = text;
	deviceWarning_ = warning;
	emit deviceChanged();
}

QString PhoneSession::pillText() const {
	switch (state_) {
	case Waiting: return tr("Waiting for permission…");
	case Connecting: return tr("Connecting…");
	case Connected: return tr("Connected");
	case Reconnecting: return tr("Reconnecting…");
	case Off:
	case Closed: break;
	}
	return tr("Not connected");
}

QString PhoneSession::connectText() const {
	switch (state_) {
	case Connected: return tr("Disconnect");
	case Waiting:
	case Connecting: return tr("Cancel");
	case Reconnecting: return tr("Stop reconnecting");
	case Off:
	case Closed: break;
	}
	return tr("Connect");
}

void PhoneSession::refreshStats() {
	if (!connected_) return;
	const double hz = polling_ ? engine_->stats().pollHz : 0.0;
	if (hz == pollHz_) return;
	pollHz_ = hz;
	if (intervalMs_ == 0) updatePlotLimit(); /* as fast as possible: the measured rate sets the limit */
	emit statsChanged();
}

void PhoneSession::addLog(LogModel::Level level, const QString &text) {
	log_->add(level, text);
	emit message(int(level), text);
}

/* ---------------------------------------------------------------- recording */

namespace {
QString ownDataFolder; /* setDataFolder's */
}

void PhoneSession::setDataFolder(const QString &folder) { ownDataFolder = folder; }

QString PhoneSession::recordingsFolder() {
	if (!ownDataFolder.isEmpty()) return ownDataFolder + QStringLiteral("/recordings");
	QString base;
#ifdef Q_OS_ANDROID
	const QJniObject context = QNativeInterface::QAndroidApplication::context();
	const QJniObject dir = context.callObjectMethod("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;",
			static_cast<jstring>(nullptr));
	if (dir.isValid()) base = dir.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString();
#endif
	if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	return base + QStringLiteral("/recordings");
}

/* MainWindow::startRecord: every register ticked Log that a poll reads, into a new file named by the time */
void PhoneSession::startRecord() {
	if (recording_) return;
	const QString folder = recordingsFolder();
	QDir().mkpath(folder);
	const QString file = folder + QStringLiteral("/evre_%1.csv")
			.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
	QVector<RegKey> columns;
	for (const RegisterModel::Row &row : model_->rows()) {
		if (!row.log || row.unavailable || !isPollable(row.def)) continue;
		columns << regKey(row.def);
	}
	recordFile_ = file;
	engine_->post([engine = engine_, file, columns] { engine->startRecord(file, columns); });
}

void PhoneSession::stopRecord() {
	engine_->post([engine = engine_] { engine->stopRecord(); });
}

void PhoneSession::onRecordStarted(bool ok, const QString &err) {
	if (!ok) {
		addLog(LogModel::Error, tr("CSV recording not started: %1").arg(err));
		recordText_ = tr("Not recording: %1").arg(err);
		emit recordingChanged();
		return;
	}
	recording_ = true;
	addLog(LogModel::Info, tr("CSV recording started: %1").arg(QDir::toNativeSeparators(recordFile_)));
	recordText_ = tr("Recording to %1").arg(QFileInfo(recordFile_).fileName());
	emit recordingChanged();
}

void PhoneSession::onRecordStopped(const QString &file, quint64 rows) {
	recording_ = false;
	recordText_ = tr("Saved %1 · rows: %2").arg(QFileInfo(file).fileName()).arg(rows);
	addLog(LogModel::Info, tr("CSV recording stopped: %1 rows in %2").arg(rows).arg(QDir::toNativeSeparators(file)));
	emit recordingChanged();
}

/* ------------------------------------------------------------ the registers */

void PhoneSession::attachWindow(QQuickWindow *window) {
	if (window_) disconnect(window_, nullptr, this, nullptr);
	window_ = window;
	/* on the window's thread, before each frame's scene is synced: the values the frame will show */
	if (window_) connect(window_, &QQuickWindow::afterAnimating, this, &PhoneSession::sync);
}

/* MainWindow::sync: the values, only the rows that moved since the last copy, and only at their pace (a number that
 * changes at every frame cannot be read); the chart's samples and fast blocks at every frame. The Monitor's frames
 * come with its screen. */
void PhoneSession::sync() {
	sinceSync_.restart();
	if (!connected_) return;
	syncChart();
	int dropped = 0;
	const QStringList frames = engine_->takeFrames(dropped);
	frames_->add(frames, dropped);
	if (!valuePacer_.due(valueClock_.elapsed())) return;
	QVector<RegValue> values;
	quint64 generation = 0;
	qint64 nowMs = 0;
	engine_->table().snapshot(values, generation, nowMs);
	if (generation != mapGeneration_ || values.size() != model_->rows().size()) return;
	if (copiedVersions_.size() != values.size()) copiedVersions_.fill(quint64(-1), values.size());
	for (int row = 0; row < values.size(); row++) {
		const RegValue &value = values[row];
		if (copiedVersions_[row] == value.version) continue;
		copiedVersions_[row] = value.version;
		model_->applyValue(row, value.raw, value.valid, value.error, value.unavailable,
				value.valid ? nowMs - value.updatedMs : 0);
	}
}

uint8_t PhoneSession::slaveOf(const RegDef &def) const { return requestSlave(def.slave, uint8_t(map_.slave)); }

QString PhoneSession::editText(int row) const {
	if (row < 0 || row >= model_->rows().size()) return QString();
	return model_->data(model_->index(row, RegisterModel::ColValue), Qt::EditRole).toString();
}

QVariantList PhoneSession::choices(int row) const {
	QVariantList list;
	if (row < 0 || row >= model_->rows().size()) return list;
	const RegDef &def = model_->rows()[row].def;
	for (auto it = def.enumValues.constBegin(); it != def.enumValues.constEnd(); ++it)
		list << QVariantMap{ { QStringLiteral("text"), QStringLiteral("%1 (%2)").arg(it.value()).arg(it.key()) },
			{ QStringLiteral("value"), QString::number(it.key()) } };
	return list;
}

QString PhoneSession::limitsText(int row) const {
	if (row < 0 || row >= model_->rows().size()) return QString();
	const RegDef &def = model_->rows()[row].def;
	const QString unit = def.unit.isEmpty() ? QString() : QStringLiteral(" ") + def.unit;
	auto number = [&](double limit) { return formatNumber(def, limit) + unit; };
	if (def.hasMin() && def.hasMax()) return tr("from %1 to %2").arg(formatNumber(def, def.min), number(def.max));
	if (def.hasMin()) return tr("at least %1").arg(number(def.min));
	if (def.hasMax()) return tr("at most %1").arg(number(def.max));
	return QString();
}

/* MainWindow::onWriteRequested's questions, in words for the sheet: past the map's min or max (a special value is
 * always allowed), then a danger register */
QString PhoneSession::writeQuestion(int row, const QString &text) const {
	if (row < 0 || row >= model_->rows().size()) return QString();
	const RegDef &def = model_->rows()[row].def;
	QByteArray bytes;
	QString err;
	if (!encodeValue(def, text, bytes, err)) return QString(); /* write() says why */
	QStringList asked;
	const QString outside = def.isNumeric() ? limitProblem(def, decodeNumber(def, bytes)) : QString();
	if (!outside.isEmpty())
		asked << tr("%1 = %2 is %3 the map gives it. The map may be stricter than the device, or the device may not take "
					"it.").arg(def.name, text.trimmed(), outside);
	if (def.danger) asked << tr("%1 moves, powers or changes something on the device.").arg(def.name);
	if (asked.isEmpty()) return QString();
	return asked.join(QLatin1Char(' ')) + QLatin1Char(' ') + tr("Write it anyway?");
}

/* MainWindow::onWriteRequested: the text into the register's bytes, written; the engine reads it back */
void PhoneSession::write(int row, const QString &text) {
	if (row < 0 || row >= model_->rows().size()) return;
	const RegDef def = model_->rows()[row].def;
	if (!model_->writesEnabled()) {
		emit writeAnswered(row, false, tr("Writes are off: turn on Allow writes in the Connect drawer"));
		return;
	}
	if (!connected_) {
		emit writeAnswered(row, false, tr("Not connected: connect first"));
		return;
	}
	QByteArray bytes;
	QString err;
	if (!encodeValue(def, text, bytes, err)) {
		const QString line = tr("%1: %2 not written: %3").arg(def.name, text, err);
		addLog(LogModel::Error, line);
		emit writeAnswered(row, false, line);
		return;
	}
	const quint64 id = nextRequest_++;
	writes_.insert(id, { row, def.name, tr("%1 = %2 (%3 at %4)").arg(def.name, text.trimmed(), evre::hex(bytes), addrText(def.addr)) });
	const uint8_t slave = slaveOf(def);
	const uint16_t addr = def.addr;
	engine_->post([engine = engine_, id, slave, addr, bytes] { engine->write(id, slave, addr, bytes, true); });
}

void PhoneSession::onWriteDone(quint64 id, bool ok, const QString &err) {
	const auto it = writes_.constFind(id);
	if (it == writes_.constEnd()) return;
	const PendingWrite done = *it;
	writes_.erase(it);
	if (ok) {
		addLog(LogModel::Info, tr("written: %1").arg(done.what));
		emit writeAnswered(done.row, true, tr("%1 written").arg(done.name));
	} else {
		const QString line = tr("write refused: %1: %2").arg(done.what, err);
		addLog(LogModel::Error, line);
		emit writeAnswered(done.row, false, line);
	}
}

bool PhoneSession::setPlot(int row, bool on) {
	if (row < 0 || row >= model_->rows().size()) return false;
	return model_->setPlot(row, on);
}

/* ------------------------------------------------------------------ the Monitor */

void PhoneSession::setMonitorShown(bool shown) {
	if (shown == monitorShown_) return;
	monitorShown_ = shown;
	pushMonitor();
	emit monitorChanged();
}

/* MonitorTab's Log frames: the engine writes the frames while the screen shows them and they are not paused */
void PhoneSession::pushMonitor() {
	const bool on = monitorShown_ && !frames_->paused();
	if (on == monitoring_) return;
	monitoring_ = on;
	engine_->post([engine = engine_, on] { engine->setMonitor(on); });
	emit monitorChanged();
}

/* ------------------------------------------------------------------ the chart */

void PhoneSession::setChart(ChartItem *chart) {
	if (chart == chart_) return;
	if (chart_) chart_->removeAll();
	chart_ = chart;
	if (chart_) {
		/* the samples' time base, as MainWindow gives ChartTab the engine's clock */
		chart_->setClock([engine = engine_] { return engine->now(); });
		for (int row = 0; row < model_->rows().size(); ++row)
			if (model_->rows()[row].plot) onPlotChanged(row, true);
	}
	emit chartChanged();
}

/* MainWindow::onPlotChanged and ChartTab::plotRegister: the register's line on or off, keyed by regKey */
void PhoneSession::onPlotChanged(int row, bool on) {
	if (row >= 0 && row < model_->rows().size() && chart_) {
		const RegDef &def = model_->rows()[row].def;
		const int id = int(regKey(def));
		if (on) chart_->addThemedLine(id, nextColour_++, def.name, def.unit);
		else chart_->removeLine(id);
	}
	pushPlotted();
}

/* MainWindow::pushPlotted: the engine keeps the samples of these registers for takeSamples */
void PhoneSession::pushPlotted() {
	QVector<RegKey> keys;
	for (const RegisterModel::Row &row : model_->rows())
		if (row.plot) keys << regKey(row.def);
	engine_->post([engine = engine_, keys] { engine->setPlotted(keys); });
}

/* a store per stream of the map, empty (ChartView::setFastStream) */
void PhoneSession::resetStreams() {
	stores_.clear();
	for (const StreamDef &def : std::as_const(map_.streams)) stores_ << std::make_shared<fast::Store>(def);
	emit fastChanged();
}

QVariantList PhoneSession::fastChannels() const {
	QVariantList list;
	for (int i = 0; i < map_.streams.size(); ++i)
		for (int c = 0; c < map_.streams[i].channels.size(); ++c) {
			const StreamChannel &channel = map_.streams[i].channels[c];
			list << QVariantMap{ { QStringLiteral("stream"), i }, { QStringLiteral("channel"), c },
				{ QStringLiteral("name"), map_.streams[i].name + QLatin1Char('.') + channel.name },
				{ QStringLiteral("unit"), channel.unit },
				{ QStringLiteral("plotted"), chart_ && chart_->hasLine(fastLineId(i, c)) } };
		}
	return list;
}

/* ChartTab::plotFastChannel and the sidebar's stream button in one: the first channel of a stream on the chart switches
 * the stream on, the last one off switches it off (its records kept, as the desktop keeps a stream's store) */
bool PhoneSession::setFastPlot(int stream, int channel, bool on) {
	if (!chart_ || stream < 0 || stream >= map_.streams.size() || stream >= stores_.size() || channel < 0
			|| channel >= map_.streams[stream].channels.size())
		return false;
	const StreamDef &def = map_.streams[stream];
	const int id = fastLineId(stream, channel);
	const auto linesOf = [&] {
		int n = 0;
		for (int c = 0; c < def.channels.size(); ++c) n += chart_->hasLine(fastLineId(stream, c));
		return n;
	};
	if (on == chart_->hasLine(id)) return true;
	if (on) {
		if (chart_->lineCount() >= RegisterModel::MAX_PLOTTED) {
			emit notice(tr("At most %1 lines on the chart, registers and fast lines together: take one off first")
					.arg(RegisterModel::MAX_PLOTTED));
			return false;
		}
		const StreamChannel &c = def.channels[channel];
		chart_->addThemedLine(id, nextColour_++, def.name + QLatin1Char('.') + c.name, c.unit, stores_[stream], channel);
		if (linesOf() == 1) engine_->post([engine = engine_, stream] { engine->setFastStream(stream, true); });
	} else {
		chart_->removeLine(id);
		if (linesOf() == 0) engine_->post([engine = engine_, stream] { engine->setFastStream(stream, false); });
	}
	emit fastChanged();
	return true;
}

void PhoneSession::unplotLine(int id) {
	if (id >= FAST_LINE) {
		setFastPlot((id - FAST_LINE) / 256, (id - FAST_LINE) % 256, false);
		return;
	}
	for (int row = 0; row < model_->rows().size(); ++row)
		if (int(regKey(model_->rows()[row].def)) == id) model_->setPlot(row, false);
}

/* each poll's samples of the plotted registers (MainWindow::sync, ChartTab::frame), and the streams' blocks into their
 * stores (ChartView::appendFast and markFast), then what is older than the chart's memory goes (trimFast without the
 * RAM's share: a phone's chart keeps 5 min) */
void PhoneSession::syncChart() {
	const QHash<RegKey, QVector<QPointF>> samples = engine_->takeSamples();
	const QVector<IoEngine::FastBlock> blocks = engine_->takeFastBlocks();
	if (!chart_) return;
	QVector<double> times, values;
	for (auto it = samples.constBegin(); it != samples.constEnd(); ++it) {
		times.resize(it->size());
		values.resize(it->size());
		for (qsizetype i = 0; i < it->size(); ++i) {
			times[i] = it->at(i).x();
			values[i] = it->at(i).y();
		}
		chart_->appendSamples(int(it.key()), times, values);
	}
	QVector<bool> fed(stores_.size(), false);
	for (const IoEngine::FastBlock &block : blocks) {
		if (block.stream < 0 || block.stream >= stores_.size() || block.count <= 0) continue;
		fast::Store &store = *stores_[block.stream];
		if (block.records.size() < qsizetype(block.count) * store.recordSize()) continue;
		store.append(block.first, block.count, block.records.constData(), block.newStart, block.lost);
		if (block.marked) store.mark(block.markRecord, block.markTime, block.markPeriod);
		fed[block.stream] = true;
	}
	for (int i = 0; i < stores_.size(); ++i) {
		if (!fed[i]) continue;
		fast::Store &store = *stores_[i];
		if (store.size() > 0 && store.hasTime()) {
			const double newest = store.timeAt(store.size() - 1), memory = chart_->memorySeconds();
			if (newest - store.timeAt(0) > memory * 1.05 + 0.5) store.dropFront(store.lowerBound(newest - memory));
		}
		chart_->storeChanged();
	}
}

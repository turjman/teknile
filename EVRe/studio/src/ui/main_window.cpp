/* SPDX-License-Identifier: Apache-2.0 */
/* The main window: building it from its parts, and the wiring between them
 * and the I/O engine (see main_window.h). Several devices on the link: main_window_bus.cpp. */
#include "ui/main_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMimeData>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>

#include "evre/frame.h"
#include "evre/registers.h"
#include "io/engine.h"
#include "model/map_document.h"
#include "model/register_model.h"
#include "ui/bus_panel.h"
#include "ui/chart_tab.h"
#include "ui/chart_widget.h"
#include "ui/elided_label.h"
#include "ui/event_log.h"
#include "ui/frame_clock.h"
#include "ui/help_dialog.h"
#include "ui/map_editor_tab.h"
#include "ui/map_settings_dialog.h"
#include "ui/monitor_tab.h"
#include "ui/recording_window.h"
#include "ui/registers_tab.h"
#include "ui/sidebar.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

constexpr int STATUS_INTERVAL_MS = 500;
constexpr qint64 READ_ERROR_LOG_MS = 5000; /* a register's read errors: one line in the log per 5 s at most */

/* a bus file (model/bus_file.h) and not a map: its "format" is "evre-bus/..." */
bool isBusFile(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return false;
	const QString format = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("format")).toString();
	return format.startsWith(QLatin1String("evre-bus/"));
}

/* the map a start opens: the one chosen last, or else the first map in maps/ (a bus file there is no map); empty:
 * none */
QString mapToOpenAtStart(const QSettings &settings) {
	const QString chosen = settings.value(QStringLiteral("map/file")).toString();
	if (!chosen.isEmpty() && QFileInfo::exists(chosen)) return chosen;
	const QDir maps(mapsFolder());
	for (const QString &name : maps.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name))
		if (!isBusFile(maps.filePath(name))) return maps.filePath(name);
	return {};
}

/* what a new map starts from: the reserved bank every EVRe device has */
QVector<RegDef> reservedRegisters() {
	RegDef deviceId;
	deviceId.addr = evre::DEVICE_ID;
	deviceId.name = QStringLiteral("DEVICE_ID");
	deviceId.type = RegType::U16;
	deviceId.size = 2;
	deviceId.group = QStringLiteral("Protocol");
	RegDef status = deviceId;
	status.addr = evre::STATUS;
	status.name = QStringLiteral("STATUS");
	RegDef config = deviceId;
	config.addr = evre::CONFIG;
	config.name = QStringLiteral("CONFIG");
	config.rw = true;
	config.danger = true;
	return { deviceId, status, config };
}

/* the Registers table's text colours: the theme's */
RegisterModel::Colors registerColors() {
	const ThemeColors &c = Theme::colors();
	RegisterModel::Colors colors;
	colors.accent = c.accent;
	colors.error = c.bad;
	colors.warn = c.warn;
	colors.muted = c.muted;
	return colors;
}

} // namespace

MainWindow::MainWindow(const QString &mapAtStart, const QString &busAtStart, QWidget *parent)
	: QMainWindow(parent), mapAtStart_(mapAtStart), busAtStart_(busAtStart) {
	setWindowTitle(QStringLiteral("EVRe Studio"));
	setAcceptDrops(true); /* a .csv dropped on the window: opened as a recording */
	resize(1400, 860);
	model_ = new RegisterModel(this);
	model_->setColors(registerColors());
	doc_ = new MapDocument(this);
	startEngine();
	buildWindow();
	chartTab_->logDrawing(); /* said while the tab was built, before the Log was there */
	connectModel();
	startClocks();
	restoreSettings();
	auto *helpKey = new QShortcut(QKeySequence::HelpContents, this);
	connect(helpKey, &QShortcut::activated, this, &MainWindow::showHelp);
	updateStaleAfter();
	refreshStatus();
	/* the link state last: Qt sizes the pill's frame when the sidebar joins the window, and a state
	 * set before that (with the border of "idle") would make the pill 2 px larger */
	sidebar_->showDisconnected();
	showDeviceInfo(); /* "not read yet": the line is there from the start, the card never grows */
	updateAutoSendOffer();
}

void MainWindow::applyStartup(const Startup &startup) {
	/* for this session: a plain start opens the one chosen last */
	if (!startup.bus.isEmpty() && startup.bus != busAtStart_) loadBus(startup.bus, false); /* not twice */
	else if (!startup.map.isEmpty() && startup.map != mapAtStart_) loadMap(startup.map, false);
	if (!startup.tcp.isEmpty()) {
		sidebar_->useTcp(startup.tcp);
	} else if (!startup.serial.isEmpty()) {
		refreshPorts(); /* the ports there now; the one asked for is added when it is not among them */
		sidebar_->useSerial(startup.serial);
	}
	const QByteArray token = qgetenv("EVRE_TOKEN"); /* never on the command line */
	if (!token.isEmpty()) sidebar_->setToken(QString::fromUtf8(token));
	int leftOff = 0;
	QStringList fixed;
	for (const QString &name : startup.plot) {
		for (int row = 0; row < model_->rows().size(); row++) {
			const RegDef &def = model_->rows()[row].def;
			if (def.name.compare(name.trimmed(), Qt::CaseInsensitive) != 0) continue;
			if (!def.canPlot()) fixed << def.name;
			else if (!model_->setPlot(row, true)) leftOff++;
		}
	}
	if (!fixed.isEmpty())
		logEvent(LogLevel::Warning, tr("--plot: not plottable (the map): %1").arg(fixed.join(QStringLiteral(", "))));
	if (leftOff > 0)
		logEvent(LogLevel::Warning, tr("--plot: %1 registers left off the chart: %2 at most at this rate")
				.arg(leftOff).arg(model_->plotLimit()));
	if (startup.tab == QLatin1String("chart")) tabs_->setCurrentIndex(TabChart);
	else if (startup.tab == QLatin1String("monitor")) tabs_->setCurrentIndex(TabMonitor);
	else if (startup.tab == QLatin1String("map")) tabs_->setCurrentIndex(TabMap);
	if (startup.interval >= 0) sidebar_->setPollInterval(startup.interval);
	if (startup.inFlight > 0) sidebar_->setInFlight(startup.inFlight);
	if (!startup.record.isEmpty()) startRecord(startup.record);
	sidebar_->tickApiSwitches(startup.api, startup.apiWrites, startup.apiDanger);
	if (startup.connect) {
		wantConnected_ = true;
		connectLink();
	}
}

MainWindow::~MainWindow() {
	RecordingWindow::closeAll();
	frameClock_->stop();
	if (ioThread_->isRunning()) {
		QMetaObject::invokeMethod(engine_, [engine = engine_] { engine->shutdown(); }, Qt::BlockingQueuedConnection);
		ioThread_->quit();
		ioThread_->wait(3000);
	}
}

void MainWindow::closeEvent(QCloseEvent *event) {
	if (doc_->isModified()) {
		const auto answer = QMessageBox::question(this, tr("Unsaved map"),
				tr("The register map has changes that are not saved. Save them?"),
				QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (answer == QMessageBox::Cancel) {
			event->ignore();
			return;
		}
		if (answer == QMessageBox::Save) saveMap(false);
	}
	if (!keepBusEdits()) {
		event->ignore();
		return;
	}
	sidebar_->saveSettings();
	QSettings settings;
	settings.setValue(QStringLiteral("map/file"), rememberedMap_);
	settings.setValue(QStringLiteral("map/bus"), rememberedBus_);
	settings.setValue(QStringLiteral("ui/dark"), Theme::isDark());
	settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
	wantConnected_ = false;
	disconnectLink();
	RecordingWindow::closeAll(); /* else the program would go on while one is open */
	event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
	for (const QUrl &url : event->mimeData()->urls()) {
		if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
			event->acceptProposedAction();
			return;
		}
	}
}

void MainWindow::dropEvent(QDropEvent *event) {
	for (const QUrl &url : event->mimeData()->urls())
		if (url.isLocalFile() && url.toLocalFile().endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
			openRecording(url.toLocalFile());
	event->acceptProposedAction();
}

void MainWindow::openRecording(const QString &file) {
	const int ramMB = QSettings().value(QStringLiteral("chart/ramMB"), ChartView::DEFAULT_RAM_MB).toInt();
	const auto opened = [this](RecordingWindow *window) {
		connect(window, &RecordingWindow::logged, this, [this](int level, const QString &text) {
			logEvent(LogLevel(level), text);
		});
		logEvent(LogLevel::Info, tr("recording opened: %1").arg(QDir::toNativeSeparators(window->file())));
	};
	if (file.isEmpty()) RecordingWindow::choose(this, model_->definitions(), ramMB, opened);
	else RecordingWindow::open(this, file, model_->definitions(), ramMB, opened);
}

/* ------------------------------------------------------------------- building */

/* the device side, on its own thread; what it reports comes back as queued signals */
void MainWindow::startEngine() {
	ioThread_ = new QThread(this);
	ioThread_->setObjectName(QStringLiteral("evre-io"));
	engine_ = new IoEngine;
	engine_->moveToThread(ioThread_);
	connect(ioThread_, &QThread::finished, engine_, &QObject::deleteLater);
	ioThread_->start(QThread::HighPriority);
	connect(engine_, &IoEngine::opened, this, &MainWindow::onOpened);
	connect(engine_, &IoEngine::closed, this, &MainWindow::onClosed);
	connect(engine_, &IoEngine::deviceInfo, this, &MainWindow::onDeviceInfo);
	connect(engine_, &IoEngine::deviceOnline, this, &MainWindow::onDeviceOnline);
	connect(engine_, &IoEngine::loginRefused, this, [this](int slave, const QString &err) {
		DeviceInfo &info = deviceInfo_[slave];
		info.state = DeviceInfo::State::TokenRefused;
		info.error = err;
		showDeviceInfo();
		logEvent(LogLevel::Error, deviceLabel(slave) + tr("token refused: %1").arg(err));
	});
	connect(engine_, &IoEngine::loginSkipped, this, [this] {
		logEvent(LogLevel::Warning, tr("the map declares no login register: the token was not sent"));
	});
	connect(engine_, &IoEngine::readDone, this,
			[this](quint64 id, bool ok, const QByteArray &data, const QString &message, double ms) {
				if (const ReadDone done = pendingReads_.take(id)) done(ok, data, message, ms);
			});
	connect(engine_, &IoEngine::writeDone, this, [this](quint64 id, bool ok, const QString &message) {
		if (const WriteDone done = pendingWrites_.take(id)) done(ok, message);
	});
	connect(engine_, &IoEngine::recordStarted, this, &MainWindow::onRecordStarted);
	connect(engine_, &IoEngine::recordStopped, this, &MainWindow::onRecordStopped);
	connect(engine_, &IoEngine::apiStarted, this, &MainWindow::onApiStarted);
	connect(engine_, &IoEngine::autoSendSet, this, &MainWindow::onAutoSendSet);
	connect(engine_, &IoEngine::autoSendSlowed, this, [this](const QString &why) { logEvent(LogLevel::Warning, why); });
	/* the device cleared it by itself (a reset, or another client): said once, not switched on again */
	connect(engine_, &IoEngine::autoSendStopped, this, [this] {
		autoSendAsked_ = false;
		autoSendHz_ = 0;
		updatePlotLimit();
		sidebar_->setAutoSendOn(false);
		logEvent(LogLevel::Warning, tr("the device stopped auto send (reset?)"));
	});
}

/* the sidebar at the left, the tabs beside it, the status bar under them */
void MainWindow::buildWindow() {
	auto *central = new QWidget;
	auto *columns = new QHBoxLayout(central);
	columns->setContentsMargins(0, 0, 0, 0);
	columns->setSpacing(0);
	sidebar_ = new Sidebar;
	connectSidebar();
	columns->addWidget(sidebar_);

	tabs_ = new QTabWidget;
	tabs_->addTab(buildRegistersTab(), tr("Registers"));
	tabs_->addTab(buildChartTab(), tr("Chart"));
	tabs_->addTab(buildMonitorTab(), tr("Monitor"));
	tabs_->addTab(buildMapTab(), tr("Map editor"));
	buildEventLog();
	auto *mainArea = new QWidget;
	auto *mainLayout = new QVBoxLayout(mainArea);
	mainLayout->setContentsMargins(18, 12, 18, 12);
	mainLayout->addWidget(tabs_);
	columns->addWidget(mainArea, 1);
	setCentralWidget(central);
	buildStatusBar();
}

void MainWindow::connectSidebar() {
	connect(sidebar_, &Sidebar::connectClicked, this, &MainWindow::toggleConnect);
	connect(sidebar_, &Sidebar::refreshPortsClicked, this, &MainWindow::refreshPorts);
	/* one device: the Monitor's requests and the link's keep-alive go to the slave chosen (a bus sets the box) */
	connect(sidebar_, &Sidebar::slaveChanged, this, [this](int slave) {
		if (isBus()) return;
		monitorTab_->setSlave(slave);
		pushLinkOptions();
	});
	connect(sidebar_, &Sidebar::inFlightChanged, this, &MainWindow::pushLinkOptions);
	connect(sidebar_, &Sidebar::pollingChanged, this, &MainWindow::pushPolling);
	connect(sidebar_, &Sidebar::timingChanged, this, &MainWindow::updateStaleAfter);
	connect(sidebar_, &Sidebar::valuePaceChanged, this, &MainWindow::applyValuePace);
	connect(sidebar_, &Sidebar::autoSendChanged, this, &MainWindow::pushAutoSend);
	connect(sidebar_, &Sidebar::openMapClicked, this, &MainWindow::openMap);
	connect(sidebar_, &Sidebar::newMapClicked, this, &MainWindow::newMap);
	connect(sidebar_, &Sidebar::saveMapClicked, this, &MainWindow::saveMap);
	connect(sidebar_, &Sidebar::recordClicked, this, &MainWindow::toggleRecord);
	connect(sidebar_, &Sidebar::openRecordingClicked, this, &MainWindow::openRecording);
	connect(sidebar_, &Sidebar::apiServeChanged, this, &MainWindow::setApiRunning);
	connect(sidebar_, &Sidebar::apiWritesChanged, this, &MainWindow::pushApiWrites);
	connect(sidebar_, &Sidebar::helpClicked, this, &MainWindow::showHelp);
	connect(sidebar_, &Sidebar::themeClicked, this, &MainWindow::toggleTheme);
	BusPanel *bus = sidebar_->busPanel();
	connect(bus, &BusPanel::newBusClicked, this, &MainWindow::newBus);
	connect(bus, &BusPanel::openBusClicked, this, &MainWindow::openBus);
	connect(bus, &BusPanel::saveBusClicked, this, [this](bool saveAs) { saveBus(saveAs); });
	connect(bus, &BusPanel::closeBusClicked, this, &MainWindow::closeBus);
	connect(bus, &BusPanel::addDeviceClicked, this, &MainWindow::addDevice);
	connect(bus, &BusPanel::editDeviceClicked, this, &MainWindow::editDevice);
	connect(bus, &BusPanel::removeDeviceClicked, this, &MainWindow::removeDevice);
	connect(bus, &BusPanel::deviceSelected, this, &MainWindow::selectDevice);
	connect(bus, &BusPanel::presetChosen, this, &MainWindow::sendPreset);
	connect(bus, &BusPanel::newPresetClicked, this, &MainWindow::newPreset);
	connect(bus, &BusPanel::removePresetClicked, this, &MainWindow::removePreset);
}

QWidget *MainWindow::buildRegistersTab() {
	registersTab_ = new RegistersTab(model_, doc_);
	connect(registersTab_, &RegistersTab::writeRequested, this, &MainWindow::onWriteRequested);
	connect(registersTab_, &RegistersTab::broadcastRequested, this, &MainWindow::onBroadcastRequested);
	connect(registersTab_, &RegistersTab::readRequested, this, [this](quint16 addr, quint16 count) {
		engineRead(selectedSlave(), addr, count, true, {}); /* the table follows through the engine */
	});
	connect(registersTab_, &RegistersTab::unplotAllRequested, this, &MainWindow::unplotAll);
	connect(registersTab_, &RegistersTab::plotFieldRequested, this, [this](int row, int field) {
		const RegDef &def = model_->rows()[row].def;
		if (field < 0 || field >= def.fields.size()) return;
		chartTab_->plotField(def, def.fields[field]);
		statusBar()->showMessage(tr("%1.%2 on the chart (a math line)").arg(def.name, def.fields[field].name), 4000);
	});
	/* the definitions are edited on the Map editor tab; a register of another device (All devices): its map first */
	connect(registersTab_, &RegistersTab::editDefinitionRequested, this, [this](quint32 uid, int slave) {
		if (isBus() && slave != selectedSlave()) {
			selectDevice(deviceOfSlave(slave));
			if (slave != selectedSlave()) return; /* its map's edits kept: cancelled */
		}
		tabs_->setCurrentIndex(TabMap);
		mapTab_->selectRegister(uid);
	});
	connect(registersTab_, &RegistersTab::deviceChosen, this, [this](int slave) {
		allDevices_ = slave < 0;
		const int index = deviceOfSlave(slave);
		if (allDevices_ || index == selectedDevice_) onBusChanged();
		else selectDevice(index);
	});
	connect(registersTab_, &RegistersTab::addRegisterRequested, this, [this](quint32 afterUid) {
		tabs_->setCurrentIndex(TabMap);
		mapTab_->addRegister(afterUid);
	});
	connect(registersTab_, &RegistersTab::statusMessage, this, [this](const QString &text, int ms) {
		statusBar()->showMessage(text, ms);
	});
	connect(model_, &RegisterModel::plotLimitReached, this, [this] {
		const long rate = std::lround(sampleRateHz()); /* 0: not polling, no rate to name */
		const QString text = rate > 0
				? tr("At most %1 registers on the chart at %2 samples a second: untick one first").arg(model_->plotLimit())
						.arg(rate)
				: tr("At most %1 registers on the chart: untick one first").arg(model_->plotLimit());
		statusBar()->showMessage(text, 6000);
	});
	connect(model_, &RegisterModel::plotsTakenOff, this, [this](const QStringList &names) {
		logEvent(LogLevel::Warning, tr("%1 samples a second: at most %2 registers on the chart; taken off: %3")
				.arg(std::lround(sampleRateHz())).arg(model_->plotLimit()).arg(names.join(QStringLiteral(", "))));
	});
	connect(registersTab_, &RegistersTab::writesAllowedChanged, this, &MainWindow::showBus); /* the presets follow */
	/* connected before the tab is added: the first tab added becomes the current one */
	connect(tabs_, &QTabWidget::currentChanged, this,
			[this](int tab) { registersTab_->setShown(tab == TabRegisters); });
	return registersTab_;
}

QWidget *MainWindow::buildChartTab() {
	chartTab_ = new ChartTab([engine = engine_] { return engine->now(); }); /* the samples' time base */
	connect(chartTab_, &ChartTab::mathRegistersChanged, this, &MainWindow::pushPlotted);
	connect(chartTab_, &ChartTab::unplotAllRequested, this, &MainWindow::unplotAll);
	connect(chartTab_, &ChartTab::logged, this, &MainWindow::logEvent);
	connect(chartTab_, &ChartTab::openRecordingRequested, this, &MainWindow::openRecording);
	connect(chartTab_, &ChartTab::notesChanged, this, &MainWindow::saveRecordingNotes);
	connect(tabs_, &QTabWidget::currentChanged, this, [this](int tab) { chartTab_->setShown(tab == TabChart); });
	return chartTab_;
}

QWidget *MainWindow::buildMonitorTab() {
	monitorTab_ = new MonitorTab(model_);
	connect(monitorTab_, &MonitorTab::logFramesToggled, this, [this](bool on) {
		engine_->post([engine = engine_, on] { engine->setMonitor(on); });
	});
	connect(monitorTab_, &MonitorTab::readRequested, this, &MainWindow::sendRawRead);
	connect(monitorTab_, &MonitorTab::writeRequested, this, &MainWindow::sendRawWrite);
	return monitorTab_;
}

QWidget *MainWindow::buildMapTab() {
	mapTab_ = new MapEditorTab(doc_);
	/* the form's live line: the value the Registers table has */
	mapTab_->setLiveValues([this](quint32 uid, QByteArray &raw) {
		const int row = model_->rowOfUid(uid);
		if (row < 0 || !model_->rows()[row].valid) return false;
		raw = model_->rows()[row].raw;
		return true;
	});
	connect(mapTab_, &MapEditorTab::mapSettingsRequested, this, &MainWindow::showMapSettings);
	/* a map several devices share: the live values from another of them (the same map: nothing else changes) */
	connect(mapTab_, &MapEditorTab::liveDeviceChosen, this, [this](int slave) { selectDevice(deviceOfSlave(slave)); });
	connect(mapTab_, &MapEditorTab::statusMessage, this, [this](const QString &text, int ms) {
		statusBar()->showMessage(text, ms);
	});
	connect(doc_, &MapDocument::changed, this, &MainWindow::onMapChanged);
	connect(doc_, &MapDocument::modifiedChanged, this, &MainWindow::updateMapInfo);
	return mapTab_;
}

void MainWindow::buildEventLog() {
	eventLog_ = new EventLog;
	tabs_->addTab(eventLog_, tr("Log"));
	notice_ = new Notice(tabs_, this);
	connect(eventLog_, &EventLog::popUp, notice_, &Notice::post);
	/* the tab's title counts the warnings not seen yet; looking at it clears the count */
	connect(eventLog_, &EventLog::unseenChanged, this, [this](int count) {
		tabs_->setTabText(TabLog, count > 0 ? tr("Log (%1)").arg(count) : tr("Log"));
	});
	connect(tabs_, &QTabWidget::currentChanged, this, [this](int tab) { eventLog_->setShown(tab == TabLog); });
	connect(notice_, &Notice::showLogClicked, this, [this] { tabs_->setCurrentIndex(TabLog); });
	connect(notice_, &Notice::noRoom, this, [this](const QString &message, int ms) {
		statusBar()->showMessage(message, ms);
	});
}

/* the link's numbers; the poll rate is in the sidebar, beside the interval asked for (not twice) */
void MainWindow::buildStatusBar() {
	statusLatency_ = new QLabel;
	statusTraffic_ = new QLabel;
	statusErrors_ = new QLabel;
	/* the numbers in labels as wide as their widest text: a digit more does not move what follows */
	const QFontMetrics metrics(statusBar()->font());
	statusLatency_->setMinimumWidth(metrics.horizontalAdvance(tr("Latency %1 ms").arg(9999.9, 0, 'f', 1)) + 8);
	statusTraffic_->setMinimumWidth(metrics.horizontalAdvance(tr("TX %1 · RX %2").arg(9999999999).arg(9999999999)) + 8);
	statusErrors_->setMinimumWidth(metrics.horizontalAdvance(tr("Timeouts %1 · Errors %2 · Bad frames %3")
			.arg(99999999).arg(99999999).arg(99999999)) + 8);
	/* why the polls are slower than asked: always there (empty when they are not), plain text as tall as the others,
	 * so it comes and goes moving nothing; it takes the room left and is cut ("...") rather than widen the window */
	statusSlow_ = new ElidedLabel;
	statusSlow_->setObjectName(QStringLiteral("statusSlow")); /* its amber is the theme's: no style of its own */
	statusSlow_->setTextFormat(Qt::PlainText);
	statusBar()->addWidget(statusLatency_);
	statusBar()->addWidget(statusTraffic_);
	statusBar()->addWidget(statusErrors_);
	statusBar()->addWidget(statusSlow_, 1);
	statusBar()->setSizeGripEnabled(false);
}

void MainWindow::connectModel() {
	connect(model_, &RegisterModel::plotChanged, this, &MainWindow::onPlotChanged);
	/* another map, or registers added, removed or redefined: the engine, the map card and the math lines follow */
	connect(model_, &RegisterModel::structureChanged, this, [this] {
		pushMap();
		updateMapInfo();
		chartTab_->setRegisters(model_->definitions());
	});
}

void MainWindow::startClocks() {
	valueClock_.start();
	applyValuePace();
	frameClock_ = new FrameClock(this);
	connect(frameClock_, &FrameClock::tick, this, &MainWindow::sync);
	frameClock_->start();
	statusTimer_.setInterval(STATUS_INTERVAL_MS);
	connect(&statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
	statusTimer_.start();
	reconnectTimer_.setSingleShot(true);
	connect(&reconnectTimer_, &QTimer::timeout, this, &MainWindow::retryConnect);
}

/* the link first, then the map, then the API (it may start at once) */
void MainWindow::restoreSettings() {
	sidebar_->restoreLinkSettings();
	QSettings settings;
	const QString mapFile = mapToOpenAtStart(settings);
	const QString busFile = settings.value(QStringLiteral("map/bus")).toString();
	/* the command line's bus or map, for this session (the one chosen last stays chosen); else the bus chosen last,
	 * the map chosen last, a new map: the first that opens */
	bool opened = !busAtStart_.isEmpty() && loadBus(busAtStart_, false);
	if (!opened && !mapAtStart_.isEmpty()) {
		loadMap(mapAtStart_, false);
		opened = true;
	}
	if (!opened && !busFile.isEmpty() && QFileInfo::exists(busFile)) opened = loadBus(busFile);
	if (!opened && QFileInfo::exists(mapFile)) {
		loadMap(mapFile);
		opened = true;
	}
	if (!opened) newMap();
	restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
	sidebar_->restoreApiSettings();
}

/* ------------------------------------------------------------------- the link */

void MainWindow::toggleConnect() {
	if (wantConnected_) {
		wantConnected_ = false;
		reconnectTimer_.stop();
		disconnectLink(); /* the engine clears AUTO_SEND on the device before it closes the link */
		/* Disconnect asked for: auto send is not switched on again at the next connect (a lost link keeps it) */
		sidebar_->setAutoSendOn(false);
		pushAutoSend();
	} else {
		wantConnected_ = true;
		connectLink();
	}
}

void MainWindow::connectLink() {
	disconnectLink();
	pushLinkOptions();
	pushPolling();
	if (sidebar_->isTcp()) {
		const QString host = sidebar_->host();
		const quint16 port = sidebar_->port();
		const QString token = sidebar_->token();
		if (!retrying_) {
			logEvent(LogLevel::Info, tr("connecting to %1:%2%3").arg(host).arg(port)
					.arg(token.isEmpty() ? QString() : tr(" (with a token)")));
		}
		engine_->post([engine = engine_, host, port, token] { engine->connectTcp(host, port, token); });
	} else {
		const QString port = sidebar_->serialPort();
		if (port.isEmpty()) {
			wantConnected_ = false;
			sidebar_->showLinkError(tr("No serial port"));
			return;
		}
		const qint32 baud = sidebar_->baud();
		if (!retrying_) logEvent(LogLevel::Info, tr("opening %1 at %2").arg(port).arg(baud));
		engine_->post([engine = engine_, port, baud] { engine->connectSerial(port, baud); });
	}
	sidebar_->showConnecting();
}

void MainWindow::retryConnect() {
	if (!wantConnected_ || connected_) return;
	retrying_ = true;
	connectLink();
	retrying_ = false;
}

void MainWindow::disconnectLink() {
	engine_->post([engine = engine_] { engine->disconnectLink(); });
	connected_ = false;
	offlineDevices_.clear();
	registersTab_->setConnected(false);
	sidebar_->showDisconnected();
	showBus();
	autoSendCap_ = -1;
	updateAutoSendOffer();
}

void MainWindow::onOpened(const QString &link) {
	logEvent(LogLevel::Info, tr("connected: %1").arg(link));
	connected_ = true;
	offlineDevices_.clear();
	deviceInfo_.clear();
	showDeviceInfo(); /* "not read yet" until the engine reads it */
	registersTab_->setConnected(true);
	sidebar_->showConnected(link);
	showBus();
	autoSendCap_ = -1; /* until STATUS is read */
	updateAutoSendOffer();
}

void MainWindow::onClosed(const QString &why) {
	const bool wasUp = connected_;
	if (!why.isEmpty()) {
		logEvent(wasUp ? LogLevel::Error : LogLevel::Warning,
				(wasUp ? tr("connection lost: %1") : tr("not connected: %1")).arg(why));
	} else if (wasUp) {
		logEvent(LogLevel::Info, tr("disconnected"));
	}
	connected_ = false; /* the engine has let go of the link already */
	offlineDevices_.clear();
	showBus();
	autoSendCap_ = -1; /* a lost link keeps Auto send ticked: the engine switches it on again after a reconnect */
	updateAutoSendOffer();
	registersTab_->setConnected(false);
	if (why.isEmpty()) sidebar_->showDisconnected();
	else sidebar_->showLinkError(why);
	if (wantConnected_ && sidebar_->autoReconnect()) {
		reconnectTimer_.start(wasUp ? 500 : 2000);
		sidebar_->showReconnecting();
	} else {
		wantConnected_ = false;
	}
}

/* a device's ID and protocol revision, under the pill (the selected device's, on a bus) and in the log; a warning
 * when its map is for another */
void MainWindow::onDeviceInfo(int slave, bool ok, quint16 id, quint16 status, const QString &err) {
	const QString who = deviceLabel(slave);
	if (!isBus()) {
		autoSendCap_ = ok && (status & evre::CAP_AUTO_SEND) ? 1 : 0;
		updateAutoSendOffer();
	}
	DeviceInfo &info = deviceInfo_[slave];
	info.state = ok ? DeviceInfo::State::Read : DeviceInfo::State::NotRead;
	info.id = id;
	info.revision = status & 0xFF;
	info.error = err;
	showDeviceInfo();
	if (!ok) {
		/* a device of a bus that does not answer at all is told of once, as offline (onDeviceOnline) */
		if (!isBus() || !err.startsWith(QLatin1String("timeout")))
			logEvent(LogLevel::Error, who + tr("DEVICE_ID not read: %1").arg(err));
		return;
	}
	logEvent(LogLevel::Info, who + tr("device ID %1, protocol revision %2").arg(addrText(id)).arg(info.revision));
	const int index = deviceOfSlave(slave);
	const uint16_t mapId = isBus() ? (index >= 0 ? deviceMap(index).deviceId : 0) : doc_->map().deviceId;
	if (mapId && id != mapId)
		logEvent(LogLevel::Warning, who + tr("the map is for device %1, this is %2").arg(addrText(mapId), addrText(id)));
}

void MainWindow::showDeviceInfo() { sidebar_->setDeviceInfo(deviceInfoHtml(isBus() ? selectedSlave() : 0)); }

/* "D2: " on a bus (whose it is), then the ID, or why it is not known; the map's own device ID compared now, so an
 * edit of the map shows at once */
QString MainWindow::deviceInfoHtml(int slave) const {
	const QString who = deviceLabel(slave);
	const auto it = deviceInfo_.constFind(slave);
	if (it == deviceInfo_.constEnd()) return (who + tr("Device ID not read yet")).toHtmlEscaped();
	switch (it->state) {
	case DeviceInfo::State::NotRead:
		return (who + tr("DEVICE_ID not read: %1").arg(it->error)).toHtmlEscaped();
	case DeviceInfo::State::TokenRefused:
		return (who + tr("token refused: %1").arg(it->error)).toHtmlEscaped();
	case DeviceInfo::State::Read:
		break;
	}
	QString html = (who + tr("Device ID %1 · protocol rev %2").arg(addrText(it->id)).arg(it->revision)).toHtmlEscaped();
	const int index = deviceOfSlave(slave);
	const uint16_t mapId = isBus() ? (index >= 0 ? deviceMap(index).deviceId : 0) : doc_->map().deviceId;
	if (mapId && it->id != mapId)
		html += QStringLiteral("<br>") + coloredSpan(tr("the map is for %1").arg(addrText(mapId)).toHtmlEscaped(),
				Theme::colors().warn);
	return html;
}

/* Auto send ticked or not, or its rate: to the engine (a rate changed while off changes nothing on the device) */
void MainWindow::pushAutoSend() {
	const bool on = sidebar_->autoSendOn();
	if (!on && !autoSendAsked_) return;
	autoSendAsked_ = on;
	const int prescaler = evre::AUTO_SEND_BASE_HZ / std::max(1, sidebar_->autoSendHz()) - 1;
	engine_->post([engine = engine_, on, prescaler] { engine->setAutoSend(on, prescaler); });
}

/* one device only, and only one whose STATUS has CAP_AUTO_SEND; the tooltip says why not */
void MainWindow::updateAutoSendOffer() {
	QString why, shortWhy;
	if (isBus()) {
		why = tr("Not with several devices on the link: devices sending by themselves would collide on a shared link.");
		shortWhy = tr("not on a bus");
	} else if (!connected_ || autoSendCap_ < 0) {
		why = tr("Offered once connected, when the device's STATUS has CAP_AUTO_SEND (bit 11).");
		shortWhy = tr("not connected");
	} else if (autoSendCap_ == 0) {
		why = tr("This device does not offer it: its STATUS has no CAP_AUTO_SEND (bit 11).");
		shortWhy = tr("not offered");
	}
	sidebar_->setAutoSendOffered(why.isEmpty(), why, shortWhy);
}

void MainWindow::onAutoSendSet(bool on, int hz, const QString &err) {
	autoSendHz_ = on && err.isEmpty() ? hz : 0;
	updatePlotLimit();
	if (err.isEmpty()) {
		logEvent(LogLevel::Info, on ? tr("auto send on: the device sends its read-only block %1 times a second").arg(hz)
									: tr("auto send off"));
		return;
	}
	if (!on) {
		logEvent(LogLevel::Warning, tr("auto send not switched off: %1").arg(err));
		return;
	}
	autoSendAsked_ = false;
	sidebar_->setAutoSendOn(false);
	logEvent(LogLevel::Error, tr("auto send not switched on: %1").arg(err));
}

void MainWindow::onDeviceOnline(int slave, bool online) {
	if (online) {
		offlineDevices_.remove(uint8_t(slave));
		logEvent(LogLevel::Info, deviceLabel(slave) + tr("answers again"));
	} else {
		if (offlineDevices_.isEmpty()) retryClock_.start();
		offlineDevices_.insert(uint8_t(slave));
		retryTimeoutsAllowed_ += 1; /* its first retry may time out before the next refresh counts it in */
		logEvent(LogLevel::Warning, deviceLabel(slave) + tr("no answer: offline, left out of the polls and asked "
				"again every %1 s").arg(IoEngine::OFFLINE_RETRY_MS / 1000.0));
	}
	showBus();
}

void MainWindow::refreshPorts() { sidebar_->refreshPorts(doc_->map()); }

/* -------------------------------------------------------------------- the map */

void MainWindow::loadMap(const QString &file, bool remember) {
	DeviceMap loaded;
	QString err;
	if (!loaded.load(file, err)) {
		logEvent(LogLevel::Error, tr("map not loaded: %1: %2").arg(QDir::toNativeSeparators(file), err));
		QMessageBox::warning(this, tr("Map not loaded"), tr("%1\n\n%2").arg(QDir::toNativeSeparators(file), err));
		return;
	}
	logEvent(LogLevel::Info, tr("map loaded: %1 (%2 registers)")
			.arg(QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath())).arg(loaded.regs.size()));
	if (remember) {
		rememberedMap_ = QFileInfo(file).absoluteFilePath();
		rememberedBus_.clear(); /* a map chosen: one device, the next start too */
	}
	chartTab_->clearLines();
	model_->setDefinitions({}); /* another map: nothing of the one before stays plotted or kept */
	const bool wasBus = isBus();
	clearBus();                 /* one device: the map's */
	doc_->reset(loaded);
	sidebar_->setSlave(doc_->map().slave);
	if (wasBus) onBusChanged();
	refreshPorts();
}

void MainWindow::openMap() {
	const QString path = doc_->map().path;
	const QString start = path.isEmpty() ? mapsFolder() : QFileInfo(path).absolutePath();
	const QString file = QFileDialog::getOpenFileName(this, tr("Open register map"), start, tr("EVRe map (*.json)"));
	if (!file.isEmpty()) loadMap(file);
}

void MainWindow::saveMap(bool saveAs) {
	/* the slave address of the sidebar is the map's (a step of its own, as any change); a device of a bus has its
	 * own in the bus file */
	const uint8_t slave = uint8_t(sidebar_->slave());
	if (!isBus() && slave != doc_->map().slave)
		doc_->edit(tr("Slave address"), [slave](DeviceMap &map) { map.slave = slave; });
	QString file = doc_->map().path;
	if (saveAs || file.isEmpty()) {
		file = QFileDialog::getSaveFileName(this, tr("Save register map"),
				file.isEmpty() ? mapsFolder() + QStringLiteral("/device.json") : file, tr("EVRe map (*.json)"));
		if (file.isEmpty()) return;
		if (doc_->map().device.isEmpty()) {
			const QString device = QFileInfo(file).completeBaseName();
			doc_->edit(tr("Device name"), [device](DeviceMap &map) { map.device = device; });
		}
	}
	QString err;
	if (!doc_->map().save(file, err)) {
		QMessageBox::warning(this, tr("Map not saved"), err);
		return;
	}
	doc_->markSaved(file);
	if (isBus()) {
		/* the bus's copy of this file is now what was saved; saved as another file: the device takes that one */
		const QString saved = QFileInfo(file).absoluteFilePath();
		busMaps_.insert(saved, doc_->map());
		BusDevice &device = bus_.devices[selectedDevice_];
		if (device.map != saved) {
			device.map = saved;
			busModified_ = true;
			showBus();
		}
	} else {
		rememberedMap_ = QFileInfo(file).absoluteFilePath();
	}
	updateMapInfo();
	statusBar()->showMessage(tr("Map saved: %1").arg(QDir::toNativeSeparators(file)), 4000);
}

void MainWindow::newMap() {
	DeviceMap map;
	rememberedMap_.clear(); /* not saved yet: the next start opens the first map in maps/ */
	rememberedBus_.clear();
	map.device = tr("New device");
	map.regs = reservedRegisters();
	chartTab_->clearLines();
	model_->setDefinitions({});
	const bool wasBus = isBus();
	clearBus();
	doc_->reset(map);
	if (wasBus) onBusChanged();
}

void MainWindow::onMapChanged() {
	const DeviceMap &map = doc_->map();
	model_->setDefinitions(tableDefinitions()); /* the values of registers that stay are kept */
	if (!isBus() && sidebar_->slave() != map.slave) {
		sidebar_->setSlave(map.slave);
		monitorTab_->setSlave(map.slave);
	}
	updateMapInfo();
	showDeviceInfo(); /* the map's device ID may have changed: the warning under the pill follows */
	/* the port list names the map's device by its USB IDs; listing the ports is slow, so only when they change */
	const QString portsKey = QStringLiteral("%1:%2:%3").arg(map.usbVid).arg(map.usbPid).arg(map.device);
	if (portsKey != portsKey_) {
		portsKey_ = portsKey;
		refreshPorts();
	}
}

void MainWindow::showMapSettings() {
	MapSettingsDialog dialog(doc_, isBus(), this); /* on a bus the slave is the bus file's: its box disabled */
	dialog.exec();
}

void MainWindow::updateMapInfo() {
	const int registers = isBus() ? int(doc_->map().regs.size()) : int(model_->rows().size());
	sidebar_->showMap(doc_->map(), registers, doc_->isModified());
}

/* the map edited gives way to another (a device selected, a bus opened): its unsaved changes saved or dropped */
bool MainWindow::keepMapEdits() {
	if (!doc_->isModified()) return true;
	const auto answer = QMessageBox::question(this, tr("Unsaved map"),
			tr("The map %1 has changes that are not saved. Save them?").arg(doc_->map().device),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
	if (answer == QMessageBox::Cancel) return false;
	if (answer == QMessageBox::Save) saveMap(false);
	return answer == QMessageBox::Discard || !doc_->isModified();
}

/* ----------------------------------------------------------------- the engine */

void MainWindow::pushMap() {
	const QVector<RegDef> defs = model_->definitions();
	const quint64 generation = ++mapGeneration_;
	copiedVersions_.fill(quint64(-1), model_->rows().size());
	/* the devices: the map's one (slave 0: the link's slave), or the bus's, each with its login register and map */
	QVector<EngineDevice> devices;
	auto engineDevice = [](const QString &name, uint8_t slave, const DeviceMap &map, bool poll, const QString &token) {
		EngineDevice device;
		device.name = name;
		device.slave = slave;
		device.token = token;
		device.loginAddr = map.loginAddr;
		device.loginSize = map.loginSize;
		device.poll = poll;
		device.map = std::make_shared<const DeviceMap>(map);
		return device;
	};
	if (!isBus()) devices << engineDevice(doc_->map().device, 0, doc_->map(), true, QString());
	for (int i = 0; i < bus_.devices.size(); i++) {
		const BusDevice &d = bus_.devices[i];
		devices << engineDevice(d.name, d.slave, deviceMap(i), d.poll, d.token);
	}
	const QString name = isBus() ? (bus_.name.isEmpty() ? QFileInfo(bus_.path).completeBaseName() : bus_.name)
								 : doc_->map().device;
	engine_->post([engine = engine_, defs, generation, name, devices] {
		engine->setMap(defs, generation, name, devices);
	});
	pushPlotted();
}

void MainWindow::pushPolling() {
	const bool on = sidebar_->pollingOn();
	const double intervalMs = sidebar_->pollIntervalMs();
	engine_->post([engine = engine_, on, intervalMs] { engine->setPolling(on, intervalMs); });
	updatePlotLimit();
}

double MainWindow::sampleRateHz() const {
	if (autoSendHz_ > 0) return autoSendHz_;
	if (!sidebar_->pollingOn()) return 0;
	const double intervalMs = sidebar_->pollIntervalMs();
	return intervalMs > 0 ? 1000.0 / intervalMs : measuredPollHz_;
}

void MainWindow::updatePlotLimit() {
	model_->setPlotLimit(RegisterModel::plotLimitFor(sampleRateHz()));
	chartTab_->setRegisterLimit(model_->plotLimit());
}

/* the link's slave: the sidebar's, or on a bus the selected device's (the keep-alive and the API's raw requests) */
void MainWindow::pushLinkOptions() {
	const int slave = selectedSlave(), timeoutMs = sidebar_->timeoutMs(), inFlight = sidebar_->inFlight();
	engine_->post([engine = engine_, slave, timeoutMs, inFlight] {
		engine->setLinkOptions(slave, timeoutMs, inFlight);
	});
}

void MainWindow::pushPlotted() {
	QVector<RegKey> keys;
	for (const RegisterModel::Row &row : model_->rows())
		if (row.plot) keys << regKey(row.def);
	for (RegKey key : chartTab_->mathRegisters())
		if (!keys.contains(key)) keys << key;
	engine_->post([engine = engine_, keys] { engine->setPlotted(keys); });
}

void MainWindow::pushApiWrites() {
	const bool writes = sidebar_->apiWrites(), danger = sidebar_->apiDanger();
	engine_->post([engine = engine_, writes, danger] { engine->apiSetWrites(writes, danger); });
}

void MainWindow::updateStaleAfter() {
	/* two poll intervals, and never less than a slow answer takes; plus the time between two copies
	 * into the table (applyValuePace): a value is not stale while its next copy is still to come */
	const double interval = sidebar_->pollIntervalMs();
	const int perSecond = valuePacer_.perSecond();
	const qint64 copyGapMs = perSecond > 0 ? 1000 / perSecond : 0;
	model_->setStaleAfterMs(copyGapMs
			+ std::max<qint64>(qint64(2 * interval), qint64(interval) + sidebar_->timeoutMs()));
}

void MainWindow::applyValuePace() {
	const int perSecond = sidebar_->valuesPerSecond();
	valuePacer_.setPerSecond(perSecond);
	chartTab_->setValuesPerSecond(perSecond);
	updateStaleAfter();
}

void MainWindow::engineRead(uint8_t slave, uint16_t addr, uint16_t count, bool intoTable, ReadDone done) {
	const quint64 id = nextRequestId_++;
	if (done) pendingReads_.insert(id, std::move(done));
	engine_->post([engine = engine_, id, slave, addr, count, intoTable] {
		engine->read(id, slave, addr, count, intoTable);
	});
}

void MainWindow::engineWrite(uint8_t slave, uint16_t addr, const QByteArray &bytes, bool ack, WriteDone done) {
	const quint64 id = nextRequestId_++;
	if (done) pendingWrites_.insert(id, std::move(done));
	engine_->post([engine = engine_, id, slave, addr, bytes, ack] { engine->write(id, slave, addr, bytes, ack); });
}

/* ----------------------------------------------------- once per display frame */

void MainWindow::sync() {
	/* values: only the rows that moved since the last copy, and only at their pace (a number that
	 * changes at every frame cannot be read); the samples below still move the chart at every frame */
	QVector<RegValue> values;
	quint64 generation = 0;
	qint64 nowMs = 0;
	const bool copyValues = valuePacer_.due(valueClock_.elapsed());
	if (copyValues) engine_->table().snapshot(values, generation, nowMs);
	if (copyValues && generation == mapGeneration_ && values.size() == model_->rows().size()) {
		for (int row = 0; row < values.size(); row++) {
			const RegValue &value = values[row];
			if (copiedVersions_.value(row) == value.version) continue;
			copiedVersions_[row] = value.version;
			logReadStateChange(row, value);
			model_->applyValue(row, value.raw, value.valid, value.error, value.unavailable,
					value.valid ? nowMs - value.updatedMs : 0);
		}
	}
	/* samples, and the chart moves on */
	chartTab_->frame(engine_->takeSamples());
	/* frames for the monitor */
	int dropped = 0;
	const QStringList frames = engine_->takeFrames(dropped);
	monitorTab_->addFrames(frames, dropped);
}

/* before the table takes the new value: reads of the register started failing, or work again */
void MainWindow::logReadStateChange(int row, const RegValue &value) {
	const RegisterModel::Row &was = model_->rows()[row];
	if (value.error == was.error && value.unavailable == was.unavailable) return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	/* a bus: a device that does not answer is told of once (offline), not for each of its registers */
	const bool deviceSilent = isBus() && value.error.startsWith(QLatin1String("timeout"));
	if (!value.error.isEmpty() && !deviceSilent && now - readErrorLoggedMs_.value(row, 0) > READ_ERROR_LOG_MS) {
		readErrorLoggedMs_[row] = now;
		const QString name = was.def.name, addr = addrText(was.def.addr);
		logEvent(value.unavailable ? LogLevel::Warning : LogLevel::Error, value.unavailable
				? tr("%1 (%2): not available on this device: %3").arg(name, addr, value.error)
				: tr("%1 (%2): read error: %3").arg(name, addr, value.error));
	} else if (value.error.isEmpty() && !was.error.isEmpty() && value.valid) {
		logEvent(LogLevel::Info, tr("%1: read again").arg(was.def.name));
	}
}

/* --------------------------------------------------------------------- writes */

/* Every write asked for in the window (an edit in the table, the quick-write
 * panel) comes here: changed on the device meanwhile? a ⚠ register? Then it
 * is written, and the engine reads it back: the table shows what the device
 * holds, never what was typed. */
void MainWindow::onWriteRequested(int row, const QString &text, const QByteArray &base) {
	const RegDef def = model_->rows()[row].def;
	const RegisterModel::Row &now = model_->rows()[row];
	if (!base.isEmpty() && now.valid && now.raw != base && !confirmOverwrite(def, base, now.raw, text)) {
		logEvent(LogLevel::Info, tr("%1: write of %2 cancelled (the value changed meanwhile)").arg(def.name, text));
		return;
	}
	QByteArray bytes;
	QString err;
	if (!encodeValue(def, text, bytes, err)) {
		logEvent(LogLevel::Error, tr("%1: %2 not written: %3").arg(def.name, text, err)); /* and a pop-up */
		return;
	}
	/* past the map's min or max: only when the user says so (a special value is always allowed) */
	const QString outside = def.isNumeric() ? limitProblem(def, decodeNumber(def, bytes)) : QString();
	if (!outside.isEmpty() && !confirmed(this, tr("Outside the map's limits"),
			tr("<b>%1</b> = <b>%2</b> is %3 the map gives it.<br><br>The map may be stricter than the device, or the "
			   "device may not take it. Write it anyway?").arg(def.name.toHtmlEscaped(), text.toHtmlEscaped(),
					outside.toHtmlEscaped()),
			tr("Write anyway"))) {
		logEvent(LogLevel::Info, tr("%1: write of %2 cancelled (%3)").arg(def.name, text, outside));
		return;
	}
	if (def.danger && !confirmDangerWrite(def, text, bytes)) {
		logEvent(LogLevel::Info, tr("%1: write of %2 cancelled at the confirmation").arg(def.name, text));
		return;
	}
	const QString what = tr("%1 = %2 (%3 at %4)").arg(def.name, text, evre::hex(bytes), addrText(def.addr));
	engineWrite(slaveOf(def), def.addr, bytes, true, [this, name = def.name, what](bool ok, const QString &message) {
		if (ok) {
			statusBar()->showMessage(tr("%1 written").arg(name), 3000);
			logEvent(LogLevel::Info, tr("written: %1").arg(what));
		} else {
			logEvent(LogLevel::Error, tr("write refused: %1: %2").arg(what, message)); /* and a pop-up */
		}
	});
}

/* the device, or another client, changed the register while this edit was open: write anyway? */
bool MainWindow::confirmOverwrite(const RegDef &def, const QByteArray &base, const QByteArray &now,
		const QString &text) {
	return confirmed(this, tr("Value changed while editing"),
			tr("<b>%1</b> changed while you were editing:<br><br>"
			   "when you started: <b>%2</b><br>now on the device: <b>%3</b><br>you typed: <b>%4</b><br><br>"
			   "Something else writes this register (the device itself, or another client). Write yours anyway?")
					.arg(def.name.toHtmlEscaped(), formatValue(def, base).toHtmlEscaped(),
							formatValue(def, now).toHtmlEscaped(), text.toHtmlEscaped()),
			tr("Write anyway"));
}

/* a ⚠ register: the value (and its name), what the register is for, and a question */
bool MainWindow::confirmDangerWrite(const RegDef &def, const QString &text, const QByteArray &bytes) {
	QString question = tr("Write <b>%1</b> to <b>%2</b> (%3)?")
			.arg(text.toHtmlEscaped(), def.name.toHtmlEscaped(), addrText(def.addr));
	const qint64 raw = qint64(decodeRaw(def, bytes));
	if (def.enumValues.contains(raw))
		question += QStringLiteral("<br><br>= %1").arg(def.enumValues.value(raw).toHtmlEscaped());
	if (!def.desc.isEmpty())
		question += QStringLiteral("<br><br>") + coloredSpan(def.desc.toHtmlEscaped(), Theme::colors().muted);
	question += tr("<br><br>This register moves, powers or changes something on the device.");
	return confirmed(this, tr("Confirm write"), question, tr("Write"));
}

void MainWindow::sendRawRead(quint8 slave, quint16 addr, quint16 count) {
	engineRead(slave, addr, count, false, [this, addr](bool ok, const QByteArray &data, const QString &message, double ms) {
		monitorTab_->showAnswer(addr, ok, data, message, ms);
	});
}

/* WRITE + ack: the device's answer. WRITE (no ack): sent, and nothing says more (not "OK"). A broadcast (slave 0):
 * only as the broadcast rule allows */
void MainWindow::sendRawWrite(quint8 slave, quint16 addr, const QByteArray &bytes, bool ack) {
	if (slave == evre::BROADCAST) {
		const QString refusal = broadcastRefusal(addr, bytes);
		if (!refusal.isEmpty()) {
			monitorTab_->showNote(tr("!! no broadcast: %1").arg(refusal));
			logEvent(LogLevel::Warning, tr("broadcast of %1 at %2 not sent: %3").arg(evre::hex(bytes), addrText(addr), refusal));
			return;
		}
	}
	const QString what = slave == evre::BROADCAST
			? tr("raw broadcast %1 at %2 (every device, none answers)").arg(evre::hex(bytes), addrText(addr))
			: tr("raw write %1 at %2").arg(evre::hex(bytes), addrText(addr));
	engineWrite(slave, addr, bytes, ack, [this, addr, bytes, ack, what](bool ok, const QString &message) {
		if (ok && !ack) {
			monitorTab_->showSent(addr, bytes);
			logEvent(LogLevel::Info, tr("%1: sent (no acknowledge)").arg(what));
			return;
		}
		monitorTab_->showAnswer(addr, ok, {}, message, -1);
		logEvent(ok ? LogLevel::Info : LogLevel::Error,
				ok ? tr("%1: OK").arg(what) : tr("%1: refused: %2").arg(what, message));
	});
}

/* ------------------------------------------------------------------ the chart */

void MainWindow::onPlotChanged(int row, bool on) {
	chartTab_->plotRegister(model_->rows()[row].def, on);
	pushPlotted();
}

void MainWindow::unplotAll() {
	for (int row = 0; row < model_->rows().size(); row++) model_->setPlot(row, false);
}

/* -------------------------------------------------------------- CSV recording */

void MainWindow::toggleRecord() {
	if (recording_) {
		stopRecord();
		return;
	}
	const QString suggested = QDir::homePath() + QStringLiteral("/evre_%1.csv")
			.arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
	const QString file = QFileDialog::getSaveFileName(this, tr("Record to CSV"), suggested, tr("CSV (*.csv)"));
	if (!file.isEmpty()) startRecord(file);
}

/* the columns: every register ticked Log that the device has and a poll reads (no long byte array) */
void MainWindow::startRecord(const QString &file) {
	QVector<RegKey> columns;
	for (const RegisterModel::Row &row : model_->rows()) {
		if (!row.log || row.unavailable || !isPollable(row.def)) continue;
		columns << regKey(row.def);
	}
	recordFile_ = file;
	engine_->post([engine = engine_, file, columns] { engine->startRecord(file, columns); });
}

void MainWindow::stopRecord() {
	engine_->post([engine = engine_] { engine->stopRecord(); });
}

void MainWindow::onRecordStarted(bool ok, const QString &err) {
	if (!ok) {
		logEvent(LogLevel::Error, tr("CSV recording not started: %1").arg(err));
		QMessageBox::warning(this, tr("Cannot record"), err);
		return;
	}
	logEvent(LogLevel::Info, tr("CSV recording started"));
	recording_ = true;
	recordFrom_ = engine_->now();
	sidebar_->setRecording(true);
	RecordingWindow::remember(recordFile_);
}

void MainWindow::saveRecordingNotes() {
	if (!recording_) return;
	QVector<ChartNote> notes;
	for (const ChartNote &note : chartTab_->view()->notes())
		if (note.time >= recordFrom_) notes << note;
	QString error;
	if (!recording::saveNotes(recordFile_, notes, chartTab_->view()->epochMs(), error))
		logEvent(LogLevel::Warning, tr("notes not saved beside %1: %2").arg(QDir::toNativeSeparators(recordFile_), error));
}

void MainWindow::onRecordStopped(const QString &file, quint64 rows) {
	saveRecordingNotes(); /* the last time, with the notes as they are now */
	recording_ = false;
	sidebar_->setRecording(false);
	sidebar_->showRecordSaved(file, rows);
	logEvent(LogLevel::Info, tr("CSV recording stopped: %1 rows in %2").arg(rows).arg(QDir::toNativeSeparators(file)));
}

/* ------------------------------------------------------------- the API server */

void MainWindow::setApiRunning(bool on) {
	if (!on) {
		engine_->post([engine = engine_] { engine->apiStop(); });
		return;
	}
	QSettings settings;
	const quint16 evrePort = quint16(settings.value(QStringLiteral("api/evrePort"), 1219).toUInt());
	const quint16 jsonPort = quint16(settings.value(QStringLiteral("api/jsonPort"), 1220).toUInt());
	const bool network = sidebar_->apiNetwork();
	pushApiWrites();
	engine_->post([engine = engine_, evrePort, jsonPort, network] { engine->apiStart(evrePort, jsonPort, network); });
}

void MainWindow::onApiStarted(bool ok, const QString &err) {
	if (ok) {
		logEvent(LogLevel::Info, tr("API server started"));
		return;
	}
	logEvent(LogLevel::Error, tr("API server not started: %1").arg(err));
	sidebar_->showApiNotStarted(err);
}

/* ---------------------------------------------- the event log, the status bar */

void MainWindow::logEvent(LogLevel level, const QString &text) { eventLog_->add(level, text); }

void MainWindow::showSlowHint() { statusSlow_->setFullText(sidebar_->slowPollHint()); }

void MainWindow::resizeEvent(QResizeEvent *event) {
	QMainWindow::resizeEvent(event);
	notice_->place(); /* the hint in the status bar is cut again to its room by itself (ElidedLabel) */
}

void MainWindow::refreshStatus() {
	const IoEngine::Stats stats = engine_->stats();
	const evre::Master::Stats &link = stats.master;
	logTimeouts(link.timeouts);
	logCounterIncrease(link.errors, loggedErrors_, tr("%1 error answer(s) from the device (%2 in all)"));
	logCounterIncrease(link.badFrames, loggedBadFrames_, tr("%1 bad frame(s) received (%2 in all)"));
	statusLatency_->setText(tr("Latency %1 ms").arg(link.avgLatencyMs, 0, 'f', 1));
	statusTraffic_->setText(tr("TX %1 · RX %2").arg(link.tx).arg(link.rx));
	statusErrors_->setText(tr("Timeouts %1 · Errors %2 · Bad frames %3")
			.arg(link.timeouts).arg(link.errors).arg(link.badFrames));
	sidebar_->showStats(stats, connected_);
	if (stats.pollHz > 0) measuredPollHz_ = std::ceil(stats.pollHz / 100.0) * 100.0; /* steps of 100: no flapping */
	if (sidebar_->pollIntervalMs() <= 0) updatePlotLimit();
	showSlowHint();
	chartTab_->refreshStatus();
	registersTab_->refreshStatus();
}

/* A device of a bus that is offline is asked for its DEVICE_ID every OFFLINE_RETRY_MS and times out each time: said
 * once (offline), not again. Only the counter is shared, so those retries are allowed for - one per offline device
 * per retry interval, at most two in hand each - and a timeout past them is another device's, logged as always. */
void MainWindow::logTimeouts(quint64 total) {
	if (offlineDevices_.isEmpty()) {
		retryTimeoutsAllowed_ = 0;
	} else if (total > loggedTimeouts_) {
		const int offline = int(offlineDevices_.size());
		retryTimeoutsAllowed_ = std::min(2.0 * offline,
				retryTimeoutsAllowed_ + double(offline) * double(retryClock_.restart()) / IoEngine::OFFLINE_RETRY_MS);
		const quint64 retries = std::min(total - loggedTimeouts_, quint64(retryTimeoutsAllowed_));
		retryTimeoutsAllowed_ -= double(retries);
		loggedTimeouts_ += retries;
	}
	logCounterIncrease(total, loggedTimeouts_, tr("%1 request(s) timed out (%2 in all)"));
}

/* a counter of link problems went up since it was last logged: a warning with how many are new */
void MainWindow::logCounterIncrease(quint64 total, quint64 &logged, const QString &text) {
	if (total > logged) logEvent(LogLevel::Warning, text.arg(total - logged).arg(total));
	logged = total;
}

/* ------------------------------------------------------------ theme, and help */

void MainWindow::toggleTheme() {
	Theme::apply(*qApp, !Theme::isDark());
	model_->setColors(registerColors());
	sidebar_->themeChanged();
	updateMapInfo();
	showBus(); /* the devices' dots in the new look */
	showDeviceInfo(); /* its amber too */
	chartTab_->themeChanged();
}

void MainWindow::showHelp() {
	auto *dialog = new HelpDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->show();
}

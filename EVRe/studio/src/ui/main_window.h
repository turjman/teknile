/* SPDX-License-Identifier: Apache-2.0 */
/* The main window: the sidebar at the left (ui/sidebar.h), the Registers,
 * Chart, Monitor, Map editor and Log tabs beside it, and the status bar under them.
 *
 * The map is a MapDocument (model/map_document.h): the Map editor, the Map
 * settings dialog and the Registers tab change it there, with undo; the table,
 * the engine and the sidebar follow its changes.
 *
 * Several devices on the link: a bus (model/bus_file.h). The table then holds
 * every device's registers, named after it (D1_SUPPLY_V) and each with its
 * slave (RegDef::slave); the Registers tab shows the selected device, whose
 * map the document holds and the Map editor edits. Another device's map is
 * kept as loaded or last saved (busMaps_). The chart, the math lines, the CSV
 * and the API see every device. A broadcast (slave 0) goes out only as the
 * rule of model/bus_file.h allows. That part of the window is in
 * main_window_bus.cpp.
 *
 * Everything that talks to the device - the link, the poller, CSV recording,
 * the API server - is the IoEngine, on its own thread. The window sends it
 * requests (IoEngine::post) and copies its values and samples once per
 * display frame (sync(), paced by the FrameClock), so drawing never delays a
 * poll. The window itself only puts the parts together and wires them: each
 * part does its own work, and the window passes the requests and the answers
 * between them and the engine. */
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <functional>

#include "model/bus_file.h"
#include "model/device_map.h"
#include "ui/value_pace.h"

class ChartTab;
class ElidedLabel;
class EventLog;
class MapDocument;
class MapEditorTab;
class FrameClock;
class IoEngine;
class MonitorTab;
class Notice;
class QLabel;
class QTabWidget;
class QThread;
class RegisterModel;
class RegistersTab;
class Sidebar;
struct RegValue;
enum class LogLevel;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	/* the main tabs, in their order */
	enum Tab { TabRegisters, TabChart, TabMonitor, TabMap, TabLog };

	/* what the command line asks for (main.cpp), applied once the window is shown */
	struct Startup {
		QString tcp;             /* host:port */
		QString serial;          /* COMx[:baud] */
		QString map;             /* a map for this run only: the next start opens the one chosen last */
		QString bus;             /* a bus file for this run only (several devices on the link), in place of a map */
		QString tab;             /* registers, chart, monitor or map */
		QStringList plot;        /* register names to chart */
		bool connect = false;
		double interval = -1;    /* poll interval, ms (0 = as fast as possible); < 0 = as saved */
		QString record;          /* CSV file to record into from the start */
		int inFlight = 0;        /* requests in flight; 0 = as saved */
		bool api = false, apiWrites = false, apiDanger = false;
	};

	/* mapAtStart, busAtStart: the command line's map or bus, opened in place of the one chosen last (not both: one
	 * "map loaded") */
	explicit MainWindow(const QString &mapAtStart = QString(), const QString &busAtStart = QString(),
			QWidget *parent = nullptr);
	~MainWindow() override;
	void applyStartup(const Startup &startup);
	/* a recording in a window of its own (recording_window.h): this file, or one chosen (empty); its registers matched
	 * with the map loaded now */
	void openRecording(const QString &file);
	/* Restart now (the language): true when the window closed for it; main() then starts the program again */
	static bool restartAsked();

protected:
	void closeEvent(QCloseEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override; /* a .csv dropped: opened as a recording */
	void dropEvent(QDropEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	/* the answers to requests sent through the engine */
	using ReadDone = std::function<void(bool ok, const QByteArray &data, const QString &message, double ms)>;
	using WriteDone = std::function<void(bool ok, const QString &message)>;

	/* building the window: the constructor, in this order */
	void startEngine();           /* its thread, and its results into the window */
	void buildWindow();           /* the sidebar, the tabs, the status bar */
	void connectSidebar();        /* its choices and buttons, to the engine and the map */
	QWidget *buildRegistersTab(); /* its writes, reads and map edits go through the window */
	QWidget *buildChartTab();     /* wired to the engine and the table */
	QWidget *buildMonitorTab();   /* its requests are sent through the engine */
	QWidget *buildMapTab();       /* the map made and changed, through the document */
	void buildEventLog();         /* the Log tab and the pop-up beside the tabs */
	void buildStatusBar();
	void connectModel();          /* the table's changes, to the engine and the chart */
	void startClocks();           /* the frame clock, the statistics, the reconnect timer */
	void restoreSettings();       /* the link, the map, the window's geometry, the API */

	/* the link */
	void toggleConnect();
	void connectLink();
	void retryConnect();          /* Reconnect by itself, after a lost link */
	void disconnectLink();
	void onOpened(const QString &link);
	void onClosed(const QString &why);
	/* slave: the device's table slave (0: the one device of a map) */
	void onDeviceInfo(int slave, bool ok, quint16 id, quint16 status, const QString &err);
	void onDeviceOnline(int slave, bool online); /* a device of a bus stopped answering, or answers again */
	/* under the pill: what is known of the shown device's ID, built when shown (the amber of the look now) */
	void showDeviceInfo();
	QString deviceInfoHtml(int slave) const;
	/* AUTO_SEND: the sidebar's choice to the engine, and whether the box may be ticked (one device whose STATUS has
	 * CAP_AUTO_SEND) */
	void pushAutoSend();
	void updateAutoSendOffer();
	void onAutoSendSet(bool on, int hz, const QString &err);
	/* Fast EVRe: the map's streams in the sidebar's card, offered with one device connected */
	void showFastStreams();
	void updateFastOffer();
	void onFastStreamSet(int stream, bool on, double rate, const QString &err);
	void stopFastStreams(); /* Disconnect asked for: no stream switched on again at the next connect */
	void onFastPlotToggled(int stream, int channel, bool on); /* a channel's Plot tick: its line, within the lines' limit */
	QHash<int, QVector<double>> fastValues_; /* each stream's newest record, its channels' values */
	void refreshPorts();          /* the sidebar's serial ports, the map's device named among them */

	/* the map */
	void loadMap(const QString &file, bool remember = true); /* remember: open it at the next start */
	void openMap();
	void saveMap(bool saveAs);
	void newMap();
	void onMapChanged();          /* the document changed: the table, the engine, the sidebar follow */
	void showMapSettings();
	void updateMapInfo();
	bool keepMapEdits();          /* before the map edited gives way: saved or dropped (true), or cancelled */

	/* several devices on the link (a bus), main_window_bus.cpp; `device` and `index` are indexes into bus_.devices */
	bool isBus() const { return !bus_.devices.isEmpty(); }
	QVector<RegDef> tableDefinitions() const; /* the map's registers, or every device's, named after it */
	const DeviceMap &deviceMap(int device) const; /* the selected device's file: as edited, else as loaded */
	int deviceOfSlave(int slave) const;           /* -1: none */
	QString deviceLabel(int slave) const;         /* "D2: " before a log line about a device; empty with one */
	uint8_t slaveOf(const RegDef &def) const;     /* where a request for this register goes */
	uint8_t selectedSlave() const;                /* the selected device's address, or the sidebar's */
	QVector<const DeviceMap *> allMaps() const;   /* every device's map: the broadcast rule's input */
	QString broadcastRefusal(uint16_t addr, int count) const; /* model/bus_file.h; empty: it may go */
	QString broadcastRefusal(uint16_t addr, const QByteArray &bytes) const; /* and never AUTO_SEND on everywhere */
	QString presetsBlocked() const;               /* why the bus's presets cannot be sent now; empty: they can */
	bool loadBus(const QString &file, bool remember = true);
	bool loadBusMap(const QString &path, QString &err); /* into busMaps_, numbered for the table */
	void openBus();
	bool saveBus(bool saveAs);
	void newBus();                /* a bus of the map's device */
	void closeBus();              /* back to one device: the selected one's map */
	bool keepBusEdits();          /* before the bus gives way: saved or dropped (true), or cancelled */
	void clearBus();              /* no bus: one device, nothing of the bus kept */
	void addDevice();
	void editDevice(int device);
	void removeDevice(int device);
	void selectDevice(int device);
	void onBusChanged();          /* the devices changed, or the one selected: the table, the engine, the tabs follow */
	void showBus();               /* the sidebar's Devices card */

	/* the engine: what it must know, and requests that are answered */
	void pushMap();               /* the table's definitions */
	void pushPolling();
	/* the samples a second each line gets (Auto send's rate, the polling interval's, or the polls measured when the
	 * interval is 0), and the registers the chart may hold at it (RegisterModel::plotLimitFor) */
	double sampleRateHz() const;
	void updatePlotLimit();
	void pushLinkOptions();
	void pushPlotted();           /* the registers to sample: plotted, or read by a math line */
	void pushApiWrites();
	void updateStaleAfter();      /* how old a value may get before the table greys it */
	void applyValuePace();        /* how often the table's and the legend's values change (value_pace.h) */
	/* slave: where it goes; a write to evre::BROADCAST (no acknowledge) reaches every device */
	void engineRead(uint8_t slave, uint16_t addr, uint16_t count, bool intoTable, ReadDone done);
	void engineWrite(uint8_t slave, uint16_t addr, const QByteArray &bytes, bool ack, WriteDone done);

	/* once per display frame: the engine's values into the table, its samples into the chart,
	 * its frames into the monitor */
	void sync();
	void logReadStateChange(int row, const RegValue &value); /* reads of a register fail, or work again */

	/* writes */
	void onWriteRequested(int row, const QString &text, const QByteArray &base);
	bool confirmOverwrite(const RegDef &def, const QByteArray &base, const QByteArray &now, const QString &text);
	bool confirmDangerWrite(const RegDef &def, const QString &text, const QByteArray &bytes);
	/* "To all devices": the value in one broadcast frame, then each device read back */
	void onBroadcastRequested(int row, const QString &text);
	void broadcastValue(const RegDef &def, const QString &text); /* the checks, the confirmation, the frame, the reads */
	/* the broadcasts kept in the bus file: sent, made, removed */
	void sendPreset(int preset);
	void newPreset();
	void removePreset(int preset);
	/* the Monitor tab's requests: the answers go back to the tab */
	void sendRawRead(quint8 slave, quint16 addr, quint16 count);
	void sendRawWrite(quint8 slave, quint16 addr, const QByteArray &bytes, bool ack);

	/* the chart */
	void onPlotChanged(int row, bool on);
	void unplotAll();             /* every register off the chart */

	/* CSV recording */
	void toggleRecord();
	void startRecord(const QString &file);
	void stopRecord();
	void onRecordStarted(bool ok, const QString &err);
	void onRecordStopped(const QString &file, quint64 rows);
	/* the live chart's notes since the recording started, beside it ("<file>.notes.json"), at every change */
	void saveRecordingNotes();

	/* the API server */
	void setApiRunning(bool on);
	void onApiStarted(bool ok, const QString &err);

	/* the event log, the status bar, the theme, help */
	void logEvent(LogLevel level, const QString &text); /* the Log tab, logs/studio_<date>.log, a pop-up */
	void refreshStatus();         /* twice a second: the statistics everywhere */
	void showSlowHint();          /* the status bar's hint (cut to its room, the whole of it in the tooltip) */
	void logTimeouts(quint64 total); /* the timeouts not made by an offline device's retries */
	void logCounterIncrease(quint64 total, quint64 &logged, const QString &text);
	void toggleTheme();
	void showHelp();

	/* the map and the table */
	MapDocument *doc_ = nullptr;  /* the map, as edited */
	QString mapAtStart_;          /* the command line's map, opened at start for this session */
	QString busAtStart_;          /* the command line's bus, the same way */
	QString portsKey_;            /* the map's USB IDs and device name the port list was made for */
	QString rememberedMap_;       /* absolute path, saved on close */
	QString rememberedBus_;       /* the same for a bus; empty: none (the map opens) */
	RegisterModel *model_;

	/* several devices on the link; no devices: one, the map's */
	BusFile bus_;                 /* the device maps' paths are kept absolute here */
	int selectedDevice_ = -1;     /* the device whose map doc_ holds; -1: no bus */
	QHash<QString, DeviceMap> busMaps_; /* each device's map, by its absolute path, as loaded or last saved */
	bool busModified_ = false;
	bool allDevices_ = false;           /* the Registers tab shows every device's registers, not the selected one's */
	QSet<uint8_t> offlineDevices_;      /* the slaves of the devices that stopped answering */
	/* per device's table slave: what its DEVICE_ID read gave; none: not read yet. The text is made when shown */
	struct DeviceInfo {
		enum class State { Read, NotRead, TokenRefused } state = State::Read;
		quint16 id = 0;
		int revision = 0;
		QString error;          /* NotRead, TokenRefused: why */
	};
	QHash<int, DeviceInfo> deviceInfo_;
	int autoSendCap_ = -1;              /* the one device's STATUS has CAP_AUTO_SEND: 1, 0, -1 not read yet */
	bool autoSendAsked_ = false;        /* the engine was last told: on */
	int autoSendHz_ = 0;                /* the device sends at this rate: on and taken; 0: off */
	double measuredPollHz_ = 0;         /* the polls a second, as last measured (an interval of 0) */

	/* the engine, and copying its values (sync) */
	IoEngine *engine_;
	QThread *ioThread_;
	quint64 mapGeneration_ = 0;         /* which map the engine's values belong to */
	QVector<quint64> copiedVersions_;   /* per row, the engine's version copied last */
	ValuePacer valuePacer_;             /* when the next copy is due: at the pace chosen for the values */
	QElapsedTimer valueClock_;
	QHash<int, qint64> readErrorLoggedMs_; /* per row: when its last read error was logged */

	/* requests that are answered: their callbacks, by request id */
	quint64 nextRequestId_ = 1;
	QHash<quint64, ReadDone> pendingReads_;
	QHash<quint64, WriteDone> pendingWrites_;

	/* the link, and recording */
	bool connected_ = false;
	bool wantConnected_ = false;  /* Connect clicked and not Disconnect: a lost link is tried again */
	bool retrying_ = false;       /* a reconnect by itself: not logged each time */
	bool recording_ = false;
	QString recordFile_;          /* the file being recorded into (asked for: startRecord; certain: onRecordStarted) */
	double recordFrom_ = 0;       /* the time base when it started: the notes from then on go beside it */

	/* the clocks */
	FrameClock *frameClock_ = nullptr;
	QTimer statusTimer_;          /* refreshStatus, every 500 ms */
	QTimer reconnectTimer_;
	/* the link counters logged so far: only what is new is logged */
	quint64 loggedTimeouts_ = 0, loggedErrors_ = 0, loggedBadFrames_ = 0;
	/* the timeouts the offline devices' retries make (one per device each IoEngine::OFFLINE_RETRY_MS), not logged */
	double retryTimeoutsAllowed_ = 0;
	QElapsedTimer retryClock_;

	/* the parts of the window */
	Sidebar *sidebar_ = nullptr;           /* the link, the map, polling and the API, at the left */
	QTabWidget *tabs_ = nullptr;
	RegistersTab *registersTab_ = nullptr;
	MapEditorTab *mapTab_ = nullptr;
	ChartTab *chartTab_ = nullptr;
	MonitorTab *monitorTab_ = nullptr;
	EventLog *eventLog_ = nullptr;         /* the Log tab */
	Notice *notice_ = nullptr;             /* the pop-up, in the free space right of the tabs */
	QLabel *statusLatency_ = nullptr, *statusTraffic_ = nullptr, *statusErrors_ = nullptr; /* the status bar */
	/* why the polls are slower than asked, in amber (theme: #statusSlow); always there, empty when they are not */
	ElidedLabel *statusSlow_ = nullptr;
};

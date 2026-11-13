/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's session (PhoneSession, a singleton for QML): what MainWindow does with the engine, the phone's part of it
 * again (Q-6), so the desktop's window stays as it is. It owns the IoEngine on its thread, the map and its
 * RegisterModel, the USB ports (PortList) and the Log (LogModel).
 *
 *   the map       from the app's maps (maps/ beside the program, the APK's assets:/maps on Android), example_device.json
 *                 first, or a file the picker gives (a content:// path on Android); the last one opens at the next start
 *   the link      a USB device (AndroidUsbLink through the UsbBridge: Android's, or the test's fake) or TCP (host,
 *                 port, a token that is never saved); its states below, each with its words for the drawer and the
 *                 header's pill; Reconnect by itself tries again 0.5 s after a lost link and every 2 s after a failed
 *                 try, as the desktop does, except after a refused token, a device that wants one, or a refused USB
 *                 permission: those would only be refused again, so the link stays closed until Connect
 *   polling       on or off and its interval; the measured rate twice a second while connected
 *   writes        Allow writes (off at every start, as on the desktop); write(row, text) is MainWindow::onWriteRequested's
 *                 path: encodeValue, the map's limits and a danger register asked first (writeQuestion), the write, and
 *                 the engine reads it back, so the card shows what the device holds; writeAnswered says how it went
 *   registers     RegisterList, the Registers screen's rows over the RegisterModel; the values copied from the engine's
 *                 table by sync(), once a frame of the window (afterAnimating), at the desktop's pace (ui/valueRate);
 *                 Plot (setPlot) with the desktop's limit of lines and its message (notice)
 *   the chart     the Chart screen's ChartItem (setChart, by ChartScreen.qml): a line per plotted register, in the
 *                 theme's next series colour as the desktop gives them (ChartTab::plotRegister), named and with its
 *                 unit; sync() hands it each poll's samples (engine takeSamples, as MainWindow::sync) on the engine's
 *                 clock; Unplot (unplotLine, the legend's chip) takes the register off as the card's menu does. A
 *                 disconnect keeps the lines and their samples, as on the desktop
 *   fast lines    a map's streams (Fast EVRe): a channel plotted (setFastPlot) switches its stream on, the last one
 *                 taken off switches it off; the blocks (engine takeFastBlocks) go into the stream's store (fast::Store,
 *                 as ChartView::appendFast, its marks as markFast), kept for the chart's memory; the chart bins them
 *                 by ChartData::binFast
 *   the ID        DEVICE_ID read when the link opens (the engine's); a read that gets no answer is tried again
 *                 ID_RETRIES times, ID_RETRY_MS apart (a board still booting when the link comes back), "reading the
 *                 device ID…" meanwhile; a refusal (permission denied) is not tried again: it is the token's
 *   recording     a CSV of every logged register into the app's own folder (recordingsFolder, A-8): a real path, so a
 *                 recording's notes can go beside it
 *   the system    while a link is wanted, Android's foreground service with its notification (the link's state, and
 *                 the recording's time once a second), so both go on with the screen off; the screen kept on while
 *                 connected (keepScreenOn, android/keepScreenOn, on by default); the orientation a screen asks for
 *                 (Main.qml, A-16). Through SystemBridge (phone_system.h): Android's, or a test's
 *
 * QML only calls it on the window's thread; the engine is reached by post() and answers by queued signals, as
 * MainWindow does. Every text through tr() in this class's context. */
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "model/device_map.h"
#include "model/fast_store.h"
#include "phone/chart_item.h"
#include "phone/log_model.h"
#include "phone/port_list.h"
#include "phone/register_list.h"
#include "phone/phone_system.h"
#include "ui/value_pace.h"

class IoEngine;
class QQuickWindow;
class QThread;
class RegisterModel;
namespace evre {
class UsbBridge;
}

class PhoneSession : public QObject {
	Q_OBJECT
	QML_ELEMENT
	QML_SINGLETON
	Q_PROPERTY(State state READ state NOTIFY linkChanged)
	Q_PROPERTY(bool wantConnected READ wantConnected NOTIFY linkChanged)
	Q_PROPERTY(QString pillText READ pillText NOTIFY linkChanged)
	Q_PROPERTY(QString linkText READ linkText NOTIFY linkChanged)
	Q_PROPERTY(QString linkDetail READ linkDetail NOTIFY linkChanged)
	Q_PROPERTY(QString connectText READ connectText NOTIFY linkChanged)
	Q_PROPERTY(int deviceId READ deviceId NOTIFY deviceChanged)
	Q_PROPERTY(QString deviceText READ deviceText NOTIFY deviceChanged)
	Q_PROPERTY(QString deviceWarning READ deviceWarning NOTIFY deviceChanged)
	Q_PROPERTY(QString mapName READ mapName NOTIFY mapChanged)
	Q_PROPERTY(QString mapFile READ mapFile NOTIFY mapChanged)
	Q_PROPERTY(QString mapText READ mapText NOTIFY mapChanged)
	Q_PROPERTY(QString mapError READ mapError NOTIFY mapChanged)
	Q_PROPERTY(QStringList mapNames READ mapNames CONSTANT)
	Q_PROPERTY(QStringList mapFiles READ mapFiles CONSTANT)
	Q_PROPERTY(bool tcp READ tcp WRITE setTcp NOTIFY settingsChanged)
	Q_PROPERTY(QString host READ host NOTIFY settingsChanged)
	Q_PROPERTY(int port READ port NOTIFY settingsChanged)
	Q_PROPERTY(QString usbDevice READ usbDevice NOTIFY settingsChanged)
	Q_PROPERTY(int baud READ baud NOTIFY settingsChanged)
	Q_PROPERTY(QVariantList baudRates READ baudRates CONSTANT)
	Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY settingsChanged)
	Q_PROPERTY(bool polling READ polling WRITE setPolling NOTIFY settingsChanged)
	Q_PROPERTY(int intervalMs READ intervalMs WRITE setIntervalMs NOTIFY settingsChanged)
	Q_PROPERTY(double pollHz READ pollHz NOTIFY statsChanged)
	Q_PROPERTY(QString pollText READ pollText NOTIFY statsChanged)
	Q_PROPERTY(bool writesEnabled READ writesEnabled WRITE setWritesEnabled NOTIFY settingsChanged)
	Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
	Q_PROPERTY(QString recordFile READ recordFile NOTIFY recordingChanged)
	Q_PROPERTY(QString recordText READ recordText NOTIFY recordingChanged)
	Q_PROPERTY(PortList *ports READ ports CONSTANT)
	Q_PROPERTY(LogModel *log READ log CONSTANT)
	Q_PROPERTY(RegisterList *registerList READ registerList CONSTANT)
	Q_PROPERTY(bool connected READ connected NOTIFY linkChanged)
	Q_PROPERTY(ChartItem *chart READ chart WRITE setChart NOTIFY chartChanged)
	Q_PROPERTY(QVariantList fastChannels READ fastChannels NOTIFY fastChanged)
	Q_PROPERTY(bool keepScreenOn READ keepScreenOn WRITE setKeepScreenOn NOTIFY settingsChanged)
	Q_PROPERTY(QString serviceText READ serviceText NOTIFY systemChanged)
	Q_PROPERTY(bool screenKeptOn READ screenKeptOn NOTIFY systemChanged)
	Q_PROPERTY(QString orientation READ orientation WRITE setOrientation NOTIFY systemChanged)

public:
	/* Off: no link, none wanted. Waiting: Android's permission dialog for the USB device is up. Connecting: a try under
	 * way. Connected. Reconnecting: lost or refused, the next try comes by itself. Closed: lost or refused, and no try
	 * comes (linkText says why) */
	enum State { Off, Waiting, Connecting, Connected, Reconnecting, Closed };
	Q_ENUM(State)
	static constexpr int TIMEOUT_MS = 500;           /* an answer's time, the desktop's default */
	static constexpr int RECONNECT_LOST_MS = 500;    /* a link that was up: again soon */
	static constexpr int RECONNECT_FAILED_MS = 2000; /* a try that failed: not faster than this */
	static constexpr int STATS_MS = 500;
	static constexpr int SYNC_IDLE_MS = 50;  /* no frame drawn for this long: sync() by the timer (nothing moves) */
	static constexpr int ID_RETRIES = 3;     /* a DEVICE_ID read without an answer: tried again this often */
	static constexpr int ID_RETRY_MS = 1000;
	/* a fast line's id on the chart: past every register's (regKey: 24 bits), 256 channels a stream */
	static constexpr int FAST_LINE = 1 << 28;
	static int fastLineId(int stream, int channel) { return FAST_LINE + stream * 256 + channel; }

	explicit PhoneSession(QObject *parent = nullptr);
	~PhoneSession() override;

	State state() const { return state_; }
	bool wantConnected() const { return wantConnected_; }
	QString pillText() const;
	QString linkText() const { return linkText_; }
	QString linkDetail() const { return linkDetail_; }
	QString connectText() const;
	int deviceId() const { return deviceId_; }
	QString deviceText() const { return deviceText_; }
	QString deviceWarning() const { return deviceWarning_; }
	QString mapName() const { return map_.device; }
	QString mapFile() const { return mapFile_; }
	QString mapText() const;
	QString mapError() const { return mapError_; }
	QStringList mapNames() const { return mapNames_; }
	QStringList mapFiles() const { return mapFiles_; }
	bool tcp() const { return tcp_; }
	void setTcp(bool tcp);
	QString host() const { return host_; }
	int port() const { return port_; }
	QString usbDevice() const { return usbDevice_; }
	int baud() const { return baud_; }
	QVariantList baudRates() const;
	bool autoReconnect() const { return autoReconnect_; }
	void setAutoReconnect(bool on);
	bool polling() const { return polling_; }
	void setPolling(bool on);
	int intervalMs() const { return intervalMs_; }
	void setIntervalMs(int ms);
	double pollHz() const { return pollHz_; }
	QString pollText() const;
	bool writesEnabled() const;
	void setWritesEnabled(bool on);
	bool recording() const { return recording_; }
	QString recordFile() const { return recordFile_; }
	QString recordText() const { return recordText_; }
	PortList *ports() const { return ports_; }
	LogModel *log() const { return log_; }
	RegisterList *registerList() const { return list_; }
	bool connected() const { return connected_; }
	ChartItem *chart() const { return chart_; }
	void setChart(ChartItem *chart);
	/* the map's streams' channels for the Chart screen's Fast lines menu: { stream, channel, name ("ADC.I_LOAD"), unit,
	 * plotted } */
	QVariantList fastChannels() const;
	const fast::Store *fastStore(int stream) const { return stores_.value(stream).get(); }
	RegisterModel *registers() const { return model_; }
	const DeviceMap &map() const { return map_; }

	/* the link: each saves what it was given (but the token), then connects; a try already under way is replaced */
	Q_INVOKABLE void connectUsb(const QString &device, int baud);
	Q_INVOKABLE void connectTcp(const QString &host, int port, const QString &token);
	Q_INVOKABLE void disconnectLink(); /* Disconnect, Cancel and Stop reconnecting: no try comes by itself */
	/* a map file: a path, a file: URL or a content:// one from the picker; false (and mapError) when it did not load */
	Q_INVOKABLE bool loadMap(const QString &file);
	Q_INVOKABLE void refreshPorts();
	Q_INVOKABLE void startRecord();
	Q_INVOKABLE void stopRecord();

	/* the write sheet; row: RegisterModel's (RegisterList's `source`) */
	Q_INVOKABLE QString editText(int row) const;     /* the value now, as the field starts with it */
	Q_INVOKABLE QVariantList choices(int row) const; /* an enum's values: { text: "blink (2)", value: "2" }; empty: none */
	Q_INVOKABLE QString limitsText(int row) const;   /* "from 0 to 100 %", or empty */
	/* what to ask before writing `text`: past the map's limits, a danger register; empty: write it at once (a text
	 * that is no value is said by write's answer) */
	Q_INVOKABLE QString writeQuestion(int row, const QString &text) const;
	Q_INVOKABLE void write(int row, const QString &text);
	/* Plot: false when refused (a bytes register; the lines' limit, said by notice) */
	Q_INVOKABLE bool setPlot(int row, bool on);
	/* a stream's channel on the chart or off; false when refused (the chart's limit of lines, said by notice) */
	Q_INVOKABLE bool setFastPlot(int stream, int channel, bool on);
	/* the legend's Unplot: the line's register (or fast channel) off the chart */
	Q_INVOKABLE void unplotLine(int id);

	/* the window whose frames run sync(): set by phone::loadMain */
	void attachWindow(QQuickWindow *window);
	/* the values from the engine's table into the RegisterModel, at their pace: once a frame, and by a timer when no
	 * frame is drawn */
	void sync();

	/* tests: the USB bridge connectUsb and the port list use instead of Android's (null: Android's, or none) */
	static void setUsbBridgeForTests(evre::UsbBridge *bridge);
	/* tests: the system's stand-in (the service, the screen, the orientation) instead of Android's (null: Android's) */
	static void setSystemBridgeForTests(SystemBridge *bridge);

	/* the screen kept on while connected (A-5): saved, on by default */
	bool keepScreenOn() const { return keepScreenOn_; }
	void setKeepScreenOn(bool on);
	/* the service's notification text while a link is wanted; empty: the service is not up */
	QString serviceText() const { return serviceText_; }
	bool screenKeptOn() const { return screenKeptOn_; }
	/* "portrait", "landscape" or "" (either), as Main.qml asks for its screen (A-16) */
	QString orientation() const { return orientation_; }
	void setOrientation(const QString &orientation);
	/* where recordings go: the app's own folder (A-8), "recordings" in Android's external files folder for the app
	 * (Android/data/<package>/files, reached by a file manager and by a PC over USB), else in the app's data folder */
	static QString recordingsFolder();
	/* a folder of its own for the recordings in place of the app's (phone::useOwnFolder: a test run's, so two runs at
	 * once never write into each other's); empty: the app's */
	static void setDataFolder(const QString &folder);

signals:
	void linkChanged();
	void deviceChanged();
	void mapChanged();
	void settingsChanged();
	void systemChanged();
	void statsChanged();
	void recordingChanged();
	/* each line of the Log as it is added (LogModel::Level) */
	void message(int level, const QString &text);
	/* a write's answer: ok, or why not (refused by the device, not a value, writes off) */
	void writeAnswered(int row, bool ok, const QString &text);
	/* a short word for the screen (the plot's limit, Copy) */
	void notice(const QString &text);
	void chartChanged();
	void fastChanged();

private:
	void startEngine();
	void findMaps();
	void pushMap();
	void pushPolling();
	void pushLinkOptions();
	void connectLink();
	void retryConnect();
	void onOpened(const QString &link);
	void onClosed(const QString &why, const QString &detail);
	void onDeviceInfo(int device, bool ok, quint16 id, quint16 status, const QString &err);
	void onRecordStarted(bool ok, const QString &err);
	void onRecordStopped(const QString &file, quint64 rows);
	void findUsbAgain();
	static QString typedNumber(const RegDef &def, const QString &text);
	void updateSystem();
	void refreshStats();
	void markDeviceLost();
	void retryDeviceId();
	void linkDown();
	void onReadDone(quint64 id, bool ok, const QByteArray &data, const QString &err);
	void onWriteDone(quint64 id, bool ok, const QString &err);
	void updatePlotLimit();
	void pushPlotted();
	void onPlotChanged(int row, bool on);
	void resetStreams();
	void syncChart();
	void updateStaleAfter();
	double sampleRateHz() const;
	uint8_t slaveOf(const RegDef &def) const;
	QString mapWarning(quint16 id) const;
	void setState(State state, const QString &text, const QString &detail = QString());
	void setDevice(int id, const QString &text, const QString &warning = QString());
	void addLog(LogModel::Level level, const QString &text);
	static evre::UsbBridge *usbBridge();

	QThread *thread_ = nullptr;
	IoEngine *engine_ = nullptr;
	RegisterModel *model_ = nullptr;
	PortList *ports_ = nullptr;
	LogModel *log_ = nullptr;
	RegisterList *list_ = nullptr;
	DeviceMap map_;
	QString mapFile_, mapError_;
	QStringList mapNames_, mapFiles_;
	quint64 mapGeneration_ = 0;

	State state_ = Off;
	QString linkText_, linkDetail_;
	bool wantConnected_ = false; /* Connect pressed and not Disconnect: tries go on while Reconnect by itself is on */
	bool connected_ = false;
	bool retrying_ = false;      /* a try by itself: not said again in the Log */
	QString loginStop_;          /* a refused token or a device that wants one: why the next close stops the tries */
	QTimer reconnectTimer_;
	QTimer statsTimer_;
	quint16 usbVid_ = 0, usbPid_ = 0; /* the USB device's IDs at Connect: found again by them */
	QTimer serviceTimer_;        /* the notification's recording time, once a second */
	QElapsedTimer recordClock_;
	bool keepScreenOn_ = true;
	bool screenKeptOn_ = false;
	QString serviceText_;
	QString orientation_;

	/* the last try's link: a try by itself makes the same one */
	bool tcp_ = true;
	QString host_;
	int port_ = 1210;
	QString token_;
	QString usbDevice_;
	int baud_ = 115200;
	bool autoReconnect_ = true;
	bool polling_ = true;
	int intervalMs_ = 100;
	double pollHz_ = 0;

	int deviceId_ = -1;          /* -1: not read on this link */
	int revision_ = 0;
	QString deviceText_, deviceWarning_;
	int idTries_ = 0;            /* DEVICE_ID tried again on this link */
	quint64 idRead_ = 0;         /* the request of that try, 0: none under way */
	QTimer idTimer_;

	/* sync(): the values copied at their pace; each row's engine version copied last */
	QQuickWindow *window_ = nullptr;
	ValuePacer valuePacer_;
	QElapsedTimer valueClock_, sinceSync_;
	QVector<quint64> copiedVersions_;
	QTimer syncTimer_;

	/* writes under way: the request, its row and what it wrote */
	struct PendingWrite {
		int row = -1;
		QString name, what;
	};
	QHash<quint64, PendingWrite> writes_;
	quint64 nextRequest_ = 1;

	bool recording_ = false;
	QString recordFile_, recordText_;

	/* the chart: its lines take the theme's series colours in turn (ChartTab's nextColor_) */
	QPointer<ChartItem> chart_;
	int nextColour_ = 0;
	QVector<std::shared_ptr<fast::Store>> stores_; /* by stream of the map */
};

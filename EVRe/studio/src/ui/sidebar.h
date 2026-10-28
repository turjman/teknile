/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar at the left of the window: a column of cards, scrolled when the
 * window is not tall enough.
 *
 *  - Connection: TCP (host, port, a token for the map's login register) or
 *    a serial port (the ports found, the baud rate, the Link: USB CDC or a
 *    UART, told apart by the port's USB IDs or chosen); the slave address, the
 *    answer timeout, the requests in flight; Reconnect by itself; Connect,
 *    with the link's state (the pill) and the device's ID under it.
 *  - Devices on the link: one device (the map's) or a bus of several (BusPanel,
 *    ui/bus_panel.h).
 *  - Device map: the map's device, its file and size; Open, New, Save, Save as.
 *    On a bus: the selected device's map.
 *  - Polling & recording: Poll and its interval, the poll rate reached (and,
 *    when it is slower than asked, why); Auto send and its rate (the device
 *    sends its read-only block by itself; one device only; the rates its map
 *    says it makes), and under it a note when what comes or what is asked is
 *    not what was chosen; Show values (how
 *    often the numbers on screen change, value_pace.h); Record CSV, and Open
 *    recording beside it (a file, or one of the last ones).
 *  - Fast streams (Fast EVRe; only for a map with "streams"): each stream's Log tick (written beside a CSV recording
 *    or not, kept by the stream's name in "fast/notLogged"), its Start / Stop button, its samples a second
 *    as the clock's fit has them with the correction, the samples lost, and a Plot tick for each of its channels with
 *    its newest value (the channels' rows are here, not in the Registers tab: they are no registers). One device only,
 *    as Auto send.
 *  - API server: Serve API, Network, Allow API writes (and the ⚠ registers).
 *  - Help and the theme, then the version.
 *
 * The sidebar holds the choices and shows the states; the window does the
 * work. It reads the choices when it needs them (connecting, telling the
 * engine), is told of every change it must follow (the signals), and says
 * what to show (the show... functions). The link, the polling and the API
 * switches are kept in the settings ("link/...", "link/serialKind" on every change, "poll/interval", "api/on",
 * "api/network"), Show values and the auto send rate too ("ui/valueRate",
 * "poll/autoSendHz", on every change); the token, the API write switches,
 * Auto send itself and the fast streams (they change the device) never are. */
#pragma once

#include <QElapsedTimer>
#include <QScrollArea>
#include <QString>

#include "io/engine.h"
#include "model/device_map.h"

class BusPanel;
class ElidedLabel;
class LimitSpinBox;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;

class Sidebar : public QScrollArea {
	Q_OBJECT
public:
	explicit Sidebar(QWidget *parent = nullptr);

	/* settings */
	void restoreLinkSettings(); /* the link and the polling as saved: before a map is loaded */
	void restoreApiSettings();  /* Serve API and Network as saved: once the map is loaded (they may start the server) */
	void saveSettings() const;

	/* the command line's choices */
	void useTcp(const QString &hostPort);      /* host, or host:port */
	void useSerial(const QString &portBaud);   /* port, or port:baud; a port not in the list is added to it */
	void setToken(const QString &token);
	void setPollInterval(double ms);
	void setInFlight(int requests);
	void tickApiSwitches(bool serve, bool writes, bool danger); /* each one true is ticked, the others stay */

	/* the connection card */
	bool isTcp() const;
	QString host() const;        /* without spaces around it */
	quint16 port() const;
	QString token() const;       /* empty: none */
	QString serialPort() const;  /* empty: none found */
	qint32 baud() const;         /* 0: not a number (an emptied box) */
	/* the rates the Baud box takes: 20000000 is the Map settings dialog's ceiling too */
	static constexpr qint32 BAUD_MIN = 300, BAUD_MAX = 20000000;
	void focusBaud();            /* a rate out of range at Connect: the box to type it in */
	/* a USB CDC port (a board's own USB, a virtual COM port) runs at USB's speed whatever the baud rate says; a UART,
	 * or a USB-to-UART bridge, really runs at it */
	enum class SerialKind { Uart, UsbCdc };
	/* told by the port's USB IDs (the description is anything: Windows names a CDC port after the device's own product
	 * string): a bridge's vendor, or no USB IDs, is a UART; Linux's ttyUSB too, ttyACM is CDC; any other USB ID is CDC.
	 * Unsure means a UART: the slower side, and the Link row can say otherwise */
	static SerialKind serialKindOf(bool hasVid, quint16 vid, quint16 pid, const QString &portName);
	/* the baud rate is the link's real speed (Auto send is kept under it): the Link row's choice, on Auto the
	 * selected port's kind */
	bool serialBaudLimited() const;
	int slave() const;
	void setSlave(int slave);
	/* a bus: each device has its own slave address (the Devices card), the box only shows the selected one's */
	void setSlaveEditable(bool editable);
	int timeoutMs() const;
	int inFlight() const;
	bool autoReconnect() const;
	/* the serial ports found now; one with the map's USB IDs is named after its device, and chosen when
	 * none was */
	void refreshPorts(const DeviceMap &map);
	/* one port of the list: "COM10 · <description> · 0483:5740" (the map's device name in place of the description
	 * when mappedName is given), cut with "…" to the box, whole in its tooltip and in the list opened */
	void addPort(const QString &name, const QString &description, bool hasVid, quint16 vid, quint16 pid,
			const QString &mappedName);

	/* the link's state: the Connect button and the pill under it. The pill is empty until the
	 * window, once the sidebar is in it, calls showDisconnected() */
	void showDisconnected();
	void showConnecting();
	/* note: a line the pill's tooltip adds under its text (the baud rate a USB CDC port ignores) */
	void showConnected(const QString &link, const QString &note = QString());
	/* a serial link as the pill names it, one left-to-right piece: "COM13 @ 115200" on a UART (baudLimited), where the
	 * number is the link's speed, "COM13 · USB CDC" on USB CDC, where it is none */
	static QString serialLinkText(const QString &port, qint32 baud, bool baudLimited);
	/* the pill in red: why, always whole, on two lines (or more) when it does not fit on one; detail: the system's own
	 * text, on the tooltip's second line */
	void showLinkError(const QString &why, const QString &detail = QString());
	void showReconnecting();                /* the button stops the next try (the pill stays) */
	/* under the pill: the device's ID, or why it is not known; always there, two lines tall (the window says "not read
	 * yet" before there is one), so the card never changes height */
	void setDeviceInfo(const QString &html);
	/* the device line's pieces kept whole ("D2:", "last seen:", "Device ID 0x1001", "protocol rev 1"): every space a
	 * no-break one but those after ":" and "·", so a narrow sidebar wraps it only between pieces (plain text in) */
	static QString wholePieces(const QString &line);

	/* the map card: its device and file; registers: how many the table holds now */
	void showMap(const DeviceMap &map, int registers, bool modified);
	/* the Devices on the link card: its choices are its own signals */
	BusPanel *busPanel() const { return busPanel_; }

	/* polling, recording, the API */
	bool pollingOn() const;
	double pollIntervalMs() const;       /* 0: as fast as possible */
	int valuesPerSecond() const;         /* Show values: ValuePace::EVERY_FRAME (0) or per second */
	/* Auto send: ticked, its rate (one of the list: the rates the map's device makes, evre::autoSendRatesFor) */
	bool autoSendOn() const;
	int autoSendHz() const;
	/* the rate asked (saved, not written to the settings): the list shows it, or the nearest it has at or below */
	void setAutoSendHz(int hz);
	/* the list from the map's "auto_send" limits (MapProtocol() or a map without them: all 16 rates), the saved rate
	 * shown as setAutoSendHz does, a note under the row while it is another; true when the list changed. Without
	 * autoSendChanged: the window asks the engine again when Auto send is on */
	bool setAutoSendLimits(const MapProtocol &protocol);
	/* the rate the device was set to (after a UART's cap; 0: off or failed): what arrives is measured against it, and
	 * on a UART (uartBaud, 0 otherwise) a lower one than chosen is said under the row, why its tooltip (the Log's) */
	void setAutoSendTaken(int hz, qint32 uartBaud = 0, const QString &why = QString());
	/* the measuring: under AUTO_SEND_SHARE of the rate for AUTO_SEND_SHORT_MS is said; nothing counts in the first
	 * AUTO_SEND_SETTLE_MS after setAutoSendTaken */
	static constexpr double AUTO_SEND_SHARE = 0.9;
	static constexpr qint64 AUTO_SEND_SHORT_MS = 2000, AUTO_SEND_SETTLE_MS = 3000;
	void shiftAutoSendClock(qint64 ms) { clockShiftMs_ += ms; } /* tests: the measuring's clock ms later */
	void setAutoSendOn(bool on);         /* ticked or not, without autoSendChanged: the device's state, not a choice */
	/* offered: the box can be ticked; why: when not, the tooltip says why (a bus, no CAP_AUTO_SEND) */
	/* offered or not; why: the box's tooltip when not; shortWhy: a word or two the greyed rate list shows instead of a
	 * rate ("not offered") */
	void setAutoSendOffered(bool offered, const QString &why, const QString &shortWhy = QString());
	/* Fast EVRe: a row for each of the map's streams (the card hidden when it has none) */
	void setFastStreams(const QVector<StreamDef> &streams);
	int fastStreamCount() const { return int(fastRows_.size()); }
	bool fastOn(int stream) const;
	void setFastOn(int stream, bool on);  /* the button's state, without fastStreamToggled: the device's, not a choice */
	/* offered or not; why: the buttons' tooltip when not; shortWhy: a word or two shown in place of the rate */
	void setFastOffered(bool offered, const QString &why, const QString &shortWhy);
	/* for the tests: the card, a stream's button and its two numbers */
	QWidget *fastCard() const { return fastCard_; }
	QPushButton *fastButton(int stream) const;
	QString fastRateText(int stream) const;
	QString fastLostText(int stream) const;
	QString fastRateTip(int stream) const; /* tests: the rate's tooltip (the counts) */
	/* a channel's Plot tick (the chart's line STREAM.CHANNEL), set without fastPlotToggled; all off */
	void setFastPlot(int stream, int channel, bool on);
	bool fastPlot(int stream, int channel) const;
	void clearFastPlots();
	void setFastPlotsEnabled(bool enabled, const QString &why = QString()); /* a bus: no fast line (why: the tooltip) */
	QCheckBox *fastPlotBox(int stream, int channel) const; /* tests */
	/* a stream's rate as the map gives it, one left-to-right piece: "1 MS/s", "10 kS/s", "500 S/s" (units as they are) */
	static QString streamRateText(double hz, int digits = 4);
	/* a stream's Log tick: recorded beside the CSV or not (on by default); the streams with it off, by name */
	QCheckBox *fastLogBox(int stream) const;
	QStringList fastNotLogged() const;
	/* the channels' newest values, at the values' pace (NaN: none yet) */
	void showFastValues(int stream, const QVector<double> &values);
	QString fastValueText(int stream, int channel) const; /* tests */
	void setRecording(bool recording);   /* the record button: start or stop */
	void showRecordSaved(const QString &file, quint64 rows);
	bool apiNetwork() const;
	bool apiWrites() const;
	bool apiDanger() const;
	void showApiNotStarted(const QString &error); /* Serve API unticked again, and why */
	/* twice a second: the poll rate, the recording, the API's clients */
	void showStats(const IoEngine::Stats &stats, bool connected);
	/* why the polls are slower than asked, as the last showStats found it; empty: they are not. The window shows it in
	 * the status bar: there it comes and goes without moving anything */
	QString slowPollHint() const { return slowHint_; }
	/* the hint for these numbers (slowPollReason), whatever the link is now: for the tests */
	QString slowPollHintFor(const IoEngine::Stats &stats) const { return slowPollReason(stats); }

	void themeChanged();

signals:
	void connectClicked();
	void refreshPortsClicked();
	void slaveChanged(int slave);  /* the Slave box (one device: the user's choice; a bus sets it, disabled) */
	void inFlightChanged();
	void pollingChanged();         /* Poll ticked or not, or the interval changed */
	void timingChanged();          /* the interval or the timeout: how old a value may get before it is greyed */
	void valuePaceChanged();       /* Show values: how often the numbers on screen change */
	void autoSendChanged();        /* Auto send ticked or not, or its rate changed */
	void fastStreamToggled(int stream, bool on); /* a fast stream's Start or Stop pressed */
	void fastPlotToggled(int stream, int channel, bool on); /* a channel's Plot tick */
	void openMapClicked();
	void newMapClicked();
	void saveMapClicked(bool saveAs);
	void recordClicked();
	void openRecordingClicked(const QString &file); /* empty: choose one */
	void statusMessage(const QString &text, int ms); /* for the status bar (a recent recording taken off its list) */
	void apiServeChanged(bool on); /* Serve API; Network switched while serving: on again, with it */
	void apiWritesChanged();       /* Allow API writes, or including ⚠ registers */
	void helpClicked();
	void themeClicked();
	void restartRequested(); /* Restart now: the language chosen applies at a start */

protected:
	/* the port box resized or its font changed: its labels cut again, its list and the Link row's widened again */
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	/* building the cards, top to bottom */
	QWidget *buildConnectionCard();
	QWidget *buildTcpFields();
	QWidget *buildSerialFields();
	QVBoxLayout *buildRequestOptions(); /* slave, timeout, In flight */
	QVBoxLayout *buildLinkState();      /* Connect, the pill, the device's ID */
	void fitDeviceInfo();               /* the device line at least as tall as its lines need */
	QWidget *buildBusCard();
	QWidget *buildMapCard();
	QWidget *buildPollingCard();
	QWidget *buildFastCard();
	void showFastStats(const IoEngine::Stats &stats, bool connected);
	void showFastButton(int stream);    /* its text, look and tooltip for its state */
	QWidget *buildApiCard();
	void addFooter(QVBoxLayout *layout);

	void setConnectButton(const QString &text, const QString &look); /* look: the theme's "primary" or "danger" */
	/* the pill: "idle", "busy", "ok", "error"; shorter: shown whole in place of a text too long (ElidedLabel);
	 * toolTip: empty for the whole text */
	void showLinkState(const QString &state, const QString &text, const QString &shorter = QString(),
			const QString &toolTip = QString());
	void elidePortLabels();              /* every port's label cut to the box's room (its whole text kept) */
	void showSerialKind();               /* the Link row's Auto item and the port box's tooltip, for the port selected */
	void fitKindBox();                   /* the Link box as wide as its widest short text */
	void showBaudIgnored();              /* the Baud row faint, "the baud rate is not used" under, while Link means USB CDC */
	int savedInFlight(bool tcp) const;   /* In flight is kept per link type: TCP pipelines, a UART does not */
	void showPollRate(const IoEngine::Stats &stats, bool connected);
	void showSavedRate();                /* the list at the saved rate, or the nearest at or below; the note if moved */
	void pickRate(int index);            /* a rate chosen: saved */
	void watchAutoSend(bool running, double hz); /* a reading of the frames a second against the rate taken */
	void showAutoSendNote();             /* the note under the Auto send row: the first of its three that applies */
	qint64 autoSendNowMs() const;
	QString slowPollReason(const IoEngine::Stats &stats) const; /* empty: not slower than asked */
public:
	/* the In flight that lets enough polls overlap for wantHz (0: as fast as possible) at this latency, never
	 * past the engine's polls at once (IoEngine::MAX_POLLS_UNDER_WAY x blocks) nor maxInFlight; public for tests */
	static int suggestedInFlight(int blocks, int inFlight, double wantHz, double latencyMs, int maxInFlight);
private:
	void showApiState(const IoEngine::Stats &stats);

	/* the connection card */
	QPushButton *tcpButton_, *serialButton_;
	QStackedWidget *linkStack_;  /* the TCP fields or the serial ones */
	QLineEdit *host_, *token_;
	QSpinBox *port_;
	QComboBox *serialPort_, *baud_;
	QComboBox *serialKind_;      /* the Link row: Auto, USB CDC, UART */
	QLabel *baudLabel_;
	ElidedLabel *baudIgnored_;   /* "USB CDC: the baud rate is not used", close under the Baud box; hidden on a UART */
	QGridLayout *serialRows_ = nullptr; /* Baud and Link */
	QString baudHelp_;           /* the Baud box's tooltip on a UART */
	LimitSpinBox *slave_, *timeout_, *inFlight_;
	QCheckBox *autoReconnect_;
	QPushButton *connectButton_;
	QPushButton *refreshButton_;  /* the serial ports looked for again */
	/* the pill: one line, a long text cut in the middle, the whole of it in the tooltip; an error wraps, never cut */
	ElidedLabel *linkState_;
	QLabel *deviceInfo_;

	/* the devices card, the map card */
	BusPanel *busPanel_;
	QLabel *mapInfo_;

	/* the polling card */
	QCheckBox *poll_;
	QDoubleSpinBox *interval_;
	QLabel *pollInfo_;
	QString slowHint_;            /* why the polls are slower than asked (the status bar shows it); empty: they are not */
	QCheckBox *autoSend_;
	QComboBox *autoSendRate_;     /* the rates the device makes exactly, in Hz */
	int rateIndex_ = 0;           /* the rate chosen: kept while the list shows why it is not offered */
	int savedHz_ = 100;           /* the rate saved (poll/autoSendHz): the list may show a lower one, the map's */
	bool autoSendLimited_ = false; /* the list is the map's limits', not all 16 */
	QString rateHelp_;            /* the list's tooltip while offered (not offered: the reason) */
	/* the note under the Auto send row and what it is made of */
	ElidedLabel *autoSendNote_;
	QString movedNote_;           /* the list shows another rate than the saved one: the map's */
	QString capNote_, capWhy_;    /* the UART set a lower rate than chosen, and the Log's sentence */
	QString shortfall_, shortfallWhy_; /* fewer frames come than were asked for */
	int takenHz_ = 0;             /* the rate the device was set to; 0: off */
	QElapsedTimer autoSendClock_;
	qint64 clockShiftMs_ = 0;     /* tests */
	qint64 takenAtMs_ = 0, underSinceMs_ = -1;
	QComboBox *valuePace_;        /* Show values */
	QPushButton *recordButton_;
	QPushButton *openRecording_;  /* its menu: Open a file…, the last recordings */
	QLabel *recordInfo_;

	/* the fast streams card */
	struct FastRow {
		StreamDef def;
		QLabel *title = nullptr;  /* the stream's name, heading its row */
		ElidedLabel *about = nullptr; /* beside it, muted: "· 2 channels · 1 MS/s" */
		QString channelsTip;      /* the header's tooltip after its first line: the channels and the description */
		bool aboutMeasured = false; /* the header says the measured rate (else the rate set) */
		qint64 aboutMs = -1;      /* when it last changed the measured rate: at most once a second */
		QCheckBox *log = nullptr; /* recorded beside the CSV or not */
		QPushButton *button = nullptr;
		QLabel *rate = nullptr;   /* the samples a second and the correction; not offered: why, in a word or two */
		QLabel *lost = nullptr;   /* the samples lost */
		QVector<QCheckBox *> plots; /* a channel's Plot tick */
		QVector<QLabel *> values;   /* its newest value */
		bool on = false;
	};
	QWidget *fastCard_;
	QVBoxLayout *fastRowsLayout_;
	QVector<FastRow> fastRows_;
	/* a stream's header: "· 2 channels · 1 MS/s" with the measured rate (3 digits), or "· 2 channels · 10 kS/s set"
	 * with the rate set (the device's rate register as last read, else the map's) */
	void showFastAbout(int stream, double hz, bool measured, bool fromDevice = false);
	bool fastOffered_ = false;
	QString fastWhy_, fastShortWhy_;

	/* the API card */
	QCheckBox *apiServe_, *apiNetwork_, *apiWrites_, *apiDanger_;
	QLabel *apiInfo_;

	QPushButton *themeButton_;
	QComboBox *language_;         /* System, English, العربية: applied at the next start (language.h) */
	QPushButton *restart_;        /* shown while the language chosen is not the one running */
};

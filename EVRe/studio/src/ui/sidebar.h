/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar at the left of the window: a column of cards, scrolled when the
 * window is not tall enough.
 *
 *  - Connection: TCP (host, port, a token for the map's login register) or
 *    a serial port (the ports found, the baud rate); the slave address, the
 *    answer timeout, the requests in flight; Reconnect by itself; Connect,
 *    with the link's state (the pill) and the device's ID under it.
 *  - Devices on the link: one device (the map's) or a bus of several (BusPanel,
 *    ui/bus_panel.h).
 *  - Device map: the map's device, its file and size; Open, New, Save, Save as.
 *    On a bus: the selected device's map.
 *  - Polling & recording: Poll and its interval, the poll rate reached (and,
 *    when it is slower than asked, why); Auto send and its rate (the device
 *    sends its read-only block by itself; one device only); Show values (how
 *    often the numbers on screen change, value_pace.h); Record CSV, and Open
 *    recording beside it (a file, or one of the last ones).
 *  - API server: Serve API, Network, Allow API writes (and the ⚠ registers).
 *  - Help and the theme, then the version.
 *
 * The sidebar holds the choices and shows the states; the window does the
 * work. It reads the choices when it needs them (connecting, telling the
 * engine), is told of every change it must follow (the signals), and says
 * what to show (the show... functions). The link, the polling and the API
 * switches are kept in the settings ("link/...", "poll/interval", "api/on",
 * "api/network"), Show values and the auto send rate too ("ui/valueRate",
 * "poll/autoSendHz", on every change); the token, the API write switches and
 * Auto send itself (it changes the device) never are. */
#pragma once

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
	qint32 baud() const;
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

	/* the link's state: the Connect button and the pill under it. The pill is empty until the
	 * window, once the sidebar is in it, calls showDisconnected() */
	void showDisconnected();
	void showConnecting();
	void showConnected(const QString &link);
	void showLinkError(const QString &why); /* the pill in red: the first 60 characters of why */
	void showReconnecting();                /* the button stops the next try (the pill stays) */
	/* under the pill: the device's ID, or why it is not known; always there, two lines tall (the window says "not read
	 * yet" before there is one), so the card never changes height */
	void setDeviceInfo(const QString &html);

	/* the map card: its device and file; registers: how many the table holds now */
	void showMap(const DeviceMap &map, int registers, bool modified);
	/* the Devices on the link card: its choices are its own signals */
	BusPanel *busPanel() const { return busPanel_; }

	/* polling, recording, the API */
	bool pollingOn() const;
	double pollIntervalMs() const;       /* 0: as fast as possible */
	int valuesPerSecond() const;         /* Show values: ValuePace::EVERY_FRAME (0) or per second */
	/* Auto send: ticked, its rate (one of 8000 / IoEngine::autoSendDividers() Hz) */
	bool autoSendOn() const;
	int autoSendHz() const;
	void setAutoSendHz(int hz);          /* the nearest rate of the list */
	void setAutoSendOn(bool on);         /* ticked or not, without autoSendChanged: the device's state, not a choice */
	/* offered: the box can be ticked; why: when not, the tooltip says why (a bus, no CAP_AUTO_SEND) */
	/* offered or not; why: the box's tooltip when not; shortWhy: a word or two the greyed rate list shows instead of a
	 * rate ("not offered") */
	void setAutoSendOffered(bool offered, const QString &why, const QString &shortWhy = QString());
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
	void openMapClicked();
	void newMapClicked();
	void saveMapClicked(bool saveAs);
	void recordClicked();
	void openRecordingClicked(const QString &file); /* empty: choose one */
	void apiServeChanged(bool on); /* Serve API; Network switched while serving: on again, with it */
	void apiWritesChanged();       /* Allow API writes, or including ⚠ registers */
	void helpClicked();
	void themeClicked();
	void restartRequested(); /* Restart now: the language chosen applies at a start */

private:
	/* building the cards, top to bottom */
	QWidget *buildConnectionCard();
	QWidget *buildTcpFields();
	QWidget *buildSerialFields();
	QVBoxLayout *buildRequestOptions(); /* slave, timeout, In flight */
	QVBoxLayout *buildLinkState();      /* Connect, the pill, the device's ID */
	QWidget *buildBusCard();
	QWidget *buildMapCard();
	QWidget *buildPollingCard();
	QWidget *buildApiCard();
	void addFooter(QVBoxLayout *layout);

	void setConnectButton(const QString &text, const QString &look); /* look: the theme's "primary" or "danger" */
	/* the pill: "idle", "busy", "ok", "error"; shorter: shown whole in place of a text too long (ElidedLabel) */
	void showLinkState(const QString &state, const QString &text, const QString &shorter = QString());
	int savedInFlight(bool tcp) const;   /* In flight is kept per link type: TCP pipelines, a UART does not */
	void showPollRate(const IoEngine::Stats &stats, bool connected);
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
	LimitSpinBox *slave_, *timeout_, *inFlight_;
	QCheckBox *autoReconnect_;
	QPushButton *connectButton_;
	QPushButton *refreshButton_;  /* the serial ports looked for again */
	ElidedLabel *linkState_;     /* the pill: one line, a long text cut in the middle, the whole of it in the tooltip */
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
	QString rateHelp_;            /* the list's tooltip while offered (not offered: the reason) */
	QComboBox *valuePace_;        /* Show values */
	QPushButton *recordButton_;
	QPushButton *openRecording_;  /* its menu: Open a file…, the last recordings */
	QLabel *recordInfo_;

	/* the API card */
	QCheckBox *apiServe_, *apiNetwork_, *apiWrites_, *apiDanger_;
	QLabel *apiInfo_;

	QPushButton *themeButton_;
	QComboBox *language_;         /* System, English, العربية: applied at the next start (language.h) */
	QPushButton *restart_;        /* shown while the language chosen is not the one running */
};

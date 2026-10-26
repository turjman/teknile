/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar: see sidebar.h. */
#include "ui/sidebar.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSerialPortInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyledItemDelegate>
#include <QValidator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ui/bus_panel.h"
#include "ui/elided_label.h"
#include "ui/language.h"
#include "ui/limit_spin_box.h"
#include "ui/recording_window.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"
#include "ui/value_pace.h"

static const QString NOT_LOGGED_KEY = QStringLiteral("fast/notLogged"); /* the streams whose Log is off, by name */
static const QString SERIAL_KIND_KEY = QStringLiteral("link/serialKind"); /* auto, cdc or uart */

namespace {

/* a port's item: its whole label (the box shows it cut to its room), and its kind (Sidebar::SerialKind) */
constexpr int FULL_LABEL_ROLE = Qt::UserRole + 1;
constexpr int SERIAL_KIND_ROLE = Qt::UserRole + 2;

/* the list opened: each item's whole label, not the cut one the box shows. A port's label is left to right in Arabic
 * too (a port name, a product string and hex are one piece); the Link row's are translated sentences, which run right
 * to left as every other text does */
class FullLabels : public QStyledItemDelegate {
public:
	FullLabels(bool ltr, QObject *parent) : QStyledItemDelegate(parent), ltr_(ltr) {}

protected:
	void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
		QStyledItemDelegate::initStyleOption(option, index);
		const QString full = index.data(FULL_LABEL_ROLE).toString();
		if (!full.isEmpty()) option->text = ltr_ ? ltrPiece(full) : full;
		/* the item under the mouse in the text colour, as the tables have it: the style sheet's selection is the accent
		 * faded, and the palette's white on it could not be read in the light look */
		option->palette.setColor(QPalette::HighlightedText, option->palette.color(QPalette::Text));
	}

private:
	bool ltr_;
};

/* Only what can become a rate is typed: digits, and never a number past the top. QIntValidator alone lets
 * "20000001" in (a number of the top's length, on its way to one in range, it says) and takes the locale's group
 * separators, which baud() would not read. Under the bottom stays typeable: "3" on the way to "300000" */
class BaudValidator : public QIntValidator {
public:
	explicit BaudValidator(QObject *parent) : QIntValidator(Sidebar::BAUD_MIN, Sidebar::BAUD_MAX, parent) {}
	State validate(QString &input, int &pos) const override {
		(void) pos;
		if (input.isEmpty()) return Intermediate;
		for (const QChar c : input)
			if (c < QLatin1Char('0') || c > QLatin1Char('9')) return Invalid;
		if (input.size() > 8) return Invalid; /* past 20000000 whatever the digits */
		const qint64 value = input.toLongLong();
		if (value > top()) return Invalid;
		return value < bottom() ? Intermediate : Acceptable;
	}
};

/* the list opened as wide as its longest label (whole, as the delegate draws it), with the box's padding and arrow:
 * wider than the box when a label is */
void widenList(QComboBox *box) {
	int widest = 0;
	for (int i = 0; i < box->count(); i++) {
		const QString full = box->itemData(i, FULL_LABEL_ROLE).toString();
		widest = std::max(widest, box->fontMetrics().horizontalAdvance(full.isEmpty() ? box->itemText(i) : full));
	}
	box->view()->setMinimumWidth(widest + 2 * 8 + 20);
}

} // namespace

Sidebar::Sidebar(QWidget *parent) : QScrollArea(parent) {
	setObjectName(QStringLiteral("sideScroll"));
	auto *content = new QFrame;
	content->setObjectName(QStringLiteral("sidebar"));
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(16, 18, 16, 14);
	layout->setSpacing(12);
	auto *title = new QLabel(QStringLiteral("EVRe Studio"));
	title->setObjectName(QStringLiteral("appTitle"));
	auto *subtitle = new QLabel(tr("registers · live charts · CSV"));
	subtitle->setObjectName(QStringLiteral("appSub"));
	layout->addWidget(title);
	layout->addWidget(subtitle);
	layout->addWidget(buildConnectionCard());
	layout->addWidget(buildBusCard());
	layout->addWidget(buildMapCard());
	layout->addWidget(buildFastCard()); /* the map's streams, right under the map; before the fold of a laptop screen */
	layout->addWidget(buildPollingCard());
	layout->addWidget(buildApiCard());
	layout->addStretch();
	addFooter(layout);

	setWidget(content);
	setWidgetResizable(true);
	setFrameShape(QFrame::NoFrame);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setFixedWidth(312);
	/* the cards as wide with the scroll bar as without it: one that comes or goes (a window made shorter, a card
	 * grown) must not rewrap their text, which would make it come and go again */
	content->setFixedWidth(312 - verticalScrollBar()->sizeHint().width());
}

/* ----------------------------------------------------------------- building */

QWidget *Sidebar::buildConnectionCard() {
	/* TCP or serial: a segmented choice over the fields of the one chosen */
	tcpButton_ = segmentButton(tr("TCP"), "first");
	serialButton_ = segmentButton(tr("Serial / USB"), "last");
	auto *linkChoice = new QButtonGroup(this);
	linkChoice->addButton(tcpButton_);
	linkChoice->addButton(serialButton_);
	auto *choiceRow = new QHBoxLayout;
	choiceRow->setSpacing(0);
	choiceRow->addWidget(tcpButton_, 1);
	choiceRow->addWidget(serialButton_, 1);
	QWidget *tcpFields = buildTcpFields();
	QWidget *serialFields = buildSerialFields();
	linkStack_ = new QStackedWidget;
	linkStack_->addWidget(tcpFields);
	linkStack_->addWidget(serialFields);
	connect(tcpButton_, &QPushButton::toggled, this, [this](bool tcp) {
		linkStack_->setCurrentIndex(tcp ? 0 : 1);
		inFlight_->setValue(savedInFlight(tcp));
	});
	QVBoxLayout *requestOptions = buildRequestOptions();
	autoReconnect_ = new QCheckBox(tr("Reconnect by itself"));
	QVBoxLayout *linkState = buildLinkState();

	auto *content = new QVBoxLayout;
	content->addLayout(choiceRow);
	content->addWidget(linkStack_);
	content->addLayout(requestOptions);
	content->addWidget(autoReconnect_);
	content->addLayout(linkState);
	return card(tr("Connection"), content);
}

QWidget *Sidebar::buildTcpFields() {
	host_ = new QLineEdit;
	host_->setPlaceholderText(tr("host"));
	port_ = new QSpinBox;
	port_->setRange(1, 65535);
	port_->setFixedWidth(80);
	token_ = new QLineEdit;
	token_->setPlaceholderText(tr("Token (not stored)"));
	token_->setToolTip(tr("Sent to the map's login register after connecting; never saved"));
	token_->setEchoMode(QLineEdit::Password);
	auto *fields = new QWidget;
	auto *layout = new QVBoxLayout(fields);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *hostRow = new QHBoxLayout;
	hostRow->addWidget(host_, 1);
	hostRow->addWidget(port_);
	layout->addLayout(hostRow);
	layout->addWidget(token_);
	return fields;
}

QWidget *Sidebar::buildSerialFields() {
	serialPort_ = new QComboBox;
	serialPort_->setObjectName(QStringLiteral("serialPort"));
	/* a long label (a board names its port after its product) never widens the sidebar: it is cut to the box with
	 * "…", whole in the tooltip and in the list opened, which opens as wide as its longest label */
	serialPort_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	serialPort_->setMinimumContentsLength(12);
	serialPort_->setItemDelegate(new FullLabels(true, serialPort_));
	serialPort_->installEventFilter(this);
	refreshButton_ = new QPushButton;
	refreshButton_->setObjectName(QStringLiteral("refreshPorts"));
	refreshButton_->setIcon(refreshIcon(Theme::colors().text));
	refreshButton_->setIconSize(QSize(18, 18));
	refreshButton_->setFixedWidth(36);
	refreshButton_->setToolTip(tr("Look for ports again"));
	connect(refreshButton_, &QPushButton::clicked, this, &Sidebar::refreshPortsClicked);
	baud_ = new QComboBox;
	baud_->setObjectName(QStringLiteral("baud"));
	baud_->setEditable(true);
	/* the list is a hint of what can be typed, not a limit: 12 M is an FTDI bridge's top */
	baud_->addItems({ QStringLiteral("9600"), QStringLiteral("57600"), QStringLiteral("115200"),
		QStringLiteral("230400"), QStringLiteral("460800"), QStringLiteral("921600"), QStringLiteral("2000000"),
		QStringLiteral("3000000"), QStringLiteral("4000000"), QStringLiteral("6000000"), QStringLiteral("12000000") });
	baud_->setValidator(new BaudValidator(baud_));
	baud_->lineEdit()->setPlaceholderText(tr("type a rate"));
	/* the number is written left to right: its placeholder and the text cursor sit with it at the left in Arabic too
	 * (AlignAbsolute, else right to left turns the left into the right) */
	baud_->lineEdit()->setAlignment(Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignVCenter);
	/* the opened list's rates at the left as the box's number, also a rate typed and added with Enter: in Arabic a list
	 * at the right under a number at the left read as two columns */
	const auto alignRates = [this](const QModelIndex &, int first, int last) {
		for (int i = first; i <= last; i++)
			baud_->setItemData(i, int(Qt::AlignLeft | Qt::AlignAbsolute | Qt::AlignVCenter), Qt::TextAlignmentRole);
	};
	alignRates(QModelIndex(), 0, baud_->count() - 1);
	connect(baud_->model(), &QAbstractItemModel::rowsInserted, this, alignRates);
	baud_->setToolTip(tr("Pick a rate or type any rate, %1 to %2. Ignored on USB CDC (see Link).")
			.arg(BAUD_MIN).arg(BAUD_MAX));
	/* USB CDC or a UART: Auto from the port's USB IDs, or chosen. The box shows a short text; the list opened says
	 * what each choice does */
	serialKind_ = new QComboBox;
	serialKind_->setObjectName(QStringLiteral("serialKind"));
	serialKind_->setItemDelegate(new FullLabels(false, serialKind_));
	serialKind_->addItem(tr("Auto: UART"), QStringLiteral("auto"));
	serialKind_->addItem(tr("USB CDC"), QStringLiteral("cdc"));
	serialKind_->setItemData(1, tr("USB CDC: full speed, baud ignored"), FULL_LABEL_ROLE);
	serialKind_->addItem(tr("UART"), QStringLiteral("uart"));
	serialKind_->setItemData(2, tr("UART: Auto send kept under the baud rate"), FULL_LABEL_ROLE);
	const QString kindHelp = tr("A USB CDC port runs at USB's speed whatever the baud rate says; a UART, or a "
			"USB-to-UART bridge (FTDI, CP210x, CH340, PL2303), really runs at the baud rate, so Auto send is kept to "
			"70% of it. Auto tells them apart by the port's USB IDs.");
	serialKind_->setToolTip(kindHelp);
	connect(serialKind_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index >= 0) QSettings().setValue(SERIAL_KIND_KEY, serialKind_->itemData(index));
	});
	connect(serialPort_, &QComboBox::currentIndexChanged, this, &Sidebar::showSerialKind);
	widenList(serialKind_);
	serialKind_->installEventFilter(this);
	auto *fields = new QWidget;
	auto *layout = new QVBoxLayout(fields);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *portRow = new QHBoxLayout;
	portRow->addWidget(serialPort_, 1);
	portRow->addWidget(refreshButton_);
	layout->addLayout(portRow);
	/* Baud and Link in a grid: their boxes start at the same place whatever the labels' lengths (Arabic too) */
	auto *rows = new QGridLayout;
	rows->setColumnStretch(1, 1);
	rows->addWidget(mutedLabel(tr("Baud")), 0, 0);
	rows->addWidget(baud_, 0, 1);
	QLabel *kindLabel = mutedLabel(tr("Link"));
	kindLabel->setToolTip(kindHelp);
	rows->addWidget(kindLabel, 1, 0);
	rows->addWidget(serialKind_, 1, 1);
	layout->addLayout(rows);
	return fields;
}

/* what every request needs: the slave address, the answer timeout, the requests in flight */
QVBoxLayout *Sidebar::buildRequestOptions() {
	slave_ = new LimitSpinBox;
	slave_->setLimits(1, 255); /* 0 is the broadcast address: no device answers it */
	slave_->setValue(1);
	connect(slave_, &QSpinBox::valueChanged, this, &Sidebar::slaveChanged);
	timeout_ = new LimitSpinBox;
	timeout_->setLimits(20, 10000);
	timeout_->setSuffix(QStringLiteral(" ms"));
	connect(timeout_, &QSpinBox::valueChanged, this, &Sidebar::timingChanged);
	auto *requestRow = new QHBoxLayout;
	requestRow->addWidget(mutedLabel(tr("Slave")));
	requestRow->addWidget(slave_);
	requestRow->addSpacing(6);
	requestRow->addWidget(mutedLabel(tr("Timeout")));
	requestRow->addWidget(timeout_, 1);
	/* up to 1024: several devices on a link make a poll many reads (twelve of three: 36), and polls overlap only when
	 * In flight holds several polls' reads */
	inFlight_ = new LimitSpinBox;
	inFlight_->setLimits(1, 1024);
	inFlight_->setHelp(tr("Requests sent before their answers come. 1: one at a time, for a device on a UART.\n"
			"More: pipelined, much faster over TCP to a server that takes several at once "
			"(a TCP gateway usually does).\n"
			"A poll is one read per block: polls overlap only with In flight >= 2 x blocks.\n"
			"Over Wi-Fi (several ms per answer) 1000 polls/s needs about (latency ms) x blocks."));
	auto *inFlightRow = new QHBoxLayout;
	inFlightRow->addWidget(mutedLabel(tr("In flight")));
	inFlightRow->addWidget(inFlight_);
	/* the range in view before anything is typed: a number past it is taken to the nearest end */
	inFlightRow->addWidget(mutedLabel(tr("%1 – %2").arg(inFlight_->minimum()).arg(inFlight_->maximum())));
	inFlightRow->addStretch();
	/* saved on every change, for the link type shown: loading one on a switch
	 * must not overwrite the other */
	connect(inFlight_, &QSpinBox::valueChanged, this, [this](int requests) {
		QSettings().setValue(isTcp() ? QStringLiteral("link/inflightTcp") : QStringLiteral("link/inflightSerial"),
				requests);
		emit inFlightChanged();
	});
	auto *rows = new QVBoxLayout;
	rows->addLayout(requestRow);
	rows->addLayout(inFlightRow);
	return rows;
}

/* Connect, and the link's state under it: the pill (a dot and the state on a tint, round at the ends and with no
 * edge: a state, not a second button), then the device's ID: always there ("not read yet" before it is), two lines
 * tall from the start (an ID and a warning that the map is for another), so the card keeps its height whatever it
 * says and whichever device of a bus is selected */
QVBoxLayout *Sidebar::buildLinkState() {
	connectButton_ = new QPushButton(tr("Connect"));
	connectButton_->setObjectName(QStringLiteral("primary"));
	connectButton_->setCursor(Qt::PointingHandCursor);
	connect(connectButton_, &QPushButton::clicked, this, &Sidebar::connectClicked);
	linkState_ = new ElidedLabel;
	linkState_->setObjectName(QStringLiteral("pill"));
	/* a long address is never cut: "Connected · " gives way first (the green and the dot still say it, the tooltip
	 * has the whole text). Only what still does not fit (a long host name) is cut, in the middle; an error's reason
	 * wraps instead (showLinkState) */
	linkState_->setElideMode(Qt::ElideMiddle);
	linkState_->setAlignment(Qt::AlignCenter);
	deviceInfo_ = mutedLabel(QString());
	deviceInfo_->setObjectName(QStringLiteral("deviceInfo"));
	deviceInfo_->setWordWrap(true);
	deviceInfo_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	deviceInfo_->setMinimumHeight(2 * deviceInfo_->fontMetrics().lineSpacing());
	auto *rows = new QVBoxLayout;
	rows->addWidget(connectButton_);
	rows->addWidget(linkState_);
	rows->addWidget(deviceInfo_);
	return rows;
}

QWidget *Sidebar::buildBusCard() {
	busPanel_ = new BusPanel;
	auto *content = new QVBoxLayout;
	content->addWidget(busPanel_);
	return card(tr("Devices on the link"), content);
}

QWidget *Sidebar::buildMapCard() {
	mapInfo_ = new QLabel;
	mapInfo_->setWordWrap(true);
	auto *openButton = new QPushButton(tr("Open…"));
	auto *saveButton = new QPushButton(tr("Save"));
	auto *saveAsButton = new QPushButton(tr("Save as…"));
	auto *newButton = new QPushButton(tr("New"));
	connect(openButton, &QPushButton::clicked, this, &Sidebar::openMapClicked);
	connect(saveButton, &QPushButton::clicked, this, [this] { emit saveMapClicked(false); });
	connect(saveAsButton, &QPushButton::clicked, this, [this] { emit saveMapClicked(true); });
	connect(newButton, &QPushButton::clicked, this, &Sidebar::newMapClicked);
	auto *buttons = new QGridLayout;
	buttons->setSpacing(6);
	buttons->addWidget(openButton, 0, 0);
	buttons->addWidget(newButton, 0, 1);
	buttons->addWidget(saveButton, 1, 0);
	buttons->addWidget(saveAsButton, 1, 1);
	auto *content = new QVBoxLayout;
	content->addWidget(mapInfo_);
	content->addLayout(buttons);
	return card(tr("Device map"), content);
}

QWidget *Sidebar::buildPollingCard() {
	interval_ = new QDoubleSpinBox;
	interval_->setRange(0.0, 60000.0);
	interval_->setDecimals(2);
	interval_->setSingleStep(1.0);
	interval_->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType); /* 10 -> 9, 1 -> 0.9, 0.1 -> 0.09 */
	interval_->setSuffix(QStringLiteral(" ms"));
	interval_->setSpecialValueText(tr("max")); /* 0: the next poll as soon as one ends */
	interval_->setToolTip(tr("Time between polls: type it, or the arrows / wheel (they step by a tenth:\n"
			"1 -> 0.9 -> ... 0.1 -> 0.09). 0.25 ms = 4000 polls/s; \"max\" (0) = back to back.\n"
			"The device and the link set the real limit: see the poll rate below."));
	connect(interval_, &QDoubleSpinBox::valueChanged, this, [this] {
		emit pollingChanged();
		emit timingChanged();
	});
	poll_ = new QCheckBox(tr("Poll"));
	poll_->setChecked(true);
	connect(poll_, &QCheckBox::toggled, this, &Sidebar::pollingChanged);
	pollInfo_ = mutedLabel(QString());
	pollInfo_->setWordWrap(true); /* the "limited by" hint is long */
	auto *pollRow = new QHBoxLayout;
	pollRow->addWidget(poll_);
	pollRow->addStretch();
	pollRow->addWidget(mutedLabel(tr("every")));
	pollRow->addWidget(interval_);
	/* Auto send: always there (disabled when not offered, the tooltip says why), so the card never changes height */
	autoSend_ = new QCheckBox(tr("Auto send"));
	autoSend_->setObjectName(QStringLiteral("autoSend"));
	autoSend_->setEnabled(false);
	autoSendRate_ = new QComboBox;
	autoSendRate_->setObjectName(QStringLiteral("autoSendRate"));
	for (const int divider : IoEngine::autoSendDividers()) {
		const int hz = evre::AUTO_SEND_BASE_HZ / divider;
		autoSendRate_->addItem(tr("%1 Hz").arg(hz), hz);
	}
	setAutoSendHz(QSettings().value(QStringLiteral("poll/autoSendHz"), 100).toInt());
	rateHelp_ = tr("The rates the device makes exactly: 8000 Hz / (prescaler + 1), 4000 Hz to 40 Hz.\n"
			"On a UART a rate the link cannot carry beside the polls is lowered (the Log says so); USB CDC takes it "
			"as asked (Link).");
	connect(autoSend_, &QCheckBox::toggled, this, &Sidebar::autoSendChanged);
	connect(autoSendRate_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (index < 0) return; /* not offered: the list shows why, the rate is kept */
		rateIndex_ = index;
		QSettings().setValue(QStringLiteral("poll/autoSendHz"), autoSendHz());
		emit autoSendChanged();
	});
	auto *autoSendRow = new QHBoxLayout;
	autoSendRow->addWidget(autoSend_);
	autoSendRow->addStretch();
	autoSendRow->addWidget(autoSendRate_);
	setAutoSendOffered(false, QString(), tr("not connected"));
	/* how often the numbers on screen change: the lines of the chart move at every frame anyway */
	valuePace_ = new QComboBox;
	valuePace_->setObjectName(QStringLiteral("valuePace"));
	for (const int perSecond : ValuePace::choices())
		valuePace_->addItem(perSecond == ValuePace::EVERY_FRAME ? tr("every frame") : tr("%1 / s").arg(perSecond),
				perSecond);
	valuePace_->setCurrentIndex(valuePace_->findData(ValuePace::saved()));
	valuePace_->setToolTip(tr("How often the values in the Registers table and in the chart's legend change.\n"
			"A number that changes at every frame cannot be read; the chart's lines still move at every frame."));
	connect(valuePace_, &QComboBox::currentIndexChanged, this, [this] {
		ValuePace::save(valuesPerSecond());
		emit valuePaceChanged();
	});
	/* the card's two lists one width, one under the other */
	const int listWidth = std::max(autoSendRate_->sizeHint().width(), valuePace_->sizeHint().width());
	autoSendRate_->setFixedWidth(listWidth);
	valuePace_->setFixedWidth(listWidth);
	auto *paceRow = new QHBoxLayout;
	paceRow->addWidget(mutedLabel(tr("Show values")));
	paceRow->addStretch();
	paceRow->addWidget(valuePace_);
	recordButton_ = new QPushButton(tr("●  Record CSV"));
	recordButton_->setCursor(Qt::PointingHandCursor);
	connect(recordButton_, &QPushButton::clicked, this, &Sidebar::recordClicked);
	/* a recording opened in a window of its own: a file, or one of the last ones */
	openRecording_ = new QPushButton(tr("Open")); /* beside Record CSV in the card's width; its menu says the rest */
	openRecording_->setObjectName(QStringLiteral("openRecording"));
	openRecording_->setToolTip(tr("Open a recording (a CSV recorded or exported here) in a window of its own: its chart, "
			"measurements and notes. The live chart goes on. A .csv dropped on the window opens too."));
	auto *recordings = new QMenu(openRecording_);
	connect(recordings, &QMenu::aboutToShow, this, [this, recordings] {
		RecordingWindow::fillRecentMenu(recordings, [this](const QString &file) { emit openRecordingClicked(file); },
				[this](const QString &text) { emit statusMessage(text, 5000); });
		QAction *choose = new QAction(tr("Open recording…"), recordings);
		connect(choose, &QAction::triggered, this, [this] { emit openRecordingClicked(QString()); });
		recordings->insertAction(recordings->actions().value(0), choose);
		recordings->insertSeparator(recordings->actions().value(1));
	});
	setButtonMenu(openRecording_, recordings);
	auto *recordRow = new QHBoxLayout;
	recordRow->addWidget(recordButton_, 3);
	recordRow->addWidget(openRecording_, 2);
	recordInfo_ = mutedLabel(tr("One CSV row per poll (Log column).")); /* one line in the card */
	recordInfo_->setWordWrap(true);
	auto *content = new QVBoxLayout;
	content->addLayout(pollRow);
	content->addWidget(pollInfo_);
	content->addLayout(autoSendRow);
	content->addLayout(paceRow);
	content->addSpacing(4);
	content->addLayout(recordRow);
	content->addWidget(recordInfo_);
	return card(tr("Polling & recording"), content);
}

/* Fast EVRe: a row for each of the map's streams, made by setFastStreams; hidden until the map has one */
QWidget *Sidebar::buildFastCard() {
	fastRowsLayout_ = new QVBoxLayout;
	fastRowsLayout_->setSpacing(8);
	fastCard_ = card(tr("Fast streams"), fastRowsLayout_);
	fastCard_->setVisible(false);
	return fastCard_;
}

QWidget *Sidebar::buildApiCard() {
	/* other programs reach the device through the Studio */
	apiServe_ = new QCheckBox(tr("Serve API"));
	apiNetwork_ = new QCheckBox(tr("Network (not only this PC)"));
	apiWrites_ = new QCheckBox(tr("Allow API writes"));
	apiDanger_ = new QCheckBox(tr("including ⚠ registers"));
	apiDanger_->setEnabled(false);
	/* their bold width from the start: no jump at the first tick */
	setHighlighted(apiWrites_, false, Theme::colors().warn);
	setHighlighted(apiDanger_, false, Theme::colors().bad);
	apiInfo_ = mutedLabel(QString());
	apiInfo_->setWordWrap(true);
	apiServe_->setToolTip(tr("EVRe frames on port 1219, JSON lines on port 1220 (Help: API)"));
	apiWrites_->setToolTip(tr("Off: clients can read and stream only. Not remembered: off at every start."));
	apiDanger_->setToolTip(
			tr("Also the registers marked ⚠ (the ones that move, switch or reset something). Not remembered."));
	connect(apiServe_, &QCheckBox::toggled, this, &Sidebar::apiServeChanged);
	connect(apiNetwork_, &QCheckBox::toggled, this, [this] {
		if (apiServe_->isChecked()) emit apiServeChanged(true); /* again, on the addresses chosen now */
	});
	connect(apiWrites_, &QCheckBox::toggled, this, [this](bool on) {
		emit apiWritesChanged();
		apiDanger_->setEnabled(on);
		if (!on) apiDanger_->setChecked(false);
		setHighlighted(apiWrites_, on, Theme::colors().warn);
	});
	connect(apiDanger_, &QCheckBox::toggled, this, [this](bool on) {
		emit apiWritesChanged();
		setHighlighted(apiDanger_, on, Theme::colors().bad);
	});
	auto *content = new QVBoxLayout;
	content->addWidget(apiServe_);
	content->addWidget(apiNetwork_);
	content->addWidget(apiWrites_);
	auto *dangerRow = new QHBoxLayout;
	dangerRow->addSpacing(22);
	dangerRow->addWidget(apiDanger_);
	content->addLayout(dangerRow);
	content->addWidget(apiInfo_);
	return card(tr("API server"), content);
}

void Sidebar::addFooter(QVBoxLayout *layout) {
	auto *helpButton = new QPushButton(tr("Help"));
	helpButton->setObjectName(QStringLiteral("sidebarHelp"));
	helpButton->setCursor(Qt::PointingHandCursor);
	helpButton->setToolTip(tr("Help (F1)"));
	connect(helpButton, &QPushButton::clicked, this, &Sidebar::helpClicked);
	themeButton_ = new QPushButton;
	themeButton_->setCursor(Qt::PointingHandCursor);
	themeChanged(); /* its text: the other theme */
	connect(themeButton_, &QPushButton::clicked, this, &Sidebar::themeClicked);
	auto *version = mutedLabel(QStringLiteral("v%1 · teknile").arg(QStringLiteral(EVRE_STUDIO_VERSION)));
	/* the buttons on a row of their own: beside the version they did not fit
	 * the sidebar ("☾  Dark theme" was cut) */
	auto *buttons = new QHBoxLayout;
	buttons->addWidget(helpButton, 1);
	buttons->addWidget(themeButton_, 1);
	layout->addLayout(buttons);
	/* the language: each in its own words; applied at the next start, with a button for it */
	language_ = new QComboBox;
	language_->setObjectName(QStringLiteral("language"));
	language_->addItem(tr("System"), QStringLiteral("system"));
	language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
	language_->addItem(QStringLiteral("العربية"), QStringLiteral("ar"));
	language_->setCurrentIndex(std::max(0, language_->findData(language::saved())));
	language_->setToolTip(tr("The window's language, applied at the next start. System: the computer's, when the Studio "
			"has it (else English)."));
	restart_ = new QPushButton(tr("Restart now"));
	restart_->setObjectName(QStringLiteral("restartNow"));
	restart_->setToolTip(tr("Close the Studio and start it again in the language chosen"));
	restart_->setVisible(language::resolve(language::saved()) != language::current());
	connect(language_, &QComboBox::activated, this, [this] {
		const QString code = language_->currentData().toString();
		language::save(code);
		restart_->setVisible(language::resolve(code) != language::current());
	});
	connect(restart_, &QPushButton::clicked, this, &Sidebar::restartRequested);
	auto *languageRow = new QHBoxLayout;
	languageRow->addWidget(mutedLabel(tr("Language")));
	languageRow->addWidget(language_, 1);
	layout->addLayout(languageRow);
	layout->addWidget(restart_);
	layout->addWidget(version, 0, Qt::AlignHCenter);
}

/* ----------------------------------------------------------------- settings */

void Sidebar::restoreLinkSettings() {
	QSettings settings;
	(settings.value(QStringLiteral("link/tcp"), true).toBool() ? tcpButton_ : serialButton_)->setChecked(true);
	linkStack_->setCurrentIndex(tcpButton_->isChecked() ? 0 : 1);
	inFlight_->setValue(savedInFlight(tcpButton_->isChecked()));
	host_->setText(settings.value(QStringLiteral("link/host"), QStringLiteral("127.0.0.1")).toString());
	port_->setValue(settings.value(QStringLiteral("link/port"), 1210).toInt());
	/* a rate saved by hand, or by an older version, that is no number in range would fail every open */
	const qint32 savedBaud = settings.value(QStringLiteral("link/baud"), QStringLiteral("115200")).toString().toInt();
	baud_->setCurrentText(savedBaud >= BAUD_MIN && savedBaud <= BAUD_MAX ? QString::number(savedBaud)
			: QStringLiteral("115200"));
	{
		const QSignalBlocker blocker(serialKind_); /* restored, not chosen: nothing to save */
		const int kind = serialKind_->findData(settings.value(SERIAL_KIND_KEY, QStringLiteral("auto")).toString());
		serialKind_->setCurrentIndex(std::max(0, kind));
	}
	interval_->setValue(settings.value(QStringLiteral("poll/interval"), 100.0).toDouble());
	timeout_->setValue(settings.value(QStringLiteral("link/timeout"), 500).toInt());
	autoReconnect_->setChecked(settings.value(QStringLiteral("link/reconnect"), true).toBool());
	const QString lastPort = settings.value(QStringLiteral("link/serial")).toString();
	refreshPorts(DeviceMap()); /* no map loaded yet */
	if (!lastPort.isEmpty()) {
		const int index = serialPort_->findData(lastPort);
		if (index >= 0) serialPort_->setCurrentIndex(index);
	}
}

void Sidebar::restoreApiSettings() {
	QSettings settings;
	apiNetwork_->setChecked(settings.value(QStringLiteral("api/network"), false).toBool());
	apiServe_->setChecked(settings.value(QStringLiteral("api/on"), false).toBool());
}

void Sidebar::saveSettings() const {
	QSettings settings;
	settings.setValue(QStringLiteral("link/tcp"), isTcp());
	settings.setValue(QStringLiteral("link/host"), host_->text());
	settings.setValue(QStringLiteral("link/port"), port_->value());
	settings.setValue(QStringLiteral("link/serial"), serialPort_->currentData().toString());
	settings.setValue(QStringLiteral("link/baud"), baud_->currentText());
	settings.setValue(QStringLiteral("link/timeout"), timeout_->value());
	settings.setValue(QStringLiteral("link/reconnect"), autoReconnect_->isChecked());
	settings.setValue(QStringLiteral("poll/interval"), interval_->value());
	settings.setValue(QStringLiteral("api/on"), apiServe_->isChecked());
	settings.setValue(QStringLiteral("api/network"), apiNetwork_->isChecked()); /* the write switches are never saved */
}

int Sidebar::savedInFlight(bool tcp) const {
	return tcp ? QSettings().value(QStringLiteral("link/inflightTcp"), 4).toInt()
			: QSettings().value(QStringLiteral("link/inflightSerial"), 1).toInt();
}

/* ----------------------------------------------- the command line's choices */

void Sidebar::useTcp(const QString &hostPort) {
	tcpButton_->setChecked(true);
	const int colon = int(hostPort.lastIndexOf(QLatin1Char(':')));
	host_->setText(colon > 0 ? hostPort.left(colon) : hostPort);
	if (colon > 0) port_->setValue(hostPort.mid(colon + 1).toInt());
}

void Sidebar::useSerial(const QString &portBaud) {
	serialButton_->setChecked(true);
	const QStringList parts = portBaud.split(QLatin1Char(':'));
	int index = serialPort_->findData(parts.value(0));
	if (index < 0) {
		addPort(parts.value(0), QString(), false, 0, 0, QString());
		index = serialPort_->count() - 1;
	}
	serialPort_->setCurrentIndex(index);
	if (parts.size() > 1) baud_->setCurrentText(parts.value(1));
}

void Sidebar::setToken(const QString &token) { token_->setText(token); }

void Sidebar::setPollInterval(double ms) { interval_->setValue(ms); }

void Sidebar::setInFlight(int requests) { inFlight_->setValue(requests); }

void Sidebar::tickApiSwitches(bool serve, bool writes, bool danger) {
	if (serve) apiServe_->setChecked(true);
	if (writes) apiWrites_->setChecked(true);
	if (danger) apiDanger_->setChecked(true);
}

/* ------------------------------------------------------ the connection card */

bool Sidebar::isTcp() const { return tcpButton_->isChecked(); }

QString Sidebar::host() const { return host_->text().trimmed(); }

quint16 Sidebar::port() const { return quint16(port_->value()); }

QString Sidebar::token() const { return token_->text(); }

QString Sidebar::serialPort() const { return serialPort_->currentData().toString(); }

qint32 Sidebar::baud() const { return baud_->currentText().toInt(); }

void Sidebar::focusBaud() {
	serialButton_->setChecked(true);
	baud_->setFocus(Qt::OtherFocusReason);
	baud_->lineEdit()->selectAll();
}

Sidebar::SerialKind Sidebar::serialKindOf(bool hasVid, quint16 vid, quint16 pid, const QString &portName) {
	/* the USB-to-UART bridges: the baud rate is real on the bridge's far side. MCP2200 and MCP2221 are CDC ACM devices,
	 * so they come before Linux's ttyACM name. A debug probe's virtual COM port is a bridge too, to the target's UART,
	 * though it carries the probe maker's IDs: read as USB CDC, Auto send would flood a board at 115200 */
	if (hasVid) {
		switch (vid) {
		case 0x0403: /* FTDI */
		case 0x10C4: /* Silicon Labs CP210x */
		case 0x1A86: /* WCH CH340, CH343 */
		case 0x067B: /* Prolific PL2303 */
		case 0x1366: /* Segger J-Link */
			return SerialKind::Uart;
		case 0x04D8: /* Microchip: only its MCP2200 and MCP2221 are bridges */
			if (pid == 0x00DF || pid == 0x00DD) return SerialKind::Uart;
			break;
		case 0x0483: /* ST: the ST-Link V2-1 and V3 probes are bridges; 5740 and the rest a chip's own USB */
			if (pid == 0x374B || pid == 0x374E || pid == 0x374F || (pid >= 0x3752 && pid <= 0x3754)) return SerialKind::Uart;
			break;
		case 0x0D28: /* Arm DAPLink */
			if (pid == 0x0204) return SerialKind::Uart;
			break;
		case 0x2E8A: /* Raspberry Pi: its Debug Probe is a bridge, a Pico's own USB is not */
			if (pid == 0x000C) return SerialKind::Uart;
			break;
		case 0x2341: /* Arduino: the Uno and the Mega 2560 talk through a 16U2 (or 8U2) bridge */
			if (pid == 0x0043 || pid == 0x0001 || pid == 0x0042 || pid == 0x0010) return SerialKind::Uart;
			break;
		default: break;
		}
	}
	/* Linux: ttyUSB is a vendor's bridge driver, ttyACM the CDC class driver */
	const QString name = portName.mid(portName.lastIndexOf(QLatin1Char('/')) + 1);
	if (name.startsWith(QLatin1String("ttyUSB"))) return SerialKind::Uart;
	if (name.startsWith(QLatin1String("ttyACM"))) return SerialKind::UsbCdc;
	/* no USB IDs: a motherboard's COM port (ttyS); any other USB port: Windows' usbser.sys, a vendor's VCP */
	return hasVid ? SerialKind::UsbCdc : SerialKind::Uart;
}

bool Sidebar::serialBaudLimited() const {
	const QString kind = serialKind_->currentData().toString();
	if (kind == QLatin1String("cdc")) return false;
	if (kind == QLatin1String("uart")) return true;
	const QVariant port = serialPort_->currentData(SERIAL_KIND_ROLE);
	return !port.isValid() || SerialKind(port.toInt()) == SerialKind::Uart;
}

int Sidebar::slave() const { return slave_->value(); }

void Sidebar::setSlave(int slave) { slave_->setValue(slave); }

void Sidebar::setSlaveEditable(bool editable) {
	slave_->setEnabled(editable);
	if (editable) slave_->setHelp(QString()); /* its range again */
	else slave_->setToolTip(tr("Each device of the bus has its own: Devices on the link"));
}

int Sidebar::timeoutMs() const { return timeout_->value(); }

int Sidebar::inFlight() const { return inFlight_->value(); }

bool Sidebar::autoReconnect() const { return autoReconnect_->isChecked(); }

void Sidebar::refreshPorts(const DeviceMap &map) {
	const QString keep = serialPort_->currentData().toString();
	{
		const QSignalBlocker blocker(serialPort_); /* the port chosen is known once the list is whole */
		serialPort_->clear();
		for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
			const bool mapped = map.usbVid && port.hasVendorIdentifier() && port.vendorIdentifier() == map.usbVid
					&& port.productIdentifier() == map.usbPid;
			addPort(port.portName(), port.description(), port.hasVendorIdentifier(), port.vendorIdentifier(),
					port.productIdentifier(), mapped ? map.device : QString());
			if (mapped && keep.isEmpty()) serialPort_->setCurrentIndex(serialPort_->count() - 1);
		}
		if (serialPort_->count() == 0) serialPort_->addItem(tr("no ports found"), QString());
		const int index = serialPort_->findData(keep);
		if (index >= 0) serialPort_->setCurrentIndex(index);
	}
	showSerialKind();
}

void Sidebar::addPort(const QString &name, const QString &description, bool hasVid, quint16 vid, quint16 pid,
		const QString &mappedName) {
	/* single spaces and a dot before each part: the old "COM10  · name  0483:5740" read as two labels */
	QStringList parts{ name };
	const QString what = hasVid && !mappedName.isEmpty() ? mappedName : description;
	if (!what.isEmpty()) parts << what;
	if (hasVid)
		parts << QStringLiteral("%1:%2").arg(vid, 4, 16, QLatin1Char('0')).arg(pid, 4, 16, QLatin1Char('0'));
	const QString label = parts.join(QStringLiteral(" · "));
	serialPort_->addItem(label, name);
	const int index = serialPort_->count() - 1;
	serialPort_->setItemData(index, label, FULL_LABEL_ROLE);
	serialPort_->setItemData(index, label, Qt::ToolTipRole);
	serialPort_->setItemData(index, int(serialKindOf(hasVid, vid, pid, name)), SERIAL_KIND_ROLE);
	widenList(serialPort_);
	elidePortLabels();
	showSerialKind();
}

/* The box clips its text at its edge, mid-word ("Example device virtual C"): each label is cut here to the room the box
 * gives its text, so an ellipsis ends what does not fit. The whole label stays in FULL_LABEL_ROLE */
void Sidebar::elidePortLabels() {
	QStyleOptionComboBox option;
	option.initFrom(serialPort_);
	option.editable = false;
	const QRect field = serialPort_->style()->subControlRect(QStyle::CC_ComboBox, &option,
			QStyle::SC_ComboBoxEditField, serialPort_);
	const int room = std::max(0, field.width() - 2); /* the label is drawn a pixel in from each side */
	const QFontMetrics metrics = serialPort_->fontMetrics();
	for (int i = 0; i < serialPort_->count(); i++) {
		const QString full = serialPort_->itemData(i, FULL_LABEL_ROLE).toString();
		if (full.isEmpty()) continue;
		const QString shown = ltrPiece(metrics.elidedText(full, Qt::ElideRight, room));
		if (serialPort_->itemText(i) != shown) serialPort_->setItemText(i, shown);
	}
}

/* the Link row's Auto says what Auto finds for the port selected; the port box's tooltip is its whole label */
void Sidebar::showSerialKind() {
	const QVariant kind = serialPort_->currentData(SERIAL_KIND_ROLE);
	const bool cdc = kind.isValid() && SerialKind(kind.toInt()) == SerialKind::UsbCdc;
	serialKind_->setItemText(0, cdc ? tr("Auto: USB CDC") : tr("Auto: UART"));
	serialPort_->setToolTip(serialPort_->currentData(FULL_LABEL_ROLE).toString());
}

/* the labels measured again whenever what they are measured by changes: the box's width, or its font (the style sheet
 * sets it when the box is polished, and again at a theme switch) */
bool Sidebar::eventFilter(QObject *watched, QEvent *event) {
	const QEvent::Type type = event->type();
	const bool font = type == QEvent::FontChange || type == QEvent::StyleChange || type == QEvent::Polish;
	if (watched == serialPort_ && (type == QEvent::Resize || font)) {
		elidePortLabels();
		if (font) widenList(serialPort_);
	} else if (watched == serialKind_ && font) {
		widenList(serialKind_);
	}
	return QScrollArea::eventFilter(watched, event);
}

void Sidebar::showDisconnected() {
	setConnectButton(tr("Connect"), QStringLiteral("primary"));
	showLinkState(QStringLiteral("idle"), tr("Disconnected"));
}

void Sidebar::showConnecting() {
	showLinkState(QStringLiteral("busy"), tr("Connecting…"));
	setConnectButton(tr("Cancel"), QStringLiteral("primary"));
}

void Sidebar::showConnected(const QString &link) {
	showLinkState(QStringLiteral("ok"), tr("Connected · %1").arg(link), link);
	setConnectButton(tr("Disconnect"), QStringLiteral("danger"));
}

/* The reason is the news: shown whole, its gist ("COM10 is gone:") on the first line and the rest under it when it does
 * not fit on one, so nothing of it is only in the tooltip. The system's text is for the tooltip */
void Sidebar::showLinkError(const QString &why, const QString &detail) {
	setConnectButton(tr("Connect"), QStringLiteral("primary"));
	showLinkState(QStringLiteral("error"), why, QString(), detail.isEmpty() ? QString() : why + QLatin1Char('\n') + detail);
}

void Sidebar::showReconnecting() { setConnectButton(tr("Stop reconnecting"), QStringLiteral("primary")); }

void Sidebar::setDeviceInfo(const QString &html) { deviceInfo_->setText(html); }

/* a wrap inside a piece ("protocol | rev 1") splits what reads as one thing; after a ":" or a "·" it falls between two
 * (the space before a "·" is a no-break one too, so the "·" ends its line) */
QString Sidebar::wholePieces(const QString &line) {
	QString kept = line;
	for (int i = 0; i < kept.size(); i++)
		if (kept[i] == QLatin1Char(' ') && !(i > 0 && (kept[i - 1] == QLatin1Char(':') || kept[i - 1] == QChar(0x00B7))))
			kept[i] = QChar::Nbsp;
	return kept;
}

void Sidebar::setConnectButton(const QString &text, const QString &look) {
	connectButton_->setText(text);
	connectButton_->setObjectName(look);
	repolish(connectButton_);
}

void Sidebar::showLinkState(const QString &state, const QString &text, const QString &shorter, const QString &toolTip) {
	linkState_->setProperty("state", state);
	/* an error grows the card while it is shown; every other state is one line */
	linkState_->setWraps(state == QLatin1String("error"));
	const QString dot = QStringLiteral("●  ");
	linkState_->setFullText(dot + text, toolTip, shorter.isEmpty() ? QString() : dot + shorter);
	repolish(linkState_);
}

/* ------------------------------------------------------------- the map card */

void Sidebar::showMap(const DeviceMap &map, int registers, bool modified) {
	const QString file = map.path.isEmpty() ? tr("not saved") : QFileInfo(map.path).fileName();
	const QString line = modified ? tr("%1 registers · %2 · modified").arg(registers).arg(file)
			: tr("%1 registers · %2").arg(registers).arg(file);
	mapInfo_->setText(QStringLiteral("<b>%1</b><br>").arg(map.device.toHtmlEscaped())
			+ coloredSpan(line.toHtmlEscaped(), Theme::colors().muted));
}

/* ---------------------------------------------- polling, recording, the API */

bool Sidebar::pollingOn() const { return poll_->isChecked(); }

double Sidebar::pollIntervalMs() const { return interval_->value(); }

int Sidebar::valuesPerSecond() const { return valuePace_->currentData().toInt(); }

bool Sidebar::autoSendOn() const { return autoSend_->isChecked(); }

int Sidebar::autoSendHz() const { return autoSendRate_->itemData(rateIndex_).toInt(); }

void Sidebar::setAutoSendHz(int hz) {
	int best = 0;
	for (int i = 1; i < autoSendRate_->count(); i++)
		if (std::abs(autoSendRate_->itemData(i).toInt() - hz) < std::abs(autoSendRate_->itemData(best).toInt() - hz))
			best = i;
	rateIndex_ = best;
	if (autoSendRate_->isEnabled()) autoSendRate_->setCurrentIndex(best);
}

void Sidebar::setAutoSendOn(bool on) {
	const QSignalBlocker blocker(autoSend_);
	autoSend_->setChecked(on);
}

void Sidebar::setAutoSendOffered(bool offered, const QString &why, const QString &shortWhy) {
	autoSend_->setEnabled(offered);
	autoSend_->setToolTip(offered ? tr("The device sends its read-only block (0xD000 on) by itself, at the rate beside;\n"
			"the rest is polled. Each frame is a point on the chart and a CSV row.\n"
			"Not remembered: it changes the device, so it is off at every start.") : why);
	/* not offered: the list, greyed, says why in a word or two in place of a rate (the box's tooltip has all of it);
	 * the rate chosen is kept and comes back with the offer */
	const QSignalBlocker blocker(autoSendRate_);
	autoSendRate_->setEnabled(offered);
	autoSendRate_->setPlaceholderText(shortWhy);
	autoSendRate_->setCurrentIndex(offered ? rateIndex_ : -1);
	autoSendRate_->setToolTip(offered ? rateHelp_ : why);
}

/* ------------------------------------------------------------- fast streams */

/* a row a stream: the button (Start / Stop and its name) over its two numbers, each on a line of its own (1.23 M
 * samples/s with its correction and "lost 123 456 789" do not fit one line, in Arabic even less); every line always
 * there, so the card keeps its height */
void Sidebar::setFastStreams(const QVector<StreamDef> &streams) {
	bool same = streams.size() == fastRows_.size();
	for (int i = 0; same && i < streams.size(); i++)
		same = streams[i].name == fastRows_[i].def.name && streams[i].rate == fastRows_[i].def.rate
				&& streams[i].enable == fastRows_[i].def.enable && streams[i].channels.size() == fastRows_[i].def.channels.size();
	if (same) {
		for (int i = 0; i < streams.size(); i++) fastRows_[i].def = streams[i];
		return;
	}
	while (QLayoutItem *item = fastRowsLayout_->takeAt(0)) {
		if (QLayout *row = item->layout())
			while (QLayoutItem *inner = row->takeAt(0)) {
				delete inner->widget();
				delete inner;
			}
		delete item->widget();
		delete item;
	}
	fastRows_.clear();
	for (int i = 0; i < streams.size(); i++) {
		FastRow row;
		row.def = streams[i];
		/* the stream's name heads its row, so the button says only Start or Stop: with the name on it, "Start ADC"
		 * read as a thing of the example's ADC, not of any stream. The header is one line, as the map card's: the name
		 * in the card's name weight, then muted what the stream is, cut ("...") when the sidebar is narrow; all of it
		 * and the map's description in the tooltip */
		QStringList channelNames;
		for (const StreamChannel &channel : streams[i].channels) channelNames << channel.name;
		row.channelsTip = QStringLiteral("%1\n%2").arg(tr("Channels: %1").arg(channelNames.join(QStringLiteral(", "))),
				streams[i].desc.isEmpty() ? tr("A fast stream of the map") : streams[i].desc);
		row.title = new QLabel(streams[i].name);
		row.title->setObjectName(QStringLiteral("fastStreamName"));
		row.about = new ElidedLabel;
		row.about->setObjectName(QStringLiteral("fastStreamAbout"));
		/* Log, as a register's Log column: whether a CSV recording writes the stream beside it. A whole stream or
		 * none: a block holds every channel of its instants, and the file keeps the blocks as they came */
		row.log = new QCheckBox(tr("Log"));
		row.log->setObjectName(QStringLiteral("fastLog"));
		row.log->setCursor(Qt::PointingHandCursor);
		row.log->setChecked(!QSettings().value(NOT_LOGGED_KEY).toStringList().contains(streams[i].name));
		row.log->setToolTip(tr("Log %1 beside the CSV while Record CSV runs (run.%1.evrs): its blocks as the device sent "
				"them. A stream is logged whole or not at all: each block carries every channel. Off: not written; its "
				"lines still plot. Taken when a recording starts; kept for the map's stream %1.").arg(streams[i].name));
		connect(row.log, &QCheckBox::toggled, this, [name = streams[i].name](bool on) {
			QStringList off = QSettings().value(NOT_LOGGED_KEY).toStringList();
			off.removeAll(name);
			if (!on) off << name;
			QSettings().setValue(NOT_LOGGED_KEY, off);
		});
		row.button = new QPushButton;
		row.button->setObjectName(QStringLiteral("fastStream"));
		row.button->setCursor(Qt::PointingHandCursor);
		connect(row.button, &QPushButton::clicked, this, [this, i] {
			if (i >= fastRows_.size()) return;
			fastRows_[i].on = !fastRows_[i].on;
			showFastButton(i);
			emit fastStreamToggled(i, fastRows_[i].on);
		});
		row.rate = mutedLabel(QString());
		row.lost = mutedLabel(QStringLiteral(" ")); /* a line's height while empty */
		auto *numbers = new QVBoxLayout;
		numbers->setSpacing(2);
		numbers->addWidget(row.rate);
		numbers->addWidget(row.lost);
		auto *head = new QHBoxLayout;
		head->setSpacing(6);
		head->addWidget(row.title);
		head->addWidget(row.about, 1);
		head->addWidget(row.log);
		fastRowsLayout_->addLayout(head);
		fastRowsLayout_->addWidget(row.button);
		fastRowsLayout_->addLayout(numbers);
		/* each channel: its Plot tick (a line on the chart, as a register's) and its newest value */
		for (int c = 0; c < streams[i].channels.size(); c++) {
			const StreamChannel &channel = streams[i].channels[c];
			auto *plot = new QCheckBox(channel.name);
			plot->setObjectName(QStringLiteral("fastPlot"));
			plot->setCursor(Qt::PointingHandCursor);
			plot->setToolTip(tr("Plot %1.%2 on the chart: every sample at its own time, the line broken where samples "
					"were lost.%3").arg(streams[i].name, channel.name,
							channel.desc.isEmpty() ? QString() : QLatin1Char('\n') + channel.desc));
			connect(plot, &QCheckBox::toggled, this, [this, i, c](bool on) { emit fastPlotToggled(i, c, on); });
			auto *value = mutedLabel(QStringLiteral(" "));
			value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
			value->setToolTip(tr("The newest sample of %1.%2").arg(streams[i].name, channel.name));
			auto *channelRow = new QHBoxLayout;
			channelRow->addSpacing(4);
			channelRow->addWidget(plot);
			channelRow->addStretch();
			channelRow->addWidget(value);
			fastRowsLayout_->addLayout(channelRow);
			row.plots << plot;
			row.values << value;
		}
		fastRows_.push_back(row);
		showFastAbout(i, streams[i].rate, false);
	}
	fastCard_->setVisible(!fastRows_.isEmpty());
	setFastOffered(fastOffered_, fastWhy_, fastShortWhy_);
}

QString Sidebar::streamRateText(double hz, int digits) {
	const QString text = hz >= 1e6 ? QStringLiteral("%1 MS/s").arg(hz / 1e6, 0, 'g', digits)
			: hz >= 1e3 ? QStringLiteral("%1 kS/s").arg(hz / 1e3, 0, 'g', digits)
			: QStringLiteral("%1 S/s").arg(hz, 0, 'g', digits);
	return QChar(0x2066) + text + QChar(0x2069); /* one piece in a right-to-left line too */
}

/* The header must not say another rate than the line under the button: the map's rate is what the device was built
 * for, yet a device may stream at another (the fake at 1 MS/s against a map's 10 kS/s). While the stream runs it says
 * the measured rate, rounded so it does not flicker, and at most once a second; while off the rate set, marked so */
void Sidebar::showFastAbout(int stream, double hz, bool measured, bool fromDevice) {
	if (stream < 0 || stream >= fastRows_.size()) return;
	FastRow &row = fastRows_[stream];
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (measured && row.aboutMeasured && row.aboutMs >= 0 && now - row.aboutMs < 1000) return;
	row.aboutMeasured = measured;
	row.aboutMs = measured ? now : -1;
	const int channels = int(row.def.channels.size());
	const QString rate = measured ? streamRateText(hz, 3)
			: tr("%1 set", "a stream's rate as set, not measured: 10 kS/s set").arg(streamRateText(hz));
	const QString about = (channels == 1 ? tr("· 1 channel · %1") : tr("· %n channels · %1", nullptr, channels))
			.arg(rate);
	const QString rateTip = measured ? tr("The rate measured now; the line under the button has it to the ppm")
			: fromDevice ? tr("The rate the device's %1 said when last read; the device may stream at another")
					.arg(row.def.rateReg)
			: tr("The rate the map gives; the device may stream at another");
	const QString headTip = QStringLiteral("%1 %2\n%3\n%4").arg(row.def.name, about, rateTip, row.channelsTip);
	row.title->setToolTip(headTip);
	/* the rate before the channels' count: a sidebar too narrow for both (Linux's fonts: "· 2 channels · 10 kS/s set"
	 * asked 163 px of 144) shows the rate whole, the count in the tooltip and in the ticks below, never a cut word */
	row.about->setFullText(about, headTip, QStringLiteral("· %1").arg(rate));
}

QCheckBox *Sidebar::fastLogBox(int stream) const {
	return stream >= 0 && stream < fastRows_.size() ? fastRows_[stream].log : nullptr;
}

QStringList Sidebar::fastNotLogged() const {
	QStringList off;
	for (const FastRow &row : fastRows_)
		if (row.log && !row.log->isChecked()) off << row.def.name;
	return off;
}

QPushButton *Sidebar::fastButton(int stream) const {
	return stream >= 0 && stream < fastRows_.size() ? fastRows_[stream].button : nullptr;
}

QString Sidebar::fastRateText(int stream) const {
	return stream >= 0 && stream < fastRows_.size() ? fastRows_[stream].rate->text() : QString();
}

QString Sidebar::fastRateTip(int stream) const {
	return stream >= 0 && stream < fastRows_.size() ? fastRows_[stream].rate->toolTip() : QString();
}

QString Sidebar::fastLostText(int stream) const {
	return stream >= 0 && stream < fastRows_.size() ? fastRows_[stream].lost->text().trimmed() : QString();
}

void Sidebar::setFastPlot(int stream, int channel, bool on) {
	QCheckBox *box = fastPlotBox(stream, channel);
	if (!box) return;
	const QSignalBlocker blocker(box);
	box->setChecked(on);
}

bool Sidebar::fastPlot(int stream, int channel) const {
	const QCheckBox *box = fastPlotBox(stream, channel);
	return box && box->isChecked();
}

void Sidebar::clearFastPlots() {
	for (int i = 0; i < fastRows_.size(); i++)
		for (int c = 0; c < fastRows_[i].plots.size(); c++) setFastPlot(i, c, false);
}

void Sidebar::setFastPlotsEnabled(bool enabled, const QString &why) {
	for (const FastRow &row : std::as_const(fastRows_))
		for (QCheckBox *box : row.plots) {
			box->setEnabled(enabled);
			if (!enabled && !why.isEmpty()) box->setToolTip(why);
		}
}

QCheckBox *Sidebar::fastPlotBox(int stream, int channel) const {
	if (stream < 0 || stream >= fastRows_.size() || channel < 0 || channel >= fastRows_[stream].plots.size()) return nullptr;
	return fastRows_[stream].plots[channel];
}

/* the value as the map writes it: its decimals, or four significant digits; and its unit */
void Sidebar::showFastValues(int stream, const QVector<double> &values) {
	if (stream < 0 || stream >= fastRows_.size()) return;
	const FastRow &row = fastRows_[stream];
	for (int c = 0; c < row.values.size(); c++) {
		const double v = values.value(c, NAN);
		if (!std::isfinite(v)) continue;
		const StreamChannel &channel = row.def.channels[c];
		const QString number = channel.decimals >= 0 ? QString::number(v, 'f', channel.decimals) : QString::number(v, 'g', 4);
		row.values[c]->setText(channel.unit.isEmpty() ? number : number + QLatin1Char(' ') + channel.unit);
	}
}

QString Sidebar::fastValueText(int stream, int channel) const {
	if (stream < 0 || stream >= fastRows_.size() || channel < 0 || channel >= fastRows_[stream].values.size()) return QString();
	return fastRows_[stream].values[channel]->text().trimmed();
}

bool Sidebar::fastOn(int stream) const { return stream >= 0 && stream < fastRows_.size() && fastRows_[stream].on; }

void Sidebar::setFastOn(int stream, bool on) {
	if (stream < 0 || stream >= fastRows_.size() || fastRows_[stream].on == on) return;
	fastRows_[stream].on = on;
	showFastButton(stream);
}

/* Start: the device is told to send (its enable register written 1); Stop: red, as Stop recording */
void Sidebar::showFastButton(int stream) {
	const FastRow &row = fastRows_[stream];
	row.button->setText(row.on ? tr("■  Stop stream") : tr("▶  Start stream"));
	row.button->setObjectName(row.on ? QStringLiteral("danger") : QStringLiteral("fastStream"));
	repolish(row.button);
	QStringList channels;
	for (const StreamChannel &c : row.def.channels) channels << c.name;
	const QString what = tr("%1: %2 samples a second of %3.").arg(row.def.name).arg(row.def.rate)
			.arg(channels.join(QStringLiteral(", ")));
	const QString how = row.def.enable.isEmpty()
			? tr("The device sends it by its own choice: Start listens for its blocks, Stop no longer takes them.")
			: tr("Start writes 1 to %1, Stop writes 0: the device sends its samples in numbered blocks, and every "
				 "sample lost on the way is counted.").arg(row.def.enable);
	row.button->setToolTip(fastOffered_ ? what + QLatin1Char('\n') + how + QLatin1Char('\n')
			+ tr("Not remembered: it changes the device, so it is off at every start.") : fastWhy_);
}

void Sidebar::setFastOffered(bool offered, const QString &why, const QString &shortWhy) {
	fastOffered_ = offered;
	fastWhy_ = why;
	fastShortWhy_ = shortWhy;
	for (int i = 0; i < fastRows_.size(); i++) {
		FastRow &row = fastRows_[i];
		row.button->setEnabled(offered);
		showFastButton(i);
		if (!offered) {
			row.rate->setText(shortWhy);
			row.rate->setToolTip(why);
			row.lost->setText(QStringLiteral(" "));
		}
	}
}

/* "10.0 k samples/s (+32 ppm)" and "lost 1 024", or the map's rate while off */
void Sidebar::showFastStats(const IoEngine::Stats &stats, bool connected) {
	for (int i = 0; i < fastRows_.size(); i++) {
		const IoEngine::Stats::Fast *f = i < stats.fast.size() ? &stats.fast[i] : nullptr;
		if (fastOffered_ && connected && f && f->on && f->rate > 0) showFastAbout(i, f->rate, true);
		else if (f && f->setRate > 0) showFastAbout(i, f->setRate, false, true);
		else showFastAbout(i, fastRows_[i].def.rate, false);
	}
	if (!fastOffered_ || !connected) return;
	auto samples = [](double hz) {
		return hz >= 1e6 ? tr("%1 M samples/s").arg(hz / 1e6, 0, 'f', 2)
				: hz >= 1e3 ? tr("%1 k samples/s").arg(hz / 1e3, 0, 'f', 1) : tr("%1 samples/s").arg(hz, 0, 'f', 1);
	};
	for (int i = 0; i < fastRows_.size(); i++) {
		FastRow &row = fastRows_[i];
		const IoEngine::Stats::Fast *f = i < stats.fast.size() ? &stats.fast[i] : nullptr;
		if (!f || !f->on) {
			row.rate->setText(tr("off · %1").arg(samples(f && f->setRate > 0 ? f->setRate : row.def.rate)));
			row.rate->setToolTip(f && f->setRate > 0 ? tr("The rate the device's %1 said when last read")
					.arg(row.def.rateReg) : tr("The rate the map gives the stream"));
		} else if (f->rate <= 0) {
			row.rate->setText(tr("waiting for the first block"));
			row.rate->setToolTip(QString());
		} else {
			const QString ppm = QStringLiteral("%1%2 ppm").arg(f->ppm >= 0 ? QStringLiteral("+") : QString())
					.arg(qRound(f->ppm));
			/* a text of its own for the line's direction alone: Arabic keeps "(+32 ppm)" one left-to-right piece */
			row.rate->setText(tr("%1 (%2)", "a stream's rate and its correction: 10.0 k samples/s (+32 ppm)")
					.arg(samples(f->rate), ppm));
			row.rate->setToolTip(tr("The samples a second as the Studio's clock measures the device's: the rate the "
					"device was set to, corrected by %1 parts in a million.\n%2 samples in %3 blocks since Start; "
					"%4 bad blocks; %5 samples not shown (the window did not take them in time).").arg(ppm).arg(f->records)
					.arg(f->blocks).arg(f->badBlocks + f->newerBlocks).arg(f->notShown));
		}
		const quint64 lost = f ? f->lost : 0;
		/* a count in groups of three, "1 024" (a space that does not break the line), kept one left-to-right number in
		 * a right-to-left line (an isolate: the groups would otherwise be laid out right to left, "024 1") */
		QString count = QString::number(lost);
		for (int at = int(count.size()) - 3; at > 0; at -= 3) count.insert(at, QChar(0x00A0));
		if (count.size() > 3) count = QChar(0x2066) + count + QChar(0x2069);
		row.lost->setText(f && (f->on || lost) ? tr("lost %1").arg(count) : QStringLiteral(" "));
		row.lost->setToolTip(tr("Samples the device numbered but the Studio did not get: a gap, never filled in"));
		setHighlighted(row.lost, lost > 0, Theme::colors().warn);
	}
}

void Sidebar::setRecording(bool recording) {
	recordButton_->setText(recording ? tr("■  Stop recording") : tr("●  Record CSV"));
	recordButton_->setObjectName(recording ? QStringLiteral("danger") : QString());
	repolish(recordButton_);
}

void Sidebar::showRecordSaved(const QString &file, quint64 rows) {
	recordInfo_->setText(tr("Saved %1 rows to %2").arg(rows).arg(QDir::toNativeSeparators(file)));
}

bool Sidebar::apiNetwork() const { return apiNetwork_->isChecked(); }

bool Sidebar::apiWrites() const { return apiWrites_->isChecked(); }

bool Sidebar::apiDanger() const { return apiDanger_->isChecked(); }

void Sidebar::showApiNotStarted(const QString &error) {
	const QSignalBlocker blocker(apiServe_);
	apiServe_->setChecked(false);
	apiInfo_->setText(coloredSpan(tr("not started: %1").arg(error.toHtmlEscaped()), Theme::colors().bad));
}

void Sidebar::showStats(const IoEngine::Stats &stats, bool connected) {
	showPollRate(stats, connected);
	showFastStats(stats, connected);
	if (stats.recording) {
		recordInfo_->setText(tr("Recording %1 columns · %2 rows\n%3").arg(stats.csvCols).arg(stats.csvRows)
				.arg(QFileInfo(stats.csvFile).fileName()));
	}
	showApiState(stats);
}

void Sidebar::showPollRate(const IoEngine::Stats &stats, bool connected) {
	/* a tenth below 100 a second; past it no decimals: the auto send line stays one line at 8000 frames and 4000 polls
	 * a second, in the wider fonts of Linux too */
	auto rate = [](double hz) { return hz >= 100 ? QString::number(qRound(hz)) : QString::number(hz, 'f', 1); };
	QString info = !connected ? tr("Not connected")
			: stats.autoSend ? tr("Auto send %1/s · %2 polls/s").arg(rate(stats.autoSendHz), rate(stats.pollHz))
			: tr("%1 polls/s, %2 registers in %3 reads").arg(stats.pollHz, 0, 'f', 1).arg(stats.pollable).arg(stats.blocks);
	/* the rate alone here: the card keeps its height. Why it is slower than asked goes to the status bar; with auto
	 * send on, the polls read only the rest, at whatever pace the frames leave them: no hint */
	slowHint_ = connected && poll_->isChecked() && !stats.autoSend ? slowPollReason(stats) : QString();
	pollInfo_->setText(info);
}

/* Slower than asked: say why, in one short line (the status bar shows it). A
 * poll is `blocks` reads, and `In flight` of them go at once:
 *  - In flight >= blocks: a poll's reads go together, one answer time per
 *    poll, and polls overlap (In flight / blocks of them, at most 8): the
 *    limit is overlap / latency;
 *  - In flight < blocks: a poll's reads go In flight at a time, so a poll
 *    takes ceil(blocks / In flight) answer times: the limit is one over that.
 * Said only when the rate is near that limit: the limit is then what holds it
 * back. A serial link answers one request at a time: no In flight to raise. */
QString Sidebar::slowPollReason(const IoEngine::Stats &stats) const {
	const double latencyMs = stats.master.avgLatencyMs;
	const double wantHz = interval_->value() > 0 ? 1000.0 / interval_->value() : 0;
	const bool slower = stats.blocks > 0 && latencyMs > 0 && stats.pollHz > 0
			&& (wantHz == 0 || stats.pollHz < wantHz * 0.9);
	if (!slower) return {};
	const int most = IoEngine::MAX_POLLS_UNDER_WAY, inFlight = inFlight_->value(), blocks = stats.blocks;
	const int overlap = std::clamp(inFlight / blocks, 1, most);
	const int rounds = inFlight >= blocks ? 1 : (blocks + inFlight - 1) / inFlight; /* answer times per poll */
	const double limitHz = inFlight >= blocks ? overlap * 1000.0 / latencyMs : 1000.0 / (rounds * latencyMs);
	if (stats.pollHz <= limitHz * 0.6) return {};
	const QString answer = QString::number(latencyMs, 'f', 1);
	if (!isTcp())
		return tr("Slower than asked: a poll is %1 reads, %2 at a time on the serial link, %3 ms per answer: %4 ms per "
				"poll.").arg(blocks).arg(inFlight).arg(answer).arg(rounds * latencyMs, 0, 'f', 1);
	const int suggest = suggestedInFlight(blocks, inFlight, wantHz, latencyMs, inFlight_->maximum());
	if (overlap >= most || suggest <= inFlight)
		return tr("Slower than asked: %1 ms per answer is the link's limit.").arg(answer);
	const QString more = suggest >= most * blocks ? tr(" (%1 polls at once: the most)").arg(most) : QString();
	if (inFlight < blocks)
		return tr("Slower than asked: a poll is %1 reads, sent %2 at a time; %3 ms per answer. Set In flight to %4%5 for "
				"more.").arg(blocks).arg(inFlight).arg(answer).arg(suggest).arg(more);
	return tr("Slower than asked: a poll is %1 reads; In flight %2 runs %3 poll(s) at once; %4 ms per answer. Set In "
			"flight to %5%6 for more.").arg(blocks).arg(inFlight).arg(overlap).arg(answer).arg(suggest).arg(more);
}

int Sidebar::suggestedInFlight(int blocks, int inFlight, double wantHz, double latencyMs, int maxInFlight) {
	const int most = IoEngine::MAX_POLLS_UNDER_WAY;
	blocks = std::max(1, blocks);
	const int overlap = std::clamp(inFlight / blocks, 1, most);
	const int needed = wantHz > 0 ? int(std::ceil(wantHz * latencyMs / 1000.0)) : most;
	const int polls = std::min(most, std::max(needed, overlap + 1)); /* past `most` the engine runs no more */
	return std::min(maxInFlight, polls * blocks);
}

void Sidebar::showApiState(const IoEngine::Stats &stats) {
	if (stats.apiRunning) {
		apiInfo_->setText(tr("EVRe :%1 · JSON :%2 on %3<br>%4 clients · %5 requests")
				.arg(stats.evrePort).arg(stats.jsonPort)
				.arg(apiNetwork_->isChecked() ? tr("the network") : tr("this PC"))
				.arg(stats.apiClients).arg(stats.apiRequests));
	} else if (!apiInfo_->text().contains(QLatin1String("not started"))) { /* why it did not start stays */
		apiInfo_->setText(tr("Off. Scripts: see Help → API.")); /* one line in the card */
	}
}

void Sidebar::themeChanged() {
	themeButton_->setText(Theme::isDark() ? tr("☀  Light theme") : tr("☾  Dark theme"));
	refreshButton_->setIcon(refreshIcon(Theme::colors().text)); /* drawn in the text colour of the look now */
	/* the switches' highlights in the colours of the look now */
	setHighlighted(apiWrites_, apiWrites_->isChecked(), Theme::colors().warn);
	setHighlighted(apiDanger_, apiDanger_->isChecked(), Theme::colors().bad);
}

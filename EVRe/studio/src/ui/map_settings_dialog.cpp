/* SPDX-License-Identifier: Apache-2.0 */
/* Map settings: see map_settings_dialog.h. */
#include "ui/map_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QListWidget>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "model/map_document.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* an ID box: empty = none (0), else 0x.. or decimal up to 0xFFFF */
bool readId(const QLineEdit *box, uint16_t &value) {
	const QString text = box->text().trimmed();
	if (text.isEmpty()) {
		value = 0;
		return true;
	}
	bool ok = false;
	const uint parsed = parseAddress(text, &ok);
	value = uint16_t(parsed);
	return ok && parsed <= 0xFFFF;
}

QString idText(uint16_t value) { return value ? addrText(value) : QString(); }

QSpinBox *numberBox(int max, const QString &zero) {
	auto *box = new QSpinBox;
	box->setRange(0, max);
	box->setSpecialValueText(zero);
	return box;
}

} // namespace

MapSettingsDialog::MapSettingsDialog(MapDocument *doc, bool onBus, QWidget *parent) : QDialog(parent), doc_(doc) {
	setWindowTitle(tr("Map settings"));
	setMinimumSize(720, 640); /* the Streams page: its form, four channels and the checks */
	auto *pages = new QTabWidget;
	pages->addTab(buildDevice(), tr("Device"));
	if (onBus) {
		slave_->setEnabled(false);
		slave_->setToolTip(tr("Not used on a bus: each device's slave address is in the bus file (Devices on the "
				"link > Edit…)."));
	}
	pages->addTab(buildProtocol(), tr("Protocol"));
	pages->addTab(buildNotes(), tr("Notes"));
	pages->addTab(buildStreams(), tr("Streams"));

	error_ = mutedLabel(QString());
	error_->setStyleSheet(QStringLiteral("color:%1").arg(Theme::colors().bad.name()));
	auto *ok = new QPushButton(tr("OK"));
	ok->setObjectName(QStringLiteral("primary"));
	ok->setDefault(true);
	auto *cancel = new QPushButton(tr("Cancel"));
	connect(ok, &QPushButton::clicked, this, &MapSettingsDialog::accept);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	auto *buttons = new QHBoxLayout;
	buttons->addWidget(error_, 1);
	buttons->addWidget(cancel);
	buttons->addWidget(ok);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 12);
	const DeviceMap &map = doc_->map();
	if (map.isOverlay()) {
		auto *overlay = mutedLabel(tr("This map extends %1: what is set here replaces the base's.")
				.arg(QDir::toNativeSeparators(map.basePath)));
		overlay->setWordWrap(true);
		layout->addWidget(overlay);
	}
	layout->addWidget(pages, 1);
	layout->addLayout(buttons);
	load(map);
}

QWidget *MapSettingsDialog::buildDevice() {
	device_ = new QLineEdit;
	desc_ = new QLineEdit;
	deviceId_ = new QLineEdit;
	deviceId_->setPlaceholderText(tr("none: not checked"));
	deviceId_->setToolTip(tr("The device's DEVICE_ID (0xA000). Connected to another device, the Studio warns."));
	slave_ = numberBox(255, QString());
	slave_->setObjectName(QStringLiteral("mapSlave"));
	slave_->setMinimum(1); /* 0 is the broadcast address: no device answers it */
	usbVid_ = new QLineEdit;
	usbPid_ = new QLineEdit;
	usbVid_->setPlaceholderText(tr("vendor ID, e.g. 0x1234"));
	usbPid_->setPlaceholderText(tr("product ID"));
	usbVid_->setToolTip(tr("The port with these IDs is marked with the device's name in the serial port list"));
	auto *usb = new QHBoxLayout;
	usb->addWidget(usbVid_);
	usb->addWidget(usbPid_);
	login_ = new QCheckBox(tr("the device wants a token after connecting"));
	loginAddr_ = new QLineEdit;
	loginAddr_->setPlaceholderText(tr("the register the token is written to"));
	loginSize_ = numberBox(0xFFFF, QString());
	loginSize_->setMinimum(1);
	connect(login_, &QCheckBox::toggled, this, [this](bool on) {
		loginAddr_->setEnabled(on);
		loginSize_->setEnabled(on);
	});
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	form->addRow(tr("Device"), device_);
	form->addRow(tr("Description"), desc_);
	form->addRow(tr("Device ID"), deviceId_);
	form->addRow(tr("Slave address"), slave_);
	form->addRow(tr("USB"), usb);
	form->addRow(tr("Login"), login_);
	form->addRow(tr("Token register"), loginAddr_);
	form->addRow(tr("Token size (bytes)"), loginSize_);
	auto *page = new QWidget;
	page->setLayout(form);
	return page;
}

QWidget *MapSettingsDialog::buildProtocol() {
	transport_ = new QComboBox;
	transport_->setEditable(true);
	transport_->addItems({ QString(), QStringLiteral("serial"), QStringLiteral("tcp"), QStringLiteral("usb"),
			QStringLiteral("serial, tcp") });
	transport_->setToolTip(tr("How the device is reached: serial, tcp, usb, or several"));
	baud_ = numberBox(20000000, tr("not given"));
	tcpPort_ = numberBox(65535, tr("not given"));
	timeout_ = numberBox(600000, tr("not given"));
	timeout_->setSuffix(QStringLiteral(" ms"));
	protocolNotes_ = new QPlainTextEdit;
	protocolNotes_->setPlaceholderText(tr("Anything else the implementer of a host needs: framing on this link, "
			"how often it may be polled, what happens on a lost link …"));
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	form->addRow(tr("Transport"), transport_);
	form->addRow(tr("Baud rate"), baud_);
	form->addRow(tr("TCP port"), tcpPort_);
	form->addRow(tr("Answer timeout"), timeout_);
	form->addRow(tr("Byte order"), mutedLabel(tr("little endian (EVRe)")));
	form->addRow(tr("Notes"), protocolNotes_);
	auto *page = new QWidget;
	page->setLayout(form);
	return page;
}

QWidget *MapSettingsDialog::buildNotes() {
	notes_ = new QPlainTextEdit;
	notes_->setPlaceholderText(tr("Notes on the whole map, for its readers and its export (Markdown is fine)"));
	groupNotes_ = new QTableWidget(0, 2);
	groupNotes_->setObjectName(QStringLiteral("groupNotes"));
	groupNotes_->setHorizontalHeaderLabels({ tr("Group"), tr("Notes") });
	groupNotes_->horizontalHeader()->setStretchLastSection(true);
	groupNotes_->verticalHeader()->hide();
	groupNotes_->setColumnWidth(0, 160);
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->addWidget(mutedLabel(tr("The map")));
	layout->addWidget(notes_, 1);
	layout->addWidget(mutedLabel(tr("Each group (double-click a note to edit it)")));
	layout->addWidget(groupNotes_, 1);
	return page;
}

namespace {

const RegType CHANNEL_TYPES[] = { RegType::U8, RegType::I8, RegType::U16, RegType::I16, RegType::U32, RegType::I32,
	RegType::F32 };
enum ChannelColumn { ChName, ChType, ChUnit, ChScale, ChOffset, ChDecimals, ChDesc, CH_COLUMNS };

QPushButton *smallButton(const QString &text, const QString &tip, const QString &name) {
	auto *button = new QPushButton(text);
	button->setObjectName(name);
	button->setToolTip(tip);
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

/* a number typed in a cell: empty gives `empty`, text that is no number gives `bad` */
double cellNumber(const QTableWidgetItem *item, double empty, double bad) {
	const QString text = item ? item->text().trimmed() : QString();
	if (text.isEmpty()) return empty;
	bool ok = false;
	const double value = QLocale::c().toDouble(QString(text).replace(QLatin1Char(','), QLatin1Char('.')), &ok);
	return ok ? value : bad;
}

QString numberText(double value) { return QString::number(value, 'g', 10); }

} // namespace

/* The streams: a list on the left, the selected one's form and its channels on the right, the checks under them. The
 * page edits a copy (streams_); OK puts it into the map with the rest of the settings. */
QWidget *MapSettingsDialog::buildStreams() {
	streams_ = doc_->map().streams;
	streamList_ = new QListWidget;
	streamList_->setObjectName(QStringLiteral("fastStreamList"));
	streamList_->setMaximumWidth(150);
	auto *add = smallButton(tr("+ Stream"), tr("A new fast stream: its window, rate and channels to fill in"),
			QStringLiteral("addStream"));
	removeStream_ = smallButton(tr("Remove"), tr("Remove the selected stream from the map"), QStringLiteral("removeStream"));
	connect(add, &QPushButton::clicked, this, &MapSettingsDialog::addStream);
	connect(removeStream_, &QPushButton::clicked, this, &MapSettingsDialog::removeStream);
	auto *listButtons = new QHBoxLayout;
	listButtons->addWidget(add);
	listButtons->addWidget(removeStream_);
	auto *left = new QVBoxLayout;
	left->addWidget(streamList_, 1);
	left->addLayout(listButtons);

	streamName_ = new QLineEdit;
	streamName_->setObjectName(QStringLiteral("streamName"));
	streamName_->setToolTip(tr("The stream's name: its lines are NAME.CHANNEL"));
	streamAddr_ = new QLineEdit;
	streamAddr_->setObjectName(QStringLiteral("streamAddr"));
	streamAddr_->setToolTip(tr("Where its blocks come from: the first address of its window in the device bank "
			"(0xD000 … 0xDFFF), which no register uses"));
	streamSize_ = new QSpinBox;
	streamSize_->setObjectName(QStringLiteral("streamSize"));
	streamSize_->setRange(0, 0x1000);
	streamSize_->setSuffix(tr(" bytes"));
	streamSize_->setToolTip(tr("The window's bytes: the largest block, its 8-byte header included"));
	streamRate_ = new QLineEdit;
	streamRate_->setObjectName(QStringLiteral("streamRate"));
	streamRate_->setToolTip(tr("The samples a second the device is built for"));
	streamEnable_ = new QComboBox;
	streamEnable_->setObjectName(QStringLiteral("streamEnable"));
	streamEnable_->setEditable(true);
	streamEnable_->setToolTip(tr("A writable register: 1 starts the stream, 0 stops it. Empty: the device sends by itself"));
	streamRateReg_ = new QComboBox;
	streamRateReg_->setObjectName(QStringLiteral("streamRateReg"));
	streamRateReg_->setEditable(true);
	streamRateReg_->setToolTip(tr("A register whose value is the rate the device is set to now. Empty: the rate above"));
	streamEnable_->addItem(QString());
	streamRateReg_->addItem(QString());
	for (const RegDef &def : doc_->map().regs) {
		if (!def.isNumeric()) continue;
		if (def.rw) streamEnable_->addItem(def.name);
		if (def.readable) streamRateReg_->addItem(def.name);
	}
	streamDesc_ = new QLineEdit;
	streamDesc_->setObjectName(QStringLiteral("streamDesc"));
	auto *form = new QFormLayout;
	form->setLabelAlignment(Qt::AlignRight);
	form->addRow(tr("Name"), streamName_);
	auto *window = new QHBoxLayout;
	window->addWidget(streamAddr_);
	window->addWidget(streamSize_);
	form->addRow(tr("Address, size"), window);
	auto *rate = new QHBoxLayout;
	rate->addWidget(streamRate_, 1);
	rate->addWidget(mutedLabel(tr("samples/s")));
	form->addRow(tr("Rate"), rate);
	form->addRow(tr("Enable"), streamEnable_);
	form->addRow(tr("Rate register"), streamRateReg_);
	form->addRow(tr("Description"), streamDesc_);

	channels_ = new QTableWidget(0, CH_COLUMNS);
	channels_->setObjectName(QStringLiteral("streamChannels"));
	channels_->setHorizontalHeaderLabels({ tr("Channel"), tr("Type"), tr("Unit"), tr("Scale"), tr("Offset"), tr("Decimals"),
			tr("Description") });
	channels_->horizontalHeader()->setStretchLastSection(true);
	channels_->verticalHeader()->hide();
	for (const auto &[column, width] : { std::pair{ ChName, 100 }, { ChType, 76 }, { ChUnit, 54 }, { ChScale, 72 },
				 { ChOffset, 60 }, { ChDecimals, 70 } })
		channels_->setColumnWidth(column, std::max(width, channels_->horizontalHeader()->fontMetrics().horizontalAdvance(
				channels_->horizontalHeaderItem(column)->text()) + 24)); /* a header never cut (Arabic is longer) */
	channels_->setMinimumHeight(channels_->horizontalHeader()->sizeHint().height() + 4 * 34 + 20); /* four channels whole */
	channels_->setToolTip(tr("The channels of one sample, in the order the device packs them: shown value = raw x scale "
			"+ offset"));
	addChannel_ = smallButton(tr("+ Channel"), tr("A new channel, after the others in a sample"), QStringLiteral("addChannel"));
	removeChannel_ = smallButton(tr("Remove channel"), tr("Remove the selected channel"), QStringLiteral("removeChannel"));
	connect(addChannel_, &QPushButton::clicked, this, &MapSettingsDialog::addChannel);
	connect(removeChannel_, &QPushButton::clicked, this, &MapSettingsDialog::removeChannel);
	streamRecord_ = mutedLabel(QString());
	streamRecord_->setObjectName(QStringLiteral("streamRecord"));
	streamRecord_->setWordWrap(true);
	auto *channelButtons = new QHBoxLayout; /* the sample's size on a line of its own above: never cut by the buttons */
	channelButtons->addStretch();
	channelButtons->addWidget(addChannel_);
	channelButtons->addWidget(removeChannel_);
	streamForm_ = new QWidget;
	auto *right = new QVBoxLayout(streamForm_);
	right->setContentsMargins(0, 0, 0, 0);
	right->addLayout(form);
	right->addWidget(channels_, 1);
	right->addWidget(streamRecord_);
	right->addLayout(channelButtons);

	streamChecks_ = mutedLabel(QString());
	streamChecks_->setObjectName(QStringLiteral("streamChecks"));
	streamChecks_->setWordWrap(true);
	streamChecks_->setStyleSheet(QStringLiteral("color:%1").arg(Theme::colors().bad.name()));
	auto *intro = mutedLabel(tr("Fast streams (Fast EVRe): the device sends their samples in numbered blocks by itself, "
			"from a window of the device bank that no register uses."));
	intro->setWordWrap(true);
	auto *columns = new QHBoxLayout;
	columns->addLayout(left);
	columns->addWidget(streamForm_, 1);
	streamsPage_ = new QWidget;
	auto *layout = new QVBoxLayout(streamsPage_);
	layout->addWidget(intro);
	layout->addLayout(columns, 1);
	layout->addWidget(streamChecks_);

	connect(streamList_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (showing_) return;
		takeStream();
		showStream(row);
	});
	const auto changed = [this] {
		if (showing_) return;
		takeStream();
		refreshStreams();
	};
	for (QLineEdit *box : { streamName_, streamAddr_, streamRate_, streamDesc_ }) connect(box, &QLineEdit::textEdited, this, changed);
	connect(streamSize_, &QSpinBox::valueChanged, this, changed);
	for (QComboBox *box : { streamEnable_, streamRateReg_ }) connect(box, &QComboBox::currentTextChanged, this, changed);
	connect(channels_, &QTableWidget::itemChanged, this, changed);
	connect(channels_, &QTableWidget::currentCellChanged, this, [this] { removeChannel_->setEnabled(current_ >= 0 && channels_->currentRow() >= 0); });

	for (const StreamDef &stream : std::as_const(streams_)) streamList_->addItem(stream.name);
	showStream(streams_.isEmpty() ? -1 : 0);
	return streamsPage_;
}

void MapSettingsDialog::showStream(int index) {
	showing_ = true;
	current_ = index >= 0 && index < streams_.size() ? index : -1;
	if (streamList_->currentRow() != current_) streamList_->setCurrentRow(current_);
	streamForm_->setEnabled(current_ >= 0);
	const StreamDef stream = current_ >= 0 ? streams_[current_] : StreamDef();
	streamName_->setText(stream.name);
	streamAddr_->setText(current_ >= 0 ? addrText(stream.addr) : QString());
	streamSize_->setValue(stream.size);
	streamRate_->setText(current_ >= 0 ? numberText(stream.rate) : QString());
	streamEnable_->setEditText(stream.enable);
	streamRateReg_->setEditText(stream.rateReg);
	streamDesc_->setText(stream.desc);
	channels_->setRowCount(0);
	for (const StreamChannel &channel : stream.channels) {
		const int row = channels_->rowCount();
		channels_->insertRow(row);
		channels_->setItem(row, ChName, new QTableWidgetItem(channel.name));
		auto *type = new QComboBox;
		for (RegType t : CHANNEL_TYPES) type->addItem(typeName(t));
		type->setCurrentText(typeName(channel.type));
		connect(type, &QComboBox::currentTextChanged, this, [this] {
			if (showing_) return;
			takeStream();
			refreshStreams();
		});
		channels_->setCellWidget(row, ChType, type);
		channels_->setItem(row, ChUnit, new QTableWidgetItem(channel.unit));
		channels_->setItem(row, ChScale, new QTableWidgetItem(numberText(channel.scale)));
		channels_->setItem(row, ChOffset, new QTableWidgetItem(numberText(channel.offset)));
		channels_->setItem(row, ChDecimals, new QTableWidgetItem(channel.decimals >= 0 ? QString::number(channel.decimals) : QString()));
		channels_->setItem(row, ChDesc, new QTableWidgetItem(channel.desc));
	}
	showing_ = false;
	refreshStreams();
}

void MapSettingsDialog::takeStream() {
	if (current_ < 0 || current_ >= streams_.size()) return;
	StreamDef &stream = streams_[current_];
	stream.name = streamName_->text().trimmed();
	bool ok = false;
	const uint addr = parseAddress(streamAddr_->text().trimmed(), &ok);
	stream.addr = ok && addr <= 0xFFFF ? uint16_t(addr) : 0; /* not an address: the checks say it is outside the bank */
	stream.size = streamSize_->value();
	bool rateOk = false; /* not a number: 0, which the checks refuse */
	stream.rate = QLocale::c().toDouble(streamRate_->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')), &rateOk);
	if (!rateOk) stream.rate = 0;
	stream.enable = streamEnable_->currentText().trimmed();
	stream.rateReg = streamRateReg_->currentText().trimmed();
	stream.desc = streamDesc_->text().trimmed();
	stream.channels.clear();
	for (int row = 0; row < channels_->rowCount(); row++) {
		StreamChannel channel;
		const auto text = [&](int column) { return channels_->item(row, column) ? channels_->item(row, column)->text().trimmed() : QString(); };
		channel.name = text(ChName);
		if (const auto *type = qobject_cast<QComboBox *>(channels_->cellWidget(row, ChType))) parseType(type->currentText(), channel.type);
		channel.unit = text(ChUnit);
		channel.scale = cellNumber(channels_->item(row, ChScale), 1, 0); /* not a number: 0, which the checks refuse */
		channel.offset = cellNumber(channels_->item(row, ChOffset), 0, 0);
		channel.decimals = text(ChDecimals).isEmpty() ? -1 : int(cellNumber(channels_->item(row, ChDecimals), -1, -1));
		channel.desc = text(ChDesc);
		stream.channels << channel;
	}
}

void MapSettingsDialog::refreshStreams() {
	for (int i = 0; i < streams_.size() && i < streamList_->count(); i++)
		streamList_->item(i)->setText(streams_[i].name.isEmpty() ? tr("(no name)") : streams_[i].name);
	removeStream_->setEnabled(current_ >= 0);
	removeChannel_->setEnabled(current_ >= 0 && channels_->currentRow() >= 0);
	if (current_ >= 0) {
		const StreamDef &stream = streams_[current_];
		streamRecord_->setText(stream.recordsPerBlock() > 0
				? tr("A sample: %1 bytes · at most %2 samples a block").arg(stream.recordSize()).arg(stream.recordsPerBlock())
				: tr("A sample: %1 bytes · no block fits the window").arg(stream.recordSize()));
	} else {
		streamRecord_->clear();
	}
	/* the map's checks of the streams, with these: what the map's checks say with them and not without */
	DeviceMap map = doc_->map(), without = doc_->map();
	map.streams = streams_;
	without.streams.clear();
	QStringList before, found;
	for (const MapIssue &issue : checkMap(without))
		if (issue.reg < 0) before << issue.text;
	for (const MapIssue &issue : checkMap(map))
		if (issue.reg < 0 && issue.error && !before.contains(issue.text)) found << issue.text;
	streamChecks_->setText(found.join(QLatin1Char('\n')));
}

void MapSettingsDialog::addStream() {
	takeStream();
	StreamDef stream;
	stream.name = QStringLiteral("S%1").arg(streams_.size() + 1);
	/* the first free 0x100 of the device bank's upper half, which neither a register nor a stream uses */
	for (uint16_t at = 0xDC00; at >= 0xD000 && at < 0xE000; at = uint16_t(at - 0x100)) {
		bool free = true;
		for (const RegDef &def : doc_->map().regs) free = free && !(def.addr < at + 0x100 && def.addr + def.size > at);
		for (const StreamDef &other : std::as_const(streams_)) free = free && !(other.addr < at + 0x100 && other.addr + other.size > at);
		if (free) {
			stream.addr = at;
			break;
		}
	}
	stream.size = 0x100;
	stream.rate = 1000;
	streams_ << stream;
	streamList_->addItem(stream.name);
	showStream(int(streams_.size() - 1));
}

void MapSettingsDialog::removeStream() {
	if (current_ < 0) return;
	streams_.removeAt(current_);
	delete streamList_->takeItem(current_);
	showStream(std::min(current_, int(streams_.size()) - 1));
}

void MapSettingsDialog::addChannel() {
	if (current_ < 0) return;
	takeStream();
	StreamChannel channel;
	channel.name = QStringLiteral("CH%1").arg(streams_[current_].channels.size() + 1);
	streams_[current_].channels << channel;
	showStream(current_);
	channels_->setCurrentCell(channels_->rowCount() - 1, ChName);
}

void MapSettingsDialog::removeChannel() {
	if (current_ < 0 || channels_->currentRow() < 0) return;
	takeStream();
	streams_[current_].channels.removeAt(channels_->currentRow());
	showStream(current_);
}

QString MapSettingsDialog::streamChecks() const { return streamChecks_->text(); }

void MapSettingsDialog::load(const DeviceMap &map) {
	device_->setText(map.device);
	desc_->setText(map.desc);
	deviceId_->setText(idText(map.deviceId));
	slave_->setValue(map.slave);
	usbVid_->setText(idText(map.usbVid));
	usbPid_->setText(idText(map.usbPid));
	login_->setChecked(map.loginAddr != 0);
	loginAddr_->setText(idText(map.loginAddr));
	loginSize_->setValue(map.loginSize);
	loginAddr_->setEnabled(map.loginAddr != 0);
	loginSize_->setEnabled(map.loginAddr != 0);
	transport_->setEditText(map.protocol.transport);
	baud_->setValue(map.protocol.baud);
	tcpPort_->setValue(map.protocol.tcpPort);
	timeout_->setValue(map.protocol.timeoutMs);
	protocolNotes_->setPlainText(map.protocol.notes);
	notes_->setPlainText(map.notes);
	/* the map's groups, and groups with notes that no register has any more */
	QStringList groups = doc_->groups();
	for (auto it = map.groupNotes.begin(); it != map.groupNotes.end(); ++it)
		if (!groups.contains(it.key())) groups << it.key();
	groupNotes_->setRowCount(int(groups.size()));
	for (int row = 0; row < groups.size(); row++) {
		auto *name = new QTableWidgetItem(groups[row]);
		name->setFlags(name->flags() & ~Qt::ItemIsEditable);
		groupNotes_->setItem(row, 0, name);
		groupNotes_->setItem(row, 1, new QTableWidgetItem(map.groupNotes.value(groups[row])));
	}
}

bool MapSettingsDialog::store(DeviceMap &map, QString &why) const {
	uint16_t id, vid, pid, login = 0;
	if (!readId(deviceId_, id)) {
		why = tr("Device ID: 0x0000 … 0xFFFF, or empty");
		return false;
	}
	if (!readId(usbVid_, vid) || !readId(usbPid_, pid)) {
		why = tr("USB IDs: 0x0000 … 0xFFFF, or empty");
		return false;
	}
	if (login_->isChecked() && (!readId(loginAddr_, login) || login == 0)) {
		why = tr("Token register: an address (not 0x0000)");
		return false;
	}
	map.device = device_->text().trimmed();
	map.desc = desc_->text().trimmed();
	map.deviceId = id;
	map.slave = uint8_t(slave_->value());
	map.usbVid = vid;
	map.usbPid = pid;
	map.loginAddr = login_->isChecked() ? login : 0;
	map.loginSize = loginSize_->value();
	map.protocol.transport = transport_->currentText().trimmed();
	map.protocol.baud = baud_->value();
	map.protocol.tcpPort = tcpPort_->value();
	map.protocol.timeoutMs = timeout_->value();
	map.protocol.notes = protocolNotes_->toPlainText().trimmed();
	map.notes = notes_->toPlainText().trimmed();
	map.streams = streams_;
	map.groupNotes.clear();
	for (int row = 0; row < groupNotes_->rowCount(); row++) {
		const QString notes = groupNotes_->item(row, 1) ? groupNotes_->item(row, 1)->text().trimmed() : QString();
		if (!notes.isEmpty()) map.groupNotes.insert(groupNotes_->item(row, 0)->text(), notes);
	}
	return true;
}

void MapSettingsDialog::accept() {
	takeStream();
	DeviceMap edited = doc_->map();
	QString why;
	if (!store(edited, why)) {
		error_->setText(why);
		return;
	}
	doc_->edit(tr("Map settings"), [edited](DeviceMap &map) {
		/* the settings only: the registers stay as the document has them */
		const QVector<RegDef> regs = map.regs;
		map = edited;
		map.regs = regs;
	});
	QDialog::accept();
}

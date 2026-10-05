/* SPDX-License-Identifier: Apache-2.0 */
/* Map settings: see map_settings_dialog.h. */
#include "ui/map_settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
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
	setMinimumSize(560, 520);
	auto *pages = new QTabWidget;
	pages->addTab(buildDevice(), tr("Device"));
	if (onBus) {
		slave_->setEnabled(false);
		slave_->setToolTip(tr("Not used on a bus: each device's slave address is in the bus file (Devices on the "
				"link > Edit…)."));
	}
	pages->addTab(buildProtocol(), tr("Protocol"));
	pages->addTab(buildNotes(), tr("Notes"));

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
	map.groupNotes.clear();
	for (int row = 0; row < groupNotes_->rowCount(); row++) {
		const QString notes = groupNotes_->item(row, 1) ? groupNotes_->item(row, 1)->text().trimmed() : QString();
		if (!notes.isEmpty()) map.groupNotes.insert(groupNotes_->item(row, 0)->text(), notes);
	}
	return true;
}

void MapSettingsDialog::accept() {
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

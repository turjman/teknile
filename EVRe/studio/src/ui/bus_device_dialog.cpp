/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for one device of a bus: see bus_device_dialog.h. */
#include "ui/bus_device_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include "ui/elided_label.h"

BusDeviceDialog::BusDeviceDialog(const BusFile &bus, int index, const BusDevice &start, QWidget *parent)
	: QDialog(parent), bus_(bus), index_(index), start_(start) {
	setWindowTitle(index < 0 ? tr("New device on the link") : tr("Device %1").arg(start.name));
	name_ = new QLineEdit(start.name);
	name_->setObjectName(QStringLiteral("deviceName"));
	name_->setToolTip(tr("The prefix of its registers' names: D1 makes D1_SUPPLY_V"));
	slave_ = new QSpinBox;
	slave_->setObjectName(QStringLiteral("deviceSlave"));
	slave_->setRange(1, 255); /* 0 is the broadcast address */
	slave_->setValue(start.slave);
	map_ = new QLineEdit(index >= 0 ? bus.mapPath(index) : start.map); /* a new device: an absolute path */
	map_->setObjectName(QStringLiteral("deviceMap"));
	map_->setMinimumWidth(320);
	auto *browse = new QPushButton(tr("Browse…"));
	auto *mapRow = new QHBoxLayout;
	mapRow->addWidget(map_, 1);
	mapRow->addWidget(browse);
	poll_ = new QCheckBox(tr("Polled with the others"));
	poll_->setChecked(start.poll);
	token_ = new QLineEdit(start.token);
	token_->setObjectName(QStringLiteral("deviceToken"));
	token_->setPlaceholderText(tr("Token (not stored): empty = the Connection card's"));
	token_->setToolTip(tr("For a device whose login token is not the others': written to its map's login register "
			"after connecting. Kept until the Studio closes, never saved."));
	problems_ = new ElidedLabel; /* one line, there from the start: nothing moves when a problem comes or goes */
	problems_->setObjectName(QStringLiteral("problem"));
	problems_->setTextFormat(Qt::PlainText);
	buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

	auto *form = new QFormLayout(this);
	form->addRow(tr("Name"), name_);
	form->addRow(tr("Slave"), slave_);
	form->addRow(tr("Map"), mapRow);
	form->addRow(QString(), poll_);
	form->addRow(tr("Token"), token_);
	form->addRow(problems_);
	form->addRow(buttons_);

	connect(browse, &QPushButton::clicked, this, [this] {
		const QString file = QFileDialog::getOpenFileName(this, tr("The device's map"),
				QFileInfo(map_->text()).absolutePath(), tr("EVRe map (*.json)"));
		if (!file.isEmpty()) map_->setText(file);
	});
	connect(name_, &QLineEdit::textChanged, this, &BusDeviceDialog::validate);
	connect(map_, &QLineEdit::textChanged, this, &BusDeviceDialog::validate);
	connect(slave_, &QSpinBox::valueChanged, this, &BusDeviceDialog::validate);
	connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
	validate();
}

BusDevice BusDeviceDialog::result() const {
	BusDevice device = start_;
	device.name = name_->text().trimmed();
	device.slave = uint8_t(slave_->value());
	const QString map = map_->text().trimmed();
	device.map = map.isEmpty() ? QString() : QFileInfo(map).absoluteFilePath();
	device.poll = poll_->isChecked();
	device.token = token_->text();
	return device;
}

/* the bus as it would be with this device: what checkBus says, and whether the map is there */
void BusDeviceDialog::validate() {
	BusFile trial = bus_;
	const BusDevice device = result();
	if (index_ >= 0) trial.devices[index_] = device;
	else trial.devices << device;
	QStringList problems = checkBus(trial);
	if (!map_->text().trimmed().isEmpty() && !QFileInfo::exists(device.map))
		problems << tr("no map file %1").arg(map_->text().trimmed());
	problems_->setFullText(problems.join(QStringLiteral("; ")), problems.join(QLatin1Char('\n')));
	buttons_->button(QDialogButtonBox::Ok)->setEnabled(problems.isEmpty());
}

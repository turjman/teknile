/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar's "Devices on the link" card: see bus_panel.h. */
#include "ui/bus_panel.h"

#include <QAction>
#include <QFileInfo>
#include <QGridLayout>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "ui/elided_label.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

BusPanel::BusPanel(QWidget *parent) : QWidget(parent) {
	info_ = new ElidedLabel;
	info_->setObjectName(QStringLiteral("muted"));

	/* one device: a bus is made or opened */
	oneDevice_ = new QWidget;
	auto *newBus = new QPushButton(tr("New bus"));
	newBus->setToolTip(tr("Several devices on this link: a bus of the map's device, to add the others to"));
	auto *openBus = new QPushButton(tr("Open bus…"));
	auto *oneRow = new QHBoxLayout(oneDevice_);
	oneRow->setContentsMargins(0, 0, 0, 0);
	oneRow->setSpacing(6);
	oneRow->addWidget(newBus);
	oneRow->addWidget(openBus);
	connect(newBus, &QPushButton::clicked, this, &BusPanel::newBusClicked);
	connect(openBus, &QPushButton::clicked, this, &BusPanel::openBusClicked);

	/* a bus: its devices, and what can be done with them */
	list_ = new QListWidget;
	list_->setObjectName(QStringLiteral("busDevices"));
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	list_->setIconSize(QSize(10, 10));
	list_->hide();
	connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
		if (!showing_ && row >= 0) emit deviceSelected(row);
	});
	connect(list_, &QListWidget::itemDoubleClicked, this,
			[this](QListWidgetItem *item) { emit editDeviceClicked(list_->row(item)); });
	busButtons_ = new QWidget;
	auto *add = new QPushButton(tr("+ Device"));
	removeButton_ = new QPushButton(tr("− Device"));
	auto *edit = new QPushButton(tr("Edit…"));
	/* the bus file's actions in one menu: three buttons in a row do not fit the card's width */
	auto *file = new QPushButton(tr("Bus file"));
	file->setObjectName(QStringLiteral("busFile"));
	auto *fileMenu = new QMenu(file);
	fileMenu->addAction(tr("Open…"), this, &BusPanel::openBusClicked);
	fileMenu->addAction(tr("Save"), this, [this] { emit saveBusClicked(false); });
	fileMenu->addAction(tr("Save as…"), this, [this] { emit saveBusClicked(true); });
	fileMenu->addSeparator();
	QAction *close = fileMenu->addAction(tr("Close bus"), this, &BusPanel::closeBusClicked);
	close->setToolTip(tr("Back to one device: the selected one's map"));
	setButtonMenu(file, fileMenu);
	auto *grid = new QGridLayout(busButtons_);
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setSpacing(6);
	grid->addWidget(add, 0, 0);
	grid->addWidget(removeButton_, 0, 1);
	grid->addWidget(edit, 0, 2);
	grid->addWidget(file, 1, 0, 1, 3);
	/* the broadcasts kept in the bus file: the menu is made again by showBus */
	auto *presets = new QPushButton(tr("Broadcast"));
	presets->setObjectName(QStringLiteral("busBroadcast"));
	presets->setToolTip(tr("Broadcasts kept in the bus file: one value to every device at once. A confirmation "
			"follows, then each device is read back."));
	presetMenu_ = new QMenu(presets);
	presetMenu_->setToolTipsVisible(true); /* a preset that cannot be sent now says why */
	setButtonMenu(presets, presetMenu_);
	grid->addWidget(presets, 2, 0, 1, 3);
	busButtons_->hide();
	connect(add, &QPushButton::clicked, this, &BusPanel::addDeviceClicked);
	connect(removeButton_, &QPushButton::clicked, this, [this] { emit removeDeviceClicked(list_->currentRow()); });
	connect(edit, &QPushButton::clicked, this, [this] { emit editDeviceClicked(list_->currentRow()); });

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);
	layout->addWidget(info_);
	layout->addWidget(list_);
	layout->addWidget(oneDevice_);
	layout->addWidget(busButtons_);
	showBus(nullptr, -1, {}, false, false);
}

void BusPanel::showBus(const BusFile *bus, int selected, const QSet<uint8_t> &offline, bool connected,
		bool modified) {
	const bool isBus = bus && !bus->devices.isEmpty();
	oneDevice_->setVisible(!isBus);
	list_->setVisible(isBus);
	busButtons_->setVisible(isBus);
	if (!isBus) {
		info_->setFullText(tr("One device on this link: the map's.")); /* one line; New bus says the rest */
		return;
	}
	const QString file = bus->path.isEmpty() ? tr("not saved") : QFileInfo(bus->path).fileName();
	const int count = int(bus->devices.size());
	info_->setFullText(modified ? tr("%n device(s) · %1 · modified", nullptr, count).arg(file)
			: tr("%n device(s) · %1", nullptr, count).arg(file));
	removeButton_->setEnabled(bus->devices.size() > 1);

	showing_ = true;
	const QVector<PickerDevice> states = pickerDevices(*bus, offline, connected);
	while (list_->count() > bus->devices.size()) delete list_->takeItem(list_->count() - 1);
	for (int i = 0; i < bus->devices.size(); i++) {
		const BusDevice &device = bus->devices[i];
		const bool down = connected && offline.contains(device.slave);
		const QColor &dot = states[i].dot;
		const QString &state = states[i].state;
		/* the text in the text colour (a selected row stays readable in either look); the state as a dot and, for
		 * offline, in words too, before the map: a long map name is cut, not it */
		const QString map = QFileInfo(device.map).completeBaseName();
		const QString text = down && !device.poll ? tr("%1 · slave %2 · offline · not polled · %3")
				: down ? tr("%1 · slave %2 · offline · %3")
				: !device.poll ? tr("%1 · slave %2 · not polled · %3")
				: tr("%1 · slave %2 · %3");
		if (i >= list_->count()) list_->addItem(new QListWidgetItem);
		QListWidgetItem *item = list_->item(i);
		item->setText(text.arg(device.name, QString::number(device.slave), map));
		item->setIcon(stateDot(dot, list_->devicePixelRatioF()));
		item->setToolTip(tr("%1 (slave %2): %3\nmap: %4\nDouble-click to edit")
				.arg(device.name).arg(device.slave).arg(state, bus->mapPath(i)));
	}
	list_->setCurrentRow(selected);
	showing_ = false;
	/* the Broadcast menu: each preset, then New, then Remove */
	presetMenu_->clear();
	presetActions_.clear();
	for (int i = 0; i < bus->presets.size(); i++) {
		const BusPreset &preset = bus->presets[i];
		presetActions_ << presetMenu_->addAction(noMnemonic(tr("%1   (%2 = %3)").arg(preset.name, preset.reg,
				preset.value)), this, [this, i] { emit presetChosen(i); });
	}
	blockPresets();
	if (!bus->presets.isEmpty()) presetMenu_->addSeparator();
	presetMenu_->addAction(tr("New broadcast…"), this, &BusPanel::newPresetClicked);
	if (!bus->presets.isEmpty()) {
		QMenu *remove = presetMenu_->addMenu(tr("Remove"));
		for (int i = 0; i < bus->presets.size(); i++)
			remove->addAction(noMnemonic(bus->presets[i].name), this, [this, i] { emit removePresetClicked(i); });
	}
	/* as tall as its devices (up to six; more scroll): no empty box under them */
	const int rows = std::clamp(int(bus->devices.size()), 1, 6);
	list_->setFixedHeight(rows * std::max(list_->sizeHintForRow(0), 1) + 2 * list_->frameWidth() + 2);
}

void BusPanel::setBroadcastBlocked(const QString &why) {
	blocked_ = why;
	blockPresets();
}

void BusPanel::blockPresets() {
	for (QAction *action : std::as_const(presetActions_)) {
		action->setEnabled(blocked_.isEmpty());
		action->setToolTip(blocked_.isEmpty() ? tr("To every device at once, after a confirmation; each device is read "
				"back") : blocked_);
	}
}

QVector<PickerDevice> BusPanel::pickerDevices(const BusFile &bus, const QSet<uint8_t> &offline, bool connected) {
	const ThemeColors &colors = Theme::colors();
	QVector<PickerDevice> devices;
	for (const BusDevice &device : bus.devices) {
		const bool down = connected && offline.contains(device.slave);
		devices.append({ device.name, device.slave, !connected ? colors.muted : down ? colors.bad : colors.good,
				!connected ? tr("not connected") : down ? tr("offline: no answer") : tr("answers") });
	}
	return devices;
}

QString BusPanel::namesText(const QStringList &names) {
	if (names.size() <= 4) return names.join(QStringLiteral(", "));
	return tr("%1 and %n more", nullptr, int(names.size() - 3)).arg(names.mid(0, 3).join(QStringLiteral(", ")));
}

/* SPDX-License-Identifier: Apache-2.0 */
/* The main window, several devices on the link (a bus, model/bus_file.h): the
 * bus opened, saved, made and closed; its devices added, edited, removed and
 * selected; the broadcasts (quick write's "To all devices", the presets kept
 * in the bus file). The rest of the window is in main_window.cpp. */
#include "ui/main_window.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStatusBar>
#include <algorithm>
#include <memory>

#include "evre/frame.h"
#include "model/map_document.h"
#include "model/register_model.h"
#include "ui/bus_device_dialog.h"
#include "ui/bus_panel.h"
#include "ui/bus_preset_dialog.h"
#include "ui/chart_tab.h"
#include "ui/event_log.h"
#include "ui/map_editor_tab.h"
#include "ui/monitor_tab.h"
#include "ui/registers_tab.h"
#include "ui/sidebar.h"
#include "ui/ui_helpers.h"

namespace {

/* a register's own name, without its device's: D1_SPEED -> SPEED (busRegisterName the other way). It belongs beside
 * busRegisterName in model/bus_file.h; kept here until that file changes */
QString busLocalName(const QString &device, const QString &name) {
	const QString prefix = busRegisterName(device, QString());
	return name.startsWith(prefix) ? name.mid(prefix.size()) : name;
}

} // namespace

QVector<RegDef> MainWindow::tableDefinitions() const {
	if (!isBus()) return doc_->map().regs;
	QVector<RegDef> defs;
	for (int i = 0; i < bus_.devices.size(); i++) {
		const BusDevice &device = bus_.devices[i];
		for (RegDef def : deviceMap(i).regs) {
			def.slave = device.slave;
			def.name = busRegisterName(device.name, def.name);
			defs << def;
		}
	}
	return defs;
}

const DeviceMap &MainWindow::deviceMap(int device) const {
	static const DeviceMap none;
	const QString path = bus_.mapPath(device);
	if (selectedDevice_ >= 0 && path == bus_.mapPath(selectedDevice_)) return doc_->map(); /* as edited */
	const auto it = busMaps_.constFind(path);
	return it != busMaps_.constEnd() ? *it : none;
}

int MainWindow::deviceOfSlave(int slave) const {
	for (int i = 0; i < bus_.devices.size(); i++)
		if (bus_.devices[i].slave == slave) return i;
	return -1;
}

QString MainWindow::deviceLabel(int slave) const {
	const int index = deviceOfSlave(slave);
	return isBus() && index >= 0 ? bus_.devices[index].name + QStringLiteral(": ") : QString();
}

uint8_t MainWindow::slaveOf(const RegDef &def) const { return requestSlave(def.slave, uint8_t(sidebar_->slave())); }

uint8_t MainWindow::selectedSlave() const {
	return isBus() ? bus_.devices[selectedDevice_].slave : uint8_t(sidebar_->slave());
}

QVector<const DeviceMap *> MainWindow::allMaps() const {
	QVector<const DeviceMap *> maps;
	if (!isBus()) maps << &doc_->map();
	for (int i = 0; i < bus_.devices.size(); i++) maps << &deviceMap(i);
	return maps;
}

QString MainWindow::broadcastRefusal(uint16_t addr, const QByteArray &bytes) const {
	return ::broadcastRefusal(allMaps(), addr, bytes);
}

QString MainWindow::broadcastRefusal(uint16_t addr, int count) const {
	return ::broadcastRefusal(allMaps(), addr, count);
}

/* the presets are offered only when a click can send one: the link up, Allow writes on */
QString MainWindow::presetsBlocked() const {
	if (!connected_) return tr("Not connected: a broadcast needs the link.");
	if (!model_->writesEnabled()) return tr("Writes are off: tick Allow writes on the Registers tab to broadcast.");
	return {};
}

/* a device's map into busMaps_, its registers numbered (uids) as the document numbers them, so that the table
 * keeps a register's value and plot when its device is selected and the document takes the map */
bool MainWindow::loadBusMap(const QString &path, QString &err) {
	if (busMaps_.contains(path)) return true;
	DeviceMap map;
	if (!map.load(path, err)) return false;
	quint32 uid = 1;
	for (RegDef &def : map.regs) def.uid = uid++;
	busMaps_.insert(path, map);
	return true;
}

bool MainWindow::loadBus(const QString &file, bool remember) {
	if (!keepBusEdits()) return false; /* the bus open now has unsaved changes: saved, dropped or kept open */
	BusFile loaded;
	QString err;
	QStringList problems;
	if (!loaded.load(file, err)) problems << err;
	else problems << checkBus(loaded);
	/* the maps first: a bus that cannot be shown whole is not opened */
	const QHash<QString, DeviceMap> before = busMaps_;
	busMaps_.clear();
	for (int i = 0; problems.isEmpty() && i < loaded.devices.size(); i++) {
		loaded.devices[i].map = loaded.mapPath(i); /* absolute here: a Save as elsewhere writes them again */
		if (!loadBusMap(loaded.devices[i].map, err))
			problems << tr("%1: map %2: %3").arg(loaded.devices[i].name, QDir::toNativeSeparators(loaded.devices[i].map), err);
	}
	if (!problems.isEmpty() || !keepMapEdits()) {
		busMaps_ = before;
		if (!problems.isEmpty()) {
			logEvent(LogLevel::Error, tr("bus not opened: %1: %2").arg(QDir::toNativeSeparators(file), problems.join(QStringLiteral("; "))));
			QMessageBox::warning(this, tr("Bus not opened"),
					tr("%1\n\n%2").arg(QDir::toNativeSeparators(file), problems.join(QLatin1Char('\n'))));
		}
		return false;
	}
	chartTab_->clearLines();
	sidebar_->clearFastPlots(); /* the fast lines went with the others */
	model_->setDefinitions({}); /* another set of devices: nothing of the one before stays plotted or kept */
	bus_ = loaded;
	busModified_ = false;
	selectedDevice_ = 0;
	offlineDevices_.clear();
	deviceInfo_.clear();
	if (remember) rememberedBus_ = bus_.path;
	doc_->reset(busMaps_.value(bus_.devices[0].map));
	onBusChanged();
	logEvent(LogLevel::Info, tr("bus opened: %1 (%n device(s))", nullptr, int(bus_.devices.size()))
			.arg(QDir::toNativeSeparators(bus_.path)));
	return true;
}

void MainWindow::openBus() {
	const QString start = isBus() && !bus_.path.isEmpty() ? QFileInfo(bus_.path).absolutePath() : mapsFolder();
	const QString file = QFileDialog::getOpenFileName(this, tr("Open bus"), start, tr("EVRe bus (*.json)"));
	if (!file.isEmpty()) loadBus(file);
}

bool MainWindow::saveBus(bool saveAs) {
	if (!isBus()) return false;
	QString file = bus_.path;
	if (saveAs || file.isEmpty()) {
		const QString folder = QFileInfo(bus_.devices[selectedDevice_].map).absolutePath();
		file = QFileDialog::getSaveFileName(this, tr("Save bus"),
				file.isEmpty() ? folder + QStringLiteral("/bus.json") : file, tr("EVRe bus (*.json)"));
		if (file.isEmpty()) return false;
	}
	QString err;
	if (!bus_.save(file, err)) {
		QMessageBox::warning(this, tr("Bus not saved"), err);
		return false;
	}
	bus_.path = QFileInfo(file).absoluteFilePath();
	busModified_ = false;
	rememberedBus_ = bus_.path;
	showBus();
	statusBar()->showMessage(tr("Bus saved: %1").arg(QDir::toNativeSeparators(file)), 4000);
	return true;
}

/* a bus of the map's device, at the sidebar's slave address: the others are added to it */
void MainWindow::newBus() {
	if (doc_->map().path.isEmpty() || doc_->isModified()) {
		saveMap(false); /* the bus names the map by its file */
		if (doc_->map().path.isEmpty() || doc_->isModified()) return;
	}
	BusFile bus;
	BusDevice device;
	device.slave = uint8_t(sidebar_->slave());
	device.name = QStringLiteral("D%1").arg(device.slave);
	device.map = QFileInfo(doc_->map().path).absoluteFilePath();
	bus.devices << device;
	busMaps_.clear();
	DeviceMap map = doc_->map(); /* numbered by the document already */
	busMaps_.insert(device.map, map);
	bus_ = bus;
	selectedDevice_ = 0;
	busModified_ = true;
	offlineDevices_.clear();
	if (deviceInfo_.contains(0)) deviceInfo_.insert(device.slave, deviceInfo_.value(0)); /* its ID, as with one */
	onBusChanged();
	logEvent(LogLevel::Info, tr("bus made of %1 (slave %2): + Device adds the others on the link")
			.arg(device.name).arg(device.slave));
}

/* one device again: the selected one's map, at its slave address */
void MainWindow::closeBus() {
	if (!isBus() || !keepBusEdits()) return;
	const uint8_t slave = bus_.devices[selectedDevice_].slave;
	/* the one device of a map is slave 0 to the engine: what is known of its ID stays under the pill */
	if (deviceInfo_.contains(slave)) deviceInfo_.insert(0, deviceInfo_.value(slave));
	clearBus();
	rememberedBus_.clear();
	if (!doc_->map().path.isEmpty()) rememberedMap_ = QFileInfo(doc_->map().path).absoluteFilePath();
	sidebar_->setSlave(slave);
	onBusChanged();
	logEvent(LogLevel::Info, tr("bus closed: one device, %1 at slave %2").arg(doc_->map().device).arg(slave));
}

/* the bus gives way (closed, another opened, the window closed): its unsaved changes saved or dropped (true), or cancelled */
bool MainWindow::keepBusEdits() {
	if (!busModified_) return true;
	const auto answer = QMessageBox::question(this, tr("Unsaved bus"),
			tr("The devices on the link have changes that are not saved. Save them?"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
	return answer == QMessageBox::Discard || (answer == QMessageBox::Save && saveBus(false));
}

void MainWindow::clearBus() {
	bus_ = BusFile();
	selectedDevice_ = -1;
	busMaps_.clear();
	busModified_ = false;
	offlineDevices_.clear();
}

void MainWindow::addDevice() {
	if (!isBus()) return;
	BusDevice start = nextBusDevice(bus_);
	/* every address 1..255 taken: nextBusDevice gives slave 0 (the broadcast address) and no name */
	if (start.slave == 0) {
		logEvent(LogLevel::Warning, tr("no device added: every slave address (1 to 255) on the link is taken"));
		return;
	}
	start.map = bus_.devices[selectedDevice_].map; /* the same kind of device, most often */
	BusDeviceDialog dialog(bus_, -1, start, this);
	if (dialog.exec() != QDialog::Accepted) return;
	const BusDevice device = dialog.result();
	QString err;
	if (!loadBusMap(device.map, err)) {
		QMessageBox::warning(this, tr("Map not loaded"), tr("%1\n\n%2").arg(QDir::toNativeSeparators(device.map), err));
		return;
	}
	bus_.devices << device;
	busModified_ = true;
	onBusChanged();
	logEvent(LogLevel::Info, tr("device %1 added: slave %2, map %3").arg(device.name).arg(device.slave)
			.arg(QFileInfo(device.map).fileName()));
}

void MainWindow::editDevice(int index) {
	if (!isBus() || index < 0 || index >= bus_.devices.size()) return;
	BusDeviceDialog dialog(bus_, index, bus_.devices[index], this);
	if (dialog.exec() != QDialog::Accepted) return;
	const BusDevice device = dialog.result();
	const bool otherMap = device.map != bus_.devices[index].map;
	QString err;
	if (otherMap && !loadBusMap(device.map, err)) {
		QMessageBox::warning(this, tr("Map not loaded"), tr("%1\n\n%2").arg(QDir::toNativeSeparators(device.map), err));
		return;
	}
	/* the selected device takes another map: the document takes it too */
	if (otherMap && index == selectedDevice_ && !keepMapEdits()) return;
	bus_.devices[index] = device;
	if (otherMap && index == selectedDevice_) doc_->reset(busMaps_.value(device.map));
	busModified_ = true;
	onBusChanged();
}

void MainWindow::removeDevice(int index) {
	if (!isBus() || bus_.devices.size() < 2 || index < 0 || index >= bus_.devices.size()) return;
	const QString name = bus_.devices[index].name;
	if (QMessageBox::question(this, tr("Remove device"), tr("Take %1 off the bus? Its map file stays as it is.").arg(name))
			!= QMessageBox::Yes)
		return;
	if (index == selectedDevice_) {
		const int other = index == 0 ? 1 : 0;
		if (bus_.devices[other].map != bus_.devices[index].map) {
			if (!keepMapEdits()) return;
			doc_->reset(busMaps_.value(bus_.devices[other].map));
		}
		selectedDevice_ = other;
	}
	bus_.devices.removeAt(index);
	if (selectedDevice_ > index) selectedDevice_--;
	busModified_ = true;
	onBusChanged();
	logEvent(LogLevel::Info, tr("device %1 taken off the bus").arg(name));
}

void MainWindow::selectDevice(int index) {
	if (!isBus() || index < 0 || index >= bus_.devices.size() || index == selectedDevice_) return;
	const bool otherMap = bus_.devices[index].map != bus_.devices[selectedDevice_].map;
	if (otherMap && !keepMapEdits()) {
		showBus(); /* the list shows the device still selected */
		return;
	}
	selectedDevice_ = index;
	if (otherMap) doc_->reset(busMaps_.value(bus_.devices[index].map)); /* the table follows (onMapChanged) */
	onBusChanged();
}

/* the devices or the one selected changed: the table (every device's registers), the device shown, the link's
 * slave, the broadcast button, the card */
void MainWindow::onBusChanged() {
	/* several devices: none may send by itself; off before the engine gets the bus */
	if (isBus() && sidebar_->autoSendOn()) {
		sidebar_->setAutoSendOn(false);
		pushAutoSend();
	}
	updateAutoSendOffer();
	if (isBus()) stopFastStreams();
	updateFastOffer();
	const uint8_t shown = isBus() ? bus_.devices[selectedDevice_].slave : 0;
	if (!isBus()) allDevices_ = false;
	model_->setSelectedDevice(shown);
	registersTab_->setDevice(isBus() && !allDevices_ ? shown : -1);
	model_->setDefinitions(tableDefinitions()); /* the engine follows (structureChanged) */
	sidebar_->setSlaveEditable(!isBus());
	if (isBus()) sidebar_->setSlave(shown);
	monitorTab_->setSlave(selectedSlave());
	pushLinkOptions();
	if (isBus()) {
		registersTab_->setBroadcastRule([this](const RegDef &def) { return broadcastRefusal(def.addr, def.size); });
	} else {
		registersTab_->setBroadcastRule({});
	}
	showDeviceInfo();
	updateMapInfo();
	showBus();
}

void MainWindow::showBus() {
	sidebar_->busPanel()->showBus(isBus() ? &bus_ : nullptr, selectedDevice_, offlineDevices_, connected_, busModified_);
	sidebar_->busPanel()->setBroadcastBlocked(presetsBlocked());
	/* the device pickers, with the card's dots: the Registers tab's (every device, or all), the Monitor's (every
	 * device, or the broadcast), the Map editor's (the devices of its map) */
	const QVector<PickerDevice> devices = isBus() ? BusPanel::pickerDevices(bus_, offlineDevices_, connected_)
												  : QVector<PickerDevice>();
	const int shown = isBus() ? bus_.devices[selectedDevice_].slave : 0;
	registersTab_->setDevices(devices, allDevices_ ? -1 : shown);
	monitorTab_->setDevices(devices);
	if (!isBus()) {
		mapTab_->showDevices(QString(), QString(), {}, 0);
		return;
	}
	QVector<PickerDevice> sharing;
	QStringList names;
	for (int i = 0; i < devices.size(); i++) {
		if (bus_.devices[i].map != bus_.devices[selectedDevice_].map) continue;
		sharing << devices[i];
		names << devices[i].name;
	}
	const QString who = BusPanel::namesText(names).toHtmlEscaped();
	const QString file = QFileInfo(bus_.devices[selectedDevice_].map).fileName().toHtmlEscaped();
	const QString html = sharing.size() > 1
			? tr("Map of <b>%1</b> · %2 · a change applies to all %3 devices").arg(who, file).arg(sharing.size())
			: tr("Map of <b>%1</b> · %2").arg(who, file);
	mapTab_->showDevices(html, names.size() > 4 ? names.join(QStringLiteral(", ")) : QString(), sharing, shown);
}

/* "To all devices": one broadcast frame (no device answers it), then each device read back: what it holds now
 * says whether it took the value */
void MainWindow::onBroadcastRequested(int row, const QString &text) { broadcastValue(model_->rows()[row].def, text); }

/* a broadcast kept in the bus file: its register on the selected device (every device has it), its value */
void MainWindow::sendPreset(int index) {
	if (!isBus() || index < 0 || index >= bus_.presets.size()) return;
	const BusPreset preset = bus_.presets[index];
	const QString name = busRegisterName(bus_.devices[selectedDevice_].name, preset.reg);
	const auto it = std::find_if(model_->rows().begin(), model_->rows().end(),
			[&name](const RegisterModel::Row &row) { return row.def.name.compare(name, Qt::CaseInsensitive) == 0; });
	if (it == model_->rows().end()) {
		logEvent(LogLevel::Error, tr("broadcast %1 not sent: %2 has no register %3").arg(preset.name,
				bus_.devices[selectedDevice_].name, preset.reg));
		return;
	}
	if (!presetsBlocked().isEmpty()) return; /* its action is disabled then, its tooltip says why */
	broadcastValue(it->def, preset.value);
}

/* a new preset: a register a broadcast may go to (by its name in the map), and a value */
void MainWindow::newPreset() {
	if (!isBus()) return;
	QVector<RegDef> allowed;
	for (const RegDef &def : doc_->map().regs)
		if (def.rw && broadcastRefusal(def.addr, def.size).isEmpty()) allowed << def;
	BusPreset start;
	BusPresetDialog dialog(allowed, start, this);
	if (dialog.exec() != QDialog::Accepted) return;
	bus_.presets << dialog.result();
	busModified_ = true;
	showBus();
	logEvent(LogLevel::Info, tr("broadcast %1 kept in the bus (%2 = %3): Bus file > Save keeps it in the file")
			.arg(bus_.presets.last().name, bus_.presets.last().reg, bus_.presets.last().value));
}

void MainWindow::removePreset(int index) {
	if (!isBus() || index < 0 || index >= bus_.presets.size()) return;
	const QString name = bus_.presets.takeAt(index).name;
	busModified_ = true;
	showBus();
	logEvent(LogLevel::Info, tr("broadcast %1 removed from the bus").arg(name));
}

void MainWindow::broadcastValue(const RegDef &def, const QString &text) {
	/* the register's own name, without its device's (D1_SPEED -> SPEED): every device has it */
	const int device = deviceOfSlave(def.slave);
	const QString name = device >= 0 ? busLocalName(bus_.devices[device].name, def.name) : def.name;
	QByteArray bytes;
	QString err;
	if (!encodeValue(def, text, bytes, err)) {
		logEvent(LogLevel::Error, tr("broadcast %1 = %2 not sent: %3").arg(name, text, err));
		return;
	}
	const QString refusal = broadcastRefusal(def.addr, bytes);
	if (!refusal.isEmpty()) {
		logEvent(LogLevel::Error, tr("broadcast of %1 not sent: %2").arg(name, refusal));
		return;
	}
	const QString outside = def.isNumeric() ? limitProblem(def, decodeNumber(def, bytes)) : QString();
	if (!outside.isEmpty() && !confirmed(this, tr("Outside the map's limits"),
			tr("<b>%1</b> = <b>%2</b> is %3 the map gives it, on every device. Broadcast it anyway?")
					.arg(name.toHtmlEscaped(), text.toHtmlEscaped(), outside.toHtmlEscaped()),
			tr("Broadcast anyway")))
		return;
	if (!confirmed(this, tr("Broadcast"),
			tr("Write <b>%1</b> to <b>%2</b> (%3) on <b>every device</b> on the link, in one frame?<br><br>"
			   "No device answers a broadcast: each one is read back afterwards.%4")
					.arg(text.toHtmlEscaped(), name.toHtmlEscaped(), addrText(def.addr),
							def.danger ? tr("<br><br>This register moves, powers or changes something on the devices.")
									   : QString()),
			tr("Broadcast")))
		return;
	const QString what = tr("broadcast %1 = %2 (%3 at %4)").arg(name, text, evre::hex(bytes), addrText(def.addr));
	engineWrite(evre::BROADCAST, def.addr, bytes, false, [this, what](bool ok, const QString &message) {
		if (!ok) logEvent(LogLevel::Error, tr("%1: not sent: %2").arg(what, message));
	});
	/* each device read back, queued after the broadcast; the last answer reports them all */
	struct Check {
		int left = 0;
		QStringList took, kept;
	};
	auto check = std::make_shared<Check>();
	check->left = int(bus_.devices.size());
	for (const BusDevice &device : std::as_const(bus_.devices)) {
		engineRead(device.slave, def.addr, uint16_t(def.size), true,
				[this, check, def, bytes, what, deviceName = device.name](bool ok, const QByteArray &data,
						const QString &message, double) {
					if (!ok) check->kept << tr("%1 no answer (%2)").arg(deviceName, message);
					else if (data == bytes) check->took << deviceName;
					else check->kept << tr("%1 holds %2").arg(deviceName, formatValue(def, data));
					if (--check->left > 0) return;
					if (check->kept.isEmpty()) {
						logEvent(LogLevel::Info, tr("%1: every device took it (%2)").arg(what, check->took.join(QStringLiteral(", "))));
						statusBar()->showMessage(tr("Broadcast: every device took it"), 4000);
					} else {
						logEvent(LogLevel::Warning, tr("%1: not every device took it: %2").arg(what,
								check->kept.join(QStringLiteral(", "))));
					}
				});
	}
}


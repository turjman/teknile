/* SPDX-License-Identifier: Apache-2.0 */
/* The bus file, its checks, and the broadcast rule: see bus_file.h. */
#include "model/bus_file.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

#include "evre/registers.h"

namespace {

const QString FORMAT_PREFIX = QStringLiteral("evre-bus/");

bool validDeviceName(const QString &name) {
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
	return pattern.match(name).hasMatch();
}

/* the registers of a map in a form two maps can be compared by: the same text = the same registers */
QByteArray registersOf(const DeviceMap &map) {
	QVector<RegDef> regs = map.regs;
	for (RegDef &def : regs) {
		def.uid = 0; /* the Studio's own numbering, not the map's */
		def.source = -1;
	}
	return registersToJson(regs);
}

/* every byte of [addr, addr+count) lies in a writable register of the map */
bool writableInMap(const DeviceMap &map, uint16_t addr, int count) {
	for (int at = addr; at < int(addr) + count; at++) {
		const bool covered = std::any_of(map.regs.begin(), map.regs.end(), [at](const RegDef &def) {
			return def.rw && at >= def.addr && at < int(def.addr) + def.size;
		});
		if (!covered) return false;
	}
	return true;
}

} // namespace

bool BusFile::load(const QString &file, QString &err) {
	QFile in(file);
	if (!in.open(QIODevice::ReadOnly)) {
		err = in.errorString();
		return false;
	}
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &parseError);
	if (doc.isNull() || !doc.isObject()) {
		err = parseError.error != QJsonParseError::NoError ? parseError.errorString()
				: QObject::tr("not a JSON object");
		return false;
	}
	const QJsonObject root = doc.object();
	const QString format = root.value(QLatin1String("format")).toString();
	if (!format.startsWith(FORMAT_PREFIX)) {
		err = QObject::tr("not a bus file (\"format\": \"evre-bus/1\" is missing)");
		return false;
	}
	BusFile bus;
	bus.format = format;
	bus.name = root.value(QLatin1String("name")).toString();
	bus.source = root;
	bus.path = QFileInfo(file).absoluteFilePath();
	for (const QJsonValue &value : root.value(QLatin1String("devices")).toArray()) {
		const QJsonObject entry = value.toObject();
		BusDevice device;
		device.name = entry.value(QLatin1String("name")).toString();
		device.slave = uint8_t(std::clamp(entry.value(QLatin1String("slave")).toInt(1), 0, 255));
		device.map = entry.value(QLatin1String("map")).toString();
		device.poll = entry.value(QLatin1String("poll")).toBool(true);
		device.source = entry;
		bus.devices.push_back(device);
	}
	for (const QJsonValue &value : root.value(QLatin1String("broadcasts")).toArray()) {
		const QJsonObject entry = value.toObject();
		BusPreset preset;
		preset.name = entry.value(QLatin1String("name")).toString();
		preset.reg = entry.value(QLatin1String("register")).toString();
		const QJsonValue v = entry.value(QLatin1String("value"));
		preset.value = v.isDouble() ? QString::number(v.toDouble(), 'g', 17) : v.toString();
		preset.source = entry;
		bus.presets.push_back(preset);
	}
	*this = bus;
	return true;
}

bool BusFile::save(const QString &file, QString &err) const {
	const QDir folder = QFileInfo(file).absoluteDir();
	QJsonObject root = source;
	root.insert(QStringLiteral("format"), format);
	if (name.isEmpty()) root.remove(QStringLiteral("name"));
	else root.insert(QStringLiteral("name"), name);
	QJsonArray list;
	for (int i = 0; i < devices.size(); i++) {
		const BusDevice &device = devices[i];
		QJsonObject entry = device.source;
		entry.insert(QStringLiteral("name"), device.name);
		entry.insert(QStringLiteral("slave"), int(device.slave));
		/* below the bus file's folder: relative, so the folder can move as a whole */
		const QString absolute = mapPath(i);
		const QString relative = folder.relativeFilePath(absolute);
		entry.insert(QStringLiteral("map"), relative.startsWith(QLatin1String("..")) ? absolute : relative);
		if (device.poll) entry.remove(QStringLiteral("poll"));
		else entry.insert(QStringLiteral("poll"), false);
		list.append(entry);
	}
	root.insert(QStringLiteral("devices"), list);
	QJsonArray presetList;
	for (const BusPreset &preset : presets) {
		QJsonObject entry = preset.source;
		entry.insert(QStringLiteral("name"), preset.name);
		entry.insert(QStringLiteral("register"), preset.reg);
		entry.insert(QStringLiteral("value"), preset.value);
		presetList.append(entry);
	}
	if (presetList.isEmpty()) root.remove(QStringLiteral("broadcasts"));
	else root.insert(QStringLiteral("broadcasts"), presetList);
	QSaveFile out(file);
	if (!out.open(QIODevice::WriteOnly)) {
		err = out.errorString();
		return false;
	}
	out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (!out.commit()) {
		err = out.errorString();
		return false;
	}
	return true;
}

QString BusFile::mapPath(int device) const {
	const QString map = devices.value(device).map;
	if (map.isEmpty() || QFileInfo(map).isAbsolute() || path.isEmpty()) return map;
	return QFileInfo(path).absoluteDir().absoluteFilePath(map);
}

QStringList checkBus(const BusFile &bus) {
	QStringList problems;
	if (bus.devices.isEmpty()) problems << QObject::tr("no device on the bus");
	QSet<QString> names;
	QSet<int> slaves;
	for (const BusDevice &device : bus.devices) {
		if (!validDeviceName(device.name))
			problems << QObject::tr("device name \"%1\": letters, digits and _, starting with a letter").arg(device.name);
		else if (names.contains(device.name.toUpper()))
			problems << QObject::tr("device name %1 is there twice").arg(device.name);
		names.insert(device.name.toUpper());
		if (device.slave == 0)
			problems << QObject::tr("%1: slave 0 is the broadcast address, which no device answers").arg(device.name);
		else if (slaves.contains(device.slave))
			problems << QObject::tr("%1: slave %2 is another device's too").arg(device.name).arg(device.slave);
		slaves.insert(device.slave);
		if (device.map.isEmpty()) problems << QObject::tr("%1: no map").arg(device.name);
	}
	/* D1 and D1_A: is D1_A_X the register A_X of D1, or X of D1_A? */
	for (const BusDevice &a : bus.devices)
		for (const BusDevice &b : bus.devices)
			if (&a != &b && b.name.startsWith(a.name + QLatin1Char('_'), Qt::CaseInsensitive))
				problems << QObject::tr("device names %1 and %2: %2 starts with %1_, so register names could be "
										"read both ways").arg(a.name, b.name);
	return problems;
}

/* slave 0 (no name) when every address is taken: a duplicate would only be refused later, by checkBus */
BusDevice nextBusDevice(const BusFile &bus) {
	BusDevice device;
	device.slave = 0;
	for (int slave = 1; slave <= 255; slave++) {
		const bool taken = std::any_of(bus.devices.begin(), bus.devices.end(),
				[slave](const BusDevice &d) { return d.slave == slave; });
		if (taken) continue;
		device.slave = uint8_t(slave);
		device.name = QStringLiteral("D%1").arg(slave);
		break;
	}
	return device;
}

QStringList broadcastNames(const QStringList &devices, const QString &name) {
	QStringList names{ name };
	for (const QString &device : devices) names << busRegisterName(device, name);
	return names;
}

QString broadcastRefusal(const QVector<const DeviceMap *> &maps, uint16_t addr, int count) {
	if (count < 1) return QObject::tr("nothing to write");
	const int end = int(addr) + count;
	if (addr >= evre::RESERVED_FIRST && end <= evre::RESERVED_END) {
		if (addr < evre::RESERVED_WRITABLE)
			return QObject::tr("DEVICE_ID and STATUS (0xA000 .. 0xA003) are read-only on every device");
		return {}; /* the reserved bank is the same on every EVRe device */
	}
	if (maps.isEmpty()) return QObject::tr("no map");
	const QByteArray first = registersOf(*maps.front());
	for (const DeviceMap *map : maps)
		if (map->deviceId != maps.front()->deviceId || registersOf(*map) != first)
			return QObject::tr("the devices on the link have different register maps: a broadcast outside the "
							   "reserved bank (0xA004 .. 0xA105) would mean different things to them");
	if (!writableInMap(*maps.front(), addr, count))
		return QObject::tr("%1 .. %2 is not all writable registers of the map")
				.arg(addrText(addr), addrText(uint16_t(end - 1)));
	return {};
}

QString broadcastRefusal(const QVector<const DeviceMap *> &maps, uint16_t addr, const QByteArray &bytes) {
	const QString refusal = broadcastRefusal(maps, addr, int(bytes.size()));
	if (!refusal.isEmpty()) return refusal;
	/* CONFIG's low byte holds the AUTO_SEND bit */
	const int at = evre::CONFIG - addr;
	if (at >= 0 && at < bytes.size() && (uint8_t(bytes[at]) & evre::CONFIG_AUTO_SEND))
		return QObject::tr("a broadcast must not switch AUTO_SEND on: every device would send by itself at once, over "
						   "the others");
	return {};
}

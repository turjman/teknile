/* SPDX-License-Identifier: Apache-2.0 */
/* Several devices on one link: a bus file (format "evre-bus/1").
 *
 *   { "format": "evre-bus/1", "name": "Test bench",
 *     "devices": [
 *       { "name": "D1", "slave": 1, "map": "motor.json" },
 *       { "name": "D2", "slave": 2, "map": "motor.json", "poll": false } ],
 *     "broadcasts": [ { "name": "Stop all", "register": "SPEED_SET", "value": "0" } ] }
 *
 * A map describes a device; a bus file says which devices share a link, at
 * which slave address, with which map (a path relative to the bus file).
 * Several devices may share one map. The map format does not change: a
 * device's "slave" here replaces its map's.
 *
 * In the Studio every register of a device is named after the device:
 * D1_SUPPLY_V, D2_STATUS. The table, the chart, the math lines, the CSV
 * columns and the API all use those names, so any register of any device is
 * reached by its name alone. A device name is letters, digits and "_",
 * starting with a letter, unique on the bus, and no device name followed by
 * "_" may start another (D1 and D1_A would make D1_A_X ambiguous).
 *
 * "poll": false keeps a device on the bus without polling it (it is still
 * read and written on request). Keys the Studio does not know are kept when
 * the file is saved again. Tokens are never in the file.
 *
 * A broadcast (slave 0) reaches every device on the link at once and is
 * answered by none (broadcastRefusal). "broadcasts" keeps some by name: a
 * register of the devices' map by its own name (SPEED_SET, not D1_SPEED_SET)
 * and the value as typed, sent again in one click. */
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cstdint>

#include "model/device_map.h"

/* One device on the link. */
struct BusDevice {
	QString name;          /* the prefix of its registers' names: D1 -> D1_SUPPLY_V */
	uint8_t slave = 1;     /* its address on the link, 1 to 255 */
	QString map;           /* its map: as written in the file (relative to it), or absolute */
	bool poll = true;      /* polled with the others; false: read and written on request only */
	QString token;         /* its own login token, typed in the Studio; empty: the Connection card's. Never saved */
	QJsonObject source;    /* the entry as loaded: keys the Studio does not know are saved again */
};

/* A broadcast kept in the bus file, sent again by its name. */
struct BusPreset {
	QString name;          /* what the menu shows: "Stop all" */
	QString reg;           /* the register, by its name in the map (not a device's: every device has it) */
	QString value;         /* as typed: a number in shown units, or a value name */
	QJsonObject source;    /* the entry as loaded: keys the Studio does not know are saved again */
};

/* The bus file in memory. */
struct BusFile {
	QString format = QStringLiteral("evre-bus/1");
	QString name;
	QVector<BusDevice> devices;
	QVector<BusPreset> presets; /* "broadcasts" */
	QString path;          /* where it was loaded from or saved to; empty if new */
	QJsonObject source;    /* the file as loaded: unknown keys are saved again */

	/* false with err set when the file cannot be read or is not a bus file; this one is then unchanged */
	bool load(const QString &file, QString &err);
	/* the map paths are written relative to `file` when they are below its folder */
	bool save(const QString &file, QString &err) const;
	/* a device's map as an absolute path (a relative one is taken from the bus file's folder) */
	QString mapPath(int device) const;
};

/* What is wrong with a bus, in words: empty when nothing is. Names, slave addresses, maps given. */
QStringList checkBus(const BusFile &bus);

/* A free name and slave address for one more device: D<slave>, the lowest address not taken. With all 255 taken
 * there is none: the device returned has slave 0 (the broadcast address, which checkBus refuses) and no name. */
BusDevice nextBusDevice(const BusFile &bus);

/* the name a register of a device has on the bus: D1 + SUPPLY_V -> D1_SUPPLY_V */
inline QString busRegisterName(const QString &device, const QString &reg) { return device + QLatin1Char('_') + reg; }

/* The names to look a broadcast's register up by, in this order: the name as given (a bus name, D1_FAN_SPEED, or
 * the one device's map name), then the map's own name on each device of the bus (FAN_SPEED -> D1_FAN_SPEED,
 * D2_FAN_SPEED). The first one found is the register. devices: the device names on the bus; empty for one device.
 * The Studio's API, the command line and the Python package all name a broadcast's register this way. */
QStringList broadcastNames(const QStringList &devices, const QString &name);

/* Why a broadcast WRITE of `count` bytes at `addr` must not go out; empty: it may. `maps` are the maps of
 * every device on the link (one: a single device). A broadcast reaches every device and none answers, so it
 * must mean the same on all of them: into the reserved bank's writable registers (CONFIG, the messages:
 * evre::RESERVED_WRITABLE .. evre::RESERVED_END, the same on every EVRe device) always; never into DEVICE_ID and
 * STATUS (read-only); anywhere else only when every device has the same register map. */
QString broadcastRefusal(const QVector<const DeviceMap *> &maps, uint16_t addr, int count);

/* The same rule, with the bytes: also refused, a broadcast that switches AUTO_SEND on, which would make every device
 * send by itself at once (they would talk over each other on a shared link). Empty: it may go. */
QString broadcastRefusal(const QVector<const DeviceMap *> &maps, uint16_t addr, const QByteArray &bytes);

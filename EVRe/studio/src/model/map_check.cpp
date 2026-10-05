/* SPDX-License-Identifier: Apache-2.0 */
/* The checks of a map (checkMap in device_map.h): what the map editor lists
 * under the registers, errors first. */
#include "model/device_map.h"

#include <QMap>
#include <QObject>
#include <algorithm>

QVector<MapIssue> checkMap(const DeviceMap &map) {
	QVector<MapIssue> issues;
	auto error = [&](int reg, const QString &text) { issues.push_back({ reg, true, text }); };
	auto warning = [&](int reg, const QString &text) { issues.push_back({ reg, false, text }); };

	if (!map.format.startsWith(QLatin1String("evre-map/")))
		warning(-1, QObject::tr("format \"%1\" is not an EVRe map format").arg(map.format));
	if ((map.usbVid == 0) != (map.usbPid == 0)) warning(-1, QObject::tr("usb: a vendor or a product ID alone"));
	if (map.slave == 0)
		error(-1, QObject::tr("slave 0 is the broadcast address, which no device answers: a device has 1 to 255"));

	QMap<QString, int> names;
	for (int i = 0; i < map.regs.size(); i++) {
		const RegDef &def = map.regs[i];
		if (def.name.trimmed().isEmpty()) error(i, QObject::tr("%1 has no name").arg(addrText(def.addr)));
		else if (names.contains(def.name))
			error(i, QObject::tr("the name %1 is also %2's").arg(def.name, addrText(map.regs[names[def.name]].addr)));
		else names.insert(def.name, i);

		if (def.size < 1) error(i, QObject::tr("%1: a size of at least 1 byte is needed").arg(def.name));
		if (int(def.addr) + def.size > 0x10000) error(i, QObject::tr("%1 ends past address 0xFFFF").arg(def.name));
		if (def.scale == 0) error(i, QObject::tr("%1: a scale of 0 shows every value as 0").arg(def.name));
		if (def.danger && !def.rw) warning(i, QObject::tr("%1: \"danger\" on a read-only register").arg(def.name));
		if (def.write != WriteKind::Normal && !def.rw)
			warning(i, QObject::tr("%1: a write behaviour on a read-only register").arg(def.name));
		if (def.write == WriteKind::WriteOneToClear && !(def.isNumeric() && def.type != RegType::F32))
			error(i, QObject::tr("%1: write-1-to-clear needs an integer type").arg(def.name));
		if (!def.readable && (!def.special.isEmpty() || !def.enumValues.isEmpty() || !def.fields.isEmpty()))
			warning(i, QObject::tr("%1: names for values of a register that is never read").arg(def.name));

		const bool integer = def.isNumeric() && def.type != RegType::F32;
		if (!def.fields.isEmpty() && !integer)
			error(i, QObject::tr("%1: bit fields need an integer type").arg(def.name));
		if (!def.enumValues.isEmpty() && !integer)
			warning(i, QObject::tr("%1: value names on a %2 register").arg(def.name, typeName(def.type)));
		if (!def.special.isEmpty() && !def.isNumeric())
			error(i, QObject::tr("%1: special values need a number type").arg(def.name));
		if (def.hasMin() && def.hasMax() && def.min > def.max)
			error(i, QObject::tr("%1: min is above max").arg(def.name));
		if (def.hasDefault() && !limitProblem(def, def.defaultValue).isEmpty())
			warning(i, QObject::tr("%1: the default is %2").arg(def.name, limitProblem(def, def.defaultValue)));

		const int bits = 8 * def.size;
		quint64 taken = 0;
		for (const BitField &field : def.fields) {
			if (field.name.trimmed().isEmpty()) warning(i, QObject::tr("%1: a field without a name").arg(def.name));
			if (field.lsb < 0 || field.lsb + field.width > bits) {
				error(i, QObject::tr("%1: field %2 goes past bit %3").arg(def.name, field.name).arg(bits - 1));
				continue;
			}
			if (!def.rw && (field.access == FieldAccess::ReadWrite || field.access == FieldAccess::WriteOneToClear))
				warning(i, QObject::tr("%1: field %2 is writable in a read-only register").arg(def.name, field.name));
			const quint64 mask = bitMask(field.width) << field.lsb;
			if (taken & mask) warning(i, QObject::tr("%1: field %2 shares bits with another field").arg(def.name, field.name));
			taken |= mask;
		}
	}

	/* registers that share bytes: by address, each against the ones after it that start inside it */
	QVector<int> order(map.regs.size());
	for (int i = 0; i < order.size(); i++) order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return map.regs[a].addr < map.regs[b].addr; });
	for (int k = 0; k < order.size(); k++) {
		const RegDef &a = map.regs[order[k]];
		for (int m = k + 1; m < order.size(); m++) {
			const RegDef &b = map.regs[order[m]];
			if (int(b.addr) >= int(a.addr) + a.size) break;
			warning(order[m], QObject::tr("%1 overlaps %2 (%3)").arg(b.name, a.name, addrText(a.addr)));
		}
	}
	return issues;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* The map file: reading (with "extends"), and writing it back so that only
 * what was edited changes. The format is described at the top of device_map.h;
 * how the text is kept is jsondoc's (json_doc.h). */
#include "model/device_map.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <functional>

#include "model/json_doc.h"

using jsondoc::Member;
using jsondoc::Value;

namespace {

/* a number as short as it can be written: 3, -1, 0.05, 1e-06 */
QString shortNumber(double value) {
	if (value == std::floor(value) && std::fabs(value) < 1e15) return QString::number(qint64(value));
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

/* a value-name key in hex: 0x1F */
QString hexKey(quint64 value) { return QStringLiteral("0x") + QString::number(value, 16).toUpper(); }

} // namespace

/* -------------------------------------------------------------------- reading */

namespace {

/* an address or another 16-bit number: "0xD004", "53252" or 53252 */
uint16_t parseUint16(const QJsonValue &json, bool &ok) {
	ok = true;
	if (json.isDouble()) {
		const double value = json.toDouble();
		ok = value >= 0 && value <= 0xFFFF && value == std::floor(value);
		return uint16_t(value);
	}
	const uint value = parseAddress(json.toString(), &ok);
	ok = ok && value <= 0xFFFF;
	return uint16_t(value);
}

bool isHexKey(const QString &key) { return key.trimmed().startsWith(QLatin1String("0x"), Qt::CaseInsensitive); }

/* the names of values, of an enum or a field: { "0": "off", "0x10": "on" }; hex: any key is 0x.. */
QMap<qint64, QString> parseValues(const QJsonObject &json, bool &hex) {
	QMap<qint64, QString> values;
	hex = false;
	for (auto it = json.begin(); it != json.end(); ++it) {
		bool ok;
		const QString key = it.key().trimmed();
		const bool keyHex = isHexKey(key);
		const qint64 number = keyHex ? key.mid(2).toLongLong(&ok, 16) : key.toLongLong(&ok);
		if (!ok) continue;
		hex = hex || keyHex;
		values.insert(number, it.value().toString());
	}
	return values;
}

/* "special": { "-1": "not measured", "0.5": "half" }, keyed by shown values (0x.. too) */
QVector<SpecialValue> parseSpecial(const QJsonObject &json, bool &hex) {
	QVector<SpecialValue> values;
	hex = false;
	for (auto it = json.begin(); it != json.end(); ++it) {
		bool ok;
		const QString key = it.key().trimmed();
		const bool keyHex = isHexKey(key);
		const double number = keyHex ? double(key.mid(2).toLongLong(&ok, 16)) : QLocale::c().toDouble(key, &ok);
		if (!ok) continue;
		hex = hex || keyHex;
		values.push_back({ number, it.value().toString() });
	}
	std::sort(values.begin(), values.end(), [](const SpecialValue &a, const SpecialValue &b) { return a.value < b.value; });
	return values;
}

/* "bits": "high:low" (either order) or one bit, "3" */
BitField parseField(const QJsonObject &json) {
	BitField field;
	field.name = json.value(QLatin1String("name")).toString();
	const QStringList bits = json.value(QLatin1String("bits")).toString().split(QLatin1Char(':'));
	const int high = bits.value(0).toInt();
	const int low = bits.size() > 1 ? bits.value(1).toInt() : high;
	field.lsb = std::min(high, low);
	field.width = std::abs(high - low) + 1;
	field.values = parseValues(json.value(QLatin1String("values")).toObject(), field.valuesHex);
	field.desc = json.value(QLatin1String("desc")).toString();
	const QString access = json.value(QLatin1String("access")).toString().trimmed().toLower();
	if (access == QLatin1String("ro")) field.access = FieldAccess::ReadOnly;
	else if (access == QLatin1String("rw")) field.access = FieldAccess::ReadWrite;
	else if (access == QLatin1String("w1c")) field.access = FieldAccess::WriteOneToClear;
	return field;
}

/* a number, or NaN when the key is not there (or is not a number) */
double optionalNumber(const QJsonObject &json, const char *key) {
	const QJsonValue value = json.value(QLatin1String(key));
	return value.isDouble() ? value.toDouble() : NO_LIMIT;
}

bool parseRegister(const QJsonObject &json, RegDef &def, QString &err) {
	bool ok;
	def.addr = parseUint16(json.value(QLatin1String("addr")), ok);
	if (!ok) {
		err = QObject::tr("register \"%1\": bad address").arg(json.value(QLatin1String("name")).toString());
		return false;
	}
	def.name = json.value(QLatin1String("name")).toString(addrText(def.addr));
	const QString type = json.value(QLatin1String("type")).toString(QStringLiteral("u16"));
	if (!parseType(type, def.type)) {
		err = QObject::tr("register %1: unknown type \"%2\"").arg(def.name, type);
		return false;
	}
	def.size = def.type == RegType::Bytes ? json.value(QLatin1String("size")).toInt(1) : typeSize(def.type);
	def.unit = json.value(QLatin1String("unit")).toString();
	def.group = json.value(QLatin1String("group")).toString(QStringLiteral("Registers"));
	def.desc = json.value(QLatin1String("desc")).toString();
	const QString access = json.value(QLatin1String("access")).toString(QStringLiteral("ro")).trimmed().toLower();
	def.rw = access.contains(QLatin1Char('w'));
	def.readable = access != QLatin1String("wo");
	const QString write = json.value(QLatin1String("write")).toString().trimmed().toLower();
	def.write = write == QLatin1String("action") ? WriteKind::Action
			: write == QLatin1String("w1c") ? WriteKind::WriteOneToClear : WriteKind::Normal;
	def.persist = json.value(QLatin1String("persist")).toBool(false);
	def.plottable = json.value(QLatin1String("plot")).toBool(true);
	def.notes = json.value(QLatin1String("notes")).toString();
	def.danger = json.value(QLatin1String("danger")).toBool(false);
	def.hex = json.value(QLatin1String("format")).toString() == QLatin1String("hex");
	def.scale = json.value(QLatin1String("scale")).toDouble(1.0);
	def.offset = json.value(QLatin1String("offset")).toDouble(0.0);
	def.decimals = std::clamp(json.value(QLatin1String("decimals")).toInt(-1), -1, 15);
	def.min = optionalNumber(json, "min");
	def.max = optionalNumber(json, "max");
	def.clamps = json.value(QLatin1String("past_limits")).toString().trimmed().toLower() == QLatin1String("clamp");
	def.closed = json.value(QLatin1String("closed")).toBool(false);
	def.reservedZero = json.value(QLatin1String("reserved_zero")).toBool(false);
	def.enumValues = parseValues(json.value(QLatin1String("enum")).toObject(), def.enumHex);
	def.special = parseSpecial(json.value(QLatin1String("special")).toObject(), def.specialHex);
	const QJsonArray fields = json.value(QLatin1String("fields")).toArray();
	for (int j = 0; j < fields.size(); j++) {
		BitField field = parseField(fields[j].toObject());
		field.source = j;
		def.fields.push_back(field);
	}
	/* "default": a number, or a name the register gives one of its values */
	const QJsonValue fallback = json.value(QLatin1String("default"));
	def.defaultValue = fallback.isDouble() ? fallback.toDouble() : NO_LIMIT;
	if (fallback.isString()) {
		const QString name = fallback.toString();
		for (const SpecialValue &special : def.special)
			if (special.name.compare(name, Qt::CaseInsensitive) == 0) def.defaultValue = special.value;
		for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
			if (it.value().compare(name, Qt::CaseInsensitive) == 0 && !def.hasDefault())
				def.defaultValue = double(it.key()) * def.scale + def.offset;
		if (!def.hasDefault()) {
			err = QObject::tr("register %1: \"default\" names no value of it: \"%2\"").arg(def.name, name);
			return false;
		}
	}
	return true;
}

/* "login": { "addr": "0xF000", "size": 16 }; address 0 would read as "no login" */
bool parseLogin(const QJsonObject &json, uint16_t &addr, int &size, QString &err) {
	bool ok;
	addr = parseUint16(json.value(QLatin1String("addr")), ok);
	if (!ok || addr == 0) {
		err = QObject::tr("login: bad address");
		return false;
	}
	size = json.value(QLatin1String("size")).toInt(16);
	if (size < 1 || size > 0xFFFF) {
		err = QObject::tr("login: bad size");
		return false;
	}
	return true;
}

/* the map's own settings (all but the registers) */
bool parseSettings(const QJsonObject &root, DeviceMap &map, QString &err) {
	map.format = root.value(QLatin1String("format")).toString(QStringLiteral("evre-map/1"));
	map.device = root.value(QLatin1String("device")).toString();
	map.desc = root.value(QLatin1String("desc")).toString();
	map.notes = root.value(QLatin1String("notes")).toString();
	const QJsonObject protocol = root.value(QLatin1String("protocol")).toObject();
	map.protocol = MapProtocol();
	map.protocol.transport = protocol.value(QLatin1String("transport")).toString();
	map.protocol.baud = protocol.value(QLatin1String("baud")).toInt(0);
	map.protocol.tcpPort = protocol.value(QLatin1String("tcp_port")).toInt(0);
	map.protocol.timeoutMs = protocol.value(QLatin1String("timeout_ms")).toInt(0);
	map.protocol.notes = protocol.value(QLatin1String("notes")).toString();
	map.groupNotes.clear();
	const QJsonObject groups = root.value(QLatin1String("groups")).toObject();
	for (auto it = groups.begin(); it != groups.end(); ++it) {
		const QString notes = it.value().toObject().value(QLatin1String("notes")).toString();
		if (!notes.isEmpty()) map.groupNotes.insert(it.key(), notes);
	}
	bool ok = true;
	map.deviceId = 0;
	if (root.contains(QLatin1String("device_id")) && !root.value(QLatin1String("device_id")).isNull()) {
		map.deviceId = parseUint16(root.value(QLatin1String("device_id")), ok);
		if (!ok) {
			err = QObject::tr("device_id: not a 16-bit number");
			return false;
		}
	}
	map.slave = uint8_t(root.value(QLatin1String("slave")).toInt(1));
	map.usbVid = map.usbPid = 0;
	const QJsonObject usb = root.value(QLatin1String("usb")).toObject();
	if (!usb.isEmpty()) {
		map.usbVid = parseUint16(usb.value(QLatin1String("vid")), ok);
		map.usbPid = parseUint16(usb.value(QLatin1String("pid")), ok);
	}
	map.loginAddr = 0;
	map.loginSize = 16;
	const QJsonValue login = root.value(QLatin1String("login"));
	if (login.isObject() && !parseLogin(login.toObject(), map.loginAddr, map.loginSize, err)) return false;
	return true;
}

} // namespace

/* What load() read: the file's text and tree, and the map it made. An overlay
 * also has the base it extends, flattened. The registers of `flat` are the
 * ones RegDef::source points at. */
struct MapSource {
	QByteArray text;              /* this file */
	Value doc;                    /* its tree, with where each value is in `text` */
	Value flat;                   /* the map it makes: for an overlay, merged with its base (the
	                               * base's values forget where they were: they are not in `text`) */
	std::vector<int> own;         /* per register of flat: its item in doc's "registers", -1 = none */
	std::vector<int> fromBase;    /* per register of flat: its item in the base's "registers", -1 = none */
	Value base;                   /* an overlay: the base map, flattened; Null otherwise */
	Value loadedSettings;         /* the settings as loaded, written the Studio's way (settingsJson) */
	Value baseSettings;           /* the same for the base */
	QVector<RegDef> loaded;       /* per register of flat: as loaded */
};

namespace {

/* the file, and for an overlay its base read first (at most 8 deep: a loop ends there) */
struct Resolved {
	QByteArray text;
	Value doc, flat, base;
	std::vector<int> own, fromBase;
	QString basePath;
};

bool readJsonFile(const QString &file, QByteArray &text, Value &doc, QString &err) {
	QFile input(file);
	if (!input.open(QIODevice::ReadOnly)) {
		err = input.errorString();
		return false;
	}
	text = input.readAll();
	if (!jsondoc::parse(text, doc, err)) return false;
	if (!doc.isObject()) {
		err = QObject::tr("not a map: the file is not a JSON object");
		return false;
	}
	const Value *registers = doc.find(QStringLiteral("registers"));
	if (registers && !registers->isArray()) {
		err = QObject::tr("\"registers\" is not a list");
		return false;
	}
	if (registers)
		for (const Value &item : registers->items)
			if (!item.isObject()) {
				err = QObject::tr("\"registers\": an item is not an object");
				return false;
			}
	return true;
}

uint16_t addressOf(const Value &reg) {
	bool ok;
	const Value *addr = reg.find(QStringLiteral("addr"));
	return addr ? parseUint16(jsondoc::toQt(*addr), ok) : 0;
}

bool isRemoval(const Value &reg) {
	const Value *remove = reg.find(QStringLiteral("remove"));
	return remove && remove->kind == Value::Bool && remove->boolean;
}

/* the overlay `doc` on the flattened base: see "extends" in device_map.h */
void mergeOverlay(const Value &base, const Value &doc, Resolved &out) {
	out.flat = Value::makeObject();
	for (const Member &m : base.members)
		if (m.key != QLatin1String("registers")) out.flat.set(m.key, m.value);
	for (const Member &m : doc.members) {
		if (m.key == QLatin1String("registers") || m.key == QLatin1String("extends")) continue;
		if (m.value.kind == Value::Null) out.flat.remove(m.key);
		else out.flat.set(m.key, m.value);
	}
	std::vector<Value> regs;
	const Value *baseRegs = base.find(QStringLiteral("registers"));
	if (baseRegs) regs = baseRegs->items;
	out.fromBase.clear();
	for (size_t i = 0; i < regs.size(); i++) out.fromBase.push_back(int(i));
	out.own.assign(regs.size(), -1);
	const Value *ownRegs = doc.find(QStringLiteral("registers"));
	const int count = ownRegs ? int(ownRegs->items.size()) : 0;
	for (int j = 0; j < count; j++) {
		const Value &item = ownRegs->items[size_t(j)];
		const uint16_t addr = addressOf(item);
		/* the base register at this address this file has not changed yet */
		int match = -1;
		for (size_t i = 0; i < regs.size() && match < 0; i++)
			if (out.fromBase[i] >= 0 && out.own[i] < 0 && addressOf(regs[i]) == addr) match = int(i);
		if (isRemoval(item)) {
			/* a removal of what the base no longer has: nothing to do (saved, it goes) */
			if (match >= 0) {
				regs.erase(regs.begin() + match);
				out.fromBase.erase(out.fromBase.begin() + match);
				out.own.erase(out.own.begin() + match);
			}
			continue;
		}
		if (match < 0) {
			regs.push_back(item);
			out.fromBase.push_back(-1);
			out.own.push_back(j);
			continue;
		}
		Value merged = regs[size_t(match)];
		for (const Member &m : item.members) {
			if (m.key == QLatin1String("addr")) continue;
			if (m.value.kind == Value::Null) merged.remove(m.key);
			else merged.set(m.key, m.value);
		}
		merged.start = merged.end = -1;
		regs[size_t(match)] = merged;
		out.own[size_t(match)] = j;
	}
	Value array = Value::makeArray();
	array.items = regs;
	out.flat.set(QStringLiteral("registers"), array);
}

bool resolveFile(const QString &file, int depth, Resolved &out, QString &err) {
	if (!readJsonFile(file, out.text, out.doc, err)) return false;
	const Value *extends = out.doc.find(QStringLiteral("extends"));
	if (!extends || extends->kind == Value::Null) {
		out.flat = out.doc;
		const Value *regs = out.doc.find(QStringLiteral("registers"));
		const int count = regs ? int(regs->items.size()) : 0;
		out.own.clear();
		out.fromBase.assign(size_t(count), -1);
		for (int i = 0; i < count; i++) out.own.push_back(i);
		return true;
	}
	if (extends->kind != Value::String || extends->string.trimmed().isEmpty()) {
		err = QObject::tr("\"extends\" is not a file name");
		return false;
	}
	if (depth >= 8) {
		err = QObject::tr("\"extends\" goes more than 8 maps deep (does a map extend itself?)");
		return false;
	}
	out.basePath = QDir::cleanPath(QFileInfo(file).absoluteDir().absoluteFilePath(extends->string.trimmed()));
	Resolved base;
	if (!resolveFile(out.basePath, depth + 1, base, err)) {
		err = QObject::tr("base map %1: %2").arg(QDir::toNativeSeparators(out.basePath), err);
		return false;
	}
	out.base = base.flat;
	jsondoc::forgetSource(out.base); /* its values are in the base's text, not this one */
	mergeOverlay(out.base, out.doc, out);
	return true;
}

} // namespace

/* -------------------------------------------------------------------- writing */

namespace {

/* the Studio's way of writing a map (a new map, a changed register): these keys, in this order */
const QStringList REGISTER_KEYS{ "addr", "name", "type", "size", "unit", "access", "write", "persist", "group",
	"desc", "notes", "danger", "plot", "format", "scale", "offset", "decimals", "min", "max", "past_limits", "closed",
	"reserved_zero", "default",
	"special", "enum", "fields" };
const QStringList FIELD_KEYS{ "name", "bits", "access", "desc", "values" };
const QStringList SETTINGS_KEYS{ "format", "device", "desc", "notes", "device_id", "slave", "usb", "login",
	"protocol", "groups", "extends", "registers" };

Value str(const QString &text) { return Value::makeString(text); }
Value num(double number) { return Value::makeNumber(number); }

void add(Value &object, const QString &key, const Value &value) {
	Member member;
	member.key = key;
	member.value = value;
	object.members.push_back(member);
}

/* value names in number order, keyed decimal or (hex) 0x.. */
Value valuesJson(const QMap<qint64, QString> &values, bool hex) {
	Value json = Value::makeObject();
	for (auto it = values.begin(); it != values.end(); ++it) {
		const QString key = hex && it.key() >= 0 ? hexKey(quint64(it.key())) : QString::number(it.key());
		add(json, key, str(it.value()));
	}
	return json;
}

Value specialJson(const QVector<SpecialValue> &values, bool hex) {
	Value json = Value::makeObject();
	for (const SpecialValue &special : values) {
		const bool asHex = hex && special.value >= 0 && special.value == std::floor(special.value);
		add(json, asHex ? hexKey(quint64(special.value)) : shortNumber(special.value), str(special.name));
	}
	return json;
}

Value fieldJson(const BitField &field) {
	Value json = Value::makeObject();
	add(json, QStringLiteral("name"), str(field.name));
	add(json, QStringLiteral("bits"), str(field.width == 1 ? QString::number(field.lsb)
			: QStringLiteral("%1:%2").arg(field.lsb + field.width - 1).arg(field.lsb)));
	switch (field.access) {
	case FieldAccess::AsRegister: break;
	case FieldAccess::ReadOnly: add(json, QStringLiteral("access"), str(QStringLiteral("ro"))); break;
	case FieldAccess::ReadWrite: add(json, QStringLiteral("access"), str(QStringLiteral("rw"))); break;
	case FieldAccess::WriteOneToClear: add(json, QStringLiteral("access"), str(QStringLiteral("w1c"))); break;
	}
	if (!field.desc.isEmpty()) add(json, QStringLiteral("desc"), str(field.desc));
	if (!field.values.isEmpty()) add(json, QStringLiteral("values"), valuesJson(field.values, field.valuesHex));
	return json;
}

/* a register the Studio's way: every key that is not at its default, and access and group always */
Value registerJson(const RegDef &def) {
	Value json = Value::makeObject();
	add(json, QStringLiteral("addr"), str(addrText(def.addr)));
	add(json, QStringLiteral("name"), str(def.name));
	add(json, QStringLiteral("type"), str(typeName(def.type)));
	if (def.type == RegType::Bytes) add(json, QStringLiteral("size"), num(def.size));
	if (!def.unit.isEmpty()) add(json, QStringLiteral("unit"), str(def.unit));
	add(json, QStringLiteral("access"), str(accessText(def)));
	if (def.write == WriteKind::Action) add(json, QStringLiteral("write"), str(QStringLiteral("action")));
	if (def.write == WriteKind::WriteOneToClear) add(json, QStringLiteral("write"), str(QStringLiteral("w1c")));
	if (def.persist) add(json, QStringLiteral("persist"), Value::makeBool(true));
	add(json, QStringLiteral("group"), str(def.group));
	if (!def.desc.isEmpty()) add(json, QStringLiteral("desc"), str(def.desc));
	if (!def.notes.isEmpty()) add(json, QStringLiteral("notes"), str(def.notes));
	if (def.danger) add(json, QStringLiteral("danger"), Value::makeBool(true));
	if (!def.plottable) add(json, QStringLiteral("plot"), Value::makeBool(false));
	if (def.hex) add(json, QStringLiteral("format"), str(QStringLiteral("hex")));
	if (def.scale != 1.0) add(json, QStringLiteral("scale"), num(def.scale));
	if (def.offset != 0.0) add(json, QStringLiteral("offset"), num(def.offset));
	if (def.decimals >= 0) add(json, QStringLiteral("decimals"), num(def.decimals));
	if (def.hasMin()) add(json, QStringLiteral("min"), num(def.min));
	if (def.hasMax()) add(json, QStringLiteral("max"), num(def.max));
	if (def.clamps) add(json, QStringLiteral("past_limits"), str(QStringLiteral("clamp")));
	if (def.closed) add(json, QStringLiteral("closed"), Value::makeBool(true));
	if (def.reservedZero) add(json, QStringLiteral("reserved_zero"), Value::makeBool(true));
	if (def.hasDefault()) add(json, QStringLiteral("default"), num(def.defaultValue));
	if (!def.special.isEmpty()) add(json, QStringLiteral("special"), specialJson(def.special, def.specialHex));
	if (!def.enumValues.isEmpty()) add(json, QStringLiteral("enum"), valuesJson(def.enumValues, def.enumHex));
	if (!def.fields.isEmpty()) {
		Value fields = Value::makeArray();
		for (const BitField &field : def.fields) fields.items.push_back(fieldJson(field));
		add(json, QStringLiteral("fields"), fields);
	}
	return json;
}

/* the map's settings the Studio's way (no "extends", no registers) */
Value settingsJson(const DeviceMap &map) {
	Value json = Value::makeObject();
	add(json, QStringLiteral("format"), str(map.format));
	add(json, QStringLiteral("device"), str(map.device));
	if (!map.desc.isEmpty()) add(json, QStringLiteral("desc"), str(map.desc));
	if (!map.notes.isEmpty()) add(json, QStringLiteral("notes"), str(map.notes));
	if (map.deviceId) add(json, QStringLiteral("device_id"), str(addrText(map.deviceId)));
	add(json, QStringLiteral("slave"), num(map.slave));
	if (map.usbVid || map.usbPid) {
		Value usb = Value::makeObject();
		add(usb, QStringLiteral("vid"), str(addrText(map.usbVid)));
		add(usb, QStringLiteral("pid"), str(addrText(map.usbPid)));
		add(json, QStringLiteral("usb"), usb);
	}
	if (map.loginAddr) {
		Value login = Value::makeObject();
		add(login, QStringLiteral("addr"), str(addrText(map.loginAddr)));
		add(login, QStringLiteral("size"), num(map.loginSize));
		add(json, QStringLiteral("login"), login);
	}
	if (!map.protocol.isEmpty()) {
		Value protocol = Value::makeObject();
		const MapProtocol &p = map.protocol;
		if (!p.transport.isEmpty()) add(protocol, QStringLiteral("transport"), str(p.transport));
		if (p.baud) add(protocol, QStringLiteral("baud"), num(p.baud));
		if (p.tcpPort) add(protocol, QStringLiteral("tcp_port"), num(p.tcpPort));
		if (p.timeoutMs) add(protocol, QStringLiteral("timeout_ms"), num(p.timeoutMs));
		if (!p.notes.isEmpty()) add(protocol, QStringLiteral("notes"), str(p.notes));
		add(json, QStringLiteral("protocol"), protocol);
	}
	if (!map.groupNotes.isEmpty()) {
		Value groups = Value::makeObject();
		for (auto it = map.groupNotes.begin(); it != map.groupNotes.end(); ++it) {
			Value group = Value::makeObject();
			add(group, QStringLiteral("notes"), str(it.value()));
			add(groups, it.key(), group);
		}
		add(json, QStringLiteral("groups"), groups);
	}
	return json;
}

bool sameOrBothMissing(const Value *a, const Value *b) { return (!a && !b) || (a && b && jsondoc::equal(*a, *b)); }

/* `member` put into `object` where `order` places it: after the last member that comes
 * before it in `order` (keys not in `order` do not count), else first */
void insertInOrder(Value &object, const Member &member, const QStringList &order) {
	const int rank = order.indexOf(member.key);
	int after = -1;
	for (int i = 0; i < int(object.members.size()); i++) {
		const int other = order.indexOf(object.members[size_t(i)].key);
		if (other >= 0 && other < rank) after = i;
	}
	object.members.insert(object.members.begin() + (after + 1), member);
}

/* An object as `written` (read from a file) with what changed since: `then` and
 * `now` are the object the Studio's way when loaded and now. A key that did not
 * change keeps its value as written (and its place, and keys the Studio does not
 * know are kept the same way); a changed key gets `changed(key, now's value,
 * written value)`; a key now at its default goes; a new key goes where `order`
 * puts it. Nothing changed: the result is identical to `written`. */
Value keepAsWritten(const Value *written, const Value &then, const Value &now, const QStringList &order,
		const std::function<Value(const QString &, const Value &, const Value *)> &changed) {
	if (!written) return now;
	Value out = Value::makeObject();
	for (const Member &m : written->members) {
		const Value *was = then.find(m.key), *is = now.find(m.key);
		if (sameOrBothMissing(was, is)) {
			out.members.push_back(m);
		} else if (is) {
			Member edited = m;
			edited.value = changed(m.key, *is, &m.value);
			out.members.push_back(edited);
		}
	}
	for (const Member &m : now.members) {
		if (written->indexOf(m.key) >= 0) continue;
		if (sameOrBothMissing(then.find(m.key), &m.value)) continue; /* a default the file left out */
		Member added = m;
		added.value = changed(m.key, m.value, nullptr);
		insertInOrder(out, added, order);
	}
	return out;
}

Value unchangedOrNow(const QString &, const Value &now, const Value *) { return now; }

/* the register's fields: each one as written in `written` (its "fields" list) where it still is */
Value fieldsAsWritten(const RegDef &def, const Value *written) {
	Value out = Value::makeArray();
	const int count = written && written->isArray() ? int(written->items.size()) : 0;
	/* which written field each field is: its own if the name is the same, then any it still has */
	std::vector<int> claim(size_t(def.fields.size()), -1);
	std::vector<bool> used(size_t(count), false);
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < def.fields.size(); i++) {
			const int j = def.fields[i].source;
			if (claim[size_t(i)] >= 0 || j < 0 || j >= count || used[size_t(j)]) continue;
			const Value *name = written->items[size_t(j)].find(QStringLiteral("name"));
			if (pass == 0 && (!name || name->string != def.fields[i].name)) continue;
			claim[size_t(i)] = j;
			used[size_t(j)] = true;
		}
	}
	for (int i = 0; i < def.fields.size(); i++) {
		const Value now = fieldJson(def.fields[i]);
		if (claim[size_t(i)] < 0) {
			out.items.push_back(now);
			continue;
		}
		const Value &was = written->items[size_t(claim[size_t(i)])];
		const Value then = fieldJson(parseField(jsondoc::toQt(was).toObject()));
		out.items.push_back(keepAsWritten(&was, then, now, FIELD_KEYS, unchangedOrNow));
	}
	return out;
}

/* a register as close to how `written` had it as it can be */
Value registerAsWritten(const RegDef &def, const Value *written, const RegDef *loaded) {
	const Value now = registerJson(def);
	if (!written || !loaded) return now;
	return keepAsWritten(written, registerJson(*loaded), now, REGISTER_KEYS,
			[&def](const QString &key, const Value &nowValue, const Value *writtenValue) {
				if (key == QLatin1String("fields")) return fieldsAsWritten(def, writtenValue);
				return nowValue;
			});
}

/* An overlay's item for a register of its base: the address, and each key whose
 * value is not the base's (null: a key the base has and the register not any
 * more), in the order the overlay had them. Only the address: nothing changed. */
Value overlayDiff(const Value &full, const Value &base, const Value *written) {
	Value changed = Value::makeObject();
	for (const Member &m : full.members) {
		if (m.key == QLatin1String("addr")) continue;
		const Value *there = base.find(m.key);
		if (!there || !jsondoc::equal(*there, m.value)) changed.members.push_back(m);
	}
	for (const Member &m : base.members)
		if (full.indexOf(m.key) < 0) add(changed, m.key, Value());
	Value out = Value::makeObject();
	const Value *writtenAddr = written ? written->find(QStringLiteral("addr")) : nullptr;
	const Value *addr = full.find(QStringLiteral("addr"));
	add(out, QStringLiteral("addr"), writtenAddr && addr && jsondoc::equal(*writtenAddr, *addr) ? *writtenAddr
			: addr ? *addr : Value());
	/* the overlay's own order first (with its own text where the value is the same), then the rest */
	if (written) {
		for (const Member &m : written->members) {
			const Value *value = changed.find(m.key);
			if (m.key == QLatin1String("addr") || !value) continue;
			add(out, m.key, jsondoc::equal(m.value, *value) ? m.value : *value);
		}
	}
	for (const Member &m : changed.members)
		if (out.indexOf(m.key) < 0) out.members.push_back(m);
	return out;
}

/* which loaded register each register is: its own if the address is the same, then any it still has */
std::vector<int> claimSources(const QVector<RegDef> &regs, const MapSource &source) {
	std::vector<int> claim(size_t(regs.size()), -1);
	std::vector<bool> used(source.loaded.size(), false);
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < regs.size(); i++) {
			const int s = regs[i].source;
			if (claim[size_t(i)] >= 0 || s < 0 || s >= source.loaded.size() || used[size_t(s)]) continue;
			if (pass == 0 && regs[i].addr != source.loaded[s].addr) continue;
			claim[size_t(i)] = s;
			used[size_t(s)] = true;
		}
	}
	return claim;
}

jsondoc::Style styleOfList(const QByteArray &text, const Value *list, const jsondoc::Style &fallback, int &column) {
	jsondoc::Style style = fallback;
	if (list && !list->items.empty()) {
		style.pretty = jsondoc::isPrettyIn(text, list->items.front());
		column = jsondoc::columnOf(text, list->items.front().start);
	}
	return style;
}

/* the settings of a plain map (not an overlay), as its file had them */
Value settingsAsWritten(const DeviceMap &map, const MapSource &source) {
	return keepAsWritten(&source.doc, source.loadedSettings, settingsJson(map), SETTINGS_KEYS, unchangedOrNow);
}

/* an overlay's settings: what differs from the base, keeping what the overlay wrote */
Value overlaySettings(const DeviceMap &map, const MapSource &source) {
	const Value now = settingsJson(map);
	Value out = Value::makeObject();
	for (const Member &m : source.doc.members) {
		if (m.key == QLatin1String("registers") || m.key == QLatin1String("extends")) {
			out.members.push_back(m); /* filled in by the caller */
			continue;
		}
		const Value *was = source.loadedSettings.find(m.key), *is = now.find(m.key), *base = source.baseSettings.find(m.key);
		if (!SETTINGS_KEYS.contains(m.key) || sameOrBothMissing(was, is)) {
			out.members.push_back(m);
		} else if (is) {
			if (base && jsondoc::equal(*base, *is)) continue; /* back to the base's value */
			Member edited = m;
			edited.value = *is;
			out.members.push_back(edited);
		} else if (base) {
			Member removed = m;
			removed.value = Value(); /* null: the base's key removed */
			out.members.push_back(removed);
		}
	}
	for (const Member &m : now.members) {
		if (source.doc.indexOf(m.key) >= 0 || sameOrBothMissing(source.baseSettings.find(m.key), &m.value)) continue;
		insertInOrder(out, m, SETTINGS_KEYS);
	}
	for (const Member &m : source.baseSettings.members) {
		if (now.indexOf(m.key) >= 0 || out.indexOf(m.key) >= 0) continue;
		Member removed = m;
		removed.value = Value();
		insertInOrder(out, removed, SETTINGS_KEYS);
	}
	return out;
}

/* the path of `base` as written in an overlay saved as `file`: relative, with forward slashes */
QString extendsText(const QString &file, const QString &base) {
	return QFileInfo(file).absoluteDir().relativeFilePath(base);
}

} // namespace

bool DeviceMap::load(const QString &file, QString &err) {
	Resolved resolved;
	if (!resolveFile(file, 0, resolved, err)) return false;
	const QJsonObject root = jsondoc::toQt(resolved.flat).toObject();
	DeviceMap map; /* filled aside: a file that fails to load leaves this map as it was */
	if (!parseSettings(root, map, err)) return false;
	auto source = std::make_shared<MapSource>();
	const QJsonArray regs = root.value(QLatin1String("registers")).toArray();
	for (int i = 0; i < regs.size(); i++) {
		RegDef def;
		if (!parseRegister(regs[i].toObject(), def, err)) return false;
		def.source = i;
		map.regs.push_back(def);
	}
	source->loaded = map.regs;
	source->loadedSettings = settingsJson(map);
	if (!resolved.basePath.isEmpty()) {
		DeviceMap base;
		QString baseErr;
		parseSettings(jsondoc::toQt(resolved.base).toObject(), base, baseErr);
		source->baseSettings = settingsJson(base);
	}
	source->text = resolved.text;
	source->doc = resolved.doc;
	source->flat = resolved.flat;
	source->own = resolved.own;
	source->fromBase = resolved.fromBase;
	source->base = resolved.base;
	map.source = source;
	map.basePath = resolved.basePath;
	map.path = file;
	map.sort();
	*this = map;
	return true;
}

QByteArray DeviceMap::toJson(const QString &file, bool flatten) const {
	const jsondoc::Style fresh; /* pretty, two spaces: a new file */
	const MapSource *src = source.get();
	const std::vector<int> claim = src ? claimSources(regs, *src) : std::vector<int>(size_t(regs.size()), -1);
	const Value *flatRegs = src ? src->flat.find(QStringLiteral("registers")) : nullptr;
	/* a register as written: its own item, or for a copy the item it was copied from (the copy is a
	 * new item in the file, with the keys of the one it came from) */
	auto registerOf = [&](int i) {
		int s = claim[size_t(i)];
		if (s < 0 && src && regs[i].source < src->loaded.size()) s = regs[i].source;
		return s >= 0 && flatRegs ? registerAsWritten(regs[i], &flatRegs->items[size_t(s)], &src->loaded[s])
				: registerJson(regs[i]);
	};

	/* a new map, or an overlay made whole: written the Studio's way (with the keys it does not know) */
	if (!src || (flatten && isOverlay())) {
		Value root = src ? keepAsWritten(&src->flat, src->loadedSettings, settingsJson(*this), SETTINGS_KEYS,
				unchangedOrNow) : settingsJson(*this);
		root.remove(QStringLiteral("extends"));
		Value list = Value::makeArray();
		for (int i = 0; i < regs.size(); i++) list.items.push_back(registerOf(i));
		root.set(QStringLiteral("registers"), list);
		jsondoc::forgetSource(root);
		return jsondoc::render(root, fresh, 0) + "\n";
	}

	/* the file patched: the settings and registers that changed, the rest as it was */
	const QByteArray &text = src->text;
	const Value &doc = src->doc;
	const bool prettyRoot = jsondoc::isPrettyIn(text, doc);
	int rootColumn = doc.members.empty() ? 2 : jsondoc::columnOf(text, doc.members.front().keyStart);
	jsondoc::Style rootStyle{ prettyRoot, QByteArray(std::max(1, prettyRoot ? rootColumn : 2), ' '), 110 };
	const Value *ownRegs = doc.find(QStringLiteral("registers"));
	int listColumn = rootColumn + int(rootStyle.unit.size());
	const jsondoc::Style listStyle = styleOfList(text, ownRegs, rootStyle, listColumn);

	std::vector<jsondoc::OutItem> items;
	std::vector<uint16_t> itemAddr;
	if (!isOverlay()) {
		for (int i = 0; i < regs.size(); i++) {
			const int s = claim[size_t(i)];
			items.push_back({ QString(), registerOf(i), s >= 0 ? src->own[size_t(s)] : -1, {} });
			itemAddr.push_back(regs[i].addr);
		}
	} else {
		const Value *baseRegs = src->base.find(QStringLiteral("registers"));
		std::vector<bool> baseUsed(baseRegs ? baseRegs->items.size() : 0, false);
		for (int i = 0; i < regs.size(); i++) {
			const int s = claim[size_t(i)];
			const Value full = registerOf(i);
			const int own = s >= 0 ? src->own[size_t(s)] : -1;
			const int b = s >= 0 ? src->fromBase[size_t(s)] : -1;
			if (b >= 0 && baseRegs) {
				baseUsed[size_t(b)] = true;
				const Value *written = own >= 0 ? &ownRegs->items[size_t(own)] : nullptr;
				const Value diff = overlayDiff(full, baseRegs->items[size_t(b)], written);
				if (diff.members.size() <= 1) continue; /* as in the base */
				items.push_back({ QString(), diff, own, {} });
			} else {
				items.push_back({ QString(), full, own, {} });
			}
			itemAddr.push_back(regs[i].addr);
		}
		/* the base's registers this map no longer has: removed (as the overlay wrote it, if it did) */
		for (size_t b = 0; b < baseUsed.size(); b++) {
			if (baseUsed[b]) continue;
			const uint16_t addr = addressOf(baseRegs->items[b]);
			int written = -1;
			for (size_t j = 0; ownRegs && j < ownRegs->items.size() && written < 0; j++)
				if (isRemoval(ownRegs->items[j]) && addressOf(ownRegs->items[j]) == addr) written = int(j);
			Value removal = Value::makeObject();
			add(removal, QStringLiteral("addr"), str(addrText(addr)));
			add(removal, QStringLiteral("remove"), Value::makeBool(true));
			if (written >= 0) {
				const Value &was = ownRegs->items[size_t(written)];
				if (jsondoc::equal(was, removal)) removal = was;
			}
			items.push_back({ QString(), removal, written, {} });
			itemAddr.push_back(addr);
		}
		/* in address order, as the registers are */
		std::vector<size_t> order(items.size());
		for (size_t k = 0; k < order.size(); k++) order[k] = k;
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return itemAddr[a] < itemAddr[b]; });
		std::vector<jsondoc::OutItem> sorted;
		for (size_t k : order) sorted.push_back(items[k]);
		items = sorted;
	}

	Value settings = isOverlay() ? overlaySettings(*this, *src) : settingsAsWritten(*this, *src);
	if (isOverlay()) {
		/* "extends": as written while it names the same base from where the file is saved */
		const QString written = doc.find(QStringLiteral("extends")) ? doc.find(QStringLiteral("extends"))->string : QString();
		const QString base = QDir::cleanPath(QFileInfo(file).absoluteDir().absoluteFilePath(written));
		if (written.isEmpty() || base != QDir::cleanPath(basePath))
			settings.set(QStringLiteral("extends"), str(extendsText(file, basePath)));
	}
	std::vector<jsondoc::OutItem> rootItems;
	bool haveRegisters = false;
	for (const Member &m : settings.members) {
		jsondoc::OutItem item{ m.key, m.value, m.keyStart >= 0 ? doc.indexOf(m.key) : -1, {} };
		if (m.key == QLatin1String("registers")) {
			haveRegisters = true;
			item.rendered = ownRegs ? jsondoc::patchSequence(text, *ownRegs, items, listStyle, listColumn) : QByteArray();
		}
		rootItems.push_back(item);
	}
	if (!haveRegisters && !items.empty()) {
		Value list = Value::makeArray();
		for (const jsondoc::OutItem &item : items) list.items.push_back(item.value);
		rootItems.push_back({ QStringLiteral("registers"), list, -1, jsondoc::render(list, rootStyle, rootColumn, &text) });
	}
	return text.left(doc.start) + jsondoc::patchSequence(text, doc, rootItems, rootStyle, rootColumn) + text.mid(doc.end);
}

bool DeviceMap::save(const QString &file, QString &err, bool flatten) const {
	const QByteArray text = toJson(file, flatten);
	QFile output(file);
	if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		err = output.errorString();
		return false;
	}
	if (output.write(text) != text.size()) {
		err = output.errorString();
		return false;
	}
	return true;
}

void DeviceMap::sort() {
	std::stable_sort(regs.begin(), regs.end(), [](const RegDef &a, const RegDef &b) { return a.addr < b.addr; });
}

/* ------------------------------------------------------------ the clipboard */

QByteArray registersToJson(const QVector<RegDef> &regs) {
	Value list = Value::makeArray();
	for (const RegDef &def : regs) list.items.push_back(registerJson(def));
	return jsondoc::render(list, jsondoc::Style(), 0) + "\n";
}

bool registersFromJson(const QByteArray &text, QVector<RegDef> &regs, QString &err) {
	Value doc;
	if (!jsondoc::parse(text.trimmed(), doc, err)) return false;
	/* a list of registers, one register, or a whole map */
	const Value *list = doc.isObject() ? doc.find(QStringLiteral("registers")) : &doc;
	std::vector<Value> items;
	if (list && list->isArray()) items = list->items;
	else if (doc.isObject() && doc.find(QStringLiteral("addr"))) items.push_back(doc);
	else {
		err = QObject::tr("no registers in it");
		return false;
	}
	regs.clear();
	for (const Value &item : items) {
		if (!item.isObject()) {
			err = QObject::tr("an item is not a register");
			return false;
		}
		RegDef def;
		if (!parseRegister(jsondoc::toQt(item).toObject(), def, err)) return false;
		regs.push_back(def);
	}
	return true;
}

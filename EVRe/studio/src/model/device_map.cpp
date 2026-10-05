/* SPDX-License-Identifier: Apache-2.0 */
/* Register values: raw bytes <-> numbers and text, and how addresses are written.
 * The map file is read and written in map_file.cpp, checked in map_check.cpp. */
#include "model/device_map.h"

#include <QLocale>
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "evre/frame.h"

using evre::littleEndian16;
using evre::littleEndian32;

namespace {

/* "0x" and upper-case digits, zero-padded: 0x00FF */
QString hexText(quint64 value, int digits) {
	return QStringLiteral("0x") + QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

/* a number as short as it can be written: 3, -1, 0.05, 1e-06 */
QString shortNumber(double value) {
	if (value == std::floor(value) && std::fabs(value) < 1e15) return QString::number(qint64(value));
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

/* the values an integer type holds */
void integerRange(RegType type, double &lo, double &hi) {
	switch (type) {
	case RegType::U8: lo = 0; hi = 255; return;
	case RegType::I8: lo = -128; hi = 127; return;
	case RegType::U16: lo = 0; hi = 65535; return;
	case RegType::I16: lo = -32768; hi = 32767; return;
	case RegType::U32: lo = 0; hi = 4294967295.0; return;
	case RegType::I32: lo = -2147483648.0; hi = 2147483647.0; return;
	case RegType::F32: case RegType::Bytes: break;
	}
	lo = hi = 0;
}

/* a shown value equal to one the map names: exact for integers, to float precision for floats */
bool sameShown(double a, double b) {
	return a == b || std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b));
}

} // namespace

/* ---------------------------------------------------------------------- types */

int typeSize(RegType type) {
	switch (type) {
	case RegType::U8: case RegType::I8: return 1;
	case RegType::U16: case RegType::I16: return 2;
	case RegType::U32: case RegType::I32: case RegType::F32: return 4;
	case RegType::Bytes: return 0;
	}
	return 0;
}

QString typeName(RegType type) {
	switch (type) {
	case RegType::U8: return QStringLiteral("u8");
	case RegType::I8: return QStringLiteral("i8");
	case RegType::U16: return QStringLiteral("u16");
	case RegType::I16: return QStringLiteral("i16");
	case RegType::U32: return QStringLiteral("u32");
	case RegType::I32: return QStringLiteral("i32");
	case RegType::F32: return QStringLiteral("f32");
	case RegType::Bytes: return QStringLiteral("bytes");
	}
	return {};
}

/* the map's type names, and the C names (uint16_t, float ...) as well */
bool parseType(const QString &text, RegType &type) {
	static const QMap<QString, RegType> types{
		{ "u8", RegType::U8 }, { "i8", RegType::I8 }, { "u16", RegType::U16 }, { "i16", RegType::I16 },
		{ "u32", RegType::U32 }, { "i32", RegType::I32 }, { "f32", RegType::F32 }, { "bytes", RegType::Bytes },
		{ "uint8_t", RegType::U8 }, { "int8_t", RegType::I8 }, { "uint16_t", RegType::U16 },
		{ "int16_t", RegType::I16 }, { "uint32_t", RegType::U32 }, { "int32_t", RegType::I32 },
		{ "float", RegType::F32 },
	};
	const auto it = types.find(text.trimmed().toLower());
	if (it == types.end()) return false;
	type = *it;
	return true;
}

/* --------------------------------------------------------------------- values */

qint64 decodeRaw(const RegDef &def, const QByteArray &raw) {
	if (raw.size() < def.size || def.type == RegType::Bytes) return 0;
	const auto *bytes = reinterpret_cast<const uint8_t *>(raw.constData());
	switch (def.type) {
	case RegType::U8: return bytes[0];
	case RegType::I8: return int8_t(bytes[0]);
	case RegType::U16: return littleEndian16(bytes);
	case RegType::I16: return int16_t(littleEndian16(bytes));
	case RegType::U32: case RegType::F32: return littleEndian32(bytes); /* a float: its bits */
	case RegType::I32: return int32_t(littleEndian32(bytes));
	case RegType::Bytes: break;
	}
	return 0;
}

double decodeNumber(const RegDef &def, const QByteArray &raw) {
	if (raw.size() < def.size || def.type == RegType::Bytes) return NAN;
	double value;
	if (def.type == RegType::F32) {
		float number;
		std::memcpy(&number, raw.constData(), 4);
		value = number;
	} else {
		value = double(decodeRaw(def, raw));
	}
	return value * def.scale + def.offset;
}

QString formatNumber(const RegDef &def, double value) {
	if (std::isnan(value)) return QStringLiteral("NaN");
	if (!def.isFloat()) return QString::number(qint64(value));
	if (def.decimals >= 0) return QString::number(value, 'f', def.decimals);
	/* fewer decimals as the number grows: 1234.5, 123.45, 12.345, 0.1234 */
	const double magnitude = std::fabs(value);
	const int decimals = magnitude >= 1000 ? 1 : magnitude >= 100 ? 2 : magnitude >= 1 ? 3 : 4;
	return QString::number(value, 'f', decimals);
}

QString formatValue(const RegDef &def, const QByteArray &raw) {
	if (raw.size() < def.size) return {};
	if (def.type == RegType::Bytes) {
		/* the first 24 bytes: more would not fit the column */
		const QString shown = QString::fromLatin1(raw.left(24).toHex(' ').toUpper());
		return raw.size() > 24 ? shown + QStringLiteral(" …") : shown;
	}
	if (!def.isFloat()) {
		/* bits in hex, as many digits as the register has */
		if (def.hex || !def.fields.isEmpty() || def.unit == QLatin1String("bitmask")) {
			return hexText(quint64(decodeRaw(def, raw)) & bitMask(8 * def.size), def.size * 2);
		}
		return QString::number(decodeRaw(def, raw));
	}
	return formatNumber(def, decodeNumber(def, raw));
}

QString specialName(const RegDef &def, double shown) {
	for (const SpecialValue &special : def.special)
		if (sameShown(shown, special.value)) return special.name;
	return {};
}

QString formatDecoded(const RegDef &def, const QByteArray &raw) {
	if (raw.size() < def.size || def.type == RegType::Bytes) return {};
	if (!def.special.isEmpty()) {
		const QString name = specialName(def, decodeNumber(def, raw));
		if (!name.isEmpty()) return name;
	}
	const qint64 bits = decodeRaw(def, raw);
	if (!def.enumValues.isEmpty()) return def.enumValues.value(bits, QStringLiteral("? (%1)").arg(bits));
	if (def.fields.isEmpty()) return {};
	QStringList parts;
	for (const BitField &field : def.fields) {
		const qint64 value = qint64(quint64(bits >> field.lsb) & bitMask(field.width));
		if (field.width == 1 && field.values.isEmpty()) {
			if (value) parts << field.name; /* flags: only the ones that are set */
		} else {
			parts << QStringLiteral("%1=%2").arg(field.name, field.values.value(value, QString::number(value)));
		}
	}
	return parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral("  "));
}

QByteArray encodeLoginToken(const QString &token, int size) {
	if (size <= 0) return {};
	QByteArray bytes = token.toUtf8().left(size);
	bytes.append(QByteArray(size - int(bytes.size()), '\0'));
	return bytes;
}

bool encodeValue(const RegDef &def, const QString &text, QByteArray &out, QString &err) {
	const QString typed = text.trimmed();
	if (def.type == RegType::Bytes) {
		out = QByteArray::fromHex(typed.toLatin1());
		if (out.size() != def.size) {
			err = QObject::tr("%1 hex bytes needed, got %2").arg(def.size).arg(out.size());
			return false;
		}
		return true;
	}
	/* a special value's name: its shown value; an enum name: its number */
	for (const SpecialValue &special : def.special) {
		if (special.name.compare(typed, Qt::CaseInsensitive) == 0)
			return encodeValue(def, shortNumber(special.value), out, err);
	}
	for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it) {
		if (it.value().compare(typed, Qt::CaseInsensitive) == 0)
			return encodeValue(def, QString::number(it.key()), out, err);
	}
	/* 0x.. and 0b.. are the raw bits; a plain number is the value as shown,
	 * so scale and offset are undone */
	const bool hex = typed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive);
	const bool binary = typed.startsWith(QLatin1String("0b"), Qt::CaseInsensitive);
	bool ok = false;
	double value;
	if (hex || binary) {
		value = double(typed.mid(2).toULongLong(&ok, hex ? 16 : 2));
	} else {
		value = QLocale::c().toDouble(typed, &ok);
	}
	if (!ok) {
		err = QObject::tr("not a number: \"%1\"").arg(typed);
		return false;
	}
	/* NaN and the infinities are no value a device takes (EVRe Guard refuses them, 15): never sent */
	if (!std::isfinite(value)) {
		err = QObject::tr("not a finite number: \"%1\"").arg(typed);
		return false;
	}
	if (!hex && !binary) value = (value - def.offset) / (def.scale == 0 ? 1 : def.scale);
	out.resize(def.size);
	auto *bytes = reinterpret_cast<uint8_t *>(out.data());
	if (def.type == RegType::F32) {
		const float number = float(value);
		if (!std::isfinite(number)) {
			err = QObject::tr("%1 is past the largest f32 value").arg(typed);
			return false;
		}
		std::memcpy(bytes, &number, 4);
		return true;
	}
	const double rounded = std::round(value);
	double lo, hi;
	integerRange(def.type, lo, hi);
	if (rounded < lo || rounded > hi) {
		err = QObject::tr("%1 is out of range for %2 (%3 … %4)").arg(rounded).arg(typeName(def.type)).arg(lo).arg(hi);
		return false;
	}
	const quint64 bits = quint64(qint64(rounded));
	for (int i = 0; i < def.size; i++) bytes[i] = uint8_t(bits >> (8 * i));
	return true;
}

QString limitProblem(const RegDef &def, double shown) {
	if (std::isnan(shown) || !specialName(def, shown).isEmpty()) return {};
	const QString unit = def.unit.isEmpty() ? QString() : QStringLiteral(" ") + def.unit;
	auto limitText = [&](double limit) { return shortNumber(limit) + unit; };
	/* a float written back reads a little off: a hair past the limit is on it */
	if (def.hasMin() && shown < def.min && !sameShown(shown, def.min))
		return QObject::tr("below the minimum %1").arg(limitText(def.min));
	if (def.hasMax() && shown > def.max && !sameShown(shown, def.max))
		return QObject::tr("above the maximum %1").arg(limitText(def.max));
	return {};
}

QString writeLimitProblem(const RegDef &def, double shown) {
	return def.clamps ? QString() : limitProblem(def, shown);
}

bool hostWrites(const RegDef &def) {
	if (def.rw || !def.readable) return true;
	for (const BitField &field : def.fields)
		if (field.access == FieldAccess::ReadWrite || field.access == FieldAccess::WriteOneToClear) return true;
	return false;
}

/* ------------------------------------------------------------------ addresses */

QString addrText(uint16_t addr) { return hexText(addr, 4); }

QString accessText(const RegDef &def) {
	if (!def.readable) return QStringLiteral("wo");
	return def.rw ? QStringLiteral("rw") : QStringLiteral("ro");
}

uint parseAddress(const QString &text, bool *ok) {
	const QString trimmed = text.trimmed();
	return trimmed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? trimmed.mid(2).toUInt(ok, 16)
			: trimmed.toUInt(ok, 10);
}

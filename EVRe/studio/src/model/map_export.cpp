/* SPDX-License-Identifier: Apache-2.0 */
/* A map as Markdown, C, Python and CSV, and read back from CSV: see map_export.h. */
#include "model/map_export.h"

#include <QHash>
#include <QLocale>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

#include "evre/registers.h"

namespace {

/* ------------------------------------------------------------------ helpers */

QString number(double value) {
	if (value == std::floor(value) && std::fabs(value) < 1e15) return QString::number(qint64(value));
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

QString writeWord(WriteKind kind) {
	switch (kind) {
	case WriteKind::Normal: return QString();
	case WriteKind::Action: return QStringLiteral("action");
	case WriteKind::WriteOneToClear: return QStringLiteral("w1c");
	}
	return {};
}

QString fieldAccessWord(FieldAccess access) {
	switch (access) {
	case FieldAccess::AsRegister: return QString();
	case FieldAccess::ReadOnly: return QStringLiteral("ro");
	case FieldAccess::ReadWrite: return QStringLiteral("rw");
	case FieldAccess::WriteOneToClear: return QStringLiteral("w1c");
	}
	return {};
}

QString bitsText(const BitField &field) {
	return field.width == 1 ? QString::number(field.lsb)
			: QStringLiteral("%1:%2").arg(field.lsb + field.width - 1).arg(field.lsb);
}

QString keyText(qint64 key, bool hex) {
	return hex && key >= 0 ? QStringLiteral("0x") + QString::number(key, 16).toUpper() : QString::number(key);
}

/* the groups in the order they first come, each with its registers in address order */
QVector<QPair<QString, QVector<const RegDef *>>> byGroup(const DeviceMap &map) {
	QVector<QPair<QString, QVector<const RegDef *>>> groups;
	for (const RegDef &def : map.regs) {
		int at = -1;
		for (int i = 0; i < groups.size(); i++)
			if (groups[i].first == def.group) at = i;
		if (at < 0) {
			groups.push_back({ def.group, {} });
			at = int(groups.size()) - 1;
		}
		groups[at].second.push_back(&def);
	}
	return groups;
}

} // namespace

QString identifier(const QString &name) {
	QString out;
	bool underscore = false;
	for (const QChar c : name.trimmed()) {
		if (c.isLetterOrNumber() && c.unicode() < 128) {
			out += c.toUpper();
			underscore = false;
		} else if (!underscore && !out.isEmpty()) {
			out += QLatin1Char('_');
			underscore = true;
		}
	}
	while (out.endsWith(QLatin1Char('_'))) out.chop(1);
	if (out.isEmpty()) out = QStringLiteral("_");
	if (out.front().isDigit()) out.prepend(QLatin1Char('_'));
	return out;
}

/* ================================================================= Markdown */

namespace {

/* text in a table cell: no | and no line break to break the table */
QString cell(QString text) {
	return text.replace(QLatin1Char('|'), QStringLiteral("\\|")).replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

/* The fields across the bits, as a datasheet draws them; ASCII only, 16 bits a line:
 *
 *       15   14   13   12   11   10    9    8
 *     +----+----+----+----+----+----+----+----+
 *     |  PRESCALER             |  -   | RDY  |
 *     +----+----+----+----+----+----+----+----+
 *
 * A field is a box across its bits with its name in it (cut to fit); a bit in no field has "-". */
QString bitDiagram(const RegDef &def) {
	const int bits = 8 * def.size;
	const int perLine = std::min(16, bits);
	auto fieldAt = [&def](int bit) -> const BitField * {
		for (const BitField &f : def.fields)
			if (bit >= f.lsb && bit < f.lsb + f.width) return &f;
		return nullptr;
	};
	QString out;
	for (int high = bits - 1; high >= 0; high -= perLine) {
		const int low = std::max(0, high - perLine + 1);
		QString numbers = QStringLiteral(" "), border = QStringLiteral("+"), names = QStringLiteral("|");
		for (int bit = high; bit >= low; bit--) {
			numbers += QStringLiteral("%1 ").arg(bit, 4);
			border += QStringLiteral("----+");
		}
		for (int bit = high; bit >= low;) {
			const BitField *f = fieldAt(bit);
			const int end = f ? std::max(low, f->lsb) : bit;
			const int width = 5 * (bit - end + 1) - 1;
			QString label = f ? f->name : QStringLiteral("-");
			if (label.size() > width) label = label.left(width); /* cut to fit: the table under it has it in full */
			const int left = (width - int(label.size())) / 2;
			names += QString(left, QLatin1Char(' ')) + label + QString(width - left - int(label.size()), QLatin1Char(' '))
					+ QLatin1Char('|');
			bit = end - 1;
		}
		out += numbers + QLatin1Char('\n');
		out += border + QLatin1Char('\n') + names + QLatin1Char('\n') + border + QLatin1Char('\n');
	}
	return out;
}

void markdownRegister(QString &md, const RegDef &def) {
	md += QStringLiteral("### %1 (`%2`)\n\n").arg(def.name, addrText(def.addr));
	if (!def.desc.isEmpty()) md += def.desc + QStringLiteral("\n\n");
	/* the facts, only those it has */
	QStringList rows;
	auto row = [&rows](const QString &what, const QString &value) {
		rows << QStringLiteral("| %1 | %2 |").arg(what, cell(value));
	};
	row(QObject::tr("Type"), def.size == 1 ? QObject::tr("%1 (1 byte)").arg(typeName(def.type))
			: QObject::tr("%1 (%2 bytes, little endian)").arg(typeName(def.type)).arg(def.size));
	row(QObject::tr("Access"), !def.readable ? QObject::tr("write-only (never read)")
			: def.rw ? QObject::tr("read-write") : QObject::tr("read-only"));
	if (def.write == WriteKind::Action) row(QObject::tr("Write"), QObject::tr("action: a write does something, then it reads back idle"));
	if (def.write == WriteKind::WriteOneToClear) row(QObject::tr("Write"), QObject::tr("w1c: a 1 written to a bit clears it, a 0 leaves it"));
	if (def.persist) row(QObject::tr("Persist"), QObject::tr("kept across a reset (non-volatile)"));
	if (def.danger) row(QObject::tr("Danger"), QObject::tr("a write moves or powers something: hosts confirm it"));
	if (!def.plottable && def.isNumeric())
		row(QObject::tr("Plot"), QObject::tr("not plotted: a fixed value (an ID, a setting)"));
	if (!def.unit.isEmpty()) row(QObject::tr("Unit"), def.unit);
	if (def.scale != 1.0 || def.offset != 0.0)
		row(QObject::tr("Value"), QObject::tr("raw × %1 + %2").arg(number(def.scale), number(def.offset)));
	if (def.decimals >= 0) row(QObject::tr("Decimals"), QString::number(def.decimals));
	if (def.hasMin() || def.hasMax())
		row(QObject::tr("Range"), QStringLiteral("%1 … %2%3").arg(def.hasMin() ? number(def.min) : QStringLiteral("−"),
				def.hasMax() ? number(def.max) : QStringLiteral("+"), def.unit.isEmpty() ? QString() : QStringLiteral(" ") + def.unit));
	if (def.hasDefault()) row(QObject::tr("Default"), number(def.defaultValue) + (def.unit.isEmpty() ? QString() : QStringLiteral(" ") + def.unit)
			+ (def.persist ? QObject::tr(" (factory value)") : QString()));
	if (def.hex) row(QObject::tr("Shown"), QObject::tr("in hex"));
	md += QStringLiteral("| | |\n|---|---|\n") + rows.join(QLatin1Char('\n')) + QStringLiteral("\n\n");
	if (!def.notes.isEmpty()) md += def.notes.trimmed() + QStringLiteral("\n\n");

	if (!def.enumValues.isEmpty()) {
		md += QObject::tr("Values:") + QStringLiteral("\n\n| Value | Name |\n|---|---|\n");
		for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
			md += QStringLiteral("| %1 | %2 |\n").arg(keyText(it.key(), def.enumHex), cell(it.value()));
		md += QLatin1Char('\n');
	}
	if (!def.special.isEmpty()) {
		md += QObject::tr("Special values (in shown units; a write may always set them):")
				+ QStringLiteral("\n\n| Value | Meaning |\n|---|---|\n");
		for (const SpecialValue &special : def.special)
			md += QStringLiteral("| %1 | %2 |\n").arg(number(special.value), cell(special.name));
		md += QLatin1Char('\n');
	}
	if (!def.fields.isEmpty()) {
		md += QObject::tr("Bit fields:") + QStringLiteral("\n\n```\n") + bitDiagram(def) + QStringLiteral("```\n\n");
		md += QStringLiteral("| Bits | Name | Access | Description | Values |\n|---|---|---|---|---|\n");
		QVector<BitField> fields = def.fields;
		std::sort(fields.begin(), fields.end(), [](const BitField &a, const BitField &b) { return a.lsb > b.lsb; });
		for (const BitField &field : fields) {
			QStringList values;
			for (auto it = field.values.begin(); it != field.values.end(); ++it)
				values << QStringLiteral("%1 = %2").arg(keyText(it.key(), field.valuesHex), it.value());
			const QString access = fieldAccessWord(field.access);
			md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n").arg(bitsText(field), cell(field.name),
					access.isEmpty() ? QObject::tr("as the register") : access, cell(field.desc), cell(values.join(QStringLiteral(", "))));
		}
		md += QLatin1Char('\n');
	}
}

} // namespace

QByteArray exportMarkdown(const DeviceMap &map, const ExportOptions &options) {
	QString md = QStringLiteral("# %1\n\n").arg(map.device.isEmpty() ? QObject::tr("Register map") : map.device);
	md += QObject::tr("EVRe register map (%1)%2. Generated by EVRe Studio: change the map, not this file.")
			.arg(map.format, options.source.isEmpty() ? QString() : QObject::tr(", from `%1`").arg(options.source))
			+ QStringLiteral("\n\n");
	if (!map.desc.isEmpty()) md += map.desc + QStringLiteral("\n\n");
	if (!map.notes.isEmpty()) md += map.notes.trimmed() + QStringLiteral("\n\n");

	md += QObject::tr("## The device and the link") + QStringLiteral("\n\n| | |\n|---|---|\n");
	if (map.deviceId) md += QObject::tr("| Device ID | `%1` (register `0xA000`) |\n").arg(addrText(map.deviceId));
	md += QObject::tr("| Slave address | %1 |\n").arg(map.slave);
	const MapProtocol &p = map.protocol;
	if (!p.transport.isEmpty()) md += QObject::tr("| Transport | %1 |\n").arg(cell(p.transport));
	if (p.baud) md += QObject::tr("| Baud rate | %1 |\n").arg(p.baud);
	if (p.tcpPort) md += QObject::tr("| TCP port | %1 |\n").arg(p.tcpPort);
	if (p.timeoutMs) md += QObject::tr("| Answer timeout | %1 ms |\n").arg(p.timeoutMs);
	md += QObject::tr("| Byte order | little endian |\n");
	if (map.loginAddr)
		md += QObject::tr("| Login | after connecting, a token of %1 bytes (UTF-8, zero-padded) is written to `%2` |\n")
				.arg(map.loginSize).arg(addrText(map.loginAddr));
	if (map.usbVid || map.usbPid)
		md += QObject::tr("| USB | vendor `%1`, product `%2` |\n").arg(addrText(map.usbVid), addrText(map.usbPid));
	md += QLatin1Char('\n');
	if (!p.notes.isEmpty()) md += p.notes.trimmed() + QStringLiteral("\n\n");

	md += QObject::tr("## Register summary") + QStringLiteral("\n\n| Address | Name | Type | Unit | Access | Group | Description |\n"
			"|---|---|---|---|---|---|---|\n");
	for (const RegDef &def : map.regs)
		md += QStringLiteral("| `%1` | %2 | %3 | %4 | %5 | %6 | %7 |\n").arg(addrText(def.addr), cell(def.name),
				typeName(def.type), cell(def.unit), accessText(def) + (def.danger ? QStringLiteral(" ⚠") : QString()),
				cell(def.group), cell(def.desc));
	md += QLatin1Char('\n');

	for (const auto &group : byGroup(map)) {
		md += QStringLiteral("## %1\n\n").arg(group.first);
		if (map.groupNotes.contains(group.first)) md += map.groupNotes.value(group.first).trimmed() + QStringLiteral("\n\n");
		for (const RegDef *def : group.second) markdownRegister(md, *def);
	}

	/* the fast streams: where their blocks come from, how fast, what a record holds (byte by byte) */
	if (!map.streams.isEmpty()) {
		md += QObject::tr("## Fast streams") + QStringLiteral("\n\n");
		md += QObject::tr("Fast EVRe (PROTOCOL.md, \"Fast EVRe\"): the device sends each stream's samples by itself, in "
				"numbered blocks: READ_RESP frames nobody asked for, at the first address of the stream's window. A block "
				"is an 8-byte header (the first record's number u32, the records' count u16, flags u8: START 0x01, LOST "
				"0x02, a spare byte 0) and its records, all channels of one instant together, little endian.")
				+ QStringLiteral("\n\n");
		for (const StreamDef &stream : map.streams) {
			md += QStringLiteral("### %1\n\n").arg(stream.name);
			if (!stream.desc.isEmpty()) md += stream.desc + QStringLiteral("\n\n");
			md += QStringLiteral("| | |\n|---|---|\n");
			md += QObject::tr("| Window | `%1`, %2 bytes |\n").arg(addrText(stream.addr)).arg(stream.size);
			md += QObject::tr("| Rate | %1 records a second |\n").arg(number(stream.rate));
			md += QObject::tr("| Record | %1 bytes; at most %2 records a block |\n").arg(stream.recordSize()).arg(stream.recordsPerBlock());
			if (!stream.enable.isEmpty()) md += QObject::tr("| Enable | `%1`: 1 starts the stream, 0 stops it |\n").arg(stream.enable);
			if (!stream.rateReg.isEmpty()) md += QObject::tr("| Rate register | `%1`: the rate now |\n").arg(stream.rateReg);
			if (!stream.group.isEmpty()) md += QObject::tr("| Group | %1 |\n").arg(cell(stream.group));
			md += QLatin1Char('\n');
			md += QObject::tr("| Byte | Channel | Type | Unit | Scale | Offset | Description |") + QStringLiteral("\n|---|---|---|---|---|---|---|\n");
			int offset = 0;
			for (const StreamChannel &channel : stream.channels) {
				md += QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 |\n").arg(offset).arg(cell(channel.name), typeName(channel.type),
						cell(channel.unit), number(channel.scale), number(channel.offset), cell(channel.desc));
				offset += typeSize(channel.type);
			}
			md += QLatin1Char('\n');
			if (!stream.notes.isEmpty()) md += stream.notes.trimmed() + QStringLiteral("\n\n");
		}
	}
	return md.toUtf8();
}

/* ================================================================ C and Python */

namespace {

/* names defined once each: a second one of a name gets _2, _3 ... */
class Names {
public:
	QString take(const QString &name) {
		QString out = name;
		for (int n = 2; used_.contains(out); n++) out = QStringLiteral("%1_%2").arg(name).arg(n);
		used_.insert(out);
		return out;
	}

private:
	QSet<QString> used_;
};

/* a C literal of a shown value: an integer for a register shown as an integer, else a double
 * (a float, with its f, for an f32 register); a negative one in parentheses, as a macro should be */
QString cNumber(const RegDef &def, double value) {
	QString text;
	if (!def.isFloat() && value == std::floor(value) && std::fabs(value) < 1e15) {
		text = QString::number(qint64(value));
	} else {
		const bool single = def.type == RegType::F32;
		text = QString::number(single ? double(float(value)) : value, 'g', single ? 9 : 17);
		if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e'))) text += QStringLiteral(".0");
		if (single) text += QLatin1Char('f');
	}
	return value < 0 ? QStringLiteral("(%1)").arg(text) : text;
}

QString cComment(QString text) { return text.replace(QStringLiteral("*/"), QStringLiteral("* /")).replace(QLatin1Char('\n'), QLatin1Char(' ')); }

/* a Python string literal */
QString py(const QString &text) {
	QString out = QStringLiteral("\"");
	for (const QChar c : text) {
		if (c == QLatin1Char('"') || c == QLatin1Char('\\')) out += QLatin1Char('\\') + QString(c);
		else if (c == QLatin1Char('\n')) out += QStringLiteral("\\n");
		else if (c.unicode() < 0x20) out += QStringLiteral("\\x%1").arg(int(c.unicode()), 2, 16, QLatin1Char('0'));
		else out += c;
	}
	return out + QLatin1Char('"');
}

QString pyNumber(double value) {
	if (std::isnan(value)) return QStringLiteral("None");
	return number(value);
}

} // namespace

QByteArray exportCHeader(const DeviceMap &map, const ExportOptions &options) {
	const QString prefix = options.prefix.isEmpty() ? QString() : identifier(options.prefix) + QLatin1Char('_');
	const QString guard = prefix + identifier(map.device.isEmpty() ? QStringLiteral("evre map") : map.device) + QStringLiteral("_MAP_H");
	Names names;
	QString h = QStringLiteral("/* %1: EVRe register map (%2)%3.\n * Generated by EVRe Studio: change the map, not this file.\n"
			" * Addresses and sizes in bytes, little endian. MIN, MAX and DEFAULT are in shown units\n"
			" * (raw x scale + offset). */\n#ifndef %4\n#define %4\n\n")
			.arg(cComment(map.device.isEmpty() ? QStringLiteral("Device") : map.device), map.format,
					options.source.isEmpty() ? QString() : QStringLiteral(", from %1").arg(cComment(options.source)), guard);
	if (map.deviceId) h += QStringLiteral("#define %1 0x%2u\n").arg(names.take(prefix + QStringLiteral("DEVICE_ID")))
			.arg(map.deviceId, 4, 16, QLatin1Char('0'));
	if (map.loginAddr) {
		h += QStringLiteral("#define %1 0x%2u\n").arg(names.take(prefix + QStringLiteral("LOGIN_ADDR"))).arg(map.loginAddr, 4, 16, QLatin1Char('0'));
		h += QStringLiteral("#define %1 %2u\n").arg(names.take(prefix + QStringLiteral("LOGIN_SIZE"))).arg(map.loginSize);
	}
	for (const auto &group : byGroup(map)) {
		h += QStringLiteral("\n/* ---- %1 */\n").arg(cComment(group.first));
		for (const RegDef *defp : group.second) {
			const RegDef &def = *defp;
			const QString base = prefix + identifier(def.name);
			QStringList facts{ typeName(def.type), accessText(def) };
			if (!def.unit.isEmpty()) facts << def.unit;
			if (def.write != WriteKind::Normal) facts << writeWord(def.write);
			if (def.persist) facts << QStringLiteral("persist");
			if (def.danger) facts << QStringLiteral("danger");
			if (def.scale != 1.0 || def.offset != 0.0) facts << QStringLiteral("x%1 %2").arg(number(def.scale), number(def.offset));
			h += QStringLiteral("\n/* %1: %2 (%3) */\n").arg(cComment(def.name), cComment(def.desc.isEmpty() ? QStringLiteral("-") : def.desc),
					cComment(facts.join(QStringLiteral(", "))));
			h += QStringLiteral("#define %1 0x%2u\n").arg(names.take(base + QStringLiteral("_ADDR"))).arg(def.addr, 4, 16, QLatin1Char('0'));
			h += QStringLiteral("#define %1 %2u\n").arg(names.take(base + QStringLiteral("_SIZE"))).arg(def.size);
			if (def.hasMin()) h += QStringLiteral("#define %1 %2\n").arg(names.take(base + QStringLiteral("_MIN")), cNumber(def, def.min));
			if (def.hasMax()) h += QStringLiteral("#define %1 %2\n").arg(names.take(base + QStringLiteral("_MAX")), cNumber(def, def.max));
			if (def.hasDefault())
				h += QStringLiteral("#define %1 %2\n").arg(names.take(base + QStringLiteral("_DEFAULT")), cNumber(def, def.defaultValue));
			for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
				h += QStringLiteral("#define %1 %2\n").arg(names.take(base + QLatin1Char('_') + identifier(it.value())),
						it.key() < 0 ? QStringLiteral("(%1)").arg(it.key()) : QString::number(it.key()) + QLatin1Char('u'));
			for (const SpecialValue &special : def.special)
				h += QStringLiteral("#define %1 %2 /* special */\n").arg(names.take(base + QLatin1Char('_') + identifier(special.name)),
						cNumber(def, special.value));
			for (const BitField &field : def.fields) {
				const QString f = base + QLatin1Char('_') + identifier(field.name);
				const quint64 mask = bitMask(field.width) << field.lsb;
				h += QStringLiteral("#define %1 %2u /* bits %3%4 */\n").arg(names.take(f + QStringLiteral("_POS"))).arg(field.lsb)
						.arg(bitsText(field), field.desc.isEmpty() ? QString() : QStringLiteral(": ") + cComment(field.desc));
				h += QStringLiteral("#define %1 0x%2u\n").arg(names.take(f + QStringLiteral("_MSK"))).arg(mask, 0, 16);
				for (auto it = field.values.begin(); it != field.values.end(); ++it)
					h += QStringLiteral("#define %1 %2u\n").arg(names.take(f + QLatin1Char('_') + identifier(it.value()))).arg(it.key());
			}
		}
	}
	/* the fast streams: the window, the rate, a record's size and each channel's byte in it */
	for (const StreamDef &stream : map.streams) {
		const QString base = prefix + identifier(stream.name) + QStringLiteral("_FAST"); /* not _STREAM: its enable register's name */
		h += QStringLiteral("\n/* ---- fast stream %1: %2 (Fast EVRe: blocks of an 8-byte header, then records) */\n")
				.arg(cComment(stream.name), cComment(stream.desc.isEmpty() ? QStringLiteral("-") : stream.desc));
		h += QStringLiteral("#define %1 0x%2u\n").arg(names.take(base + QStringLiteral("_ADDR"))).arg(stream.addr, 4, 16, QLatin1Char('0'));
		h += QStringLiteral("#define %1 %2u\n").arg(names.take(base + QStringLiteral("_SIZE"))).arg(stream.size);
		h += QStringLiteral("#define %1 %2u /* records a second */\n").arg(names.take(base + QStringLiteral("_RATE")))
				.arg(qint64(std::llround(stream.rate)));
		h += QStringLiteral("#define %1 %2u\n").arg(names.take(base + QStringLiteral("_RECORD_SIZE"))).arg(stream.recordSize());
		h += QStringLiteral("#define %1 %2u\n").arg(names.take(base + QStringLiteral("_RECORDS_PER_BLOCK"))).arg(stream.recordsPerBlock());
		int offset = 0;
		for (const StreamChannel &channel : stream.channels) {
			QStringList facts{ typeName(channel.type) };
			if (!channel.unit.isEmpty()) facts << channel.unit;
			if (channel.scale != 1.0 || channel.offset != 0.0)
				facts << QStringLiteral("x%1 %2").arg(number(channel.scale), number(channel.offset));
			h += QStringLiteral("#define %1 %2u /* %3 */\n").arg(names.take(prefix + identifier(stream.name) + QLatin1Char('_')
					+ identifier(channel.name) + QStringLiteral("_OFFSET"))).arg(offset).arg(cComment(facts.join(QStringLiteral(", "))));
			offset += typeSize(channel.type);
		}
	}
	h += QStringLiteral("\n#endif /* %1 */\n").arg(guard);
	return h.toUtf8();
}

QByteArray exportPython(const DeviceMap &map, const ExportOptions &options) {
	const QString prefix = options.prefix.isEmpty() ? QString() : identifier(options.prefix) + QLatin1Char('_');
	Names names;
	QString s = QStringLiteral("\"\"\"%1: EVRe register map (%2)%3.\n\nGenerated by EVRe Studio: change the map, not this file.\n"
			"Addresses and sizes in bytes, little endian; min, max and default in shown units.\n\"\"\"\n\n")
			.arg(QString(map.device).replace(QStringLiteral("\"\"\""), QStringLiteral("'''")), map.format,
					options.source.isEmpty() ? QString() : QStringLiteral(", from %1").arg(options.source));
	s += QStringLiteral("DEVICE = %1\n").arg(py(map.device));
	s += QStringLiteral("DEVICE_ID = %1\n").arg(map.deviceId ? QStringLiteral("0x%1").arg(map.deviceId, 4, 16, QLatin1Char('0')) : QStringLiteral("None"));
	s += QStringLiteral("SLAVE = %1\n").arg(map.slave);
	s += QStringLiteral("LOGIN = %1\n\n").arg(map.loginAddr ? QStringLiteral("(0x%1, %2)").arg(map.loginAddr, 4, 16, QLatin1Char('0')).arg(map.loginSize)
			: QStringLiteral("None"));
	for (const RegDef &def : map.regs)
		s += QStringLiteral("%1 = 0x%2\n").arg(names.take(prefix + identifier(def.name))).arg(def.addr, 4, 16, QLatin1Char('0'));
	s += QStringLiteral("\n# every register: its definition as the map gives it\nREGISTERS = {\n");
	for (const RegDef &def : map.regs) {
		QStringList items{ QStringLiteral("\"addr\": 0x%1").arg(def.addr, 4, 16, QLatin1Char('0')),
			QStringLiteral("\"type\": %1").arg(py(typeName(def.type))), QStringLiteral("\"size\": %1").arg(def.size),
			QStringLiteral("\"access\": %1").arg(py(accessText(def))), QStringLiteral("\"group\": %1").arg(py(def.group)) };
		if (!def.unit.isEmpty()) items << QStringLiteral("\"unit\": %1").arg(py(def.unit));
		if (!def.desc.isEmpty()) items << QStringLiteral("\"desc\": %1").arg(py(def.desc));
		if (def.write != WriteKind::Normal) items << QStringLiteral("\"write\": %1").arg(py(writeWord(def.write)));
		if (def.persist) items << QStringLiteral("\"persist\": True");
		if (def.danger) items << QStringLiteral("\"danger\": True");
		if (!def.plottable) items << QStringLiteral("\"plot\": False");
		if (def.scale != 1.0) items << QStringLiteral("\"scale\": %1").arg(number(def.scale));
		if (def.offset != 0.0) items << QStringLiteral("\"offset\": %1").arg(number(def.offset));
		if (def.hasMin()) items << QStringLiteral("\"min\": %1").arg(pyNumber(def.min));
		if (def.hasMax()) items << QStringLiteral("\"max\": %1").arg(pyNumber(def.max));
		if (def.hasDefault()) items << QStringLiteral("\"default\": %1").arg(pyNumber(def.defaultValue));
		if (!def.enumValues.isEmpty()) {
			QStringList enumItems;
			for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
				enumItems << QStringLiteral("%1: %2").arg(it.key()).arg(py(it.value()));
			items << QStringLiteral("\"enum\": {%1}").arg(enumItems.join(QStringLiteral(", ")));
		}
		if (!def.special.isEmpty()) {
			QStringList special;
			for (const SpecialValue &value : def.special) special << QStringLiteral("%1: %2").arg(pyNumber(value.value), py(value.name));
			items << QStringLiteral("\"special\": {%1}").arg(special.join(QStringLiteral(", ")));
		}
		if (!def.fields.isEmpty()) {
			QStringList fields;
			for (const BitField &field : def.fields) {
				QStringList values;
				for (auto it = field.values.begin(); it != field.values.end(); ++it) values << QStringLiteral("%1: %2").arg(it.key()).arg(py(it.value()));
				fields << QStringLiteral("{\"name\": %1, \"lsb\": %2, \"width\": %3%4}").arg(py(field.name)).arg(field.lsb).arg(field.width)
						.arg(values.isEmpty() ? QString() : QStringLiteral(", \"values\": {%1}").arg(values.join(QStringLiteral(", "))));
			}
			items << QStringLiteral("\"fields\": [%1]").arg(fields.join(QStringLiteral(", ")));
		}
		s += QStringLiteral("    %1: {%2},\n").arg(py(def.name), items.join(QStringLiteral(", ")));
	}
	s += QStringLiteral("}\n");
	/* the fast streams, as the map gives them; record_size and each channel's byte ("at") in a record */
	s += QStringLiteral("\n# every fast stream (Fast EVRe): its window, rate and the channels of one record\nSTREAMS = {\n");
	for (const StreamDef &stream : map.streams) {
		QStringList items{ QStringLiteral("\"addr\": 0x%1").arg(stream.addr, 4, 16, QLatin1Char('0')),
			QStringLiteral("\"size\": %1").arg(stream.size), QStringLiteral("\"rate\": %1").arg(number(stream.rate)),
			QStringLiteral("\"record_size\": %1").arg(stream.recordSize()) };
		if (!stream.enable.isEmpty()) items << QStringLiteral("\"enable\": %1").arg(py(stream.enable));
		if (!stream.rateReg.isEmpty()) items << QStringLiteral("\"rate_reg\": %1").arg(py(stream.rateReg));
		if (!stream.desc.isEmpty()) items << QStringLiteral("\"desc\": %1").arg(py(stream.desc));
		QStringList channels;
		int offset = 0;
		for (const StreamChannel &channel : stream.channels) {
			QStringList c{ QStringLiteral("\"name\": %1").arg(py(channel.name)), QStringLiteral("\"type\": %1").arg(py(typeName(channel.type))),
				QStringLiteral("\"at\": %1").arg(offset) };
			if (!channel.unit.isEmpty()) c << QStringLiteral("\"unit\": %1").arg(py(channel.unit));
			if (channel.scale != 1.0) c << QStringLiteral("\"scale\": %1").arg(number(channel.scale));
			if (channel.offset != 0.0) c << QStringLiteral("\"offset\": %1").arg(number(channel.offset));
			channels << QStringLiteral("{%1}").arg(c.join(QStringLiteral(", ")));
			offset += typeSize(channel.type);
		}
		items << QStringLiteral("\"channels\": [%1]").arg(channels.join(QStringLiteral(", ")));
		s += QStringLiteral("    %1: {%2},\n").arg(py(stream.name), items.join(QStringLiteral(", ")));
	}
	s += QStringLiteral("}\n");
	return s.toUtf8();
}

/* ============================================================= device table */

namespace {

constexpr uint16_t RESERVED_FIRST = evre::RESERVED_FIRST, RESERVED_LAST = evre::RESERVED_END - 1; /* the library's own bank */
constexpr uint16_t BANK_FIRST = evre::READ_ONLY_BLOCK, BANK_LAST = 0xDFFF; /* the device's, D000[addr & 0x0FFF] */

/* written by the host: the library can only have it at or above DEVICE_REG_WRITE_MIN */
bool hostWrites(const RegDef &def) {
	if (def.rw || !def.readable) return true;
	for (const BitField &field : def.fields)
		if (field.access == FieldAccess::ReadWrite || field.access == FieldAccess::WriteOneToClear) return true;
	return false;
}

QString cType(RegType type) {
	switch (type) {
	case RegType::U8: return QStringLiteral("uint8_t");
	case RegType::I8: return QStringLiteral("int8_t");
	case RegType::U16: return QStringLiteral("uint16_t");
	case RegType::I16: return QStringLiteral("int16_t");
	case RegType::U32: return QStringLiteral("uint32_t");
	case RegType::I32: return QStringLiteral("int32_t");
	case RegType::F32: return QStringLiteral("float");
	case RegType::Bytes: return QStringLiteral("uint8_t");
	}
	return {};
}

/* what the raw of an integer register can hold: a limit out there needs no check */
void rawRange(RegType type, double &low, double &high) {
	const double inf = std::numeric_limits<double>::infinity();
	switch (type) {
	case RegType::U8: low = 0; high = 255; break;
	case RegType::I8: low = -128; high = 127; break;
	case RegType::U16: low = 0; high = 65535; break;
	case RegType::I16: low = -32768; high = 32767; break;
	case RegType::U32: low = 0; high = 4294967295.0; break;
	case RegType::I32: low = -2147483648.0; high = 2147483647.0; break;
	default: low = -inf; high = inf; break;
	}
}

/* a shown value as the raw one the image holds */
double rawOf(const RegDef &def, double shown) { return (shown - def.offset) / def.scale; }

/* a raw value as a literal of the register's C++ type */
QString rawLiteral(const RegDef &def, double raw) {
	if (def.type == RegType::F32) {
		QString text = QString::number(double(float(raw)), 'g', 9);
		if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e')) && !text.contains(QLatin1Char('n')))
			text += QStringLiteral(".0");
		return text + QLatin1Char('f');
	}
	const qint64 value = qint64(std::llround(raw));
	if (def.type == RegType::U32) return QString::number(value) + QLatin1Char('u');
	if (value == -2147483648LL) return QStringLiteral("(-2147483647 - 1)");
	return QString::number(value);
}

/* a struct member's name: the register's, lower case, never a C++ keyword */
QString memberName(const QString &name) {
	static const QSet<QString> keywords{ "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool",
		"break", "case", "catch", "char", "char16_t", "char32_t", "class", "compl", "const", "constexpr", "const_cast",
		"continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
		"extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new",
		"noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
		"reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
		"switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union",
		"unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq" };
	QString out = identifier(name).toLower();
	if (keywords.contains(out) || out.startsWith(QLatin1String("_gap"))) out += QLatin1Char('_');
	return out;
}

/* "  what   NAME, NAME, ..." wrapped under itself, for the header's comment */
QString listLine(const QString &what, const QStringList &names) {
	const QString head = QStringLiteral(" *   %1").arg(what, -12);
	QString out = head, line = head;
	for (int i = 0; i < names.size(); i++) {
		const QString item = names[i] + (i + 1 < names.size() ? QStringLiteral(",") : QString());
		if (line.size() > head.size() && line.size() + 1 + item.size() > 100) {
			out += QLatin1Char('\n');
			line = QStringLiteral(" *") + QString(head.size() - 2, QLatin1Char(' '));
			out += line;
		} else if (line.size() > head.size()) {
			out += QLatin1Char(' ');
			line += QLatin1Char(' ');
		}
		out += item;
		line += item;
	}
	return out + QLatin1Char('\n');
}

} // namespace

bool exportDeviceTable(const DeviceMap &map, const ExportOptions &options, QByteArray &out, QStringList &problems) {
	problems.clear();
	/* the device bank's registers in address order; the protocol bank is the library's own */
	QVector<const RegDef *> regs;
	for (const RegDef &def : map.regs) {
		if (def.addr >= RESERVED_FIRST && def.addr <= RESERVED_LAST) continue;
		if (def.addr < BANK_FIRST)
			problems << QObject::tr("%1 (%2): outside 0xD000..0xDFFF, the only device bank the EVRe library serves")
					.arg(def.name, addrText(def.addr));
		else if (int(def.addr) + def.size - 1 > BANK_LAST)
			problems << QObject::tr("%1 (%2): runs past 0xDFFF, the end of the device bank").arg(def.name, addrText(def.addr));
		else regs << &def;
	}
	std::stable_sort(regs.begin(), regs.end(), [](const RegDef *a, const RegDef *b) { return a->addr < b->addr; });
	if (regs.isEmpty() && problems.isEmpty()) problems << QObject::tr("no register in the device bank 0xD000..0xDFFF");
	const RegDef *firstWritable = nullptr;
	QStringList late; /* read-only above the first writable one */
	for (int i = 0; i < regs.size(); i++) {
		const RegDef &def = *regs[i];
		if (i && def.addr < regs[i - 1]->addr + regs[i - 1]->size)
			problems << QObject::tr("%1 (%2) overlaps %3 (%4)").arg(def.name, addrText(def.addr), regs[i - 1]->name,
					addrText(regs[i - 1]->addr));
		if (hostWrites(def)) {
			if (!firstWritable) firstWritable = &def;
		} else if (firstWritable) {
			late << QStringLiteral("%1 (%2)").arg(def.name, addrText(def.addr));
		}
	}
	if (late.size() == 1)
		problems << QObject::tr("%1 is read-only but comes after %2 (%3), the first writable register: the EVRe library "
				"makes everything from DEVICE_REG_WRITE_MIN on writable. Move it below %3, or the writable registers above it.")
				.arg(late.first(), firstWritable->name, addrText(firstWritable->addr));
	else if (!late.isEmpty())
		problems << QObject::tr("%1 read-only registers come after %2 (%3), the first writable register: %4. The EVRe "
				"library makes everything from DEVICE_REG_WRITE_MIN on writable. Move them below %3, or the writable "
				"registers above them.").arg(late.size()).arg(firstWritable->name, addrText(firstWritable->addr),
						late.join(QStringLiteral(", ")));
	if (!problems.isEmpty()) return false;

	const QString upper = identifier(!options.prefix.isEmpty() ? options.prefix
			: map.device.isEmpty() ? QStringLiteral("device") : map.device);
	const QString lower = upper.toLower();
	const QString roType = lower + QStringLiteral("_ro_t"), rwType = lower + QStringLiteral("_rw_t");
	const uint16_t readMax = uint16_t(regs.last()->addr + regs.last()->size - 1);
	const uint16_t writeMin = firstWritable ? firstWritable->addr : uint16_t(readMax + 1);
	const int roSize = writeMin - BANK_FIRST, rwSize = readMax + 1 - writeMin;
	const auto hex = [](uint32_t value) { return QStringLiteral("0x%1u").arg(value, 0, 16); };

	/* what the map asks and the library leaves to the device, for the comment on top */
	QStringList limited, actions, w1c, roBits, writeOnly, persist;
	for (const RegDef *defp : regs) {
		const RegDef &def = *defp;
		const bool writes = hostWrites(def);
		if (writes && def.isNumeric() && def.write != WriteKind::WriteOneToClear && (def.hasMin() || def.hasMax()))
			limited << def.name;
		if (def.write == WriteKind::Action) actions << def.name;
		if (def.write == WriteKind::WriteOneToClear) w1c << def.name;
		for (const BitField &field : def.fields) {
			const QString name = def.name + QLatin1Char('.') + field.name;
			if (field.access == FieldAccess::WriteOneToClear && def.write != WriteKind::WriteOneToClear) w1c << name;
			if (field.access == FieldAccess::ReadOnly && writes) roBits << name;
		}
		if (!def.readable) writeOnly << def.name;
		if (def.persist) persist << def.name;
	}

	QString h = QStringLiteral("/* %1: the device side of its EVRe register map (%2)%3.\n"
			" * Generated by EVRe Studio: change the map, not this file.\n *\n")
			.arg(cComment(map.device.isEmpty() ? QStringLiteral("Device") : map.device), map.format,
					options.source.isEmpty() ? QString() : QStringLiteral(", from %1").arg(cComment(options.source)));
	h += QStringLiteral(" * For the EVRe device library (lib/EVRe.h). The device bank %1..%2 is served from\n")
			.arg(addrText(BANK_FIRST), addrText(readMax));
	if (roSize && rwSize)
		h += QStringLiteral(" * two images in address order, the read-only one (%1..%2), then the read-write one\n"
				" * (%3..%4): the library's permission is one boundary, DEVICE_REG_WRITE_MIN.\n")
				.arg(addrText(BANK_FIRST), addrText(uint16_t(writeMin - 1)), addrText(writeMin), addrText(readMax));
	else
		h += QStringLiteral(" * one image, %1: the map has no %2 register.\n")
				.arg(roSize ? QStringLiteral("read-only") : QStringLiteral("read-write"), roSize ? QStringLiteral("writable")
						: QStringLiteral("read-only"));
	h += QStringLiteral(" * The images hold the values as they go on the wire (raw, little endian): where a register has a\n"
			" * scale, shown = raw x scale + offset. A gap between registers reads 0.\n *\n"
			" * In one source file of the device:\n *\n");
	QStringList params, args;
	if (roSize) {
		h += QStringLiteral(" *     %1 ro;  // the device keeps it up to date\n").arg(roType);
		params << roType + QStringLiteral(" *ro");
		args << QStringLiteral("&ro");
	}
	if (rwSize) {
		h += QStringLiteral(" *     %1 rw;  // the host writes it; it starts at the map's defaults\n").arg(rwType);
		params << rwType + QStringLiteral(" *rw");
		args << QStringLiteral("&rw");
	}
	const QString call = QStringLiteral("%1_bind(dev, %2)").arg(lower, args.join(QStringLiteral(", ")));
	h += QStringLiteral(" *     uint8_t protocolConfigure(base_t *dev) { return %1; }\n *\n").arg(call);
	h += QStringLiteral(" * or call %1_bind() after protocolInit().\n").arg(lower);
	if (rwSize)
		h += QStringLiteral(" * A host write lands in rw at once: compare rw with a copy after each packet to see what\n"
				" * changed, and apply it from there.\n");
	if (!limited.isEmpty() || !actions.isEmpty() || !w1c.isEmpty() || !roBits.isEmpty() || !writeOnly.isEmpty()
			|| !persist.isEmpty() || map.loginAddr) {
		h += QStringLiteral(" *\n * What the map says and the library does not do, the device's part:\n");
		if (!limited.isEmpty())
			h += QStringLiteral(" *   limits      %1_keep_limits() puts a write past them back:\n").arg(lower)
					+ listLine(QString(), limited);
		if (!actions.isEmpty()) h += QStringLiteral(" *   action      do it, then set the register back to idle:\n") + listLine(QString(), actions);
		if (!w1c.isEmpty())
			h += QStringLiteral(" *   w1c         a 1 written clears the bit, a 0 leaves it (the library stores what was written):\n")
					+ listLine(QString(), w1c);
		if (!roBits.isEmpty())
			h += QStringLiteral(" *   ro bits     read-only bits of a writable register: put them back after a write:\n")
					+ listLine(QString(), roBits);
		if (!writeOnly.isEmpty())
			h += QStringLiteral(" *   write-only  the library still answers a read, with what was written:\n") + listLine(QString(), writeOnly);
		if (!persist.isEmpty())
			h += QStringLiteral(" *   persist     keep across a reset (EEPROM, flash) and load at start:\n") + listLine(QString(), persist);
		if (map.loginAddr)
			h += QStringLiteral(" *   login       the map's login at %1 is not in the library\n").arg(addrText(map.loginAddr));
	}
	h += QStringLiteral(" *\n * Value names and bit fields: the C header export has them (_POS, _MSK).\n */\n");
	const QString guard = upper + QStringLiteral("_TABLE_H");
	h += QStringLiteral("#ifndef %1\n#define %1\n\n#include <stddef.h>\n#include <stdint.h>\n\n#include \"EVRe.h\"\n\n").arg(guard);
	if (map.deviceId) h += QStringLiteral("#define %1_ID %2\n").arg(upper, hex(map.deviceId));
	h += QStringLiteral("#define %1_SLAVE %2u\n").arg(upper).arg(map.slave);
	h += QStringLiteral("#define %1_WRITE_MIN %2 /* DEVICE_REG_WRITE_MIN */\n").arg(upper, hex(writeMin));
	h += QStringLiteral("#define %1_READ_MAX %2 /* DEVICE_REG_READ_MAX */\n").arg(upper, hex(readMax));
	if (roSize) h += QStringLiteral("#define %1_RO_SIZE %2u\n").arg(upper).arg(roSize);
	if (rwSize) h += QStringLiteral("#define %1_RW_SIZE %2u\n").arg(upper).arg(rwSize);

	/* the two images: a member per register, in address order, gaps filled */
	Names members;
	QHash<const RegDef *, QString> memberOf;
	QStringList asserts;
	auto image = [&](const QString &type, uint16_t first, uint16_t end, const QString &what) {
		QString s = QStringLiteral("\n/* %1 */\ntypedef struct __attribute__((packed)) {\n").arg(what);
		uint16_t at = first;
		for (const RegDef *defp : regs) {
			const RegDef &def = *defp;
			if (def.addr < first || def.addr >= end) continue;
			if (def.addr > at)
				s += QStringLiteral("\tuint8_t _gap_%1[%2]; /* %3..%4: no register */\n").arg(QString::number(at, 16)).arg(def.addr - at)
						.arg(addrText(at), addrText(uint16_t(def.addr - 1)));
			const QString member = members.take(memberName(def.name));
			memberOf.insert(defp, member);
			QString decl = cType(def.type) + QLatin1Char(' ') + member;
			if (def.type == RegType::Bytes) decl += QStringLiteral("[%1]").arg(def.size);
			if (def.hasDefault() && def.isNumeric()) decl += QStringLiteral(" = ") + rawLiteral(def, rawOf(def, def.defaultValue));
			QStringList facts{ QStringLiteral("%1 %2").arg(addrText(def.addr), def.name) };
			if (!def.unit.isEmpty()) facts << def.unit;
			if (def.scale != 1.0 || def.offset != 0.0)
				facts << QStringLiteral("shown = raw x %1").arg(number(def.scale))
								+ (def.offset != 0.0 ? QStringLiteral(" + ") + number(def.offset) : QString());
			if (!def.readable) facts << QStringLiteral("write-only");
			if (def.write != WriteKind::Normal) facts << writeWord(def.write);
			if (def.persist) facts << QStringLiteral("persist");
			if (def.danger) facts << QStringLiteral("danger");
			if (def.hasDefault() && def.isNumeric() && (def.scale != 1.0 || def.offset != 0.0))
				facts << QStringLiteral("default %1").arg(number(def.defaultValue));
			const int natural = typeSize(def.type);
			if (natural > 1 && (def.addr - BANK_FIRST) % natural) facts << QStringLiteral("not aligned");
			s += QStringLiteral("\t%1; /* %2 */\n").arg(decl, cComment(facts.join(QStringLiteral(", "))));
			asserts << QStringLiteral("static_assert(offsetof(%1, %2) == 0x%3u, \"%4 at %5\");").arg(type, member)
					.arg(def.addr - first, 0, 16).arg(cComment(def.name), addrText(def.addr));
			at = uint16_t(def.addr + def.size);
		}
		if (end > at)
			s += QStringLiteral("\tuint8_t _gap_%1[%2]; /* %3..%4: no register */\n").arg(QString::number(at, 16)).arg(end - at)
					.arg(addrText(at), addrText(uint16_t(end - 1)));
		return s + QStringLiteral("} %1;\n").arg(type);
	};
	if (roSize) {
		h += image(roType, BANK_FIRST, writeMin, QStringLiteral("%1..%2, read-only: the device writes it").arg(addrText(BANK_FIRST),
				addrText(uint16_t(writeMin - 1))));
		asserts.prepend(QStringLiteral("static_assert(sizeof(%1) == %2_RO_SIZE, \"the read-only image\");").arg(roType, upper));
	}
	if (rwSize) {
		const int before = int(asserts.size());
		h += image(rwType, writeMin, uint16_t(readMax + 1), QStringLiteral("%1..%2, read-write: the host writes it").arg(addrText(writeMin),
				addrText(readMax)));
		asserts.insert(before, QStringLiteral("static_assert(sizeof(%1) == %2_RW_SIZE, \"the read-write image\");").arg(rwType, upper));
	}
	h += QLatin1Char('\n') + asserts.join(QLatin1Char('\n')) + QLatin1Char('\n');

	h += QStringLiteral("\n/* The device bank served from the images: return it from protocolConfigure(), or call it after\n"
			" * protocolInit(). The library reads and writes through a pointer per byte. */\n");
	h += QStringLiteral("inline uint8_t %1_bind(base_t *dev%2) {\n").arg(lower,
			params.isEmpty() ? QString() : QStringLiteral(", ") + params.join(QStringLiteral(", ")));
	h += QStringLiteral("\tstatic uint8_t *bank[%1_READ_MAX - 0xd000u + 1u];\n\tif (dev == nullptr) return INSTANCE_IS_NULL;\n").arg(upper);
	if (roSize)
		h += QStringLiteral("\tuint8_t *p = reinterpret_cast<uint8_t *>(ro);\n"
				"\tfor (uint16_t i = 0; i < %1_RO_SIZE; ++i) bank[i] = p + i;\n").arg(upper);
	if (rwSize)
		h += QStringLiteral("\tuint8_t *q = reinterpret_cast<uint8_t *>(rw);\n"
				"\tfor (uint16_t i = 0; i < %1_RW_SIZE; ++i) bank[%2 + i] = q + i;\n").arg(upper, roSize ? upper + QStringLiteral("_RO_SIZE")
						: QStringLiteral("0"));
	h += QStringLiteral("\tdev->SALVE_ID_REG = %1_SLAVE;\n").arg(upper);
	if (map.deviceId) h += QStringLiteral("\tdev->DEVICE_ID = %1_ID;\n").arg(upper);
	h += QStringLiteral("\tdev->DEVICE_REG_READ_MAX = %1_READ_MAX;\n\tdev->DEVICE_REG_WRITE_MIN = %1_WRITE_MIN;\n"
			"\tdev->D000 = bank;\n\treturn NO_ERROR;\n}\n").arg(upper);

	if (!limited.isEmpty()) {
		h += QStringLiteral("\n/* A host write past a register's limits (the map's min and max, raw here), put back as it was:\n"
				" * seen is rw as the last pass left it. A special value is always let through. Returns how many\n"
				" * registers were put back. */\n");
		h += QStringLiteral("inline unsigned %1_keep_limits(%2 *rw, const %2 *seen) {\n\tunsigned back = 0;\n").arg(lower, rwType);
		for (const RegDef *defp : regs) {
			const RegDef &def = *defp;
			if (!limited.contains(def.name) || !memberOf.contains(defp)) continue;
			const QString m = QStringLiteral("rw->") + memberOf.value(defp);
			double low = def.hasMin() ? rawOf(def, def.min) : -std::numeric_limits<double>::infinity();
			double high = def.hasMax() ? rawOf(def, def.max) : std::numeric_limits<double>::infinity();
			if (low > high) std::swap(low, high); /* a negative scale */
			QStringList inRange;
			if (def.type == RegType::F32) {
				if (std::isfinite(low)) inRange << QStringLiteral("%1 >= %2").arg(m, rawLiteral(def, low));
				if (std::isfinite(high)) inRange << QStringLiteral("%1 <= %2").arg(m, rawLiteral(def, high));
			} else {
				double typeLow, typeHigh;
				rawRange(def.type, typeLow, typeHigh);
				low = std::ceil(low - 1e-9);
				high = std::floor(high + 1e-9);
				if (low > typeLow) inRange << QStringLiteral("%1 >= %2").arg(m, rawLiteral(def, low));
				if (high < typeHigh) inRange << QStringLiteral("%1 <= %2").arg(m, rawLiteral(def, high));
			}
			if (inRange.isEmpty()) continue;
			QString condition = QStringLiteral("!(%1)").arg(inRange.join(QStringLiteral(" && ")));
			for (const SpecialValue &special : def.special)
				condition += QStringLiteral(" && %1 != %2").arg(m, rawLiteral(def, rawOf(def, special.value)));
			h += QStringLiteral("\tif (%1) { /* %2 */\n\t\t%3 = seen->%4;\n\t\tback++;\n\t}\n").arg(condition, cComment(def.name), m,
					memberOf.value(defp));
		}
		h += QStringLiteral("\treturn back;\n}\n");
	}
	h += QStringLiteral("\n#endif /* %1 */\n").arg(guard);
	out = h.toUtf8();
	return true;
}

/* ====================================================================== CSV */

namespace {

const QStringList CSV_COLUMNS{ "addr", "name", "type", "size", "unit", "access", "write", "persist", "group", "desc",
	"notes", "danger", "format", "scale", "offset", "decimals", "min", "max", "default", "special", "enum", "fields",
	"plot" };

/* inside the compact columns, \ ; = | { } # @ are written with a \ before them */
QString esc(const QString &text) {
	QString out;
	for (const QChar c : text) {
		if (QStringLiteral("\\;=|{}#@").contains(c)) out += QLatin1Char('\\');
		out += c;
	}
	return out;
}

/* text split at `separator`, where it is not escaped; the pieces still escaped */
QStringList splitEscaped(const QString &text, QChar separator) {
	QStringList parts;
	QString part;
	int depth = 0;
	for (int i = 0; i < text.size(); i++) {
		const QChar c = text[i];
		if (c == QLatin1Char('\\') && i + 1 < text.size()) {
			part += c;
			part += text[++i];
			continue;
		}
		if (c == QLatin1Char('{')) depth++;
		if (c == QLatin1Char('}')) depth--;
		if (c == separator && depth == 0) {
			parts << part;
			part.clear();
			continue;
		}
		part += c;
	}
	if (!part.isEmpty() || !parts.isEmpty()) parts << part;
	return parts;
}

QString unesc(const QString &text) {
	QString out;
	for (int i = 0; i < text.size(); i++) {
		if (text[i] == QLatin1Char('\\') && i + 1 < text.size()) i++;
		out += text[i];
	}
	return out;
}

/* "0=off;1=on" and back */
QString namesText(const QVector<QPair<QString, QString>> &names) {
	QStringList parts;
	for (const auto &name : names) parts << esc(name.first) + QLatin1Char('=') + esc(name.second);
	return parts.join(QLatin1Char(';'));
}

QVector<QPair<QString, QString>> namesFrom(const QString &text) {
	QVector<QPair<QString, QString>> names;
	for (const QString &part : splitEscaped(text, QLatin1Char(';'))) {
		const QStringList kv = splitEscaped(part, QLatin1Char('='));
		if (kv.size() >= 2) names.push_back({ unesc(kv[0]).trimmed(), unesc(kv.mid(1).join(QLatin1Char('='))).trimmed() });
	}
	return names;
}

QString csvCell(const QString &text) {
	if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n'))
			&& !text.contains(QLatin1Char('\r')))
		return text;
	return QLatin1Char('"') + QString(text).replace(QStringLiteral("\""), QStringLiteral("\"\"")) + QLatin1Char('"');
}

/* RFC 4180: quoted cells may hold commas, quotes (doubled) and line breaks */
QVector<QStringList> parseCsv(const QString &text) {
	QVector<QStringList> rows;
	QStringList row;
	QString cellText;
	bool quoted = false, any = false;
	for (int i = 0; i < text.size(); i++) {
		const QChar c = text[i];
		if (quoted) {
			if (c == QLatin1Char('"')) {
				if (i + 1 < text.size() && text[i + 1] == QLatin1Char('"')) {
					cellText += c;
					i++;
				} else {
					quoted = false;
				}
			} else {
				cellText += c;
			}
			continue;
		}
		if (c == QLatin1Char('"')) {
			quoted = true;
			any = true;
		} else if (c == QLatin1Char(',')) {
			row << cellText;
			cellText.clear();
			any = true;
		} else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
			if (c == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) i++;
			if (any || !cellText.isEmpty()) {
				row << cellText;
				rows << row;
			}
			row.clear();
			cellText.clear();
			any = false;
		} else {
			cellText += c;
			any = true;
		}
	}
	if (any || !cellText.isEmpty()) {
		row << cellText;
		rows << row;
	}
	return rows;
}

} // namespace

QByteArray exportCsv(const DeviceMap &map) {
	QString csv = CSV_COLUMNS.join(QLatin1Char(',')) + QLatin1Char('\n');
	for (const RegDef &def : map.regs) {
		QVector<QPair<QString, QString>> enumNames, special;
		for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it) enumNames.push_back({ keyText(it.key(), def.enumHex), it.value() });
		for (const SpecialValue &value : def.special) special.push_back({ number(value.value), value.name });
		QStringList fields;
		for (const BitField &field : def.fields) {
			QVector<QPair<QString, QString>> values;
			for (auto it = field.values.begin(); it != field.values.end(); ++it) values.push_back({ keyText(it.key(), field.valuesHex), it.value() });
			QString part = esc(field.name) + QLatin1Char('@') + bitsText(field);
			const QString access = fieldAccessWord(field.access);
			if (!access.isEmpty()) part += QLatin1Char('@') + access;
			if (!values.isEmpty()) part += QLatin1Char('{') + namesText(values) + QLatin1Char('}');
			if (!field.desc.isEmpty()) part += QLatin1Char('#') + esc(field.desc);
			fields << part;
		}
		const QStringList cells{ addrText(def.addr), def.name, typeName(def.type), QString::number(def.size), def.unit,
			accessText(def), writeWord(def.write), def.persist ? QStringLiteral("1") : QString(), def.group, def.desc,
			def.notes, def.danger ? QStringLiteral("1") : QString(), def.hex ? QStringLiteral("hex") : QString(),
			def.scale != 1.0 ? number(def.scale) : QString(), def.offset != 0.0 ? number(def.offset) : QString(),
			def.decimals >= 0 ? QString::number(def.decimals) : QString(), def.hasMin() ? number(def.min) : QString(),
			def.hasMax() ? number(def.max) : QString(), def.hasDefault() ? number(def.defaultValue) : QString(),
			namesText(special), namesText(enumNames), fields.join(QLatin1Char('|')),
			def.plottable ? QString() : QStringLiteral("0") };
		QStringList out;
		for (const QString &text : cells) out << csvCell(text);
		csv += out.join(QLatin1Char(',')) + QLatin1Char('\n');
	}
	return csv.toUtf8();
}

bool importCsv(const QByteArray &text, QVector<RegDef> &regs, QString &err) {
	QString content = QString::fromUtf8(text);
	if (content.startsWith(QChar(0xFEFF))) content.remove(0, 1);
	const QVector<QStringList> rows = parseCsv(content);
	if (rows.isEmpty()) {
		err = QObject::tr("the file is empty");
		return false;
	}
	QHash<QString, int> column;
	for (int i = 0; i < rows[0].size(); i++) column.insert(rows[0][i].trimmed().toLower(), i);
	if (!column.contains(QStringLiteral("addr")) || !column.contains(QStringLiteral("name"))) {
		err = QObject::tr("the first line must name the columns, \"addr\" and \"name\" among them");
		return false;
	}
	regs.clear();
	for (int r = 1; r < rows.size(); r++) {
		const QStringList &row = rows[r];
		auto get = [&](const char *name) {
			const int i = column.value(QLatin1String(name), -1);
			return i >= 0 && i < row.size() ? row[i].trimmed() : QString();
		};
		auto numberOr = [&](const char *name, double fallback, bool &bad) {
			const QString t = get(name);
			if (t.isEmpty()) return fallback;
			bool ok = false;
			const double v = QLocale::c().toDouble(t, &ok);
			if (!ok) bad = true;
			return v;
		};
		const QString where = QObject::tr("line %1").arg(r + 1);
		RegDef def;
		bool ok = false;
		const uint addr = parseAddress(get("addr"), &ok);
		if (!ok || addr > 0xFFFF) {
			err = QObject::tr("%1: bad address \"%2\"").arg(where, get("addr"));
			return false;
		}
		def.addr = uint16_t(addr);
		def.name = get("name");
		if (def.name.isEmpty()) def.name = addrText(def.addr);
		const QString type = get("type").isEmpty() ? QStringLiteral("u16") : get("type");
		if (!parseType(type, def.type)) {
			err = QObject::tr("%1: unknown type \"%2\"").arg(where, type);
			return false;
		}
		def.size = def.type == RegType::Bytes ? std::max(1, get("size").toInt()) : typeSize(def.type);
		def.unit = get("unit");
		const QString access = get("access").toLower();
		def.rw = access.contains(QLatin1Char('w'));
		def.readable = access != QLatin1String("wo");
		const QString write = get("write").toLower();
		def.write = write == QLatin1String("action") ? WriteKind::Action
				: write == QLatin1String("w1c") ? WriteKind::WriteOneToClear : WriteKind::Normal;
		auto yes = [](const QString &t) { return t == QLatin1String("1") || t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
					|| t.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0; };
		def.persist = yes(get("persist"));
		def.danger = yes(get("danger"));
		const QString plot = get("plot").trimmed(); /* empty (an older file without the column): plotted */
		def.plottable = plot.isEmpty() || yes(plot);
		def.group = get("group").isEmpty() ? QStringLiteral("Registers") : get("group");
		def.desc = get("desc");
		def.notes = get("notes");
		def.hex = get("format").compare(QLatin1String("hex"), Qt::CaseInsensitive) == 0;
		bool bad = false;
		def.scale = numberOr("scale", 1.0, bad);
		def.offset = numberOr("offset", 0.0, bad);
		def.decimals = get("decimals").isEmpty() ? -1 : std::clamp(get("decimals").toInt(), -1, 15);
		def.min = numberOr("min", NO_LIMIT, bad);
		def.max = numberOr("max", NO_LIMIT, bad);
		def.defaultValue = numberOr("default", NO_LIMIT, bad);
		if (bad) {
			err = QObject::tr("%1 (%2): a number column holds something else").arg(where, def.name);
			return false;
		}
		auto integerKey = [](const QString &key, bool &hex, qint64 &value) {
			bool fine = false;
			const bool isHex = key.startsWith(QLatin1String("0x"), Qt::CaseInsensitive);
			value = isHex ? key.mid(2).toLongLong(&fine, 16) : key.toLongLong(&fine);
			hex = hex || isHex;
			return fine;
		};
		for (const auto &name : namesFrom(get("enum"))) {
			qint64 key;
			if (integerKey(name.first, def.enumHex, key)) def.enumValues.insert(key, name.second);
		}
		for (const auto &name : namesFrom(get("special"))) {
			bool fine = false;
			const double value = QLocale::c().toDouble(name.first, &fine);
			if (fine) def.special.push_back({ value, name.second });
		}
		for (const QString &part : splitEscaped(get("fields"), QLatin1Char('|'))) {
			if (part.trimmed().isEmpty()) continue;
			QString rest = part;
			BitField field;
			const QStringList descSplit = splitEscaped(rest, QLatin1Char('#'));
			if (descSplit.size() > 1) field.desc = unesc(descSplit.mid(1).join(QLatin1Char('#')));
			rest = descSplit.value(0);
			const int brace = int(rest.indexOf(QLatin1Char('{')));
			if (brace >= 0) {
				const QString values = rest.mid(brace + 1, rest.lastIndexOf(QLatin1Char('}')) - brace - 1);
				for (const auto &name : namesFrom(values)) {
					qint64 key;
					if (integerKey(name.first, field.valuesHex, key)) field.values.insert(key, name.second);
				}
				rest = rest.left(brace);
			}
			const QStringList at = splitEscaped(rest, QLatin1Char('@'));
			field.name = unesc(at.value(0)).trimmed();
			const QStringList bits = at.value(1).split(QLatin1Char(':'));
			bool okHigh = false, okLow = true;
			const int high = bits.value(0).toInt(&okHigh);
			const int low = bits.size() > 1 ? bits.value(1).toInt(&okLow) : high;
			if (!okHigh || !okLow) {
				err = QObject::tr("%1 (%2): field \"%3\" has no bits (NAME@7:4)").arg(where, def.name, field.name);
				return false;
			}
			field.lsb = std::min(high, low);
			field.width = std::abs(high - low) + 1;
			const QString access = at.value(2).trimmed().toLower();
			field.access = access == QLatin1String("ro") ? FieldAccess::ReadOnly : access == QLatin1String("rw") ? FieldAccess::ReadWrite
					: access == QLatin1String("w1c") ? FieldAccess::WriteOneToClear : FieldAccess::AsRegister;
			def.fields.push_back(field);
		}
		regs.push_back(def);
	}
	return true;
}

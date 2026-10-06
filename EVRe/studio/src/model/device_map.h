/* SPDX-License-Identifier: Apache-2.0 */
/* A device's register map, as a JSON file (format "evre-map/1"):
 *
 *   { "format": "evre-map/1", "device": "...", "desc": "...", "notes": "...",
 *     "device_id": "0x1001", "slave": 1,
 *     "usb": { "vid": "0x1234", "pid": "0x5678" },   optional: marks its port in the list
 *     "login": { "addr": "0xF000", "size": 16 },     optional: see below
 *     "protocol": { "transport": "serial", "baud": 115200, "tcp_port": 1210,
 *                   "timeout_ms": 200, "notes": "..." },   optional: how the device is reached
 *     "groups": { "Power": { "notes": "..." } },    optional: notes on a group
 *     "extends": "base.json",                        optional: see below
 *     "registers": [
 *       { "addr": "0xD004", "name": "SUPPLY_V", "type": "f32", "unit": "V",
 *         "access": "ro", "group": "Power", "desc": "...",
 *         "notes": "...",                     optional: longer text (several lines, Markdown)
 *         "write": "action",                  optional: "action" (does something, then reads
 *                                             back idle) or "w1c" (a 1 written clears that bit)
 *         "persist": true,                    optional: kept across a reset (the default is
 *                                             then the factory value)
 *         "scale": 1, "offset": 0,            optional: shown = raw * scale + offset
 *         "decimals": 2,                      optional: the shown value with 2 decimals
 *         "min": 0, "max": 30,                optional: the shown value's limits for writes
 *         "past_limits": "clamp",             optional: "refuse" (the default) or "clamp": the
 *                                             device takes a value past them and clamps it
 *         "default": 12,                      optional: the value after a reset (a number,
 *                                             or one of the register's names)
 *         "special": { "-1": "not measured" },  optional: names for single values of a number
 *         "danger": true,                     optional: confirm every write
 *         "format": "hex",                    optional: show the value in hex
 *         "enum": { "0": "off", "1": "on" },  optional: names of values
 *         "fields": [ { "name": "MODE", "bits": "1:0", "access": "rw", "desc": "...",
 *                       "values": { "0": "idle", "1": "run" } } ] } ] }
 *
 * Types: u8 i8 u16 i16 u32 i32 f32, and bytes (with "size"). Little endian.
 * Access: "ro", "rw", or "wo" (write-only: never read, so never polled). A field's
 * "access" ("ro", "rw", "w1c") is only given where it differs from its register's.
 * Value names ("enum", a field's "values") are keyed by the raw number, decimal
 * or 0x..; "special", "min", "max" and "default" are in shown units (after scale
 * and offset).
 *
 * "login" is for a device, or a TCP gateway in front of it, that wants a
 * token before it answers: right after the link opens, the token typed in the
 * window is written to "addr" as UTF-8, cut or zero-padded to "size" bytes
 * (16 when not given). A map without "login" sends no token.
 *
 * "extends" makes the file an overlay on another map (its path relative to this
 * file): the overlay's settings replace the base's; a register with the address
 * of one in the base changes only the keys it gives (null removes a key); a new
 * address adds a register; { "addr": "0x..", "remove": true } removes one.
 * Saved, an overlay keeps only what differs from its base.
 *
 * Saving keeps the file as it was written wherever nothing changed: layout,
 * blank lines, key order, keys the Studio does not know, numbers and strings as
 * typed. A changed register is written again in its own layout.
 *
 * Also here: a register's bytes as numbers and text (decode, format, encode),
 * how addresses are written, the checks of a map, and which registers are read
 * together, for every part of the program.
 */
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>
#include <cstdint>
#include <limits>
#include <memory>

/* A register's type: its size, and how its bytes read. */
enum class RegType { U8, I8, U16, I16, U32, I32, F32, Bytes };

/* How a register is written, beyond read-write: "write" in the map. */
enum class WriteKind {
	Normal,
	Action,          /* "action": a write does something, then the register reads back idle */
	WriteOneToClear, /* "w1c": a 1 written to a bit clears it, a 0 leaves it */
};

/* A bit field's own access, where it differs from its register's. */
enum class FieldAccess { AsRegister, ReadOnly, ReadWrite, WriteOneToClear };

/* "protocol": how the device is reached, for the reader of the map (and the export). */
struct MapProtocol {
	QString transport;   /* "serial", "tcp", "usb", or several: "serial, tcp" */
	int baud = 0;        /* 0 = not given */
	int tcpPort = 0;
	int timeoutMs = 0;
	QString notes;
	bool isEmpty() const { return transport.isEmpty() && !baud && !tcpPort && !timeoutMs && notes.isEmpty(); }
};

/* A field of a register: `width` bits from `lsb` up, and names for some of its values. */
struct BitField {
	QString name;
	int lsb = 0;
	int width = 1;
	QMap<qint64, QString> values;
	QString desc;
	FieldAccess access = FieldAccess::AsRegister;
	bool valuesHex = false;  /* the file keyed its value names 0x..: saved the same way */
	int source = -1;         /* which field of the register as loaded (to save it as it was); -1 = new */
};

/* "special": a name for one value of a number register ("-1": "not measured"), in shown units. */
struct SpecialValue {
	double value = 0;
	QString name;
};

/* no "min", "max" or "default" */
constexpr double NO_LIMIT = std::numeric_limits<double>::quiet_NaN();

/* One register of the map: where it is, how its bytes read, and how it is shown. */
struct RegDef {
	uint16_t addr = 0;
	QString name;
	RegType type = RegType::U16;
	int size = 2;            /* bytes; fixed by the type except for Bytes */
	QString unit;
	QString group;
	QString desc;
	bool rw = false;
	bool readable = true;    /* false: "access": "wo", written only, never read */
	WriteKind write = WriteKind::Normal;
	bool persist = false;    /* kept across a reset */
	bool plottable = true;   /* "plot": false: a fixed register (an ID, a setting), never a line on the chart */
	QString notes;           /* longer than desc: several lines, Markdown */
	bool danger = false;
	bool hex = false;        /* "format": "hex": shown as 0x.. */
	double scale = 1.0;
	double offset = 0.0;
	QMap<qint64, QString> enumValues;
	QVector<BitField> fields;
	int decimals = -1;               /* the shown value's decimals; -1 = as many as its size needs */
	double min = NO_LIMIT;           /* the shown value's limits for writes; NaN = none */
	double max = NO_LIMIT;
	/* "past_limits": "clamp": the device takes any value of the type and clamps it, so a host sends a value past
	 * min or max as it is (EVRe Guard checks only NaN and the infinities). false: "refuse", the default */
	bool clamps = false;
	double defaultValue = NO_LIMIT;  /* the value after a reset; NaN = not given */
	QVector<SpecialValue> special;
	bool enumHex = false;            /* the file keyed its enum 0x..: saved the same way */
	bool specialHex = false;
	/* which register of the loaded file this is (to save it as it was); -1 = new. A copy
	 * keeps it: one of the two is saved as the original, the other as a new register
	 * with the keys of the one it came from. */
	int source = -1;
	/* the register's identity while the map is edited (MapDocument): the same through every
	 * change, even of its address; 0 = not given yet. Not in the file. */
	quint32 uid = 0;
	/* the device it is on, when several share the link (model/bus_file.h): that device's slave address.
	 * 0 = the one device of a map (the link's slave). Not in the file. */
	uint8_t slave = 0;

	bool isNumeric() const { return type != RegType::Bytes; }
	bool canPlot() const { return isNumeric() && plottable; } /* a line on the chart: a number the map lets plot */
	bool isFloat() const { return type == RegType::F32 || scale != 1.0 || offset != 0.0; }
	bool hasMin() const { return min == min; } /* not NaN */
	bool hasMax() const { return max == max; }
	bool hasDefault() const { return defaultValue == defaultValue; }
};

/* ---------------------------------------------------------------------- types */

int typeSize(RegType type);   /* bytes; 0 for Bytes (its size is the register's own) */
QString typeName(RegType type); /* as in the map files: "u16", "f32", "bytes" */
/* a map's type name, or a C one ("uint16_t", "float"); false if it is neither */
bool parseType(const QString &text, RegType &type);

/* --------------------------------------------------------------------- values */

/* raw bytes (exactly def.size) -> value, as a double (scale and offset applied) */
double decodeNumber(const RegDef &def, const QByteArray &raw);
/* raw -> the integer the bits are in (for fields and enums) */
qint64 decodeRaw(const RegDef &def, const QByteArray &raw);
/* the lowest `width` bits set (all 64 from 64 on): a field's bits, before they are shifted to its lsb */
inline quint64 bitMask(int width) { return width >= 64 ? ~0ULL : (1ULL << width) - 1; }
/* the value column: number with sensible digits, enum name, or hex for bytes */
QString formatValue(const RegDef &def, const QByteArray &raw);
/* a shown value as the value column writes it: the register's decimals, or as many as it needs */
QString formatNumber(const RegDef &def, double shown);
/* the decoded column: a special value's name, the enum name, or the fields "MODE=run  READY" */
QString formatDecoded(const RegDef &def, const QByteArray &raw);
/* the name "special" gives this shown value; empty if none */
QString specialName(const RegDef &def, double shown);
/* text typed by the user -> raw bytes to write; accepts 12, -3.5, 0x1F, an enum or special name */
bool encodeValue(const RegDef &def, const QString &text, QByteArray &out, QString &err);
/* why a shown value is outside the register's "min" / "max" ("above the maximum 30 V");
 * empty if it is inside them, or is one of its special values */
QString limitProblem(const RegDef &def, double shown);
/* limitProblem for a value a host is about to send: empty too for a register the device clamps
 * ("past_limits": "clamp"), which takes any value of its type */
QString writeLimitProblem(const RegDef &def, double shown);
/* a host writes it: access rw or wo, or a field with access rw or w1c (EVRe Guard's table has an entry for it) */
bool hostWrites(const RegDef &def);
/* the bytes written to a map's login register: the token as UTF-8, cut or zero-padded to size.
 * One definition for the Studio and the probe, so both send exactly the same login. */
QByteArray encodeLoginToken(const QString &token, int size);

/* ------------------------------------------------------------------ addresses */

/* 0x00AB: four upper-case hex digits after a lower-case 0x, as in the map files */
QString addrText(uint16_t addr);
/* an address as typed or written in a file: "0x1F" (any case) or "31", spaces around it
 * allowed; *ok is false for anything else. Not range-checked: the caller refuses > 0xFFFF. */
uint parseAddress(const QString &text, bool *ok = nullptr);

/* ---------------------------------------------------------------- block reads */

/* Registers close together are read in one request: a register joins the
 * read before it when both are in the same 256-address bank (the same high
 * byte: 0xD0xx) and at most MAX_BLOCK_GAP bytes lie between them. The poller,
 * the API server and the probe merge them the same way. */
constexpr int MAX_BLOCK_GAP = 8;
inline bool sameBank(uint16_t a, uint16_t b) { return (a & 0xFF00) == (b & 0xFF00); }

/* Polled (and recorded) with the others: every register but a write-only one and a
 * byte array longer than MAX_POLLED_BYTES (a message buffer), which is read on request only. */
constexpr int MAX_POLLED_BYTES = 32;
inline bool isPollable(const RegDef &def) {
	return def.readable && (def.type != RegType::Bytes || def.size <= MAX_POLLED_BYTES);
}
/* the map's "access" word: "ro", "rw" or "wo" */
QString accessText(const RegDef &def);

/* A register's identity on the link: its device (RegDef::slave) and its address. With one device (slave 0) it
 * is the address itself. The engine's samples and CSV columns, the chart's lines and the math lines' inputs are
 * keyed by it; it never reaches 1 << 24. */
using RegKey = quint32;
inline RegKey regKey(uint8_t slave, uint16_t addr) { return (RegKey(slave) << 16) | addr; }
inline RegKey regKey(const RegDef &def) { return regKey(def.slave, def.addr); }
inline uint8_t regKeySlave(RegKey key) { return uint8_t(key >> 16); }
inline uint16_t regKeyAddr(RegKey key) { return uint16_t(key); }

/* Where a request for a register of the table goes on the link: a device of a bus at its own slave (RegDef::slave),
 * the one device of a map (table slave 0) at the link's slave. */
inline uint8_t requestSlave(uint8_t tableSlave, uint8_t linkSlave) { return tableSlave ? tableSlave : linkSlave; }

/* ------------------------------------------------------------------- the file */

/* What load() read, kept so that a save changes only what was edited (device_map.cpp). */
struct MapSource;

/* The map file in memory: the device, how to reach it, and its registers. */
struct DeviceMap {
	QString format = QStringLiteral("evre-map/1");
	QString device;
	QString desc;
	QString notes;           /* longer text on the whole map (Markdown) */
	MapProtocol protocol;
	QMap<QString, QString> groupNotes; /* "groups": notes per group name */
	uint16_t deviceId = 0;   /* 0 = not checked */
	uint8_t slave = 1;
	uint16_t usbVid = 0, usbPid = 0; /* 0 = none: the port list marks this device */
	uint16_t loginAddr = 0;  /* the register the token is written to after connecting; 0 = none */
	int loginSize = 16;      /* its size in bytes: the token is cut or zero-padded to it */
	QVector<RegDef> regs;
	QString path;            /* where it was loaded from, empty if new */
	QString basePath;        /* an overlay: the absolute path of the map it extends; empty = none */
	std::shared_ptr<const MapSource> source; /* the file as loaded; null for a new map */

	bool isOverlay() const { return !basePath.isEmpty(); }
	/* false with err set when the file cannot be read or is not a valid map; this map is then unchanged */
	bool load(const QString &file, QString &err);
	/* writes the map as it is now to `file` (see the top of this file). flatten: an overlay
	 * is written as a whole map, without "extends". */
	bool save(const QString &file, QString &err, bool flatten = false) const;
	/* the text save() writes */
	QByteArray toJson(const QString &file, bool flatten = false) const;
	void sort();             /* the registers by address (the same address: in file order) */
};

/* --------------------------------------------------------------------- checks */

/* Something wrong in a map: an error makes a register unusable or ambiguous, a
 * warning is only odd. `reg` is the index in DeviceMap::regs, -1 for the map itself. */
struct MapIssue {
	int reg = -1;
	bool error = false;
	QString text;
};
QVector<MapIssue> checkMap(const DeviceMap &map);

/* ------------------------------------------------------------ the clipboard */

/* registers as a JSON list, the map file's way (copy) */
QByteArray registersToJson(const QVector<RegDef> &regs);
/* registers from JSON (paste): a list of them, one register, or a whole map; false with err if none */
bool registersFromJson(const QByteArray &text, QVector<RegDef> &regs, QString &err);

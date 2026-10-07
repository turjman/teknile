/* SPDX-License-Identifier: Apache-2.0 */
/* evre: the EVRe tools on the command line, on EVRe Studio's protocol core
 * (link, master, map, exports) without a window.
 *
 *   evre validate MAP...                   the map's checks; exit 1 if one has an error
 *   evre export MAP --to md|h|py|csv|table a specification, a C header, a Python module, a sheet,
 *                   [--prefix P] [-o FILE]  or the device table for the EVRe library (C++)
 *   evre info  LINK [--map MAP]            DEVICE_ID, protocol revision, capabilities
 *   evre read  LINK --map MAP NAME...      values of registers (by name or 0x address)
 *   evre read  LINK --addr 0xD000 --count N    raw bytes, no map needed
 *   evre dump  LINK --map MAP              every readable register, once
 *   evre watch LINK --map MAP NAME... [--interval MS] [--count N]    a line per poll
 *   evre write LINK --map MAP NAME=VALUE... [--force]    written, then read back
 *   evre check LINK --map MAP [--writes] [--force]       does the device answer as its map says?
 *   evre broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]   to every device at once, then each read back
 *   evre record LINK --map MAP --stream NAME -o FILE [--seconds S]  a fast stream's blocks into a .evrs file
 *
 *   --bus BUS in place of --map MAP: several devices on one link (a bus file, evre-bus/1). Their registers are
 *   named after their devices (D1_SPEED, D2_SPEED) and each request goes to its device's slave.
 *
 *   LINK: --tcp HOST:PORT | --serial PORT[:BAUD]   (baud 115200 when not given)
 *   --slave N, --timeout MS: else the map's (1, 1000 ms)     --json: one JSON object per line
 *
 * Writes are only what `write` is told. A read-only register is refused; a
 * register marked danger, and a value past the map's min or max, need --force.
 * The login token (a map with "login") is taken from EVRE_TOKEN only, never
 * from the command line, where other users of the computer could see it.
 *
 * Exit codes: 0 done, 1 the device or the map said no (an error, a refusal, a
 * timeout), 2 the command line or a file is wrong. */
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <memory>

#include "evre/master.h"
#include "evre/registers.h"
#include "io/fast_stream.h"
#include "model/bus_file.h"
#include "model/device_map.h"
#include "model/fast_recording.h"
#include "model/map_export.h"

namespace {

/* ----------------------------------------------------------------- output */

bool jsonOutput = false;

void out(const QString &text) {
	std::fputs(text.toUtf8().constData(), stdout);
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

void err(const QString &text) {
	std::fputs(("evre: " + text).toUtf8().constData(), stderr);
	std::fputc('\n', stderr);
}

void outJson(const QJsonObject &object) { out(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))); }

const char *USAGE = R"(usage:
  evre validate MAP...
  evre export MAP --to md|h|py|csv|table [--prefix P] [-o FILE]
  evre info  LINK [--map MAP]
  evre read  LINK --map MAP NAME...          (or --addr 0xD000 --count N)
  evre dump  LINK --map MAP
  evre watch LINK --map MAP NAME... [--interval MS] [--count N]
  evre write LINK --map MAP NAME=VALUE... [--force]
  evre check LINK --map MAP [--writes] [--force]
  evre broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]
  evre record LINK --map MAP --stream NAME -o FILE [--seconds S]
read, dump, watch, write: --bus BUS in place of --map MAP (registers named D1_NAME, D2_NAME, ...)
LINK: --tcp HOST:PORT | --serial PORT[:BAUD]; also --slave N, --timeout MS, --json
the login token (if the map has a login register): EVRE_TOKEN)";

/* ---------------------------------------------------------- the arguments */

struct Args {
	QString command;
	QStringList positional;
	QString tcp, serial, map, bus, to, prefix, output, addr, stream;
	int count = -1, slave = -1, timeoutMs = -1;
	double intervalMs = 500, seconds = -1;
	bool force = false, writes = false;
};

bool parseArgs(const QStringList &argv, Args &a, QString &why) {
	if (argv.size() < 2) {
		why = QStringLiteral("no command");
		return false;
	}
	a.command = argv[1];
	for (int i = 2; i < argv.size(); i++) {
		const QString &arg = argv[i];
		auto value = [&](QString &into) {
			if (i + 1 >= argv.size()) {
				why = QStringLiteral("%1 needs a value").arg(arg);
				return false;
			}
			into = argv[++i];
			return true;
		};
		auto number = [&](auto &into) {
			QString text;
			if (!value(text)) return false;
			bool ok = false;
			const double v = text.toDouble(&ok);
			if (!ok) {
				why = QStringLiteral("%1: not a number: %2").arg(arg, text);
				return false;
			}
			into = static_cast<std::remove_reference_t<decltype(into)>>(v);
			return true;
		};
		bool ok = true;
		if (arg == QLatin1String("--tcp")) ok = value(a.tcp);
		else if (arg == QLatin1String("--serial")) ok = value(a.serial);
		else if (arg == QLatin1String("--map")) ok = value(a.map);
		else if (arg == QLatin1String("--bus")) ok = value(a.bus);
		else if (arg == QLatin1String("--to")) ok = value(a.to);
		else if (arg == QLatin1String("--prefix")) ok = value(a.prefix);
		else if (arg == QLatin1String("-o") || arg == QLatin1String("--output")) ok = value(a.output);
		else if (arg == QLatin1String("--addr")) ok = value(a.addr);
		else if (arg == QLatin1String("--stream")) ok = value(a.stream);
		else if (arg == QLatin1String("--seconds")) ok = number(a.seconds);
		else if (arg == QLatin1String("--count")) ok = number(a.count);
		else if (arg == QLatin1String("--slave")) ok = number(a.slave);
		else if (arg == QLatin1String("--timeout")) ok = number(a.timeoutMs);
		else if (arg == QLatin1String("--interval")) ok = number(a.intervalMs);
		else if (arg == QLatin1String("--force")) a.force = true;
		else if (arg == QLatin1String("--writes")) a.writes = true;
		else if (arg == QLatin1String("--json")) jsonOutput = true;
		else if (arg.startsWith(QLatin1String("--"))) {
			why = QStringLiteral("unknown option %1").arg(arg);
			return false;
		} else a.positional << arg;
		if (!ok) return false;
	}
	return true;
}

bool loadMap(const QString &file, DeviceMap &map) {
	QString why;
	if (map.load(file, why)) return true;
	err(QStringLiteral("%1: %2").arg(file, why));
	return false;
}

/* --bus: a device's login register (the session logs every one in) */
struct DeviceLogin {
	uint8_t slave;
	uint16_t addr;
	int size;
};

/* What a command works on: --map MAP, or --bus BUS: then every device's registers in one map, named after their
 * device (D1_SPEED), each with its slave (RegDef::slave), and each device's login register, own map and name. */
struct Target {
	DeviceMap map;
	QVector<DeviceLogin> logins;   /* --bus only */
	QVector<DeviceMap> deviceMaps; /* --bus only: the broadcast rule compares them */
	QStringList deviceNames;       /* --bus only: D1, D2 */
};

bool hasTarget(const Args &a) { return !a.map.isEmpty() || !a.bus.isEmpty(); }
QString targetName(const Args &a) { return a.bus.isEmpty() ? a.map : a.bus; }
bool loadTarget(const Args &a, Target &target) {
	target = Target();
	DeviceMap &map = target.map;
	if (a.bus.isEmpty()) return loadMap(a.map, map);
	BusFile bus;
	QString why;
	if (!bus.load(a.bus, why)) {
		err(QStringLiteral("%1: %2").arg(a.bus, why));
		return false;
	}
	const QStringList problems = checkBus(bus);
	if (!problems.isEmpty()) {
		err(QStringLiteral("%1: %2").arg(a.bus, problems.join(QStringLiteral("; "))));
		return false;
	}
	map = DeviceMap();
	map.device = bus.name;
	for (int i = 0; i < bus.devices.size(); i++) {
		const BusDevice &device = bus.devices[i];
		DeviceMap one;
		if (!loadMap(bus.mapPath(i), one)) return false;
		if (one.loginAddr) target.logins.push_back({ device.slave, one.loginAddr, one.loginSize });
		for (RegDef def : one.regs) {
			def.slave = device.slave;
			def.name = busRegisterName(device.name, def.name);
			map.regs << def;
		}
		target.deviceMaps << one;
		target.deviceNames << device.name;
	}
	return true;
}

/* ------------------------------------------------------ the link, blocking */

/* A link and a master, used one request at a time: each call waits for its answer. */
class Session {
public:
	/* logins: the devices of a bus (Target::logins); none: the map's own login register, if it has one */
	bool open(const Args &a, const DeviceMap *map, const QVector<DeviceLogin> &logins = {}) {
		if (!a.tcp.isEmpty()) {
			const int colon = int(a.tcp.lastIndexOf(QLatin1Char(':')));
			if (colon <= 0) {
				err(QStringLiteral("--tcp HOST:PORT"));
				return false;
			}
			link_ = std::make_unique<evre::TcpLink>(a.tcp.left(colon), quint16(a.tcp.mid(colon + 1).toUInt()));
		} else if (!a.serial.isEmpty()) {
			const QStringList parts = a.serial.split(QLatin1Char(':'));
			link_ = std::make_unique<evre::SerialLink>(parts[0], parts.size() > 1 ? parts[1].toInt() : 115200);
		} else {
			err(QStringLiteral("a link is needed: --tcp HOST:PORT or --serial PORT[:BAUD]"));
			return false;
		}
		const int timeout = a.timeoutMs > 0 ? a.timeoutMs
				: map && map->protocol.timeoutMs > 0 ? map->protocol.timeoutMs : 1000;
		master_.setSlave(uint8_t(a.slave >= 0 ? a.slave : map ? map->slave : 1));
		master_.setTimeoutMs(timeout);
		master_.setInFlight(1);
		master_.setLink(link_.get());
		QEventLoop loop;
		bool opened = false;
		QString why;
		QObject::connect(link_.get(), &evre::Link::opened, &loop, [&] {
			opened = true;
			loop.quit();
		});
		QObject::connect(link_.get(), &evre::Link::closed, &loop, [&](const QString &reason) {
			why = reason;
			loop.quit();
		});
		QTimer::singleShot(std::max(3000, timeout), &loop, &QEventLoop::quit);
		link_->open();
		loop.exec();
		if (!opened) {
			err(QStringLiteral("%1: %2").arg(link_->describe(), why.isEmpty() ? QStringLiteral("no connection") : why));
			return false;
		}
		/* the login first, as EVRe Studio sends it: every device of a bus */
		const QByteArray token = qgetenv("EVRE_TOKEN");
		if (token.isEmpty()) return true;
		for (const DeviceLogin &login : logins) {
			const evre::Result r = write(login.addr, encodeLoginToken(QString::fromUtf8(token), login.size), login.slave);
			if (!r.ok) {
				err(QStringLiteral("slave %1: token refused: %2").arg(login.slave).arg(r.message));
				return false;
			}
		}
		if (logins.isEmpty() && map && map->loginAddr) {
			const evre::Result r = write(map->loginAddr, encodeLoginToken(QString::fromUtf8(token), map->loginSize));
			if (!r.ok) {
				err(QStringLiteral("token refused: %1").arg(r.message));
				return false;
			}
		}
		return true;
	}

	/* slave: a device of a bus (RegDef::slave); 0: the session's (--slave, else the map's) */
	evre::Result read(uint16_t addr, uint16_t count, uint8_t slave = 0) {
		evre::Result result;
		QEventLoop loop;
		master_.readFrom(requestSlave(slave, master_.slave()), addr, count, [&](const evre::Result &r) {
			result = r;
			loop.quit();
		});
		loop.exec();
		return result;
	}

	evre::Result write(uint16_t addr, const QByteArray &bytes, uint8_t slave = 0) {
		evre::Result result;
		QEventLoop loop;
		master_.writeTo(requestSlave(slave, master_.slave()), addr, bytes, [&](const evre::Result &r) {
			result = r;
			loop.quit();
		});
		loop.exec();
		return result;
	}

	/* to every device at once (slave 0): a WRITE nobody answers, done once it is sent (often before this returns) */
	evre::Result broadcast(uint16_t addr, const QByteArray &bytes) {
		evre::Result result;
		bool done = false;
		QEventLoop loop;
		master_.writeNoAckTo(evre::BROADCAST, addr, bytes, [&](const evre::Result &r) {
			result = r;
			done = true;
			loop.quit();
		});
		if (!done) loop.exec();
		return result;
	}

	QString describe() const { return link_ ? link_->describe() : QString(); }
	evre::Master &master() { return master_; }
	evre::Link *link() { return link_.get(); }

private:
	std::unique_ptr<evre::Link> link_;
	evre::Master master_;
};

/* a register by name (any case) or by address (0x..); nullptr if the map has none */
const RegDef *findRegister(const DeviceMap &map, const QString &key) {
	if (key.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
		bool ok = false;
		const uint addr = parseAddress(key, &ok);
		for (const RegDef &def : map.regs)
			if (ok && def.addr == addr) return &def;
		return nullptr;
	}
	for (const RegDef &def : map.regs)
		if (def.name.compare(key, Qt::CaseInsensitive) == 0) return &def;
	return nullptr;
}

/* one register's value as a line, or as JSON */
QJsonObject valueJson(const RegDef &def, const QByteArray &raw) {
	QJsonObject o{ { QStringLiteral("name"), def.name }, { QStringLiteral("addr"), addrText(def.addr) },
		{ QStringLiteral("raw"), QString::fromLatin1(raw.toHex()) } };
	if (def.isNumeric()) {
		const double v = decodeNumber(def, raw);
		o.insert(QStringLiteral("value"), std::isfinite(v) ? QJsonValue(v) : QJsonValue());
	}
	if (!def.unit.isEmpty()) o.insert(QStringLiteral("unit"), def.unit);
	const QString decoded = formatDecoded(def, raw);
	if (!decoded.isEmpty()) o.insert(QStringLiteral("decoded"), decoded);
	return o;
}

QString valueLine(const RegDef &def, const QByteArray &raw) {
	QString line = QStringLiteral("%1  %2  %3").arg(addrText(def.addr), def.name.leftJustified(20), formatValue(def, raw));
	if (!def.unit.isEmpty() && def.isNumeric()) line += QLatin1Char(' ') + def.unit;
	const QString decoded = formatDecoded(def, raw);
	if (!decoded.isEmpty()) line += QStringLiteral("  (%1)").arg(decoded);
	return line;
}

/* the registers a dump reads, merged into blocks as EVRe Studio polls them (never across two devices) */
struct Block {
	uint8_t slave = 0;
	uint16_t addr = 0;
	int size = 0;
	QVector<const RegDef *> regs;
};

QVector<Block> blocksOf(const QVector<const RegDef *> &regs) {
	QVector<const RegDef *> sorted = regs;
	std::sort(sorted.begin(), sorted.end(), [](const RegDef *a, const RegDef *b) { return regKey(*a) < regKey(*b); });
	QVector<Block> blocks;
	for (const RegDef *def : sorted) {
		const bool joins = !blocks.isEmpty() && blocks.last().slave == def->slave && sameBank(def->addr, blocks.last().addr)
				&& int(def->addr) - (blocks.last().addr + blocks.last().size) <= MAX_BLOCK_GAP
				&& int(def->addr) + def->size - blocks.last().addr <= 255;
		if (!joins) blocks.push_back({ def->slave, def->addr, 0, {} });
		Block &b = blocks.last();
		b.regs << def;
		b.size = std::max(b.size, int(def->addr) + def->size - int(b.addr));
	}
	return blocks;
}

/* every register of `regs` read, in blocks; false if a block failed (its registers are left out) */
bool readRegisters(Session &session, const QVector<const RegDef *> &regs, QVector<QPair<const RegDef *, QByteArray>> &values) {
	bool all = true;
	for (const Block &block : blocksOf(regs)) {
		const evre::Result r = session.read(block.addr, uint16_t(block.size), block.slave);
		if (!r.ok) {
			/* a merged block the device refuses: one read per register */
			if (block.regs.size() > 1 && r.error) {
				for (const RegDef *def : block.regs) {
					const evre::Result one = session.read(def->addr, uint16_t(def->size), def->slave);
					if (one.ok) values.push_back({ def, one.data });
					else {
						err(QStringLiteral("%1: %2").arg(def->name, one.message));
						all = false;
					}
				}
				continue;
			}
			err(QStringLiteral("read %1 (%2 bytes): %3").arg(addrText(block.addr)).arg(block.size).arg(r.message));
			all = false;
			continue;
		}
		for (const RegDef *def : block.regs) values.push_back({ def, r.data.mid(def->addr - block.addr, def->size) });
	}
	return all;
}

/* --------------------------------------------------------------- commands */

int cmdValidate(const Args &a) {
	if (a.positional.isEmpty()) {
		err(QStringLiteral("validate: which map?"));
		return 2;
	}
	int errors = 0;
	for (const QString &file : a.positional) {
		DeviceMap map;
		if (!loadMap(file, map)) return 2;
		const QVector<MapIssue> issues = checkMap(map);
		int fileErrors = 0;
		for (const MapIssue &issue : issues) {
			fileErrors += issue.error;
			if (jsonOutput)
				outJson({ { QStringLiteral("file"), file }, { QStringLiteral("error"), issue.error },
						{ QStringLiteral("register"), issue.reg >= 0 ? map.regs[issue.reg].name : QString() },
						{ QStringLiteral("text"), issue.text } });
			else out(QStringLiteral("%1: %2: %3").arg(QFileInfo(file).fileName(), issue.error ? "error" : "warning", issue.text));
		}
		if (!jsonOutput)
			out(QStringLiteral("%1: %2 registers, %3 error(s), %4 warning(s)").arg(QFileInfo(file).fileName())
					.arg(map.regs.size()).arg(fileErrors).arg(issues.size() - fileErrors));
		errors += fileErrors;
	}
	return errors ? 1 : 0;
}

int cmdExport(const Args &a) {
	if (a.positional.size() != 1 || a.to.isEmpty()) {
		err(QStringLiteral("export MAP --to md|h|py|csv|table"));
		return 2;
	}
	DeviceMap map;
	if (!loadMap(a.positional[0], map)) return 2;
	ExportOptions options;
	options.source = QFileInfo(a.positional[0]).fileName();
	options.prefix = a.prefix;
	QByteArray text;
	if (a.to == QLatin1String("md")) text = exportMarkdown(map, options);
	else if (a.to == QLatin1String("h")) text = exportCHeader(map, options);
	else if (a.to == QLatin1String("py")) text = exportPython(map, options);
	else if (a.to == QLatin1String("csv")) text = exportCsv(map);
	else if (a.to == QLatin1String("table")) {
		QStringList problems;
		if (!exportDeviceTable(map, options, text, problems)) {
			err(QStringLiteral("%1: not a device table for the EVRe library:").arg(QFileInfo(a.positional[0]).fileName()));
			for (const QString &problem : problems) err(QStringLiteral("  ") + problem);
			return 1;
		}
	} else {
		err(QStringLiteral("--to md, h, py, csv or table"));
		return 2;
	}
	if (a.output.isEmpty()) {
		std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
		return 0;
	}
	QFile file(a.output);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text) != text.size()) {
		err(QStringLiteral("%1: %2").arg(a.output, file.errorString()));
		return 2;
	}
	return 0;
}

int cmdInfo(const Args &a) {
	DeviceMap map;
	if (!a.map.isEmpty() && !loadMap(a.map, map)) return 2;
	Session session;
	if (!session.open(a, a.map.isEmpty() ? nullptr : &map)) return 1;
	const evre::Result r = session.read(evre::DEVICE_ID, 6); /* DEVICE_ID, STATUS, CONFIG */
	if (!r.ok || r.data.size() < 6) {
		err(QStringLiteral("DEVICE_ID not read: %1").arg(r.message));
		return 1;
	}
	const auto *b = reinterpret_cast<const uint8_t *>(r.data.constData());
	const quint16 id = evre::littleEndian16(b), status = evre::littleEndian16(b + 2), config = evre::littleEndian16(b + 4);
	static const QPair<uint16_t, const char *> caps[] = { { evre::CAP_ERROR_FRAME, "error frames" },
		{ evre::CAP_BROADCAST, "broadcast" }, { evre::CAP_MSG, "messages" }, { evre::CAP_AUTO_SEND, "auto send" },
		{ evre::CAP_DFU, "DFU" }, { evre::CAP_STATIC, "static" } };
	QStringList has;
	for (const auto &cap : caps)
		if (status & cap.first) has << QLatin1String(cap.second);
	const bool mismatch = !a.map.isEmpty() && map.deviceId && map.deviceId != id;
	if (jsonOutput) {
		QJsonArray streams;
		for (const StreamDef &stream : map.streams)
			streams.append(QJsonDocument::fromJson(streamToJson(stream)).object());
		QJsonObject info{ { QStringLiteral("link"), session.describe() }, { QStringLiteral("device_id"), addrText(id) },
				{ QStringLiteral("revision"), status & evre::STATUS_REVISION_MASK }, { QStringLiteral("capabilities"), QJsonArray::fromStringList(has) },
				{ QStringLiteral("config"), addrText(config) }, { QStringLiteral("map_matches"), !mismatch } };
		if (!map.streams.isEmpty()) info.insert(QStringLiteral("streams"), streams);
		outJson(info);
	} else {
		out(QStringLiteral("link          %1").arg(session.describe()));
		out(QStringLiteral("device ID     %1%2").arg(addrText(id), mismatch ? QStringLiteral("  (the map is for %1)").arg(addrText(map.deviceId)) : QString()));
		out(QStringLiteral("protocol rev  %1").arg(status & evre::STATUS_REVISION_MASK));
		out(QStringLiteral("capabilities  %1").arg(has.isEmpty() ? QStringLiteral("-") : has.join(QStringLiteral(", "))));
		out(QStringLiteral("config        %1").arg(addrText(config)));
		/* the map's fast streams (Fast EVRe): where their blocks come from, how fast, what a record holds */
		for (const StreamDef &stream : map.streams) {
			QStringList channels;
			for (const StreamChannel &c : stream.channels)
				channels << c.name + (c.unit.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(c.unit))
						+ QLatin1Char(' ') + typeName(c.type);
			out(QStringLiteral("fast stream   %1: window %2, %3 bytes, %4 records/s, %5 records a block; %6%7")
					.arg(stream.name, addrText(stream.addr)).arg(stream.size).arg(stream.rate)
					.arg(stream.recordsPerBlock()).arg(channels.join(QStringLiteral(", ")),
							stream.enable.isEmpty() ? QString() : QStringLiteral("; enable %1").arg(stream.enable)));
		}
	}
	return mismatch ? 1 : 0;
}

int cmdRead(const Args &a) {
	if (!a.addr.isEmpty()) {
		/* raw bytes: no map needed */
		bool ok = false;
		const uint addr = parseAddress(a.addr, &ok);
		const int count = a.count > 0 ? a.count : 1;
		if (!ok || addr > 0xFFFF || count > 0xFFFF) {
			err(QStringLiteral("--addr 0x0000 ... 0xFFFF, --count 1 ... 65535"));
			return 2;
		}
		DeviceMap map;
		if (!a.map.isEmpty() && !loadMap(a.map, map)) return 2;
		Session session;
		if (!session.open(a, a.map.isEmpty() ? nullptr : &map)) return 1;
		const evre::Result r = session.read(uint16_t(addr), uint16_t(count));
		if (!r.ok) {
			err(r.message);
			return 1;
		}
		if (jsonOutput) outJson({ { QStringLiteral("addr"), addrText(uint16_t(addr)) }, { QStringLiteral("hex"), QString::fromLatin1(r.data.toHex()) } });
		else out(QString::fromLatin1(r.data.toHex(' ').toUpper()));
		return 0;
	}
	if (!hasTarget(a) || a.positional.isEmpty()) {
		err(QStringLiteral("read LINK --map MAP NAME...  (or --addr 0x.. --count N)"));
		return 2;
	}
	Target target;
	if (!loadTarget(a, target)) return 2;
	const DeviceMap &map = target.map;
	QVector<const RegDef *> regs;
	for (const QString &key : a.positional) {
		const RegDef *def = findRegister(map, key);
		if (!def) {
			err(QStringLiteral("no register %1 in %2").arg(key, targetName(a)));
			return 2;
		}
		regs << def;
	}
	Session session;
	if (!session.open(a, &map, target.logins)) return 1;
	QVector<QPair<const RegDef *, QByteArray>> values;
	const bool all = readRegisters(session, regs, values);
	for (const RegDef *def : regs)
		for (const auto &value : values)
			if (value.first == def) jsonOutput ? outJson(valueJson(*def, value.second)) : out(valueLine(*def, value.second));
	return all ? 0 : 1;
}

int cmdDump(const Args &a) {
	if (!hasTarget(a)) {
		err(QStringLiteral("dump LINK --map MAP"));
		return 2;
	}
	Target target;
	if (!loadTarget(a, target)) return 2;
	const DeviceMap &map = target.map;
	QVector<const RegDef *> regs;
	for (const RegDef &def : map.regs)
		if (isPollable(def)) regs << &def;
	Session session;
	if (!session.open(a, &map, target.logins)) return 1;
	QVector<QPair<const RegDef *, QByteArray>> values;
	const bool all = readRegisters(session, regs, values);
	std::sort(values.begin(), values.end(), [](const auto &x, const auto &y) { return x.first->addr < y.first->addr; });
	for (const auto &value : values) jsonOutput ? outJson(valueJson(*value.first, value.second)) : out(valueLine(*value.first, value.second));
	return all ? 0 : 1;
}

int cmdWatch(const Args &a) {
	if (!hasTarget(a) || a.positional.isEmpty()) {
		err(QStringLiteral("watch LINK --map MAP NAME... [--interval MS] [--count N]"));
		return 2;
	}
	Target target;
	if (!loadTarget(a, target)) return 2;
	const DeviceMap &map = target.map;
	QVector<const RegDef *> regs;
	for (const QString &key : a.positional) {
		const RegDef *def = findRegister(map, key);
		if (!def || !def->isNumeric()) {
			err(QStringLiteral("no numeric register %1 in %2").arg(key, targetName(a)));
			return 2;
		}
		regs << def;
	}
	Session session;
	if (!session.open(a, &map, target.logins)) return 1;
	if (!jsonOutput) {
		QStringList head{ QStringLiteral("time_s") };
		for (const RegDef *def : regs) head << def->name + (def->unit.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(def->unit));
		out(head.join(QLatin1Char(',')));
	}
	QElapsedTimer clock;
	clock.start();
	for (int n = 0; a.count < 0 || n < a.count; n++) {
		const qint64 due = qint64(n * a.intervalMs);
		while (clock.elapsed() < due) {
			QEventLoop wait;
			QTimer::singleShot(int(due - clock.elapsed()), &wait, &QEventLoop::quit);
			wait.exec();
		}
		QVector<QPair<const RegDef *, QByteArray>> values;
		readRegisters(session, regs, values);
		const double t = clock.elapsed() / 1000.0;
		QJsonObject line{ { QStringLiteral("t"), t } };
		QStringList cells{ QString::number(t, 'f', 3) };
		for (const RegDef *def : regs) {
			QString cell;
			for (const auto &value : values)
				if (value.first == def) {
					cell = formatValue(*def, value.second);
					line.insert(def->name, decodeNumber(*def, value.second));
				}
			cells << cell;
		}
		jsonOutput ? outJson(line) : out(cells.join(QLatin1Char(',')));
	}
	return 0;
}

int cmdWrite(const Args &a) {
	if (!hasTarget(a) || a.positional.isEmpty()) {
		err(QStringLiteral("write LINK --map MAP NAME=VALUE... [--force]"));
		return 2;
	}
	Target target;
	if (!loadTarget(a, target)) return 2;
	const DeviceMap &map = target.map;
	/* everything checked and encoded before the first write: all or nothing */
	struct Write {
		const RegDef *def;
		QString text;
		QByteArray bytes;
	};
	QVector<Write> writes;
	for (const QString &item : a.positional) {
		const int eq = int(item.indexOf(QLatin1Char('=')));
		if (eq <= 0) {
			err(QStringLiteral("%1: NAME=VALUE").arg(item));
			return 2;
		}
		const RegDef *def = findRegister(map, item.left(eq));
		if (!def) {
			err(QStringLiteral("no register %1 in %2").arg(item.left(eq), targetName(a)));
			return 2;
		}
		if (!def->rw) {
			err(QStringLiteral("%1 is read-only").arg(def->name));
			return 1;
		}
		Write w{ def, item.mid(eq + 1), {} };
		QString why;
		if (!encodeValue(*def, w.text, w.bytes, why)) {
			err(QStringLiteral("%1: %2").arg(def->name, why));
			return 1;
		}
		const QString outside = def->isNumeric() ? limitProblem(*def, decodeNumber(*def, w.bytes)) : QString();
		if (!outside.isEmpty() && !a.force) {
			err(QStringLiteral("%1 = %2 is %3 (the map's limit; --force writes it anyway)").arg(def->name, w.text, outside));
			return 1;
		}
		if (def->danger && !a.force) {
			err(QStringLiteral("%1 is marked danger (it moves, powers or resets something): --force to write it").arg(def->name));
			return 1;
		}
		writes << w;
	}
	Session session;
	if (!session.open(a, &map, target.logins)) return 1;
	for (const Write &w : writes) {
		const evre::Result r = session.write(w.def->addr, w.bytes, w.def->slave);
		if (!r.ok) {
			err(QStringLiteral("%1 = %2: %3").arg(w.def->name, w.text, r.message));
			return 1;
		}
		/* what the device holds now */
		if (!w.def->readable) {
			jsonOutput ? outJson({ { QStringLiteral("name"), w.def->name }, { QStringLiteral("written"), w.text } })
					: out(QStringLiteral("%1 written (write-only: not read back)").arg(w.def->name));
			continue;
		}
		const evre::Result back = session.read(w.def->addr, uint16_t(w.def->size), w.def->slave);
		if (!back.ok) {
			err(QStringLiteral("%1 written, not read back: %2").arg(w.def->name, back.message));
			return 1;
		}
		jsonOutput ? outJson(valueJson(*w.def, back.data)) : out(valueLine(*w.def, back.data));
	}
	return 0;
}

/* One value to every device at once: a broadcast (slave 0) WRITE nobody answers, where the broadcast rule allows it
 * (model/bus_file.h), then the register read back from each device that has it. Exit 1 when one holds another value. */
int cmdBroadcast(const Args &a) {
	const QString item = a.positional.value(0);
	const int eq = int(item.indexOf(QLatin1Char('=')));
	if (!hasTarget(a) || a.positional.size() != 1 || eq <= 0) {
		err(QStringLiteral("broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]"));
		return 2;
	}
	Target target;
	if (!loadTarget(a, target)) return 2;
	const DeviceMap &map = target.map;
	/* by a device's name for it (D1_SPEED) or the map's own (SPEED): model/bus_file.h, broadcastNames */
	const RegDef *def = nullptr;
	for (const QString &name : broadcastNames(target.deviceNames, item.left(eq)))
		if (!def) def = findRegister(map, name);
	if (!def) {
		err(QStringLiteral("no register %1 in %2").arg(item.left(eq), targetName(a)));
		return 2;
	}
	if (!def->rw) {
		err(QStringLiteral("%1 is read-only").arg(def->name));
		return 1;
	}
	const QString text = item.mid(eq + 1);
	QByteArray bytes;
	QString why;
	if (!encodeValue(*def, text, bytes, why)) {
		err(QStringLiteral("%1: %2").arg(def->name, why));
		return 1;
	}
	QVector<const DeviceMap *> maps;
	if (target.deviceMaps.isEmpty()) maps << &map;
	for (const DeviceMap &one : std::as_const(target.deviceMaps)) maps << &one;
	const QString refusal = broadcastRefusal(maps, def->addr, bytes);
	if (!refusal.isEmpty()) {
		err(QStringLiteral("no broadcast: %1").arg(refusal));
		return 1;
	}
	const QString outside = def->isNumeric() ? limitProblem(*def, decodeNumber(*def, bytes)) : QString();
	if (!outside.isEmpty() && !a.force) {
		err(QStringLiteral("%1 = %2 is %3 (the map's limit; --force sends it anyway)").arg(def->name, text, outside));
		return 1;
	}
	if (def->danger && !a.force) {
		err(QStringLiteral("%1 is marked danger (it moves, powers or resets something): --force to broadcast it").arg(def->name));
		return 1;
	}
	Session session;
	if (!session.open(a, &map, target.logins)) return 1;
	const evre::Result sent = session.broadcast(def->addr, bytes);
	if (!sent.ok) {
		err(sent.message);
		return 1;
	}
	/* no device answers a broadcast: each one read back */
	bool all = true;
	for (const RegDef &reg : std::as_const(map.regs)) {
		if (reg.addr != def->addr || !reg.readable) continue;
		const evre::Result back = session.read(reg.addr, uint16_t(reg.size), reg.slave);
		if (!back.ok) {
			err(QStringLiteral("%1: %2").arg(reg.name, back.message));
			all = false;
			continue;
		}
		if (back.data != bytes) all = false;
		jsonOutput ? outJson(valueJson(reg, back.data)) : out(valueLine(reg, back.data));
	}
	return all ? 0 : 1;
}

/* Does the device answer as its map says? Reads only, unless --writes: then each writable register is
 * written with the value it holds (nothing changes) and read back; danger registers only with --force. */
/* Ctrl+C during evre record: the stream is switched off and the file closed, not left running */
volatile std::sig_atomic_t interrupted = 0;

/* A fast stream (Fast EVRe) into a .evrs file (model/fast_recording.h): its enable register written 1, every block
 * from the device's slave at the stream's window checked by the block's rules and written as it came, a time mark
 * before the first block of every start and about one a second, CONFIG read every 100 ms meanwhile (the device's
 * host watchdog), then the enable written 0. Ends after --seconds, at Ctrl+C, or when no block came in 2 s. */
int cmdRecord(const Args &a) {
	if (a.map.isEmpty() || a.stream.isEmpty() || a.output.isEmpty()) {
		err(QStringLiteral("record LINK --map MAP --stream NAME -o FILE [--seconds S]"));
		return 2;
	}
	DeviceMap map;
	if (!loadMap(a.map, map)) return 2;
	const StreamDef *stream = nullptr;
	for (const StreamDef &s : map.streams)
		if (s.name.compare(a.stream, Qt::CaseInsensitive) == 0) stream = &s;
	if (!stream) {
		QStringList names;
		for (const StreamDef &s : map.streams) names << s.name;
		err(QStringLiteral("no stream %1 in %2 (%3)").arg(a.stream, a.map,
				names.isEmpty() ? QStringLiteral("it has none") : names.join(QStringLiteral(", "))));
		return 2;
	}
	for (const MapIssue &issue : checkMap(map))
		if (issue.error && issue.reg < 0 && issue.text.contains(stream->name)) {
			err(QStringLiteral("%1: %2").arg(a.map, issue.text));
			return 2;
		}
	const RegDef *enable = map.registerNamed(stream->enable);
	Session session;
	if (!session.open(a, &map)) return 1;
	/* the rate the device was set to, where the map names its register */
	double rate = stream->rate;
	if (const RegDef *rateReg = map.registerNamed(stream->rateReg)) {
		const evre::Result r = session.read(rateReg->addr, uint16_t(rateReg->size));
		if (r.ok && r.data.size() == rateReg->size && decodeNumber(*rateReg, r.data) > 0) rate = decodeNumber(*rateReg, r.data);
	}
	fast::FastStream state;
	state.reset(*stream, rate);
	fast::RecordingWriter writer;
	QString why;
	if (!writer.open(a.output, map.device, *stream, QDateTime::currentDateTime(), why)) {
		err(QStringLiteral("%1: %2").arg(a.output, why));
		return 2;
	}
	evre::Master &master = session.master();
	QElapsedTimer clock;
	clock.start();
	bool writeFailed = false;
	QObject::connect(&master, &evre::Master::unsolicited, &master, [&](const evre::Frame &frame) {
		if (frame.fn != evre::READ_RESP || frame.slave != master.slave() || frame.addr != stream->addr
				|| frame.data.size() != frame.cnt)
			return;
		fast::BlockTaken taken;
		if (state.take(frame.data, double(clock.nsecsElapsed()) / 1e9, taken) != fast::BlockCheck::Ok) return;
		if (taken.newMark) writeFailed |= !writer.mark(state.clock().mark().record, state.clock().mark().time);
		writeFailed |= !writer.block(frame.data);
	});
	/* the host watchdog: CONFIG every 100 ms, one under way at most */
	bool heartbeatPending = false;
	QTimer heartbeat;
	heartbeat.setInterval(100);
	QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] {
		if (heartbeatPending) return;
		heartbeatPending = true;
		master.readFrom(master.slave(), evre::CONFIG, 2, [&](const evre::Result &) { heartbeatPending = false; });
	});
	heartbeat.start();
	if (enable) {
		QByteArray one;
		encodeValue(*enable, QStringLiteral("1"), one, why);
		const evre::Result r = session.write(enable->addr, one);
		if (!r.ok) {
			err(QStringLiteral("%1 not written: %2").arg(enable->name, r.message));
			return 1;
		}
	}
	std::signal(SIGINT, [](int) { interrupted = 1; });
	QEventLoop loop;
	QString stopped;
	QTimer watch;
	watch.setInterval(50);
	QObject::connect(&watch, &QTimer::timeout, &loop, [&] {
		const double elapsed = double(clock.elapsed()) / 1000.0;
		if (interrupted) stopped = QStringLiteral("interrupted");
		else if (a.seconds > 0 && elapsed >= a.seconds) stopped = QStringLiteral("done");
		else if (state.blocks == 0 && elapsed >= 2.0) stopped = QStringLiteral("no block came in 2 s");
		else if (writeFailed) stopped = QStringLiteral("%1: %2").arg(a.output, writer.errorString());
		else if (session.link() && !session.link()->isOpen()) stopped = QStringLiteral("the link closed");
		if (!stopped.isEmpty()) loop.quit();
	});
	watch.start();
	loop.exec();
	heartbeat.stop();
	if (enable && session.link() && session.link()->isOpen()) {
		QByteArray zero;
		encodeValue(*enable, QStringLiteral("0"), zero, why);
		session.write(enable->addr, zero);
	}
	writer.close();
	const double seconds = double(clock.elapsed()) / 1000.0;
	const bool ok = state.blocks > 0 && !writeFailed && stopped != QLatin1String("the link closed");
	if (jsonOutput) {
		outJson({ { QStringLiteral("file"), a.output }, { QStringLiteral("stream"), stream->name },
				{ QStringLiteral("records"), double(state.records) }, { QStringLiteral("blocks"), double(state.blocks) },
				{ QStringLiteral("lost"), double(state.lost) }, { QStringLiteral("bad_blocks"), double(state.badBlocks) },
				{ QStringLiteral("newer_blocks"), double(state.newerBlocks) }, { QStringLiteral("starts"), double(state.starts) },
				{ QStringLiteral("seconds"), seconds }, { QStringLiteral("rate"), state.clock().rate() },
				{ QStringLiteral("ppm"), state.clock().ppm() }, { QStringLiteral("stopped"), stopped } });
	} else {
		out(QStringLiteral("%1: %2 records in %3 blocks, %4 lost, %5 bad, %6 of a newer kind, %7 s; %8 records/s (%9 ppm); %10")
				.arg(stream->name).arg(state.records).arg(state.blocks).arg(state.lost).arg(state.badBlocks)
				.arg(state.newerBlocks).arg(seconds, 0, 'f', 1).arg(state.clock().rate(), 0, 'f', 1)
				.arg(state.clock().ppm(), 0, 'f', 0).arg(stopped));
	}
	if (!stopped.isEmpty() && stopped != QLatin1String("done") && stopped != QLatin1String("interrupted")) err(stopped);
	return ok ? 0 : 1;
}

int cmdCheck(const Args &a) {
	if (a.map.isEmpty()) {
		err(QStringLiteral("check LINK --map MAP [--writes] [--force]"));
		return 2;
	}
	DeviceMap map;
	if (!loadMap(a.map, map)) return 2;
	Session session;
	if (!session.open(a, &map)) return 1;
	int fails = 0, warnings = 0, passes = 0;
	auto report = [&](const char *level, const QString &what, const QString &text) {
		if (*level == 'F') fails++;
		else if (*level == 'W') warnings++;
		else passes++;
		if (jsonOutput) outJson({ { QStringLiteral("result"), QLatin1String(level) }, { QStringLiteral("register"), what },
				{ QStringLiteral("text"), text } });
		else out(QStringLiteral("%1  %2  %3").arg(QLatin1String(level), what.leftJustified(20), text));
	};

	/* the protocol bank */
	const evre::Result id = session.read(evre::DEVICE_ID, 4);
	if (!id.ok || id.data.size() < 4) {
		report("FAIL", QStringLiteral("DEVICE_ID"), QStringLiteral("not read: %1").arg(id.message));
	} else {
		const auto *b = reinterpret_cast<const uint8_t *>(id.data.constData());
		const quint16 deviceId = evre::littleEndian16(b), status = evre::littleEndian16(b + 2);
		if (map.deviceId && deviceId != map.deviceId)
			report("FAIL", QStringLiteral("DEVICE_ID"), QStringLiteral("%1, the map is for %2").arg(addrText(deviceId), addrText(map.deviceId)));
		else report("PASS", QStringLiteral("DEVICE_ID"), addrText(deviceId) + (map.deviceId ? QString() : QStringLiteral(" (the map gives none)")));
		const int revision = status & evre::STATUS_REVISION_MASK;
		if (revision != 1) report("WARN", QStringLiteral("STATUS"), QStringLiteral("protocol revision %1, not 1").arg(revision));
	}

	for (const RegDef &def : map.regs) {
		if (def.addr >= evre::RESERVED_FIRST && def.addr < evre::RESERVED_END) continue; /* the protocol bank: above */
		const evre::Result r = session.read(def.addr, uint16_t(def.size));
		if (!def.readable) {
			/* write-only: a read should be refused */
			if (r.ok) report("WARN", def.name, QStringLiteral("write-only in the map, but the device answers a read"));
			else report("PASS", def.name, QStringLiteral("write-only: a read is refused (%1)").arg(r.message));
			continue;
		}
		if (!r.ok) {
			report("FAIL", def.name, QStringLiteral("not read at %1: %2").arg(addrText(def.addr), r.message));
			continue;
		}
		if (r.data.size() != def.size) {
			report("FAIL", def.name, QStringLiteral("%1 bytes answered, %2 expected").arg(r.data.size()).arg(def.size));
			continue;
		}
		QString text = formatValue(def, r.data) + (def.unit.isEmpty() || !def.isNumeric() ? QString() : QLatin1Char(' ') + def.unit);
		const char *level = "PASS";
		if (def.isNumeric()) {
			const double shown = decodeNumber(def, r.data);
			const QString outside = limitProblem(def, shown);
			if (!outside.isEmpty()) {
				level = "WARN";
				text += QStringLiteral(": ") + outside;
			}
			if (!def.enumValues.isEmpty() && !def.enumValues.contains(decodeRaw(def, r.data)) && specialName(def, shown).isEmpty()) {
				level = "WARN";
				text += QStringLiteral(": a value the map has no name for");
			}
		}
		if (a.writes && def.rw && def.write == WriteKind::Normal) {
			if (def.danger && !a.force) {
				text += QStringLiteral("; not written (danger: --force)");
			} else {
				/* the value it holds, written back: nothing changes */
				const evre::Result w = session.write(def.addr, r.data);
				const evre::Result back = w.ok ? session.read(def.addr, uint16_t(def.size)) : evre::Result();
				if (!w.ok) {
					level = "FAIL";
					text += QStringLiteral("; read-write in the map, but a write is refused: %1").arg(w.message);
				} else if (!back.ok || back.data != r.data) {
					level = "WARN";
					text += QStringLiteral("; written back, then read %1").arg(back.ok ? formatValue(def, back.data) : back.message);
				} else {
					text += QStringLiteral("; writable");
				}
			}
		}
		report(level, def.name, text);
	}
	if (!jsonOutput)
		out(QStringLiteral("%1 passed, %2 warning(s), %3 failed%4").arg(passes).arg(warnings).arg(fails)
				.arg(a.writes ? QString() : QStringLiteral(" (reads only; --writes also checks the writable registers)")));
	return fails ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	Args a;
	QString why;
	const QStringList argv2 = app.arguments();
	if (argv2.size() >= 2 && (argv2[1] == QLatin1String("--help") || argv2[1] == QLatin1String("-h") || argv2[1] == QLatin1String("help"))) {
		out(QString::fromUtf8(USAGE));
		return 0;
	}
	if (!parseArgs(argv2, a, why)) {
		err(why);
		std::fputs(USAGE, stderr);
		std::fputc('\n', stderr);
		return 2;
	}
	if (a.command == QLatin1String("validate")) return cmdValidate(a);
	if (a.command == QLatin1String("export")) return cmdExport(a);
	if (a.command == QLatin1String("info")) return cmdInfo(a);
	if (a.command == QLatin1String("read")) return cmdRead(a);
	if (a.command == QLatin1String("dump")) return cmdDump(a);
	if (a.command == QLatin1String("watch")) return cmdWatch(a);
	if (a.command == QLatin1String("write")) return cmdWrite(a);
	if (a.command == QLatin1String("check")) return cmdCheck(a);
	if (a.command == QLatin1String("broadcast")) return cmdBroadcast(a);
	if (a.command == QLatin1String("record")) return cmdRecord(a);
	err(QStringLiteral("unknown command %1").arg(a.command));
	std::fputs(USAGE, stderr);
	std::fputc('\n', stderr);
	return 2;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* evre: the EVRe tools on the command line, on EVRe Studio's protocol core
 * (link, master, map, exports) without a window.
 *
 *   evre validate MAP...                   the map's checks; exit 1 if one has an error
 *   evre export MAP --to md|h|py|csv|table|guard   a specification, a C header, a Python module, a sheet,
 *                   [--prefix P] [-o FILE]  the device table for the EVRe library (C++), or EVRe
 *                   [--lib 1.0|1.1]         Guard's table (FILE.h and FILE.cpp). --lib 1.1: the table on
 *                   [--check]               ranges with the Guard's entries. --check: export again and
 *                                           compare with the files there; exit 1 if they differ (a build's
 *                                           check that its generated files are fresh)
 *   evre info  LINK [--map MAP]            DEVICE_ID, protocol revision, capabilities
 *   evre read  LINK --map MAP NAME...      values of registers (by name or 0x address)
 *   evre read  LINK --addr 0xD000 --count N    raw bytes, no map needed
 *   evre dump  LINK --map MAP              every readable register, once
 *   evre watch LINK --map MAP NAME... [--interval MS] [--count N]    a line per poll
 *   evre write LINK --map MAP NAME=VALUE... [--force]    written, then read back
 *   evre check LINK --map MAP [--writes] [--force]       does the device answer as its map says?
 *   evre broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]   to every device at once, then each read back
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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <memory>

#include "evre/master.h"
#include "evre/registers.h"
#include "model/bus_file.h"
#include "model/device_map.h"
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
  evre export MAP --to md|h|py|csv|table|guard [--prefix P] [-o FILE] [--lib 1.0|1.1] [--check]
  evre info  LINK [--map MAP]
  evre read  LINK --map MAP NAME...          (or --addr 0xD000 --count N)
  evre dump  LINK --map MAP
  evre watch LINK --map MAP NAME... [--interval MS] [--count N]
  evre write LINK --map MAP NAME=VALUE... [--force]
  evre check LINK --map MAP [--writes] [--force]
  evre broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]
read, dump, watch, write: --bus BUS in place of --map MAP (registers named D1_NAME, D2_NAME, ...)
LINK: --tcp HOST:PORT | --serial PORT[:BAUD]; also --slave N, --timeout MS, --json
the login token (if the map has a login register): EVRE_TOKEN)";

/* ---------------------------------------------------------- the arguments */

struct Args {
	QString command;
	QStringList positional;
	QString tcp, serial, map, bus, to, prefix, output, addr, lib;
	int count = -1, slave = -1, timeoutMs = -1;
	double intervalMs = 500;
	bool force = false, writes = false, check = false;
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
		else if (arg == QLatin1String("--lib")) ok = value(a.lib);
		else if (arg == QLatin1String("-o") || arg == QLatin1String("--output")) ok = value(a.output);
		else if (arg == QLatin1String("--addr")) ok = value(a.addr);
		else if (arg == QLatin1String("--count")) ok = number(a.count);
		else if (arg == QLatin1String("--slave")) ok = number(a.slave);
		else if (arg == QLatin1String("--timeout")) ok = number(a.timeoutMs);
		else if (arg == QLatin1String("--interval")) ok = number(a.intervalMs);
		else if (arg == QLatin1String("--force")) a.force = true;
		else if (arg == QLatin1String("--check")) a.check = true;
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

/* the generated files and their texts: written, or with --check compared with what is there (exit 1 if one
 * differs or is missing) */
int writeOrCheck(const Args &a, const QVector<QPair<QString, QByteArray>> &files) {
	if (a.check) {
		int stale = 0;
		for (const auto &file : files) {
			QFile in(file.first);
			if (!in.open(QIODevice::ReadOnly) || in.readAll() != file.second) {
				err(QStringLiteral("%1: not as the map exports it now: export it again").arg(file.first));
				stale++;
			}
		}
		if (!stale) out(QStringLiteral("%1: as the map exports it").arg(QFileInfo(files.first().first).fileName()
				+ (files.size() > 1 ? QStringLiteral(" and %1").arg(QFileInfo(files.last().first).fileName()) : QString())));
		return stale ? 1 : 0;
	}
	for (const auto &file : files) {
		QFile output(file.first);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) || output.write(file.second) != file.second.size()) {
			err(QStringLiteral("%1: %2").arg(file.first, output.errorString()));
			return 2;
		}
	}
	return 0;
}

int cmdExport(const Args &a) {
	if (a.positional.size() != 1 || a.to.isEmpty()) {
		err(QStringLiteral("export MAP --to md|h|py|csv|table|guard"));
		return 2;
	}
	/* the library the device table is for: 1.0 (a pointer per byte, the default) or 1.1 (ranges and the Guard) */
	if (!a.lib.isEmpty() && (a.to != QLatin1String("table") || (a.lib != QLatin1String("1.0") && a.lib != QLatin1String("1.1")))) {
		err(QStringLiteral("--lib 1.0 or 1.1, with --to table"));
		return 2;
	}
	DeviceMap map;
	if (!loadMap(a.positional[0], map)) return 2;
	ExportOptions options;
	options.source = QFileInfo(a.positional[0]).fileName();
	options.prefix = a.prefix;
	QByteArray text;
	if (a.to == QLatin1String("guard")) {
		/* FILE.h and FILE.cpp: -o FILE (an .h or .cpp on it is taken off), else MAP_guard beside where it runs */
		QString base = a.output.isEmpty() ? QFileInfo(a.positional[0]).completeBaseName() + QStringLiteral("_guard") : a.output;
		if (base.endsWith(QLatin1String(".h")) || base.endsWith(QLatin1String(".cpp"))) base = base.left(base.lastIndexOf(QLatin1Char('.')));
		QByteArray header, source;
		QStringList problems;
		if (!exportGuard(map, options, QFileInfo(base).fileName() + QStringLiteral(".h"), header, source, problems)) {
			err(QStringLiteral("%1: no EVRe Guard table:").arg(options.source));
			for (const QString &problem : problems) err(QStringLiteral("  ") + problem);
			return 1;
		}
		return writeOrCheck(a, { { base + QStringLiteral(".h"), header }, { base + QStringLiteral(".cpp"), source } });
	}
	if (a.to == QLatin1String("md")) text = exportMarkdown(map, options);
	else if (a.to == QLatin1String("h")) text = exportCHeader(map, options);
	else if (a.to == QLatin1String("py")) text = exportPython(map, options);
	else if (a.to == QLatin1String("csv")) text = exportCsv(map);
	else if (a.to == QLatin1String("table")) {
		QStringList problems;
		const bool lib11 = a.lib == QLatin1String("1.1");
		if (!(lib11 ? exportDeviceTable11(map, options, text, problems) : exportDeviceTable(map, options, text, problems))) {
			err(QStringLiteral("%1: not a device table for the EVRe library:").arg(QFileInfo(a.positional[0]).fileName()));
			for (const QString &problem : problems) err(QStringLiteral("  ") + problem);
			return 1;
		}
	} else {
		err(QStringLiteral("--to md, h, py, csv, table or guard"));
		return 2;
	}
	if (a.output.isEmpty()) {
		if (a.check) {
			err(QStringLiteral("--check needs -o FILE: the file to compare with"));
			return 2;
		}
		std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
		return 0;
	}
	return writeOrCheck(a, { { a.output, text } });
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
		outJson({ { QStringLiteral("link"), session.describe() }, { QStringLiteral("device_id"), addrText(id) },
				{ QStringLiteral("revision"), status & evre::STATUS_REVISION_MASK }, { QStringLiteral("capabilities"), QJsonArray::fromStringList(has) },
				{ QStringLiteral("config"), addrText(config) }, { QStringLiteral("map_matches"), !mismatch } });
	} else {
		out(QStringLiteral("link          %1").arg(session.describe()));
		out(QStringLiteral("device ID     %1%2").arg(addrText(id), mismatch ? QStringLiteral("  (the map is for %1)").arg(addrText(map.deviceId)) : QString()));
		out(QStringLiteral("protocol rev  %1").arg(status & evre::STATUS_REVISION_MASK));
		out(QStringLiteral("capabilities  %1").arg(has.isEmpty() ? QStringLiteral("-") : has.join(QStringLiteral(", "))));
		out(QStringLiteral("config        %1").arg(addrText(config)));
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
		const QString outside = writeProblem(*def, w.bytes);
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
	const QString outside = writeProblem(*def, bytes);
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
	err(QStringLiteral("unknown command %1").arg(a.command));
	std::fputs(USAGE, stderr);
	std::fputc('\n', stderr);
	return 2;
}

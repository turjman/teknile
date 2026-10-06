/* SPDX-License-Identifier: Apache-2.0 */
/* evre-sim: an EVRe device made from a map, over TCP. For trying a host, a
 * script or EVRe Studio before the hardware exists, and for tests.
 *
 *   evre-sim MAP [--port 1210] [--any] [--slave N] [--token T] [--require-login]
 *                [--strict] [--state FILE] [--verbose]
 *
 * The device does what its map says:
 *   - every register starts at its "default" (else 0); DEVICE_ID is the map's,
 *     STATUS says protocol revision 1; the protocol's bank (0xA000...) is never
 *     moved or reset, even when the map lists its registers
 *   - read-only numbers move: floats as slow sine waves, integers as slow
 *     triangles, both inside min .. max when the map gives them; a u32 in "ms"
 *     counts the milliseconds since the start. Registers with value names or
 *     fields stay as they are.
 *   - "access": "wo" registers are written, never read: a read is refused
 *     (ERROR_RESP 3, permission denied); "ro" registers refuse writes the same way
 *   - "write": "action": the value written is held 200 ms, then the register
 *     reads back idle (its default, else 0)
 *   - "write": "w1c", and fields with "access": "w1c": a 1 written clears that
 *     bit, a 0 leaves it. Such bits start set, as a latched fault would be.
 *   - a field with "access": "ro" in a writable register keeps the device's bits
 *   - --strict: the device answers as one with EVRe Guard's register checks
 *     would: a value past min or max (a special value aside), NaN or an
 *     infinity, a value outside a "closed" set, a bit no field covers set on a
 *     "reserved_zero" register is refused with 15 (value refused), a write of
 *     part of a number with 3; a register with "past_limits": "clamp" takes a
 *     value past its limits and clamps it, as such a device's main loop does
 *   - the login (a map with "login"): a write of the whole login register is
 *     accepted when it holds the token (--token, default "example-token"), else
 *     refused (3). --require-login: on a connection that has not logged in,
 *     every other write is refused (3).
 *   - --state FILE: the "persist" registers are kept in FILE (JSON) across
 *     restarts of the simulator
 *   - addresses in no register are refused (ERROR_RESP 4, offset out of range)
 *   - its slave address is the map's "slave" (--slave N: another); a frame for
 *     another slave gets no answer. A broadcast (slave 0) WRITE is taken as a
 *     WRITE to it, and not answered; any other broadcast is dropped.
 *
 * It listens on 127.0.0.1 (--any: every interface). --verbose prints each write.
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "evre/frame.h"
#include "evre/registers.h"
#include "model/device_map.h"

namespace {

constexpr int MEMORY_SIZE = 0x10000;
constexpr char PERMISSION_DENIED = 3;
constexpr char OFFSET_OUT_OF_RANGE = 4;
constexpr char VALUE_REFUSED = 15; /* EVRe Guard's register checks: a value the device does not take */
constexpr int ACTION_HOLD_MS = 200;

/* the protocol's own bank (DEVICE_ID, STATUS, CONFIG, messages): what the protocol says, not moved or reset */
bool protocolBank(const RegDef &r) { return r.addr >= evre::RESERVED_FIRST && r.addr < evre::RESERVED_END; }

struct Options {
	quint16 port = 1210;
	int slave = -1; /* -1: the map's */
	bool any = false, requireLogin = false, strict = false, verbose = false;
	QString token = QStringLiteral("example-token"), stateFile;
};

/* each connection's own state */
struct Connection {
	bool loggedIn = false;
};

class Simulator : public QObject {
public:
	Simulator(const DeviceMap &map, const Options &options)
		: map_(map), options_(options), slave_(uint8_t(options.slave >= 0 ? options.slave : map.slave)) {
		for (const RegDef &r : map.regs)
			for (int i = 0; i < r.size && r.addr + i < MEMORY_SIZE; i++) covered_[size_t(r.addr + i)] = 1;
		for (const RegDef &r : map.regs) byAddress_ << &r;
		std::stable_sort(byAddress_.begin(), byAddress_.end(), [](const RegDef *a, const RegDef *b) { return a->addr < b->addr; });
		if (map.loginAddr) {
			for (int i = 0; i < map.loginSize && map.loginAddr + i < MEMORY_SIZE; i++) covered_[size_t(map.loginAddr + i)] = 1;
			token_ = encodeLoginToken(options.token, map.loginSize);
		}
		for (int i = 0; i < 6; i++) covered_[size_t(evre::DEVICE_ID + i)] = 1; /* DEVICE_ID, STATUS, CONFIG */
		put16(evre::DEVICE_ID, map.deviceId);
		put16(evre::STATUS, 0x0001); /* protocol revision 1, no optional capabilities */
		for (const RegDef &r : map.regs)
			if (!protocolBank(r)) setIdle(r);
		for (const RegDef &r : map.regs)
			if (!protocolBank(r)) setLatchedBits(r);
		loadState();
		clock_.start();
	}

	uint8_t slave() const { return slave_; }

	/* a frame heard on the link: this device's, a broadcast (a WRITE taken, never answered) or another's (silence) */
	QByteArray hear(const evre::Frame &request, Connection &connection) {
		if (request.slave == evre::BROADCAST) {
			if (request.fn == evre::WRITE) {
				evre::Frame own = request;
				own.slave = slave_;
				answer(own, connection);
			}
			return {};
		}
		return request.slave == slave_ ? answer(request, connection) : QByteArray();
	}

private:
	QByteArray answer(const evre::Frame &request, Connection &connection) {
		const uint16_t addr = request.addr, count = request.cnt;
		const bool ack = request.fn != evre::WRITE;
		auto refuse = [&](char code) {
			return ack ? evre::build(request.slave, evre::ERROR_RESP, addr, count, QByteArray(1, code)) : QByteArray();
		};
		if (!covered(addr, count)) return refuse(OFFSET_OUT_OF_RANGE);
		if (request.fn == evre::READ) {
			/* a write-only register is never read */
			for (const RegDef &r : map_.regs)
				if (!r.readable && overlaps(r, addr, count)) return refuse(PERMISSION_DENIED);
			animate();
			return evre::build(request.slave, evre::READ_RESP, addr, count,
					QByteArray(reinterpret_cast<const char *>(&memory_[addr]), count));
		}
		if (request.fn != evre::WRITE && request.fn != evre::WRITE_ACK) return refuse(2);
		if (touchesLogin(addr, count)) return logIn(request, connection);
		if (options_.requireLogin && map_.loginAddr && !connection.loggedIn) return refuse(PERMISSION_DENIED);
		/* every register the write touches must take it, or none is written; with --strict in address order, the
		 * first one refused gives the code, as EVRe Guard's walk does */
		QByteArray data = request.data.left(count);
		for (const RegDef *rp : byAddress_) {
			const RegDef &r = *rp;
			if (!overlaps(r, addr, count)) continue;
			if (!r.rw) return refuse(PERMISSION_DENIED);
			if (!options_.strict || !r.isNumeric()) continue;
			if (int(r.addr) < addr || r.addr + r.size > addr + count) return refuse(PERMISSION_DENIED); /* part of a number */
			const QByteArray bytes = data.mid(r.addr - addr, r.size);
			if (!std::isfinite(decodeNumber(r, bytes)) || !writeProblem(r, bytes).isEmpty()) return refuse(VALUE_REFUSED);
		}
		for (int i = 0; i < data.size(); i++) writeByte(uint16_t(addr + i), uint8_t(data[i]));
		if (options_.strict) clampWritten(addr, count);
		for (const RegDef &r : map_.regs) {
			if (!overlaps(r, addr, count)) continue;
			if (options_.verbose) {
				const QByteArray now(reinterpret_cast<const char *>(&memory_[r.addr]), r.size);
				std::printf("write %s = %s\n", qPrintable(r.name), qPrintable(formatValue(r, now)));
				std::fflush(stdout);
			}
			if (r.write == WriteKind::Action) scheduleIdle(r);
			if (r.persist) saveState();
		}
		return ack ? evre::build(request.slave, evre::WRITE_ACK_RESP, addr, count) : QByteArray();
	}

	/* ---- memory */

	void put16(int addr, uint16_t value) {
		memory_[size_t(addr)] = uint8_t(value);
		memory_[size_t(addr + 1)] = uint8_t(value >> 8);
	}

	bool covered(uint16_t addr, uint16_t count) const {
		if (count == 0 || int(addr) + count > MEMORY_SIZE) return false;
		for (int i = 0; i < count; i++)
			if (!covered_[size_t(addr + i)]) return false;
		return true;
	}

	static bool overlaps(const RegDef &r, uint16_t addr, uint16_t count) {
		return int(r.addr) < addr + count && int(addr) < r.addr + r.size;
	}

	/* the register that holds a byte, or nullptr */
	const RegDef *registerAt(uint16_t addr) const {
		for (const RegDef &r : map_.regs)
			if (addr >= r.addr && addr < r.addr + r.size) return &r;
		return nullptr;
	}

	/* one byte of a write, as the register takes it: w1c bits cleared by a 1, ro field bits kept */
	/* a register that clamps ("past_limits": "clamp"), written past its limits: set to the limit it passed */
	void clampWritten(uint16_t addr, uint16_t count) {
		for (const RegDef &r : map_.regs) {
			if (!r.clamps || !r.isNumeric() || int(r.addr) < addr || r.addr + r.size > addr + count) continue;
			const double shown = decodeNumber(r, QByteArray(reinterpret_cast<const char *>(&memory_[r.addr]), r.size));
			const bool low = r.hasMin() && shown < r.min, high = r.hasMax() && shown > r.max;
			if (!specialName(r, shown).isEmpty() || (!low && !high)) continue;
			QByteArray bytes;
			QString why;
			if (encodeValue(r, QString::number(low ? r.min : r.max, 'g', 17), bytes, why))
				for (int i = 0; i < bytes.size(); i++) writeByte(uint16_t(r.addr + i), uint8_t(bytes[i]));
		}
	}

	void writeByte(uint16_t addr, uint8_t value) {
		const RegDef *r = registerAt(addr);
		uint8_t &cell = memory_[addr];
		if (!r || !r->isNumeric()) {
			cell = value;
			return;
		}
		const int shift = 8 * (addr - r->addr);
		uint8_t w1c = r->write == WriteKind::WriteOneToClear ? 0xFF : 0, keep = 0;
		for (const BitField &f : r->fields) {
			const quint64 mask = (bitMask(f.width) << f.lsb) >> shift;
			if (f.access == FieldAccess::WriteOneToClear) w1c |= uint8_t(mask);
			if (f.access == FieldAccess::ReadOnly) keep |= uint8_t(mask);
		}
		const uint8_t normal = uint8_t(~w1c & ~keep);
		cell = uint8_t((cell & keep) | (cell & w1c & ~value) | (value & normal));
	}

	/* the value a register rests at: its default, or 0 */
	void setIdle(const RegDef &r) {
		QByteArray bytes(r.size, '\0');
		QString why;
		if (r.hasDefault() && r.isNumeric()) {
			QByteArray encoded;
			if (encodeValue(r, QString::number(r.defaultValue, 'g', 17), encoded, why)) bytes = encoded;
		}
		std::memcpy(&memory_[r.addr], bytes.constData(), size_t(std::min<int>(r.size, MEMORY_SIZE - r.addr)));
	}

	/* w1c bits start set: a latched fault to see and clear */
	void setLatchedBits(const RegDef &r) {
		if (!r.isNumeric() || r.type == RegType::F32) return;
		quint64 bits = 0;
		if (r.write == WriteKind::WriteOneToClear) bits = r.fields.isEmpty() ? 1 : 0;
		for (const BitField &f : r.fields)
			if (f.access == FieldAccess::WriteOneToClear || r.write == WriteKind::WriteOneToClear) bits |= 1ULL << f.lsb;
		for (int i = 0; i < r.size; i++) memory_[size_t(r.addr + i)] |= uint8_t(bits >> (8 * i));
	}

	void scheduleIdle(const RegDef &r) {
		const RegDef def = r;
		QTimer::singleShot(ACTION_HOLD_MS, this, [this, def] { setIdle(def); });
	}

	/* ---- the moving values, refreshed on every READ */

	void animate() {
		const double t = double(clock_.nsecsElapsed()) / 1e9;
		for (int i = 0; i < map_.regs.size(); i++) {
			const RegDef &r = map_.regs[i];
			if (r.rw || !r.isNumeric() || !r.enumValues.isEmpty() || !r.fields.isEmpty() || r.hasDefault() || protocolBank(r))
				continue;
			if (r.type == RegType::U32 && r.unit == QLatin1String("ms")) {
				const uint32_t ms = uint32_t(t * 1000.0);
				std::memcpy(&memory_[r.addr], &ms, 4);
				continue;
			}
			const double phase = i + 1;
			const double wave = std::sin(t * (0.3 + 0.07 * (i % 9)) + phase); /* -1 .. 1 */
			double lo = r.hasMin() ? r.min : 5.0, hi = r.hasMax() ? r.max : 15.0;
			if (!r.hasMin() && !r.hasMax() && r.type != RegType::F32) {
				lo = 0;
				hi = 100;
			}
			const double shown = lo + (hi - lo) * (0.5 + 0.5 * wave);
			QByteArray bytes;
			QString why;
			if (encodeValue(r, QString::number(shown, 'g', 9), bytes, why))
				std::memcpy(&memory_[r.addr], bytes.constData(), size_t(r.size));
		}
	}

	/* ---- the login */

	bool touchesLogin(uint16_t addr, uint16_t count) const {
		return map_.loginAddr && int(addr) < map_.loginAddr + map_.loginSize && int(map_.loginAddr) < int(addr) + count;
	}

	QByteArray logIn(const evre::Frame &request, Connection &connection) {
		const bool whole = request.addr == map_.loginAddr && request.cnt == map_.loginSize
				&& request.data.size() == map_.loginSize;
		if (whole) std::memcpy(&memory_[request.addr], request.data.constData(), size_t(map_.loginSize));
		const bool ok = whole && request.data == token_;
		connection.loggedIn = ok;
		if (request.fn == evre::WRITE) return {};
		if (!ok) return evre::build(request.slave, evre::ERROR_RESP, request.addr, request.cnt, QByteArray(1, PERMISSION_DENIED));
		return evre::build(request.slave, evre::WRITE_ACK_RESP, request.addr, request.cnt);
	}

	/* ---- persist */

	void loadState() {
		if (options_.stateFile.isEmpty()) return;
		QFile file(options_.stateFile);
		if (!file.open(QIODevice::ReadOnly)) return; /* not there yet: the first run */
		const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
		for (const RegDef &r : map_.regs) {
			if (!r.persist || !state.contains(r.name)) continue;
			const QByteArray bytes = QByteArray::fromHex(state.value(r.name).toString().toLatin1());
			if (bytes.size() == r.size) std::memcpy(&memory_[r.addr], bytes.constData(), size_t(r.size));
		}
	}

	void saveState() {
		if (options_.stateFile.isEmpty()) return;
		QJsonObject state;
		for (const RegDef &r : map_.regs)
			if (r.persist)
				state.insert(r.name, QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(&memory_[r.addr]), r.size).toHex()));
		QFile file(options_.stateFile);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(QJsonDocument(state).toJson());
	}

	const DeviceMap &map_;
	QVector<const RegDef *> byAddress_; /* the map's registers in address order */
	const Options options_;
	const uint8_t slave_;
	QByteArray token_;
	std::vector<uint8_t> memory_ = std::vector<uint8_t>(MEMORY_SIZE);
	std::vector<uint8_t> covered_ = std::vector<uint8_t>(MEMORY_SIZE);
	QElapsedTimer clock_;
};

void serve(QTcpSocket *socket, Simulator &sim) {
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	auto parser = std::make_shared<evre::Parser>();
	auto connection = std::make_shared<Connection>();
	QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, parser, connection, &sim] {
		parser->feed(socket->readAll());
		evre::Frame request;
		QByteArray out;
		while (parser->next(request)) out += sim.hear(request, *connection);
		if (!out.isEmpty()) socket->write(out);
	});
	QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const QStringList args = app.arguments();
	Options options;
	QString mapFile;
	for (int i = 1; i < args.size(); i++) {
		const QString &a = args[i];
		auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
		if (a == QLatin1String("--port")) options.port = quint16(next().toUInt());
		else if (a == QLatin1String("--any")) options.any = true;
		else if (a == QLatin1String("--slave")) {
			/* 0 is the broadcast address, and past 255 the address byte would wrap round to another device's */
			bool ok = false;
			options.slave = next().toInt(&ok);
			if (!ok || options.slave < 1 || options.slave > 255) {
				std::fprintf(stderr, "evre-sim: --slave 1 to 255 (0 is the broadcast address)\n");
				return 2;
			}
		}
		else if (a == QLatin1String("--token")) options.token = next();
		else if (a == QLatin1String("--require-login")) options.requireLogin = true;
		else if (a == QLatin1String("--strict")) options.strict = true;
		else if (a == QLatin1String("--state")) options.stateFile = next();
		else if (a == QLatin1String("--verbose")) options.verbose = true;
		else if (a.startsWith(QLatin1String("--")) || !mapFile.isEmpty()) {
			std::fprintf(stderr, "usage: evre-sim MAP [--port 1210] [--any] [--slave N] [--token T] [--require-login]"
					" [--strict] [--state FILE] [--verbose]\n");
			return 2;
		} else mapFile = a;
	}
	if (mapFile.isEmpty()) {
		std::fprintf(stderr, "evre-sim: which map?\n");
		return 2;
	}
	DeviceMap map;
	QString error;
	if (!map.load(mapFile, error)) {
		std::fprintf(stderr, "evre-sim: %s: %s\n", qPrintable(mapFile), qPrintable(error));
		return 2;
	}
	Simulator sim(map, options);
	if (sim.slave() == evre::BROADCAST) {
		std::fprintf(stderr, "evre-sim: slave 0 is the broadcast address: a device has 1 to 255 (--slave)\n");
		return 2;
	}
	QTcpServer server;
	QObject::connect(&server, &QTcpServer::newConnection, [&] {
		while (QTcpSocket *socket = server.nextPendingConnection()) serve(socket, sim);
	});
	if (!server.listen(options.any ? QHostAddress::Any : QHostAddress::LocalHost, options.port)) {
		std::fprintf(stderr, "evre-sim: port %u: %s\n", options.port, qPrintable(server.errorString()));
		return 1;
	}
	std::printf("evre-sim: %s, %d registers, slave %d, on %s:%u%s\n", qPrintable(map.device), int(map.regs.size()),
			int(sim.slave()), options.any ? "0.0.0.0" : "127.0.0.1", options.port,
			map.loginAddr ? qPrintable(QStringLiteral(" (login at %1%2)").arg(addrText(map.loginAddr),
					options.requireLogin ? QStringLiteral(", required") : QString())) : "");
	std::fflush(stdout);
	return app.exec();
}

/* SPDX-License-Identifier: Apache-2.0 */
/* evre_fake_fast: tests/fake_device.py in C++, fast enough to measure EVRe
 * Studio itself at thousands of polls a second (the Python one answers a
 * request in ~100 us and becomes the bottleneck).
 *
 *   evre_fake_fast [port] [map.json] [token] [--slave N] [--node SLAVE=MAP]... [--login-required]
 *                     default 1210, maps/example_device.json, example-token
 *
 * Serves a map's registers from 64 KiB: DEVICE_ID from the map, read-only
 * f32 registers as slow sine waves, read-only u32 registers in ms counting
 * the milliseconds since the start, writes kept. Addresses in no register
 * are refused with ERROR_RESP 4 (offset out of range).
 *
 * Slaves, as on a bus: the device answers its own slave address only (--slave,
 * else the map's "slave"); a frame for another slave gets no answer. Each
 * --node adds one more device on the same port, with its own slave address,
 * map and memory (the same token). A broadcast (slave 0) WRITE is taken by
 * every device and answered by none; any other broadcast is dropped.
 *
 * The login, as in fake_device.py: when the map declares "login", that
 * register exists too. A write of exactly that register is accepted when it
 * holds the token (UTF-8, zero-padded to the size) and refused with
 * ERROR_RESP 3 (permission denied) when it holds anything else; a write of
 * part of it is refused as well. A refused WRITE (no acknowledge) gets no
 * answer, as every WRITE here. The login is checked, never required. A read
 * of the login register returns the last token written to it, accepted or
 * refused.
 *
 * --login-required: the login is required, as a device with EVRe Guard part 1
 * does. Until the token is written (by any client: the session is the
 * device's), every request but a write over the login register is refused
 * with ERROR_RESP 13 (login required), a WRITE silently; a refused token
 * closes the session again.
 *
 * CONFIG (0xA004) holds what was written, with HEARTBEAT (bit 0) set as a
 * read sees it. AUTO_SEND, per connection: a write of CONFIG with bit 3 set
 * makes the device send, on that connection, READ_RESP frames of its
 * read-only block (0xD000 up to the end of the last read-only register below
 * the first writable one) at 8000 / (prescaler + 1) Hz, the prescaler in bits
 * 15..8 (0 taken as 1, 4000 Hz, as some devices do: it is their timer's reload), unasked; a write with bit 3 clear
 * stops them. STATUS has
 * CAP_AUTO_SEND (bit 11).
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
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
constexpr char PERMISSION_DENIED = 3;   /* the ERROR_RESP code for a wrong token */
constexpr char OFFSET_OUT_OF_RANGE = 4; /* the ERROR_RESP code for an address in no register */
constexpr char LOGIN_REQUIRED = 13;     /* the ERROR_RESP code for a request without a session (--login-required) */
constexpr uint16_t CONFIG = evre::CONFIG, STREAM_START = evre::READ_ONLY_BLOCK;
constexpr uint16_t HEARTBEAT = evre::CONFIG_HEARTBEAT, AUTO_SEND = evre::CONFIG_AUTO_SEND;
const char *const DEFAULT_TOKEN = "example-token"; /* the token accepted, unless another is given */

class FakeDevice {
public:
	FakeDevice(const DeviceMap &map, uint8_t slave, const QString &token, bool loginRequired)
		: map_(map), slave_(slave), loginRequired_(loginRequired && map.loginAddr != 0) {
		for (const RegDef &r : map.regs)
			for (int i = 0; i < r.size && r.addr + i < MEMORY_SIZE; i++) covered_[r.addr + i] = true;
		if (map.loginAddr != 0) {
			for (int i = 0; i < map.loginSize && map.loginAddr + i < MEMORY_SIZE; i++)
				covered_[map.loginAddr + i] = true;
			token_ = token.toUtf8().left(map.loginSize); /* as the Studio sends it */
			token_.append(QByteArray(map.loginSize - int(token_.size()), '\0'));
		}
		memory_[evre::DEVICE_ID] = uint8_t(map.deviceId);
		memory_[evre::DEVICE_ID + 1] = uint8_t(map.deviceId >> 8);
		memory_[evre::STATUS] = 0x01;            /* STATUS: protocol revision 1, capability bits */
		memory_[evre::STATUS + 1] = 0x3F;
		/* the block AUTO_SEND sends: up to the first writable register at or above 0xD000 */
		int firstWritable = MEMORY_SIZE;
		for (const RegDef &r : map.regs)
			if (r.rw && r.addr >= STREAM_START) firstWritable = std::min(firstWritable, int(r.addr));
		streamEnd_ = STREAM_START;
		for (const RegDef &r : map.regs)
			if (!r.rw && r.addr >= STREAM_START && r.addr < firstWritable) streamEnd_ = std::max(streamEnd_, r.addr + r.size);
		clock_.start();
	}

	uint8_t slave() const { return slave_; }
	uint16_t config() const { return uint16_t(memory_[CONFIG] | (memory_[CONFIG + 1] << 8)); }

	/* one AUTO_SEND frame: the read-only block now, as a READ_RESP nobody asked for; empty when the map has none */
	QByteArray streamFrame() {
		const int count = streamEnd_ - STREAM_START;
		if (count <= 0) return {};
		animate();
		return evre::build(slave_, evre::READ_RESP, STREAM_START, uint16_t(count),
				QByteArray(reinterpret_cast<const char *>(&memory_[STREAM_START]), count));
	}

	/* the answer to one request for this device (or a broadcast); empty for a WRITE, which gets none */
	QByteArray answer(const evre::Frame &request) {
		const uint8_t slave = slave_;
		const uint16_t addr = request.addr, count = request.cnt;
		const bool write = request.fn == evre::WRITE || request.fn == evre::WRITE_ACK;
		if (loginRequired_ && !loggedIn_ && !(write && touchesLogin(addr, count))) {
			if (request.fn == evre::WRITE) return {};
			return evre::build(slave, evre::ERROR_RESP, addr, count, QByteArray(1, LOGIN_REQUIRED));
		}
		if (!covered(addr, count)) {
			if (request.fn == evre::WRITE) return {};
			return evre::build(slave, evre::ERROR_RESP, addr, count, QByteArray(1, OFFSET_OUT_OF_RANGE));
		}
		if (request.fn == evre::READ) {
			animate();
			memory_[CONFIG] |= HEARTBEAT; /* the device sets it on every request it takes */
			return evre::build(slave, evre::READ_RESP, addr, count,
					QByteArray(reinterpret_cast<const char *>(&memory_[addr]), count));
		}
		if (write && touchesLogin(addr, count)) return logIn(request);
		if (write) {
			const size_t written = size_t(std::min<qsizetype>(request.data.size(), count));
			std::memcpy(&memory_[addr], request.data.constData(), written);
			if (request.fn == evre::WRITE_ACK) return evre::build(slave, evre::WRITE_ACK_RESP, addr, count);
		}
		return {};
	}

private:
	/* the request covers some byte of the login register */
	bool touchesLogin(uint16_t addr, uint16_t count) const {
		return map_.loginAddr != 0 && int(addr) < map_.loginAddr + map_.loginSize
				&& int(map_.loginAddr) < int(addr) + count;
	}

	/* a write to the login register: kept when it is the whole register, accepted only when it holds the token */
	QByteArray logIn(const evre::Frame &request) {
		const bool whole = request.addr == map_.loginAddr && request.cnt == map_.loginSize
				&& request.data.size() == map_.loginSize;
		if (whole) std::memcpy(&memory_[request.addr], request.data.constData(), size_t(map_.loginSize));
		loggedIn_ = whole && request.data == token_;
		if (request.fn == evre::WRITE) return {};
		if (!whole || request.data != token_)
			return evre::build(slave_, evre::ERROR_RESP, request.addr, request.cnt, QByteArray(1, PERMISSION_DENIED));
		return evre::build(slave_, evre::WRITE_ACK_RESP, request.addr, request.cnt);
	}

	bool covered(uint16_t addr, uint16_t count) const {
		if (count == 0 || int(addr) + count > MEMORY_SIZE) return false;
		for (int i = 0; i < count; i++)
			if (!covered_[addr + i]) return false;
		return true;
	}

	/* the moving values, refreshed on every READ */
	void animate() {
		const double t = double(clock_.nsecsElapsed()) / 1e9;
		for (int i = 0; i < map_.regs.size(); i++) {
			const RegDef &r = map_.regs[i];
			if (r.rw) continue;
			if (r.type == RegType::U32 && r.unit == QLatin1String("ms")) {
				const uint32_t ms = uint32_t(t * 1000.0);
				std::memcpy(&memory_[r.addr], &ms, 4);
			} else if (r.type == RegType::F32) {
				const int phase = i + 1;
				const float v = float(10.0 + 5.0 * std::sin(t * (0.3 + 0.07 * (phase % 9)) + phase));
				std::memcpy(&memory_[r.addr], &v, 4);
			}
		}
	}

	const DeviceMap map_;
	const uint8_t slave_;
	const bool loginRequired_;
	bool loggedIn_ = false;
	QByteArray token_; /* what a login must write: the token, cut or zero-padded to the login size */
	int streamEnd_ = STREAM_START; /* the end of the block AUTO_SEND sends */
	std::vector<uint8_t> memory_ = std::vector<uint8_t>(MEMORY_SIZE);
	std::vector<uint8_t> covered_ = std::vector<uint8_t>(MEMORY_SIZE); /* 1: some register holds this byte */
	QElapsedTimer clock_;
};

using Devices = std::vector<std::unique_ptr<FakeDevice>>;

/* one request on the bus: the device of its slave answers; a broadcast WRITE reaches every device, unanswered */
QByteArray answerOnBus(Devices &devices, const evre::Frame &request) {
	if (request.slave == evre::BROADCAST) {
		if (request.fn == evre::WRITE)
			for (auto &device : devices) device->answer(request);
		return {};
	}
	for (auto &device : devices)
		if (device->slave() == request.slave) return device->answer(request);
	return {}; /* no device has that address */
}

/* One device's AUTO_SEND on one connection: a timer at 1 ms or slower, and as many frames at each tick as the rate
 * asks for by then (a timer cannot tick every 125 us for 8000 Hz; the frames a second come out right all the same). */
class Stream {
public:
	Stream(QTcpSocket *socket, FakeDevice *device) : timer_(new QTimer(socket)) {
		timer_->setTimerType(Qt::PreciseTimer);
		QObject::connect(timer_, &QTimer::timeout, timer_, [this, socket, device] {
			const qint64 due = clock_.nsecsElapsed() * hz_ / 1000000000LL;
			QByteArray out;
			for (int i = 0; sent_ < due && i < 100; i++, sent_++) out += device->streamFrame();
			sent_ = std::max(sent_, due - 100); /* far behind (a stall): not made up in a burst */
			if (!out.isEmpty()) socket->write(out);
		});
	}
	/* config: CONFIG as written; AUTO_SEND set starts it (again, at its rate), cleared stops it */
	void follow(uint16_t config) {
		if (!(config & AUTO_SEND)) {
			timer_->stop();
			return;
		}
		const int prescaler = (config >> evre::CONFIG_PRESCALER_SHIFT) & 0xFF;
		hz_ = evre::AUTO_SEND_BASE_HZ / (std::max(prescaler, evre::AUTO_SEND_PRESCALER_MIN) + 1);
		sent_ = 0;
		clock_.start();
		timer_->start(std::max(1, 1000 / hz_));
	}

private:
	QTimer *timer_; /* the socket's child: gone with the connection */
	QElapsedTimer clock_;
	qint64 sent_ = 0;
	int hz_ = 100;
};

/* the request wrote CONFIG of this device (or of every device, a broadcast) */
bool writesConfig(const evre::Frame &request, const FakeDevice &device) {
	const bool write = request.fn == evre::WRITE || request.fn == evre::WRITE_ACK;
	const bool reaches = request.slave == device.slave() || request.slave == evre::BROADCAST;
	return write && reaches && request.addr <= CONFIG && int(request.addr) + request.cnt > CONFIG;
}

/* each connection has its own parser and its own AUTO_SEND streams; all answers to one read of the socket go out in
 * one send */
void serve(QTcpSocket *socket, Devices &devices) {
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	auto parser = std::make_shared<evre::Parser>();
	auto streams = std::make_shared<QHash<FakeDevice *, std::shared_ptr<Stream>>>();
	QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, parser, streams, &devices] {
		parser->feed(socket->readAll());
		evre::Frame request;
		QByteArray out;
		while (parser->next(request)) {
			out += answerOnBus(devices, request);
			for (auto &device : devices) {
				if (!writesConfig(request, *device)) continue;
				std::shared_ptr<Stream> &stream = (*streams)[device.get()];
				if (!stream) stream = std::make_shared<Stream>(socket, device.get());
				stream->follow(device->config());
			}
		}
		if (!out.isEmpty()) socket->write(out);
	});
	QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	/* the positional arguments (port, map, token) and the options, in any order */
	QStringList positional;
	int slave = -1;                    /* the first device's: -1 = its map's */
	QVector<QPair<int, QString>> nodes; /* --node SLAVE=MAP */
	bool loginRequired = false;
	const QStringList args = app.arguments();
	for (int i = 1; i < args.size(); i++) {
		const QString &a = args[i];
		const QString value = i + 1 < args.size() ? args[i + 1] : QString();
		if (a == QLatin1String("--login-required")) {
			loginRequired = true;
		} else if (a == QLatin1String("--slave")) {
			slave = value.toInt();
			i++;
		} else if (a == QLatin1String("--node")) {
			const int eq = int(value.indexOf(QLatin1Char('=')));
			if (eq < 1) {
				std::printf("--node SLAVE=MAP, not \"%s\"\n", qPrintable(value));
				return 2;
			}
			nodes.push_back({ value.left(eq).toInt(), value.mid(eq + 1) });
			i++;
		} else {
			positional << a;
		}
	}
	const quint16 port = positional.size() > 0 ? quint16(positional[0].toUInt()) : 1210;
	const QString defaultMap = QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_device.json");
	const QString token = positional.size() > 2 ? positional[2] : QString::fromLatin1(DEFAULT_TOKEN);
	nodes.prepend({ slave, positional.size() > 1 ? positional[1] : defaultMap });

	Devices devices;
	for (const auto &node : nodes) {
		DeviceMap map;
		QString error;
		if (!map.load(node.second, error)) {
			std::printf("map: %s\n", qPrintable(error));
			return 2;
		}
		const int address = node.first >= 0 ? node.first : map.slave;
		if (address < 1 || address > 255) {
			std::printf("slave %d: a device address is 1 to 255 (0 is the broadcast)\n", address);
			return 2;
		}
		devices.push_back(std::make_unique<FakeDevice>(map, uint8_t(address), token, loginRequired));
		const QString login = map.loginAddr != 0 ? QStringLiteral(", login at %1%2").arg(addrText(map.loginAddr),
				loginRequired ? QStringLiteral(", required") : QString()) : QString();
		std::printf("%s slave %d: %s (%d registers%s)\n", devices.size() == 1 ? "device" : "  and", address,
				qPrintable(map.device), int(map.regs.size()), qPrintable(login));
	}

	QTcpServer server;
	QObject::connect(&server, &QTcpServer::newConnection, [&] {
		while (QTcpSocket *socket = server.nextPendingConnection()) serve(socket, devices);
	});
	if (!server.listen(QHostAddress::LocalHost, port)) {
		std::printf("listen %u: %s\n", port, qPrintable(server.errorString()));
		return 1;
	}
	std::printf("fast fake EVRe device on 127.0.0.1:%u\n", port);
	std::fflush(stdout);
	return app.exec();
}

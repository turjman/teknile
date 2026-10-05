/* SPDX-License-Identifier: Apache-2.0 */
/* evre_probe: EVRe Studio's protocol core (link, master, map) without the
 * window, for checking it against a real device. READ ONLY: it never writes
 * a register value. The one write it may send is the login: with EVRE_TOKEN
 * set and a "login" register in the map, the token goes there first, as the
 * Studio sends it.
 *
 *   evre_probe tcp <host> <port> <map.json> [cycles]
 *   evre_probe serial <COMx> <baud> <map.json> [cycles]
 *   EVRE_TOKEN=...: the login token      EVRE_INFLIGHT=4: requests pipelined (default 1)
 *
 * It reads the map's registers in the same blocks as the Studio, cycle after
 * cycle as fast as the device answers (100 cycles by default), then prints
 * the rate, the latency, the link's counters and every numeric value.
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <memory>

#include "evre/master.h"
#include "model/device_map.h"

namespace {

/* one read request: registers close to each other, read together */
struct Block {
	uint16_t addr, count;
	QVector<int> regs; /* indexes into the map */
};

/* the same blocks as the Studio polls (model/device_map.h): within one bank, gaps up to MAX_BLOCK_GAP bytes,
 * no long byte arrays. DeviceMap::load sorts the registers by address, so they are merged in address order, as
 * the Studio merges them. One difference: the Studio stops polling a register the device refuses for good, the
 * probe keeps reading it and reports the failed reads. */
QVector<Block> buildBlocks(const DeviceMap &map) {
	QVector<Block> blocks;
	for (int i = 0; i < map.regs.size(); i++) {
		const RegDef &def = map.regs[i];
		if (!isPollable(def)) continue;
		if (!blocks.isEmpty()) {
			Block &last = blocks.last();
			const int end = last.addr + last.count;
			if (sameBank(def.addr, last.addr) && int(def.addr) - end <= MAX_BLOCK_GAP) {
				last.count = uint16_t(std::max(end, int(def.addr) + def.size) - last.addr);
				last.regs << i;
				continue;
			}
		}
		blocks.push_back({ def.addr, uint16_t(def.size), { i } });
	}
	return blocks;
}

std::unique_ptr<evre::Link> makeLink(const QStringList &args) {
	if (args[1] == QLatin1String("tcp")) return std::make_unique<evre::TcpLink>(args[2], quint16(args[3].toUInt()));
	return std::make_unique<evre::SerialLink>(args[2], args[3].toInt());
}

class Probe {
public:
	Probe(const DeviceMap &map, evre::Link *link, int cycles)
		: map_(map), link_(link), cycles_(cycles), blocks_(buildBlocks(map)), values_(map.regs.size()) {
		const int inFlight = qEnvironmentVariableIntValue("EVRE_INFLIGHT");
		master_.setSlave(map.slave);
		master_.setTimeoutMs(1000);
		master_.setInFlight(inFlight > 0 ? inFlight : 1);
		master_.setLink(link);
	}

	/* the link is open: log in if asked to, then read */
	void start() {
		std::printf("connected to %s\n", qPrintable(link_->describe()));
		sendLogin();
		clock_.start();
		readCycle();
	}

	bool finished() const { return done_ >= cycles_; }

private:
	/* EVRE_TOKEN to the map's login register, encoded as the Studio does (encodeLoginToken) */
	void sendLogin() {
		const QString token = qEnvironmentVariable("EVRE_TOKEN");
		if (token.isEmpty()) return;
		if (map_.loginAddr == 0 || map_.loginSize <= 0) { /* as the Studio: IoEngine::sendLogin */
			std::printf("the map declares no login register: the token was not sent\n");
			return;
		}
		master_.write(map_.loginAddr, encodeLoginToken(token, map_.loginSize), [](const evre::Result &r) {
			std::printf("token: %s\n", r.ok ? "accepted" : qPrintable(r.message));
		});
	}

	/* every block once; the next cycle starts when the last answer is in */
	void readCycle() {
		if (finished()) {
			report();
			QCoreApplication::quit();
			return;
		}
		pending_ = int(blocks_.size());
		for (const Block &block : blocks_)
			master_.read(block.addr, block.count, [this, block](const evre::Result &r) { onAnswer(block, r); });
	}

	void onAnswer(const Block &block, const evre::Result &r) {
		if (r.ok) {
			for (int i : block.regs) values_[i] = r.data.mid(map_.regs[i].addr - block.addr, map_.regs[i].size);
		} else if (++failures_ < 5) {
			std::printf("read 0x%04X x%u: %s\n", block.addr, block.count, qPrintable(r.message));
		}
		if (--pending_ == 0) {
			done_++;
			QTimer::singleShot(0, [this] { readCycle(); });
		}
	}

	void report() const {
		const double seconds = double(clock_.nsecsElapsed()) / 1e9;
		const evre::Master::Stats &stats = master_.stats();
		std::printf("\n%d polls of %d blocks (%d registers) in %.2f s = %.1f polls/s\n", cycles_, int(blocks_.size()),
				int(map_.regs.size()), seconds, cycles_ / seconds);
		std::printf("latency avg %.2f ms | tx %llu rx %llu | timeouts %llu errors %llu bad frames %llu"
				" | failed reads %d\n",
				stats.avgLatencyMs, (unsigned long long) stats.tx, (unsigned long long) stats.rx,
				(unsigned long long) stats.timeouts, (unsigned long long) stats.errors,
				(unsigned long long) stats.badFrames, failures_);
		for (int i = 0; i < map_.regs.size(); i++) {
			const RegDef &def = map_.regs[i];
			if (!def.isNumeric()) continue;
			std::printf("  %-14s %-12s %-4s %s\n", qPrintable(def.name), qPrintable(formatValue(def, values_[i])),
					qPrintable(def.unit), qPrintable(formatDecoded(def, values_[i])));
		}
	}

	const DeviceMap &map_;
	evre::Link *link_;
	const int cycles_;
	const QVector<Block> blocks_;
	QVector<QByteArray> values_; /* the last value read of each register of the map */
	evre::Master master_;
	QElapsedTimer clock_;
	int done_ = 0, pending_ = 0, failures_ = 0;
};

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() < 5) {
		std::printf("usage: evre_probe tcp|serial <host|COMx> <port|baud> <map.json> [cycles]\n");
		return 2;
	}
	DeviceMap map;
	QString error;
	if (!map.load(args[4], error)) {
		std::printf("map: %s\n", qPrintable(error));
		return 2;
	}
	const int cycles = args.size() > 5 ? args[5].toInt() : 100;

	const std::unique_ptr<evre::Link> link = makeLink(args);
	Probe probe(map, link.get(), cycles);
	QObject::connect(link.get(), &evre::Link::opened, [&probe] { probe.start(); });
	QObject::connect(link.get(), &evre::Link::closed, [&](const QString &why) {
		if (!why.isEmpty()) std::printf("link closed: %s\n", qPrintable(why));
		if (!probe.finished()) app.exit(1);
	});
	link->open();
	return app.exec();
}

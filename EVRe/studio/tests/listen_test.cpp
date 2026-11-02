/* SPDX-License-Identifier: Apache-2.0 */
/* evre_listen_test: Listen without a window, a driver or a USB device (the capture sources of src/io/usb_capture.h):
 * the two decoders give the same events on every scene tests/make_captures.py wrote, FTDI's status bytes come off,
 * only the target's device and endpoints are heard, a stream may be cut at any byte, and a file replays through the
 * interface the live captures have. */
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cstring>
#include <random>
#include <utility>

#include "evre/frame.h"
#include "io/fast_stream.h"
#include "io/listen_link.h"
#include "io/usb_capture.h"
#include "model/device_map.h"

namespace {

constexpr int ADDRESS = 5;          /* the scenes' device; the second device is 6 */
constexpr int OTHER_ADDRESS = 6;
constexpr uint8_t OTHER_SLAVE = 2;  /* frames the scenes send where they must not be heard say this slave */
constexpr int FTDI_OUT = 0x02;
constexpr int BULK = UsbDecoder::BULK;
constexpr int INTERRUPT = 1;
constexpr int CONTROL = 2;

UsbTarget cdcTarget() {
	UsbTarget target;
	target.port = QStringLiteral("COM13");
	target.bus = 2;
	target.address = ADDRESS;
	target.vid = 0x0483;
	target.pid = 0x5740;
	target.kind = UsbTarget::Cdc;
	target.inEndpoints = { 0x81 };
	target.outEndpoints = { 0x01 };
	target.inMaxPacket = 64;
	return target;
}

UsbTarget ftdiTarget() {
	UsbTarget target = cdcTarget();
	target.vid = 0x0403;
	target.pid = 0x6001;
	target.kind = UsbTarget::Ftdi;
	target.outEndpoints = { FTDI_OUT };
	return target;
}

UsbTarget compositeTarget() {
	UsbTarget target = cdcTarget();
	target.inEndpoints = { 0x83 };
	target.outEndpoints = { 0x03 };
	return target;
}

UsbTarget targetOf(const QString &scene) {
	if (scene == QLatin1String("ftdi_poll")) return ftdiTarget();
	if (scene == QLatin1String("composite")) return compositeTarget();
	return cdcTarget();
}

QString captureFile(const QString &name) {
	return QCoreApplication::applicationDirPath() + QStringLiteral("/captures/") + name;
}

QByteArray readFile(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/* the scene's file in one format, fed `chunk` bytes at a time (0: all at once) */
template <typename Decoder>
QVector<UsbEvent> decoded(const QByteArray &bytes, const UsbTarget &target, int chunk = 0) {
	Decoder decoder(target);
	QVector<UsbEvent> events;
	if (chunk == 0) decoder.feed(bytes, events);
	else for (qsizetype at = 0; at < bytes.size(); at += chunk) decoder.feed(bytes.mid(at, chunk), events);
	return events;
}

QVector<UsbEvent> decodedPcap(const QString &scene, const UsbTarget &target) {
	return decoded<UsbPcapDecoder>(readFile(captureFile(scene + QStringLiteral(".pcap"))), target);
}

QVector<UsbEvent> decodedMon(const QString &scene, const UsbTarget &target) {
	return decoded<UsbmonDecoder>(readFile(captureFile(scene + QStringLiteral(".usbmon"))), target);
}

/* what must be the same. The status is left out: an OUT is taken at its submission, where usbmon says "in progress" */
bool sameEvent(const UsbEvent &a, const UsbEvent &b) {
	return a.direction == b.direction && a.endpoint == b.endpoint && a.transfer == b.transfer && a.data == b.data
			&& a.stamp == b.stamp;
}

bool sameEvents(const QVector<UsbEvent> &a, const QVector<UsbEvent> &b) {
	return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameEvent);
}

/* the same bytes each way in the same transfers, wherever and whenever they went */
bool sameBytes(const QVector<UsbEvent> &a, const QVector<UsbEvent> &b) {
	return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const UsbEvent &x, const UsbEvent &y) {
		return x.direction == y.direction && x.data == y.data;
	});
}

QVector<evre::Frame> framesOf(const QVector<UsbEvent> &events, UsbDirection direction) {
	evre::Parser parser;
	for (const UsbEvent &event : events)
		if (event.direction == direction) parser.feed(event.data);
	QVector<evre::Frame> frames;
	evre::Frame frame;
	while (parser.next(frame)) frames.append(frame);
	return frames;
}

/* a bulk record of each format, for what the scenes do not hold */
void put16(QByteArray &bytes, quint16 value) { bytes.append(reinterpret_cast<const char *>(&value), 2); }
void put32(QByteArray &bytes, quint32 value) { bytes.append(reinterpret_cast<const char *>(&value), 4); }

QByteArray pcapHeader(quint32 magic = 0xA1B2C3D4, quint32 link = 249) {
	QByteArray head;
	put32(head, magic);
	put16(head, 2);
	put16(head, 4);
	put32(head, 0);
	put32(head, 0);
	put32(head, 65535);
	put32(head, link);
	return head;
}

struct Packet {
	int device = ADDRESS;
	int endpoint = 0x81;
	int transfer = BULK;
	bool completion = true;
	qint32 status = 0;
	QByteArray data;
	int extraHeader = 0;        /* a longer driver header, a control transfer's stage for one */
	quint32 sayLength = 0xFFFFFFFF; /* the dataLength written, when it is not the data's */
};

QByteArray pcapRecord(const Packet &p) {
	QByteArray packet;
	put16(packet, quint16(27 + p.extraHeader));
	packet.append(8, '\x11');
	put32(packet, quint32(p.status));
	put16(packet, 9);
	packet.append(char(p.completion ? 1 : 0));
	put16(packet, 2);
	put16(packet, quint16(p.device));
	packet.append(char(p.endpoint));
	packet.append(char(p.transfer));
	put32(packet, p.sayLength == 0xFFFFFFFF ? quint32(p.data.size()) : p.sayLength);
	packet.append(p.extraHeader, '\0');
	packet.append(p.data);
	QByteArray record;
	put32(record, 1790000000);
	put32(record, 250);
	put32(record, quint32(packet.size()));
	put32(record, quint32(packet.size()));
	return record + packet;
}

QByteArray monRecord(const Packet &p, char tag = 0) {
	QByteArray head(64, '\0');
	const char type = p.completion ? 'C' : 'S';
	head[8] = type;
	head[9] = char(p.transfer);
	head[10] = char(p.endpoint);
	head[11] = char(p.device);
	head[15] = tag;
	const qint64 seconds = 1790000000;
	const qint32 microseconds = 250;
	const qint32 status = p.status;
	const quint32 length = quint32(p.data.size());
	std::memcpy(head.data() + 16, &seconds, 8);
	std::memcpy(head.data() + 24, &microseconds, 4);
	std::memcpy(head.data() + 28, &status, 4);
	std::memcpy(head.data() + 36, &length, 4);
	return head + p.data;
}

QVector<UsbEvent> heardPcap(const QVector<Packet> &packets, const UsbTarget &target) {
	QByteArray stream = pcapHeader();
	for (const Packet &packet : packets) stream += pcapRecord(packet);
	return decoded<UsbPcapDecoder>(stream, target);
}

QVector<UsbEvent> heardMon(const QVector<Packet> &packets, const UsbTarget &target) {
	QByteArray stream;
	for (const Packet &packet : packets) stream += monRecord(packet);
	return decoded<UsbmonDecoder>(stream, target);
}

/* an IN transfer as an FTDI chip sends it: 2 status bytes at the start of every packet */
QByteArray ftdiTransfer(const QByteArray &payload, int packet) {
	QByteArray transfer;
	for (qsizetype at = 0; at < payload.size(); at += packet - 2) transfer += QByteArray("\x01\x60", 2) + payload.mid(at, packet - 2);
	return transfer;
}

QByteArray pattern(int size) {
	QByteArray data(size, '\0');
	for (int i = 0; i < size; i++) data[i] = char(0x20 + i % 90);
	return data;
}

} // namespace

class ListenTest : public QObject {
	Q_OBJECT

private slots:
	void scenes_data() {
		QTest::addColumn<QString>("scene");
		for (const char *name : { "cdc_poll", "cdc_autosend", "cdc_fast", "ftdi_poll", "composite", "cdc_quiet" })
			QTest::newRow(name) << QString::fromLatin1(name);
	}

	/* the two formats of a scene are the same transfers: nothing in the decoders depends on the system */
	void scenes() {
		QFETCH(QString, scene);
		const UsbTarget target = targetOf(scene);
		const QVector<UsbEvent> pcap = decodedPcap(scene, target), mon = decodedMon(scene, target);
		QVERIFY(!pcap.isEmpty());
		QCOMPARE(pcap.size(), mon.size());
		for (int i = 0; i < pcap.size(); i++) QVERIFY2(sameEvent(pcap[i], mon[i]), qPrintable(QString::number(i)));
		for (const UsbEvent &event : pcap) {
			QCOMPARE(event.transfer, BULK);
			QVERIFY(!event.data.isEmpty());
			if (event.direction == UsbDirection::In) {
				QVERIFY(target.inEndpoints.contains(event.endpoint));
				QCOMPARE(event.status, 0);
			} else {
				QVERIFY(target.outEndpoints.contains(event.endpoint));
			}
		}
		for (int i = 1; i < pcap.size(); i++) QVERIFY(pcap[i].stamp >= pcap[i - 1].stamp);
	}

	/* the same stream fed one byte at a time, and in chunks of every size a pipe could give */
	void cutAnywhere_data() { scenes_data(); }
	void cutAnywhere() {
		QFETCH(QString, scene);
		const UsbTarget target = targetOf(scene);
		const QByteArray pcap = readFile(captureFile(scene + QStringLiteral(".pcap")));
		const QByteArray mon = readFile(captureFile(scene + QStringLiteral(".usbmon")));
		const QVector<UsbEvent> pcapWhole = decoded<UsbPcapDecoder>(pcap, target);
		const QVector<UsbEvent> monWhole = decoded<UsbmonDecoder>(mon, target);
		QVERIFY(sameEvents(decoded<UsbPcapDecoder>(pcap, target, 1), pcapWhole));
		QVERIFY(sameEvents(decoded<UsbmonDecoder>(mon, target, 1), monWhole));
		for (int chunk : { 2, 3, 7, 26, 27, 28, 64, 65, 500 }) {
			QVERIFY(sameEvents(decoded<UsbPcapDecoder>(pcap, target, chunk), pcapWhole));
			QVERIFY(sameEvents(decoded<UsbmonDecoder>(mon, target, chunk), monWhole));
		}
		std::mt19937 random(1);
		UsbPcapDecoder decoder(target);
		QVector<UsbEvent> events;
		for (qsizetype at = 0; at < pcap.size();) {
			const int chunk = int(random() % 300) + 1;
			decoder.feed(pcap.mid(at, chunk), events);
			at += chunk;
		}
		QVERIFY(sameEvents(events, pcapWhole));
		QCOMPARE(decoder.pendingBytes(), qsizetype(0));
	}

	/* a stream that stops at any byte: the whole records are heard, the cut one waits, nothing is invented */
	void truncated() {
		const UsbTarget target = compositeTarget();
		const QByteArray pcap = readFile(captureFile(QStringLiteral("composite.pcap")));
		const QByteArray mon = readFile(captureFile(QStringLiteral("composite.usbmon")));
		const QVector<UsbEvent> pcapWhole = decoded<UsbPcapDecoder>(pcap, target);
		const QVector<UsbEvent> monWhole = decoded<UsbmonDecoder>(mon, target);
		QVERIFY(pcap.size() > 1000);
		QVERIFY(mon.size() > 1000);
		for (int size = 0; size < pcap.size(); size++) {
			UsbPcapDecoder decoder(target);
			QVector<UsbEvent> events;
			decoder.feed(pcap.left(size), events);
			QVERIFY(decoder.error().isEmpty());
			QVERIFY(sameEvents(events, pcapWhole.mid(0, events.size())));
			QVERIFY(decoder.pendingBytes() < 512);
		}
		for (int size = 0; size < mon.size(); size++) {
			UsbmonDecoder decoder(target);
			QVector<UsbEvent> events;
			decoder.feed(mon.left(size), events);
			QVERIFY(decoder.error().isEmpty());
			QVERIFY(sameEvents(events, monWhole.mid(0, events.size())));
			QVERIFY(decoder.pendingBytes() < 512);
		}
	}

	/* scene 1, what a program and the device said, every one of its oddities where the parser above can find it */
	void pollScene() {
		const QVector<UsbEvent> events = decodedPcap(QStringLiteral("cdc_poll"), cdcTarget());
		const QVector<evre::Frame> requests = framesOf(events, UsbDirection::Out);
		const QVector<evre::Frame> answers = framesOf(events, UsbDirection::In);
		QCOMPARE(requests.size(), 46); /* DEVICE_ID, a long read, 2 x 20 rounds, a WRITE_ACK, a WRITE, a refused, a late */
		QCOMPARE(answers.size(), 46);  /* the same less the WRITE, plus the answer nobody asked for */
		QCOMPARE(int(requests.first().fn), int(evre::READ));
		QCOMPARE(requests.first().addr, uint16_t(0xA000));
		QCOMPARE(requests.first().cnt, uint16_t(4));
		QCOMPARE(answers.first().data.left(2), QByteArray("\x01\x10", 2)); /* DEVICE_ID 0x1001, the example map's */

		int ramp = 0, writes = 0, acks = 0, errors = 0, unasked = 0;
		for (const evre::Frame &frame : requests) {
			QCOMPARE(frame.slave, uint8_t(1)); /* the second device's are not here */
			if (frame.fn == evre::WRITE || frame.fn == evre::WRITE_ACK) writes++;
		}
		for (const evre::Frame &frame : answers) {
			QCOMPARE(frame.slave, uint8_t(1));
			if (frame.fn == evre::READ_RESP && frame.addr == 0xD000) {
				float supply = 0;
				std::memcpy(&supply, frame.data.constData() + 4, 4);
				QVERIFY(qAbs(supply - (12.0f + 0.1f * float(ramp))) < 1e-4f);
				ramp++;
			}
			if (frame.fn == evre::READ_RESP && frame.addr == 0xD010) unasked++;
			if (frame.fn == evre::WRITE_ACK_RESP) acks++;
			if (frame.fn == evre::ERROR_RESP) errors++;
		}
		QCOMPARE(ramp, 20);
		QCOMPARE(writes, 2);
		QCOMPARE(acks, 1);
		QCOMPARE(errors, 1);
		QCOMPARE(unasked, 1);

		/* the transfers: the answer in two, the two answers in one, and the text, heard as bytes */
		int inTransfers = 0, text = 0, twoInOne = 0, secondPart = 0;
		for (const UsbEvent &event : events) {
			if (event.direction != UsbDirection::In) continue;
			inTransfers++;
			if (event.data == "hello, world") text++;
			if (event.data.size() == 20 && !event.data.startsWith('\x7B')) secondPart++; /* 32 bytes cut as 12 + 20 */
			if (event.data.size() == 50) twoInOne++;
		}
		QCOMPARE(inTransfers, 47); /* 46 frames, the text, one more for the split, one less for the two in one */
		QCOMPARE(text, 1);
		QCOMPARE(secondPart, 1);
		QCOMPARE(twoInOne, 1);
	}

	/* the frames of scenes 2 and 3 are what they say: the rate, and the blocks' numbers */
	void autoSendScene() {
		const QVector<UsbEvent> events = decodedPcap(QStringLiteral("cdc_autosend"), cdcTarget());
		const QVector<evre::Frame> answers = framesOf(events, UsbDirection::In);
		QCOMPARE(framesOf(events, UsbDirection::Out).size(), 1);
		QCOMPARE(answers.size(), 101);
		QCOMPARE(int(answers.first().fn), int(evre::WRITE_ACK_RESP));
		const QVector<evre::Frame> frames = framesOf(events, UsbDirection::Out);
		QCOMPARE(frames.first().addr, uint16_t(0xA004));
		QCOMPARE(evre::littleEndian16(frames.first().data, 0), uint16_t(79 << 8 | 0x08)); /* 8000 / (79 + 1) = 100 a second */
		QCOMPARE(events.last().stamp - events[events.size() - 100].stamp, qint64(990000));
		for (int i = 1; i < answers.size(); i++) {
			QCOMPARE(int(answers[i].fn), int(evre::READ_RESP));
			QCOMPARE(answers[i].addr, uint16_t(0xD000));
		}
	}

	void fastScene() {
		DeviceMap map;
		QString err;
		QVERIFY2(map.load(QCoreApplication::applicationDirPath() + QStringLiteral("/maps/example_fast.json"), err), qPrintable(err));
		const StreamDef def = map.streams.first();
		const QVector<UsbEvent> events = decodedMon(QStringLiteral("cdc_fast"), cdcTarget());
		QCOMPARE(framesOf(events, UsbDirection::Out).size(), 1);
		fast::FastStream stream;
		stream.reset(def);
		const QVector<evre::Frame> answers = framesOf(events, UsbDirection::In);
		QCOMPARE(answers.size(), 19); /* the enable's answer and 20 blocks less 2 */
		for (const evre::Frame &frame : answers) {
			if (frame.addr != def.addr) continue;
			fast::BlockTaken taken;
			QCOMPARE(stream.take(frame.data, 0, taken), fast::BlockCheck::Ok);
		}
		QCOMPARE(stream.blocks, quint64(18));
		QCOMPARE(stream.lost, quint64(2 * 50)); /* the numbers say what the capture lost */
		QCOMPARE(stream.badBlocks, quint64(0));
	}

	/* behind a bridge, the same bytes in the same transfers: the status bytes and the chip's polls are gone */
	void ftdiScene() {
		const QVector<UsbEvent> ftdi = decodedPcap(QStringLiteral("ftdi_poll"), ftdiTarget());
		const QVector<UsbEvent> cdc = decodedPcap(QStringLiteral("cdc_poll"), cdcTarget());
		QVERIFY(sameBytes(ftdi, cdc));
		QCOMPARE(framesOf(ftdi, UsbDirection::In).size(), 46);
		/* the long read crosses a packet: its frame survives only if the status bytes inside it are taken out */
		bool crossed = false;
		for (const UsbEvent &event : ftdi) crossed |= event.direction == UsbDirection::In && event.data.size() > 62;
		QVERIFY(crossed);
		/* and they are there to take out: read as a plain CDC device, the same file is not those transfers */
		UsbTarget plain = ftdiTarget();
		plain.kind = UsbTarget::Cdc;
		evre::Parser parser;
		int heard = 0;
		for (const UsbEvent &event : decodedPcap(QStringLiteral("ftdi_poll"), plain))
			if (event.direction == UsbDirection::In) { parser.feed(event.data); heard++; }
		QVERIFY(heard > 47); /* the status polls are transfers too */
		evre::Frame frame;
		int frames = 0;
		while (parser.next(frame)) frames++;
		QVERIFY(frames < 46);
	}

	void ftdiPackets() {
		const auto heard = [](const QByteArray &transfer, int maxPacket) {
			UsbTarget target = ftdiTarget();
			target.inMaxPacket = maxPacket;
			Packet packet;
			packet.data = transfer;
			return QVector<QVector<UsbEvent>> { heardPcap({ packet }, target), heardMon({ packet }, target) };
		};
		const auto payload = [&](const QByteArray &transfer, int maxPacket) {
			const QVector<QVector<UsbEvent>> both = heard(transfer, maxPacket);
			/* both formats, the same answer */
			if (!sameEvents(both[0], both[1])) return QByteArray("differ");
			return both[0].isEmpty() ? QByteArray() : both[0].first().data;
		};
		const QByteArray data = pattern(1000);
		QCOMPARE(payload(ftdiTransfer(data, 64), 64), data);    /* full-speed: 17 packets */
		QCOMPARE(payload(ftdiTransfer(data, 512), 512), data);  /* high-speed: 3 */
		QCOMPARE(payload(ftdiTransfer(data, 64), 0), data);     /* not known: the 64-byte rule */
		QCOMPARE(payload(ftdiTransfer(pattern(62), 64), 64), pattern(62));  /* exactly one full packet */
		QCOMPARE(payload(ftdiTransfer(pattern(63), 64), 64), pattern(63));  /* one byte into the second */
		QVERIFY(heard(QByteArray("\x01\x60", 2), 64)[0].isEmpty());         /* a status poll: no data */
		QVERIFY(heard(QByteArray("\x01", 1), 64)[0].isEmpty());             /* cut inside the status */
		/* a transfer that ends on a packet's status bytes: nothing after them */
		QCOMPARE(payload(ftdiTransfer(pattern(62), 64) + QByteArray("\x01\x60", 2), 64), pattern(62));

		/* OUT carries no status, and an FTDI's OUT with data at a multiple of the packet size keeps every byte */
		Packet out;
		out.endpoint = FTDI_OUT;
		out.completion = false;
		out.data = pattern(128);
		const QVector<UsbEvent> outs = heardPcap({ out }, ftdiTarget());
		QCOMPARE(outs.size(), 1);
		QCOMPARE(outs.first().data, pattern(128));
	}

	/* the second device on the bus: its traffic is another target's, and each hears its own */
	void deviceFilter() {
		UsbTarget other = cdcTarget();
		other.address = OTHER_ADDRESS;
		for (const QVector<UsbEvent> &events : { decodedPcap(QStringLiteral("cdc_poll"), other), decodedMon(QStringLiteral("cdc_poll"), other) }) {
			const QVector<evre::Frame> requests = framesOf(events, UsbDirection::Out);
			QCOMPARE(requests.size(), 20);
			QCOMPARE(framesOf(events, UsbDirection::In).size(), 20);
			for (const evre::Frame &frame : requests) QCOMPARE(frame.slave, OTHER_SLAVE);
		}
	}

	/* a composite device: the other function's endpoints are not ours; a lookup that found none hears both */
	void endpointFilter() {
		const QVector<UsbEvent> ours = decodedPcap(QStringLiteral("composite"), compositeTarget());
		QCOMPARE(framesOf(ours, UsbDirection::Out).size(), 10);
		QCOMPARE(framesOf(ours, UsbDirection::In).size(), 10);
		for (const evre::Frame &frame : framesOf(ours, UsbDirection::In)) QCOMPARE(frame.slave, uint8_t(1));

		const QVector<UsbEvent> theirs = decodedMon(QStringLiteral("composite"), cdcTarget());
		QCOMPARE(framesOf(theirs, UsbDirection::In).size(), 10);
		for (const evre::Frame &frame : framesOf(theirs, UsbDirection::In)) QCOMPARE(frame.slave, OTHER_SLAVE);

		UsbTarget all = compositeTarget();
		all.inEndpoints.clear();
		all.outEndpoints.clear();
		for (const QVector<UsbEvent> &events : { decodedPcap(QStringLiteral("composite"), all), decodedMon(QStringLiteral("composite"), all) }) {
			QCOMPARE(framesOf(events, UsbDirection::Out).size(), 20);
			QCOMPARE(framesOf(events, UsbDirection::In).size(), 20);
			/* every bulk endpoint, and still no interrupt notification */
			for (const UsbEvent &event : events) QVERIFY(event.endpoint != 0x82 && event.endpoint != 0x84);
		}
	}

	/* the rule of a transfer's two halves, in both formats */
	void transferRules_data() {
		QTest::addColumn<int>("transfer");
		QTest::addColumn<int>("endpoint");
		QTest::addColumn<bool>("completion");
		QTest::addColumn<int>("status");
		QTest::addColumn<bool>("heard");
		QTest::newRow("OUT submitted") << BULK << 0x01 << false << 0 << true;
		QTest::newRow("OUT completed") << BULK << 0x01 << true << 0 << false;
		QTest::newRow("IN completed") << BULK << 0x81 << true << 0 << true;
		QTest::newRow("IN submitted") << BULK << 0x81 << false << 0 << false;
		QTest::newRow("IN failed") << BULK << 0x81 << true << -32 << false;
		QTest::newRow("interrupt") << INTERRUPT << 0x81 << true << 0 << false;
		QTest::newRow("control") << CONTROL << 0x81 << true << 0 << false;
	}
	void transferRules() {
		QFETCH(int, transfer);
		QFETCH(int, endpoint);
		QFETCH(bool, completion);
		QFETCH(int, status);
		QFETCH(bool, heard);
		Packet packet;
		packet.transfer = transfer;
		packet.endpoint = endpoint;
		packet.completion = completion;
		packet.status = status;
		packet.data = pattern(10);
		const QVector<UsbEvent> pcap = heardPcap({ packet }, cdcTarget());
		const QVector<UsbEvent> mon = heardMon({ packet }, cdcTarget());
		QCOMPARE(pcap.size(), heard ? 1 : 0);
		QCOMPARE(mon.size(), heard ? 1 : 0);
		if (!heard) return;
		QCOMPARE(pcap.first().data, packet.data);
		QVERIFY(sameEvent(pcap.first(), mon.first()));
		QCOMPARE(pcap.first().stamp, qint64(1790000000) * 1000000 + 250);
		QCOMPARE(pcap.first().direction, completion ? UsbDirection::In : UsbDirection::Out);
	}

	void recordEdges() {
		/* the header's own length is obeyed: a driver's longer header is not data */
		Packet longer;
		longer.extraHeader = 5;
		longer.data = pattern(10);
		QVector<UsbEvent> events = heardPcap({ longer }, cdcTarget());
		QCOMPARE(events.size(), 1);
		QCOMPARE(events.first().data, longer.data);

		/* the snap length: the record holds less than the transfer had */
		Packet cut;
		cut.data = pattern(10);
		cut.sayLength = 4000;
		events = heardPcap({ cut }, cdcTarget());
		QCOMPARE(events.size(), 1);
		QCOMPARE(events.first().data, cut.data);

		/* a zero-length transfer is no event, and neither is usbmon's "no data" whatever its length */
		Packet empty;
		QVERIFY(heardPcap({ empty }, cdcTarget()).isEmpty());
		QVERIFY(heardMon({ empty }, cdcTarget()).isEmpty());
		Packet tagged;
		tagged.data = pattern(10);
		QVector<UsbEvent> none;
		UsbmonDecoder mon(cdcTarget());
		mon.feed(monRecord(tagged, '<'), none);
		QVERIFY(none.isEmpty());
		QCOMPARE(mon.pendingBytes(), qsizetype(0));

		/* a packet too short for its header is skipped, and the stream goes on behind it */
		QByteArray stream = pcapHeader();
		QByteArray bad;
		put32(bad, 1);
		put32(bad, 2);
		put32(bad, 5);
		put32(bad, 5);
		stream += bad + QByteArray(5, '\x01');
		Packet good;
		good.data = pattern(10);
		stream += pcapRecord(good);
		QCOMPARE(decoded<UsbPcapDecoder>(stream, cdcTarget()).size(), 1);
	}

	void notAStream() {
		QVector<UsbEvent> events;
		/* another program's pcap, or none at all: said, and nothing more is read */
		const auto refused = [&](const QByteArray &head) {
			UsbPcapDecoder decoder(cdcTarget());
			decoder.feed(head, events);
			if (decoder.error().isEmpty()) return false;
			Packet packet;
			packet.data = pattern(10);
			decoder.feed(pcapRecord(packet), events);
			return events.isEmpty() && decoder.pendingBytes() == 0;
		};
		QVERIFY(refused(pcapHeader(0xA1B2C3D4, 1)));    /* Ethernet */
		QVERIFY(refused(pcapHeader(0xD4C3B2A1)));       /* the other byte order */
		QVERIFY(refused(QByteArray(24, 'x')));

		/* a length no record can have: a broken stream, not a wait for ever */
		QByteArray stream = pcapHeader();
		put32(stream, 0);
		put32(stream, 0);
		put32(stream, 0x7FFFFFFF);
		put32(stream, 0x7FFFFFFF);
		UsbPcapDecoder decoder(cdcTarget());
		decoder.feed(stream, events);
		QVERIFY(!decoder.error().isEmpty());

		QByteArray head(64, '\0');
		head[39] = '\x7F';
		UsbmonDecoder mon(cdcTarget());
		mon.feed(head, events);
		QVERIFY(!mon.error().isEmpty());
		QVERIFY(events.isEmpty());
	}

	/* random bytes after a good head, and a good file with bytes changed: no crash, and the decoders keep to their rule */
	void fuzz() {
		std::mt19937 random(2);
		const QByteArray scene = readFile(captureFile(QStringLiteral("cdc_poll.pcap")));
		const QByteArray sceneMon = readFile(captureFile(QStringLiteral("cdc_poll.usbmon")));
		for (int round = 0; round < 2000; round++) {
			QByteArray noise(int(random() % 4000), '\0');
			for (char &c : noise) c = char(random());
			QByteArray pcap = round % 2 ? pcapHeader() + noise : scene;
			QByteArray mon = round % 2 ? noise : sceneMon;
			if (!(round % 2))
				for (int i = 0; i < 40; i++) {
					pcap[random() % pcap.size()] = char(random());
					mon[random() % mon.size()] = char(random());
				}
			const QVector<UsbEvent> a = decoded<UsbPcapDecoder>(pcap, cdcTarget(), int(random() % 200) + 1);
			const QVector<UsbEvent> b = decoded<UsbmonDecoder>(mon, cdcTarget());
			for (const QVector<UsbEvent> &events : { a, b })
				for (const UsbEvent &event : events) {
					QVERIFY(!event.data.isEmpty());
					QVERIFY(event.endpoint == 0x81 || event.endpoint == 0x01);
				}
		}
	}

	/* a file through the interface the live captures have: at start() nothing yet, then the events, then the end */
	void replayAtOnce_data() {
		QTest::addColumn<QString>("file");
		QTest::newRow("pcap") << QStringLiteral("cdc_poll.pcap");
		QTest::newRow("usbmon") << QStringLiteral("cdc_poll.usbmon");
	}
	void replayAtOnce() {
		QFETCH(QString, file);
		const QVector<UsbEvent> want = decodedPcap(QStringLiteral("cdc_poll"), cdcTarget());
		FileCapture capture(captureFile(file));
		QVector<UsbEvent> heard;
		QStringList ended;
		connect(&capture, &UsbCapture::data, this, [&](UsbDirection direction, const QByteArray &bytes) {
			UsbEvent event;
			event.direction = direction;
			event.data = bytes;
			heard.append(event);
		});
		connect(&capture, &UsbCapture::ended, this, [&](const QString &why) { ended.append(why); });
		capture.start(cdcTarget());
		QVERIFY(heard.isEmpty()); /* whoever started it says "opened" first */
		QTRY_COMPARE(ended.size(), 1);
		QVERIFY(!ended.first().isEmpty());
		QVERIFY(sameBytes(heard, want));
		QTest::qWait(20);
		QCOMPARE(ended.size(), 1);
	}

	/* paced: never ahead of its stamps, and in the same order */
	void replayPaced() {
		const QString file = captureFile(QStringLiteral("cdc_autosend.usbmon"));
		FileCapture capture(file, FileCapture::ByStamps);
		QElapsedTimer clock;
		qint64 lastAt = 0;
		int heard = 0;
		bool ended = false;
		connect(&capture, &UsbCapture::data, this, [&](UsbDirection, const QByteArray &) {
			heard++;
			lastAt = clock.elapsed();
		});
		connect(&capture, &UsbCapture::ended, this, [&] { ended = true; });
		clock.start();
		capture.start(cdcTarget());
		QTRY_VERIFY_WITH_TIMEOUT(ended, 10000);
		const QVector<UsbEvent> want = decodedMon(QStringLiteral("cdc_autosend"), cdcTarget());
		QCOMPARE(heard, want.size());
		QVERIFY(lastAt >= (want.last().stamp - want.first().stamp) / 1000 - 1);
	}

	/* stop() is the end: nothing after it, not even ended(), and a second start replays from the top */
	void replayStop() {
		FileCapture capture(captureFile(QStringLiteral("cdc_poll.pcap")));
		int heard = 0, ended = 0;
		connect(&capture, &UsbCapture::data, this, [&] {
			if (++heard == 5) capture.stop();
		});
		connect(&capture, &UsbCapture::ended, this, [&] { ended++; });
		capture.start(cdcTarget());
		QTest::qWait(50);
		QCOMPARE(heard, 5);
		QCOMPARE(ended, 0);

		capture.start(cdcTarget());
		capture.stop();
		QTest::qWait(20);
		QCOMPARE(heard, 5);
		QCOMPARE(ended, 0);
	}

	/* what cannot be replayed says so through ended(), from the event loop as everything else */
	void replayRefused() {
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto why = [&](const QString &path) {
			FileCapture capture(path);
			int heard = 0;
			QString said;
			connect(&capture, &UsbCapture::data, this, [&] { heard++; });
			connect(&capture, &UsbCapture::ended, this, [&](const QString &text) { said = text; });
			capture.start(cdcTarget());
			(void) QTest::qWaitFor([&] { return !said.isEmpty(); }, 2000);
			return std::make_pair(said, heard);
		};
		QVERIFY(why(dir.filePath(QStringLiteral("none.pcap"))).first.contains(QLatin1String("none.pcap")));
		QVERIFY(why(captureFile(QStringLiteral("cdc_poll.pcap")).replace(QLatin1String(".pcap"), QLatin1String(".txt"))).first.contains(QLatin1String(".txt")));

		/* a file that is no capture: said in the decoder's words */
		const QString text = dir.filePath(QStringLiteral("text.pcap"));
		QFile file(text);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(QByteArray(100, 'x'));
		file.close();
		QVERIFY(why(text).first.contains(QLatin1String("USBPcap")));

		/* a file cut inside a record: what is whole is heard, and it ends as any other */
		const QByteArray whole = readFile(captureFile(QStringLiteral("composite.pcap")));
		const QString cut = dir.filePath(QStringLiteral("cut.pcap"));
		QFile cutFile(cut);
		QVERIFY(cutFile.open(QIODevice::WriteOnly));
		cutFile.write(whole.left(whole.size() - 7));
		cutFile.close();
		FileCapture capture(cut);
		int heard = 0, ended = 0;
		connect(&capture, &UsbCapture::data, this, [&] { heard++; });
		connect(&capture, &UsbCapture::ended, this, [&] { ended++; });
		capture.start(compositeTarget());
		QTRY_COMPARE(ended, 1);
		QVERIFY(heard > 0);
		QVERIFY(heard <= decodedPcap(QStringLiteral("composite"), compositeTarget()).size());
	}

	/* the link: opened before anything is heard, the device's bytes as received(), the other program's as
	 * heardRequest(), each in its order, and the capture's end as closed() with its reason */
	void linkBothWays() {
		QVector<UsbEvent> want = decodedPcap(QStringLiteral("cdc_poll"), cdcTarget());
		evre::ListenLink link(QStringLiteral("COM13"), new FileCapture(captureFile(QStringLiteral("cdc_poll.pcap"))), cdcTarget());
		QByteArray in, out;
		QStringList closed;
		int opened = 0;
		bool heardBeforeOpen = false;
		connect(&link, &evre::Link::opened, this, [&] {
			opened++;
			heardBeforeOpen = !in.isEmpty() || !out.isEmpty();
		});
		connect(&link, &evre::Link::received, this, [&](const QByteArray &bytes) { in += bytes; });
		connect(&link, &evre::ListenLink::heardRequest, this, [&](const QByteArray &bytes) { out += bytes; });
		connect(&link, &evre::Link::closed, this, [&](const QString &why) { closed << why; });
		QVERIFY(!link.isOpen());
		link.open();
		QCOMPARE(opened, 1);
		QVERIFY(!heardBeforeOpen);
		QVERIFY(link.isOpen());
		QVERIFY(in.isEmpty() && out.isEmpty());
		QTRY_COMPARE(closed.size(), 1);
		QByteArray wantIn, wantOut;
		for (const UsbEvent &event : want) (event.direction == UsbDirection::In ? wantIn : wantOut) += event.data;
		QCOMPARE(in, wantIn);
		QCOMPARE(out, wantOut);
		QCOMPARE(closed.first(), QStringLiteral("the capture ended"));
		QVERIFY(!link.isOpen());
		QCOMPARE(link.describe(), QStringLiteral("COM13 (listening)"));
		QCOMPARE(link.place(), QStringLiteral("COM13 (bus 2, device 5)"));
	}

	/* close() is the end of the capture: nothing after it, and no reason given */
	void linkClosed() {
		evre::ListenLink link(QStringLiteral("COM13"), new FileCapture(captureFile(QStringLiteral("cdc_poll.pcap"))), cdcTarget());
		int heard = 0;
		QStringList closed;
		connect(&link, &evre::Link::received, this, [&] { heard++; });
		connect(&link, &evre::Link::closed, this, [&](const QString &why) { closed << why; });
		link.open();
		link.close();
		QTest::qWait(30);
		QCOMPARE(heard, 0);
		QCOMPARE(closed, QStringList{ QString() });
		QVERIFY(!link.isOpen());
	}

	/* nothing can hear this port: open() says so and the link stays shut; what send() is given goes nowhere, counted */
	void linkWithoutCapture() {
		evre::ListenLink link(QStringLiteral("COM13"), nullptr, UsbTarget());
		QStringList closed;
		connect(&link, &evre::Link::closed, this, [&](const QString &why) { closed << why; });
		link.open();
		QVERIFY(!link.isOpen());
		QCOMPARE(closed.size(), 1);
		QVERIFY(closed.first().startsWith(QLatin1String("COM13 cannot be listened to")));
		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("listening on COM13")));
		link.send(QByteArray("\x7B", 1));
		link.send(QByteArray("\x7B", 1)); /* said once */
		QCOMPARE(link.sendAttempts(), 2);
	}

	/* what capture says it lost comes through the link */
	void linkLostCaptures() {
		class Lossy : public UsbCapture {
		public:
			using UsbCapture::UsbCapture;
			void start(const UsbTarget &) override { emit dropped(7); }
			void stop() override {}
		};
		evre::ListenLink link(QStringLiteral("COM13"), new Lossy, cdcTarget());
		QList<quint64> lost;
		connect(&link, &evre::ListenLink::capturesLost, this, [&](quint64 total) { lost << total; });
		link.open();
		QCOMPARE(lost, QList<quint64>{ 7 });
	}

	/* Parser::skippedBytes: what left the stream without a frame, whatever way, and no frame's own bytes */
	void parserSkips() {
		const QByteArray frame = evre::build(1, evre::READ, 0xD000, 4);
		evre::Parser parser;
		evre::Frame found;
		parser.feed(frame);
		QVERIFY(parser.next(found));
		QCOMPARE(parser.skippedBytes(), quint64(0));
		parser.feed(QByteArray("hello, world"));            /* no start byte */
		QVERIFY(!parser.next(found));
		QCOMPARE(parser.skippedBytes(), quint64(12));
		parser.feed(QByteArray("abc") + frame);             /* before a frame */
		QVERIFY(parser.next(found));
		QCOMPARE(parser.skippedBytes(), quint64(15));
		parser.feed(QByteArray("\x7B\x01\x00\x00\x00\x00\x00", 7) + frame); /* a 7B that started nothing */
		QVERIFY(parser.next(found));
		QCOMPARE(parser.skippedBytes(), quint64(22));
		QByteArray bad = frame;
		bad[3] = char(bad[3] ^ 1);                          /* a wrong CRC: every byte of it, as no frame */
		parser.feed(bad + frame);
		QVERIFY(parser.next(found));
		QCOMPARE(parser.skippedBytes(), quint64(22 + bad.size()));
		QCOMPARE(parser.badFrames(), 1);
		parser.feed(frame.left(4));                         /* half a frame waits */
		QVERIFY(!parser.next(found));
		parser.clear();                                     /* and clearing is no skipping */
		QCOMPARE(parser.skippedBytes(), quint64(22 + bad.size()));
	}
};

QTEST_GUILESS_MAIN(ListenTest)
#include "listen_test.moc"

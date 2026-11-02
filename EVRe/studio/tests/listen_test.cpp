/* SPDX-License-Identifier: Apache-2.0 */
/* evre_listen_test: Listen without a window, a driver or a USB device (the capture sources of src/io/usb_capture.h):
 * the two decoders give the same events on every scene tests/make_captures.py wrote, FTDI's status bytes come off,
 * only the target's device and endpoints are heard, a stream may be cut at any byte, a file replays through the
 * interface the live captures have, a port finds its device in a sysfs the test lays out, and what Windows says of a
 * port is decided on made-up answers. UsbPcapCapture runs on a stand-in for USBPcapCMD: this same program, started again
 * (standIn, below), so that no test ever starts the real one, which below administrator would raise a UAC prompt. */
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QSerialPortInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <utility>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#undef interface /* a macro there, and a word here */
#else
#include <csignal>
#endif

#include "evre/frame.h"
#include "io/fast_stream.h"
#include "io/listen_link.h"
#include "io/usb_capture.h"
#include "io/usbpcap_capture.h"
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

/* A /sys and a /dev of the test's own, for usbmonPortState, laid out as the kernel does: real links and colons where
 * the system has them; on Windows a table for the links and "_" for the colon, which the lookup never looks at. */
class FakeSysfs {
public:
	FakeSysfs() {
		sys_ = dir_.path() + QStringLiteral("/sys");
		dev_ = dir_.path() + QStringLiteral("/dev");
		QDir().mkpath(sys_ + QStringLiteral("/class/tty"));
		QDir().mkpath(dev_);
	}

	/* a USB device: the directory with busnum, devnum and the IDs */
	QString device(const QString &name, int bus, int address, const char *vid, const char *pid) {
		const QString dir = sys_ + QStringLiteral("/devices/pci0000/usb%1/%2").arg(bus).arg(name);
		write(dir, "busnum", QByteArray::number(bus));
		write(dir, "devnum", QByteArray::number(address));
		write(dir, "idVendor", vid);
		write(dir, "idProduct", pid);
		return dir;
	}

	/* the kernel names it with the number in decimal and writes the file in hexadecimal */
	QString interface(const QString &device, int number, const char *interfaceClass) {
#ifdef Q_OS_WIN
		const QChar colon = QLatin1Char('_');
#else
		const QChar colon = QLatin1Char(':');
#endif
		const QString dir = device + QLatin1Char('/') + device.section(QLatin1Char('/'), -1) + colon + QStringLiteral("1.%1").arg(number);
		write(dir, "bInterfaceNumber", QByteArray::number(number, 16).rightJustified(2, '0'));
		write(dir, "bInterfaceClass", interfaceClass);
		return dir;
	}

	void endpoint(const QString &interface, int address, const char *type, const char *maxPacket) {
		const QByteArray hex = QByteArray::number(address, 16).rightJustified(2, '0');
		const QString dir = interface + QStringLiteral("/ep_") + QString::fromLatin1(hex);
		write(dir, "bEndpointAddress", hex);
		write(dir, "type", type);
		write(dir, "wMaxPacketSize", maxPacket);
	}

	void bulkPair(const QString &interface, int in, int out, const char *maxPacket) {
		endpoint(interface, in, "Bulk", maxPacket);
		endpoint(interface, out, "Bulk", maxPacket);
	}

	/* the tty and its link; an empty target: a tty with no device under it */
	void tty(const QString &name, const QString &target) {
		const QString dir = sys_ + QStringLiteral("/class/tty/") + name;
		QDir().mkpath(dir);
		QDir().mkpath(target);
		if (target.isEmpty()) return;
#ifdef Q_OS_WIN
		links_.insert(dir + QStringLiteral("/device"), target);
#else
		QVERIFY(QFile::link(target, dir + QStringLiteral("/device")));
#endif
	}

	void forgetTty(const QString &name) {
		QVERIFY(QDir(sys_ + QStringLiteral("/class/tty/") + name).removeRecursively());
		links_.remove(sys_ + QStringLiteral("/class/tty/") + name + QStringLiteral("/device"));
	}

	QString usbmon(int bus) {
		const QString file = dev_ + QStringLiteral("/usbmon%1").arg(bus);
		QFile out(file);
		out.open(QIODevice::WriteOnly);
		return file;
	}

	void write(const QString &dir, const char *name, const QByteArray &line) {
		QDir().mkpath(dir);
		QFile out(dir + QLatin1Char('/') + QLatin1String(name));
		QVERIFY(out.open(QIODevice::WriteOnly));
		out.write(line + '\n');
	}

	UsbPortState state(const QString &port) const { return usbmonPortState(sys_, dev_, port, links_); }

	QString dev() const { return dev_; }

private:
	QTemporaryDir dir_;
	QString sys_, dev_;
	QHash<QString, QString> links_;
};

} // namespace

/* A configuration descriptor, byte by byte, as a device would give it */
QByteArray bytesOf(std::initializer_list<int> values) {
	QByteArray out;
	for (int value : values) out.append(char(value));
	return out;
}

QByteArray interfaceDescriptor(int number, int klass, int alternate = 0) {
	return bytesOf({ 9, 4, number, alternate, 2, klass, 0, 0, 0 });
}

QByteArray endpointDescriptor(int address, int type, int maxPacket) {
	return bytesOf({ 7, 5, address, type, maxPacket & 0xFF, maxPacket >> 8, 0 });
}

QByteArray bulkPair(int in, int out, int maxPacket) {
	return endpointDescriptor(in, 2, maxPacket) + endpointDescriptor(out, 2, maxPacket);
}

QByteArray unionDescriptor(int control, int data) { return bytesOf({ 5, 0x24, 6, control, data }); }

/* a function's association, CDC's header: what stands between the descriptors that count */
QByteArray cdcPreamble(int first) { return bytesOf({ 8, 0x0B, first, 2, 2, 2, 0, 0 }) + bytesOf({ 5, 0x24, 0, 0x10, 1 }); }

QByteArray cdcControl(int number, int data, int notify) {
	return cdcPreamble(number) + interfaceDescriptor(number, 2) + unionDescriptor(number, data) + endpointDescriptor(notify, 3, 8);
}

QByteArray cdcFunction(int control, int notify, int in, int out, int maxPacket = 64) {
	return cdcControl(control, control + 1, notify) + interfaceDescriptor(control + 1, 0x0A) + bulkPair(in, out, maxPacket);
}

QByteArray configurationOf(const QByteArray &body) {
	const int total = 9 + int(body.size());
	return bytesOf({ 9, 2, total & 0xFF, total >> 8, 2, 1, 0, 0x80, 50 }) + body;
}

const QString HUB_GUID = QStringLiteral("#{f18a0e88-c30c-11d0-8815-00a0c906bed8}");

/* a board's own USB, on the second of two root hubs, with nothing in the way */
UsbPortFacts cdcFacts() {
	UsbPortFacts facts;
	facts.port = QStringLiteral("COM13");
	facts.chain = { QStringLiteral("USB\\VID_0483&PID_5740\\0001"), QStringLiteral("USB\\ROOT_HUB30\\4&1&0&0"),
		QStringLiteral("PCI\\VEN_1234&DEV_0001\\3&1&0&A0"), QStringLiteral("HTREE\\ROOT\\0") };
	facts.address = ADDRESS;
	facts.stack = { QStringLiteral("\\Driver\\usbser"), QStringLiteral("\\Driver\\USBPcap"), QStringLiteral("\\Driver\\USBHUB3") };
	facts.configuration = configurationOf(cdcFunction(0, 0x82, 0x81, 0x01));
	facts.filters = { QStringLiteral("\\??\\USB#ROOT_HUB20#3&9&0&0") + HUB_GUID, QStringLiteral("\\??\\USB#ROOT_HUB30#4&1&0&0") + HUB_GUID,
		QString() };
	facts.program = true;
	facts.elevated = true;
	return facts;
}

/* The stand-in for USBPcapCMD: what it does comes by the environment, its arguments are the capture's own. Like the
 * real one it takes no notice of terminate(): only a kill ends it. */
constexpr char STAND_IN[] = "EVRE_TEST_USBPCAPCMD";             /* stay, end, refused, code or garbage */
constexpr char STAND_IN_FILE[] = "EVRE_TEST_USBPCAPCMD_FILE";   /* what it writes to its standard output */
constexpr char STAND_IN_ARGS[] = "EVRE_TEST_USBPCAPCMD_ARGS";   /* where it notes its arguments */

int standIn(int argc, char **argv) {
#ifdef Q_OS_WIN
	(void) _setmode(_fileno(stdout), _O_BINARY);
#else
	std::signal(SIGTERM, SIG_IGN);
#endif
	const QByteArray what = qgetenv(STAND_IN);
	if (QFile notes(qEnvironmentVariable(STAND_IN_ARGS)); notes.open(QIODevice::WriteOnly)) {
		for (int i = 1; i < argc; i++) notes.write(QByteArray(argv[i]) + '\n');
#ifdef Q_OS_WIN
		/* what the real program's main thread would spin on: a line more than the arguments, for the check to see. A
		 * console program like this one is given its console for a missing handle; USBPcapCMD, which has none, gets none */
		if (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_PIPE) notes.write("a pipe for its standard input\n");
#endif
	}
	if (what == "refused") {
		fputs("Couldn't open device - 5\n", stderr);
		return 0;
	}
	if (what == "code") return 3;
	const QByteArray bytes = what == "garbage" ? QByteArray(64, 'x') : readFile(qEnvironmentVariable(STAND_IN_FILE));
	fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
	fflush(stdout);
	if (what == "end") return 0;
	for (;;) QThread::sleep(1);
}

/* the stand-in's orders for one test */
class StandInOrders {
public:
	StandInOrders(const QByteArray &what, const QString &file = QString()) {
		qputenv(STAND_IN, what);
		qputenv(STAND_IN_FILE, file.toUtf8());
		qputenv(STAND_IN_ARGS, args_.filePath(QStringLiteral("args.txt")).toUtf8());
	}
	~StandInOrders() {
		qunsetenv(STAND_IN);
		qunsetenv(STAND_IN_FILE);
		qunsetenv(STAND_IN_ARGS);
	}
	QStringList arguments() const {
		return QString::fromUtf8(readFile(args_.filePath(QStringLiteral("args.txt")))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
	}

private:
	QTemporaryDir args_;
};

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

	/* no capture: open() gives the reason of the port's state, in the window's words */
	void linkRefused_data() {
		QTest::addColumn<int>("kind");
		QTest::addColumn<QString>("said");
		QTest::newRow("not USB") << int(UsbPortState::NotUsb) << QStringLiteral("ttyS0 is not a USB port: it cannot be listened to");
		QTest::newRow("usbmon") << int(UsbPortState::NoUsbmon) << QStringLiteral("usbmon is not loaded or /dev/usbmon2 is not readable");
		QTest::newRow("USBPcap") << int(UsbPortState::NoUsbPcap) << QStringLiteral("USBPcap is not installed");
		QTest::newRow("no filter") << int(UsbPortState::NoFilter) << QStringLiteral("USBPcap is not on this device yet");
		QTest::newRow("administrator") << int(UsbPortState::NeedsAdmin) << QStringLiteral("run the Studio as administrator");
		QTest::newRow("no capture") << int(UsbPortState::NoCapture) << QStringLiteral("ttyS0 cannot be listened to: no USB capture on this system");
	}

	void linkRefused() {
		QFETCH(int, kind);
		QFETCH(QString, said);
		UsbPortState state;
		state.kind = UsbPortState::Kind(kind);
		state.device = QStringLiteral("/dev/usbmon2");
		evre::ListenLink link(QStringLiteral("ttyS0"), nullptr, UsbTarget());
		link.setRefused(state);
		QStringList closed;
		connect(&link, &evre::Link::closed, this, [&](const QString &why) { closed << why; });
		link.open();
		QCOMPARE(closed, QStringList{ said });
		QVERIFY(!link.isOpen());
	}

	/* a board's own USB: the tty on the control interface, the data on the next one; its interrupt endpoint is not data */
	void sysfsCdc() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-3"), 2, 5, "0483", "5740");
		const QString control = sys.interface(device, 0, "02");
		sys.endpoint(control, 0x82, "Interrupt", "0008");
		const QString data = sys.interface(device, 1, "0a");
		sys.bulkPair(data, 0x81, 0x01, "0040");
		sys.tty(QStringLiteral("ttyACM0"), control);
		sys.usbmon(2);
		for (const char *port : { "ttyACM0", "/dev/ttyACM0" }) {
			const UsbPortState state = sys.state(QLatin1String(port));
			QCOMPARE(state.kind, UsbPortState::Listenable);
			QCOMPARE(state.target.port, QStringLiteral("ttyACM0"));
			QCOMPARE(state.target.bus, 2);
			QCOMPARE(state.target.address, 5);
			QCOMPARE(state.target.vid, quint16(0x0483));
			QCOMPARE(state.target.pid, quint16(0x5740));
			QCOMPARE(state.target.kind, UsbTarget::Cdc);
			QCOMPARE(state.target.inEndpoints, QVector<int>{ 0x81 });
			QCOMPARE(state.target.outEndpoints, QVector<int>{ 0x01 });
			QCOMPARE(state.target.inMaxPacket, 64);
			QVERIFY(state.device.isEmpty());
		}
	}

	/* a bridge: the tty one level under its own interface, whose endpoints are the data; the kind by the VID */
	void sysfsBridge_data() {
		QTest::addColumn<QByteArray>("vid");
		QTest::addColumn<QByteArray>("maxPacket");
		QTest::addColumn<int>("kind");
		QTest::addColumn<int>("packet");
		QTest::newRow("FT232R") << QByteArray("0403") << QByteArray("0040") << int(UsbTarget::Ftdi) << 64;
		QTest::newRow("FT232H") << QByteArray("0403") << QByteArray("0200") << int(UsbTarget::Ftdi) << 512;
		QTest::newRow("CP210x") << QByteArray("10c4") << QByteArray("0040") << int(UsbTarget::OtherBridge) << 64;
		QTest::newRow("CH340") << QByteArray("1a86") << QByteArray("0020") << int(UsbTarget::OtherBridge) << 32;
	}

	void sysfsBridge() {
		QFETCH(QByteArray, vid);
		QFETCH(QByteArray, maxPacket);
		QFETCH(int, kind);
		QFETCH(int, packet);
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("1-2"), 1, 7, vid.constData(), "6001");
		const QString own = sys.interface(device, 0, "ff");
		sys.bulkPair(own, 0x81, 0x02, maxPacket.constData());
		sys.endpoint(own, 0x83, "Interrupt", "0008");
		sys.tty(QStringLiteral("ttyUSB0"), own + QStringLiteral("/ttyUSB0"));
		sys.usbmon(1);
		const UsbPortState state = sys.state(QStringLiteral("/dev/ttyUSB0"));
		QCOMPARE(state.kind, UsbPortState::Listenable);
		QCOMPARE(state.target.bus, 1);
		QCOMPARE(state.target.address, 7);
		QCOMPARE(int(state.target.kind), kind);
		QCOMPARE(state.target.inEndpoints, QVector<int>{ 0x81 });
		QCOMPARE(state.target.outEndpoints, QVector<int>{ 0x02 });
		QCOMPARE(state.target.inMaxPacket, packet);
	}

	/* an FTDI's VID on the cdc-acm driver is USB CDC: no status bytes to take off */
	void sysfsCdcByDriver() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-1"), 2, 3, "0403", "6015");
		const QString control = sys.interface(device, 0, "02");
		sys.bulkPair(sys.interface(device, 1, "0a"), 0x81, 0x01, "0040");
		sys.tty(QStringLiteral("ttyACM0"), control);
		sys.usbmon(2);
		QCOMPARE(sys.state(QStringLiteral("ttyACM0")).target.kind, UsbTarget::Cdc);
	}

	/* two functions on one device: each port its own data interface, never the other's */
	void sysfsComposite() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-4"), 2, 9, "0483", "5740");
		QString controls[3];
		const QVector<int> numbers{ 0, 2, 10 };
		for (int i = 0; i < 3; i++) {
			controls[i] = sys.interface(device, numbers[i], "02");
			sys.bulkPair(sys.interface(device, numbers[i] + 1, "0a"), 0x81 + i, 0x01 + i, "0200");
			sys.tty(QStringLiteral("ttyACM%1").arg(i), controls[i]);
		}
		sys.usbmon(2);
		for (int i = 0; i < 3; i++) {
			const UsbPortState state = sys.state(QStringLiteral("ttyACM%1").arg(i));
			QCOMPARE(state.kind, UsbPortState::Listenable);
			QCOMPARE(state.target.inEndpoints, QVector<int>{ 0x81 + i });
			QCOMPARE(state.target.outEndpoints, QVector<int>{ 0x01 + i });
			QCOMPARE(state.target.inMaxPacket, 512);
		}
	}

	/* the data interface before its control one, as the kernel's "switched interfaces" allows: the nearest one down */
	void sysfsDataBefore() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-5"), 2, 4, "1234", "0001");
		sys.bulkPair(sys.interface(device, 0, "0a"), 0x81, 0x01, "0040");
		const QString control = sys.interface(device, 1, "02");
		sys.tty(QStringLiteral("ttyACM0"), control);
		sys.usbmon(2);
		QCOMPARE(sys.state(QStringLiteral("ttyACM0")).target.inEndpoints, QVector<int>{ 0x81 });
	}

	/* a tty with no USB device above it, or with no device at all, or none: no USB traffic to read */
	void sysfsNotUsb() {
		FakeSysfs sys;
		sys.tty(QStringLiteral("ttyS0"), sys.dev() + QStringLiteral("/../sys/devices/pnp0/00_01"));
		sys.write(sys.dev() + QStringLiteral("/../sys/devices/pnp0/00_01"), "id", "PNP0501");
		sys.tty(QStringLiteral("tty1"), QString());
		sys.usbmon(2);
		QCOMPARE(sys.state(QStringLiteral("ttyS0")).kind, UsbPortState::NotUsb);
		QCOMPARE(sys.state(QStringLiteral("tty1")).kind, UsbPortState::NotUsb);
	}

	void sysfsMissing() {
		FakeSysfs sys;
		sys.usbmon(2);
		for (const char *port : { "ttyACM9", "/dev/ttyACM9", "/dev/", "" })
			QCOMPARE(sys.state(QLatin1String(port)).kind, UsbPortState::NotUsb);
	}

	/* a device without bulk endpoints, or without a data interface: the device is known, and an empty list hears every
	 * bulk endpoint (the Listen line says so) */
	void sysfsNoBulk() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-6"), 2, 8, "1234", "0002");
		const QString control = sys.interface(device, 0, "02");
		sys.endpoint(control, 0x82, "Interrupt", "0008");
		sys.tty(QStringLiteral("ttyACM0"), control);
		sys.usbmon(2);
		UsbPortState state = sys.state(QStringLiteral("ttyACM0"));
		QCOMPARE(state.kind, UsbPortState::Listenable);
		QCOMPARE(state.target.address, 8);
		QVERIFY(state.target.inEndpoints.isEmpty());
		QVERIFY(state.target.outEndpoints.isEmpty());
		QCOMPARE(state.target.inMaxPacket, 0);
		sys.endpoint(sys.interface(device, 1, "0a"), 0x83, "Interrupt", "0008");
		state = sys.state(QStringLiteral("ttyACM0"));
		QCOMPARE(state.kind, UsbPortState::Listenable);
		QVERIFY(state.target.inEndpoints.isEmpty());
	}

	/* a USB port on a bus that cannot be read: its file named, the target known; a USB port on another bus does not help */
	void sysfsNoUsbmon() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-3"), 2, 5, "0483", "5740");
		const QString control = sys.interface(device, 0, "02");
		sys.bulkPair(sys.interface(device, 1, "0a"), 0x81, 0x01, "0040");
		sys.tty(QStringLiteral("ttyACM0"), control);
		sys.usbmon(1);
		const UsbPortState state = sys.state(QStringLiteral("ttyACM0"));
		QCOMPARE(state.kind, UsbPortState::NoUsbmon);
		QCOMPARE(state.device, sys.dev() + QStringLiteral("/usbmon2"));
		QCOMPARE(state.target.bus, 2);
		const QString file = sys.usbmon(2);
		QCOMPARE(sys.state(QStringLiteral("ttyACM0")).kind, UsbPortState::Listenable);
#ifdef Q_OS_UNIX
		QVERIFY(QFile::setPermissions(file, QFile::WriteOwner));
		if (!QFileInfo(file).isReadable()) QCOMPARE(sys.state(QStringLiteral("ttyACM0")).kind, UsbPortState::NoUsbmon); /* root reads all */
		QVERIFY(QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner));
#else
		Q_UNUSED(file);
#endif
	}

	/* what the live capture looks at: unplugged, plugged in again on another address, gone from the list */
	void sysfsHolds() {
		FakeSysfs sys;
		const QString device = sys.device(QStringLiteral("2-3"), 2, 5, "0483", "5740");
		const QString control = sys.interface(device, 0, "02");
		sys.tty(QStringLiteral("ttyACM0"), control);
		sys.usbmon(2);
		const UsbTarget running = sys.state(QStringLiteral("ttyACM0")).target;
		QVERIFY(sys.state(QStringLiteral("ttyACM0")).holds(running));
		sys.write(device, "devnum", "6");
		QVERIFY(!sys.state(QStringLiteral("ttyACM0")).holds(running));
		sys.write(device, "devnum", "5");
		QVERIFY(sys.state(QStringLiteral("ttyACM0")).holds(running));
		QFile::remove(sys.dev() + QStringLiteral("/usbmon2"));
		QVERIFY(sys.state(QStringLiteral("ttyACM0")).holds(running)); /* the capture running has its file open */
		sys.forgetTty(QStringLiteral("ttyACM0"));
		QVERIFY(!sys.state(QStringLiteral("ttyACM0")).holds(running));
	}

	/* a forced state wins, NoCapture too; the system's own answer is under it, and the system's capture has its place */
	void systemAnswer() {
		const QString port = QStringLiteral("noSuchPortHere");
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
		const UsbPortState::Kind none = UsbPortState::NotUsb;
#else
		const UsbPortState::Kind none = UsbPortState::NoCapture;
#endif
		QCOMPARE(UsbTarget::stateOf(port).kind, none);
		UsbPortState forced;
		forced.kind = UsbPortState::NoCapture;
		UsbTarget::stateForTests(port, forced);
		QCOMPARE(UsbTarget::stateOf(port).kind, UsbPortState::NoCapture);
		QCOMPARE(UsbTarget::liveStateOf(port).kind, none);
		forced.kind = UsbPortState::Listenable;
		UsbTarget::stateForTests(port, forced);
		QCOMPARE(UsbTarget::stateOf(port).kind, UsbPortState::Listenable);
		QCOMPARE(UsbTarget::liveStateOf(port).kind, none);
	}

	/* The live source without hardware: a bus nobody has. Its start returns at once, the reason comes from the event loop
	 * as a replayed file's does, once, and nothing follows a stop. Elsewhere there is no live source yet. */
	void liveCapture() {
		std::unique_ptr<UsbCapture> capture(UsbCapture::live());
#ifdef Q_OS_LINUX
		QVERIFY(capture);
		UsbTarget target = cdcTarget();
		target.bus = 987;
		QStringList ended;
		connect(capture.get(), &UsbCapture::ended, this, [&](const QString &why) { ended << why; });
		capture->start(target);
		capture->stop();
		QTest::qWait(30);
		QVERIFY(ended.isEmpty());
		capture->start(target);
		QVERIFY(ended.isEmpty());
		QTRY_COMPARE(ended.size(), 1);
		QVERIFY2(ended.first().startsWith(QLatin1String("/dev/usbmon987 cannot be read: ")), qPrintable(ended.first()));
		QTest::qWait(30);
		QCOMPARE(ended.size(), 1);
#elif defined(Q_OS_WIN)
		/* only made, never started: this is the real USBPcapCMD, and below administrator it raises a UAC prompt */
		QVERIFY(qobject_cast<UsbPcapCapture *>(capture.get()));
#else
		QVERIFY(!capture);
#endif
	}

	/* Windows's lookup on the PC it runs on. No device is needed: a port that is not there is no USB port, and every real
	 * one gets an answer, which only a board can say is right, without touching the port */
	void windowsLookup() {
#ifdef Q_OS_WIN
		const QString missing = QStringLiteral("COM999");
		QVERIFY(QSerialPortInfo(missing).isNull());
		const UsbPortFacts facts = usbPcapFacts(missing);
		QVERIFY(facts.chain.isEmpty());
		QCOMPARE(usbPcapPortState(facts).kind, UsbPortState::NotUsb);
		UsbTarget gone;
		gone.port = missing;
		QVERIFY(!UsbTarget::stillThere(gone));
		for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
			QVERIFY(UsbTarget::liveStateOf(info.portName()).kind != UsbPortState::NoCapture);
#else
		QSKIP("Windows's own lookup");
#endif
	}

	/* a device with one function: no MI_xx, and the data interface is the one the control interface's Union names */
	void descriptorCdc() {
		UsbTarget target;
		target.vid = 0x0483;
		target.kind = UsbTarget::Ftdi; /* to be overwritten */
		QVERIFY(usbReadEndpoints(configurationOf(cdcFunction(0, 0x82, 0x81, 0x01)), -1, target));
		QCOMPARE(target.inEndpoints, QVector<int>{ 0x81 });
		QCOMPARE(target.outEndpoints, QVector<int>{ 0x01 });
		QCOMPARE(target.inMaxPacket, 64);
		QCOMPARE(target.kind, UsbTarget::Cdc);
	}

	/* two CDC functions on one device, MI_xx picks one; its interrupt endpoint and the other's bulk ones stay out */
	void descriptorComposite_data() {
		QTest::addColumn<int>("interface");
		QTest::addColumn<int>("in");
		QTest::addColumn<int>("out");
		QTest::newRow("the first function") << 0 << 0x81 << 0x01;
		QTest::newRow("the second function") << 2 << 0x83 << 0x03;
	}

	void descriptorComposite() {
		QFETCH(int, interface);
		QFETCH(int, in);
		QFETCH(int, out);
		UsbTarget target;
		QVERIFY(usbReadEndpoints(configurationOf(cdcFunction(0, 0x84, 0x81, 0x01) + cdcFunction(2, 0x85, 0x83, 0x03)), interface, target));
		QCOMPARE(target.inEndpoints, QVector<int>{ in });
		QCOMPARE(target.outEndpoints, QVector<int>{ out });
		QCOMPARE(target.kind, UsbTarget::Cdc);
	}

	/* a device's numbering need not be neighbours: Union is the answer, and only without it the nearest one */
	void descriptorUnion() {
		const QByteArray distant = cdcControl(0, 5, 0x82) + interfaceDescriptor(1, 0x0A) + bulkPair(0x81, 0x01, 64)
				+ interfaceDescriptor(5, 0x0A) + bulkPair(0x83, 0x03, 64);
		UsbTarget byUnion;
		QVERIFY(usbReadEndpoints(configurationOf(distant), 0, byUnion));
		QCOMPARE(byUnion.inEndpoints, QVector<int>{ 0x83 });
		const QByteArray noUnion = interfaceDescriptor(0, 2) + endpointDescriptor(0x82, 3, 8) + interfaceDescriptor(1, 0x0A)
				+ bulkPair(0x81, 0x01, 64) + interfaceDescriptor(5, 0x0A) + bulkPair(0x83, 0x03, 64);
		UsbTarget nearest;
		QVERIFY(usbReadEndpoints(configurationOf(noUnion), 0, nearest));
		QCOMPARE(nearest.inEndpoints, QVector<int>{ 0x81 });
	}

	/* endpoints on an alternate setting of the data interface, as some CDC stacks declare them */
	void descriptorAlternate() {
		const QByteArray body = cdcControl(0, 1, 0x82) + interfaceDescriptor(1, 0x0A) + interfaceDescriptor(1, 0x0A, 1)
				+ bulkPair(0x81, 0x01, 512);
		UsbTarget target;
		QVERIFY(usbReadEndpoints(configurationOf(body), 0, target));
		QCOMPARE(target.inEndpoints, QVector<int>{ 0x81 });
		QCOMPARE(target.inMaxPacket, 512);
	}

	/* bridges: a vendor's class on one interface, the packet size that FTDI's status bytes go by */
	void descriptorBridge_data() {
		QTest::addColumn<int>("vid");
		QTest::addColumn<QByteArray>("body");
		QTest::addColumn<int>("interface");
		QTest::addColumn<int>("in");
		QTest::addColumn<int>("out");
		QTest::addColumn<int>("packet");
		QTest::addColumn<int>("kind");
		const QByteArray ft232r = interfaceDescriptor(0, 0xFF) + bulkPair(0x81, 0x02, 64);
		const QByteArray ft2232h = interfaceDescriptor(0, 0xFF) + bulkPair(0x81, 0x02, 512)
				+ interfaceDescriptor(1, 0xFF) + bulkPair(0x83, 0x04, 512);
		QTest::newRow("FT232R") << 0x0403 << ft232r << -1 << 0x81 << 0x02 << 64 << int(UsbTarget::Ftdi);
		QTest::newRow("FT2232H, its second port") << 0x0403 << ft2232h << 1 << 0x83 << 0x04 << 512 << int(UsbTarget::Ftdi);
		QTest::newRow("CP210x") << 0x10C4 << ft232r << -1 << 0x81 << 0x02 << 64 << int(UsbTarget::OtherBridge);
		QTest::newRow("a CDC chip of a bridge's maker") << 0x1A86 << cdcFunction(0, 0x82, 0x81, 0x01) << -1 << 0x81 << 0x01 << 64
				<< int(UsbTarget::Cdc);
	}

	void descriptorBridge() {
		QFETCH(int, vid);
		QFETCH(QByteArray, body);
		QFETCH(int, interface);
		QFETCH(int, in);
		QFETCH(int, out);
		QFETCH(int, packet);
		QFETCH(int, kind);
		UsbTarget target;
		target.vid = quint16(vid);
		QVERIFY(usbReadEndpoints(configurationOf(body), interface, target));
		QCOMPARE(target.inEndpoints, QVector<int>{ in });
		QCOMPARE(target.outEndpoints, QVector<int>{ out });
		QCOMPARE(target.inMaxPacket, packet);
		QCOMPARE(int(target.kind), kind);
	}

	/* nothing to hear: false, and the target as it was, which makes the capture hear every bulk endpoint */
	void descriptorNoBulk_data() {
		QTest::addColumn<QByteArray>("body");
		QTest::addColumn<int>("interface");
		QTest::newRow("a HID device") << (interfaceDescriptor(0, 3) + endpointDescriptor(0x81, 3, 8)) << -1;
		QTest::newRow("no interfaces") << QByteArray() << -1;
		QTest::newRow("no such interface") << (interfaceDescriptor(0, 0xFF) + bulkPair(0x81, 0x02, 64)) << 3;
		QTest::newRow("a CDC function with no data interface") << cdcControl(0, 1, 0x82) << 0;
		QTest::newRow("bulk one way only") << (interfaceDescriptor(0, 0xFF) + endpointDescriptor(0x81, 2, 64)) << 0;
	}

	void descriptorNoBulk() {
		QFETCH(QByteArray, body);
		QFETCH(int, interface);
		UsbTarget target = cdcTarget();
		QVERIFY(!usbReadEndpoints(configurationOf(body), interface, target));
		QCOMPARE(target.inEndpoints, cdcTarget().inEndpoints);
		QCOMPARE(target.outEndpoints, cdcTarget().outEndpoints);
		QCOMPARE(target.kind, cdcTarget().kind);
	}

	/* cut anywhere it is refused whole: half a list of endpoints would stop a direction being heard */
	void descriptorCut() {
		const QByteArray whole = configurationOf(cdcFunction(0, 0x84, 0x81, 0x01) + cdcFunction(2, 0x85, 0x83, 0x03));
		UsbTarget target;
		QVERIFY(usbReadEndpoints(whole, 2, target));
		for (int size = 0; size < whole.size(); size++) {
			UsbTarget cut;
			QVERIFY2(!usbReadEndpoints(whole.left(size), 2, cut), qPrintable(QString::number(size)));
			QVERIFY(cut.inEndpoints.isEmpty());
		}
	}

	/* lengths that cannot be: refused, and no endless walk */
	void descriptorBroken() {
		QByteArray zero = configurationOf(interfaceDescriptor(0, 0xFF) + bulkPair(0x81, 0x02, 64));
		zero[9] = 0;
		UsbTarget target;
		QVERIFY(!usbReadEndpoints(zero, -1, target));
		QByteArray overrun = configurationOf(interfaceDescriptor(0, 0xFF));
		overrun[9] = char(200);
		QVERIFY(!usbReadEndpoints(overrun, -1, target));
		QVERIFY(!usbReadEndpoints(QByteArray("not a descriptor"), -1, target));
		QVERIFY(!usbReadEndpoints(endpointDescriptor(0x81, 2, 64), -1, target));
		/* an endpoint before any interface belongs to nothing */
		QVERIFY(!usbReadEndpoints(configurationOf(endpointDescriptor(0x81, 2, 64)), -1, target));
	}

	void interfaceOfInstance_data() {
		QTest::addColumn<QString>("instance");
		QTest::addColumn<int>("number");
		QTest::newRow("the first") << QStringLiteral("USB\\VID_1234&PID_0001&MI_00\\6&2&0&0000") << 0;
		QTest::newRow("the third") << QStringLiteral("USB\\VID_1234&PID_0001&MI_02\\6&2&0&0002") << 2;
		QTest::newRow("hexadecimal") << QStringLiteral("USB\\VID_1234&PID_0001&MI_0A\\6&2&0&000A") << 10;
		QTest::newRow("as Windows may write it") << QStringLiteral("usb\\vid_1234&pid_0001&mi_01\\6&2&0&0001") << 1;
		QTest::newRow("a whole device") << QStringLiteral("USB\\VID_1234&PID_0001\\0001") << -1;
		QTest::newRow("no digits") << QStringLiteral("USB\\VID_1234&PID_0001&MI_\\0") << -1;
	}

	void interfaceOfInstance() {
		QFETCH(QString, instance);
		QFETCH(int, number);
		QCOMPARE(usbInterfaceOf(instance), number);
	}

	/* the walk up the device tree */
	void chainWalk_data() {
		QTest::addColumn<QStringList>("chain");
		QTest::addColumn<int>("device");
		QTest::addColumn<int>("rootHub");
		QTest::addColumn<int>("interface");
		const QString root = QStringLiteral("USB\\ROOT_HUB30\\4&1&0&0");
		const QString pci = QStringLiteral("PCI\\VEN_1234&DEV_0001\\3&1&0&A0");
		const QString tree = QStringLiteral("HTREE\\ROOT\\0");
		const QString device = QStringLiteral("USB\\VID_1234&PID_0001\\0001");
		const QString function = QStringLiteral("USB\\VID_1234&PID_0001&MI_02\\6&2&0&0002");
		const QString hub = QStringLiteral("USB\\VID_1234&PID_0002\\5&1&0&1");
		const QString ftdiBus = QStringLiteral("FTDIBUS\\VID_0403+PID_6010+A1A\\0000");
		const QString bluetooth = QStringLiteral("BTHENUM\\{00001101-0000-1000-8000-00805F9B34FB}_LOCALMFG&0002\\7&1&0&000000000000_00000001");
		QTest::newRow("CDC on the root hub") << QStringList{ device, root, pci, tree } << 0 << 1 << -1;
		QTest::newRow("a composite's function") << QStringList{ function, device, root, pci, tree } << 1 << 2 << 2;
		QTest::newRow("FTDI's own bus") << QStringList{ ftdiBus, device, root, pci, tree } << 1 << 2 << -1;
		QTest::newRow("FTDI's bus on a function") << QStringList{ ftdiBus, function, device, root, pci, tree } << 2 << 3 << 2;
		QTest::newRow("behind two hubs") << QStringList{ device, hub, hub, root, pci, tree } << 0 << 3 << -1;
		const UsbChain none;
		QTest::newRow("a Bluetooth port on a USB radio")
				<< QStringList{ bluetooth, QStringLiteral("BTH\\MS_BTHBRB\\7&1&0&1"), device, root, pci, tree }
				<< none.device << none.rootHub << none.firstInterface;
		QTest::newRow("a motherboard's port") << QStringList{ QStringLiteral("ACPI\\PNP0501\\0"), tree }
				<< none.device << none.rootHub << none.firstInterface;
		QTest::newRow("a virtual pair") << QStringList{ QStringLiteral("ROOT\\PORTS\\0000"), tree }
				<< none.device << none.rootHub << none.firstInterface;
		QTest::newRow("a root hub alone") << QStringList{ root, pci, tree } << none.device << none.rootHub << none.firstInterface;
		QTest::newRow("nothing") << QStringList{} << none.device << none.rootHub << none.firstInterface;
	}

	void chainWalk() {
		QFETCH(QStringList, chain);
		QFETCH(int, device);
		QFETCH(int, rootHub);
		QFETCH(int, interface);
		const UsbChain found = usbChainOf(chain);
		QCOMPARE(found.device, device);
		QCOMPARE(found.rootHub, rootHub);
		QCOMPARE(found.firstInterface, interface);
	}

	void hubLink_data() {
		QTest::addColumn<QString>("link");
		QTest::addColumn<QString>("rootHub");
		QTest::addColumn<bool>("same");
		const QString hub = QStringLiteral("USB\\ROOT_HUB30\\4&1d3f2a&0&0");
		QTest::newRow("the driver's name") << (QStringLiteral("\\??\\USB#ROOT_HUB30#4&1d3f2a&0&0") + HUB_GUID) << hub << true;
		QTest::newRow("in Windows's other case") << (QStringLiteral("\\??\\usb#root_hub30#4&1D3F2A&0&0") + HUB_GUID) << hub << true;
		QTest::newRow("a device interface's path") << (QStringLiteral("\\\\?\\USB#ROOT_HUB30#4&1d3f2a&0&0") + HUB_GUID) << hub << true;
		QTest::newRow("without a prefix") << (QStringLiteral("USB#ROOT_HUB30#4&1d3f2a&0&0") + HUB_GUID) << hub << true;
		QTest::newRow("another controller") << (QStringLiteral("\\??\\USB#ROOT_HUB30#4&1d3f2a&0&1") + HUB_GUID) << hub << false;
		QTest::newRow("another kind") << (QStringLiteral("\\??\\USB#ROOT_HUB20#4&1d3f2a&0&0") + HUB_GUID) << hub << false;
		QTest::newRow("no answer") << QString() << hub << false;
		QTest::newRow("nothing to compare") << QString() << QString() << false;
	}

	void hubLink() {
		QFETCH(QString, link);
		QFETCH(QString, rootHub);
		QFETCH(bool, same);
		QCOMPARE(usbHubLinkIs(link, rootHub), same);
	}

	/* One device's traffic, in the plan's order; never -A, which would capture the keyboard on the same hub */
	void pcapArguments() {
		UsbTarget target = cdcTarget();
		target.bus = 2;
		target.address = 5;
		const QStringList arguments = usbPcapArguments(target);
		QCOMPARE(arguments, QStringList({ QStringLiteral("-d"), QStringLiteral("\\\\.\\USBPcap2"), QStringLiteral("--devices"),
				QStringLiteral("5"), QStringLiteral("-s"), QStringLiteral("65535"), QStringLiteral("-b"), QStringLiteral("33554432"),
				QStringLiteral("-o"), QStringLiteral("-") }));
		for (const QString &word : { QStringLiteral("-A"), QStringLiteral("--capture-from-all-devices"), QStringLiteral("--capture-from-new-devices") })
			QVERIFY(!arguments.contains(word));
	}

	void pcapEndReason_data() {
		QTest::addColumn<int>("code");
		QTest::addColumn<bool>("crashed");
		QTest::addColumn<QByteArray>("errors");
		QTest::addColumn<QString>("said");
		QTest::newRow("another capture") << 0 << false << QByteArray("Couldn't open device - 5\nThread started with invalid read handle!\n")
				<< QStringLiteral("USBPcap refused this USB bus: another capture holds it");
		QTest::newRow("its first line") << 0 << false << QByteArray("Couldn't open device - 2\nThread started with invalid read handle!\r\n")
				<< QStringLiteral("USBPcapCMD stopped: Couldn't open device - 2");
		QTest::newRow("words before a crash") << 1 << true << QByteArray("Write failed (232). Stopping capture.\n")
				<< QStringLiteral("USBPcapCMD stopped: Write failed (232). Stopping capture.");
		QTest::newRow("a crash") << 0 << true << QByteArray() << QStringLiteral("USBPcapCMD crashed");
		QTest::newRow("a code") << 3 << false << QByteArray() << QStringLiteral("USBPcapCMD stopped with code 3");
		QTest::newRow("no word") << 0 << false << QByteArray("  \n") << QStringLiteral("USBPcapCMD stopped");
	}

	void pcapEndReason() {
		QFETCH(int, code);
		QFETCH(bool, crashed);
		QFETCH(QByteArray, errors);
		QFETCH(QString, said);
		QCOMPARE(usbPcapEndReason(code, crashed, errors), said);
	}

	void pcapProgramBeside_data() {
		QTest::addColumn<QString>("uninstall");
		QTest::addColumn<QString>("program");
		QTest::newRow("quoted") << QStringLiteral("\"D:\\Tools\\USBPcap\\Uninstall.exe\"") << QStringLiteral("D:/Tools/USBPcap/USBPcapCMD.exe");
		QTest::newRow("with an argument") << QStringLiteral("\"C:\\Program Files\\USBPcap\\Uninstall.exe\" /S")
				<< QStringLiteral("C:/Program Files/USBPcap/USBPcapCMD.exe");
		QTest::newRow("bare") << QStringLiteral("D:\\USBPcap\\Uninstall.exe") << QStringLiteral("D:/USBPcap/USBPcapCMD.exe");
		QTest::newRow("no entry") << QString() << QString();
		QTest::newRow("no folder") << QStringLiteral("Uninstall.exe") << QString();
	}

	void pcapProgramBeside() {
		QFETCH(QString, uninstall);
		QFETCH(QString, program);
		QCOMPARE(usbPcapProgramBeside(uninstall), program);
	}

	/* everything is there: the target as the capture needs it, the bus being the number of the root hub's filter */
	void pcapStateListenable() {
		const UsbPortState state = usbPcapPortState(cdcFacts());
		QCOMPARE(state.kind, UsbPortState::Listenable);
		QCOMPARE(state.target.port, QStringLiteral("COM13"));
		QCOMPARE(state.target.bus, 2);
		QCOMPARE(state.target.address, ADDRESS);
		QCOMPARE(state.target.vid, quint16(0x0483));
		QCOMPARE(state.target.pid, quint16(0x5740));
		QCOMPARE(state.target.kind, UsbTarget::Cdc);
		QCOMPARE(state.target.inEndpoints, QVector<int>{ 0x81 });
		QCOMPARE(state.target.outEndpoints, QVector<int>{ 0x01 });
		QVERIFY(state.holds(state.target));
		UsbPortFacts composite = cdcFacts();
		composite.chain.prepend(QStringLiteral("USB\\VID_0483&PID_5740&MI_02\\6&2&0&0002"));
		composite.configuration = configurationOf(cdcFunction(0, 0x84, 0x81, 0x01) + cdcFunction(2, 0x85, 0x83, 0x03));
		QCOMPARE(usbPcapPortState(composite).target.inEndpoints, QVector<int>{ 0x83 });
	}

	/* a filter number may be missing, after a dock went */
	void pcapStateBusGap() {
		UsbPortFacts facts = cdcFacts();
		facts.filters = { QString(), QString(), QString(), facts.filters[1] };
		QCOMPARE(usbPcapPortState(facts).target.bus, 4);
	}

	/* the descriptor not read: heard all the same, every bulk endpoint, and the kind by the VID */
	void pcapStateNoDescriptor_data() {
		QTest::addColumn<QByteArray>("configuration");
		QTest::addColumn<QString>("device");
		QTest::addColumn<int>("kind");
		const QString ftdi = QStringLiteral("USB\\VID_0403&PID_6001\\A1");
		QTest::newRow("not read, a CDC board") << QByteArray() << QStringLiteral("USB\\VID_1234&PID_0001\\0001") << int(UsbTarget::Cdc);
		QTest::newRow("not read, FTDI") << QByteArray() << ftdi << int(UsbTarget::Ftdi);
		QTest::newRow("not read, a bridge") << QByteArray() << QStringLiteral("USB\\VID_10C4&PID_EA60\\0001") << int(UsbTarget::OtherBridge);
		QTest::newRow("cut") << configurationOf(cdcFunction(0, 0x82, 0x81, 0x01)).left(30) << ftdi << int(UsbTarget::Ftdi);
	}

	void pcapStateNoDescriptor() {
		QFETCH(QByteArray, configuration);
		QFETCH(QString, device);
		QFETCH(int, kind);
		UsbPortFacts facts = cdcFacts();
		facts.chain[0] = device;
		facts.configuration = configuration;
		const UsbPortState state = usbPcapPortState(facts);
		QCOMPARE(state.kind, UsbPortState::Listenable);
		QVERIFY(state.target.inEndpoints.isEmpty() && state.target.outEndpoints.isEmpty());
		QCOMPARE(int(state.target.kind), kind);
	}

	/* each reason, and which wins when several hold: the first thing to do about it */
	void pcapStateRefused_data() {
		QTest::addColumn<int>("change");
		QTest::addColumn<int>("kind");
		enum { NoChain, NoProgram, NothingAtAll, NoFilters, OtherHub, NoStackEntry, NoAddress, NotElevated, NotElevatedNoFilter };
		QTest::newRow("no USB under the port") << int(NoChain) << int(UsbPortState::NotUsb);
		QTest::newRow("no USBPcapCMD") << int(NoProgram) << int(UsbPortState::NoUsbPcap);
		QTest::newRow("a motherboard's port, nothing installed") << int(NothingAtAll) << int(UsbPortState::NotUsb);
		QTest::newRow("installed, the driver not started") << int(NoFilters) << int(UsbPortState::NoFilter);
		QTest::newRow("the filter on another hub only") << int(OtherHub) << int(UsbPortState::NoFilter);
		QTest::newRow("plugged in before the install") << int(NoStackEntry) << int(UsbPortState::NoFilter);
		QTest::newRow("its hub did not say the address") << int(NoAddress) << int(UsbPortState::NoFilter);
		QTest::newRow("a normal user") << int(NotElevated) << int(UsbPortState::NeedsAdmin);
		QTest::newRow("a normal user, no filter") << int(NotElevatedNoFilter) << int(UsbPortState::NoFilter);
	}

	void pcapStateRefused() {
		QFETCH(int, change);
		QFETCH(int, kind);
		enum { NoChain, NoProgram, NothingAtAll, NoFilters, OtherHub, NoStackEntry, NoAddress, NotElevated, NotElevatedNoFilter };
		UsbPortFacts facts = cdcFacts();
		const QStringList noUsb{ QStringLiteral("ACPI\\PNP0501\\0"), QStringLiteral("HTREE\\ROOT\\0") };
		switch (change) {
		case NoChain: facts.chain = noUsb; break;
		case NoProgram: facts.program = false; break;
		case NothingAtAll:
			facts.chain = noUsb;
			facts.program = false;
			facts.elevated = false;
			break;
		case NoFilters: facts.filters.clear(); break;
		case OtherHub: facts.filters = QStringList{ facts.filters[0] }; break;
		case NoStackEntry: facts.stack.removeAll(QStringLiteral("\\Driver\\USBPcap")); break;
		case NoAddress: facts.address = 0; break;
		case NotElevated: facts.elevated = false; break;
		case NotElevatedNoFilter:
			facts.elevated = false;
			facts.filters.clear();
			break;
		default: break;
		}
		QCOMPARE(int(usbPcapPortState(facts).kind), kind);
	}

	/* a system that does not say the stack is no sign the filter is missing */
	void pcapStateNoStackAnswer() {
		UsbPortFacts facts = cdcFacts();
		facts.stack.clear();
		QCOMPARE(usbPcapPortState(facts).kind, UsbPortState::Listenable);
	}

	/* holds: what a capture running asks; the light lookup has no descriptor, and it must still hold. Plugged in again on a
	 * new address it does not, nor gone */
	void pcapHolds() {
		const UsbTarget running = usbPcapPortState(cdcFacts()).target;
		UsbPortFacts light = cdcFacts();
		light.configuration.clear();
		QVERIFY(usbPcapPortState(light).holds(running));
		light.address = 7;
		QVERIFY(!usbPcapPortState(light).holds(running));
		QVERIFY(!usbPcapPortState(UsbPortFacts()).holds(running));
	}

	/* The capture on the stand-in: what the decoder gives from the process's output comes out, nothing inside start(),
	 * started with the arguments asked; a stop ends it, a kill being what does, and nothing follows */
	void pcapCaptureHears() {
		const StandInOrders orders("stay", captureFile(QStringLiteral("cdc_poll.pcap")));
		UsbPcapCapture capture(QCoreApplication::applicationFilePath());
		capture.setStillThereForTests([](const UsbTarget &) { return true; });
		QVector<QPair<UsbDirection, QByteArray>> heard;
		QStringList ended;
		connect(&capture, &UsbCapture::data, this, [&](UsbDirection direction, const QByteArray &bytes) { heard.append({ direction, bytes }); });
		connect(&capture, &UsbCapture::ended, this, [&](const QString &why) { ended << why; });
		capture.start(cdcTarget());
		QVERIFY(heard.isEmpty());
		const QVector<UsbEvent> expected = decodedPcap(QStringLiteral("cdc_poll"), cdcTarget());
		QVERIFY(!expected.isEmpty());
		QTRY_COMPARE_WITH_TIMEOUT(heard.size(), expected.size(), 15000);
		for (qsizetype i = 0; i < expected.size(); i++) {
			QCOMPARE(heard[i].first, expected[i].direction);
			QCOMPARE(heard[i].second, expected[i].data);
		}
		QCOMPARE(orders.arguments(), usbPcapArguments(cdcTarget()));
		QPointer<QProcess> process = capture.findChild<QProcess *>();
		QVERIFY(process && process->state() == QProcess::Running);
		QElapsedTimer took;
		took.start();
		capture.stop();
		QVERIFY2(took.elapsed() >= 450, qPrintable(QString::number(took.elapsed()))); /* terminate() is given its 500 ms */
		QVERIFY(process && process->state() == QProcess::NotRunning);
		QTest::qWait(60);
		QVERIFY(ended.isEmpty());
		QCOMPARE(heard.size(), expected.size());
		QVERIFY(!process); /* and it is gone */
	}

	/* A process that ends is a capture that ends: what it wrote first is heard, the reason from the event loop, once */
	void pcapCaptureEnds_data() {
		QTest::addColumn<QByteArray>("what");
		QTest::addColumn<QString>("said");
		QTest::addColumn<bool>("hears");
		QTest::newRow("it ends") << QByteArray("end") << QStringLiteral("USBPcapCMD stopped") << true;
		QTest::newRow("another capture holds the bus") << QByteArray("refused") << QStringLiteral("USBPcap refused this USB bus: another capture holds it") << false;
		QTest::newRow("a code") << QByteArray("code") << QStringLiteral("USBPcapCMD stopped with code 3") << false;
	}

	void pcapCaptureEnds() {
		QFETCH(QByteArray, what);
		QFETCH(QString, said);
		QFETCH(bool, hears);
		const StandInOrders orders(what, captureFile(QStringLiteral("cdc_poll.pcap")));
		UsbPcapCapture capture(QCoreApplication::applicationFilePath());
		capture.setStillThereForTests([](const UsbTarget &) { return true; });
		qsizetype heard = 0;
		QStringList ended;
		connect(&capture, &UsbCapture::data, this, [&] { heard++; });
		connect(&capture, &UsbCapture::ended, this, [&](const QString &why) { ended << why; });
		capture.start(cdcTarget());
		QVERIFY(ended.isEmpty());
		QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 15000);
		QCOMPARE(ended.first(), said);
		QCOMPARE(heard, hears ? decodedPcap(QStringLiteral("cdc_poll"), cdcTarget()).size() : qsizetype(0));
		QTest::qWait(60);
		QCOMPARE(ended.size(), 1);
	}

	/* no such program: the reason from the event loop, once, and none after a stop */
	void pcapCaptureNoProgram() {
		UsbPcapCapture capture(QCoreApplication::applicationDirPath() + QStringLiteral("/no_such_program.exe"));
		capture.setStillThereForTests([](const UsbTarget &) { return true; });
		QStringList ended;
		connect(&capture, &UsbCapture::ended, this, [&](const QString &why) { ended << why; });
		capture.start(cdcTarget());
		capture.stop();
		QTest::qWait(30);
		QVERIFY(ended.isEmpty());
		capture.start(cdcTarget());
		QVERIFY(ended.isEmpty());
		QTRY_COMPARE(ended.size(), 1);
		QVERIFY2(ended.first().startsWith(QLatin1String("USBPcapCMD could not be started: ")), qPrintable(ended.first()));
		QTest::qWait(30);
		QCOMPARE(ended.size(), 1);
	}

	/* what is no pcap stream ends the capture, and the process with it */
	void pcapCaptureBroken() {
		const StandInOrders orders("garbage");
		UsbPcapCapture capture(QCoreApplication::applicationFilePath());
		capture.setStillThereForTests([](const UsbTarget &) { return true; });
		QStringList ended;
		connect(&capture, &UsbCapture::ended, this, [&](const QString &why) { ended << why; });
		capture.start(cdcTarget());
		QPointer<QProcess> process = capture.findChild<QProcess *>();
		QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 15000);
		QCOMPARE(ended.first(), QStringLiteral("not a USBPcap capture"));
		QVERIFY(!process || process->state() == QProcess::NotRunning);
	}

	/* USBPcapCMD goes on when the device is unplugged: the look finds it, stops the process and says so; and a process that
	 * ends on a device that went says that, not the program's words */
	void pcapCaptureGone_data() {
		QTest::addColumn<QByteArray>("what");
		QTest::newRow("it goes on") << QByteArray("stay");
		QTest::newRow("it ends too") << QByteArray("end");
	}

	void pcapCaptureGone() {
		QFETCH(QByteArray, what);
		const StandInOrders orders(what, captureFile(QStringLiteral("cdc_poll.pcap")));
		UsbPcapCapture capture(QCoreApplication::applicationFilePath());
		bool there = what == "stay";
		capture.setStillThereForTests([&](const UsbTarget &) { return there; });
		QStringList ended;
		connect(&capture, &UsbCapture::ended, this, [&](const QString &why) { ended << why; });
		qsizetype heard = 0;
		connect(&capture, &UsbCapture::data, this, [&] { heard++; });
		capture.start(cdcTarget());
		QPointer<QProcess> process = capture.findChild<QProcess *>();
		QTRY_VERIFY_WITH_TIMEOUT(heard > 0, 15000);
		there = false;
		QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 15000);
		QCOMPARE(ended.first(), QStringLiteral("COM13 is gone: unplugged or reset?"));
		QVERIFY(!process || process->state() == QProcess::NotRunning);
	}

	/* a listener may stop the capture from what it heard: nothing more comes, and nothing breaks */
	void pcapCaptureStoppedFromASlot() {
		const StandInOrders orders("stay", captureFile(QStringLiteral("cdc_poll.pcap")));
		UsbPcapCapture capture(QCoreApplication::applicationFilePath());
		capture.setStillThereForTests([](const UsbTarget &) { return true; });
		int heard = 0;
		connect(&capture, &UsbCapture::data, this, [&] {
			heard++;
			capture.stop();
		});
		capture.start(cdcTarget());
		QTRY_COMPARE_WITH_TIMEOUT(heard, 1, 15000);
		QTest::qWait(60);
		QCOMPARE(heard, 1);
		QVERIFY(!capture.findChild<QProcess *>() || capture.findChild<QProcess *>()->state() == QProcess::NotRunning);
	}

	/* the same capture started again runs a new process: Reconnect by itself does that */
	void pcapCaptureStartsAgain() {
		const StandInOrders orders("stay", captureFile(QStringLiteral("cdc_poll.pcap")));
		UsbPcapCapture capture(QCoreApplication::applicationFilePath());
		capture.setStillThereForTests([](const UsbTarget &) { return true; });
		qsizetype heard = 0;
		connect(&capture, &UsbCapture::data, this, [&] { heard++; });
		const qsizetype once = decodedPcap(QStringLiteral("cdc_poll"), cdcTarget()).size();
		capture.start(cdcTarget());
		QTRY_COMPARE_WITH_TIMEOUT(heard, once, 15000);
		capture.start(cdcTarget());
		QTRY_COMPARE_WITH_TIMEOUT(heard, 2 * once, 15000);
	}

	/* a transfer longer than the buffer: the header still says what usbmon kept, the bytes are what the buffer took, and
	 * the next record is found where the cut one ends */
	void fitRecord() {
		const int room = 64;
		QByteArray record(UsbmonDecoder::HEADER + room, '\0');
		record[8] = 'C';
		record[9] = char(BULK);
		record[10] = char(0x81);
		record[11] = char(ADDRESS);
		qToLittleEndian<quint32>(5000, record.data() + 36);
		record.replace(UsbmonDecoder::HEADER, room, pattern(room));
		const qsizetype whole = UsbmonDecoder::fitRecord(record.data(), room);
		QCOMPARE(whole, qsizetype(UsbmonDecoder::HEADER + room));
		QCOMPARE(qFromLittleEndian<quint32>(record.constData() + 36), quint32(room));
		QByteArray two = record;
		two += record;
		UsbmonDecoder decoder(cdcTarget());
		QVector<UsbEvent> events;
		decoder.feed(two, events);
		QCOMPARE(events.size(), 2);
		QCOMPARE(events.first().data, pattern(room));
		QCOMPARE(decoder.pendingBytes(), qsizetype(0));
		/* one that fits stays as it was */
		qToLittleEndian<quint32>(10, record.data() + 36);
		QCOMPARE(UsbmonDecoder::fitRecord(record.data(), room), qsizetype(UsbmonDecoder::HEADER + 10));
		QCOMPARE(qFromLittleEndian<quint32>(record.constData() + 36), quint32(10));
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

/* QTEST_GUILESS_MAIN but for the stand-in, which is this program started again */
int main(int argc, char *argv[]) {
	if (qEnvironmentVariableIsSet(STAND_IN)) return standIn(argc, argv);
	QCoreApplication app(argc, argv);
	ListenTest test;
	QTEST_SET_MAIN_SOURCE_PATH
	return QTest::qExec(&test, argc, argv);
}

#include "listen_test.moc"

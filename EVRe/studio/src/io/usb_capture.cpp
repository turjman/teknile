/* SPDX-License-Identifier: Apache-2.0 */
#include "io/usb_capture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <climits>
#include <memory>

/* Qt defines Q_OS_LINUX on Android too, where an app has no usbmon (and CMake builds usbmon_capture.cpp only for
 * Linux): Android takes the "no live capture" branches */
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#include "io/usbmon_capture.h"
#elif defined(Q_OS_WIN)
#include "io/usbpcap_capture.h"
#endif

namespace {

/* the pcap stream USBPcap writes */
constexpr quint32 PCAP_MAGIC = 0xA1B2C3D4;      /* microsecond stamps, little-endian: the driver's only form */
constexpr quint32 PCAP_LINK_USBPCAP = 249;
constexpr int PCAP_FILE_HEADER = 24;
constexpr int PCAP_RECORD_HEADER = 16;

/* USBPCAP_BUFFER_PACKET_HEADER, packed: headerLen 0, irpId 2, status 10, function 14, info 16, bus 17, device 19,
 * endpoint 21, transfer 22, dataLength 23. A control transfer's is a byte longer, which is why headerLen is obeyed. */
constexpr int PACKET_HEADER = 27;
constexpr uchar INFO_COMPLETION = 0x01;

/* struct mon_bin_hdr: id 0, type 8, xfer_type 9, epnum 10, devnum 11, busnum 12, flag_setup 14, flag_data 15, ts_sec 16,
 * ts_usec 24, status 28, len_urb 32, len_cap 36, then setup and the like up to UsbmonDecoder::HEADER */
constexpr int MON_LEN_CAP = 36;

constexpr int ENDPOINT_IN = 0x80;

constexpr int CDC_CONTROL_CLASS = 0x02;
constexpr int CDC_DATA_CLASS = 0x0A;
constexpr quint16 FTDI_VID = 0x0403;

/* USB 2.0 chapter 9's descriptors and CDC's Union functional descriptor, by their bytes */
constexpr uchar DESCRIPTOR_CONFIGURATION = 2;
constexpr uchar DESCRIPTOR_INTERFACE = 4;
constexpr uchar DESCRIPTOR_ENDPOINT = 5;
constexpr uchar DESCRIPTOR_CS_INTERFACE = 0x24;
constexpr uchar CDC_UNION = 6;
constexpr uchar ENDPOINT_TYPE_MASK = 3;
constexpr uchar ENDPOINT_TYPE_BULK = 2;
constexpr int MAX_PACKET_MASK = 0x7FF;

/* 32 MiB in the driver, which a pause of the Studio's thread can fall behind into. A full buffer drops packets and says
 * so nowhere, so what is lost shows only in the EVRe-level signs (the lost-capture text) */
constexpr int USBPCAP_SNAP_LENGTH = 65535;
constexpr int USBPCAP_BUFFER = 33554432;

template <typename T>
T little(const char *bytes) { return qFromLittleEndian<T>(bytes); }

qint64 stampOf(qint64 seconds, qint64 microseconds) {
	return qint64(quint64(seconds) * 1000000U + quint64(microseconds)); /* unsigned: a wrong stamp in a bad record cannot overflow */
}

QString brokenStream() {
	return QCoreApplication::translate("UsbCapture", "the capture's stream is broken: a record longer than any can be");
}

/* a sysfs file holds one line; a number in it is hexadecimal or decimal by the file, -1 when there is none */
QByteArray sysfsLine(const QString &file) {
	QFile in(file);
	return in.open(QIODevice::ReadOnly) ? in.readAll().trimmed() : QByteArray();
}

int sysfsNumber(const QString &file, int base) {
	bool ok = false;
	const int value = sysfsLine(file).toInt(&ok, base);
	return ok ? value : -1;
}

/* Up from the tty to the USB device above it, empty when there is none (a motherboard's port walks up to the root).
 * interface: the step below the device, the tty's own interface */
QString usbDeviceAbove(const QString &start, QString &interface) {
	QString below, dir = start;
	for (;;) {
		if (sysfsNumber(dir + QLatin1String("/busnum"), 10) > 0 && sysfsNumber(dir + QLatin1String("/devnum"), 10) > 0) {
			interface = below;
			return dir;
		}
		const QString up = QFileInfo(dir).path();
		if (up == dir || up.isEmpty() || up == QLatin1String(".")) return {};
		below = dir;
		dir = up;
	}
}

/* A USB CDC port's data, where its tty sits on the control interface: the data interface of the same function. A
 * composite device has one per function and sysfs does not say which is whose, but a function's interfaces are
 * neighbours: the next one up, or failing that the nearest one down. None: the tty's own (one interface doing both) */
QString cdcDataInterface(const QString &device, const QString &own) {
	const int control = sysfsNumber(own + QLatin1String("/bInterfaceNumber"), 16);
	QString found = own;
	int nearest = INT_MAX;
	const QStringList names = QDir(device).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
	for (const QString &name : names) {
		const QString dir = device + QLatin1Char('/') + name;
		if (sysfsNumber(dir + QLatin1String("/bInterfaceClass"), 16) != CDC_DATA_CLASS) continue;
		const int number = sysfsNumber(dir + QLatin1String("/bInterfaceNumber"), 16);
		const int distance = number > control ? number - control : 256 + control - number;
		if (distance < nearest) {
			nearest = distance;
			found = dir;
		}
	}
	return found;
}

/* The active configuration as the device sent it: 18 bytes of device descriptor, then each configuration in turn */
QByteArray sysfsConfiguration(const QString &device) {
	QFile in(device + QLatin1String("/descriptors"));
	if (!in.open(QIODevice::ReadOnly)) return {};
	const QByteArray all = in.readAll();
	const int active = sysfsNumber(device + QLatin1String("/bConfigurationValue"), 10);
	for (int at = 18; at + 9 <= all.size(); ) {
		const int total = little<quint16>(all.constData() + at + 2);
		if (total < 9) break;
		if (uchar(all[at + 5]) == active) return all.mid(at, total);
		at += total;
	}
	return {};
}

/* the interface's bulk endpoints; what its interrupt endpoint says of the line is not the data */
void readBulkEndpoints(const QString &interface, UsbTarget &target) {
	const QStringList names = QDir(interface).entryList({ QStringLiteral("ep_*") }, QDir::Dirs, QDir::Name);
	for (const QString &name : names) {
		const QString dir = interface + QLatin1Char('/') + name;
		const int address = sysfsNumber(dir + QLatin1String("/bEndpointAddress"), 16);
		if (address < 0 || sysfsLine(dir + QLatin1String("/type")) != "Bulk") continue;
		if (address & ENDPOINT_IN) {
			target.inEndpoints << address;
			if (!target.inMaxPacket) target.inMaxPacket = std::max(0, sysfsNumber(dir + QLatin1String("/wMaxPacketSize"), 16)) & 0x7FF;
		} else {
			target.outEndpoints << address;
		}
	}
}

} // namespace

UsbPortState usbmonPortState(const QString &sys, const QString &dev, const QString &port, const QHash<QString, QString> &links) {
	UsbPortState state;
	state.kind = UsbPortState::NotUsb;
	const QString tty = QFileInfo(port).fileName();
	if (tty.isEmpty()) return state;
	const QString ttyDir = sys + QLatin1String("/class/tty/") + tty;
	const QString link = ttyDir + QLatin1String("/device");
	const auto follow = [&links](const QString &file) { return links.contains(file) ? links.value(file) : QFileInfo(file).canonicalFilePath(); };
	state.missing = !links.contains(link) && !QFileInfo::exists(ttyDir);
	/* no such tty, or a tty with no device under it: a virtual one, a Bluetooth one */
	const QString start = follow(link);
	if (start.isEmpty()) return state;
	QString own;
	const QString device = usbDeviceAbove(start, own);
	if (device.isEmpty()) return state;

	UsbTarget &target = state.target;
	target.port = tty;
	target.bus = sysfsNumber(device + QLatin1String("/busnum"), 10);
	target.address = sysfsNumber(device + QLatin1String("/devnum"), 10);
	target.vid = quint16(sysfsNumber(device + QLatin1String("/idVendor"), 16));
	target.pid = quint16(sysfsNumber(device + QLatin1String("/idProduct"), 16));
	/* the driver's name decides, not the VID: ttyACM is cdc-acm, whose bulk data has no framing whoever made the chip */
	const bool cdc = tty.startsWith(QLatin1String("ttyACM"));
	/* A CDC port's data interface is the one its Union descriptor names, wherever that is: functions can be nested,
	 * crossed or data-first, and then the neighbours are another port's. They are the guess when there is nothing to read */
	if (!cdc || !usbReadEndpoints(sysfsConfiguration(device), sysfsNumber(own + QLatin1String("/bInterfaceNumber"), 16), target))
		readBulkEndpoints(cdc ? cdcDataInterface(device, own) : own, target);
	/* ftdi_sio takes FTDI chips under other makers' VIDs too */
	const bool ftdi = target.vid == FTDI_VID || follow(own + QLatin1String("/driver")).endsWith(QLatin1String("/ftdi_sio"));
	target.kind = cdc ? UsbTarget::Cdc : ftdi ? UsbTarget::Ftdi : UsbTarget::OtherBridge;

	const QFileInfo capture(dev + QLatin1String("/usbmon") + QString::number(target.bus));
	const bool readable = capture.exists() && capture.isReadable();
	state.kind = readable ? UsbPortState::Listenable : UsbPortState::NoUsbmon;
	if (!readable) state.device = capture.filePath();
	return state;
}

namespace {

/* one interface of a configuration, its alternate settings together: a data interface may have its endpoints only on
 * alternate 1 */
struct ConfiguredInterface {
	int number = 0;
	int klass = 0;
	int unionData = -1;         /* a control interface's data interface, by its Union descriptor */
	QVector<int> in, out;
	int inMaxPacket = 0;

	bool hasData() const { return !in.isEmpty() && !out.isEmpty(); }
};

ConfiguredInterface *interfaceNumbered(QVector<ConfiguredInterface> &interfaces, int number) {
	for (ConfiguredInterface &each : interfaces)
		if (each.number == number) return &each;
	return nullptr;
}

/* false when the bytes stop before the descriptor says they do, or a length cannot be right */
bool readConfiguration(const QByteArray &bytes, QVector<ConfiguredInterface> &interfaces) {
	if (bytes.size() < 9 || uchar(bytes[1]) != DESCRIPTOR_CONFIGURATION) return false;
	const int total = little<quint16>(bytes.constData() + 2);
	if (total < 9 || total > bytes.size()) return false;
	int current = -1;
	for (int at = 0; at < total; ) {
		const char *one = bytes.constData() + at;
		const int length = uchar(one[0]);
		if (length < 2 || at + length > total) return false;
		const uchar type = uchar(one[1]);
		if (type == DESCRIPTOR_INTERFACE && length >= 9) {
			current = uchar(one[2]);
			if (!interfaceNumbered(interfaces, current)) {
				ConfiguredInterface fresh;
				fresh.number = current;
				fresh.klass = uchar(one[5]);
				interfaces.append(fresh);
			}
		} else if (type == DESCRIPTOR_CS_INTERFACE && length >= 5 && uchar(one[2]) == CDC_UNION) {
			if (ConfiguredInterface *control = interfaceNumbered(interfaces, uchar(one[3]))) control->unionData = uchar(one[4]);
		} else if (type == DESCRIPTOR_ENDPOINT && length >= 7 && (uchar(one[3]) & ENDPOINT_TYPE_MASK) == ENDPOINT_TYPE_BULK) {
			ConfiguredInterface *owner = interfaceNumbered(interfaces, current);
			if (!owner) return false;
			const int address = uchar(one[2]);
			QVector<int> &list = address & ENDPOINT_IN ? owner->in : owner->out;
			if (!list.contains(address)) list << address;
			if ((address & ENDPOINT_IN) && !owner->inMaxPacket) owner->inMaxPacket = little<quint16>(one + 4) & MAX_PACKET_MASK;
		}
		at += length;
	}
	return true;
}

/* a device of one function does not say which interface a port is on: its CDC control interface, or the first that
 * can carry data each way */
ConfiguredInterface *onlyFunction(QVector<ConfiguredInterface> &interfaces) {
	for (ConfiguredInterface &each : interfaces)
		if (each.klass == CDC_CONTROL_CLASS) return &each;
	for (ConfiguredInterface &each : interfaces)
		if (each.hasData()) return &each;
	return nullptr;
}

/* a control interface's data: the one its Union names, else as the Linux lookup goes, the nearest one up, then down */
ConfiguredInterface *dataOf(QVector<ConfiguredInterface> &interfaces, ConfiguredInterface *own) {
	if (own->klass != CDC_CONTROL_CLASS) return own;
	if (ConfiguredInterface *named = interfaceNumbered(interfaces, own->unionData)) return named;
	ConfiguredInterface *found = nullptr;
	int nearest = INT_MAX;
	for (ConfiguredInterface &each : interfaces) {
		if (each.klass != CDC_DATA_CLASS) continue;
		const int distance = each.number > own->number ? each.number - own->number : 256 + own->number - each.number;
		if (distance < nearest) {
			nearest = distance;
			found = &each;
		}
	}
	return found;
}

/* a hexadecimal field after a word of an instance ID, VID_0483 or PID_5740; 0 when there is none */
quint16 instanceField(const QString &instance, QLatin1String word) {
	const int at = instance.indexOf(word, 0, Qt::CaseInsensitive);
	return at < 0 ? quint16(0) : instance.mid(at + word.size(), 4).toUShort(nullptr, 16);
}

bool enumeratedBy(const QString &instance, QLatin1String bus) {
	return instance.section(QLatin1Char('\\'), 0, 0).compare(bus, Qt::CaseInsensitive) == 0;
}

} // namespace

int usbInterfaceOf(const QString &instance) {
	const QLatin1String word("&MI_");
	const int at = instance.indexOf(word, 0, Qt::CaseInsensitive);
	if (at < 0) return -1;
	bool ok = false;
	const int number = instance.mid(at + word.size(), 2).toInt(&ok, 16);
	return ok ? number : -1;
}

UsbChain usbChainOf(const QStringList &chain) {
	UsbChain found;
	for (int i = 0; i < chain.size(); i++) {
		/* a Bluetooth radio is often a USB device, and the walk would end on it: its ports are not its USB traffic */
		if (enumeratedBy(chain[i], QLatin1String("BTHENUM")) || enumeratedBy(chain[i], QLatin1String("BTH"))) return {};
		if (!enumeratedBy(chain[i], QLatin1String("USB"))) continue;
		found.rootHub = i;
		if (found.device >= 0) continue;
		const int number = usbInterfaceOf(chain[i]);
		if (number >= 0) found.firstInterface = number;
		else found.device = i;
	}
	if (found.device < 0 || found.device == found.rootHub) return {};
	return found;
}

bool usbHubLinkIs(const QString &link, const QString &rootHub) {
	QString name = link;
	for (QLatin1String prefix : { QLatin1String("\\??\\"), QLatin1String("\\\\?\\"), QLatin1String("\\\\.\\") })
		if (name.startsWith(prefix)) name.remove(0, prefix.size());
	const int guid = name.indexOf(QLatin1String("#{"));
	if (guid >= 0) name.truncate(guid);
	name.replace(QLatin1Char('#'), QLatin1Char('\\'));
	return !name.isEmpty() && name.compare(rootHub, Qt::CaseInsensitive) == 0;
}

bool usbReadEndpoints(const QByteArray &configuration, int firstInterface, UsbTarget &target) {
	QVector<ConfiguredInterface> interfaces;
	if (!readConfiguration(configuration, interfaces)) return false;
	ConfiguredInterface *own = firstInterface < 0 ? onlyFunction(interfaces) : interfaceNumbered(interfaces, firstInterface);
	ConfiguredInterface *data = own ? dataOf(interfaces, own) : nullptr;
	if (!data || !data->hasData()) return false;
	target.inEndpoints = data->in;
	target.outEndpoints = data->out;
	target.inMaxPacket = data->inMaxPacket;
	if (own->klass == CDC_CONTROL_CLASS || data->klass == CDC_DATA_CLASS) target.kind = UsbTarget::Cdc;
	else target.kind = target.vid == FTDI_VID ? UsbTarget::Ftdi : UsbTarget::OtherBridge;
	return true;
}

UsbTarget::Kind usbKindByVid(quint16 vid) {
	switch (vid) {
	case FTDI_VID: return UsbTarget::Ftdi;
	case 0x10C4: case 0x1A86: case 0x067B: case 0x04D8: return UsbTarget::OtherBridge; /* no framing on the data */
	default: return UsbTarget::Cdc;
	}
}

UsbPortState usbPcapPortState(const UsbPortFacts &facts) {
	UsbPortState state;
	state.kind = UsbPortState::NotUsb;
	const UsbChain chain = usbChainOf(facts.chain);
	state.missing = facts.chain.isEmpty();
	if (chain.device < 0) return state;

	UsbTarget &target = state.target;
	const QString &device = facts.chain[chain.device];
	target.port = facts.port;
	target.address = facts.address;
	target.vid = instanceField(device, QLatin1String("VID_"));
	target.pid = instanceField(device, QLatin1String("PID_"));
	if (!usbReadEndpoints(facts.configuration, chain.firstInterface, target)) target.kind = usbKindByVid(target.vid);
	/* FTDI's driver makes a bus of its own, for chips under other makers' VIDs too */
	for (const QString &node : facts.chain)
		if (enumeratedBy(node, QLatin1String("FTDIBUS"))) target.kind = UsbTarget::Ftdi;
	/* USBPcap numbers its buses by its root hubs' filters, and the number is what a capture opens */
	for (int i = 0; i < facts.filters.size() && !target.bus; i++)
		if (usbHubLinkIs(facts.filters[i], facts.chain[chain.rootHub])) target.bus = i + 1;

	if (!facts.program) {
		state.kind = UsbPortState::NoUsbPcap;
		return state;
	}
	const bool onDevice = facts.stack.isEmpty() || facts.stack.contains(QLatin1String("\\Driver\\USBPcap"), Qt::CaseInsensitive);
	state.kind = !target.bus || !target.address || !onDevice ? UsbPortState::NoFilter : UsbPortState::Listenable;
	target.asksPermission = !facts.elevated; /* USBPcap's driver opens only to an administrator: the program asks for one */
	return state;
}

bool usbPcapStillThere(const UsbPortFacts &facts, const UsbTarget &running) {
	if (usbChainOf(facts.chain).device < 0) return false;
	const UsbTarget seen = usbPcapPortState(facts).target;
	return (!seen.address || seen.address == running.address) && (!seen.bus || seen.bus == running.bus);
}

QStringList usbPcapArguments(const UsbTarget &target) {
	return { QStringLiteral("-d"), QStringLiteral("\\\\.\\USBPcap%1").arg(target.bus),
		QStringLiteral("--devices"), QString::number(target.address),
		QStringLiteral("-s"), QString::number(USBPCAP_SNAP_LENGTH),
		QStringLiteral("-b"), QString::number(USBPCAP_BUFFER),
		QStringLiteral("-o"), QStringLiteral("-") };
}

/* USBPcapCMD says what went wrong on its standard error and still ends with code 0, so what it said comes first. A prompt
 * refused is no worker made. The driver lets one reader in: error 5, elevated, is another capture */
QString usbPcapEndReason(int exitCode, bool crashed, const QByteArray &errors, bool workerSilent) {
	const QString said = QString::fromLocal8Bit(errors).trimmed();
	if (said.contains(QLatin1String("Failed to create worker process")))
		return QCoreApplication::translate("UsbCapture", "permission refused: press Listen to ask again");
	if (said.contains(QLatin1String("Couldn't open device - 5")))
		return QCoreApplication::translate("UsbCapture", "USBPcap refused this USB bus: another capture holds it");
	if (!said.isEmpty())
		return QCoreApplication::translate("UsbCapture", "USBPcapCMD stopped: %1").arg(said.section(QLatin1Char('\n'), 0, 0).trimmed());
	if (workerSilent)
		return QCoreApplication::translate("UsbCapture", "the capture ended before anything came: another capture may hold this USB bus");
	if (crashed) return QCoreApplication::translate("UsbCapture", "USBPcapCMD crashed");
	if (exitCode) return QCoreApplication::translate("UsbCapture", "USBPcapCMD stopped with code %1").arg(exitCode);
	return QCoreApplication::translate("UsbCapture", "USBPcapCMD stopped");
}

QString usbPcapProgramBeside(const QString &uninstallString) {
	QString path = uninstallString.trimmed();
	if (path.startsWith(QLatin1Char('"'))) path = path.section(QLatin1Char('"'), 1, 1);
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int folder = path.lastIndexOf(QLatin1Char('/'));
	return folder < 0 ? QString() : path.left(folder) + QLatin1String("/USBPcapCMD.exe");
}

/* the tests' ports (stateForTests) */
static QHash<QString, UsbPortState> &portStatesForTests() {
	static QHash<QString, UsbPortState> states;
	return states;
}

/* a count for the tests, on both threads a lookup runs on */
static std::atomic<quint64> descriptorLookups{ 0 };

quint64 UsbTarget::descriptorLookupsForTests() { return descriptorLookups; }

UsbPortState UsbTarget::stateOf(const QString &port, bool descriptors) {
	const auto forced = portStatesForTests().constFind(port);
	if (forced == portStatesForTests().constEnd()) return liveStateOf(port, descriptors);
	if (descriptors) descriptorLookups++;
	return *forced;
}

/* Where each system's answer goes: the port's USB device and endpoints (sysfs, or SetupAPI and the hub's descriptors),
 * or why not (not a USB port, the capture not installed or not readable) */
UsbPortState UsbTarget::liveStateOf(const QString &port, bool descriptors) {
	if (descriptors) descriptorLookups++;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
	/* a name with a path may be a link of /dev/serial/by-id */
	const QString name = port.contains(QLatin1Char('/')) ? QFileInfo(port).canonicalFilePath() : port;
	return usbmonPortState(QStringLiteral("/sys"), QStringLiteral("/dev"), name);
#elif defined(Q_OS_WIN)
	return usbPcapPortState(usbPcapFacts(port, descriptors));
#else
	Q_UNUSED(port);
	return {};
#endif
}

bool UsbTarget::stillThere(const UsbTarget &running) {
#ifdef Q_OS_WIN
	return usbPcapStillThere(usbPcapFacts(running.port, false), running);
#else
	return liveStateOf(running.port, false).holds(running);
#endif
}

UsbCapture *UsbCapture::live() {
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
	return new UsbmonCapture;
#elif defined(Q_OS_WIN)
	return new UsbPcapCapture(usbPcapProgram());
#else
	return nullptr;
#endif
}

void UsbTarget::stateForTests(const QString &port, const UsbPortState &state) {
	UsbPortState forced = state;
	forced.forced = true;
	portStatesForTests().insert(port, forced);
}

void UsbDecoder::feed(const QByteArray &bytes, QVector<UsbEvent> &out) {
	if (!error_.isEmpty()) return;
	buffer_.append(bytes);
	qsizetype used = 0;
	while (const qsizetype size = record(buffer_.constData() + used, buffer_.size() - used, out)) used += size;
	if (error_.isEmpty()) buffer_.remove(0, used);
	else buffer_.clear();
}

void UsbDecoder::hear(UsbDirection direction, int device, int endpoint, int status, const char *bytes, qsizetype size,
		qint64 stamp, QVector<UsbEvent> &out) const {
	if (device != target_.address || size <= 0) return;
	const QVector<int> &endpoints = direction == UsbDirection::In ? target_.inEndpoints : target_.outEndpoints;
	if (!endpoints.isEmpty() && !endpoints.contains(endpoint)) return;
	const bool ftdi = direction == UsbDirection::In && target_.kind == UsbTarget::Ftdi;
	const QByteArray data = ftdi ? withoutFtdiStatus(bytes, size) : QByteArray(bytes, size);
	if (data.isEmpty()) return; /* FTDI's status poll: the chip answers every read, with nothing to say */
	UsbEvent event;
	event.direction = direction;
	event.endpoint = endpoint;
	event.transfer = BULK;
	event.status = status;
	event.data = data;
	event.stamp = stamp;
	out.append(event);
}

/* An FTDI chip starts every packet of an IN transfer with 2 status bytes, and only the capture of the whole transfer
 * shows where the packets were: at each multiple of wMaxPacketSize. */
QByteArray UsbDecoder::withoutFtdiStatus(const char *bytes, qsizetype size) const {
	const qsizetype packet = target_.inMaxPacket > 2 ? target_.inMaxPacket : FTDI_PACKET;
	QByteArray data;
	data.reserve(size);
	for (qsizetype at = 0; at < size; at += packet)
		if (size - at > 2) data.append(bytes + at + 2, qMin(packet, size - at) - 2);
	return data;
}

qsizetype UsbPcapDecoder::record(const char *bytes, qsizetype size, QVector<UsbEvent> &out) {
	if (!headerSeen_) {
		if (size < PCAP_FILE_HEADER) return 0;
		if (little<quint32>(bytes) != PCAP_MAGIC || little<quint32>(bytes + 20) != PCAP_LINK_USBPCAP) {
			fail(QCoreApplication::translate("UsbCapture", "not a USBPcap capture"));
			return 0;
		}
		headerSeen_ = true;
		return PCAP_FILE_HEADER;
	}
	if (size < PCAP_RECORD_HEADER) return 0;
	const quint32 included = little<quint32>(bytes + 8);
	if (included > quint32(MAX_RECORD)) {
		fail(brokenStream());
		return 0;
	}
	const qsizetype whole = PCAP_RECORD_HEADER + qsizetype(included);
	if (size < whole) return 0;
	const qint64 stamp = stampOf(little<quint32>(bytes), little<quint32>(bytes + 4));
	packet(bytes + PCAP_RECORD_HEADER, included, stamp, out);
	return whole;
}

void UsbPcapDecoder::packet(const char *bytes, qsizetype size, qint64 stamp, QVector<UsbEvent> &out) const {
	if (size < PACKET_HEADER) return;
	const int headerLen = little<quint16>(bytes);
	if (headerLen < PACKET_HEADER || headerLen > size) return;
	if (uchar(bytes[22]) != BULK) return; /* control transfers and interrupt notifications are not the data */
	const int status = little<qint32>(bytes + 10);
	const bool completion = uchar(bytes[16]) & INFO_COMPLETION;
	const int device = little<quint16>(bytes + 19);
	const int endpoint = uchar(bytes[21]);
	/* snap length: the record may hold less than the transfer had */
	const qsizetype length = qMin(qsizetype(little<quint32>(bytes + 23)), size - headerLen);
	if (endpoint & ENDPOINT_IN) {
		if (completion && status == 0) hear(UsbDirection::In, device, endpoint, status, bytes + headerLen, length, stamp, out);
	} else if (!completion) {
		hear(UsbDirection::Out, device, endpoint, status, bytes + headerLen, length, stamp, out);
	}
}

qsizetype UsbmonDecoder::fitRecord(char *bytes, quint32 room) {
	const quint32 captured = std::min(little<quint32>(bytes + MON_LEN_CAP), room);
	qToLittleEndian(captured, bytes + MON_LEN_CAP);
	return HEADER + qsizetype(captured);
}

qsizetype UsbmonDecoder::record(const char *bytes, qsizetype size, QVector<UsbEvent> &out) {
	if (size < HEADER) return 0;
	const quint32 captured = little<quint32>(bytes + MON_LEN_CAP);
	if (captured > quint32(MAX_RECORD)) {
		fail(brokenStream());
		return 0;
	}
	const qsizetype whole = HEADER + qsizetype(captured);
	if (size < whole) return 0;
	const char type = bytes[8];
	const bool hasData = bytes[15] == 0; /* flag_data: 0 is data, '<' or '>' says why there is none */
	if (uchar(bytes[9]) == BULK && hasData) {
		const int endpoint = uchar(bytes[10]);
		const int device = uchar(bytes[11]);
		const int status = little<qint32>(bytes + 28);
		const qint64 stamp = stampOf(little<qint64>(bytes + 16), little<qint32>(bytes + 24));
		if (endpoint & ENDPOINT_IN) {
			if (type == 'C' && status == 0) hear(UsbDirection::In, device, endpoint, status, bytes + HEADER, captured, stamp, out);
		} else if (type == 'S') {
			hear(UsbDirection::Out, device, endpoint, status, bytes + HEADER, captured, stamp, out);
		}
	}
	return whole;
}

FileCapture::FileCapture(const QString &file, Pace pace, QObject *parent)
	: UsbCapture(parent), file_(file), pace_(pace), timer_(new QTimer(this)) {
	timer_->setSingleShot(true);
	timer_->setTimerType(Qt::PreciseTimer);
	connect(timer_, &QTimer::timeout, this, &FileCapture::next);
}

void FileCapture::start(const UsbTarget &target) {
	stop();
	events_.clear();
	next_ = 0;
	why_ = tr("the capture ended");
	std::unique_ptr<UsbDecoder> decoder;
	const QString suffix = QFileInfo(file_).suffix().toLower();
	if (suffix == QLatin1String("pcap")) decoder = std::make_unique<UsbPcapDecoder>(target);
	else if (suffix == QLatin1String("usbmon")) decoder = std::make_unique<UsbmonDecoder>(target);
	QFile file(file_);
	if (!decoder) why_ = tr("%1 is not a .pcap or .usbmon capture").arg(file_);
	else if (!file.open(QIODevice::ReadOnly)) why_ = tr("%1 cannot be read: %2").arg(file_, file.errorString());
	else {
		decoder->feed(file.readAll(), events_);
		if (!decoder->error().isEmpty()) why_ = decoder->error();
	}
	running_ = true;
	clock_.start();
	timer_->start(0);
}

void FileCapture::stop() {
	running_ = false;
	timer_->stop();
}

void FileCapture::next() {
	while (running_ && next_ < events_.size()) {
		if (pace_ == ByStamps) {
			const qint64 due = events_[next_].stamp - events_.first().stamp;
			const qint64 wait = due - clock_.nsecsElapsed() / 1000;
			if (wait > 0) {
				timer_->start(int((wait + 999) / 1000));
				return;
			}
		}
		const UsbEvent event = events_[next_++]; /* a copy: a listener may stop or start this capture from its slot */
		emit data(event.direction, event.data);
	}
	if (!running_) return;
	running_ = false;
	emit ended(why_);
}

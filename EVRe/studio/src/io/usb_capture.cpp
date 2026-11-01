/* SPDX-License-Identifier: Apache-2.0 */
#include "io/usb_capture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <climits>
#include <memory>

#ifdef Q_OS_LINUX
#include "io/usbmon_capture.h"
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

constexpr int CDC_DATA_CLASS = 0x0A;
constexpr quint16 FTDI_VID = 0x0403;

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
	const QString link = sys + QLatin1String("/class/tty/") + tty + QLatin1String("/device");
	/* no such tty, or a tty with no device under it: a virtual one, a Bluetooth one */
	const QString start = links.contains(link) ? links.value(link) : QFileInfo(link).canonicalFilePath();
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
	target.kind = cdc ? UsbTarget::Cdc : target.vid == FTDI_VID ? UsbTarget::Ftdi : UsbTarget::OtherBridge;
	readBulkEndpoints(cdc ? cdcDataInterface(device, own) : own, target);

	const QFileInfo capture(dev + QLatin1String("/usbmon") + QString::number(target.bus));
	const bool readable = capture.exists() && capture.isReadable();
	state.kind = readable ? UsbPortState::Listenable : UsbPortState::NoUsbmon;
	if (!readable) state.device = capture.filePath();
	return state;
}

/* the tests' ports (stateForTests) */
static QHash<QString, UsbPortState> &portStatesForTests() {
	static QHash<QString, UsbPortState> states;
	return states;
}

UsbPortState UsbTarget::stateOf(const QString &port) {
	const auto forced = portStatesForTests().constFind(port);
	return forced != portStatesForTests().constEnd() ? *forced : liveStateOf(port);
}

/* Where each system's answer goes: the port's USB device and endpoints (sysfs, or SetupAPI and the hub's descriptors),
 * or why not (not a USB port, the capture not installed or not readable). Windows has none yet */
UsbPortState UsbTarget::liveStateOf(const QString &port) {
#ifdef Q_OS_LINUX
	/* a name with a path may be a link of /dev/serial/by-id */
	const QString name = port.contains(QLatin1Char('/')) ? QFileInfo(port).canonicalFilePath() : port;
	return usbmonPortState(QStringLiteral("/sys"), QStringLiteral("/dev"), name);
#else
	Q_UNUSED(port);
	return {};
#endif
}

UsbCapture *UsbCapture::live() {
#ifdef Q_OS_LINUX
	return new UsbmonCapture;
#else
	return nullptr;
#endif
}

void UsbTarget::stateForTests(const QString &port, const UsbPortState &state) { portStatesForTests().insert(port, state); }

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

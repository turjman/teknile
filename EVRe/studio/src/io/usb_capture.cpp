/* SPDX-License-Identifier: Apache-2.0 */
#include "io/usb_capture.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTimer>
#include <QtEndian>

#include <memory>

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
 * ts_usec 24, status 28, len_urb 32, len_cap 36, then setup and the like up to 64 */
constexpr int MON_HEADER = 64;

constexpr int ENDPOINT_IN = 0x80;

template <typename T>
T little(const char *bytes) { return qFromLittleEndian<T>(bytes); }

qint64 stampOf(qint64 seconds, qint64 microseconds) {
	return qint64(quint64(seconds) * 1000000U + quint64(microseconds)); /* unsigned: a wrong stamp in a bad record cannot overflow */
}

QString brokenStream() {
	return QCoreApplication::translate("UsbCapture", "the capture's stream is broken: a record longer than any can be");
}

} // namespace

/* the tests' ports (stateForTests) */
static QHash<QString, UsbPortState> &portStatesForTests() {
	static QHash<QString, UsbPortState> states;
	return states;
}

/* Where each system's answer goes: the port's USB device and endpoints (sysfs, or SetupAPI and the hub's descriptors),
 * or why not (not a USB port, the capture not installed or not readable). No system has one yet */
UsbPortState UsbTarget::stateOf(const QString &port) {
	return portStatesForTests().value(port);
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

qsizetype UsbmonDecoder::record(const char *bytes, qsizetype size, QVector<UsbEvent> &out) {
	if (size < MON_HEADER) return 0;
	const quint32 captured = little<quint32>(bytes + 36);
	if (captured > quint32(MAX_RECORD)) {
		fail(brokenStream());
		return 0;
	}
	const qsizetype whole = MON_HEADER + qsizetype(captured);
	if (size < whole) return 0;
	const char type = bytes[8];
	const bool hasData = bytes[15] == 0; /* flag_data: 0 is data, '<' or '>' says why there is none */
	if (uchar(bytes[9]) == BULK && hasData) {
		const int endpoint = uchar(bytes[10]);
		const int device = uchar(bytes[11]);
		const int status = little<qint32>(bytes + 28);
		const qint64 stamp = stampOf(little<qint64>(bytes + 16), little<qint32>(bytes + 24));
		if (endpoint & ENDPOINT_IN) {
			if (type == 'C' && status == 0) hear(UsbDirection::In, device, endpoint, status, bytes + MON_HEADER, captured, stamp, out);
		} else if (type == 'S') {
			hear(UsbDirection::Out, device, endpoint, status, bytes + MON_HEADER, captured, stamp, out);
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

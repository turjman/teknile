/* SPDX-License-Identifier: Apache-2.0 */
#include "io/usbmon_capture.h"

#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace {

/* usbmon's binary interface. No public header has it: these are drivers/usb/mon/mon_bin.c's own definitions, written
 * out here as libpcap's pcap-usb-linux.c does (MON_IOC_MAGIC 0x92). read() would give the 48-byte header of the first
 * version of the interface; MON_IOCX_GETX gives the 64 bytes UsbmonDecoder takes. */
struct MonBinStats {
	quint32 queued;
	quint32 dropped;
};

struct MonBinGet {
	void *hdr;
	void *data;
	size_t alloc;
};

constexpr unsigned long MON_IOCG_STATS = _IOR(0x92, 3, MonBinStats);
constexpr unsigned long MON_IOCT_RING_SIZE = _IO(0x92, 4);
constexpr unsigned long MON_IOCX_GETX = _IOW(0x92, 10, MonBinGet);

/* The ring in the kernel, per reader: it gives the Studio time when the engine thread is busy. The limit grew with the
 * buses (64 MiB now, EINVAL above it), older kernels stop at 1200 KiB, and the default is 300 KiB. */
constexpr unsigned long RING_WANTED = 8 * 1024 * 1024;
constexpr unsigned long RING_OLD_LIMIT = 1200 * 1024;

constexpr int DATA_SIZE = 64 * 1024;    /* a longer transfer is cut here: the parser counts what is left of it as bytes not EVRe */
constexpr int RECORDS_AT_A_TURN = 1024; /* the ring says "more" again at once: the rest of the engine goes between */
constexpr int LOOK_MS = 500;

} // namespace

UsbmonCapture::UsbmonCapture(QObject *parent) : UsbCapture(parent), look_(new QTimer(this)) {
	connect(look_, &QTimer::timeout, this, &UsbmonCapture::look);
}

UsbmonCapture::~UsbmonCapture() { stop(); }

void UsbmonCapture::start(const UsbTarget &target) {
	stop();
	target_ = target;
	decoder_ = std::make_unique<UsbmonDecoder>(target);
	record_.resize(UsbmonDecoder::HEADER + DATA_SIZE);
	dropped_ = 0;
	why_.clear();
	const QString device = QStringLiteral("/dev/usbmon%1").arg(target.bus);
	file_ = ::open(qPrintable(device), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (file_ < 0) {
		why_ = tr("%1 cannot be read: %2").arg(device, qt_error_string(errno));
		look_->start(0);
		return;
	}
	if (::ioctl(file_, MON_IOCT_RING_SIZE, RING_WANTED) < 0) (void) ::ioctl(file_, MON_IOCT_RING_SIZE, RING_OLD_LIMIT);
	notifier_ = new QSocketNotifier(file_, QSocketNotifier::Read, this);
	connect(notifier_, &QSocketNotifier::activated, this, &UsbmonCapture::readRecords);
	look_->start(LOOK_MS);
}

void UsbmonCapture::stop() {
	look_->stop();
	if (notifier_) {
		notifier_->setEnabled(false); /* before its file goes; and this may be its own signal, so not deleted here */
		notifier_->deleteLater();
		notifier_ = nullptr;
	}
	if (file_ >= 0) ::close(file_);
	file_ = -1;
}

void UsbmonCapture::readRecords() {
	QVector<UsbEvent> events;
	QString why;
	for (int i = 0; i < RECORDS_AT_A_TURN; i++) {
		MonBinGet get{ record_.data(), record_.data() + UsbmonDecoder::HEADER, DATA_SIZE };
		if (::ioctl(file_, MON_IOCX_GETX, &get) < 0) {
			if (errno == EINTR) continue;
			if (errno != EAGAIN) why = tr("the capture stopped: %1").arg(qt_error_string(errno));
			break;
		}
		const qsizetype whole = UsbmonDecoder::fitRecord(record_.data(), DATA_SIZE);
		decoder_->feed(QByteArray::fromRawData(record_.constData(), whole), events);
	}
	if (why.isEmpty()) why = decoder_->error();
	for (const UsbEvent &event : std::as_const(events)) {
		emit data(event.direction, event.data);
		if (file_ < 0) return; /* a listener stopped this capture from its slot */
	}
	if (!why.isEmpty()) finish(why);
}

void UsbmonCapture::look() {
	if (!why_.isEmpty()) {
		finish(why_);
		return;
	}
	MonBinStats stats{};
	/* the kernel hands over what it lost since the last ask and counts from 0 again: the total is ours */
	if (::ioctl(file_, MON_IOCG_STATS, &stats) == 0 && stats.dropped) {
		dropped_ += stats.dropped;
		emit dropped(dropped_);
		if (file_ < 0) return;
	}
	/* usbmon hears a bus, which outlives the device: it would just go quiet. Plugged in again the device may have
	 * another address, which only a new start hears */
	if (!UsbTarget::liveStateOf(target_.port).holds(target_)) finish(tr("%1 is gone: unplugged or reset?").arg(target_.port));
}

void UsbmonCapture::finish(const QString &why) {
	stop();
	emit ended(why);
}

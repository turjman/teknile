/* SPDX-License-Identifier: Apache-2.0 */
/* Listen's live capture on Linux: the kernel's usbmon, read through /dev/usbmonN and its binary interface. The device
 * holds one bus: the decoder keeps the target's device and endpoints, as it does on a replayed file.
 *
 * Built on Linux only (CMakeLists.txt): the device and its ioctls exist nowhere else. What can be tested without one
 * is in usb_capture.h: the lookup, the decoder and UsbmonDecoder::fitRecord.
 */
#pragma once

#include <QByteArray>

#include <memory>

#include "io/usb_capture.h"

class QSocketNotifier;
class QTimer;

class UsbmonCapture : public UsbCapture {
	Q_OBJECT
public:
	explicit UsbmonCapture(QObject *parent = nullptr);
	~UsbmonCapture() override;
	void start(const UsbTarget &target) override;
	void stop() override;

private:
	void readRecords();                 /* what the ring holds, some of it at a turn of the event loop */
	void look();                        /* every LOOK_MS: what was lost, and is the device still there */
	void finish(const QString &why);

	UsbTarget target_;
	std::unique_ptr<UsbmonDecoder> decoder_;
	QByteArray record_;                 /* MON_IOCX_GETX's buffer: the header, the data after it */
	QSocketNotifier *notifier_ = nullptr;
	QTimer *look_;
	int file_ = -1;
	quint64 dropped_ = 0;
	QString why_;                       /* start() failed: what ended() says, from the event loop */
};

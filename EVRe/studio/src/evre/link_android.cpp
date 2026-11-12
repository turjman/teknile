/* SPDX-License-Identifier: Apache-2.0 */
/* The USB CDC link on Android: the bridge's words, queued to the link's thread. */
#include "evre/link_android.h"

namespace evre {

AndroidUsbLink::AndroidUsbLink(const QString &device, qint32 baud, UsbBridge *bridge, QObject *parent)
	: Link(parent), device_(device), baud_(baud), bridge_(bridge) {}

/* the bridge says nothing more once close() returned: nothing comes for a link that is gone */
AndroidUsbLink::~AndroidUsbLink() {
	if ((opening_ || open_) && bridge_) bridge_->close();
}

void AndroidUsbLink::open() {
	if (!bridge_) {
		emit closed(tr("USB is not available here"));
		return;
	}
	opening_ = true;
	open_ = false;
	bridge_->open(device_, baud_, this);
}

void AndroidUsbLink::close() {
	if ((opening_ || open_) && bridge_) bridge_->close();
	opening_ = false;
	open_ = false;
	emit closed(QString());
}

bool AndroidUsbLink::isOpen() const { return open_; }

/* dropped while not open, as the serial link drops it */
void AndroidUsbLink::send(const QByteArray &bytes) {
	if (open_) bridge_->send(bytes);
}

QString AndroidUsbLink::describe() const { return QStringLiteral("%1 @ %2").arg(device_).arg(baud_); }

QString AndroidUsbLink::refusedText(const QString &device) { return tr("permission refused for %1").arg(device); }

/* the serial link's words for the same news: a board unplugged or reset reads the same on every system */
QString AndroidUsbLink::goneText(const QString &device) {
	return SerialLink::plainReason(QSerialPort::ResourceError, device, true);
}

/* --------------------------------------------- the bridge's words (any thread) */

void AndroidUsbLink::usbAsking() {
	QMetaObject::invokeMethod(this, [this] {
		if (opening_) emit waiting();
	}, Qt::QueuedConnection);
}

void AndroidUsbLink::usbOpened() {
	QMetaObject::invokeMethod(this, [this] {
		if (!opening_) return; /* closed meanwhile */
		opening_ = false;
		open_ = true;
		emit opened();
	}, Qt::QueuedConnection);
}

void AndroidUsbLink::usbRefused() {
	QMetaObject::invokeMethod(this, [this] {
		if (opening_) ended(refusedText(device_));
	}, Qt::QueuedConnection);
}

void AndroidUsbLink::usbFailed(const QString &why) {
	QMetaObject::invokeMethod(this, [this, why] {
		if (opening_ || open_) ended(tr("%1 could not be opened").arg(device_), why);
	}, Qt::QueuedConnection);
}

void AndroidUsbLink::usbBytes(const QByteArray &bytes) {
	QMetaObject::invokeMethod(this, [this, bytes] {
		if (open_) emit received(bytes);
	}, Qt::QueuedConnection);
}

void AndroidUsbLink::usbGone() {
	QMetaObject::invokeMethod(this, [this] {
		if (opening_ || open_) ended(goneText(device_));
	}, Qt::QueuedConnection);
}

void AndroidUsbLink::ended(const QString &why, const QString &detail) {
	bridge_->close();
	opening_ = false;
	open_ = false;
	emit closed(why, detail);
}

} // namespace evre

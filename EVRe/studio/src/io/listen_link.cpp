/* SPDX-License-Identifier: Apache-2.0 */
/* Listen's link (see listen_link.h). */
#include "io/listen_link.h"

#include <QtGlobal>

namespace evre {

ListenLink::ListenLink(const QString &port, UsbCapture *capture, const UsbTarget &target, QObject *parent)
	: Link(parent), port_(port), capture_(capture), target_(target) {
	if (!capture_) return;
	capture_->setParent(this);
	connect(capture_, &UsbCapture::data, this, [this](UsbDirection direction, const QByteArray &bytes) {
		if (direction == UsbDirection::In) emit received(bytes);
		else emit heardRequest(bytes);
	});
	connect(capture_, &UsbCapture::started, this, [this] {
		if (target_.asksPermission) emit opened();
	});
	connect(capture_, &UsbCapture::dropped, this, &ListenLink::capturesLost);
	connect(capture_, &UsbCapture::ended, this, [this](const QString &why) {
		open_ = false;
		emit closed(why);
	});
}

/* opened() comes first: a capture hears only from the event loop, so the first bytes find the engine listening. One that
 * asks a person first has opened when its first bytes say the answer was yes; it may take as long as the person does, so
 * nothing here runs a clock on it. Meanwhile isOpen() holds, for a Stop to reach the capture */
void ListenLink::open() {
	if (!capture_) {
		emit closed(refusal());
		return;
	}
	open_ = true;
	capture_->start(target_);
	if (!target_.asksPermission) emit opened();
}

/* The window's words (Sidebar::listenRefusal): it refuses a fresh Listen itself, and a try by itself comes as far as here */
QString ListenLink::refusal() const {
	switch (refused_.kind) {
	case UsbPortState::NotUsb: return tr("%1 is not a USB port: it cannot be listened to").arg(port_);
	case UsbPortState::NoUsbPcap: return tr("USBPcap is not installed");
	case UsbPortState::NoUsbmon: return tr("usbmon is not loaded or %1 is not readable").arg(refused_.device);
	case UsbPortState::NoFilter: return tr("USBPcap is not on this device yet");
	case UsbPortState::Listenable:
		if (refused_.target.asksPermission) return tr("press Listen: Windows asks for permission first");
		break;
	case UsbPortState::NoCapture: break;
	}
	return tr("%1 cannot be listened to: no USB capture on this system").arg(port_);
}

void ListenLink::close() {
	if (capture_) capture_->stop();
	open_ = false;
	emit closed(QString());
}

/* nothing reaches the wire from here; said once, for whoever looks into why a request got this far */
void ListenLink::send(const QByteArray &bytes) {
	(void) bytes;
	if (sendAttempts_++ == 0) qWarning("listening on %s: a request reached the link and was dropped", qUtf8Printable(port_));
}

QString ListenLink::describe() const { return tr("%1 (listening)").arg(port_); }

QString ListenLink::place() const { return tr("%1 (bus %2, device %3)").arg(port_).arg(target_.bus).arg(target_.address); }

} // namespace evre

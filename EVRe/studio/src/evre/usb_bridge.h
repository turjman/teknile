/* SPDX-License-Identifier: Apache-2.0 */
/* What AndroidUsbLink needs of Android's USB host API: the CDC devices plugged in, and one of them opened, written,
 * read and closed. On Android it is the Java layer (android/src/com/teknile/evre/EvreUsb.java) over JNI
 * (usb_bridge_jni.cpp); the GUI test drives the link with a fake one on any system.
 *
 * An app may not open /dev/tty* on Android, and Qt Serial Port has no USB host backend: this is the way to a board.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace evre {

class UsbBridge {
public:
	/* a USB device plugged in: name is Android's ("/dev/bus/usb/001/002"), the key open() takes */
	struct Device {
		QString name;
		quint16 vid = 0;
		quint16 pid = 0;
		QString product; /* the device's product string; empty when it has none or it cannot be read yet */
	};

	/* What the bridge says of the device being opened. Called on any thread (Java's reader thread, Android's main
	 * thread): the sink passes each on to its own thread. None is called after close() returned. */
	class Sink {
	public:
		virtual ~Sink() = default;
		virtual void usbAsking() = 0;                     /* Android's permission dialog is shown */
		virtual void usbOpened() = 0;
		virtual void usbRefused() = 0;                    /* the person said no */
		virtual void usbFailed(const QString &why) = 0;   /* not there, not a CDC device, claimed by another app */
		virtual void usbBytes(const QByteArray &bytes) = 0;
		virtual void usbGone() = 0;                       /* unplugged (or reset) while open */
	};

	virtual ~UsbBridge() = default;
	virtual QVector<Device> devices() = 0;
	/* asks the permission when it is not granted yet, then opens: the sink hears how it went. One device at a time:
	 * a second open() closes the first */
	virtual void open(const QString &name, qint32 baud, Sink *sink) = 0;
	virtual void send(const QByteArray &bytes) = 0;
	virtual void close() = 0;

#ifdef Q_OS_ANDROID
	/* the Java layer's bridge, made at its first use */
	static UsbBridge *system();
#endif
};

} // namespace evre

/* SPDX-License-Identifier: Apache-2.0 */
/* A USB CDC device on Android, through the Java layer (usb_bridge.h): what SerialLink is on a desktop. Built on every
 * system, so the GUI test drives it with a fake bridge; only Android's IoEngine::connectSerial makes one.
 *
 * The bridge speaks from Java's threads: each word is queued to the thread the link lives in (the engine's), and a
 * link deleted before it arrives never hears it.
 */
#pragma once

#include "evre/link.h"
#include "evre/usb_bridge.h"

namespace evre {

class AndroidUsbLink : public Link, private UsbBridge::Sink {
	Q_OBJECT
public:
	/* device: the bridge's name of it; baud: sent as the line coding, which a native CDC device ignores */
	AndroidUsbLink(const QString &device, qint32 baud, UsbBridge *bridge, QObject *parent = nullptr);
	~AndroidUsbLink() override;
	void open() override;
	void close() override;
	bool isOpen() const override;
	void send(const QByteArray &bytes) override;
	QString describe() const override;

	static QString refusedText(const QString &device);
	static QString goneText(const QString &device);

signals:
	/* Android's permission dialog is up: the sidebar says so until the person answers */
	void waiting();

private:
	void usbAsking() override;
	void usbOpened() override;
	void usbRefused() override;
	void usbFailed(const QString &why) override;
	void usbBytes(const QByteArray &bytes) override;
	void usbGone() override;
	void ended(const QString &why, const QString &detail = QString()); /* on the link's thread */

	QString device_;
	qint32 baud_;
	UsbBridge *bridge_;
	bool opening_ = false; /* open() asked, no answer yet */
	bool open_ = false;
};

} // namespace evre

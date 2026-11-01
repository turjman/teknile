/* SPDX-License-Identifier: Apache-2.0 */
/* Listen's link: what another program says to a device on a port that program holds, heard from the system's USB
 * capture. It is a Link so that everything above it stays as it is: the device's bytes come out as received(), so the
 * master's parser, the frames nobody asked for, AUTO_SEND and the fast blocks run unchanged, and the other program's
 * bytes as heardRequest(), for the engine's own parser.
 *
 * It sends nothing, ever: send() counts and drops. The engine's guards see that nothing gets this far (IoEngine,
 * "Nothing is sent"); send() is the last belt, and sendAttempts() the proof the tests read.
 *
 * In src/io, not src/evre: the protocol module stays free of USB.
 */
#pragma once

#include "evre/link.h"
#include "io/usb_capture.h"

namespace evre {

/* A file to replay for a port, in place of the live capture (the tests). */
struct ListenFile {
	QString file;
	UsbTarget target;
	FileCapture::Pace pace = FileCapture::AtOnce;
};

class ListenLink : public Link {
	Q_OBJECT
public:
	/* capture: what hears this port, taken as a child; nullptr: nothing here can, and open() says so. target: what it
	 * listens to, and where the Monitor says it is */
	ListenLink(const QString &port, UsbCapture *capture, const UsbTarget &target, QObject *parent = nullptr);
	/* why this port has no capture, for open() to say */
	void setRefused(const UsbPortState &state) { refused_ = state; }
	void open() override;
	void close() override;
	bool isOpen() const override { return open_; }
	void send(const QByteArray &bytes) override;
	QString describe() const override;
	QString place() const;                          /* the port and its USB device, for the Monitor's first line */
	int sendAttempts() const { return sendAttempts_; }

signals:
	void heardRequest(const QByteArray &bytes);     /* the other program's bytes, as they came */
	void capturesLost(quint64 total);               /* captures the system says it lost, so far */

private:
	QString refusal() const;

	QString port_;
	UsbCapture *capture_;
	UsbTarget target_;
	UsbPortState refused_;
	bool open_ = false;
	int sendAttempts_ = 0;
};

} // namespace evre

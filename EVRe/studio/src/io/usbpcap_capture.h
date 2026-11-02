/* SPDX-License-Identifier: Apache-2.0 */
/* Listen's live capture on Windows: USBPcap's own program, USBPcapCMD.exe, started for one device of one root hub, its
 * standard output a pcap stream for UsbPcapDecoder. It is only started, never linked, so USBPcap's licences do not touch
 * the Studio's.
 *
 * It must run elevated: below that USBPcapCMD raises a UAC prompt of its own, so UsbPortState::NeedsAdmin keeps it from
 * being started at all. Nothing here needs Windows: the tests run the whole of it, on every system, on a stand-in for the
 * program, and so never on the real one.
 */
#pragma once

#include <QByteArray>
#include <QProcess>

#include <functional>
#include <memory>

#include "io/usb_capture.h"

class QTimer;

class UsbPcapCapture : public UsbCapture {
	Q_OBJECT
public:
	explicit UsbPcapCapture(const QString &program, QObject *parent = nullptr);
	~UsbPcapCapture() override;
	void start(const UsbTarget &target) override;
	void stop() override;              /* waits for the process: STOP_MS, at most twice */

	/* tests: what says the device is still there, for a target no system has */
	void setStillThereForTests(std::function<bool(const UsbTarget &)> stillThere) { stillThere_ = std::move(stillThere); }

private:
	void readOutput();
	void processEnded(int exitCode, QProcess::ExitStatus status);
	void look();                        /* every LOOK_MS: is the device still there */
	void finish(const QString &why);
	QString goneText() const;

	QString program_;
	std::function<bool(const UsbTarget &)> stillThere_;
	UsbTarget target_;
	std::unique_ptr<UsbPcapDecoder> decoder_;
	QProcess *process_ = nullptr;
	QTimer *look_;
	QString why_;                       /* it could not start: what ended() says, from the event loop */
};

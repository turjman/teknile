/* SPDX-License-Identifier: Apache-2.0 */
/* Listen's live capture on Windows: USBPcap's own program, USBPcapCMD.exe, started for one device of one root hub, its
 * standard output a pcap stream for UsbPcapDecoder. It is only started, never linked, so USBPcap's licences do not touch
 * the Studio's.
 *
 * Below administrator the program raises a UAC prompt of its own and starts an elevated worker, which the Studio cannot
 * kill, and which does not notice the program going until it next has something to write: on a silent device, never.
 * So the program runs in a job of the Studio's, which the worker is born into. Ending the job ends the worker, and the
 * system ends the job when the Studio goes, however it goes.
 *
 * The capture itself needs no Windows: the tests run it, on every system, on a stand-in for the program, and so never on
 * the real one, whose prompt a test must not raise. The job is Windows's, and tried there.
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
	void stop() override;

	/* tests: what says the device is still there, for a target no system has */
	void setStillThereForTests(std::function<bool(const UsbTarget &)> stillThere) { stillThere_ = std::move(stillThere); }

private:
	void readOutput();
	void processEnded(int exitCode, QProcess::ExitStatus status);
	void look();                        /* every LOOK_MS: is the device still there */
	void finish(const QString &why);
	QString goneText() const;
	void keepInJob(void *process);      /* Windows */
	void endJob();

	QString program_;
	std::function<bool(const UsbTarget &)> stillThere_;
	UsbTarget target_;
	std::unique_ptr<UsbPcapDecoder> decoder_;
	QProcess *process_ = nullptr;
	QTimer *look_;
	QString why_;                       /* it could not start: what ended() says, from the event loop */
	bool started_ = false;              /* the stream's first bytes came */
	void *job_ = nullptr;               /* Windows: the job the program and its worker run in */
};

/* SPDX-License-Identifier: Apache-2.0 */
#include "io/usbpcap_capture.h"

#include <QTimer>

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int LOOK_MS = 500;
constexpr int STOP_MS = 500;

} // namespace

UsbPcapCapture::UsbPcapCapture(const QString &program, QObject *parent)
	: UsbCapture(parent), program_(program), stillThere_(&UsbTarget::stillThere), look_(new QTimer(this)) {
	connect(look_, &QTimer::timeout, this, &UsbPcapCapture::look);
}

UsbPcapCapture::~UsbPcapCapture() { stop(); }

void UsbPcapCapture::start(const UsbTarget &target) {
	stop();
	target_ = target;
	decoder_ = std::make_unique<UsbPcapDecoder>(target);
	why_.clear();
	process_ = new QProcess(this);
	connect(process_, &QProcess::readyReadStandardOutput, this, &UsbPcapCapture::readOutput);
	connect(process_, &QProcess::finished, this, &UsbPcapCapture::processEnded);
	/* a program that cannot start says so inside QProcess::start, and start() promises nothing before the event loop */
	connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		why_ = tr("USBPcapCMD could not be started: %1").arg(process_->errorString());
		look_->start(0);
	});
	look_->start(LOOK_MS);
#ifdef Q_OS_WIN
	/* USBPcapCMD's main thread waits on its standard input for a 'q' at a console. A pipe or NUL is always ready, and
	 * reading one as a console fails, so it would spin a whole core for as long as it runs; given no handle it leaves
	 * standard input out of that wait */
	process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
		arguments->startupInfo->hStdInput = nullptr;
	});
#endif
	process_->start(program_, usbPcapArguments(target));
}

/* QProcess::terminate is a WM_CLOSE on Windows, and USBPcapCMD has no window and stops on nothing but 'q' at a console:
 * it is the kill that ends it, which closes its handle on the driver and so ends the filtering too. Only this process:
 * it is the Studio's own child, and it runs no worker when elevated */
void UsbPcapCapture::stop() {
	look_->stop();
	if (!process_) return;
	QProcess *process = std::exchange(process_, nullptr);
	process->disconnect(this);
	if (process->state() != QProcess::NotRunning) {
		process->terminate();
		if (!process->waitForFinished(STOP_MS)) {
			process->kill();
			process->waitForFinished(STOP_MS);
		}
	}
	process->deleteLater(); /* this may be its own signal */
}

void UsbPcapCapture::readOutput() {
	QVector<UsbEvent> events;
	decoder_->feed(process_->readAllStandardOutput(), events);
	const QString why = decoder_->error();
	for (const UsbEvent &event : std::as_const(events)) {
		emit data(event.direction, event.data);
		if (!process_) return; /* a listener stopped this capture from its slot */
	}
	if (!why.isEmpty()) finish(why);
}

void UsbPcapCapture::processEnded(int exitCode, QProcess::ExitStatus status) {
	readOutput(); /* what it wrote before it went */
	if (!process_) return;
	const QByteArray errors = process_->readAllStandardError();
	finish(stillThere_(target_) ? usbPcapEndReason(exitCode, status == QProcess::CrashExit, errors) : goneText());
}

/* USBPcap hears a root hub, which outlives the device: USBPcapCMD would just go quiet, and a device plugged in again has
 * another address, which only a new start hears */
void UsbPcapCapture::look() {
	if (!why_.isEmpty()) finish(why_);
	else if (!stillThere_(target_)) finish(goneText());
}

void UsbPcapCapture::finish(const QString &why) {
	stop();
	emit ended(why);
}

QString UsbPcapCapture::goneText() const { return tr("%1 is gone: unplugged or reset?").arg(target_.port); }

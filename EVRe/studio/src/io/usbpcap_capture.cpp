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
	started_ = false;
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
	/* Suspended, so that nothing of it runs outside the job: its worker must be born inside.
	 *
	 * Its main thread waits on its standard input for a 'q' at a console. A pipe or NUL is always ready, and reading one
	 * as a console fails, so it would spin a whole core for as long as it runs; given no handle it leaves standard input
	 * out of that wait, on the program and on its worker alike */
	PROCESS_INFORMATION *process = nullptr;
	process_->setCreateProcessArgumentsModifier([&process](QProcess::CreateProcessArguments *arguments) {
		arguments->startupInfo->hStdInput = nullptr;
		arguments->flags |= CREATE_SUSPENDED;
		process = arguments->processInformation;
	});
	process_->start(program_, usbPcapArguments(target));
	if (process_->state() != QProcess::NotRunning && process) {
		keepInJob(process->hProcess);
		ResumeThread(process->hThread);
	}
#else
	process_->start(program_, usbPcapArguments(target));
#endif
}

/* The job goes first: that ends the program and its elevated worker at once. Without a job (it could not be made, or
 * another system) terminate() is a WM_CLOSE on Windows, and the program has no window and stops on nothing but 'q' at a
 * console: the kill ends it, and then the job the program made itself ends its worker. Only this process: it is the
 * Studio's own child */
void UsbPcapCapture::stop() {
	look_->stop();
	endJob();
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
	const QByteArray bytes = process_->readAllStandardOutput();
	if (bytes.isEmpty()) return;
	if (!started_) {
		started_ = true;
		emit started();
		if (!process_) return; /* a listener stopped this capture from its slot */
	}
	QVector<UsbEvent> events;
	decoder_->feed(bytes, events);
	const QString why = decoder_->error();
	for (const UsbEvent &event : std::as_const(events)) {
		emit data(event.direction, event.data);
		if (!process_) return;
	}
	if (!why.isEmpty()) finish(why);
}

void UsbPcapCapture::processEnded(int exitCode, QProcess::ExitStatus status) {
	readOutput(); /* what it wrote before it went */
	if (!process_) return;
	const QByteArray errors = process_->readAllStandardError();
	/* the worker's own words reach nobody: its end before any bytes is all that is known of it */
	const bool workerSilent = target_.asksPermission && !started_;
	finish(stillThere_(target_) ? usbPcapEndReason(exitCode, status == QProcess::CrashExit, errors, workerSilent) : goneText());
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

/* KILL_ON_JOB_CLOSE, and the handle is the Studio's alone: the system closes it however the Studio goes, a crash too.
 * USBPcapCMD looks for this flag and leaves dangling processes to whoever made the job, and the worker it starts through
 * UAC is born into the job of its parent, as with the job it makes for itself. Without a job of ours it makes that one:
 * a Stop then still ends the worker, and a crash does not */
void UsbPcapCapture::keepInJob(void *process) {
#ifdef Q_OS_WIN
	const HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (!job) return;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
			&& AssignProcessToJobObject(job, process))
		job_ = job;
	else
		CloseHandle(job);
#else
	Q_UNUSED(process);
#endif
}

void UsbPcapCapture::endJob() {
#ifdef Q_OS_WIN
	if (!job_) return;
	TerminateJobObject(job_, 1);
	CloseHandle(job_);
	job_ = nullptr;
#endif
}

/* SPDX-License-Identifier: Apache-2.0 */
/* The frame clock: see frame_clock.h. */
#include "ui/frame_clock.h"

#include <chrono>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

namespace {

constexpr int TIMER_INTERVAL_MS = 16;

#ifdef Q_OS_WIN
/* DwmFlush back sooner than this waited for no refresh: there is no compositor */
constexpr std::chrono::milliseconds NO_WAIT(2);
/* that many times in a row: the timer takes over, and the thread only looks again now and then */
constexpr int MAX_NO_WAITS = 20;
constexpr std::chrono::milliseconds RETRY_SOON(8);
constexpr std::chrono::milliseconds RETRY_LATER(250);
#endif

} // namespace

FrameClock::FrameClock(QObject *parent) : QObject(parent) {
	timer_.setInterval(TIMER_INTERVAL_MS);
	timer_.setTimerType(Qt::PreciseTimer);
	connect(&timer_, &QTimer::timeout, this, &FrameClock::tick);
}

FrameClock::~FrameClock() { stop(); }

void FrameClock::start() {
#ifdef Q_OS_WIN
	running_ = true;
	waiter_ = std::thread([this] { waitForRefreshes(); });
#else
	timer_.start();
#endif
}

void FrameClock::stop() {
	running_ = false;
	if (waiter_.joinable()) waiter_.join();
	timer_.stop();
}

#ifdef Q_OS_WIN
void FrameClock::waitForRefreshes() {
	int noWaits = 0;
	while (running_) {
		const auto before = std::chrono::steady_clock::now();
		const bool ok = SUCCEEDED(DwmFlush());
		if (!ok || std::chrono::steady_clock::now() - before < NO_WAIT) {
			if (++noWaits > MAX_NO_WAITS) {
				QMetaObject::invokeMethod(this, [this] {
					if (!timer_.isActive()) timer_.start();
				}, Qt::QueuedConnection);
				std::this_thread::sleep_for(RETRY_LATER);
			} else {
				std::this_thread::sleep_for(RETRY_SOON);
			}
		} else {
			/* the compositor is back: the refreshes pace the ticks again */
			if (noWaits > MAX_NO_WAITS)
				QMetaObject::invokeMethod(this, [this] { timer_.stop(); }, Qt::QueuedConnection);
			noWaits = 0;
		}
		if (noWaits <= MAX_NO_WAITS) postTick();
	}
}

void FrameClock::postTick() {
	if (tickQueued_.exchange(true)) return;
	QMetaObject::invokeMethod(this, [this] {
		tickQueued_ = false;
		emit tick();
	}, Qt::QueuedConnection);
}
#endif

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

bool RefreshPacing::waited(double ms) {
	/* a wait between the two limits ends both runs */
	slow_ = ms > SLOW_MS ? slow_ + 1 : 0;
	fast_ = ms <= FAST_MS ? fast_ + 1 : 0;
	if (!timer_ && slow_ >= SLOW_IN_A_ROW) timer_ = true;
	else if (timer_ && fast_ >= FAST_IN_A_ROW) timer_ = false;
	return timer_;
}

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
/* The timer ticks while there is no compositor, or while its refreshes come too slowly (RefreshPacing: a laptop on
 * battery answered every 76 ms); the thread keeps calling DwmFlush meanwhile, to see them come back. */
void FrameClock::waitForRefreshes() {
	int noWaits = 0;
	RefreshPacing pacing;
	bool timerOn = false;
	const auto useTimer = [this, &timerOn](bool on) {
		if (on == timerOn) return;
		timerOn = on;
		QMetaObject::invokeMethod(this, [this, on] {
			if (!on) timer_.stop();
			else if (!timer_.isActive()) timer_.start();
		}, Qt::QueuedConnection);
	};
	while (running_) {
		const auto before = std::chrono::steady_clock::now();
		const bool ok = SUCCEEDED(DwmFlush());
		const auto waited = std::chrono::steady_clock::now() - before;
		if (!ok || waited < NO_WAIT) {
			noWaits++;
			std::this_thread::sleep_for(noWaits > MAX_NO_WAITS ? RETRY_LATER : RETRY_SOON);
		} else {
			noWaits = 0; /* the compositor is back */
			pacing.waited(std::chrono::duration<double, std::milli>(waited).count());
		}
		useTimer(noWaits > MAX_NO_WAITS || pacing.timer());
		if (!timerOn) postTick();
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

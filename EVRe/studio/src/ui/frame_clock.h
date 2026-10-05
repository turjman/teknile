/* SPDX-License-Identifier: Apache-2.0 */
/* The frame clock: one tick() per refresh of the display, on which the window
 * copies the engine's values and samples and the chart moves on
 * (MainWindow::sync).
 *
 * On Windows a thread waits for each refresh of the display (DwmFlush) and
 * posts a tick into the thread the clock lives in, never a second one while
 * the first is still queued: the chart then moves by the same step at every
 * refresh of the screen, whatever its rate (a 16 ms timer against a 60 Hz
 * screen drops a frame every 0.4 s: a visible hitch). Elsewhere, and while the
 * compositor does not answer (a remote session, the screen off), a 16 ms timer
 * ticks instead. So it does while the refreshes come too slowly: a laptop on
 * battery answered DwmFlush every 76 ms (13 Hz), and the whole window ran at
 * 13 frames a second (RefreshPacing decides; the thread keeps waiting for the
 * refreshes meanwhile, to see them come back). */
#pragma once

#include <QObject>
#include <QTimer>
#include <atomic>
#include <thread>

/* Whether the 16 ms timer ticks in place of the display's refreshes, from how long each wait for a refresh took:
 * later than SLOW_MS SLOW_IN_A_ROW times in a row, the timer; back within FAST_MS FAST_IN_A_ROW times in a row, the
 * refreshes again. Apart from the clock, without Windows, so the rule is tested everywhere. */
class RefreshPacing {
public:
	static constexpr double SLOW_MS = 34, FAST_MS = 25;
	static constexpr int SLOW_IN_A_ROW = 3, FAST_IN_A_ROW = 10;
	bool waited(double ms); /* a wait ended: whether the timer ticks now */
	bool timer() const { return timer_; }

private:
	bool timer_ = false;
	int slow_ = 0, fast_ = 0; /* waits in a row of each kind */
};

class FrameClock : public QObject {
	Q_OBJECT
public:
	explicit FrameClock(QObject *parent = nullptr);
	~FrameClock() override; /* stops */

	void start();
	void stop(); /* the waiting thread has ended when this returns */

signals:
	void tick();

private:
	/* Windows only */
	void waitForRefreshes(); /* the waiting thread's loop */
	void postTick();         /* from that thread: one tick, unless one is queued already */

	QTimer timer_;           /* elsewhere, or with no compositor: every 16 ms */
	std::thread waiter_;
	std::atomic<bool> running_{ false };
	std::atomic<bool> tickQueued_{ false };
};

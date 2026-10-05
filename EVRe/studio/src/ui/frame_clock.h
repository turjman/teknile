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
 * ticks instead. */
#pragma once

#include <QObject>
#include <QTimer>
#include <atomic>
#include <thread>

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

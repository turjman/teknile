/* SPDX-License-Identifier: Apache-2.0 */
/* How often the numbers on screen change: the values in the Registers table and
 * in the chart's legend. The lines of the chart still move at every refresh of
 * the display (frame_clock.h): a number that changes 60 times a second cannot
 * be read, a line that moves 60 times a second is smooth.
 *
 * Chosen in the Polling & recording card ("Show values"), saved on every
 * change as "ui/valueRate" (values per second; 0 = at every refresh). */
#pragma once

#include <QVector>

namespace ValuePace {

constexpr int DEFAULT_PER_SECOND = 10;
constexpr int EVERY_FRAME = 0; /* at every refresh of the display */

const QVector<int> &choices(); /* per second, slowest first, then EVERY_FRAME */
int saved();                   /* "ui/valueRate"; one not among the choices: the default */
void save(int perSecond);

} // namespace ValuePace

/* due() says when the values on screen may change again: at most perSecond
 * times a second. It is asked once per display refresh, so a period is met at
 * the refresh closest to it, not one refresh late. */
class ValuePacer {
public:
	void setPerSecond(int perSecond); /* ValuePace::EVERY_FRAME: due at every call; the next call is due */
	int perSecond() const { return perSecond_; }
	bool due(qint64 nowMs);           /* true: show the newest values now */

private:
	int perSecond_ = ValuePace::DEFAULT_PER_SECOND;
	qint64 lastMs_ = -1;              /* when due() last said true; -1: never */
};

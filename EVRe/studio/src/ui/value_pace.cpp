/* SPDX-License-Identifier: Apache-2.0 */
/* How often the numbers on screen change: see value_pace.h. */
#include "ui/value_pace.h"

#include <QSettings>

namespace {

const QString SETTINGS_KEY = QStringLiteral("ui/valueRate");

/* a refresh comes every ~16.7 ms at 60 Hz, with some jitter: a period that is
 * short by less than this is met now, not at the next refresh */
constexpr qint64 FRAME_SLACK_MS = 4;

} // namespace

namespace ValuePace {

const QVector<int> &choices() {
	static const QVector<int> list{ 2, 5, 10, 30, EVERY_FRAME };
	return list;
}

int saved() {
	const int perSecond = QSettings().value(SETTINGS_KEY, DEFAULT_PER_SECOND).toInt();
	return choices().contains(perSecond) ? perSecond : DEFAULT_PER_SECOND;
}

void save(int perSecond) { QSettings().setValue(SETTINGS_KEY, perSecond); }

} // namespace ValuePace

void ValuePacer::setPerSecond(int perSecond) {
	perSecond_ = perSecond > 0 ? perSecond : ValuePace::EVERY_FRAME;
	lastMs_ = -1; /* the new pace shows the values at once */
}

bool ValuePacer::due(qint64 nowMs) {
	const bool now = perSecond_ == ValuePace::EVERY_FRAME || lastMs_ < 0 || nowMs < lastMs_
			|| nowMs - lastMs_ >= 1000 / perSecond_ - FRAME_SLACK_MS;
	if (now) lastMs_ = nowMs;
	return now;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* The proof P-9: see p9_proof.h. */
#include "phone/p9_proof.h"

#include <QQuickWindow>
#include <cmath>
#include <cstdio>
#ifdef Q_OS_ANDROID
#include <android/log.h>
#endif

#include "phone/phone_app.h"

P9Registers::P9Registers(QObject *parent) : QAbstractListModel(parent) {
	QString err;
	if (!map_.load(phone::mapsFolder() + QStringLiteral("/example_device.json"), err))
		qWarning("P-9: the example map: %s", qPrintable(err));
	values_.fill(QStringLiteral("-"), map_.regs.size());
	timer_.setTimerType(Qt::PreciseTimer);
	connect(&timer_, &QTimer::timeout, this, &P9Registers::tick);
	timer_.start(TICK_MS);
}

int P9Registers::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(map_.regs.size()); }

QVariant P9Registers::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= map_.regs.size()) return {};
	const RegDef &def = map_.regs[index.row()];
	switch (role) {
	case NameRole: return def.name;
	case AddressRole: return QStringLiteral("0x%1").arg(QString::number(def.addr, 16).toUpper().rightJustified(4, QLatin1Char('0')));
	case ValueRole: return values_[index.row()];
	case UnitRole: return def.unit;
	default: return {};
	}
}

QHash<int, QByteArray> P9Registers::roleNames() const {
	return { { NameRole, "name" }, { AddressRole, "address" }, { ValueRole, "value" }, { UnitRole, "unit" } };
}

/* every value a step along a slow sine of its own (its limits when the map gives them), as a polled device's would
 * change, all rows at once as a snapshot does */
void P9Registers::tick() {
	++ticks_;
	const double t = ticks_ * TICK_MS / 1000.0;
	for (int i = 0; i < map_.regs.size(); ++i) {
		const RegDef &def = map_.regs[i];
		if (!def.isNumeric()) continue;
		const double lo = std::isfinite(def.min) ? def.min : 0.0;
		const double hi = std::isfinite(def.max) ? def.max : 100.0;
		values_[i] = formatNumber(def, lo + (hi - lo) * (0.5 + 0.5 * std::sin(2 * M_PI * 0.5 * t + i * 0.7)));
	}
	if (!values_.isEmpty()) emit dataChanged(index(0), index(int(values_.size()) - 1), { ValueRole });
	emit ticked();
}

P9Meter::P9Meter(QQuickWindow *window, const char *tag, std::function<bool()> shown)
	: QObject(window), tag_(tag), shown_(std::move(shown)) {
	clock_.start();
	second_.start();
	/* direct: frameSwapped comes on the render thread (Android's threaded render loop); a queued call would time
	 * the window thread's queue instead of the frames */
	connect(window, &QQuickWindow::frameSwapped, this, &P9Meter::frameSwapped, Qt::DirectConnection);
	connect(&timer_, &QTimer::timeout, this, &P9Meter::report);
	timer_.start(1000);
}

void P9Meter::frameSwapped() {
	const qint64 now = clock_.nsecsElapsed();
	QMutexLocker lock(&mutex_);
	if (lastNs_ >= 0) {
		const qint64 frame = now - lastNs_;
		longestNs_ = std::max(longestNs_, frame);
		if (frame > qint64(SLOW_MS * 1e6)) ++slow_;
	}
	lastNs_ = now;
	++frames_;
}

void P9Meter::report() {
	int frames, slow;
	qint64 longest;
	{
		QMutexLocker lock(&mutex_);
		frames = frames_;
		slow = slow_;
		longest = longestNs_;
		frames_ = slow_ = 0;
		longestNs_ = 0;
	}
	const double seconds = second_.nsecsElapsed() / 1e9;
	second_.restart();
	if (shown_ && !shown_()) return; /* another screen: its second is not this line's */
	const QString line = QStringLiteral("fps %1, longest %2 ms, over %3 ms: %4")
			.arg(frames / std::max(seconds, 1e-3), 0, 'f', 1)
			.arg(longest / 1e6, 0, 'f', 1)
			.arg(SLOW_MS, 0, 'f', 0)
			.arg(slow);
#ifdef Q_OS_ANDROID
	__android_log_write(ANDROID_LOG_INFO, tag_, qPrintable(line));
#else
	std::fprintf(stdout, "%s %s\n", tag_, qPrintable(line));
	std::fflush(stdout);
#endif
	emit logged(line);
}

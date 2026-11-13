/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's test feed: see chart_demo.h. */
#include "phone/chart_demo.h"

#include <QColor>
#include <QVector>
#include <cmath>

ChartDemo::ChartDemo(QObject *parent) : QObject(parent) {
	timer_.setTimerType(Qt::PreciseTimer);
	connect(&timer_, &QTimer::timeout, this, &ChartDemo::tick);
}

void ChartDemo::setChart(ChartItem *chart) {
	if (chart == chart_) return;
	chart_ = chart;
	if (chart_ && running()) addLines();
	emit chartChanged();
}

void ChartDemo::setRunning(bool on) {
	if (on == running()) return;
	if (on) {
		clock_.start();
		sent_ = 0;
		if (chart_) addLines();
		timer_.start(TICK_MS);
	} else {
		timer_.stop();
	}
	emit runningChanged();
}

void ChartDemo::setColours(const QVariantList &colours) {
	if (colours == colours_) return;
	colours_ = colours;
	if (chart_ && running()) addLines(); /* the lines there take the new colours */
	emit coloursChanged();
}

/* the lines in a 1 min window, live, on the demo's clock */
void ChartDemo::addLines() {
	for (int k = 0; k < LINES; ++k) {
		const QColor colour = colours_.isEmpty() ? QColor::fromHsv(k * 45, 200, 230) : colours_[k % colours_.size()].value<QColor>();
		chart_->addLine(k, colour);
	}
	chart_->setClock([this] { return clock_.isValid() ? clock_.nsecsElapsed() / 1e9 : 0.0; });
	chart_->setWindowSeconds(60);
	chart_->setLive(true);
}

double ChartDemo::signal(int k, double t) {
	const double offset = 7 - 2.0 * k; /* each line a band of its own: 7, 5, 3 ... -7 */
	const double phase = std::fmod(t, 1.0);
	switch (k) {
	case 0: return offset + 0.8 * std::sin(2 * M_PI * 1.0 * t);                           /* a sine, 1 Hz */
	case 1: return offset + 0.6 * std::sin(2 * M_PI * 7.0 * t);                           /* 7 Hz, noise added */
	case 2: return offset + (std::fmod(t, 2.0) < 1.0 ? 0.7 : -0.7);                       /* a square, 0.5 Hz */
	case 3: return offset + 0.8 * (4 * std::fabs(std::fmod(t / 4.0, 1.0) - 0.5) - 1);     /* a triangle, 0.25 Hz */
	case 4: return offset + 0.8 * (2 * std::fmod(t * 2.0, 1.0) - 1);                      /* a sawtooth, 2 Hz */
	case 5: return offset;                                                                /* noise alone */
	case 6: return offset + 0.8 * std::sin(2 * M_PI * (0.5 + 4.5 * phase) * phase * 2);   /* a chirp, each second */
	default: return offset + 0.5 * std::sin(2 * M_PI * 0.2 * t) + 0.15 * std::sin(2 * M_PI * 50 * t); /* a ripple */
	}
}

/* every line's samples from the last one sent up to now, at RATE: a polled device's samples come in such bursts */
void ChartDemo::tick() {
	if (!chart_) return;
	const qint64 due = qint64(clock_.nsecsElapsed() / 1e9 * RATE);
	if (due <= sent_) return;
	std::normal_distribution<double> gauss(0.0, 1.0);
	QVector<double> times, values;
	times.reserve(int(due - sent_));
	values.reserve(int(due - sent_));
	for (int k = 0; k < LINES; ++k) {
		times.clear();
		values.clear();
		for (qint64 i = sent_; i < due; ++i) {
			const double t = double(i) / RATE;
			double v = signal(k, t);
			if (k == 1) v += 0.15 * gauss(noise_);
			if (k == 5) v += 0.4 * gauss(noise_);
			times.push_back(t);
			values.push_back(v);
		}
		chart_->appendSamples(k, times, values);
	}
	sent_ = due;
}

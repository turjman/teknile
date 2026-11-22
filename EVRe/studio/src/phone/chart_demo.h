/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's test feed for the phone (android_plan 10.7, B.3: P-6 again on the GPU): with EVRE_CHARTDEMO=1, or on
 * Android the start intent's extra (am start ... --ez chartdemo true), the Chart screen shows LINES lines of a
 * synthetic signal at RATE samples a second each (a sine, a noisy sine, a square, a triangle, a sawtooth, noise, a
 * chirp, a ripple) in a 1 min window, and the window's frames go to logcat once a second under the tag EVRE_PERF
 * (P9Meter: fps, the longest frame, frames over 20 ms). The target: 60 fps, no frame over 20 ms. No device, no engine:
 * what the chart costs the screen. Never shown otherwise. */
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <random>

#include "phone/chart_item.h"

class ChartDemo : public QObject {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(ChartItem *chart READ chart WRITE setChart NOTIFY chartChanged)
	Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
	Q_PROPERTY(QVariantList colours READ colours WRITE setColours NOTIFY coloursChanged)

public:
	static constexpr int LINES = 8;
	static constexpr double RATE = 1000; /* samples a second, each line */
	static constexpr int TICK_MS = 8;    /* the samples up to now, twice a frame at 60 fps */

	explicit ChartDemo(QObject *parent = nullptr);

	ChartItem *chart() const { return chart_; }
	void setChart(ChartItem *chart);
	bool running() const { return timer_.isActive(); }
	void setRunning(bool on);
	QVariantList colours() const { return colours_; }
	void setColours(const QVariantList &colours); /* the theme's series colours: the lines take them in order */
	/* line k's value at time t (seconds), without its noise */
	static double signal(int k, double t);

signals:
	void chartChanged();
	void runningChanged();
	void coloursChanged();

private:
	void addLines();
	void tick();

	QPointer<ChartItem> chart_;
	QVariantList colours_;
	QTimer timer_;
	QElapsedTimer clock_;
	qint64 sent_ = 0; /* samples given to each line so far */
	std::mt19937 noise_{ 12345 };
};

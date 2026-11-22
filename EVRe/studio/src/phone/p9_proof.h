/* SPDX-License-Identifier: Apache-2.0 */
/* The proof P-9 (android_plan 10.7, B.0): can Qt Quick flick the registers at 60 frames a second on the phone while
 * their values change? Reached only with EVRE_P9=1 or the start intent's extra p9 (phone_app.cpp), never otherwise.
 *
 *   P9Registers  the example map's registers as a list (name, address, value, unit), every value changed 20 times
 *                a second by a timer: no device, no engine, only what a live list costs the screen
 *   P9Meter      the window's frames, from QQuickWindow::frameSwapped: once a second one line, "fps 60.0, longest
 *                17.1 ms, over 20 ms: 0", to logcat under the tag EVRE_P9 (on a desktop: the program's output). The
 *                speed line of the Registers screen (perf, phone_app.h) is the same meter under the tag EVRE_PERF,
 *                writing only while that screen shows; so is the chart's test feed's (chartdemo, chart_demo.h) while
 *                the Chart screen shows
 *
 * The screen (P9Screen.qml) keeps a small bar running, so the window draws every frame and the time between two
 * frames is a frame's time, also while nothing is flicked. */
#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <QtQml/qqmlregistration.h>

#include "model/device_map.h"

class QQuickWindow;

class P9Registers : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	Q_PROPERTY(int ticks READ ticks NOTIFY ticked)

public:
	enum Role { NameRole = Qt::UserRole + 1, AddressRole, ValueRole, UnitRole };
	static constexpr int TICK_MS = 50; /* 20 changes a second */

	explicit P9Registers(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	int ticks() const { return ticks_; }

signals:
	void ticked();

private:
	void tick();

	DeviceMap map_;
	QStringList values_;
	QTimer timer_;
	int ticks_ = 0;
};

class P9Meter : public QObject {
	Q_OBJECT

public:
	static constexpr double SLOW_MS = 20.0; /* a frame longer than this is counted: P-9 wants none */

	/* tag: logcat's; shown: whether to write this second's line (none given: always) */
	explicit P9Meter(QQuickWindow *window, const char *tag = "EVRE_P9", std::function<bool()> shown = {});
	QByteArray tag() const { return QByteArray(tag_); }

signals:
	void logged(const QString &line);

private:
	void frameSwapped(); /* on the render thread */
	void report();       /* once a second, on the window's thread */

	QMutex mutex_;       /* the counts below: written on the render thread, read and reset by report() */
	QElapsedTimer clock_;
	qint64 lastNs_ = -1;
	int frames_ = 0;
	qint64 longestNs_ = 0;
	int slow_ = 0;
	QElapsedTimer second_;
	QTimer timer_;
	const char *tag_;
	std::function<bool()> shown_;
};

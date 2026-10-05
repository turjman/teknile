/* SPDX-License-Identifier: Apache-2.0 */
/* The I/O thread's register table: the map's definitions and the values last
 * read from the device. Written only in the I/O thread (the poller, the API,
 * reads on request), so that thread reads it without a lock; every write
 * takes the mutex, so the window can copy it from its own thread
 * (snapshot()). */
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include "model/device_map.h"

/* One register of the table: its definition and what was last read of it. */
struct RegValue {
	RegDef def;
	QByteArray raw;
	bool valid = false;
	QString error;            /* last read error, empty if fine */
	bool unavailable = false; /* the device refused the address: not polled */
	qint64 updatedMs = -1;    /* table clock of the last good read */
	quint64 version = 0;      /* +1 on every change, so a copy knows what moved */
};

/* The table itself: see the top of this file. */
class RegTable {
public:
	RegTable() { clock_.start(); }

	/* in the I/O thread only */
	const QVector<RegValue> &rows() const { return rows_; }
	int size() const { return int(rows_.size()); }

	/* generation: the window's number for this map, so its copies can be matched */
	/* a register read the same way as before (its device and address, type, size) keeps its value: an edit
	 * of the map does not blank the table or the API until the next poll. By device too: on a bus every
	 * device has a register at the same address, and D2's took D1's value until its next poll */
	void setDefs(const QVector<RegDef> &defs, quint64 generation) {
		QMutexLocker lock(&mutex_);
		QVector<RegValue> before;
		before.swap(rows_);
		QHash<RegKey, int> beforeRow;
		for (int i = int(before.size()) - 1; i >= 0; i--) beforeRow.insert(regKey(before[i].def), i); /* the first wins */
		for (const RegDef &d : defs) {
			RegValue v;
			const int was = beforeRow.value(regKey(d), -1);
			if (was >= 0 && before[was].def.type == d.type && before[was].def.size == d.size) v = before[was];
			v.def = d;
			rows_.push_back(v);
		}
		generation_ = generation;
	}
	void setRaw(int row, const QByteArray &raw) {
		QMutexLocker lock(&mutex_);
		RegValue &v = rows_[row];
		v.updatedMs = clock_.elapsed();
		v.version++; /* also for the same value read again: its age changes */
		if (v.valid && v.raw == raw && v.error.isEmpty()) return;
		v.raw = raw;
		v.valid = true;
		v.error.clear();
	}
	void setError(int row, const QString &error, bool unavailable) {
		QMutexLocker lock(&mutex_);
		RegValue &v = rows_[row];
		if (v.error == error && v.unavailable == unavailable) return;
		v.error = error;
		v.unavailable = unavailable;
		v.version++;
	}
	void clearValues() {
		QMutexLocker lock(&mutex_);
		for (RegValue &v : rows_) {
			v.valid = false;
			v.raw.clear();
			v.error.clear();
			v.unavailable = false;
			v.version++;
		}
	}

	/* from any thread: a copy, the map generation it belongs to, and the
	 * table clock now (ages = nowMs - updatedMs) */
	void snapshot(QVector<RegValue> &out, quint64 &generation, qint64 &nowMs) const {
		QMutexLocker lock(&mutex_);
		out = rows_;
		generation = generation_;
		nowMs = clock_.elapsed();
	}

private:
	mutable QMutex mutex_;
	QVector<RegValue> rows_;
	quint64 generation_ = 0;
	QElapsedTimer clock_;
};

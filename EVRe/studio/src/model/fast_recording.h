/* SPDX-License-Identifier: Apache-2.0 */
/* A fast stream's recording, `.evrs` (Fast EVRe, FAST_PLAN.md section 8.3): the blocks as they came, in a file of
 * pieces, each a name of 4 ASCII bytes and a length (u32, little endian: the body's bytes) before its body, so a
 * reader skips a piece it does not know and stops at one that is cut off:
 *
 *   EVRS   always first: JSON, UTF-8: "format": "evre-fast-rec/1", "device" (the map's), "stream" (the
 *          stream's object from the map), "start" (local time, ISO 8601 with milliseconds) and "start_s" (the
 *          writer's clock at that moment, the clock of the TIME pieces: the Studio's time_s; 0 for evre record)
 *   TIME   16 bytes: a record number (u64) and the writer's clock for it in seconds (f64, little endian): one
 *          before the first block of every start, then about one a second, as the clock's fit had it
 *   BLK    (the fourth byte a space) one block as it came: its 8-byte header and its records
 *
 * Nothing is converted and nothing is lost: gaps stay gaps. One file a stream, beside the CSV of the same
 * recording (run.csv, run.ADC.evrs). evre record and the Studio's recording write it; the recording window and
 * evre.read_recording (Python) read it.
 */
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

#include "model/device_map.h"

class QFile;

namespace fast {

constexpr char RECORDING_FORMAT[] = "evre-fast-rec/1";

class RecordingWriter {
public:
	RecordingWriter();
	~RecordingWriter();
	/* the file made and its head written; false with err when it cannot be */
	bool open(const QString &file, const QString &device, const StreamDef &stream, const QDateTime &start, QString &err,
			double startClock = 0);
	bool isOpen() const;
	bool mark(quint64 record, double time);       /* a TIME piece */
	bool block(const QByteArray &data);           /* a BLK piece: a block's frame data as it came */
	bool flush();
	void close();
	qint64 bytes() const { return bytes_; }       /* written so far */
	QString errorString() const;

private:
	bool piece(const char name[4], const QByteArray &body);

	std::unique_ptr<QFile> file_;
	qint64 bytes_ = 0;
};

/* the file name of a stream's recording beside a CSV: run.csv -> run.ADC.evrs */
QString recordingFileFor(const QString &csv, const QString &stream);
/* the streams' recordings beside a CSV: run.csv -> run.*.evrs, by name */
QStringList recordingsBeside(const QString &csv);

class Store;

/* A recording read: the file mapped into memory, not read into it (one larger than the RAM opens), its blocks
 * through the block's rules into a store that leaves their records where they lie; the TIME pieces lay them on the
 * writer's clock. A file cut off (a crash, a full disk) is read up to its last whole piece. */
struct Recording {
	QString file, device;
	StreamDef stream;
	QDateTime start;
	double startClock = 0;              /* the writer's clock at `start` */
	std::shared_ptr<Store> store;       /* holds the mapped file while it lives */
	qint64 blocks = 0, badBlocks = 0;
	quint64 lost = 0;                   /* records lost between blocks (gaps), as counted when it was written */
	qint64 bytes = 0, bytesRead = 0;    /* the file's, and up to its last whole piece */
	bool cut = false;                   /* it ends inside a piece */
	double firstTime = 0, lastTime = 0; /* the first and last record's, on the writer's clock */
	qint64 epochMs() const { return start.toMSecsSinceEpoch() - qint64(startClock * 1000.0 + 0.5); } /* clock 0 */
};
/* false with error ("" when cancelled); progress gets 0..1 now and then */
bool readRecording(const QString &path, const std::atomic<bool> &cancel, const std::function<void(double)> &progress,
		Recording &out, QString &error);

} // namespace fast

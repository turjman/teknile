/* SPDX-License-Identifier: Apache-2.0 */
/* A fast stream's recording, `.evrs` (Fast EVRe, FAST_PLAN.md section 8.3): the blocks as they came, in a file of
 * pieces, each a name of 4 ASCII bytes and a length (u32, little endian: the body's bytes) before its body, so a
 * reader skips a piece it does not know and stops at one that is cut off:
 *
 *   EVRS   always first: JSON, UTF-8: "format": "evre-fast-rec/1", "device" (the map's), "stream" (the
 *          stream's object from the map), "start" (local time, ISO 8601 with milliseconds)
 *   TIME   16 bytes: a record number (u64) and the writer's clock for it in seconds (f64, little endian): one
 *          before the first block of every start, then about one a second, as the clock's fit had it
 *   BLK    (the fourth byte a space) one block as it came: its 8-byte header and its records
 *
 * Nothing is converted and nothing is lost: gaps stay gaps. One file a stream, beside the CSV of the same
 * recording (run.csv, run.ADC.evrs). The writer is here; evre record and (from 5.4) the Studio use it.
 */
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
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
	bool open(const QString &file, const QString &device, const StreamDef &stream, const QDateTime &start, QString &err);
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

} // namespace fast

/* SPDX-License-Identifier: Apache-2.0 */
/* Recordings as files: the CSV the Studio writes (a recording, chapter 12, or
 * an export of the chart) read back into lines of samples, the chart's lines
 * written out in that format, and the notes kept beside a recording.
 *
 *  - The format: a header "time_s,datetime,NAME [unit],...", then a row per
 *    time: time_s (the Studio's clock, seconds), the local clock time (ISO 8601
 *    with ms), a cell per column; an empty cell is no sample. A comma in a
 *    title is written as a semicolon.
 *  - Reading: the columns by their titles, each a line of (time, value). A
 *    column with a cell that is not a number (a byte array's hex) is left out.
 *    estimate() looks at the head and the tail only (a file of gigabytes): its
 *    rows, columns, span and the memory its samples need, so the caller can ask
 *    to keep the last part (read() from a byte offset).
 *  - Writing: the lines' samples merged into rows; samples of different lines
 *    closer than a quarter of their own interval share a row (a poll gives
 *    its registers one time; other rates are never forced together).
 *  - Notes: "<recording>.notes.json", the chart's labelled markers by time_s.
 *
 * Nothing here knows the chart or a window: the reading and the writing run on
 * a thread of their own (cancel, progress). */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

/* a labelled marker on the chart, at a time of the chart's (or the recording's) time base */
struct ChartNote {
	double time = 0;
	QString text;
	bool operator==(const ChartNote &other) const { return time == other.time && text == other.text; }
};

namespace recording {

/* one column of a recording: its title "NAME [unit]" taken apart, and its samples */
struct Column {
	QString name, unit;
	QVector<double> times, values;
};

/* what a file holds, from its head and its tail */
struct Estimate {
	QStringList titles;      /* the value columns' titles, after time_s and datetime */
	qint64 bytes = 0;        /* the file's size */
	qint64 headerBytes = 0;  /* where the first row starts */
	qint64 rows = 0;         /* about; from the first rows' length */
	double firstTime = 0, lastTime = 0; /* time_s of the first and the last row */
	qint64 samples() const { return rows * titles.size(); } /* at most */
};

/* the recording's columns, and the wall-clock time of its time base's zero (ms since the epoch; from the first
 * row's datetime) */
struct Data {
	QVector<Column> columns;
	qint64 epochMs = 0;
	qint64 rows = 0;
	int skipped = 0; /* columns left out: not numbers */
};

/* "NAME [unit]" -> NAME, unit; a title without "[...]" is all name */
void splitTitle(const QString &title, QString &name, QString &unit);
/* a column's title for name and unit (a comma becomes a semicolon) */
QString title(const QString &name, const QString &unit);

bool estimate(const QString &path, Estimate &out, QString &error);
/* the rows from byte `from` on (0 or the header's end: all; further: the first whole row after it); progress
 * gets 0..1 now and then; false and error empty when cancelled */
bool read(const QString &path, qint64 from, const std::atomic<bool> &cancel,
		const std::function<void(double)> &progress, Data &out, QString &error);

/* a line to write: its title's parts and its samples (times rising) */
struct Line {
	QString name, unit;
	QVector<double> times, values;
};
/* the lines in the recording's format, rows merged as said above; rows: how many were written. A cancelled or failed
 * write removes the file. */
bool write(const QString &path, const QVector<Line> &lines, qint64 epochMs, const std::atomic<bool> &cancel,
		const std::function<void(double)> &progress, qint64 &rows, QString &error);
/* a sample of another line shares a row when it is this close, as a share of that line's own interval */
constexpr double ROW_SHARE = 0.25;

/* the notes beside a recording */
QString notesPath(const QString &recordingPath);
/* false with error empty: there is no notes file */
bool loadNotes(const QString &recordingPath, QVector<ChartNote> &notes, QString &error);
/* notes empty: the file removed (if there is one) */
bool saveNotes(const QString &recordingPath, const QVector<ChartNote> &notes, qint64 epochMs, QString &error);

} // namespace recording

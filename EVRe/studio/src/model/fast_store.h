/* SPDX-License-Identifier: Apache-2.0 */
/* A fast stream's records as the chart keeps them (Fast EVRe, FAST_PLAN.md section 8.2): the records as they came,
 * a few bytes each (two i16 channels: 4 bytes, not the 23 a polled sample takes), in pieces of PIECE records; the
 * starts and gaps as a short list; and for each channel summaries at two levels, made as the records come: the
 * min, max, sum and sum of squares of every SMALL and every LARGE records. A view of an hour and one of 100 us then
 * cost the same: the work follows the pixels, not the records.
 *
 *   index i (0 .. size() - 1): the records kept, oldest first; dropped() + i is the record's place since the store
 *   began (it never moves: summaries and bins are counted on it)
 *
 *   a segment: records that follow one another with nothing lost; a gap (records lost) or a new start of the stream
 *   begins the next. An epoch: the records of one start, laid on the host's clock by its time marks (the record
 *   number and its time, and the period from there on, io/fast_stream.h FastClock): between two marks, and after the
 *   last, the time is the mark's plus the records since it times its period.
 *
 * Trimming drops whole pieces from the front (dropFront). The store is written by one thread (the window's, between
 * frames) and read by any number while nobody writes (the chart's threads, binning).
 */
#pragma once

#include <QByteArray>
#include <QVector>
#include <cstdint>
#include <memory>

#include "model/device_map.h"

namespace fast {

class Store {
public:
	static constexpr qsizetype PIECE = 65536; /* records per piece: what a trim drops at least */
	static constexpr qsizetype SMALL = 256;   /* the summaries' two levels */
	static constexpr qsizetype LARGE = 4096;

	struct Mark {
		quint64 record = 0;  /* the record's number in its start */
		double time = 0;     /* its time, seconds of the host's clock */
		double period = 0;   /* seconds a record from there on */
	};

	Store() = default;
	explicit Store(const StreamDef &def) { reset(def); }
	void reset(const StreamDef &def); /* empty, for this stream */
	const StreamDef &def() const { return def_; }
	int recordSize() const { return recordSize_; }
	int channels() const { return int(def_.channels.size()); }

	/* Records in: count records (count x recordSize() bytes) numbered from `first` in their start; newStart: a new
	 * start (the first block, a restart: a new epoch), lost: records missing before them (a new segment). */
	void append(quint64 first, qsizetype count, const char *records, bool newStart, quint64 lost);
	/* the same, the records left where they are (a recording's file mapped into memory, which must outlive the store):
	 * only the summaries and the lists are made. A store is filled one way or the other; it is not trimmed. */
	void appendMapped(quint64 first, qsizetype count, const char *records, bool newStart, quint64 lost);
	bool mapped() const { return !spans_.isEmpty(); }
	void keep(std::shared_ptr<void> owner) { owner_ = std::move(owner); } /* mapped: what holds the records' memory */
	/* a time mark of the current start (FastClock::mark and its period) */
	void mark(quint64 record, double time, double period);
	void clear();
	/* the oldest whole pieces, as many as fit in `records` (none of the records after the last whole piece) */
	void dropFront(qsizetype records);

	qsizetype size() const { return size_; }
	qint64 dropped() const { return dropped_; }
	qint64 bytes() const;                 /* the memory held now: pieces, summaries, the lists */
	double bytesPerRecord() const;        /* a record's share: its bytes (not when mapped) and its summaries' */
	bool hasTime() const;                 /* a mark has come: the records have times */

	/* a record's time; its channel's shown value (raw x scale + offset); i in 0 .. size() - 1 */
	double timeAt(qsizetype i) const;
	double value(int channel, qsizetype i) const;
	/* the first record at or after t (size(): none), the first after t */
	qsizetype lowerBound(double t) const;
	qsizetype upperBound(double t) const;
	/* the record after the last one of i's segment (a gap or a new start follows it, or the end) */
	qsizetype segmentEnd(qsizetype i) const;
	/* i begins a segment after a gap or a new start (the oldest record kept begins none) */
	bool startsAfterGap(qsizetype i) const;
	/* the records lost just before i, where i begins a segment after a gap; -1: a new start of the stream (its
	 * numbers begin again, what was lost cannot be counted) */
	qint64 lostBefore(qsizetype i) const;
	qsizetype gaps() const { return segments_.size() - 1; } /* tests */

	/* over records i0 .. i1 - 1 of a channel, from the summaries where they cover whole pieces of it */
	void minMax(int channel, qsizetype i0, qsizetype i1, double &lo, double &hi) const;
	void sums(int channel, qsizetype i0, qsizetype i1, double &sum, double &sumSquares) const;

private:
	struct Segment {
		qint64 begin = 0;   /* its first record, counted since the store began */
		quint64 record = 0; /* that record's number in its start */
		int epoch = 0;
	};
	struct Epoch {
		QVector<Mark> marks; /* by record */
		double shift = 0;    /* added to its times: a start never begins before the one before ended */
	};
	struct Summary {
		QVector<double> min, max, sum, squares; /* one per full chunk, from chunk `first` on */
		qint64 first = 0;
	};
	struct Channel {
		int offset = 0;     /* its bytes in a record */
		RegType type = RegType::I16;
		double scale = 1, offset0 = 0;
		Summary small, large;
	};

	const char *recordAt(qsizetype i) const;
	void begin(quint64 first, qsizetype count, bool newStart, quint64 lost); /* the lists for records coming in */
	double decode(const Channel &c, const char *record) const;
	int segmentOf(qint64 absolute) const;
	qsizetype bound(double t, bool strict) const;
	double timeOfRecord(int epoch, double record) const;
	void summarize();                     /* the chunks the newest records completed */

	StreamDef def_;
	int recordSize_ = 0;
	QVector<Channel> channels_;
	QVector<QByteArray> pieces_;          /* each PIECE records (the last filling) */
	struct Span {
		const char *data = nullptr;
		qsizetype begin = 0, count = 0;   /* its first record's index, its records */
	};
	QVector<Span> spans_;                 /* mapped: where each block's records lie */
	QVector<int> spanOfChunk_;            /* mapped: the span of each SMALL records' first, so finding one is short */
	std::shared_ptr<void> owner_;
	qint64 dropped_ = 0;
	qsizetype size_ = 0;
	QVector<Segment> segments_;
	QVector<Epoch> epochs_;               /* each start's marks */
	int epochBase_ = 0;                   /* epochs dropped from the front */
	quint64 nextRecord_ = 0;              /* the number the next record would have, in the current start */
};

} // namespace fast

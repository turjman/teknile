/* SPDX-License-Identifier: Apache-2.0 */
/* A fast stream's records as the chart keeps them (see fast_store.h). */
#include "model/fast_store.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace fast {

namespace {

uint16_t le16(const uint8_t *b) { return uint16_t(b[0] | (b[1] << 8)); }
uint32_t le32(const uint8_t *b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }

} // namespace

void Store::reset(const StreamDef &def) {
	def_ = def;
	recordSize_ = def.recordSize();
	channels_.clear();
	int offset = 0;
	for (const StreamChannel &c : def.channels) {
		Channel channel;
		channel.offset = offset;
		channel.type = c.type;
		channel.scale = c.scale;
		channel.offset0 = c.offset;
		channels_.push_back(channel);
		offset += typeSize(c.type);
	}
	epochs_.clear();
	epochBase_ = 0;
	clear();
}

/* Everything let go but the newest time mark: the stream goes on, and its next records still need their times */
void Store::clear() {
	pieces_.clear();
	spans_.clear();
	spanOfChunk_.clear();
	dropped_ = 0;
	size_ = 0;
	segments_.clear();
	if (!epochs_.isEmpty()) {
		Epoch kept = epochs_.last();
		if (kept.marks.size() > 1) kept.marks = { kept.marks.last() };
		epochs_ = { kept };
	}
	epochBase_ = 0;
	for (Channel &c : channels_) c.small = c.large = Summary();
}

void Store::begin(quint64 first, qsizetype count, bool newStart, quint64 lost) {
	if (newStart || epochs_.isEmpty()) epochs_.push_back(Epoch());
	if (recordSize_ <= 0 || count <= 0) return;
	const int epoch = epochBase_ + int(epochs_.size()) - 1;
	const qint64 at = dropped_ + size_;
	if (segments_.isEmpty() || newStart || lost > 0 || first != nextRecord_ || segments_.last().epoch != epoch)
		segments_.push_back({ at, first, epoch });
}

void Store::appendMapped(quint64 first, qsizetype count, const char *records, bool newStart, quint64 lost) {
	begin(first, count, newStart, lost);
	if (recordSize_ <= 0 || count <= 0) return;
	spans_.push_back({ records, size_, count });
	for (qsizetype chunk = spanOfChunk_.size(); chunk * SMALL < size_ + count; chunk++)
		spanOfChunk_.push_back(int(spans_.size() - 1));
	size_ += count;
	nextRecord_ = first + quint64(count);
	summarize();
}

void Store::append(quint64 first, qsizetype count, const char *records, bool newStart, quint64 lost) {
	begin(first, count, newStart, lost);
	if (recordSize_ <= 0 || count <= 0) return;
	/* into the pieces, a piece's room taken at once */
	qsizetype done = 0;
	while (done < count) {
		if (pieces_.isEmpty() || pieces_.last().size() == PIECE * recordSize_) {
			pieces_.push_back(QByteArray());
			pieces_.last().reserve(PIECE * recordSize_);
		}
		QByteArray &piece = pieces_.last();
		const qsizetype room = PIECE - piece.size() / recordSize_;
		const qsizetype n = std::min(room, count - done);
		piece.append(records + done * recordSize_, n * recordSize_);
		done += n;
	}
	size_ += count;
	nextRecord_ = first + quint64(count);
	summarize();
}

/* The first mark of a start lays its records on the clock; one that would put them before the start before (the
 * clock of the last start ran ahead) shifts the whole start after it: the times never go back. */
void Store::mark(quint64 record, double time, double period) {
	if (epochs_.isEmpty()) epochs_.push_back(Epoch());
	Epoch &epoch = epochs_.last();
	epoch.marks.push_back({ record, time, period });
	if (epoch.marks.size() > 1 || segments_.size() < 2) return;
	const int current = epochBase_ + int(epochs_.size()) - 1;
	int k = int(segments_.size()) - 1;
	while (k > 0 && segments_[k - 1].epoch == current) k--;
	if (k == 0 || segments_[k].begin <= dropped_) return; /* nothing of an earlier start kept */
	const double before = timeAt(qsizetype(segments_[k].begin - dropped_) - 1);
	const double firstTime = timeAt(qsizetype(segments_[k].begin - dropped_));
	if (firstTime <= before) epoch.shift = before + period - firstTime;
}

bool Store::hasTime() const {
	return !segments_.isEmpty() && std::any_of(epochs_.begin(), epochs_.end(), [](const Epoch &e) { return !e.marks.isEmpty(); });
}

void Store::dropFront(qsizetype records) {
	if (!spans_.isEmpty()) return; /* mapped: the file holds them */
	const qsizetype pieces = std::min(records / PIECE, size_ / PIECE);
	if (pieces <= 0) return;
	const qsizetype n = pieces * PIECE;
	pieces_.remove(0, pieces);
	dropped_ += n;
	size_ -= n;
	for (Channel &c : channels_) {
		for (Summary *s : { &c.small, &c.large }) {
			const qsizetype chunk = s == &c.small ? SMALL : LARGE;
			const qsizetype gone = std::min<qsizetype>(s->min.size(), dropped_ / chunk - s->first);
			if (gone <= 0) continue;
			for (QVector<double> *v : { &s->min, &s->max, &s->sum, &s->squares }) {
				v->remove(0, gone);
				/* the room freed at the front let go: Qt's vectors keep it */
				if (v->capacity() > 2 * v->size() + 1024) v->squeeze();
			}
			s->first += gone;
		}
	}
	while (segments_.size() > 1 && segments_[1].begin <= dropped_) segments_.removeFirst();
	/* the starts no segment is of any more, and the marks before the one the oldest record needs */
	if (!segments_.isEmpty()) {
		const int firstEpoch = segments_.first().epoch - epochBase_;
		if (firstEpoch > 0) {
			epochs_.remove(0, firstEpoch);
			epochBase_ += firstEpoch;
		}
		const quint64 oldest = segments_.first().record + quint64(dropped_ - std::min(dropped_, segments_.first().begin));
		QVector<Mark> &marks = epochs_.first().marks;
		while (marks.size() > 1 && marks[1].record <= oldest) marks.removeFirst();
	}
}

qint64 Store::bytes() const {
	qint64 total = 0;
	for (const QByteArray &piece : pieces_) total += piece.capacity();
	total += spans_.capacity() * qint64(sizeof(Span)) + spanOfChunk_.capacity() * qint64(sizeof(int)); /* not the file's */
	for (const Channel &c : channels_)
		total += qint64(c.small.min.capacity() + c.large.min.capacity()) * 4 * qint64(sizeof(double));
	total += segments_.capacity() * qint64(sizeof(Segment));
	for (const Epoch &e : epochs_) total += e.marks.capacity() * qint64(sizeof(Mark));
	return total;
}

double Store::bytesPerRecord() const {
	return (spans_.isEmpty() ? recordSize_ : 0) + channels() * 4.0 * sizeof(double) * (1.0 / SMALL + 1.0 / LARGE);
}

/* ------------------------------------------------------------------ reading */

const char *Store::recordAt(qsizetype i) const {
	if (spans_.isEmpty()) return pieces_[i / PIECE].constData() + (i % PIECE) * recordSize_;
	/* mapped: from the span of its chunk's first record on, a step or two (a block holds hundreds of records) */
	int k = spanOfChunk_[i / SMALL];
	while (spans_[k].begin + spans_[k].count <= i) k++;
	return spans_[k].data + (i - spans_[k].begin) * recordSize_;
}

double Store::decode(const Channel &c, const char *record) const {
	const auto *b = reinterpret_cast<const uint8_t *>(record + c.offset);
	double raw = 0;
	switch (c.type) {
	case RegType::U8: raw = b[0]; break;
	case RegType::I8: raw = int8_t(b[0]); break;
	case RegType::U16: raw = le16(b); break;
	case RegType::I16: raw = int16_t(le16(b)); break;
	case RegType::U32: raw = le32(b); break;
	case RegType::I32: raw = int32_t(le32(b)); break;
	case RegType::F32: {
		const uint32_t bits = le32(b);
		float f;
		std::memcpy(&f, &bits, 4);
		raw = f;
		break;
	}
	case RegType::Bytes: return std::numeric_limits<double>::quiet_NaN();
	}
	return raw * c.scale + c.offset0;
}

double Store::value(int channel, qsizetype i) const { return decode(channels_[channel], recordAt(i)); }

int Store::segmentOf(qint64 absolute) const {
	const auto it = std::upper_bound(segments_.begin(), segments_.end(), absolute,
			[](qint64 a, const Segment &s) { return a < s.begin; });
	return std::max(0, int(it - segments_.begin()) - 1);
}

double Store::timeOfRecord(int epoch, double record) const {
	const Epoch &e = epochs_[epoch - epochBase_];
	if (e.marks.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
	const auto it = std::upper_bound(e.marks.begin(), e.marks.end(), record,
			[](double r, const Mark &m) { return r < double(m.record); });
	const Mark &m = it == e.marks.begin() ? e.marks.first() : *(it - 1);
	return m.time + e.shift + (record - double(m.record)) * m.period;
}

double Store::timeAt(qsizetype i) const {
	const qint64 a = dropped_ + i;
	const Segment &s = segments_[segmentOf(a)];
	return timeOfRecord(s.epoch, double(s.record) + double(a - s.begin));
}

qsizetype Store::segmentEnd(qsizetype i) const {
	const int k = segmentOf(dropped_ + i);
	return k + 1 < segments_.size() ? qsizetype(segments_[k + 1].begin - dropped_) : size_;
}

bool Store::startsAfterGap(qsizetype i) const {
	const qint64 a = dropped_ + i;
	const int k = segmentOf(a);
	return k > 0 && segments_[k].begin == a && a > dropped_;
}

qint64 Store::lostBefore(qsizetype i) const {
	if (!startsAfterGap(i)) return 0;
	const int k = segmentOf(dropped_ + i);
	const Segment &before = segments_[k - 1], &now = segments_[k];
	if (now.epoch != before.epoch) return -1;
	return qint64(now.record - before.record) - (now.begin - before.begin);
}

/* the first record whose time is at or after t (strict: after it): the segment by its last record's time, then in it
 * the record from the marks, made exact by a step or two */
qsizetype Store::bound(double t, bool strict) const {
	if (size_ == 0 || !hasTime()) return size_;
	const auto past = [&](double time) { return strict ? time > t : time >= t; };
	int lo = segmentOf(dropped_), hi = int(segments_.size()) - 1;
	const auto lastOf = [&](int k) {
		return (k + 1 < segments_.size() ? qsizetype(segments_[k + 1].begin - dropped_) : size_) - 1;
	};
	if (!past(timeAt(lastOf(hi)))) return size_;
	while (lo < hi) { /* the first segment whose last record is past t */
		const int mid = (lo + hi) / 2;
		if (past(timeAt(lastOf(mid)))) hi = mid;
		else lo = mid + 1;
	}
	const Segment &s = segments_[lo];
	const qsizetype first = std::max<qsizetype>(0, qsizetype(s.begin - dropped_)), last = lastOf(lo);
	if (past(timeAt(first))) return first;
	/* the mark at or before t, the record from it */
	const Epoch &e = epochs_[s.epoch - epochBase_];
	const auto it = std::upper_bound(e.marks.begin(), e.marks.end(), t,
			[&](double time, const Mark &m) { return time < m.time + e.shift; });
	const Mark &m = it == e.marks.begin() ? e.marks.first() : *(it - 1);
	const double record = double(m.record) + (t - m.time - e.shift) / m.period;
	qsizetype i = qsizetype(std::ceil(record - double(s.record))) + qsizetype(s.begin - dropped_);
	i = std::clamp(i, first, last);
	while (i > first && past(timeAt(i - 1))) i--;
	while (i < last && !past(timeAt(i))) i++;
	return i;
}

qsizetype Store::lowerBound(double t) const { return bound(t, false); }
qsizetype Store::upperBound(double t) const { return bound(t, true); }

/* ---------------------------------------------------------------- summaries */

void Store::summarize() {
	const qint64 end = dropped_ + size_;
	for (Channel &c : channels_) {
		Summary &small = c.small, &large = c.large;
		if (small.min.isEmpty() && small.first == 0) small.first = dropped_ / SMALL;
		if (large.min.isEmpty() && large.first == 0) large.first = dropped_ / LARGE;
		for (qint64 j = small.first + small.min.size(); (j + 1) * SMALL <= end; j++) {
			double lo = std::numeric_limits<double>::infinity(), hi = -lo, sum = 0, squares = 0;
			for (qint64 a = j * SMALL; a < (j + 1) * SMALL; a++) {
				const double v = decode(c, recordAt(qsizetype(a - dropped_)));
				lo = std::min(lo, v);
				hi = std::max(hi, v);
				sum += v;
				squares += v * v;
			}
			small.min << lo;
			small.max << hi;
			small.sum << sum;
			small.squares << squares;
		}
		constexpr qint64 STEP = LARGE / SMALL;
		for (qint64 j = large.first + large.min.size(); (j + 1) * LARGE <= end; j++) {
			const qsizetype k0 = qsizetype(j * STEP - small.first);
			double lo = small.min[k0], hi = small.max[k0], sum = 0, squares = 0;
			for (qsizetype k = k0; k < k0 + STEP; k++) {
				lo = std::min(lo, small.min[k]);
				hi = std::max(hi, small.max[k]);
				sum += small.sum[k];
				squares += small.squares[k];
			}
			large.min << lo;
			large.max << hi;
			large.sum << sum;
			large.squares << squares;
		}
	}
}

/* the records i0 .. i1 - 1 in the largest summaries that lie wholly in them, single records at the ends */
template <typename Take, typename TakeOne>
static void walk(qint64 a0, qint64 a1, qint64 smallFirst, qsizetype smallCount, qint64 largeFirst, qsizetype largeCount,
		Take take, TakeOne one) {
	for (qint64 a = a0; a < a1;) {
		const qint64 l = a / Store::LARGE, s = a / Store::SMALL;
		if (a % Store::LARGE == 0 && a + Store::LARGE <= a1 && l >= largeFirst && l < largeFirst + largeCount) {
			take(true, qsizetype(l - largeFirst));
			a += Store::LARGE;
		} else if (a % Store::SMALL == 0 && a + Store::SMALL <= a1 && s >= smallFirst && s < smallFirst + smallCount) {
			take(false, qsizetype(s - smallFirst));
			a += Store::SMALL;
		} else {
			one(a);
			a++;
		}
	}
}

void Store::minMax(int channel, qsizetype i0, qsizetype i1, double &lo, double &hi) const {
	lo = std::numeric_limits<double>::infinity();
	hi = -lo;
	const Channel &c = channels_[channel];
	walk(dropped_ + i0, dropped_ + i1, c.small.first, c.small.min.size(), c.large.first, c.large.min.size(),
			[&](bool large, qsizetype k) {
				const Summary &s = large ? c.large : c.small;
				lo = std::min(lo, s.min[k]);
				hi = std::max(hi, s.max[k]);
			},
			[&](qint64 a) {
				const double v = decode(c, recordAt(qsizetype(a - dropped_)));
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			});
}

void Store::sums(int channel, qsizetype i0, qsizetype i1, double &sum, double &sumSquares) const {
	sum = sumSquares = 0;
	const Channel &c = channels_[channel];
	walk(dropped_ + i0, dropped_ + i1, c.small.first, c.small.min.size(), c.large.first, c.large.min.size(),
			[&](bool large, qsizetype k) {
				const Summary &s = large ? c.large : c.small;
				sum += s.sum[k];
				sumSquares += s.squares[k];
			},
			[&](qint64 a) {
				const double v = decode(c, recordAt(qsizetype(a - dropped_)));
				sum += v;
				sumSquares += v * v;
			});
}

} // namespace fast

/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's data: see chart_data.h. Moved from chart_widget.cpp as it was. */
#include "ui/chart_data.h"

#include <algorithm>
#include <limits>

namespace {

/* the smallest positive value a bin is known to hold (its min, else its first, last or max); +inf: none. A bin of
 * chunks whose min is <= 0 may hold a smaller positive one: close enough for the Log scale's Auto range */
double smallestPositive(double min, double first, double last, double max) {
	if (min > 0) return min;
	double out = std::numeric_limits<double>::infinity();
	for (const double v : { first, last, max })
		if (v > 0) out = std::min(out, v);
	return out;
}

} // namespace

bool ChartData::latestInView(const Series &s, double t0, double t1, double &value) {
	if (s.fast) {
		const qsizetype k = s.fast->upperBound(t1) - 1;
		if (k < s.fast->recordsFrom() || s.fast->timeAt(k) < t0) return false;
		value = s.fast->value(s.channel, k);
		return true;
	}
	const qsizetype k = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin() - 1;
	if (k < 0 || s.times[k] < t0) return false;
	value = s.values[k];
	return true;
}

QString ChartData::number(double v) {
	const double a = std::fabs(v);
	if (a == 0) return QStringLiteral("0");
	if (a >= 1e5 || a < 1e-3) return QString::number(v, 'g', 4);
	return QString::number(v, 'f', a >= 100 ? 0 : a >= 10 ? 1 : a >= 1 ? 2 : 3);
}

/* the value at t, straight between the samples around it; NaN outside the samples */
double ChartData::valueAt(const QVector<double> &times, const QVector<double> &values, double t) {
	if (!std::isfinite(t) || t < times.front() || t > times.back()) return NAN;
	const qsizetype k = std::lower_bound(times.begin(), times.end(), t) - times.begin();
	if (k == 0) return values[0];
	const double ta = times[k - 1], tb = times[k];
	return tb > ta ? values[k - 1] + (values[k] - values[k - 1]) * (t - ta) / (tb - ta) : values[k];
}

void ChartData::Bin::add(double ta, double tb, double firstValue, double lastValue, double lo, double hi, int samples) {
	if (count == 0) {
		t0 = ta;
		first = firstValue;
		min = lo;
		max = hi;
	} else {
		min = std::min(min, lo);
		max = std::max(max, hi);
	}
	t1 = tb;
	last = lastValue;
	count += samples;
}

/* the chunk of each level the newest sample completes: the smallest from its
 * samples, each larger one from the LEVEL_STEP chunks below it */
void ChartData::addChunks(Series &s, qsizetype limit) {
	const qsizetype n = s.times.size();
	for (int level = 0; level < LEVELS && n % CHUNK_SIZE[level] == 0; level++) {
		Chunk chunk;
		if (level == 0) {
			const qsizetype begin = n - CHUNK_SIZE[0];
			chunk = { s.times[begin], s.times[n - 1], s.values[begin], s.values[begin], s.values[begin], s.values[n - 1] };
			for (qsizetype i = begin + 1; i < n; i++) {
				chunk.min = std::min(chunk.min, s.values[i]);
				chunk.max = std::max(chunk.max, s.values[i]);
			}
		} else {
			const QVector<Chunk> &below = s.chunks[level - 1];
			const qsizetype begin = below.size() - LEVEL_STEP;
			chunk = below[begin];
			for (qsizetype k = begin + 1; k < below.size(); k++) {
				chunk.min = std::min(chunk.min, below[k].min);
				chunk.max = std::max(chunk.max, below[k].max);
			}
			chunk.t1 = below.last().t1;
			chunk.last = below.last().last;
		}
		roomForOne(s.chunks[level], limit / CHUNK_SIZE[level] + 1, s.spread);
		s.chunks[level].push_back(chunk);
	}
}

/* the trapezoids of a fast line's records i0 .. i1 - 1, each segment apart (nothing across a gap): a segment's
 * records are evenly spaced, so its trapezoids are dt x (sum - (first + last) / 2), the sums from the summaries.
 * shifted: the same of (value - shift), for the standard deviation (statsOf's reason) */
ChartData::Trapezoids ChartData::trapezoids(const fast::Store &store, int channel, qsizetype i0, qsizetype i1, double shift) {
	Trapezoids out;
	for (qsizetype k = i0; k < i1;) {
		const qsizetype e = std::min(store.segmentEnd(k), i1);
		const qsizetype n = e - k;
		if (n >= 2) {
			double sum, squares;
			store.sums(channel, k, e, sum, squares);
			const double f = store.value(channel, k), l = store.value(channel, e - 1);
			const double duration = store.timeAt(e - 1) - store.timeAt(k), dt = duration / double(n - 1);
			const double sumShifted = sum - double(n) * shift;
			const double squaresShifted = squares - 2 * shift * sum + double(n) * shift * shift;
			const double fs = f - shift, ls = l - shift;
			out.area += dt * (sum - 0.5 * (f + l));
			out.areaOfSquares += dt * (squares - 0.5 * (f * f + l * l));
			out.shifted += dt * (sumShifted - 0.5 * (fs + ls));
			out.shiftedSquares += dt * (squaresShifted - 0.5 * (fs * fs + ls * ls));
			out.span += duration;
		}
		k = e;
	}
	return out;
}

ChartData::Stats ChartData::statsOf(const QVector<double> &times, const QVector<double> &values, double t0, double t1,
		double a, double b) {
	Stats result;
	result.atA = valueAt(times, values, a);
	result.atB = valueAt(times, values, b);
	const qsizetype i0 = std::lower_bound(times.begin(), times.end(), t0) - times.begin();
	const qsizetype i1 = std::upper_bound(times.begin(), times.end(), t1) - times.begin();
	if (i1 - i0 < 1) return result;
	result.min = result.max = values[i0];
	/* the standard deviation from sums shifted by the first value: a 12 V line with 1 mV of ripple squared whole loses
	 * the ripple to the 144 V^2 (rms^2 - mean^2 cancels to the rounding) */
	const double shift = values[i0];
	double area = 0, areaOfSquares = 0, span = 0, shifted = 0, shiftedSquares = 0;
	for (qsizetype i = i0; i < i1; i++) {
		result.min = std::min(result.min, values[i]);
		result.max = std::max(result.max, values[i]);
		if (i == i0) continue;
		const double dt = times[i] - times[i - 1];
		area += 0.5 * (values[i] + values[i - 1]) * dt;
		areaOfSquares += 0.5 * (values[i] * values[i] + values[i - 1] * values[i - 1]) * dt;
		const double d1 = values[i] - shift, d0 = values[i - 1] - shift;
		shifted += 0.5 * (d1 + d0) * dt;
		shiftedSquares += 0.5 * (d1 * d1 + d0 * d0) * dt;
		span += dt;
	}
	result.n = int(i1 - i0);
	result.integral = area;
	result.p2p = result.max - result.min;
	if (span > 0) {
		result.mean = area / span;
		result.rms = std::sqrt(std::max(0.0, areaOfSquares / span));
		const double shiftedMean = shifted / span;
		result.std = std::sqrt(std::max(0.0, shiftedSquares / span - shiftedMean * shiftedMean));
	} else {
		result.mean = result.rms = values[i0];
	}
	result.ok = true;
	return result;
}

double ChartData::fastValueAt(const fast::Store &store, int channel, double t) {
	if (!std::isfinite(t) || store.size() == 0 || !store.hasTime()) return NAN;
	const qsizetype k = store.lowerBound(t);
	if (k >= store.size()) return NAN;
	const double tb = store.timeAt(k);
	if (tb == t) return store.value(channel, k);
	if (k == 0 || store.startsAfterGap(k)) return NAN; /* before the first, or in a gap: not measured */
	const double ta = store.timeAt(k - 1), va = store.value(channel, k - 1), vb = store.value(channel, k);
	return tb > ta ? va + (vb - va) * (t - ta) / (tb - ta) : vb;
}

/* the same as statsOf, from the summaries: what it costs follows the segments and the summaries' chunks, not the
 * records (an hour at a million a second within a frame) */
ChartData::Stats ChartData::statsOfFast(const fast::Store &store, int channel, double t0, double t1, double a, double b) {
	Stats result;
	result.atA = fastValueAt(store, channel, a);
	result.atB = fastValueAt(store, channel, b);
	if (store.size() == 0 || !store.hasTime()) return result;
	const qsizetype i0 = store.lowerBound(t0), i1 = store.upperBound(t1);
	if (i1 - i0 < 1) return result;
	store.minMax(channel, i0, i1, result.min, result.max);
	result.n = int(std::min<qsizetype>(i1 - i0, std::numeric_limits<int>::max()));
	if (i0 < store.recordsFrom()) { /* summaries only in it: their min and max; a mean, an area need every record */
		result.p2p = result.max - result.min;
		result.mean = result.rms = result.std = result.integral = NAN;
		result.summaries = true;
		result.ok = true;
		return result;
	}
	const double shift = store.value(channel, i0);
	const Trapezoids sums = trapezoids(store, channel, i0, i1, shift);
	result.integral = sums.area;
	result.p2p = result.max - result.min;
	if (sums.span > 0) {
		result.mean = sums.area / sums.span;
		result.rms = std::sqrt(std::max(0.0, sums.areaOfSquares / sums.span));
		const double shiftedMean = sums.shifted / sums.span;
		result.std = std::sqrt(std::max(0.0, sums.shiftedSquares / sums.span - shiftedMean * shiftedMean));
	} else {
		result.mean = result.rms = shift;
	}
	result.ok = true;
	return result;
}

/* A line's samples from t0 to t1, one bin per pixel column on absolute time,
 * with the min / max of what lies inside [t0, t1]. With many samples per column
 * it bins min/max chunks instead, of the largest level that is at most half a
 * column (a whole one on the overview), smaller ones up to where those start,
 * so the cost follows the pixels, not the samples: a long view with millions
 * of samples costs what a short one does. */
void ChartData::binSeries(const Series &s, double t0, double t1, double columns, bool overview,
		BinnedLine &out, std::atomic<qint64> &fastBinned) {
	if (s.fast) { /* the memory strip: binned whole (a strip of 60 px) */
		binFast(s, t0, t1, columns, out, nullptr, fastBinned);
		return;
	}
	out.series = &s;
	out.bins.clear();
	out.lo = std::numeric_limits<double>::max();
	out.hi = -out.lo;
	out.posLo = std::numeric_limits<double>::infinity();
	if (s.times.isEmpty() || t1 <= t0) return;
	const double columnSeconds = (t1 - t0) / columns;
	/* from one sample before to one after, so the line enters and leaves at the edges */
	qsizetype i0 = std::lower_bound(s.times.begin(), s.times.end(), t0) - s.times.begin();
	qsizetype i1 = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin();
	i0 = std::max<qsizetype>(0, i0 - 1);
	i1 = std::min<qsizetype>(s.times.size(), i1 + 1);
	if (i1 <= i0) return;
	out.bins.reserve(int(std::min<qsizetype>(i1 - i0, qsizetype(columns) + 4)));
	const double perColumn = double(i1 - i0) / columns, fit = overview ? 1.0 : 2.0;
	int top = LEVELS - 1; /* the largest level that fits a column */
	while (top >= 0 && perColumn <= fit * CHUNK_SIZE[top]) top--;
	binRange(s, i0, i1, columnSeconds, top, out.bins);
	for (const Bin &bin : std::as_const(out.bins)) {
		if (bin.t1 < t0 || bin.t0 > t1) continue; /* the range of what lies inside the span */
		out.lo = std::min(out.lo, bin.min);
		out.hi = std::max(out.hi, bin.max);
		out.posLo = std::min(out.posLo, smallestPositive(bin.min, bin.first, bin.last, bin.max));
	}
	rangeAtEdges(out, t0, t1);
}

void ChartData::rangeAtEdges(BinnedLine &out, double t0, double t1) {
	const auto take = [&out](double v) {
		out.lo = std::min(out.lo, v);
		out.hi = std::max(out.hi, v);
		if (v > 0) out.posLo = std::min(out.posLo, v);
	};
	/* the line's value at t between bin a's end and bin b's start */
	const auto at = [](const Bin &a, const Bin &b, double t) {
		if (b.t0 <= a.t1) return b.first;
		return a.last + (b.first - a.last) * (t - a.t1) / (b.t0 - a.t1);
	};
	const QVector<Bin> &bins = out.bins;
	for (qsizetype k = 0; k + 1 < bins.size(); k++) {
		const Bin &a = bins[k], &b = bins[k + 1];
		if (b.gap) continue;
		if (a.t1 < t0 && b.t0 > t0) take(at(a, b, t0));
		if (a.t1 < t1 && b.t0 > t1) take(at(a, b, t1));
	}
}

void ChartData::binRange(const Series &s, qsizetype i0, qsizetype i1, double columnSeconds, int top, QVector<Bin> &bins) {
	qsizetype current = i0; /* the sample the piece put starts with */
	auto put = [&](double ta, double tb, double first, double last, double lo, double hi, int samples) {
		const qint64 column = qint64(std::floor(ta / columnSeconds));
		if (bins.isEmpty() || bins.back().column != column) {
			Bin bin;
			bin.column = column;
			bin.firstSample = s.dropped + current;
			bins.push_back(bin);
		}
		bins.back().add(ta, tb, first, last, lo, hi, samples);
	};
	/* a chunk that starts here, ends inside the samples asked for, and lies in one column (so a column's bin is its
	 * own samples, whatever sample binning began at: the kept bins and the ones binned at once agree) */
	auto fits = [&](int level, qsizetype i) {
		const int n = CHUNK_SIZE[level];
		if (i % n || i + n > i1 || i / n >= s.chunks[level].size()) return false;
		const Chunk &chunk = s.chunks[level][i / n];
		return std::floor(chunk.t0 / columnSeconds) == std::floor(chunk.t1 / columnSeconds);
	};
	for (qsizetype i = i0; i < i1;) {
		current = i;
		int level = top; /* the largest that fits */
		while (level >= 0 && !fits(level, i)) level--;
		if (level < 0) {
			put(s.times[i], s.times[i], s.values[i], s.values[i], s.values[i], s.values[i], 1);
			i++;
			continue;
		}
		const Chunk &chunk = s.chunks[level][i / CHUNK_SIZE[level]];
		put(chunk.t0, chunk.t1, chunk.first, chunk.last, chunk.min, chunk.max, CHUNK_SIZE[level]);
		i += CHUNK_SIZE[level];
	}
}

/* The view's bins, the complete columns kept from the frame before: only the samples after them are binned (the
 * open last column again), so a live view costs the new samples, not the whole window. Binned from the view's start
 * when they do not hold: another column width (window, size), the lines changed, or the view starts before them
 * (held, dragged back). Chunks start on fixed sample numbers, so the bins come out as binned at once, give or take
 * where a chunk at a column's edge falls (less than half a column). */
void ChartData::binViewSeries(const Series &s, double t0, double t1, double columns, BinnedLine &out,
		const BinnedLine *previous, quint64 generation, std::atomic<qint64> &polledBinned,
		std::atomic<qint64> &fastBinned) {
	if (s.fast) { /* from its summaries: what a column costs does not grow with its records */
		binFast(s, t0, t1, columns, out, previous, fastBinned);
		return;
	}
	out.series = &s;
	out.bins.clear();
	out.lo = std::numeric_limits<double>::max();
	out.hi = -out.lo;
	out.posLo = std::numeric_limits<double>::infinity();
	Series::ViewBins &kept = s.viewBins;
	if (s.times.isEmpty() || t1 <= t0) {
		kept = {};
		return;
	}
	const double columnSeconds = (t1 - t0) / columns;
	qsizetype i0 = std::lower_bound(s.times.begin(), s.times.end(), t0) - s.times.begin();
	qsizetype i1 = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin();
	i0 = std::max<qsizetype>(0, i0 - 1);
	i1 = std::min<qsizetype>(s.times.size(), i1 + 1);
	if (i1 <= i0) return;
	const qint64 firstColumn = qint64(std::floor(s.times[i0] / columnSeconds));
	/* drop what is left of the view; the rest holds when it starts at or before the view and is of these columns */
	while (!kept.bins.isEmpty() && kept.bins.first().column < firstColumn) kept.bins.removeFirst();
	const bool holds = kept.columnSeconds == columnSeconds && kept.generation == generation
			&& kept.binnedTo >= s.dropped + i0 && kept.binnedTo <= s.dropped + i1
			&& (kept.bins.isEmpty() ? kept.binnedTo == s.dropped + i0 : kept.bins.first().column <= firstColumn);
	if (!holds) {
		kept = {};
		kept.columnSeconds = columnSeconds;
		kept.generation = generation;
		kept.binnedTo = s.dropped + i0;
	}
	const double perColumn = double(i1 - i0) / columns;
	int top = LEVELS - 1; /* the largest level that fits half a column, as binSeries */
	while (top >= 0 && perColumn <= 2.0 * CHUNK_SIZE[top]) top--;
	QVector<Bin> fresh;
	binRange(s, kept.binnedTo - s.dropped, i1, columnSeconds, top, fresh);
	polledBinned += fresh.size();
	out.bins.reserve(kept.bins.size() + fresh.size());
	out.bins = kept.bins;
	out.bins += fresh;
	/* the complete columns are kept: all but the last, which may still fill */
	if (fresh.size() > 1) {
		kept.bins.append(fresh.first(fresh.size() - 1));
		kept.binnedTo = fresh.last().firstSample;
	}
	for (const Bin &bin : std::as_const(out.bins)) {
		if (bin.t1 < t0 || bin.t0 > t1) continue; /* the range of what lies inside the span */
		out.lo = std::min(out.lo, bin.min);
		out.hi = std::max(out.hi, bin.max);
		out.posLo = std::min(out.posLo, smallestPositive(bin.min, bin.first, bin.last, bin.max));
	}
	rangeAtEdges(out, t0, t1);
}

/* A fast line's bins: for each column on absolute time its records, found by their times (the store inverts its
 * clock), their first, last, min and max (the min and max from the summaries: the work follows the columns, not the
 * records, and a spike of one record is in every column's max that holds it). A column where few records lie keeps
 * each at its own time (toPolyline: one or two in a bin). A gap ends a column's bin early, and the bin after it says
 * so: the line is not drawn across. */
/* A column is a whole number of columnSeconds from time 0, so the last frame's bins of the columns this frame
 * shares are the same bins: a live view, which moves by a column or two a frame, keeps them and bins only the
 * columns at its ends (the first and the last bin of a frame may be part of a column: never kept). Kept bins hold
 * their records' numbers, so a trim that dropped them, a clear, a new start or a shift of a start (the store's
 * timeVersion) and another column width start afresh. */
void ChartData::binFast(const Series &s, double t0, double t1, double columns, BinnedLine &out, const BinnedLine *previous,
		std::atomic<qint64> &fastBinned) {
	out.series = &s;
	out.bins.clear();
	out.lo = std::numeric_limits<double>::max();
	out.hi = -out.lo;
	out.posLo = std::numeric_limits<double>::infinity();
	const fast::Store &store = *s.fast;
	out.columnSeconds = (t1 - t0) / columns;
	out.timeVersion = store.timeVersion();
	out.wholeFrom = store.dropped() + store.recordsFrom();
	if (store.size() == 0 || !store.hasTime() || t1 <= t0) return;
	const double columnSeconds = out.columnSeconds;
	const qsizetype i0 = std::max<qsizetype>(0, store.lowerBound(t0) - 1);
	const qsizetype i1 = std::min(store.size(), store.upperBound(t1) + 1);
	if (i1 <= i0) return;
	out.bins.reserve(int(std::min<qsizetype>(i1 - i0, qsizetype(columns) * 2 + 4)));
	qint64 binned = 0;
	/* the records from i to end into bins, as the loop always did */
	auto binRecords = [&](qsizetype i, qsizetype end) {
		while (i < end) {
			const double ti = store.timeAt(i);
			const qint64 column = qint64(std::floor(ti / columnSeconds));
			qsizetype j = std::min(store.segmentEnd(i), end);
			j = std::min(j, store.lowerBound(double(column + 1) * columnSeconds));
			if (j <= i) j = i + 1;
			Bin bin;
			bin.column = column;
			bin.count = int(std::min<qsizetype>(j - i, std::numeric_limits<int>::max()));
			bin.firstSample = qsizetype(store.dropped()) + i;
			bin.t0 = ti;
			bin.t1 = j - 1 == i ? ti : store.timeAt(j - 1);
			bin.first = store.value(s.channel, i);
			bin.last = store.value(s.channel, j - 1);
			store.minMax(s.channel, i, j, bin.min, bin.max);
			bin.gap = i > i0 && store.startsAfterGap(i);
			out.bins.push_back(bin);
			binned++;
			i = j;
		}
	};
	/* Records kept as summaries only (before whole): a summary of SMALL records at a time, in the column of its first
	 * record (its min and max are the records' own, so a column of SMALL records or more is binned as from them). A
	 * column it holds no first record of, zoomed in past one a column, gets it again as a bar: the summary drawn over
	 * every column it covers, up to the next one's */
	const qsizetype whole = store.recordsFrom();
	auto binSummaries = [&](qsizetype i, qsizetype end) {
		constexpr qsizetype SMALL = fast::Store::SMALL;
		i -= i % SMALL; /* to its summary's first (dropped() is a whole number of them) */
		const qint64 firstShown = qint64(std::floor(t0 / columnSeconds)) - 1;
		const qint64 lastShown = qint64(std::floor(t1 / columnSeconds)) + 1;
		qsizetype before = -1; /* the bin before's first record */
		while (i < end) {
			const double ti = store.timeAt(i);
			const qint64 column = qint64(std::floor(ti / columnSeconds));
			qsizetype j = store.lowerBound(double(column + 1) * columnSeconds);
			j = std::min(end, (j + SMALL - 1) / SMALL * SMALL);
			if (j <= i) j = std::min(end, i + SMALL);
			Bin bin;
			bin.column = column;
			bin.count = int(std::min<qsizetype>(j - i, std::numeric_limits<int>::max()));
			bin.firstSample = qsizetype(store.dropped()) + i;
			bin.t0 = ti;
			bin.t1 = store.timeAt(j - 1);
			store.minMax(s.channel, i, j, bin.min, bin.max);
			bin.first = bin.last = (bin.min + bin.max) / 2; /* their values are gone: the line runs through the middle */
			/* a segment begun since the bin before (a gap inside a summary breaks the line at the next bin) */
			bin.gap = i > i0 && (before >= 0 ? store.segmentEnd(before) <= i : store.startsAfterGap(i));
			out.bins.push_back(bin);
			binned++;
			before = i;
			const qint64 next = j < store.size() ? qint64(std::floor(store.timeAt(j) / columnSeconds)) : column + 1;
			const qint64 barTo = std::min({ next - 1, qint64(std::floor(bin.t1 / columnSeconds)), lastShown });
			if (barTo > column) {
				Bin bar;
				store.minMax(s.channel, j - 1, j, bar.min, bar.max); /* the last summary's: the one that goes on */
				bar.first = bar.last = (bar.min + bar.max) / 2;
				bar.firstSample = qsizetype(store.dropped()) + j;
				bar.bar = true;
				for (qint64 k = std::max(column + 1, firstShown); k <= barTo; k++) {
					bar.column = k;
					bar.t0 = bar.t1 = (double(k) + 0.5) * columnSeconds;
					out.bins.push_back(bar);
				}
			}
			i = j;
		}
	};
	/* records from i to end, those before whole as summaries */
	auto binSpan = [&](qsizetype i, qsizetype end) {
		if (i < whole) binSummaries(i, std::min(end, whole));
		binRecords(std::max(i, whole), end);
	};
	/* the kept bins: whole columns of the last frame (not its first or last bin) inside this frame's columns, with
	 * their records still kept, made with the same column width over the same times; those of summaries only while
	 * the records kept whole began where they did */
	qsizetype keep0 = -1, keep1 = -1; /* previous->bins[keep0 .. keep1) */
	if (previous && previous->series == &s && previous->columnSeconds == columnSeconds
			&& previous->timeVersion == store.timeVersion() && previous->bins.size() >= 3) {
		const qint64 c0 = qint64(std::floor(t0 / columnSeconds)), c1 = qint64(std::floor(t1 / columnSeconds));
		const qint64 firstColumn = previous->bins.first().column, lastColumn = previous->bins.last().column;
		for (qsizetype k = 1; k + 1 < previous->bins.size(); k++) {
			const Bin &b = previous->bins[k];
			const qsizetype from = qsizetype(b.firstSample - store.dropped()); /* its records, as indexes now */
			const bool inside = b.column > firstColumn && b.column < lastColumn && b.column >= c0 && b.column <= c1
					&& from >= i0 && from + b.count <= i1 /* inside what this frame bins: its ends stay partial bins */
					&& (from >= whole || previous->wholeFrom == out.wholeFrom);
			if (inside && keep0 < 0) keep0 = k;
			if (inside) keep1 = k + 1;
			if (!inside && keep0 >= 0) break; /* one run: the kept bins stay contiguous in records */
		}
	}
	if (keep0 < 0) {
		binSpan(i0, i1);
	} else {
		const Bin &firstKept = previous->bins[keep0], &lastKept = previous->bins[keep1 - 1];
		const qsizetype keptFrom = qsizetype(firstKept.firstSample - store.dropped());
		const qsizetype keptTo = qsizetype(lastKept.firstSample - store.dropped()) + lastKept.count;
		binSpan(i0, std::min(keptFrom, i1));              /* before the kept columns */
		for (qsizetype k = keep0; k < keep1; k++) out.bins.push_back(previous->bins[k]);
		binSpan(std::max(keptTo, i0), i1);                /* the newest columns */
	}
	fastBinned += binned;
	for (const Bin &bin : out.bins) {
		if (bin.t1 >= t0 && bin.t0 <= t1) { /* the range of what lies inside the span */
			out.lo = std::min(out.lo, bin.min);
			out.hi = std::max(out.hi, bin.max);
			out.posLo = std::min(out.posLo, smallestPositive(bin.min, bin.first, bin.last, bin.max));
		}
	}
	rangeAtEdges(out, t0, t1);
}

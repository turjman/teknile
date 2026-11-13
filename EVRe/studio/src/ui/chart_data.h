/* SPDX-License-Identifier: Apache-2.0 */
/* The chart's data (STUDIO.md 23): a line's samples with their min/max chunks, the bins a view draws from them, the
 * axes that map a time span and a value range onto pixels, and the measurements. Shared by the desktop's chart
 * (ChartView, chart_widget.h, which keeps these names by `using`) and the phone's (ChartItem, phone/chart_item.h), so
 * both bin and measure a line alike; nothing here knows a widget or a window. Moved from chart_widget as it was
 * (android_plan Q-7): the functions that read the view (the lines' generation, the counts of columns binned) get it as
 * arguments. */
#pragma once

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <atomic>
#include <cmath>
#include <memory>

#include "model/fast_store.h"

struct ChartData {
	/* Samples per min/max chunk, four levels, each LEVEL_STEP chunks of the one
	 * below. Chunks of a level start at multiples of its size in a line's samples
	 * (the memory drops whole chunks of the largest to keep it so), and a span with
	 * many samples per pixel column bins the largest chunks that still fit it. */
	static constexpr int LEVELS = 4;
	static constexpr int LEVEL_STEP = 8;
	static constexpr int CHUNK_SIZE[LEVELS] = { 8, 64, 512, 4096 };

	struct Chunk {
		double t0, t1, min, max, first, last;
	};
	/* one pixel column's samples; columns are counted from time zero, so a sample
	 * stays in its column while the view scrolls */
	struct Bin {
		qint64 column = 0;
		int count = 0;
		qsizetype firstSample = 0; /* the line's sample it starts with, counted since the line began */
		double t0 = 0, t1 = 0, first = 0, last = 0, min = 0, max = 0;
		bool gap = false;          /* a fast line: records were lost (or the stream started again) before it */
		/* a fast line's records kept as summaries only, zoomed in past one a column: the summary of the column before
		 * drawn again over this one (no records of its own: count 0, its times the column's middle), so each summary
		 * is a bar over the columns it covers */
		bool bar = false;
		void add(double ta, double tb, double firstValue, double lastValue, double lo, double hi, int samples);
	};
	struct Series {
		QString name, unit;
		QColor color;
		QVector<double> times, values; /* the samples kept, times rising */
		QVector<Chunk> chunks[LEVELS]; /* one per full CHUNK_SIZE[level] of samples */
		bool hasLast = false;
		double last = 0;
		bool hasShown = false; /* the legend's value: last, taken at the values' pace */
		double shown = 0;
		/* the total since Clear, and the sample it was summed to (time NaN: none yet) */
		double total = 0, totalT = NAN, totalV = 0;
		qsizetype dropped = 0; /* the samples the memory let go: times[0] is sample `dropped` of the line */
		int spread = 0;        /* 0 to 7, by the order the lines came: its arrays' growth step (roomForOne) */
		/* the view's bins kept from frame to frame: a column on absolute time that is complete never changes, so a
		 * frame bins only the samples after `binnedTo` (the open last column again). Its window's column width
		 * and the lines' generation say whether it still holds. */
		struct ViewBins {
			double columnSeconds = 0;
			quint64 generation = 0;
			QVector<Bin> bins;
			qsizetype binnedTo = 0; /* counted since the line began */
		};
		mutable ViewBins viewBins;
		/* a fast line: its stream's records, and its channel there (times and values stay empty) */
		std::shared_ptr<fast::Store> fast;
		int channel = 0;
		qint64 totalTo = 0; /* a fast line: its total sums its records up to this one (counted since its store began) */
	};
	/* one line's samples in a span, binned per pixel column, with the range of
	 * what lies inside the span */
	struct BinnedLine {
		const Series *series = nullptr;
		QVector<Bin> bins;
		double lo = 0, hi = 0;
		double posLo = 0; /* the smallest positive value of what lies inside the span (the Log scale); +inf: none */
		/* a fast line: what its bins were made with, so the next frame can keep the whole columns it shares */
		double columnSeconds = 0;
		int timeVersion = -1;
		qint64 wholeFrom = -1; /* the store's first record kept whole then, counted since it began (recordsFrom) */
	};
	/* a time span and a value range mapped onto a rectangle of pixels: the plot
	 * in one frame, or the memory strip */
	struct Axes {
		QRectF rect;
		double t0 = 0, t1 = 1, span = 1; /* the times shown, seconds; span = t1 - t0 */
		double lo = 0, hi = 1;           /* the values shown */
		bool log = false;                /* log10 between lo and hi (both > 0); a value <= 0 on the bottom edge */
		double logLo = 0, logHi = 1;
		double columns = 1;              /* pixel columns across, at least 1 */
		void setRange(double low, double high, bool logScale) {
			lo = low;
			hi = high;
			log = logScale && low > 0 && high > low;
			if (log) {
				logLo = std::log10(low);
				logHi = std::log10(high);
			}
		}
		double columnSeconds() const { return span / columns; }
		double x(double t) const { return rect.left() + (t - t0) / span * rect.width(); }
		double y(double v) const {
			if (!log) return rect.bottom() - (v - lo) / (hi - lo) * rect.height();
			return v > 0 ? rect.bottom() - (std::log10(v) - logLo) / (logHi - logLo) * rect.height() : rect.bottom();
		}
		double value(double yPixel) const { /* the value at a height: y's inverse */
			const double part = (rect.bottom() - yPixel) / rect.height();
			return log ? std::pow(10.0, logLo + part * (logHi - logLo)) : lo + part * (hi - lo);
		}
	};
	/* measurements of one line over A..B, or over the view */
	struct Stats {
		bool ok = false;
		/* integral: unit x s; std: the standard deviation, time-weighted as the mean; p2p: max - min */
		double atA = NAN, atB = NAN, min = 0, max = 0, mean = 0, rms = 0, std = 0, p2p = 0, integral = 0;
		double total = NAN; /* the area since Clear (totalsSince), unit x s; NaN: no sample yet */
		int n = 0;
		/* a fast line's records kept as summaries only in the range: min, max and p2p from them, the rest (and A or B
		 * there) need samples: NaN */
		bool summaries = false;
	};
	/* the samples */
	static void addChunks(Series &s, qsizetype limit);
	/* A line's arrays take what its share of the RAM needs, not up to twice it (2 GB for a RAM of 1 GB): they grow by
	 * about doubling only up to `most` elements, and what goes from the front moves the rest to the front. Qt's vectors
	 * keep the room freed at the front, and when the end comes with two thirds of them in use they double instead of
	 * moving. spread (0 to 7, the line's): it grows 2 + spread / 16 times, so lines that fill together grow at different
	 * moments (all at once, 64 lines at RAM 1 GB moved 0.5 GB in one frame at their last growth) */
	template <typename T>
	static void roomForOne(QVector<T> &v, qsizetype most, int spread) {
		if (v.size() < v.capacity()) return;
		const qsizetype grown = v.size() * (32 + spread) / 16;
		v.reserve(std::max(v.size() + 1, std::min(std::max<qsizetype>(grown, 1024), most)));
	}

	/* the binning */
	/* overview: a chunk may fill a whole column (the memory strip); otherwise at most half of one. fastBinned counts the
	 * columns of fast lines binned (binFast) */
	static void binSeries(const Series &s, double t0, double t1, double columns, bool overview, BinnedLine &out,
			std::atomic<qint64> &fastBinned);
	/* the same for the view, with the bins kept from the frame before (Series::viewBins); generation: the lines'
	 * generation (a line added or removed, the samples cleared), which the kept bins must be of; polledBinned counts
	 * the polled lines' columns binned, fastBinned the fast lines' */
	static void binViewSeries(const Series &s, double t0, double t1, double columns, BinnedLine &out,
			const BinnedLine *previous, quint64 generation, std::atomic<qint64> &polledBinned,
			std::atomic<qint64> &fastBinned);
	/* samples i0 .. i1 - 1 into bins, chunks of level `top` at most where they start and end inside and lie in one
	 * column */
	static void binRange(const Series &s, qsizetype i0, qsizetype i1, double columnSeconds, int top, QVector<Bin> &bins);
	/* a fast line from its store: a bin per column (two where a gap falls in one), each column's min and max from the
	 * summaries, so the cost follows the columns, not the records */
	static void binFast(const Series &s, double t0, double t1, double columns, BinnedLine &out, const BinnedLine *previous,
			std::atomic<qint64> &fastBinned);
	/* the line's range takes its values where it crosses the span's edges too (between the sample before an edge and
	 * the one after it, not across a gap): drawn, that piece of it is in view. Without, a line with one sample in a
	 * short view (a register polled at 100 Hz in 10 ms) was ranged on that sample alone, and Normalise drew the piece
	 * from the edge to it far off the plot: a jump at the view's end that is not in the data */
	static void rangeAtEdges(BinnedLine &out, double t0, double t1);

	/* the measurements */
	/* the value at t, straight between the samples around it; NaN outside the samples */
	static double valueAt(const QVector<double> &times, const QVector<double> &values, double t);
	/* the trapezoids of a fast line's records i0 .. i1 - 1, each segment apart (nothing across a gap): a segment's
	 * records are evenly spaced, so its trapezoids are dt x (sum - (first + last) / 2), the sums from the summaries.
	 * shifted: the same of (value - shift), for the standard deviation (statsOf's reason) */
	struct Trapezoids {
		double area = 0, areaOfSquares = 0, shifted = 0, shiftedSquares = 0, span = 0;
	};
	static Trapezoids trapezoids(const fast::Store &store, int channel, qsizetype i0, qsizetype i1, double shift);
	/* a line's measurements from its arrays, over t0..t1, its values at the times a and b */
	static Stats statsOf(const QVector<double> &times, const QVector<double> &values, double t0, double t1, double a,
			double b);
	/* a fast line over records i0 .. i1 - 1: min, max, and from the summaries the trapezoids of each segment (nothing
	 * across a gap), as statsOf from a polled line's arrays */
	static Stats statsOfFast(const fast::Store &store, int channel, double t0, double t1, double a, double b);
	static double fastValueAt(const fast::Store &store, int channel, double t); /* NaN: outside, or in a gap */

	/* the drawing */
	template <typename MapX, typename MapY>
	/* bands: given, a full column's min..max is a bar there (bandWidth wide) and the polyline only goes through its
	 * first and last value; not given (the memory strip), the polyline draws the whole stroke */
	/* breaks: given, where a bin says a gap (a fast line's), the polyline is broken: the index of the first point after
	 * each break; nothing is drawn across it */
	static QPolygonF toPolyline(const QVector<Bin> &bins, double columnSeconds, MapX x, MapY y,
			QVector<QRectF> *bands = nullptr, double bandWidth = 0, double pixel = 0, QVector<qsizetype> *breaks = nullptr);
};

/* A line's bins as a polyline. A column with one or two samples keeps them at
 * their exact times (smooth at slow polls); a fuller one becomes a vertical
 * stroke at the column's centre: its first value, the min and the max (the
 * max first when the line falls), then its last value. With `bands`, that
 * stroke is a plain bar the line's width instead, from the min to the max, and
 * the polyline goes through the first and the last value only: a column of a
 * fast, noisy line is a few pixels wide and as tall as its swing, and filling
 * it once costs a fraction of five antialiased strokes over it. A level run is
 * one segment: while the line stays at the same height its end moves on instead
 * of a point being added, the same pixels drawn (a register that does not
 * change costs two points, not four per column). A full column whose swing is
 * under `pixel` (one device pixel) is one point: a slow line costs one point a
 * column, not four. */
template <typename MapX, typename MapY>
QPolygonF ChartData::toPolyline(const QVector<Bin> &bins, double columnSeconds, MapX x, MapY y,
		QVector<QRectF> *bands, double bandWidth, double pixel, QVector<qsizetype> *breaks) {
	QPolygonF poly;
	poly.reserve(bins.size() * 4);
	qsizetype piece = 0; /* where the piece being drawn began: a level run or a point is never merged across a break */
	const auto put = [&poly, &piece](const QPointF &point) {
		const qsizetype n = poly.size();
		if (n - piece >= 2 && poly[n - 1].y() == point.y() && poly[n - 2].y() == point.y()) poly[n - 1] = point;
		else if (n == piece || poly[n - 1] != point) poly << point;
	};
	for (const Bin &b : bins) {
		if (b.gap && breaks && !poly.isEmpty()) { /* a fast line's gap: the next point begins a new piece */
			piece = poly.size();
			breaks->push_back(piece);
		}
		if (b.count <= 2 && !b.bar) {
			put(QPointF(x(b.t0), y(b.first)));
			if (b.count == 2) put(QPointF(x(b.t1), y(b.last)));
			continue;
		}
		const double cx = x((double(b.column) + 0.5) * columnSeconds);
		const double top = y(b.max), bottom = y(b.min);
		if (std::fabs(bottom - top) < pixel) { /* a stroke shorter than a pixel draws nothing more than its end */
			put(QPointF(cx, y(b.last)));
			continue;
		}
		if (bands && std::fabs(bottom - top) > bandWidth) {
			bands->push_back(QRectF(QPointF(cx - bandWidth / 2, std::min(top, bottom)),
					QPointF(cx + bandWidth / 2, std::max(top, bottom))));
			put(QPointF(cx, y(b.first)));
			put(QPointF(cx, y(b.last)));
			continue;
		}
		put(QPointF(cx, y(b.first)));
		put(QPointF(cx, y(b.first <= b.last ? b.min : b.max)));
		put(QPointF(cx, y(b.first <= b.last ? b.max : b.min)));
		put(QPointF(cx, y(b.last)));
	}
	return poly;
}

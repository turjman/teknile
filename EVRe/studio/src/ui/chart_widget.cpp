/* SPDX-License-Identifier: Apache-2.0 */
/* The chart: keeping the samples, the view and the mouse, the measurements, and
 * the drawing (see chart_widget.h for what it does and why it is drawn this way). */
#include "ui/chart_widget.h"

#include <QBackingStore>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QDateTime>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QThread>
#include <QToolTip>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* the layout around the plot, in logical pixels */
constexpr double AXIS_W = 64;              /* the value labels, left of the plot */
constexpr double LEGEND_H = 44;            /* the legend chips and the state, above the plot */
constexpr double RIGHT_PAD = 18;
constexpr double TIME_AXIS_H = 30;         /* the time labels, under the plot */
constexpr double OVERVIEW_H = 30;          /* the memory strip, under the time labels */
constexpr double BOTTOM_PAD = 8;
constexpr double CARD_RADIUS = 10;
constexpr double LEGEND_TOP = 12;          /* the row of the legend and the state */
constexpr double LEGEND_ROW_H = 22;
constexpr double STATE_SHARE = 0.4;        /* the state's text, top right: at most this share of the plot's width */
constexpr double STATE_GAP = 16;           /* between the legend's end and the state's text */
constexpr double LANE_GAP = 10;            /* between two lanes (Lanes) */
constexpr double LANE_UNIT_W = 18;         /* a lane's unit name, rotated, left of its value labels (Lanes) */
constexpr double LANE_BUTTON_H = 16;      /* the fold button at the top of an open lane's unit column */
constexpr double LANE_BUTTON_GAP = 2;      /* between the fold button and the lane's menu button under it */
constexpr double LANE_NAME_MIN = 24;       /* the menu button only with room left for a short unit name ("°C") */
constexpr double LANE_WHEEL_STEP = 40;     /* pixels per wheel notch over the lanes' value labels */
constexpr double LANE_BAR_X = 6;           /* the lanes' scroll bar: this far right of the plot, in its right pad */
constexpr double LANE_BAR_W = 6;
constexpr double LANE_BAR_GRIP = 4;        /* the bar takes clicks this far either side of it */
constexpr double LANE_HANDLE_MIN = 24;
constexpr double FOLDED_ITEM_GAP = 14;     /* between the items of a folded strip */
constexpr double CHIP_GAP = 6;             /* between two chips */
constexpr double CHIP_TEXT_LEFT = 20;      /* a chip's text starts after its dot */
constexpr double CHIP_PAD_RIGHT = 8;
constexpr double LEGEND_BAR_Y = 37;        /* the legend's scroll bar, under the chips, inside LEGEND_H */
constexpr double LEGEND_BAR_H = 4;
constexpr double LEGEND_BAR_GRIP = 5;      /* the bar takes clicks this far above and below it */
constexpr double LEGEND_THUMB_MIN = 28;
constexpr double LEGEND_ARROW_W = 16;      /* the "more this way" marks at the row's ends */
constexpr double LEGEND_WHEEL_STEP = 60;   /* pixels per wheel notch over the legend */
constexpr double READOUT_ROW_H = 18;       /* a line of the crosshair's box */
constexpr double DOT_PICTURE = 9;          /* the crosshair's dot picture: a 3.5 radius dot and its edge */
constexpr int Y_TICKS = 5;                 /* about this many value grid lines */
constexpr double TIME_LABEL_SPACING = 140; /* about one time label per this many pixels */

/* the view (the longest: ChartView::MAX_SPAN) */
constexpr double MIN_WINDOW = 0.001;  /* seconds */
constexpr double MIN_MEMORY = 1;      /* seconds */
constexpr double ZOOM_STEP = 1.25;    /* per wheel notch */
constexpr double LIVE_SNAP = 0.002;   /* a held view this close to now (of the view) is live again */
constexpr double MAX_FRAME_DT = 0.25; /* a stalled frame counts as this long, seconds */
constexpr int READOUT_REACH = 20;     /* the crosshair reads a line with a sample within 1/20 of the view */
constexpr qint64 READOUT_FOLLOW_MS = 50; /* while the mouse moves, its box made again at most this often */

/* the lines */
constexpr double LINE_WIDTH = 1.5;   /* logical pixels */
constexpr double Y_MARGIN = 0.08;    /* free space above and below the lines, of the range */
constexpr double FLAT_RANGE = 1e-12; /* a line's own range narrower than this: it is flat */
constexpr int STRIP_ALPHA = 170;     /* the lines on the memory strip, a little faded */
constexpr qsizetype POINTS_PER_STRIPE = 20000; /* the lines' points that make drawing on threads worth it */
constexpr int STRIPE_OVERLAP = 8; /* device pixels each stripe draws past its edges: an image's own edge pixels are
                                     antialiased a little differently, and are never shown */
constexpr double FRAME_SHARE = 0.6;            /* the chart's frames take at most this share of the thread */
constexpr qsizetype TRIM_PER_FRAME = 4000000; /* samples moved by trims in a frame at most (about 5 ms) */
constexpr double BUDGET_SAMPLE_MS = 30;        /* a slow paint spends at most this (or twice the frames' average) */
constexpr int LAYER_AFTER_FRAMES = 2;          /* the card's layer shown after this many frames under it (paintFrame) */
constexpr qint64 FRAMES_STOPPED_MS = 250;      /* no frame() this long: a change is painted at once (refresh) */

/* the notes' tags: at the bottom of the plot, the text cut to this width at most */
constexpr double NOTE_TAG_H = 16;
constexpr double NOTE_TAG_BOTTOM = 4;          /* above the plot's bottom edge */
constexpr double NOTE_TEXT_MAX = 180;
constexpr double NOTE_PAD = 6;

/* 1, 2, 5 x 10^n steps giving about `target` ticks over span */
double niceStep(double span, int target) {
	if (span <= 0) return 1;
	const double raw = span / target;
	const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
	const double n = raw / magnitude;
	return (n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10) * magnitude;
}

/* time steps: 1/2/5 below a second, then whole seconds, minutes, hours */
double niceTimeStep(double span, int target) {
	const double raw = span / target;
	if (raw < 1) return niceStep(span, target);
	static const double steps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400 };
	for (double s : steps)
		if (s >= raw) return s;
	return 14400;
}

/* A value with about four significant digits, for the legend, the crosshair
 * and the state line. Not the register's own format (formatValue in the model)
 * nor the measurements' (measureText in the Chart tab): short, whatever the line. */
QString chartNumber(double v) {
	const double a = std::fabs(v);
	if (a == 0) return QStringLiteral("0");
	if (a >= 1e5 || a < 1e-3) return QString::number(v, 'g', 4);
	return QString::number(v, 'f', a >= 100 ? 0 : a >= 10 ? 1 : a >= 1 ? 2 : 3);
}

/* chartNumber's widest text: the exponent form with a sign (a three-digit
 * exponent, beyond 1e100, is wider still). The legend gives every value this
 * room, so a chip keeps its width whatever the value. */
QString widestChartNumber() {
	return QStringLiteral("-0.000e+00");
}

/* a length of time for the memory strip: seconds, minutes or hours */
QString formatDuration(double seconds) {
	if (seconds < 120) return QStringLiteral("%1 s").arg(seconds, 0, 'f', 0);
	if (seconds < 7200) return QStringLiteral("%1 min").arg(seconds / 60, 0, 'f', 1);
	return QStringLiteral("%1 h").arg(seconds / 3600, 0, 'f', 1);
}

QDateTime wallClock(qint64 epochMsAtZero, double t) {
	return QDateTime::fromMSecsSinceEpoch(epochMsAtZero + qint64(std::llround(t * 1000)));
}

/* a time grid label, as precise as the grid step needs */
QString timeLabel(qint64 epochMsAtZero, double t, double step) {
	const QDateTime at = wallClock(epochMsAtZero, t);
	if (step >= 1) return at.toString(QStringLiteral("HH:mm:ss"));
	if (step >= 0.1) return at.toString(QStringLiteral("HH:mm:ss.z"));
	QString label = at.toString(QStringLiteral("HH:mm:ss.zzz"));
	if (step >= 0.01) label.chop(1); /* hundredths */
	return label;
}

QFont smallFont() {
	QFont font = QGuiApplication::font();
	font.setPointSizeF(8.5);
	return font;
}

QFont labelFont() {
	QFont font = QGuiApplication::font();
	font.setPointSizeF(9);
	return font;
}

/* the Log scale's lines at 2..9 of a decade: the grid's colour, fainter */
QColor faintGrid() {
	QColor faint = Theme::colors().grid;
	faint.setAlphaF(faint.alphaF() * 0.45);
	return faint;
}

/* a range too narrow to scale into: one unit around it */
void widenFlatRange(double &lo, double &hi) {
	if (hi - lo > FLAT_RANGE) return;
	lo -= 0.5;
	hi += 0.5;
}

/* the index of the sample nearest to t (times rising, not empty) */
qsizetype nearestIndex(const QVector<double> &times, double t) {
	qsizetype k = std::lower_bound(times.begin(), times.end(), t) - times.begin();
	if (k >= times.size()) k = times.size() - 1;
	if (k > 0 && std::fabs(times[k - 1] - t) < std::fabs(times[k] - t)) k--;
	return k;
}

/* the value at t, straight between the samples around it; NaN outside the samples */
double valueAt(const QVector<double> &times, const QVector<double> &values, double t) {
	if (!std::isfinite(t) || t < times.front() || t > times.back()) return NAN;
	const qsizetype k = std::lower_bound(times.begin(), times.end(), t) - times.begin();
	if (k == 0) return values[0];
	const double ta = times[k - 1], tb = times[k];
	return tb > ta ? values[k - 1] + (values[k] - values[k - 1]) * (t - ta) / (tb - ta) : values[k];
}

/* the smallest positive value a bin is known to hold (its min, else its first, last or max); +inf: none. A bin of
 * chunks whose min is <= 0 may hold a smaller positive one: close enough for the Log scale's Auto range */
double smallestPositive(double min, double first, double last, double max) {
	if (min > 0) return min;
	double out = std::numeric_limits<double>::infinity();
	for (const double v : { first, last, max })
		if (v > 0) out = std::min(out, v);
	return out;
}

/* a value on the Y scale's own axis: log10 on the Log scale */
double toScale(double v, bool log) { return log ? std::log10(v) : v; }
double fromScale(double v, bool log) { return log ? std::pow(10.0, v) : v; }

} // namespace

/* A value grid label: a percentage when normalized; a rounding error at zero shows as 0. Every label of the axis
 * with the decimals its step needs (step 2: 14, 12 … 6; step 0.2: 0.2, 0.4), not each its own (14.0 over 8.00). */
QString chartAxisLabel(double v, double step, bool percent) {
	if (percent) return QString::number(int(std::round(v * 100))) + QLatin1Char('%');
	if (std::fabs(v) < step * 1e-6) return QStringLiteral("0");
	if (step < 1e-3 || std::fabs(v) >= 1e5) return chartNumber(v);
	const int decimals = std::max(0, int(-std::floor(std::log10(step) + 1e-9)));
	return QString::number(v, 'f', decimals);
}

/* 1, 10 ... 10000 as the chart writes values; below and above with an SI prefix, three digits at most before it */
QString chartLogLabel(double v) {
	if (!(v > 0)) return QStringLiteral("0");
	if (v >= 1 && v < 1e5) return QString::number(v, 'g', 6);
	static const char *const prefixes[] = { "f", "p", "n", "µ", "m", "", "k", "M", "G", "T", "P" };
	const int exponent = int(std::floor(std::log10(v) + 1e-9));
	const int group = std::clamp(int(std::floor(exponent / 3.0)), -5, 5);
	const double mantissa = v / std::pow(10.0, group * 3);
	return QString::number(mantissa, 'g', 4) + QLatin1Char(' ') + QString::fromUtf8(prefixes[group + 5]);
}

void ChartView::Bin::add(double ta, double tb, double firstValue, double lastValue, double lo, double hi, int samples) {
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

ChartView::ChartView(QWidget *parent) : QWidget(parent) {
	setMouseTracking(true);
	setFocusPolicy(Qt::ClickFocus); /* Delete removes the note clicked */
	setLayoutDirection(Qt::LeftToRight); /* time runs left to right, in any language */
	/* it paints every pixel itself: Qt need not paint what is behind it */
	setAttribute(Qt::WA_OpaquePaintEvent);
	frameClock_.start();
	fpsClock_.start();
	valueClock_.start();
	/* the threads that bin and draw with this one; the I/O thread and the system keep theirs */
	pool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 3, 1, 8));
	pool_.setExpiryTimeout(-1);
	opener_.setMaxThreadCount(1);
}

/* job(i) for every i, taken in turn by the pool's threads and this one. This one waits for the jobs taken, never for
 * a thread still waking up (on Windows that can take milliseconds): a thread that starts late finds none left and
 * ends, so the shared state outlives this call. Fewer than jobsPerThread jobs a thread (few lines): not worth waking
 * one, this thread alone. */
void ChartView::inParallel(qsizetype count, const std::function<void(qsizetype)> &job, qsizetype jobsPerThread) const {
	const int helpers = int(std::min<qsizetype>(count / jobsPerThread - 1, pool_.maxThreadCount()));
	if (helpers <= 0) {
		for (qsizetype i = 0; i < count; i++) job(i);
		return;
	}
	struct Batch {
		std::atomic<qsizetype> next{ 0 }, done{ 0 };
		qsizetype count = 0;
		const std::function<void(qsizetype)> *job = nullptr; /* used only while a job is left: this call is still on */
	};
	const auto batch = std::make_shared<Batch>();
	batch->count = count;
	batch->job = &job;
	const auto work = [batch] {
		for (qsizetype i = batch->next++; i < batch->count; i = batch->next++) {
			(*batch->job)(i);
			batch->done++;
		}
	};
	for (int k = 0; k < helpers; k++) pool_.start(work);
	work();
	while (batch->done.load() < count) QThread::yieldCurrentThread(); /* the last jobs others took: short */
}

/* -------------------------------------------------------------- the samples */

void ChartView::addSeries(int key, const QString &name, const QString &unit, const QColor &color) {
	Series s;
	s.name = name;
	s.unit = unit;
	s.color = color;
	s.spread = int(seriesAdded_++ % 8);
	/* a line put back keeps its total since Clear; another line under its key starts at 0 */
	const auto kept = keptTotals_.constFind(key);
	if (kept != keptTotals_.constEnd() && kept->name == name && kept->unit == unit) {
		s.total = kept->total;
		s.totalT = kept->t;
		s.totalV = kept->v;
	}
	keptTotals_.remove(key);
	series_.insert(key, s);
	seriesGeneration_++;
	capped_ = false;
	forgetRanges();
	refresh();
}

void ChartView::removeSeries(int key) {
	const auto it = series_.constFind(key);
	if (it != series_.constEnd()) keptTotals_.insert(key, { it->name, it->unit, it->total, it->totalT, it->totalV });
	series_.remove(key);
	seriesGeneration_++;
	forgetRanges();
	refresh();
}

void ChartView::clearSeries() {
	series_.clear();
	keptTotals_.clear(); /* another set of lines: the totals from 0 */
	totalsSince_ = NAN;
	seriesGeneration_++;
	capped_ = false;
	forgetRanges();
	refresh();
}

void ChartView::clearData() {
	for (Series &s : series_) {
		s.times.clear();
		s.values.clear();
		for (QVector<Chunk> &chunks : s.chunks) chunks.clear();
		s.hasLast = false;
		s.hasShown = false;
		s.total = 0;
		s.totalT = NAN;
	}
	keptTotals_.clear();
	totalsSince_ = NAN; /* the first sample from now on */
	seriesGeneration_++;
	capped_ = false;
	forgetRanges();
	refresh();
}

/* A part of what p paints, drawn apart into an image (on another thread too): `device` is its place in pixels of
 * p's device. `world` maps the widget's coordinates into the image's pixels: p's own mapping, then a shift by whole
 * pixels only, so a line lands on exactly the pixels it would take drawn on p (the widget need not start on a whole
 * device pixel: at 225 % a widget at x = 13 starts at 29.25). The image is drawn in at pixel ratio 1; finishTile
 * gives it p's, and `at` is where it goes back, in the widget's coordinates. */
void ChartView::prepareTile(const QPainter &p, const QRect &device, QImage &image, QTransform &world, QPointF &at) {
	const QTransform toDevice = p.deviceTransform();
	if (image.size() != device.size()) image = QImage(device.size(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(1);
	image.fill(Qt::transparent);
	world = toDevice * QTransform::fromTranslate(-device.left(), -device.top());
	at = toDevice.inverted().map(QPointF(device.topLeft()));
}

qint64 ChartView::bytesNeeded() const {
	double samples = 0;
	for (const Series &s : series_) {
		const qsizetype n = s.times.size();
		if (n < 2 || s.times.back() <= s.times.front()) continue;
		const double rate = double(n - 1) / (s.times.back() - s.times.front());
		samples += std::min(rate * memory_, double(MAX_POINTS));
	}
	return qint64(samples * BYTES_PER_SAMPLE);
}

qint64 ChartView::bytesHeld() const {
	qint64 bytes = 0;
	for (const Series &s : series_) {
		bytes += qint64(s.times.capacity() + s.values.capacity()) * qint64(sizeof(double));
		for (const QVector<Chunk> &level : s.chunks) bytes += qint64(level.capacity()) * qint64(sizeof(Chunk));
	}
	return bytes;
}

qsizetype ChartView::pointsKept(int key) const {
	const auto it = series_.find(key);
	return it == series_.end() ? 0 : it->times.size();
}

void ChartView::setRamBudget(int megabytes) {
	ramMB_ = std::max(megabytes, MIN_RAM_MB);
	capped_ = false; /* the lines past their new share say so again at their next sample */
	refresh();
}

qsizetype ChartView::pointsPerLine() const {
	const qsizetype lines = std::max<qsizetype>(1, series_.size());
	const qsizetype total = qsizetype(ramMB_) * 1024 * 1024 / BYTES_PER_SAMPLE;
	return std::clamp<qsizetype>(total / lines, qsizetype(CHUNK_SIZE[LEVELS - 1]) * 16, MAX_POINTS);
}

namespace {
/* A line's arrays take what its share of the RAM needs, not up to twice it (2 GB for a RAM of 1 GB): they grow by
 * about doubling only up to `most` elements, and what goes from the front moves the rest to the front. Qt's vectors
 * keep the room freed at the front, and when the end comes with two thirds of them in use they double instead of
 * moving. spread (0 to 7, the line's): it grows 2 + spread / 16 times, so lines that fill together grow at different
 * moments (all at once, 64 lines at RAM 1 GB moved 0.5 GB in one frame at their last growth) */
template <typename T>
void roomForOne(QVector<T> &v, qsizetype most, int spread) {
	if (v.size() < v.capacity()) return;
	const qsizetype grown = v.size() * (32 + spread) / 16;
	v.reserve(std::max(v.size() + 1, std::min(std::max<qsizetype>(grown, 1024), most)));
}
template <typename T>
void dropFront(QVector<T> &v, qsizetype n, qsizetype most) {
	n = std::min(n, v.size());
	/* the share shrank (more lines, less RAM): the room let go, what stays copied once */
	if (v.capacity() > most + most / 4 + 1024) {
		QVector<T> fitted;
		fitted.reserve(std::max(most, v.size() - n));
		fitted.resize(v.size() - n);
		std::copy(v.constData() + n, v.constData() + v.size(), fitted.data()); /* one block copy */
		v.swap(fitted);
		return;
	}
	std::move(v.begin() + n, v.end(), v.begin());
	v.resize(v.size() - n);
}
} // namespace

void ChartView::append(int key, double t, double v) {
	if (measuring_) { /* the threads read the arrays as they were: in when they are done (measureAsync) */
		heldSamples_.push_back({ key, t, v });
		return;
	}
	appendNow(key, t, v);
}

void ChartView::appendNow(int key, double t, double v) {
	auto it = series_.find(key);
	if (it == series_.end() || !std::isfinite(v)) return;
	Series &s = *it;
	/* the total since Clear, from every sample as it comes (not from what the memory keeps: trims lose nothing); a gap
	 * over TOTAL_GAP is not bridged */
	if (std::isnan(s.totalT) || t >= s.totalT) {
		const double dt = t - s.totalT;
		if (dt > 0 && dt <= TOTAL_GAP) s.total += 0.5 * (v + s.totalV) * dt;
		s.totalT = t;
		s.totalV = v;
		if (std::isnan(totalsSince_)) totalsSince_ = t;
	}
	/* the trigger: this line crossing the level since it was armed, at the time straight between the two samples */
	if (trigger_.armed && key == trigger_.key && !s.times.isEmpty() && t > trigger_.armedFrom && t > s.times.back()) {
		const double pt = s.times.back(), pv = s.values.back(), level = trigger_.level;
		const bool up = pv < level && v >= level, down = pv > level && v <= level;
		const TriggerEdge edge = trigger_.edge;
		if ((edge != TriggerEdge::Falling && up) || (edge != TriggerEdge::Rising && down))
			fireTrigger(v != pv ? pt + (level - pv) / (v - pv) * (t - pt) : t);
	}
	const qsizetype limit = pointsPerLine();
	dropExpired(s, t, limit);
	roomForOne(s.times, limit, s.spread);
	roomForOne(s.values, limit, s.spread);
	s.times.push_back(t);
	s.values.push_back(v);
	addChunks(s, limit);
	s.last = v;
	s.hasLast = true;
}

/* the chunk of each level the newest sample completes: the smallest from its
 * samples, each larger one from the LEVEL_STEP chunks below it */
void ChartView::addChunks(Series &s, qsizetype limit) {
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

/* The memory: what is older than `memory` goes, about a twentieth at a time (it
 * is let grow 5 % past it first), and from a sixteenth short of the line's share
 * of the budget it goes down to seven eighths of the share, in whole chunks of
 * the largest level (so chunk k of each level still covers samples k * its size
 * on): not a sample at a time, which would move the whole vector at every poll.
 * The lines fill together, and trimmed together they moved 0.9 GB in one frame
 * (65 lines at RAM 1 GB: 85 ms, five frames, every 86 s): a frame moves at most
 * TRIM_PER_FRAME samples, the other lines waiting their turn, none past its share. */
void ChartView::dropExpired(Series &s, double t, qsizetype limit) {
	if (s.times.isEmpty()) return;
	const bool expired = t - s.times.front() > memory_ * 1.05 + 0.5;
	const bool full = s.times.size() >= limit - limit / 16;
	if (!expired && !full) return;
	if (s.times.size() < limit && movedThisFrame_ > 0 && movedThisFrame_ + s.times.size() > TRIM_PER_FRAME)
		return; /* its turn comes at a later frame */
	const qsizetype largest = CHUNK_SIZE[LEVELS - 1];
	qsizetype drop = std::lower_bound(s.times.begin(), s.times.end(), t - memory_) - s.times.begin();
	if (full) { /* past the share by more than an eighth (the RAM lowered, lines added): all of it at once, not an
		     * eighth at each sample, each moving the whole line (1 GB to 512 MB held the window 3 s) */
		drop = std::max(drop, s.times.size() - limit + std::max(limit / 8, largest));
		capped_ = true;
	}
	drop = (drop / largest) * largest;
	if (drop <= 0) return;
	dropFront(s.times, drop, limit);
	dropFront(s.values, drop, limit);
	s.dropped += drop;
	for (int level = 0; level < LEVELS; level++)
		dropFront(s.chunks[level], drop / CHUNK_SIZE[level], limit / CHUNK_SIZE[level] + 1);
	movedThisFrame_ += s.times.size();
}

/* ----------------------------------------------------------------- the view */

void ChartView::setClock(std::function<double()> clock, qint64 epochMsAtZero) {
	clock_ = std::move(clock);
	epochMs_ = epochMsAtZero;
}

double ChartView::clockNow() const { return clock_ ? clock_() : 0; } /* 0 until the clock is set */

void ChartView::frame() {
	const double sinceLastMs = framesCome_.isValid() ? framesCome_.nsecsElapsed() / 1e6 : 0;
	framesCome_.restart();
	movedThisFrame_ = 0; /* the trims' turn: the samples of this frame were appended before */
	/* Normal: armed again once the view after the last crossing is full */
	if (trigger_.on && trigger_.mode == TriggerMode::Normal && !trigger_.armed && std::isfinite(trigger_.at)) {
		const double full = trigger_.at + (1 - TRIGGER_AT) * window_;
		if (clockNow() >= full) {
			trigger_.armed = true;
			trigger_.armedFrom = full;
		}
	}
	if (valuePacer_.due(valueClock_.elapsed())) {
		for (Series &s : series_) {
			s.shown = s.last;
			s.hasShown = s.hasLast;
		}
		valuesTick_++; /* the crosshair's box too */
	}
	/* the frame budget: the window's thread stays free for the rest (FrameBudget) */
	if (budget_.due(sinceLastMs) && isVisible()) paintSoon();
}

/* Frames a little over the share skip one now and then (11 ms frames at 60 Hz: about 55 a second); waiting after
 * each frame over 10 ms as long as it took to stay at 60 % halved the rate there (35 a second for 11.5 ms frames,
 * 2026-10-05). Heavy frames still take 60 % at most: 20 ms ones every other refresh. */
bool ChartView::FrameBudget::due(double sinceLastMs) {
	const double earned = std::max(0.0, sinceLastMs) * FRAME_SHARE;
	creditMs = std::min(creditMs + earned, earned);
	return creditMs >= 0;
}

/* one slow paint among quick ones (a resize, a theme switch) counts little; slow ones all along count in full, so
 * they too stay within the share (41 ms frames counted as 30 took 80 % of the thread) */
void ChartView::FrameBudget::spent(double paintMs) {
	creditMs -= std::min(paintMs, std::max(BUDGET_SAMPLE_MS, 2 * averageMs));
	averageMs = averageMs * 0.8 + paintMs * 0.2;
}

/* While frames come, a change waits for the next: the mouse moves up to 1000 times a second, and a frame for each
 * painted the chart past the display's rate and past the frame budget (with 64 lines on a card: 35 frames a second
 * where 60 were drawn, and the Smooth delay up as frames came between the samples) */
void ChartView::refresh() {
	if (framesCome_.isValid() && framesCome_.elapsed() < FRAMES_STOPPED_MS) return; /* the next frame paints it */
	paintSoon();
}

void ChartView::paintSoon() {
	if (plotOnCard()) update(QRegion(rect()).subtracted(QRegion(layerRect()))); /* the card presents the plot */
	else update();
}

void ChartView::setValuesPerSecond(int perSecond) {
	valuePacer_.setPerSecond(perSecond); /* the next frame shows the newest values */
}

void ChartView::setWindow(double seconds) {
	window_ = std::clamp(seconds, MIN_WINDOW, MAX_SPAN);
	if (window_ > memory_) setMemory(window_); /* the view must fit in the memory */
	refresh();
}

void ChartView::setMemory(double seconds) {
	const double clamped = std::clamp(seconds, MIN_MEMORY, MAX_SPAN);
	if (clamped == memory_) return;
	memory_ = clamped;
	if (window_ > memory_) window_ = memory_;
	emit memoryChanged(memory_);
	refresh();
}

void ChartView::setLive(bool on) {
	if (on == live_) return;
	if (!on) viewEnd_ = lastViewEnd_; /* hold what is shown */
	live_ = on;
	emit liveChanged(live_);
	refresh();
}

void ChartView::showSpan(double t0, double t1) {
	const double span = std::clamp(t1 - t0, MIN_WINDOW, MAX_SPAN);
	if (span > memory_) setMemory(span);
	window_ = std::min(span, memory_);
	viewEnd_ = t1;
	if (live_) {
		live_ = false;
		emit liveChanged(false);
	}
	refresh();
}

void ChartView::setTrigger(int key, double level, TriggerEdge edge, TriggerMode mode) {
	trigger_.on = true;
	trigger_.key = key;
	trigger_.level = level;
	trigger_.edge = edge;
	trigger_.mode = mode;
	trigger_.at = NAN;
	armTrigger();
}

void ChartView::stopTrigger() {
	trigger_.on = trigger_.armed = false;
	trigger_.at = NAN;
	refresh();
}

/* from the line's newest sample on: a crossing already in the memory does not count */
void ChartView::armTrigger() {
	if (!trigger_.on) return;
	trigger_.armed = true;
	const auto it = series_.constFind(trigger_.key);
	trigger_.armedFrom = it != series_.constEnd() && !it->times.isEmpty() ? it->times.back()
			: -std::numeric_limits<double>::infinity();
	refresh();
}

void ChartView::setTriggerLevel(double level) {
	trigger_.level = level;
	refresh();
}

/* the view holds with the crossing at TRIGGER_AT of it: what comes after fills its right part as it arrives */
void ChartView::fireTrigger(double time) {
	trigger_.at = time;
	trigger_.armed = false;
	viewEnd_ = time + (1 - TRIGGER_AT) * window_;
	if (live_) {
		live_ = false;
		emit liveChanged(false);
	}
	emit triggered(time);
	refresh();
}

void ChartView::lineSamples(int key, double t0, double t1, QVector<double> &times, QVector<double> &values) const {
	times.clear();
	values.clear();
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return;
	const qsizetype i0 = std::lower_bound(it->times.begin(), it->times.end(), t0) - it->times.begin();
	const qsizetype i1 = std::upper_bound(it->times.begin(), it->times.end(), t1) - it->times.begin();
	if (i1 <= i0) return;
	times = it->times.mid(i0, i1 - i0);
	values = it->values.mid(i0, i1 - i0);
}

int ChartView::chipAt(const QPointF &pos) const {
	const LegendLayout legend = legendLayout(plotRect());
	if (!legend.viewport.contains(pos)) return -1;
	const double offset = legendOffset(legend);
	const QList<int> keys = series_.keys();
	for (qsizetype i = 0; i < legend.chips.size() && i < keys.size(); i++)
		if (legend.chips[i].translated(-offset, 0).contains(pos)) return keys[i];
	return -1;
}

void ChartView::showLastValues() {
	for (Series &s : series_) {
		s.shown = s.last;
		s.hasShown = s.hasLast;
	}
	valuesTick_++;
	refresh();
}

QVector<recording::Line> ChartView::samples(double t0, double t1) const {
	QVector<recording::Line> out;
	for (const Series &s : series_) {
		const qsizetype i0 = std::lower_bound(s.times.begin(), s.times.end(), t0) - s.times.begin();
		const qsizetype i1 = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin();
		recording::Line line;
		line.name = s.name;
		line.unit = s.unit;
		if (i1 > i0) {
			line.times = s.times.mid(i0, i1 - i0);
			line.values = s.values.mid(i0, i1 - i0);
		}
		out << line;
	}
	return out;
}

void ChartView::setNotes(const QVector<ChartNote> &notes) {
	notes_ = notes;
	selectedNote_ = -1;
	refresh();
}

int ChartView::addNote(double time, const QString &text) {
	notes_ << ChartNote{ time, text };
	selectedNote_ = int(notes_.size() - 1);
	emit notesChanged();
	refresh();
	return selectedNote_;
}

void ChartView::setNoteText(int index, const QString &text) {
	if (index < 0 || index >= notes_.size() || notes_[index].text == text) return;
	notes_[index].text = text;
	emit notesChanged();
	refresh();
}

void ChartView::removeNote(int index) {
	if (index < 0 || index >= notes_.size()) return;
	notes_.removeAt(index);
	selectedNote_ = -1;
	emit notesChanged();
	refresh();
}

QRectF ChartView::noteTag(int index) const { return index >= 0 && index < noteTags_.size() ? noteTags_[index] : QRectF(); }

int ChartView::noteAtPoint(const QPointF &pos) const {
	for (qsizetype i = std::min(noteTags_.size(), notes_.size()) - 1; i >= 0; i--)
		if (noteTags_[i].adjusted(-2, -2, 2, 2).contains(pos)) return int(i);
	return -1;
}

void ChartView::setHoverValues(bool on) {
	hoverValues_ = on;
	readoutTick_ = ~quint64(0); /* made again (or let go) at the next frame */
	refresh();
}

void ChartView::setSmooth(bool on) {
	smooth_ = on;
	if (!on) delay_ = 0;
	refresh();
}

void ChartView::holdAt(double end) {
	double m0, m1;
	memorySpan(m0, m1);
	const double liveEdge = liveEnd();
	end = std::clamp(end, std::min(m0 + window_, liveEdge), liveEdge);
	if (end >= liveEdge - window_ * LIVE_SNAP) { /* back at now: live again */
		setLive(true);
		return;
	}
	viewEnd_ = end;
	if (live_) {
		live_ = false;
		emit liveChanged(false);
	}
	refresh();
}

void ChartView::memorySpan(double &m0, double &m1) const {
	m1 = liveEnd();
	m0 = m1;
	for (const Series &s : series_)
		if (!s.times.isEmpty()) m0 = std::min(m0, s.times.front());
	m0 = std::max(m0, m1 - memory_);
}

void ChartView::setYAuto() {
	y_.autoRange = true;
	y_.initialized = false;
	refresh();
}

bool ChartView::setYManual(double lo, double hi) {
	if (!(hi > lo) || (y_.log && !(lo > 0))) return false;
	y_.autoRange = false;
	y_.lo = lo;
	y_.hi = hi;
	refresh();
	return true;
}

void ChartView::setYLog(bool on) {
	if (on == y_.log) return;
	y_.log = on;
	y_.initialized = false; /* Auto ranges again on the new scale */
	if (on && !y_.autoRange && !(y_.lo > 0)) y_.autoRange = true; /* a range through zero has no logarithm */
	refresh();
}

double ChartView::total(int key) const {
	const auto it = series_.constFind(key);
	return it == series_.constEnd() || std::isnan(it->totalT) ? NAN : it->total;
}

void ChartView::clearCursors() {
	cursorA_ = cursorB_ = NAN;
	emit cursorsChanged();
	refresh();
}

void ChartView::setCursors(double a, double b) {
	cursorA_ = a;
	cursorB_ = b;
	emit cursorsChanged();
	refresh();
}

QRectF ChartView::plotRect() const {
	return QRectF(rect()).adjusted(AXIS_W, LEGEND_H, -RIGHT_PAD, -(TIME_AXIS_H + OVERVIEW_H + BOTTOM_PAD));
}

QRectF ChartView::overviewRect() const {
	const QRectF plot = plotRect();
	return QRectF(plot.left(), height() - OVERVIEW_H - BOTTOM_PAD, plot.width(), OVERVIEW_H);
}

double ChartView::timeAtX(double x) const {
	const QRectF plot = plotRect();
	return viewEnd() - window_ + (x - plot.left()) / plot.width() * window_;
}

double ChartView::xAtTime(double t) const {
	const QRectF plot = plotRect();
	return plot.left() + (t - (viewEnd() - window_)) / window_ * plot.width();
}

/* The display delay (Smooth): a little more than the latest gap between now and
 * the newest sample, since samples come in bursts (per poll, per frame, per TCP
 * packet), so the line always reaches the right edge. It follows slowly, so the
 * scroll speed does not wobble; at most 0.5 s (a slow poll just shows its gap). */
void ChartView::updateDelay(double frameDt) {
	constexpr double MAX_GAP = 1.0;     /* a longer gap: no data coming, nothing to smooth */
	constexpr double PEAK_DECAY = 0.05; /* the peak gap is forgotten this fast, seconds per second */
	constexpr double HEADROOM = 1.1, HEADROOM_S = 0.003;
	constexpr double MAX_DELAY = 0.5;
	constexpr double RISE_TIME = 0.15, FALL_TIME = 2.0; /* seconds: the delay grows fast, shrinks slowly */
	constexpr double NONE = -std::numeric_limits<double>::max();
	if (!smooth_) return;
	double newest = NONE;
	for (const Series &s : series_)
		if (!s.times.isEmpty()) newest = std::max(newest, s.times.back());
	if (newest == NONE) return; /* no samples yet */
	const double gap = clockNow() - newest;
	if (gap < 0 || gap > MAX_GAP) return;
	peakGap_ = std::max(gap, peakGap_ - frameDt * PEAK_DECAY);
	const double target = std::min(MAX_DELAY, peakGap_ * HEADROOM + HEADROOM_S);
	const double rate = 1 - std::exp(-frameDt / (target > delay_ ? RISE_TIME : FALL_TIME));
	delay_ += (target - delay_) * rate;
}

/* ---------------------------------------------------------------- measuring */

QVector<ChartView::Info> ChartView::lines() const {
	QVector<Info> out;
	for (auto it = series_.begin(); it != series_.end(); ++it)
		out.push_back({ it.key(), it->name, it->unit, it->color });
	return out;
}

void ChartView::range(double &t0, double &t1, bool &cursors) const {
	cursors = std::isfinite(cursorA_) && std::isfinite(cursorB_) && cursorA_ != cursorB_;
	if (cursors) {
		t0 = std::min(cursorA_, cursorB_);
		t1 = std::max(cursorA_, cursorB_);
	} else {
		t1 = lastViewEnd_;
		t0 = t1 - window_;
	}
}

/* the values at the cursors, and over the range: min, max, and from the
 * trapezoids between the samples the area, the mean and the RMS */
ChartView::Stats ChartView::stats(int key) const {
	Stats result;
	auto it = series_.find(key);
	if (it == series_.end()) return result;
	double t0, t1;
	bool cursors;
	range(t0, t1, cursors);
	if (!it->times.isEmpty()) result = statsOf(it->times, it->values, t0, t1, cursorA_, cursorB_);
	result.total = std::isnan(it->totalT) ? NAN : it->total;
	return result;
}

ChartView::Stats ChartView::statsOf(const QVector<double> &times, const QVector<double> &values, double t0, double t1,
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

/* each line's on a thread of the chart's: 64 lines over minutes of samples took tens of ms on one */
QVector<ChartView::Stats> ChartView::stats(const QVector<int> &keys, bool cursorsOnly) const {
	if (!cursorsOnly) const_cast<ChartView *>(this)->fullStatsSync_++; /* the window thread waits for these */
	QVector<Stats> all(keys.size());
	inParallel(keys.size(), [&](qsizetype i) {
		if (!cursorsOnly) {
			all[i] = stats(keys[i]);
			return;
		}
		const auto it = series_.find(keys[i]);
		if (it == series_.end() || it->times.isEmpty()) return;
		all[i].total = std::isnan(it->totalT) ? NAN : it->total;
		all[i].atA = valueAt(it->times, it->values, cursorA_);
		all[i].atB = valueAt(it->times, it->values, cursorB_);
	}, 2);
	return all;
}

/* The measurements' key: equal keys, equal values. The samples in the range are numbered from the line's start
 * (dropped + index), so a sample added after the range or one let go before it changes nothing; with the cursors, the
 * samples beside them too (the values at A and B lie between them). */
QVector<double> ChartView::measureKey(const QVector<int> &keys) const {
	double t0, t1;
	bool cursors;
	range(t0, t1, cursors);
	/* a cursor not placed as -inf: NaN is never equal to itself, the key would never be the same */
	const auto placed = [](double t) { return std::isfinite(t) ? t : -std::numeric_limits<double>::infinity(); };
	QVector<double> key{ t0, t1, placed(cursorA_), placed(cursorB_), double(seriesGeneration_), double(normalized_) };
	for (int k : keys) {
		const auto it = series_.constFind(k);
		if (it == series_.constEnd()) {
			key << -1 << -1;
			continue;
		}
		const Series &s = *it;
		qsizetype i0 = std::lower_bound(s.times.begin(), s.times.end(), t0) - s.times.begin();
		qsizetype i1 = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin();
		if (cursors) {
			i0 = std::max<qsizetype>(0, i0 - 1);
			i1 = std::min<qsizetype>(s.times.size(), i1 + 1);
		}
		key << double(s.dropped + i0) << double(s.dropped + i1);
	}
	return key;
}

ChartView::~ChartView() { pool_.waitForDone(); }

void ChartView::measureAsync(const QVector<int> &keys, std::function<void(const QVector<Stats> &, double)> done) {
	MeasureRequest request{ keys, std::move(done) };
	if (measuring_) { /* its turn when the one under way is done; a newer one replaces it */
		nextMeasure_ = std::move(request);
		measureNext_ = true;
		return;
	}
	startMeasure(std::move(request));
}

/* The lines' arrays shared with the threads (Qt's vectors copy only when one is changed): the window thread changes
 * none while they read, its samples held back (append), and a line taken off or cleared meanwhile leaves the threads
 * its arrays. The threads give them back before the result is posted. */
void ChartView::startMeasure(MeasureRequest request) {
	struct Job {
		QVector<QVector<double>> times, values;
		QVector<double> totals;
		double t0 = 0, t1 = 0, a = NAN, b = NAN;
		QVector<Stats> out;
		std::atomic<int> left{ 0 };
		QElapsedTimer clock;
		MeasureRequest request;
	};
	const auto job = std::make_shared<Job>();
	bool cursors;
	range(job->t0, job->t1, cursors);
	job->a = cursorA_;
	job->b = cursorB_;
	const qsizetype n = request.keys.size();
	job->times.resize(n);
	job->values.resize(n);
	job->totals.fill(NAN, n);
	job->out.resize(n);
	for (qsizetype i = 0; i < n; i++) {
		const auto it = series_.constFind(request.keys[i]);
		if (it == series_.constEnd()) continue;
		job->times[i] = it->times;
		job->values[i] = it->values;
		job->totals[i] = std::isnan(it->totalT) ? NAN : it->total;
	}
	job->request = std::move(request);
	measuring_ = true;
	job->clock.start();
	if (n == 0) {
		QMetaObject::invokeMethod(this, [this, job] {
			measuring_ = false;
			job->request.done(job->out, 0);
		}, Qt::QueuedConnection);
		return;
	}
	job->left = int(n);
	for (qsizetype i = 0; i < n; i++)
		pool_.start([this, job, i] {
			if (!job->times[i].isEmpty())
				job->out[i] = statsOf(job->times[i], job->values[i], job->t0, job->t1, job->a, job->b);
			job->out[i].total = job->totals[i];
			if (--job->left > 0) return;
			/* the last: the arrays given back, then the result to the window thread */
			job->times.clear();
			job->values.clear();
			const double ms = job->clock.nsecsElapsed() / 1e6;
			QMetaObject::invokeMethod(this, [this, job, ms] {
				measuring_ = false;
				const QVector<HeldSample> held = std::exchange(heldSamples_, {});
				for (const HeldSample &sample : held) appendNow(sample.key, sample.t, sample.v);
				if (!held.isEmpty()) refresh();
				job->request.done(job->out, ms);
				if (measureNext_ && !measuring_) {
					measureNext_ = false;
					startMeasure(std::move(nextMeasure_));
				}
			}, Qt::QueuedConnection);
		});
}

/* ---------------------------------------------------------------- the mouse */

bool ChartView::event(QEvent *e) {
	if (e->type() == QEvent::Leave) {
		mouseX_ = -1;
		hoverLane_ = -1;
		hoverMenu_ = -1;
		hoverBar_ = false;
		hoverSeparator_ = -1;
		refresh();
	}
	if (e->type() == QEvent::ToolTip) { /* the lanes' own: their buttons, strips and value labels */
		auto *help = static_cast<QHelpEvent *>(e);
		const QString text = toolTipAt(help->pos());
		if (text.isEmpty()) QToolTip::hideText();
		else QToolTip::showText(help->globalPos(), text, this);
		return true;
	}
	return QWidget::event(e);
}

void ChartView::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) return;
	const QPointF pos = e->position();
	pressedLanes_ = false;
	if (pressLegend(pos)) return;
	/* the trigger's level: dragged */
	if (trigger_.on && std::isfinite(triggerLineY_) && std::fabs(pos.y() - triggerLineY_) <= 4
			&& pos.x() >= triggerLane_.left() && pos.x() <= triggerLane_.right()) {
		drag_ = Drag::Level;
		return;
	}
	/* a note's tag: chosen (Delete removes it) and dragged */
	const int note = noteAtPoint(pos);
	if (note >= 0 || selectedNote_ >= 0) {
		selectedNote_ = note;
		refresh();
	}
	if (note >= 0) {
		drag_ = Drag::Note;
		return;
	}
	/* on (or just by) the memory strip: the view goes there */
	if (overviewRect().adjusted(0, -4, 0, 4).contains(pos)) {
		drag_ = Drag::Overview;
		mouseMoveEvent(e);
		return;
	}
	if (pressLanes(pos)) { /* the lanes' scroll bar, a unit name, a folded strip: no cursor, no pan */
		pressedLanes_ = true;
		return;
	}
	if (!plotRect().contains(pos)) return;
	if (cursorMode_) {
		pickCursor(pos.x());
		return;
	}
	drag_ = Drag::Pan;
	dragStartX_ = pos.x();
	dragStartEnd_ = viewEnd();
	setCursor(Qt::ClosedHandCursor);
}

/* Lanes: on the scroll bar, the handle is dragged and a click above or below it moves one plot height; a click on an
 * open lane's unit name folds it, one anywhere on a folded strip opens it */
bool ChartView::pressLanes(const QPointF &pos) {
	if (!lanes_) return false;
	const QRectF plot = plotRect();
	const QRectF track = laneScrollBarRect();
	if (!track.isEmpty() && track.adjusted(-LANE_BAR_GRIP, 0, LANE_BAR_GRIP, 0).contains(pos)) {
		const QRectF handle = laneScrollHandleRect();
		if (pos.y() < handle.top()) scrollLanesTo(laneScroll() - plot.height());
		else if (pos.y() > handle.bottom()) scrollLanesTo(laneScroll() + plot.height());
		else {
			drag_ = Drag::LaneBar;
			dragStartY_ = pos.y();
			dragStartScroll_ = laneScroll();
		}
		return true;
	}
	const int gap = separatorAt(pos);
	if (gap >= 0) { /* a separator: the lanes above and below it share their heights as it is dragged */
		drag_ = Drag::LaneBorder;
		dragSeparator_ = gap;
		dragStartY_ = pos.y();
		dragHeights_[0] = lanesShown_[gap].axes.rect.height();
		dragHeights_[1] = lanesShown_[gap + 1].axes.rect.height();
		QVector<double> heights;
		laneHeights(lanesShown_, plot.height(), heights, dragUnit_);
		return true;
	}
	if (pos.x() > plot.right() || pos.y() < plot.top() || pos.y() > plot.bottom()) return false;
	const int lane = laneAtY(pos.y());
	if (lane < 0) return false;
	const QRectF shown = laneVisible(lanesShown_[lane].axes.rect, plot);
	if (pos.y() < shown.top() || pos.y() > shown.bottom()) return false; /* between two lanes */
	const bool folded = lanesShown_[lane].folded;
	if (!folded && pos.x() >= LANE_UNIT_W) return false;
	if (laneMenuButtonAt(pos) == lane) { /* its menu button: the lane's menu, under the button */
		const QRectF menu = laneMenuButtonRect(lane);
		emit laneMenuRequested(lane, mapToGlobal(QPoint(int(menu.left()), int(menu.bottom()) + 1)));
		return true;
	}
	setLaneFolded(lane, !folded);
	return true;
}

/* cursor mode: a click places A, then B, then moves the nearer of the two */
void ChartView::pickCursor(double x) {
	const double t = timeAtX(x);
	if (!std::isfinite(cursorA_)) drag_ = Drag::CurA;
	else if (!std::isfinite(cursorB_)) drag_ = Drag::CurB;
	else drag_ = std::fabs(xAtTime(cursorA_) - x) <= std::fabs(xAtTime(cursorB_) - x) ? Drag::CurA : Drag::CurB;
	(drag_ == Drag::CurA ? cursorA_ : cursorB_) = t;
	emit cursorsChanged();
	refresh();
}

void ChartView::mouseMoveEvent(QMouseEvent *e) {
	const QPointF pos = e->position();
	mouseX_ = int(pos.x());
	const QRectF plot = plotRect();
	switch (drag_) {
	case Drag::Pan:
		holdAt(dragStartEnd_ - (pos.x() - dragStartX_) / plot.width() * window_);
		break;
	case Drag::Overview: {
		/* the strip spans the whole memory depth, filled or not */
		const double m1 = liveEnd(), m0 = m1 - memory_;
		const QRectF strip = overviewRect();
		const double t = m0 + std::clamp((pos.x() - strip.left()) / strip.width(), 0.0, 1.0) * (m1 - m0);
		holdAt(t + window_ / 2); /* the view centred where the mouse is */
		break;
	}
	case Drag::CurA:
	case Drag::CurB:
		(drag_ == Drag::CurA ? cursorA_ : cursorB_) = timeAtX(std::clamp(pos.x(), plot.left(), plot.right()));
		emit cursorsChanged();
		break;
	case Drag::Level: {
		const double at = triggerAxes_.value(std::clamp(pos.y(), triggerLane_.top(), triggerLane_.bottom()));
		trigger_.level = normalized_ ? triggerLo_ + at * (triggerHi_ - triggerLo_) : at;
		break;
	}
	case Drag::Note:
		if (selectedNote_ >= 0 && selectedNote_ < notes_.size())
			notes_[selectedNote_].time = timeAtX(std::clamp(pos.x(), plot.left(), plot.right()));
		break;
	case Drag::LaneBorder: { /* the lane above takes what the one below gives, neither under LANE_MIN_H */
		if (dragSeparator_ < 0 || dragSeparator_ + 1 >= lanesShown_.size() || dragUnit_ <= 0) break;
		const double d = std::clamp(pos.y() - dragStartY_, LANE_MIN_H - dragHeights_[0], dragHeights_[1] - LANE_MIN_H);
		laneWeights_[lanesShown_[dragSeparator_].key] = (dragHeights_[0] + d) / dragUnit_;
		laneWeights_[lanesShown_[dragSeparator_ + 1].key] = (dragHeights_[1] - d) / dragUnit_;
		lanesShown_ = plotLayout();
		break;
	}
	case Drag::LaneBar: { /* the handle follows the mouse: its free travel spans the whole scroll */
		const double travel = laneScrollBarRect().height() - laneScrollHandleRect().height();
		if (travel > 0) scrollLanesTo(dragStartScroll_ + (pos.y() - dragStartY_) / travel * maxLaneScroll());
		break;
	}
	case Drag::LegendBar: {
		/* the thumb follows the mouse: its free travel spans the whole scroll */
		const LegendLayout legend = legendLayout(plot);
		const double travel = legendTrack(legend).width() - legendThumb(legend, 0).width();
		if (travel > 0) scrollLegendTo(dragStartScroll_ + (pos.x() - dragStartX_) / travel * legend.maxScroll(), legend);
		break;
	}
	case Drag::None: {
		const LegendLayout legend = legendLayout(plot);
		const double offset = legendOffset(legend);
		const bool onLegendBar = legend.maxScroll() > 0
				&& (legendTrack(legend).adjusted(0, -LEGEND_BAR_GRIP, 0, LEGEND_BAR_GRIP).contains(pos)
					|| (offset > 0 && legendArrow(legend, false).contains(pos))
					|| (offset < legend.maxScroll() && legendArrow(legend, true).contains(pos)));
		/* lanes: the scroll bar, a unit name (folds) and a folded strip (opens) take a click */
		const int lane = lanes_ && pos.y() >= plot.top() && pos.y() <= plot.bottom() && pos.x() <= plot.right()
				? laneAtY(pos.y()) : -1;
		const bool onLanes = lane >= 0 && laneVisible(lanesShown_[lane].axes.rect, plot).contains(QPointF(plot.left(), pos.y()))
				&& (lanesShown_[lane].folded || pos.x() < LANE_UNIT_W);
		const bool onLaneBar = laneScrollBarRect().adjusted(-LANE_BAR_GRIP, 0, LANE_BAR_GRIP, 0).contains(pos);
		hoverMenu_ = onLanes ? laneMenuButtonAt(pos) : -1; /* the menu button highlighted, not the fold's */
		hoverLane_ = onLanes && hoverMenu_ < 0 ? lane : -1; /* its button drawn highlighted */
		hoverBar_ = onLaneBar;
		hoverSeparator_ = separatorAt(pos); /* a drag there resizes: lit, and the resize cursor */
		if (hoverSeparator_ >= 0) {
			setCursor(Qt::SizeVerCursor);
			break;
		}
		setCursor(overviewRect().contains(pos) || onLegendBar || onLanes || onLaneBar ? Qt::PointingHandCursor
				: noteAtPoint(pos) >= 0 ? Qt::SizeHorCursor
				: trigger_.on && std::isfinite(triggerLineY_) && std::fabs(pos.y() - triggerLineY_) <= 4
						&& plot.contains(pos) ? Qt::SizeVerCursor
				: plot.contains(pos) ? (cursorMode_ ? Qt::SizeHorCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
		break;
	}
	}
	refresh();
}

void ChartView::mouseReleaseEvent(QMouseEvent *) {
	const bool cursorLetGo = draggingCursor(), noteLetGo = drag_ == Drag::Note, levelLetGo = drag_ == Drag::Level;
	const bool borderLetGo = drag_ == Drag::LaneBorder;
	drag_ = Drag::None;
	if (borderLetGo) {
		dragSeparator_ = -1;
		emit laneHeightsChanged();
		return; /* the resize cursor stays while the mouse is on the separator */
	}
	if (noteLetGo) emit notesChanged();
	if (levelLetGo) emit triggerLevelChanged(trigger_.level);
	setCursor(cursorMode_ ? Qt::SizeHorCursor : Qt::OpenHandCursor);
	if (cursorLetGo) emit cursorsChanged(); /* measured in full now: while dragged, A and B alone followed it */
}

/* the wheel zooms the time; with Ctrl, the values; over a legend wider than
 * its row, it scrolls the legend */
void ChartView::wheelEvent(QWheelEvent *e) {
	if (wheelLegend(e)) return;
	const double notches = e->angleDelta().y() / 120.0;
	if (notches == 0) return;
	/* lanes: over their value labels (or the scroll bar), the wheel scrolls them; with Ctrl, it stays the lane's zoom */
	const QRectF plot = plotRect();
	const QPointF pos = e->position();
	if (lanes_ && !(e->modifiers() & Qt::ControlModifier) && pos.y() >= plot.top() && pos.y() <= plot.bottom()
			&& (pos.x() < plot.left() || pos.x() > plot.right())) {
		scrollLanesTo(laneScroll() - notches * LANE_WHEEL_STEP);
		e->accept();
		return;
	}
	const double factor = std::pow(ZOOM_STEP, -notches); /* up: zoom in */
	if (e->modifiers() & Qt::ControlModifier) {
		zoomY(factor, e->position().y());
		if (lanes_) emit laneYChanged();
		else emit yChangedByUser();
	} else {
		zoomTime(factor, e->position().x());
		emit windowChangedByUser(window_);
	}
	refresh();
	e->accept();
}

void ChartView::zoomTime(double factor, double mouseX) {
	const double newWindow = std::clamp(window_ * factor, MIN_WINDOW, memory_);
	if (live_) { /* live: the right edge stays at now */
		window_ = newWindow;
		return;
	}
	/* held: zoom around the time under the mouse */
	const QRectF plot = plotRect();
	const double at = timeAtX(std::clamp(mouseX, plot.left(), plot.right()));
	const double fraction = (at - (viewEnd_ - window_)) / window_;
	window_ = newWindow;
	holdAt(at + (1 - fraction) * newWindow);
}

/* around the value under the mouse; the Y range becomes Manual */
void ChartView::zoomY(double factor, double mouseY) {
	const QRectF plot = plotRect();
	/* lanes: the lane under the mouse */
	const int lane = laneAtY(mouseY);
	if (lanes_ && (lane < 0 || lanesShown_[lane].folded)) return;
	YScale &scale = lane >= 0 ? scaleOf(lanesShown_[lane]) : y_;
	const QRectF rect = lane >= 0 ? lanesShown_[lane].axes.rect : plot;
	if (lane >= 0 && scale.autoRange) { /* from what the lane shows now */
		scale.lo = lanesShown_[lane].axes.lo;
		scale.hi = lanesShown_[lane].axes.hi;
	}
	const double y = std::clamp(mouseY, rect.top(), rect.bottom());
	const bool log = logOf(scale) && scale.lo > 0 && scale.hi > scale.lo; /* Log: around the value under the mouse in decades */
	const double lo = toScale(scale.lo, log), hi = toScale(scale.hi, log);
	const double at = hi - (y - rect.top()) / rect.height() * (hi - lo);
	scale.autoRange = false;
	scale.lo = fromScale(at - (at - lo) * factor, log);
	scale.hi = fromScale(at + (hi - at) * factor, log);
}

void ChartView::mouseDoubleClickEvent(QMouseEvent *e) {
	const int note = noteAtPoint(e->position());
	if (note >= 0) { /* a note's tag: its text edited (the Chart tab asks) */
		emit noteEditRequested(note);
		return;
	}
	if (separatorAt(e->position()) >= 0) { /* a separator: every lane its equal share again */
		resetLaneHeights();
		return;
	}
	if (lanes_) { /* the lane under the mouse: Auto (not after a click on the bar, a unit name or a strip) */
		const int lane = laneAtY(e->position().y());
		if (!pressedLanes_ && lane >= 0 && !laneFolded(lane)) setLaneYAuto(lane);
		return;
	}
	setYAuto();
	emit yChangedByUser();
}

void ChartView::contextMenuEvent(QContextMenuEvent *e) {
	const QRectF plot = plotRect();
	/* on a line's chip: that line's menu */
	const int chip = chipAt(e->pos());
	if (chip >= 0) {
		emit lineMenuRequested(chip, e->globalPos());
		e->accept();
		return;
	}
	/* on a lane's value labels, or anywhere on a folded strip: the lane's menu */
	const int under = lanes_ && e->pos().y() >= plot.top() && e->pos().y() <= plot.bottom() ? laneAtY(e->pos().y()) : -1;
	if (under >= 0 && (e->pos().x() < plot.left() || (lanesShown_[under].folded && e->pos().x() <= plot.right()))) {
		emit laneMenuRequested(under, e->globalPos());
		e->accept();
		return;
	}
	emit menuRequested(e->globalPos(), timeAtX(std::clamp(double(e->pos().x()), plot.left(), plot.right())));
	e->accept();
}

void ChartView::keyPressEvent(QKeyEvent *e) {
	if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && selectedNote_ >= 0) {
		removeNote(selectedNote_);
		e->accept();
		return;
	}
	QWidget::keyPressEvent(e);
}

/* A press on the legend's scroll bar or its arrows, when the chips overflow:
 * an arrow scrolls half a row that way; on the bar, the thumb comes under the
 * mouse and then follows it. True if the press was the legend's. */
bool ChartView::pressLegend(const QPointF &pos) {
	const LegendLayout legend = legendLayout(plotRect());
	if (legend.maxScroll() <= 0) return false;
	const double offset = legendOffset(legend);
	const double half = legend.viewport.width() / 2;
	if (offset > 0 && legendArrow(legend, false).contains(pos)) {
		scrollLegendTo(offset - half, legend);
		return true;
	}
	if (offset < legend.maxScroll() && legendArrow(legend, true).contains(pos)) {
		scrollLegendTo(offset + half, legend);
		return true;
	}
	if (!legendTrack(legend).adjusted(0, -LEGEND_BAR_GRIP, 0, LEGEND_BAR_GRIP).contains(pos)) return false;
	const QRectF thumb = legendThumb(legend, offset);
	if (pos.x() < thumb.left() || pos.x() > thumb.right()) {
		const double travel = legendTrack(legend).width() - thumb.width();
		const double at = (pos.x() - thumb.width() / 2 - legendTrack(legend).left()) / travel;
		scrollLegendTo(at * legend.maxScroll(), legend);
	}
	drag_ = Drag::LegendBar;
	dragStartX_ = pos.x();
	dragStartScroll_ = legendOffset(legend);
	return true;
}

/* The wheel over the legend's row scrolls the chips when they overflow (a
 * sideways wheel too); otherwise the wheel zooms, as over the plot. */
bool ChartView::wheelLegend(QWheelEvent *e) {
	if (e->position().y() >= LEGEND_H) return false;
	const LegendLayout legend = legendLayout(plotRect());
	if (legend.maxScroll() <= 0) return false;
	const QPoint delta = e->angleDelta();
	const double notches = (delta.x() != 0 ? delta.x() : delta.y()) / 120.0;
	scrollLegendTo(legendOffset(legend) - notches * LEGEND_WHEEL_STEP, legend);
	e->accept();
	return true;
}

void ChartView::scrollLegendTo(double pixels, const LegendLayout &legend) {
	legendScroll_ = std::clamp(pixels, 0.0, legend.maxScroll());
	refresh();
}

/* ---------------------------------------------------------------- the legend */

/* every chip at its fixed place: the dot, the name, room for the widest value
 * (right-aligned in it), then the unit. The widths are measured once while the
 * lines and the font stay: 64 names measured at every frame and at every mouse
 * move (the pointer's shape) held the chart near 52 frames a second */
ChartView::LegendLayout ChartView::legendLayout(const QRectF &plot) const {
	const QFont font = labelFont();
	if (chipsGeneration_ != seriesGeneration_ || chipsFont_ != font.key()) {
		const QFontMetricsF metrics(font);
		chipValueRoom_ = metrics.horizontalAdvance(widestChartNumber());
		const double nameGap = metrics.horizontalAdvance(QStringLiteral("  "));
		chipWidths_.clear();
		for (const Series &s : series_) {
			const double unit = s.unit.isEmpty() ? 0 : metrics.horizontalAdvance(QLatin1Char(' ') + s.unit);
			chipWidths_ << CHIP_TEXT_LEFT + metrics.horizontalAdvance(s.name) + nameGap + chipValueRoom_ + unit
					+ CHIP_PAD_RIGHT;
		}
		chipsGeneration_ = seriesGeneration_;
		chipsFont_ = font.key();
		chipMeasures_++;
	}
	/* the chips end where the state's text begins, so neither lies under the other */
	int variant = -1;
	double stateWidth = 0;
	fitState(plot.width(), variant, stateWidth);
	LegendLayout legend;
	legend.viewport = QRectF(plot.left(), LEGEND_TOP,
			std::max(0.0, plot.width() - (variant >= 0 ? stateWidth + STATE_GAP : 0)), LEGEND_ROW_H);
	legend.valueRoom = chipValueRoom_;
	double x = legend.viewport.left();
	for (const double w : std::as_const(chipWidths_)) {
		legend.chips.push_back(QRectF(x, LEGEND_TOP, w, LEGEND_ROW_H));
		x += w + CHIP_GAP;
	}
	legend.content = legend.chips.isEmpty() ? 0 : x - CHIP_GAP - legend.viewport.left();
	return legend;
}

/* the scroll within what the chips need now (the window may have grown, or a
 * line gone, since it was set) */
double ChartView::legendOffset(const LegendLayout &legend) const {
	return std::clamp(legendScroll_, 0.0, legend.maxScroll());
}

QRectF ChartView::legendTrack(const LegendLayout &legend) {
	return QRectF(legend.viewport.left(), LEGEND_BAR_Y, legend.viewport.width(), LEGEND_BAR_H);
}

/* the thumb: as wide as the share of the chips in view, where the scroll is */
QRectF ChartView::legendThumb(const LegendLayout &legend, double offset) {
	const QRectF track = legendTrack(legend);
	const double shown = legend.content > 0 ? std::min(1.0, legend.viewport.width() / legend.content) : 1;
	const double w = std::min(track.width(), std::max(LEGEND_THUMB_MIN, track.width() * shown));
	const double at = legend.maxScroll() > 0 ? offset / legend.maxScroll() : 0;
	return QRectF(track.left() + at * (track.width() - w), track.top(), w, track.height());
}

/* the mark at either end of the chips' row, shown when more chips lie that way */
QRectF ChartView::legendArrow(const LegendLayout &legend, bool right) {
	const QRectF &v = legend.viewport;
	return QRectF(right ? v.right() - LEGEND_ARROW_W : v.left(), v.top(), LEGEND_ARROW_W, v.height());
}

QVector<QRectF> ChartView::legendChips() const {
	const LegendLayout legend = legendLayout(plotRect());
	const double offset = legendOffset(legend);
	QVector<QRectF> chips;
	for (const QRectF &chip : legend.chips) chips.push_back(chip.translated(-offset, 0));
	return chips;
}

QRectF ChartView::legendViewport() const {
	return legendLayout(plotRect()).viewport;
}

double ChartView::legendScroll() const {
	return legendOffset(legendLayout(plotRect()));
}

void ChartView::setLegendScroll(double pixels) {
	scrollLegendTo(pixels, legendLayout(plotRect()));
}

QString ChartView::legendValue(int key) const {
	const auto it = series_.constFind(key);
	return it != series_.constEnd() && it->hasShown ? chartNumber(it->shown) : QString();
}

/* ------------------------------------------------------------------ drawing */

void ChartView::paintEvent(QPaintEvent *) {
	QPainter p(this);
	/* to the window (its backing store), not a picture of the chart (grab) */
	QBackingStore *store = QWidget::window()->backingStore();
	const bool onScreen = store && p.paintEngine() && p.paintEngine()->paintDevice() == store->paintDevice();
	paintFrame(p, onScreen);
}

/* The chart hidden (another tab): the card's layer stays over the window until the window has painted what is there
 * now, all of it (the tabs' area, else the window), then goes, both at the same refresh of the screen. Taken away
 * at once, the window's old pixels showed there until it had painted the new tab (a tenth of a second at 4K); with
 * only the layer's part painted first, the new tab showed through the plot for a frame. */
void ChartView::hideEvent(QHideEvent *e) {
	if (plotOnCard())
		QTimer::singleShot(0, this, [this] {
			if (!plotOnCard() || isVisible()) return;
			QWidget *area = QWidget::window();
			for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
				if (qobject_cast<QStackedWidget *>(w)) {
					area = w;
					break;
				}
			}
			if (area->isVisible()) area->repaint();
			showLayer(false);
		});
	QWidget::hideEvent(e);
}

void ChartView::paintFrame(QPainter &p, bool onScreen) {
	QElapsedTimer paintTimer;
	paintTimer.start();
	const double frameDt = std::clamp(frameClock_.nsecsElapsed() / 1e9, 0.0, MAX_FRAME_DT);
	frameClock_.restart();
	updateDelay(frameDt);

	Axes axes;
	axes.rect = plotRect();
	axes.t1 = lastViewEnd_ = viewEnd();
	axes.t0 = axes.t1 - window_;
	axes.span = window_;
	axes.columns = std::max(1.0, axes.rect.width());
	QElapsedTimer stage; /* the timing aid's stages (perf_) */
	stage.start();
	const QVector<BinnedLine> binned = viewBins(axes);
	perf_.bin += stage.nsecsElapsed() / 1e6;
	/* the plots: all of it, or the lanes, each with its own Y range on the same times */
	QVector<Lane> plots = plotLayout();
	for (Lane &plot : plots) {
		const QRectF rect = plot.axes.rect;
		plot.axes = axes;
		plot.axes.rect = rect;
		YScale &scale = scaleOf(plot);
		double lo, hi;
		updateYRange(scale, binned, plot.lines, frameDt, lo, hi);
		plot.axes.setRange(lo, hi, logOf(scale));
	}
	axes.setRange(plots.first().axes.lo, plots.first().axes.hi, plots.first().axes.log);
	if (lanes_) { /* the scroll as drawn: within what there is to scroll now */
		const double content = plots.last().axes.rect.bottom() - plots.first().axes.rect.top();
		laneScroll_ = std::clamp(laneScroll_, 0.0, std::max(0.0, content - axes.rect.height()));
	}
	lanesShown_ = plots;
	lastAxes_ = plots.first().axes;

	/* On a card (on the screen, with lines): the plot (grid, lines, cursors, crosshair) is drawn by it into its layer
	 * over the window, and the chart paints what is around it. While the layer is not shown yet (the chart shown
	 * again, a card opened), the card's frame is painted under it too (read back), and the layer is shown after the
	 * second such frame is on the window (the first is surely done on the card by then; one not done yet when the
	 * screen shows the layer would leave its old frame there): neither the window's old pixels nor the layer's old
	 * frame show. The other way (no line left), the CPU draws all of it, and the layer goes once that is on the window:
	 * the whole chart painted again first, the plot's part too (a frame painted around the plot alone, as while the
	 * layer is there, left the old lines in the window, which showed for a frame where the layer had been). */
	stage.restart();
	const double cardBefore = perf_.segments + perf_.present;
	const bool onCard = onScreen && gpu_ && !series_.isEmpty() && plotOnGpu(axes, plots, binned);
	if (onCard) perf_.marks += stage.nsecsElapsed() / 1e6 - (perf_.segments + perf_.present - cardBefore);
	QImage under;
	if (onCard && !gpu_->shown()) {
		under = gpu_->lastPicture();
		framesUnder_++;
	}
	drawCard(p);
	drawGrid(p, plots, axes, !onCard);
	if (!onCard) {
		drawCursorSpan(p, axes);
		stage.restart();
		if (!live_ && lineReuse_) {
			drawLinesPicture(p, plots, binned); /* held: drawn again only when they change */
		} else {
			drawLines(p, plots, binned);
			lineBuilds_++;
		}
		perf_.lines += stage.nsecsElapsed() / 1e6;
		stage.restart();
		/* the marks' lines, the folded strips over them, then the marks' tags (as the card: its pictures over all) */
		drawNotes(p, axes, Marks::Lines);
		drawTrigger(p, plots, binned, Marks::Lines);
		drawCursors(p, axes, Marks::Lines);
		drawFolded(p, plots);
		drawNotes(p, axes, Marks::Tags);
		drawTrigger(p, plots, binned, Marks::Tags);
		drawCursors(p, axes, Marks::Tags);
		perf_.marks += stage.nsecsElapsed() / 1e6;
	} else if (!under.isNull()) {
		under.setDevicePixelRatio(devicePixelRatioF());
		p.drawImage((QPointF(layerPixels_.topLeft()) - layerOrigin_) / devicePixelRatioF(), under);
	}
	drawLaneBar(p); /* in the right pad, outside the card's layer */
	stage.restart();
	drawMemoryStrip(p, axes);
	perf_.strip += stage.nsecsElapsed() / 1e6;
	stage.restart();
	drawLegend(p, axes);
	perf_.legend += stage.nsecsElapsed() / 1e6;
	stage.restart();
	if (!onCard) drawCrosshair(p, axes, plots, binned);
	perf_.marks += stage.nsecsElapsed() / 1e6;
	drawState(p, axes);
	if (onScreen && gpu_ && onCard != gpu_->shown() && (!onCard || framesUnder_ >= LAYER_AFTER_FRAMES))
		QTimer::singleShot(0, this, [this, shown = onCard] { /* after this frame is on the window */
			if (!gpu_ || gpu_->shown() == shown || (shown && (!isVisible() || series_.isEmpty()))) return;
			if (!shown && isVisible()) repaint(); /* taken away: all of the chart on the window first */
			showLayer(shown);
		});
	const double paintMs = paintTimer.nsecsElapsed() / 1e6;
	budget_.spent(paintMs);
	updatePaintStats(paintMs);
	paints_++;
	perf_.frames++;
	perf_.paintSum += paintMs;
	perf_.paintMax = std::max(perf_.paintMax, paintMs);
}

ChartView::PerfStats ChartView::takePerfStats() {
	const PerfStats taken = perf_;
	perf_ = PerfStats();
	return taken;
}

/* Binning a held view again gives the same bins while its times and columns, the lines and each line's samples in
 * view (as absolute numbers: samples are only added at the end and let go at the start) are the same: then the last
 * binning is used again. A live view moves at every frame and is binned (its complete columns kept, binViewSeries). */
QVector<ChartView::BinnedLine> ChartView::viewBins(const Axes &axes) {
	QVector<double> key{ axes.t0, axes.t1, axes.columns, double(seriesGeneration_) };
	key.reserve(4 + 2 * series_.size());
	for (const Series &s : series_) {
		if (s.times.isEmpty()) {
			key << -1 << -1;
			continue;
		}
		/* the samples binViewSeries takes: one past each end of the view */
		const qsizetype i0 = std::max<qsizetype>(0, std::lower_bound(s.times.begin(), s.times.end(), axes.t0) - s.times.begin() - 1);
		const qsizetype i1 = std::min<qsizetype>(s.times.size(),
				std::upper_bound(s.times.begin(), s.times.end(), axes.t1) - s.times.begin() + 1);
		key << double(s.dropped + i0) << double(s.dropped + i1);
	}
	if (lineReuse_ && key == lastBinKey_ && lastBinned_.size() == series_.size()) return lastBinned_;
	lastBinned_ = binView(axes);
	lastBinKey_ = key;
	binnings_++;
	binnedVersion_++;
	perf_.binnings++;
	return lastBinned_;
}

QString ChartView::linesKey(const QVector<Lane> &plots, qreal dpr) const {
	QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(binnedVersion_).arg(dpr).arg(normalized_).arg(Theme::isDark())
			.arg(lanes_);
	for (const Lane &plot : plots)
		key += QStringLiteral("|%1,%2,%3,%4,%5,%6,%7,%8").arg(plot.axes.rect.x()).arg(plot.axes.rect.y())
				.arg(plot.axes.rect.width()).arg(plot.axes.rect.height()).arg(plot.axes.lo, 0, 'g', 17)
				.arg(plot.axes.hi, 0, 'g', 17).arg(plot.axes.log).arg(plot.folded);
	return key;
}

/* The lines as a picture on whole device pixels (as a stripe of drawLines is: the same pixels), drawn again only
 * when they change; between, a cursor dragged over a held view costs a copy of it */
void ChartView::drawLinesPicture(QPainter &p, const QVector<Lane> &plots, const QVector<BinnedLine> &lines) {
	const qreal dpr = p.device()->devicePixelRatioF();
	const QRectF plotArea = plotRect();
	QRectF all;
	for (const Lane &plot : plots) {
		const QRectF shown = lanes_ ? laneVisible(plot.axes.rect, plotArea) : plot.axes.rect;
		if (!plot.folded && !shown.isEmpty()) all = all.united(shown);
	}
	if (all.isEmpty()) return;
	const QRect device = p.deviceTransform().mapRect(all.adjusted(-2, -2, 2, 2)).toAlignedRect();
	const QString key = linesKey(plots, dpr) + QStringLiteral("|%1,%2,%3,%4").arg(device.x()).arg(device.y())
			.arg(device.width()).arg(device.height());
	if (key != linesPictureKey_ || linesPicture_.isNull()) {
		linesPictureKey_ = key;
		lineBuilds_++;
		QTransform world;
		prepareTile(p, device, linesPicture_, world, linesAt_);
		QPainter ip(&linesPicture_);
		ip.setRenderHint(QPainter::Antialiasing);
		ip.setWorldTransform(world);
		drawLines(ip, plots, lines, dpr);
		ip.end();
		linesPicture_.setDevicePixelRatio(dpr);
	}
	p.drawImage(linesAt_, linesPicture_);
}

/* A line's samples from t0 to t1, one bin per pixel column on absolute time,
 * with the min / max of what lies inside [t0, t1]. With many samples per column
 * it bins min/max chunks instead, of the largest level that is at most half a
 * column (a whole one on the overview), smaller ones up to where those start,
 * so the cost follows the pixels, not the samples: a long view with millions
 * of samples costs what a short one does. */
void ChartView::binSeries(const Series &s, double t0, double t1, double columns, bool overview,
		BinnedLine &out) const {
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
}

void ChartView::binRange(const Series &s, qsizetype i0, qsizetype i1, double columnSeconds, int top, QVector<Bin> &bins) {
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
void ChartView::binViewSeries(const Series &s, double t0, double t1, double columns, BinnedLine &out) const {
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
	const bool holds = kept.columnSeconds == columnSeconds && kept.generation == seriesGeneration_
			&& kept.binnedTo >= s.dropped + i0 && kept.binnedTo <= s.dropped + i1
			&& (kept.bins.isEmpty() ? kept.binnedTo == s.dropped + i0 : kept.bins.first().column <= firstColumn);
	if (!holds) {
		kept = {};
		kept.columnSeconds = columnSeconds;
		kept.generation = seriesGeneration_;
		kept.binnedTo = s.dropped + i0;
	}
	const double perColumn = double(i1 - i0) / columns;
	int top = LEVELS - 1; /* the largest level that fits half a column, as binSeries */
	while (top >= 0 && perColumn <= 2.0 * CHUNK_SIZE[top]) top--;
	QVector<Bin> fresh;
	binRange(s, kept.binnedTo - s.dropped, i1, columnSeconds, top, fresh);
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
}

/* every line binned for the view, in the order of series_, on the chart's threads */
QVector<ChartView::BinnedLine> ChartView::binView(const Axes &axes) const {
	QVector<const Series *> lines;
	lines.reserve(series_.size());
	for (const Series &s : series_) lines << &s;
	QVector<BinnedLine> binned(lines.size());
	inParallel(lines.size(), [&](qsizetype i) { binViewSeries(*lines[i], axes.t0, axes.t1, axes.columns, binned[i]); });
	return binned;
}

/* The Y range of this frame for a plot. Normalize: 0..1 with the margins, each line scaled
 * into it by its own range. Auto: follows the lines. Manual: as set. */
void ChartView::updateYRange(YScale &scale, const QVector<BinnedLine> &lines, const QVector<int> &which, double frameDt,
		double &lo, double &hi) {
	if (normalized_) {
		lo = -Y_MARGIN;
		hi = 1 + Y_MARGIN;
		return;
	}
	if (scale.autoRange) followData(scale, lines, which, frameDt);
	lo = scale.lo;
	hi = scale.hi;
}

void ChartView::forgetRanges() {
	y_.initialized = false;
	for (YScale &scale : laneScales_) scale.initialized = false;
}

/* The plots of a frame. Without lanes: the whole plot, every line. With: a lane per unit in the order the lines came,
 * stacked LANE_GAP apart: a folded one LANE_FOLDED_H high, the open ones sharing the rest equally but never lower than
 * LANE_MIN_H; when they do not fit, they go on below the plot and scroll (laneScroll_, clamped here too). */
QVector<ChartView::Lane> ChartView::plotLayout() const {
	QVector<Lane> plots;
	const QRectF plot = plotRect();
	if (!lanes_ || series_.isEmpty()) {
		Lane all;
		all.axes.rect = plot;
		for (int i = 0; i < series_.size(); i++) all.lines << i;
		plots << all;
		return plots;
	}
	QHash<QString, int> laneOfUnit;
	int i = 0;
	for (const Series &s : series_) {
		auto it = laneOfUnit.find(s.unit);
		if (it == laneOfUnit.end()) {
			Lane lane;
			lane.key = s.unit;
			lane.label = s.unit;
			lane.folded = lanesFolded_.contains(s.unit);
			plots << lane;
			it = laneOfUnit.insert(s.unit, int(plots.size() - 1));
		}
		plots[*it].lines << i++;
	}
	QVector<double> heights;
	double unit;
	laneHeights(plots, plot.height(), heights, unit);
	double content = LANE_GAP * double(plots.size() - 1);
	for (double height : std::as_const(heights)) content += height;
	double y = plot.top() - std::clamp(laneScroll_, 0.0, std::max(0.0, content - plot.height()));
	for (qsizetype k = 0; k < plots.size(); k++) {
		plots[k].axes.rect = QRectF(plot.left(), y, plot.width(), heights[k]);
		y += heights[k] + LANE_GAP;
	}
	return plots;
}

/* The open lanes share what the folded ones and the gaps leave by their weights (a lane dragged taller has a weight
 * over 1, the one below it under), as equal shares do when all are 1; when that is under LANE_MIN_H each, the equal
 * share is LANE_MIN_H and the lanes go on below the plot (scrolled) */
void ChartView::laneHeights(const QVector<Lane> &lanes, double plotHeight, QVector<double> &heights, double &unit) const {
	int folded = 0;
	double weights = 0;
	for (const Lane &lane : lanes) {
		if (lane.folded) folded++;
		else weights += laneWeights_.value(lane.key, 1.0);
	}
	const int open = int(lanes.size()) - folded;
	const double gaps = LANE_GAP * std::max<qsizetype>(0, lanes.size() - 1);
	const double share = open > 0 ? std::max(LANE_MIN_H, (plotHeight - gaps - folded * LANE_FOLDED_H) / open) : 0;
	unit = open > 0 && weights > 0 ? share * open / weights : share;
	heights.resize(lanes.size());
	for (qsizetype k = 0; k < lanes.size(); k++)
		heights[k] = lanes[k].folded ? LANE_FOLDED_H : std::max(LANE_MIN_H, unit * laneWeights_.value(lanes[k].key, 1.0));
}

QStringList ChartView::laneHeights() const {
	QStringList texts;
	for (auto it = laneWeights_.begin(); it != laneWeights_.end(); ++it)
		texts << it.key() + QLatin1Char('\t') + QString::number(it.value(), 'g', 6);
	texts.sort();
	return texts;
}

void ChartView::setLaneHeights(const QStringList &texts) {
	laneWeights_.clear();
	for (const QString &text : texts) {
		const QStringList f = text.split(QLatin1Char('\t'));
		bool ok = false;
		const double weight = f.size() == 2 ? f[1].toDouble(&ok) : 0;
		if (ok && weight > 0 && std::isfinite(weight)) laneWeights_.insert(f[0], weight);
	}
	refresh();
}

void ChartView::resetLaneHeights() {
	if (laneWeights_.isEmpty()) return;
	laneWeights_.clear();
	lanesShown_ = plotLayout();
	emit laneHeightsChanged();
	refresh();
}

/* a separator takes a drag a few pixels either side of its line, from the value labels across the plot, when the lanes
 * above and below it are open */
int ChartView::separatorAt(const QPointF &pos) const {
	if (!lanes_ || lanesShown_.size() < 2) return -1;
	const QRectF plot = plotRect();
	if (pos.x() < LANE_UNIT_W || pos.x() > plot.right()) return -1;
	QVector<int> gaps;
	const QVector<double> ys = separatorsY(lanesShown_, &gaps);
	for (qsizetype i = 0; i < ys.size(); i++) {
		const int k = gaps[i];
		if (std::fabs(pos.y() - ys[i]) <= LANE_GAP / 2 - 1 && !lanesShown_[k].folded && !lanesShown_[k + 1].folded)
			return k;
	}
	return -1;
}

double ChartView::laneContentHeight() const {
	if (!lanes_ || series_.isEmpty()) return 0;
	const QVector<Lane> plots = plotLayout();
	return plots.last().axes.rect.bottom() - plots.first().axes.rect.top();
}

double ChartView::maxLaneScroll() const { return std::max(0.0, laneContentHeight() - plotRect().height()); }

double ChartView::laneScroll() const { return std::clamp(laneScroll_, 0.0, maxLaneScroll()); }

void ChartView::setLaneScroll(double pixels) { scrollLanesTo(pixels); }

void ChartView::scrollLanesTo(double pixels) {
	const double to = std::clamp(pixels, 0.0, maxLaneScroll());
	if (to == laneScroll_) return;
	laneScroll_ = to;
	refresh();
}

QRectF ChartView::laneVisible(const QRectF &lane, const QRectF &plot) {
	const double top = std::max(lane.top(), plot.top()), bottom = std::min(lane.bottom(), plot.bottom());
	return bottom > top ? QRectF(lane.left(), top, lane.width(), bottom - top) : QRectF();
}

/* an open lane's buttons at the top of its unit column, in its part in view: the fold button ("▾"), and under it the
 * lane's menu ("⋯") when there is room for it and some of the unit name */
void ChartView::laneButtons(const QRectF &shown, QRectF *fold, QRectF *menu) {
	*fold = QRectF(0, shown.top() + 1, LANE_UNIT_W, std::min(LANE_BUTTON_H, shown.height() - 1));
	const double top = fold->bottom() + LANE_BUTTON_GAP;
	*menu = shown.bottom() - top >= LANE_BUTTON_H + LANE_NAME_MIN ? QRectF(0, top, LANE_UNIT_W, LANE_BUTTON_H) : QRectF();
}

/* the track: as tall as the plot, in the right pad; the handle: the plot's share of the lanes, where the scroll is */
QRectF ChartView::laneScrollBarRect() const {
	const QRectF plot = plotRect();
	if (!lanes_ || laneContentHeight() <= plot.height()) return QRectF();
	return QRectF(plot.right() + LANE_BAR_X, plot.top(), LANE_BAR_W, plot.height());
}

QRectF ChartView::laneScrollHandleRect() const {
	const QRectF track = laneScrollBarRect();
	if (track.isEmpty()) return QRectF();
	const double content = laneContentHeight(), most = content - track.height();
	const double height = std::max(LANE_HANDLE_MIN, track.height() * track.height() / content);
	return QRectF(track.left(), track.top() + laneScroll() / most * (track.height() - height), track.width(), height);
}

int ChartView::laneAtY(double y) const {
	if (!lanes_ || lanesShown_.isEmpty()) return -1;
	const QRectF plot = plotRect();
	int nearest = -1;
	double best = std::numeric_limits<double>::max();
	for (int k = 0; k < lanesShown_.size(); k++) {
		const QRectF r = laneVisible(lanesShown_[k].axes.rect, plot); /* a lane out of view is nowhere */
		if (r.isEmpty()) continue;
		const double d = y < r.top() ? r.top() - y : y > r.bottom() ? y - r.bottom() : 0;
		if (d < best) {
			best = d;
			nearest = k;
		}
	}
	return nearest;
}

bool ChartView::laneFolded(int lane) const {
	const QVector<Lane> plots = plotLayout();
	return lanes_ && lane >= 0 && lane < plots.size() && plots[lane].folded;
}

void ChartView::setLaneFolded(int lane, bool folded) {
	const QString key = laneKey(lane);
	if (!lanes_ || lane < 0 || lane >= laneCount() || lanesFolded_.contains(key) == folded) return;
	if (folded) lanesFolded_.insert(key);
	else lanesFolded_.remove(key);
	lanesShown_ = plotLayout(); /* the mouse finds the lanes where they are now, before the next frame */
	emit laneFoldsChanged();
	refresh();
}

QStringList ChartView::foldedLanes() const {
	QStringList units(lanesFolded_.begin(), lanesFolded_.end());
	units.sort();
	return units;
}

void ChartView::setFoldedLanes(const QStringList &units) {
	lanesFolded_ = QSet<QString>(units.begin(), units.end());
	refresh();
}

QString ChartView::foldedText(int lane) const { return laneFolded(lane) ? foldedTexts_.value(laneKey(lane)) : QString(); }

QRectF ChartView::laneFoldButtonRect(int lane) const {
	const QVector<Lane> plots = plotLayout();
	if (!lanes_ || lane < 0 || lane >= plots.size()) return QRectF();
	const QRectF shown = laneVisible(plots[lane].axes.rect, plotRect());
	if (shown.isEmpty()) return QRectF();
	if (plots[lane].folded) return QRectF(0, shown.top(), LANE_UNIT_W, shown.height());
	QRectF fold, menu;
	laneButtons(shown, &fold, &menu);
	return fold;
}

QRectF ChartView::laneMenuButtonRect(int lane) const {
	const QVector<Lane> plots = plotLayout();
	if (!lanes_ || lane < 0 || lane >= plots.size() || plots[lane].folded) return QRectF();
	const QRectF shown = laneVisible(plots[lane].axes.rect, plotRect());
	if (shown.isEmpty()) return QRectF();
	QRectF fold, menu;
	laneButtons(shown, &fold, &menu);
	return menu;
}

/* the open lane whose menu button is at pos, of the lanes as last painted; -1: none */
int ChartView::laneMenuButtonAt(const QPointF &pos) const {
	const QRectF plot = plotRect();
	if (!lanes_ || pos.x() >= LANE_UNIT_W || pos.y() < plot.top() || pos.y() > plot.bottom()) return -1;
	const int lane = laneAtY(pos.y());
	if (lane < 0 || lanesShown_[lane].folded) return -1;
	const QRectF shown = laneVisible(lanesShown_[lane].axes.rect, plot);
	if (shown.isEmpty()) return -1;
	QRectF fold, menu;
	laneButtons(shown, &fold, &menu);
	return menu.contains(pos) ? lane : -1;
}

int ChartView::foldedLaneCount() const {
	if (!lanes_ || series_.isEmpty()) return 0;
	int folded = 0;
	for (const Lane &lane : plotLayout()) folded += lane.folded ? 1 : 0;
	return folded;
}

void ChartView::setAllLanesFolded(bool folded) {
	if (!lanes_ || series_.isEmpty()) return;
	bool changed = false;
	for (const Lane &lane : plotLayout()) {
		if (lane.folded == folded) continue;
		if (folded) lanesFolded_.insert(lane.key);
		else lanesFolded_.remove(lane.key);
		changed = true;
	}
	if (!changed) return;
	lanesShown_ = plotLayout();
	emit laneFoldsChanged();
	refresh();
}

/* where the lanes take a click or the wheel, what it does: the fold button and the unit name fold, a strip opens,
 * the value labels scroll (when the lanes do not fit), zoom and have the lane's menu */
QString ChartView::toolTipAt(const QPointF &pos) const {
	if (stateRect_.contains(pos)) return stateFull_; /* the state corner: its whole text */
	const QRectF plot = plotRect();
	if (laneScrollBarRect().adjusted(-LANE_BAR_GRIP, 0, LANE_BAR_GRIP, 0).contains(pos))
		return tr("Scroll the lanes: drag the handle, or click above or below it for a page");
	if (separatorAt(pos) >= 0) return tr("Drag: this lane's height · Double-click: equal heights");
	if (!lanes_ || pos.y() < plot.top() || pos.y() > plot.bottom() || pos.x() > plot.right()) return QString();
	const int lane = laneAtY(pos.y());
	if (lane < 0) return QString();
	const QRectF shown = laneVisible(lanesShown_[lane].axes.rect, plot);
	if (pos.y() < shown.top() || pos.y() > shown.bottom()) return QString(); /* between two lanes */
	if (lanesShown_[lane].folded) return tr("Open lane");
	if (laneMenuButtonAt(pos) == lane) return tr("Y range and lane options");
	if (pos.x() < LANE_UNIT_W) return tr("Fold lane");
	if (pos.x() >= plot.left()) return QString();
	QStringList parts;
	if (maxLaneScroll() > 0) parts << tr("Wheel: scroll the lanes");
	parts << tr("Ctrl + wheel: zoom this lane") << tr("Right-click: its Y range and Fold lane");
	return parts.join(QStringLiteral(" · "));
}

/* a line in the middle of each gap between two lanes, where it lies in the plot: the lanes read as plots of their own */
QVector<double> ChartView::separatorsY(const QVector<Lane> &plots, QVector<int> *gaps) const {
	QVector<double> ys;
	if (!lanes_) return ys;
	const QRectF plot = plotRect();
	for (qsizetype k = 0; k + 1 < plots.size(); k++) {
		const double y = plots[k].axes.rect.bottom() + LANE_GAP / 2;
		if (y < plot.top() || y > plot.bottom()) continue;
		ys << y;
		if (gaps) *gaps << int(k);
	}
	return ys;
}

void ChartView::setLanes(bool on) {
	if (on == lanes_) return;
	lanes_ = on;
	laneScroll_ = 0;
	lanesShown_.clear();
	forgetRanges();
	refresh();
}

int ChartView::laneCount() const { return lanes_ ? int(plotLayout().size()) : 0; }

QString ChartView::laneLabel(int lane) const {
	const QVector<Lane> plots = plotLayout();
	return lanes_ && lane >= 0 && lane < plots.size() ? plots[lane].label : QString();
}

QRectF ChartView::laneRect(int lane) const {
	const QVector<Lane> plots = plotLayout();
	return lanes_ && lane >= 0 && lane < plots.size() ? plots[lane].axes.rect : QRectF();
}

QVector<int> ChartView::laneLines(int lane) const {
	const QVector<Lane> plots = plotLayout();
	QVector<int> keys;
	if (!lanes_ || lane < 0 || lane >= plots.size()) return keys;
	const QList<int> all = series_.keys();
	for (int i : plots[lane].lines) keys << all[i];
	return keys;
}

QString ChartView::laneKey(int lane) const {
	const QVector<Lane> plots = plotLayout();
	return lane >= 0 && lane < plots.size() ? plots[lane].key : QString();
}

bool ChartView::laneYAuto(int lane) const { return laneScales_.value(laneKey(lane)).autoRange; }
bool ChartView::laneYLog(int lane) const { return laneScales_.value(laneKey(lane)).log; }
double ChartView::laneYLo(int lane) const { return laneScales_.value(laneKey(lane)).lo; }
double ChartView::laneYHi(int lane) const { return laneScales_.value(laneKey(lane)).hi; }

double ChartView::laneYOfValue(int lane, double value) const {
	return lane >= 0 && lane < lanesShown_.size() ? lanesShown_[lane].axes.y(value) : NAN;
}

void ChartView::setLaneYAuto(int lane) {
	const QVector<Lane> plots = plotLayout();
	if (lane < 0 || lane >= plots.size()) return;
	YScale &scale = laneScales_[plots[lane].key];
	scale.autoRange = true;
	scale.initialized = false;
	emit laneYChanged();
	refresh();
}

bool ChartView::setLaneYManual(int lane, double lo, double hi) {
	const QVector<Lane> plots = plotLayout();
	if (lane < 0 || lane >= plots.size()) return false;
	YScale &scale = laneScales_[plots[lane].key];
	if (!(hi > lo) || (scale.log && !(lo > 0))) return false;
	scale.autoRange = false;
	scale.lo = lo;
	scale.hi = hi;
	emit laneYChanged();
	refresh();
	return true;
}

void ChartView::setLaneYLog(int lane, bool on) {
	const QVector<Lane> plots = plotLayout();
	if (lane < 0 || lane >= plots.size()) return;
	YScale &scale = laneScales_[plots[lane].key];
	if (scale.log == on) return;
	scale.log = on;
	scale.initialized = false;
	if (on && !scale.autoRange && !(scale.lo > 0)) scale.autoRange = true; /* a range through zero has no logarithm */
	emit laneYChanged();
	refresh();
}

QStringList ChartView::laneScales() const {
	QStringList texts;
	for (auto it = laneScales_.begin(); it != laneScales_.end(); ++it)
		texts << QStringList{ it.key(), it->autoRange ? QStringLiteral("1") : QStringLiteral("0"),
			it->log ? QStringLiteral("1") : QStringLiteral("0"), QString::number(it->lo, 'g', 17),
			QString::number(it->hi, 'g', 17) }.join(QLatin1Char('\t'));
	texts.sort();
	return texts;
}

void ChartView::setLaneScales(const QStringList &texts) {
	laneScales_.clear();
	for (const QString &text : texts) {
		const QStringList f = text.split(QLatin1Char('\t'));
		if (f.size() != 5) continue;
		YScale scale;
		scale.autoRange = f[1] != QLatin1String("0");
		scale.log = f[2] == QLatin1String("1");
		scale.lo = f[3].toDouble();
		scale.hi = f[4].toDouble();
		if (!scale.autoRange && (!(scale.hi > scale.lo) || (scale.log && !(scale.lo > 0)))) scale.autoRange = true;
		laneScales_.insert(f[0], scale);
	}
	refresh();
}

/* Auto: the lines' range with a margin. At first, or held, it jumps there; live,
 * it grows at once (nothing is cut off) and shrinks gently (no jumping). */
void ChartView::followData(YScale &scale, const QVector<BinnedLine> &all, const QVector<int> &which, double frameDt) {
	constexpr double FLAT = 1e-9;       /* the lines are flat: give them a range */
	constexpr double SHRINK_TIME = 0.4; /* seconds */
	constexpr double MIN_DECADES = 1;   /* Log: at least one decade, around what is shown */
	/* Log: the positive values in view, at most MAX_DECADES under the top; the margins and the gentle shrink in decades */
	const bool log = logOf(scale);
	double lo = std::numeric_limits<double>::max(), hi = -lo;
	for (const int index : which) {
		const BinnedLine &line = all[index];
		if (log) {
			if (!(line.hi > 0)) continue;
			lo = std::min(lo, line.posLo);
		} else {
			lo = std::min(lo, line.lo);
		}
		hi = std::max(hi, line.hi);
	}
	if (lo > hi) { /* nothing in the view: keep what is shown */
		lo = scale.initialized ? toScale(scale.lo, log) : 0;
		hi = scale.initialized ? toScale(scale.hi, log) : 1;
	} else {
		lo = toScale(lo, log);
		hi = toScale(hi, log);
		if (log) {
			lo = std::max(lo, hi - MAX_DECADES);
			if (hi - lo < MIN_DECADES) {
				const double middle = (lo + hi) / 2;
				lo = middle - MIN_DECADES / 2;
				hi = middle + MIN_DECADES / 2;
			}
		} else if (hi - lo < FLAT) {
			const double pad = std::max(std::fabs(hi) * 0.05, 0.5);
			lo -= pad;
			hi += pad;
		}
		const double margin = (hi - lo) * Y_MARGIN;
		lo -= margin;
		hi += margin;
	}
	if (!scale.initialized || !live_) {
		scale.lo = fromScale(lo, log);
		scale.hi = fromScale(hi, log);
		scale.initialized = true;
		return;
	}
	const double rate = 1 - std::exp(-frameDt / SHRINK_TIME);
	const double shownLo = toScale(scale.lo, log), shownHi = toScale(scale.hi, log);
	scale.lo = fromScale(lo < shownLo ? lo : shownLo + (lo - shownLo) * rate, log);
	scale.hi = fromScale(hi > shownHi ? hi : shownHi + (hi - shownHi) * rate, log);
}

/* The card: a plain fill, then its four rounded corners in the window's colour
 * (one antialiased rounded shape the size of the chart costs milliseconds at 4K). */
void ChartView::drawCard(QPainter &p) const {
	const ThemeColors &c = Theme::colors();
	p.fillRect(rect(), c.surface);
	QPainterPath corner; /* the top-left one: the square outside a quarter circle */
	corner.moveTo(0, 0);
	corner.lineTo(CARD_RADIUS, 0);
	corner.arcTo(0, 0, 2 * CARD_RADIUS, 2 * CARD_RADIUS, 90, 90);
	corner.closeSubpath();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(c.bg);
	const double w = width(), h = height();
	const QPointF corners[4] = { { 0, 0 }, { w, 0 }, { w, h }, { 0, h } };
	for (int k = 0; k < 4; k++) {
		p.save();
		p.translate(corners[k]);
		p.rotate(90 * k);
		p.drawPath(corner);
		p.restore();
	}
}

/* The value grid and its labels, then the time grid at fixed wall-clock times,
 * so it moves with the data. Crisp 1 px lines: no antialiasing. */
ChartView::GridTicks ChartView::gridTicks(const Axes &axes) const {
	GridTicks ticks;
	ticks.timeStep = niceTimeStep(window_, std::max(2, int(axes.columns / TIME_LABEL_SPACING)));
	for (double t = std::ceil(axes.t0 / ticks.timeStep) * ticks.timeStep; t <= axes.t1; t += ticks.timeStep) ticks.times << t;
	if (axes.log) {
		/* a line at each decade, faint ones at 2..9 of it while a decade is tall enough; over less than two decades
		 * those are labelled too where they have room */
		constexpr double MINOR_DECADE_PX = 24, MINOR_LABEL_PX = 16;
		const double decadePx = axes.rect.height() / (axes.logHi - axes.logLo);
		for (int d = int(std::ceil(axes.logLo - 1e-9)); d <= int(std::floor(axes.logHi + 1e-9)); d++)
			ticks.values << std::pow(10.0, d);
		if (decadePx >= MINOR_DECADE_PX) {
			for (int d = int(std::floor(axes.logLo)); d <= int(std::floor(axes.logHi)); d++)
				for (int m = 2; m <= 9; m++) {
					const double v = m * std::pow(10.0, d);
					if (v >= axes.lo && v <= axes.hi) ticks.minor << v;
				}
		}
		ticks.labelMinor = ticks.values.size() < 2 && decadePx * std::log10(10.0 / 9) >= MINOR_LABEL_PX;
		if (ticks.values.size() < 2 && !ticks.labelMinor) { /* too tight for all of them: 2 and 5 */
			QVector<double> some;
			for (double v : std::as_const(ticks.minor)) {
				const double m = v / std::pow(10.0, std::floor(std::log10(v) + 1e-9));
				if (std::fabs(m - 2) < 1e-6 || std::fabs(m - 5) < 1e-6) some << v;
			}
			ticks.values += some;
		}
		return ticks;
	}
	/* about one value line per 60 px, at most Y_TICKS: a lane is lower than the plot */
	ticks.valueStep = niceStep(axes.hi - axes.lo, std::clamp(int(axes.rect.height() / 60), 2, Y_TICKS));
	for (double v = std::ceil(axes.lo / ticks.valueStep) * ticks.valueStep; v <= axes.hi + ticks.valueStep * 1e-6;
			v += ticks.valueStep) {
		const double y = axes.y(v);
		if (y >= axes.rect.top() - 1 && y <= axes.rect.bottom() + 1) ticks.values << v;
	}
	return ticks;
}

void ChartView::drawGrid(QPainter &p, const QVector<Lane> &plots, const Axes &axes, bool lines) const {
	const ThemeColors &c = Theme::colors();
	const QRectF &plot = axes.rect;
	const GridTicks ticks = gridTicks(axes); /* its times: one time axis under all the plots */
	p.setFont(smallFont());
	p.setRenderHint(QPainter::Antialiasing, false);
	valueLabels_.clear();
	valueLabelRects_.clear();
	/* a lane's fold button: "▾" at the top of an open lane's unit column, "▸" on a folded strip; highlighted while the
	 * mouse is on it, the unit name or the strip */
	const auto foldButton = [&](int index, const QRectF &button, bool folded) {
		const double cx = button.center().x(), cy = button.center().y();
		const QPointF open[3] = { { cx - 4, cy - 2 }, { cx + 4, cy - 2 }, { cx, cy + 3 } };
		const QPointF shut[3] = { { cx + 3, cy }, { cx - 2, cy - 4 }, { cx - 2, cy + 4 } };
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		/* a button's shape at rest, a stronger one under the mouse */
		p.setPen(Qt::NoPen);
		p.setBrush(index == hoverLane_ ? c.border : c.surface2);
		p.drawRoundedRect(QRectF(cx - 8, cy - 7, 16, 14), 4, 4);
		p.setBrush(index == hoverLane_ ? c.text : c.muted);
		p.drawPolygon(folded ? shut : open, 3);
		p.restore();
	};
	/* a lane's menu button ("⋯", three dots) under its fold button, in the same shape; highlighted under the mouse */
	const auto menuButton = [&](int index, const QRectF &button) {
		const double cx = button.center().x(), cy = button.center().y();
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(index == hoverMenu_ ? c.border : c.surface2);
		p.drawRoundedRect(QRectF(cx - 8, cy - 7, 16, 14), 4, 4);
		p.setBrush(index == hoverMenu_ ? c.text : c.muted);
		for (double dx : { -4.0, 0.0, 4.0 }) p.drawEllipse(QPointF(cx + dx, cy), 1.5, 1.5);
		p.restore();
	};
	for (int index = 0; index < plots.size(); index++) {
		const Lane &lane = plots[index];
		const Axes &a = lane.axes;
		/* lanes: only the part in the plot (scrolled), and nothing of a folded one but its button */
		const QRectF shown = lanes_ ? laneVisible(a.rect, plot) : a.rect;
		if (shown.isEmpty()) continue;
		if (lane.folded) {
			foldButton(index, QRectF(0, shown.top(), LANE_UNIT_W, shown.height()), true);
			continue;
		}
		const GridTicks values = gridTicks(a);
		p.save();
		const auto label = [&](double v) {
			const QString text = a.log ? chartLogLabel(v) : chartAxisLabel(v, values.valueStep, normalized_);
			/* lanes: a label kept inside its lane's part in view, so it is never cut by the plot's edge nor runs into
			 * the next lane's labels across the gap; none where there is no room for a whole one */
			double top = a.y(v) - 8;
			if (lanes_) {
				if (shown.height() < 16) return;
				top = std::clamp(top, shown.top(), shown.bottom() - 16);
			}
			valueLabels_ << text;
			p.setPen(c.muted);
			const double left = lanes_ ? LANE_UNIT_W : 2; /* lanes: their units up the left edge */
			valueLabelRects_ << QRectF(left, top, plot.left() - 6 - left, 16);
			p.drawText(valueLabelRects_.last(), Qt::AlignRight | Qt::AlignVCenter, text);
		};
		const auto inView = [&shown](double y) { return y >= shown.top() - 1 && y <= shown.bottom() + 1; };
		for (double v : values.minor) {
			if (!inView(a.y(v))) continue;
			if (lines) {
				p.setPen(QPen(faintGrid(), 1));
				p.drawLine(QPointF(plot.left(), a.y(v)), QPointF(plot.right(), a.y(v)));
			}
			if (values.labelMinor) label(v);
		}
		for (double v : values.values) {
			const double y = a.y(v);
			if (!inView(y)) continue;
			if (lines) {
				p.setPen(QPen(c.grid, 1));
				p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
			}
			label(v);
		}
		for (double t : ticks.times) {
			if (!lines) break;
			p.setPen(QPen(c.grid, 1));
			p.drawLine(QPointF(axes.x(t), shown.top()), QPointF(axes.x(t), shown.bottom()));
		}
		if (lanes_) { /* its fold button and its menu's, then its unit up the left edge of its labels, in the part in view
		               * (a click on the unit name folds it too) */
			QRectF button, menu;
			laneButtons(shown, &button, &menu);
			foldButton(index, button, false);
			if (!menu.isEmpty()) menuButton(index, menu);
			const double nameTop = menu.isEmpty() ? button.bottom() : menu.bottom();
			const QRectF name(0, nameTop, LANE_UNIT_W, shown.bottom() - nameTop);
			p.setPen(c.text);
			p.translate(LANE_UNIT_W / 2, name.center().y());
			p.rotate(-90);
			const QString units = lane.label.isEmpty() ? tr("no unit") : lane.label;
			p.drawText(QRectF(-name.height() / 2, -7, name.height(), 14), Qt::AlignCenter,
					QFontMetricsF(p.font()).elidedText(units, Qt::ElideRight, name.height()));
		}
		p.restore();
	}
	/* between two lanes a line from their value labels across the plot, in the colour of a control's edge (3:1 to the
	 * chart, where the border's 1.3:1 left the lanes reading as one chart); the card draws its part over the plot with
	 * the grid */
	QVector<int> gaps;
	laneSeparators_ = separatorsY(plots, &gaps);
	for (qsizetype i = 0; i < laneSeparators_.size(); i++) { /* the one under the mouse (a drag resizes) lit */
		p.setPen(QPen(gaps[i] == hoverSeparator_ ? c.accent : c.control, 1));
		p.drawLine(QPointF(LANE_UNIT_W, laneSeparators_[i]), QPointF(plot.right(), laneSeparators_[i]));
	}
	for (double t : ticks.times) {
		const double x = axes.x(t);
		p.setPen(c.muted);
		p.drawText(QRectF(x - 60, plot.bottom() + 6, 120, 16), Qt::AlignCenter, timeLabel(epochMs_, t, ticks.timeStep));
	}
	p.setRenderHint(QPainter::Antialiasing, true);
}

/* the span between the cursors, shaded behind the lines */
void ChartView::drawCursorSpan(QPainter &p, const Axes &axes) const {
	if (!std::isfinite(cursorA_) || !std::isfinite(cursorB_)) return;
	const QRectF &plot = axes.rect;
	QColor shade = Theme::colors().accent;
	shade.setAlpha(28);
	const double xa = std::clamp(axes.x(std::min(cursorA_, cursorB_)), plot.left(), plot.right());
	const double xb = std::clamp(axes.x(std::max(cursorA_, cursorB_)), plot.left(), plot.right());
	if (xb > xa) p.fillRect(QRectF(xa, plot.top(), xb - xa, plot.height()), shade);
}

/* A polyline about LINE_WIDTH thick, drawn the fast way: 1-device-pixel
 * antialiased cosmetic polylines, a row of them side by side and a column above
 * and below (a plus), since Qt's raster engine has a fast path for those and
 * none for a wide antialiased stroke. `thin`: a single one. */
void ChartView::strokePolyline(QPainter &p, const QPolygonF &poly, const QColor &color, bool thin, qreal dpr) {
	QPen pen(color, 0);
	pen.setCosmetic(true);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const int copies = thin ? 1 : std::max(2, int(std::lround(LINE_WIDTH * dpr)));
	const qreal devicePixel = 1.0 / dpr, middle = (copies - 1) / 2.0;
	auto drawShifted = [&](qreal dx, qreal dy) {
		p.save();
		p.translate(dx, dy);
		p.drawPolyline(poly);
		p.restore();
	};
	for (int i = 0; i < copies; i++) drawShifted((i - middle) * devicePixel, 0);
	for (int j = 0; j < copies; j++)
		if (std::fabs(j - middle) >= 0.51) drawShifted(0, (j - middle) * devicePixel); /* the row covers the middle */
}

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
QPolygonF ChartView::toPolyline(const QVector<Bin> &bins, double columnSeconds, MapX x, MapY y,
		QVector<QRectF> *bands, double bandWidth, double pixel) {
	QPolygonF poly;
	poly.reserve(bins.size() * 4);
	const auto put = [&poly](const QPointF &point) {
		const qsizetype n = poly.size();
		if (n >= 2 && poly[n - 1].y() == point.y() && poly[n - 2].y() == point.y()) poly[n - 1] = point;
		else if (n == 0 || poly[n - 1] != point) poly << point;
	};
	for (const Bin &b : bins) {
		if (b.count <= 2) {
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

/* a line's bars, plain fills, antialiased: an edge on half a pixel is the same partly covered pixel whether it is
 * drawn here or on a stripe (without, the two rounded it to different sides) */
void ChartView::fillBands(QPainter &p, const QRectF *bands, qsizetype count, const QColor &color) {
	if (count <= 0) return;
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRects(bands, int(count));
	p.restore();
}

/* The lines, clipped to the plot (with room for their thickness). Many points:
 * the plot is cut into vertical stripes on whole device pixels, each drawn on a
 * thread into an image of its own (every line's part in it, a little past its
 * edges), then the stripes side by side: the same pixels as drawn on p. */
void ChartView::drawLines(QPainter &p, const QVector<Lane> &plots, const QVector<BinnedLine> &lines, qreal dpr) const {
	/* each line in its plot (lanes: its unit's), clipped to it with room for its thickness; lanes: to its part in the
	 * plot, none for a folded lane or one scrolled out of view */
	const QRectF plotArea = plotRect();
	QVector<const Axes *> axesOf(lines.size(), nullptr);
	QVector<QRectF> clips(lines.size());
	QRectF all;
	for (const Lane &plot : plots) {
		const QRectF shown = lanes_ ? laneVisible(plot.axes.rect, plotArea) : plot.axes.rect;
		if (plot.folded || shown.isEmpty()) continue;
		all = all.united(shown);
		for (int i : plot.lines) {
			axesOf[i] = &plot.axes;
			clips[i] = shown.adjusted(-2, -2, 2, 2);
		}
	}
	if (all.isEmpty()) return;
	const QRectF clip = all.adjusted(-2, -2, 2, 2);
	const auto clipOf = [&clips](qsizetype i) { return clips[i]; };
	if (dpr <= 0) dpr = p.device()->devicePixelRatioF();
	/* a bar as wide as the line: its copies side by side, one device pixel each */
	const double bandWidth = std::max(2, int(std::lround(LINE_WIDTH * dpr))) / dpr;
	QVector<QPolygonF> polys(lines.size());
	QVector<QVector<QRectF>> bands(lines.size());
	inParallel(lines.size(), [&](qsizetype i) {
		const BinnedLine &line = lines[i];
		if (line.bins.isEmpty() || !axesOf[i]) return;
		const Axes &axes = *axesOf[i];
		const auto x = [&axes](double t) { return axes.x(t); };
		double lo = line.lo, hi = line.hi; /* Normalize: the line's own range */
		if (normalized_) widenFlatRange(lo, hi);
		const auto y = [&](double v) { return normalized_ ? axes.y((v - lo) / (hi - lo)) : axes.y(v); };
		polys[i] = toPolyline(line.bins, axes.columnSeconds(), x, y, &bands[i], bandWidth, 1 / dpr);
	});
	/* a line that is mostly bars (fast and noisy): its polyline only joins them, one stroke is enough; five over the
	 * whole height of each column cost the most of a frame */
	QVector<char> thin(lines.size());
	qsizetype points = 0;
	for (qsizetype i = 0; i < lines.size(); i++) {
		thin[i] = 2 * bands[i].size() > lines[i].bins.size();
		points += polys[i].size() + 2 * bands[i].size();
	}
	const int threads = drawThreads_ > 0 ? drawThreads_ : pool_.maxThreadCount() + 1;
	const int stripes = int(std::clamp<qsizetype>(points / POINTS_PER_STRIPE, 1, threads));
	if (stripes == 1) {
		p.save();
		for (qsizetype i = 0; i < lines.size(); i++) {
			if (!axesOf[i]) continue;
			p.setClipRect(clipOf(i));
			fillBands(p, bands[i].constData(), bands[i].size(), lines[i].series->color);
			if (!polys[i].isEmpty()) strokePolyline(p, polys[i], lines[i].series->color, thin[i], dpr);
		}
		p.restore();
		return;
	}
	const QRect device = p.deviceTransform().mapRect(clip).toAlignedRect();
	const QTransform toWidget = p.deviceTransform().inverted();
	stripeImages_.resize(stripes);
	QVector<QPointF> places(stripes);
	QVector<QRect> shown(stripes); /* each image's own stripe, in its pixels */
	inParallel(stripes, [&](qsizetype k) {
		const int a = device.left() + int(qint64(device.width()) * k / stripes);
		const int b = device.left() + int(qint64(device.width()) * (k + 1) / stripes);
		QImage &image = stripeImages_[k];
		QTransform world;
		QPointF origin;
		prepareTile(p, QRect(a - STRIPE_OVERLAP, device.top(), b - a + 2 * STRIPE_OVERLAP, device.height()), image, world,
				origin);
		places[k] = toWidget.map(QPointF(a, device.top()));
		shown[k] = QRect(STRIPE_OVERLAP, 0, b - a, device.height());
		const QRectF rect(QPointF(toWidget.map(QPointF(a - STRIPE_OVERLAP, 0)).x(), clip.top()),
				QPointF(toWidget.map(QPointF(b + STRIPE_OVERLAP, 0)).x(), clip.bottom())); /* drawn, in the widget's coordinates */
		QPainter ip(&image);
		ip.setRenderHint(QPainter::Antialiasing);
		ip.setWorldTransform(world);
		/* each line clipped to its plot, as drawn on p; the image's own edges cut the stripe on whole pixels */
		constexpr double REACH = 3; /* a line's thickness past the stripe's edges */
		const auto beforeX = [](const QPointF &point, double xValue) { return point.x() < xValue; };
		const auto afterX = [](double xValue, const QPointF &point) { return xValue < point.x(); };
		const auto bandBefore = [](const QRectF &band, double xValue) { return band.right() < xValue; };
		const auto bandAfter = [](double xValue, const QRectF &band) { return xValue < band.left(); };
		for (qsizetype i = 0; i < lines.size(); i++) {
			if (!axesOf[i]) continue;
			ip.setClipRect(clipOf(i));
			const QVector<QRectF> &bars = bands[i]; /* in x order too: the ones that reach the stripe */
			const qsizetype first = std::lower_bound(bars.begin(), bars.end(), rect.left(), bandBefore) - bars.begin();
			const qsizetype last = std::upper_bound(bars.begin(), bars.end(), rect.right(), bandAfter) - bars.begin();
			fillBands(ip, bars.constData() + first, last - first, lines[i].series->color);
			const QPolygonF &poly = polys[i]; /* its x never falls: the part in the stripe, a point either side */
			const qsizetype from = std::max<qsizetype>(
					0, std::lower_bound(poly.begin(), poly.end(), rect.left() - REACH, beforeX) - poly.begin() - 1);
			const qsizetype to = std::min<qsizetype>(
					poly.size(), std::upper_bound(poly.begin(), poly.end(), rect.right() + REACH, afterX) - poly.begin() + 1);
			if (to - from >= 1) strokePolyline(ip, poly.mid(from, to - from), lines[i].series->color, thin[i], dpr);
		}
		ip.end();
		image.setDevicePixelRatio(dpr);
	}, 1); /* a stripe is a big job: a thread each */
	for (int k = 0; k < stripes; k++) p.drawImage(places[k], stripeImages_[k], shown[k]);
}

namespace {
/* a colour as GpuLines takes it: bytes R, G, B, A in memory */
quint32 gpuColor(const QColor &c) {
	return quint32(c.red()) | quint32(c.green()) << 8 | quint32(c.blue()) << 16 | quint32(c.alpha()) << 24;
}
/* a segment cut to the rows top..bottom (a lane, in the layer's pixels): the card has no clip of its own; false: none
 * of it there */
bool clipSegmentY(GpuLines::Segment &s, float top, float bottom) {
	if ((s.y0 < top && s.y1 < top) || (s.y0 > bottom && s.y1 > bottom)) return false;
	const auto cut = [&](float &xa, float &ya, float xb, float yb) {
		const float edge = ya < top ? top : ya > bottom ? bottom : ya;
		if (edge == ya) return;
		xa += (xb - xa) * (edge - ya) / (yb - ya);
		ya = edge;
	};
	cut(s.x0, s.y0, s.x1, s.y1);
	cut(s.x1, s.y1, s.x0, s.y0);
	return true;
}

/* a dashed line from a to b as segments: dash on, gap off (pixels) */
void dashes(QVector<GpuLines::Segment> &out, QPointF a, QPointF b, double dash, double gap, quint32 rgba) {
	const double length = QLineF(a, b).length();
	if (length <= 0) return;
	const QPointF step = (b - a) / length;
	for (double at = 0; at < length; at += dash + gap) {
		const QPointF p0 = a + step * at, p1 = a + step * std::min(at + dash, length);
		out.push_back({ float(p0.x()), float(p0.y()), float(p1.x()), float(p1.y()), rgba });
	}
}
} // namespace

QRect ChartView::layerRect() const { return plotRect().adjusted(-2, -2, 2, 2).toAlignedRect(); }

void ChartView::showLayer(bool shown) {
	QString error;
	framesUnder_ = 0;
	if (!gpu_ || gpu_->setShown(shown, error)) return;
	gpu_.reset(); /* the CPU draws, from the next frame on */
	emit drawingFailed(error);
	update();
}

QImage ChartView::gpuPicture(QRect *inWindow) const {
	if (!gpu_) return QImage();
	if (inWindow) *inWindow = layerPixels_;
	return gpu_->lastPicture();
}

/* The frame's plot as the card draws it, in the layer's pixels: the layer lies on whole pixels of the window, the
 * chart's coordinates times the scaling may not. The grid, the cursors' span, the lines, the cursors and the
 * crosshair's line, then the pictures over them: the cursors' tags, the crosshair's dots and its box. */
bool ChartView::plotOnGpu(const Axes &axes, const QVector<Lane> &plots, const QVector<BinnedLine> &lines) {
	QString error;
	const qreal dpr = devicePixelRatioF();
	QWidget *top = QWidget::window();
	const QPointF origin = QPointF(mapTo(top, QPoint(0, 0))) * dpr; /* the chart's top left, in the window's pixels */
	const QRect logical = layerRect();
	const QRect pixels = QRectF(origin + QPointF(logical.topLeft()) * dpr, QSizeF(logical.size()) * dpr).toAlignedRect();
	layerPixels_ = pixels;
	layerOrigin_ = origin;
	const double dx = origin.x() - pixels.left(), dy = origin.y() - pixels.top();
	const auto map = [&](QPointF pt) { return QPointF(pt.x() * dpr + dx, pt.y() * dpr + dy); };
	const auto whole = [&](QPointF pt) { return map(pt).toPoint(); };
	const ThemeColors &c = Theme::colors();
	const QRectF &plot = axes.rect;
	const QPointF topLeft = map(plot.topLeft()), bottomRight = map(plot.bottomRight());

	GpuLines::Frame frame;
	frame.background = c.surface.rgb();
	/* the grid: crisp, each line on whole pixels as the CPU's 1 px pen (not antialiased) */
	GpuLines::Layer grid;
	grid.widthPx = float(std::max(1, int(std::lround(dpr))));
	grid.caps = false;
	const quint32 gridRgba = gpuColor(c.grid);
	const GridTicks ticks = gridTicks(axes); /* the times; each plot its values */
	const quint32 faintRgba = gpuColor(faintGrid());
	for (const Lane &plot : plots) {
		const Axes &a = plot.axes;
		/* lanes: the part in the plot, as drawGrid; nothing of a folded lane (its strip is a picture) */
		const QRectF shown = lanes_ ? laneVisible(a.rect, axes.rect) : a.rect;
		if (plot.folded || shown.isEmpty()) continue;
		const GridTicks values = gridTicks(a);
		const float laneTop = float(map(shown.topLeft()).y()), laneBottom = float(map(shown.bottomLeft()).y());
		const auto inView = [&shown](double y) { return y >= shown.top() - 1 && y <= shown.bottom() + 1; };
		for (double v : values.minor) {
			if (!inView(a.y(v))) continue;
			const float y = float(std::floor(map(QPointF(0, a.y(v))).y()) + grid.widthPx / 2);
			grid.segments.push_back({ float(topLeft.x()), y, float(bottomRight.x()), y, faintRgba });
		}
		for (double v : values.values) {
			if (!inView(a.y(v))) continue;
			const float y = float(std::floor(map(QPointF(0, a.y(v))).y()) + grid.widthPx / 2);
			grid.segments.push_back({ float(topLeft.x()), y, float(bottomRight.x()), y, gridRgba });
		}
		for (double t : ticks.times) {
			const float x = float(std::floor(map(QPointF(axes.x(t), 0)).x()) + grid.widthPx / 2);
			grid.segments.push_back({ x, laneTop, x, laneBottom, gridRgba });
		}
	}
	/* the lanes' separators: crisp as the grid, from the layer's left edge (the CPU draws them up to it) */
	QVector<int> gaps;
	const QVector<double> separators = separatorsY(plots, &gaps);
	for (qsizetype i = 0; i < separators.size(); i++) {
		const float sy = float(std::floor(map(QPointF(0, separators[i])).y()) + grid.widthPx / 2);
		grid.segments.push_back({ float(map(QPointF(plot.left() - 2, 0)).x()), sy, float(bottomRight.x()), sy,
				gpuColor(gaps[i] == hoverSeparator_ ? c.accent : c.control) });
	}
	frame.layers << grid;
	/* the cursors' span: one bar as tall as the plot */
	if (std::isfinite(cursorA_) && std::isfinite(cursorB_)) {
		QColor shade = c.accent;
		shade.setAlpha(28);
		const double xa = std::clamp(axes.x(std::min(cursorA_, cursorB_)), plot.left(), plot.right());
		const double xb = std::clamp(axes.x(std::max(cursorA_, cursorB_)), plot.left(), plot.right());
		if (xb > xa) {
			GpuLines::Layer span;
			span.caps = false;
			span.widthPx = float(bottomRight.y() - topLeft.y());
			const float y = float((topLeft.y() + bottomRight.y()) / 2);
			span.segments.push_back({ float(map(QPointF(xa, 0)).x()), y, float(map(QPointF(xb, 0)).x()), y, gpuColor(shade) });
			frame.layers << span;
		}
	}
	/* the lines: as thick as the CPU's, its copies one device pixel each; each in its plot (lanes: cut to its lane's
	 * part in the plot; none for a folded lane or one out of view) */
	QVector<const Axes *> axesOf(lines.size(), nullptr);
	QVector<QRectF> shownOf(lines.size());
	for (const Lane &plot : plots) {
		const QRectF shown = lanes_ ? laneVisible(plot.axes.rect, axes.rect) : plot.axes.rect;
		if (plot.folded || shown.isEmpty()) continue;
		for (int i : plot.lines) {
			axesOf[i] = &plot.axes;
			shownOf[i] = shown;
		}
	}
	const float lineWidth = float(std::max(2, int(std::lround(LINE_WIDTH * dpr))));
	/* held, the same lines in the same places: the segments made last time (a dragged cursor over a held view) */
	QElapsedTimer made;
	made.start();
	const QString linesKeyNow = linesKey(plots, dpr) + QStringLiteral("|%1,%2").arg(dx).arg(dy);
	const bool sameLines = lineReuse_ && !live_ && linesKeyNow == gpuLinesKey_;
	QVector<QVector<GpuLines::Segment>> parts(sameLines ? 0 : lines.size());
	inParallel(parts.size(), [&](qsizetype i) {
		const BinnedLine &line = lines[i];
		if (line.bins.isEmpty() || !axesOf[i]) return;
		const Axes &axes = *axesOf[i];
		const auto x = [&axes](double t) { return axes.x(t); };
		double lo = line.lo, hi = line.hi; /* Normalize: the line's own range */
		if (normalized_) widenFlatRange(lo, hi);
		const auto y = [&](double v) { return normalized_ ? axes.y((v - lo) / (hi - lo)) : axes.y(v); };
		const QPolygonF poly = toPolyline(line.bins, axes.columnSeconds(), x, y, nullptr, 0, 1 / dpr);
		const quint32 rgba = gpuColor(line.series->color);
		QVector<GpuLines::Segment> &segments = parts[i];
		segments.resize(std::max<qsizetype>(1, poly.size() - 1));
		/* scale and shift as arithmetic, not a transform per point */
		float x0 = float(poly[0].x() * dpr + dx), y0 = float(poly[0].y() * dpr + dy);
		if (poly.size() == 1) segments[0] = { x0, y0, x0, y0, rgba };
		for (qsizetype k = 1; k < poly.size(); k++) {
			const float x1 = float(poly[k].x() * dpr + dx), y1 = float(poly[k].y() * dpr + dy);
			segments[k - 1] = { x0, y0, x1, y1, rgba };
			x0 = x1;
			y0 = y1;
		}
		if (!lanes_) return; /* the layer's edge cuts the one plot */
		const float top = float(shownOf[i].top() * dpr + dy) - lineWidth, bottom = float(shownOf[i].bottom() * dpr + dy) + lineWidth;
		qsizetype kept = 0;
		for (qsizetype k = 0; k < segments.size(); k++)
			if (clipSegmentY(segments[k], top, bottom)) segments[kept++] = segments[k];
		segments.resize(kept);
	});
	GpuLines::Layer drawn;
	drawn.widthPx = lineWidth;
	if (sameLines) {
		drawn.segments = gpuLines_;
	} else {
		qsizetype total = 0;
		for (const auto &part : std::as_const(parts)) total += part.size();
		drawn.segments.reserve(total);
		for (const auto &part : std::as_const(parts)) drawn.segments += part;
		gpuLines_ = drawn.segments;
		gpuLinesKey_ = linesKeyNow;
		lineBuilds_++;
	}
	frame.layers << drawn;
	perf_.segments += made.nsecsElapsed() / 1e6;
	/* the folded lanes' strips: the first pictures, over the marks' lines and under their tags, as the CPU draws them */
	for (const Lane &plot : plots) {
		const QRectF shown = lanes_ ? laneVisible(plot.axes.rect, axes.rect) : QRectF();
		if (plot.folded && !shown.isEmpty())
			frame.sprites.push_back({ foldedPicture(plot, shown, axes.t0, axes.t1, dpr), whole(shown.topLeft()) });
	}
	/* the cursors: dashed as the CPU's pen (1.2 px: dashes of 4.8, gaps of 2.4), a tag each */
	GpuLines::Layer marks;
	marks.widthPx = float(1.2 * dpr);
	const double times[2] = { cursorA_, cursorB_ };
	for (int k = 0; k < 2; k++) {
		const double t = times[k];
		if (!std::isfinite(t) || t < axes.t0 || t > axes.t1) continue;
		const double cx = axes.x(t);
		dashes(marks.segments, map(QPointF(cx, plot.top())), map(QPointF(cx, plot.bottom())), 4.8 * dpr, 2.4 * dpr,
				gpuColor(c.accent));
		frame.sprites.push_back({ tagPicture(k, dpr), whole(QPointF(cx - 9, plot.top() - 2)) });
	}
	/* the notes: dashed as the CPU's pen, a tag each */
	noteTags_.resize(notes_.size());
	for (int i = 0; i < notes_.size(); i++) {
		noteTags_[i] = noteTagRect(axes, i);
		if (noteTags_[i].isEmpty()) continue;
		const double nx = axes.x(notes_[i].time);
		dashes(marks.segments, map(QPointF(nx, plot.top())), map(QPointF(nx, plot.bottom())), 4.8 * dpr, 2.4 * dpr,
				gpuColor(c.warn));
		frame.sprites.push_back({ notePicture(i, noteTags_[i], dpr), whole(noteTags_[i].topLeft()) });
	}
	/* the trigger: its level dashed in its line's colour, its marker */
	double levelY;
	QRectF levelLane, tag;
	if (triggerGeometry(plots, lines, levelY, levelLane, tag)) {
		const QColor color = series_.value(trigger_.key).color;
		/* as the CPU's pen (1.2 px, Qt::DashLine: dashes of 4.8, gaps of 2.4), the cursors and the notes */
		dashes(marks.segments, map(QPointF(levelLane.left(), levelY)), map(QPointF(levelLane.right(), levelY)), 4.8 * dpr,
				2.4 * dpr, gpuColor(color));
		if (!tag.isEmpty()) frame.sprites.push_back({ triggerPicture(dpr), whole(tag.topLeft()) });
	}
	frame.layers << marks;
	spanBar_ = spanBar(axes);
	if (!spanBar_.text.isEmpty()) {
		const QRectF area = spanBar_.bar.isEmpty() ? spanBar_.textRect : spanBar_.bar.united(spanBar_.textRect);
		frame.sprites.push_back({ spanBarPicture(spanBar_, area, dpr), whole(area.topLeft()) });
	}
	/* the crosshair: its dashed line (1 px: 4 on, 2 off), the dots, the box */
	Crosshair hair;
	if (crosshair(axes, plots, lines, dpr, hair)) {
		GpuLines::Layer line;
		line.widthPx = float(dpr);
		dashes(line.segments, map(QPointF(hair.x, plot.top())), map(QPointF(hair.x, plot.bottom())), 4 * dpr, 2 * dpr,
				gpuColor(c.muted));
		frame.layers << line;
		for (const auto &dot : std::as_const(hair.dots))
			frame.sprites.push_back({ dotPicture(dot.second, dpr), whole(dot.first - QPointF(DOT_PICTURE, DOT_PICTURE) / 2) });
		if (!readout_.isNull()) frame.sprites.push_back({ readout_, whole(hair.boxAt) });
	}

	QElapsedTimer presenting;
	presenting.start();
	const bool presented = gpu_->present(top->winId(), pixels, frame, error);
	perf_.present += presenting.nsecsElapsed() / 1e6;
	if (presented) return true;
	gpu_.reset(); /* the CPU draws, from this frame on, and paints all of the plot at the next (its layer is gone) */
	emit drawingFailed(error);
	update();
	return false;
}

void ChartView::setDrawing(Drawing drawing) {
	drawing_ = drawing;
	openGeneration_++; /* a card still being opened for an earlier choice goes when it is ready */
	opening_ = false;
	if (gpu_) { /* the CPU's frame on the window first, then the layer away once that is on the screen (setShown) */
		std::unique_ptr<GpuLines> closing = std::move(gpu_);
		if (isVisible()) repaint();
		QString error;
		closing->setShown(false, error); /* failed: the card is closed all the same */
	}
	const QVector<GpuLines::Adapter> adapters = drawing == Drawing::Cpu ? QVector<GpuLines::Adapter>() : GpuLines::adapters();
	const GpuLines::Adapter *chosen = nullptr;
	for (const GpuLines::Adapter &adapter : adapters) {
		const bool wanted = drawing == Drawing::Internal ? !adapter.dedicated : adapter.dedicated;
		if (wanted && !chosen) chosen = &adapter;
	}
	if (chosen) {
		/* Opened on a thread of its own: making its device wakes the card (0.8 s on an Optimus laptop the first time,
		 * 0.13 s awake; its shaders take 10 ms), and the window's thread was held all along. The CPU draws meanwhile;
		 * gpuOpened takes the card on this thread. The posted call is let go if the chart goes first (opener_ waits) */
		opening_ = true;
		openingName_ = chosen->name;
		const auto card = std::make_shared<std::unique_ptr<GpuLines>>(std::make_unique<GpuLines>());
		opener_.start([this, card, adapter = *chosen, generation = openGeneration_] {
			QString error;
			const bool ok = (*card)->open(adapter, error);
			QMetaObject::invokeMethod(this, [this, card, ok, error, generation] {
				gpuOpened(std::move(*card), ok, error, generation);
			}, Qt::QueuedConnection);
		});
	} else if (drawing == Drawing::Dedicated || drawing == Drawing::Internal) {
		/* Auto without a dedicated card: the CPU, as meant; a card asked for and not there: said */
		emit drawingFailed(drawing == Drawing::Dedicated ? tr("no dedicated GPU here") : tr("no internal GPU here"));
	}
	refresh();
}

QString ChartView::drawingName() const {
	if (gpu_) return tr("GPU: %1").arg(gpu_->name());
	return opening_ ? tr("CPU, opening the GPU: %1").arg(openingName_) : tr("CPU");
}

void ChartView::gpuOpened(std::unique_ptr<GpuLines> gpu, bool ok, const QString &error, quint64 generation) {
	if (generation != openGeneration_) return; /* chosen again since: this card goes */
	opening_ = false;
	if (ok) gpu_ = std::move(gpu);
	else emit drawingFailed(error);
	refresh();
	emit drawingChanged();
}

/* A folded lane's items: each line's name and value as the legend writes it (live, the legend's value; held, the
 * latest sample in view), with its colour */
QVector<ChartView::FoldedItem> ChartView::foldedItems(const Lane &lane, double t0, double t1) const {
	QVector<const Series *> byIndex;
	byIndex.reserve(series_.size());
	for (const Series &s : series_) byIndex << &s;
	QVector<FoldedItem> items;
	for (int i : lane.lines) {
		const Series &s = *byIndex[i];
		QString value;
		if (live_) {
			if (s.hasShown) value = chartNumber(s.shown);
		} else {
			const qsizetype k = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin() - 1;
			if (k >= 0 && s.times[k] >= t0) value = chartNumber(s.values[k]);
		}
		QString text = s.name;
		if (!value.isEmpty()) text += QLatin1Char(' ') + value + (s.unit.isEmpty() ? QString() : QLatin1Char(' ') + s.unit);
		items.push_back({ s.color, text });
	}
	return items;
}

/* The strip's part in the plot (`visible`) as a picture: its background, its unit, then each line's dot and text;
 * what does not fit is cut with "…". Made again when its texts, place or look change. */
const QImage &ChartView::foldedPicture(const Lane &lane, const QRectF &visible, double t0, double t1, qreal dpr) const {
	const QVector<FoldedItem> items = foldedItems(lane, t0, t1);
	const QString unit = lane.label.isEmpty() ? tr("no unit") : lane.label;
	QStringList texts{ unit };
	QString key = QStringLiteral("%1|%2|%3|%4|%5|%6").arg(visible.width()).arg(visible.height())
			.arg(lane.axes.rect.top() - visible.top()).arg(dpr).arg(Theme::isDark()).arg(unit);
	for (const FoldedItem &item : items) {
		texts << item.text;
		key += QLatin1Char('|') + item.color.name() + QLatin1Char('|') + item.text;
	}
	/* its text only when its rows are wholly in view: a strip cut by the plot's edge shows no half letters */
	const QRectF strip = lane.axes.rect;
	const bool whole = strip.center().y() - 8 >= visible.top() && strip.center().y() + 8 <= visible.bottom();
	foldedTexts_[lane.key] = whole ? texts.join(QStringLiteral("  ")) : QString();
	key += whole ? QStringLiteral("|text") : QString();
	QImage &image = foldedImages_[lane.key];
	if (foldedKeys_.value(lane.key) == key && !image.isNull()) return image;
	foldedKeys_[lane.key] = key;
	const ThemeColors &c = Theme::colors();
	image = QImage((visible.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(dpr);
	image.fill(Qt::transparent);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(-visible.left(), -visible.top());
	p.setPen(Qt::NoPen);
	p.setBrush(c.surface2);
	p.drawRoundedRect(strip, 4, 4);
	if (!whole) {
		p.end();
		return image;
	}
	p.setFont(labelFont());
	const QFontMetricsF metrics(p.font());
	const double right = strip.right() - 6;
	double x = strip.left() + 8;
	p.setPen(c.text);
	p.drawText(QRectF(x, strip.top(), right - x, strip.height()), Qt::AlignVCenter | Qt::AlignLeft,
			metrics.elidedText(unit, Qt::ElideRight, right - x));
	x += metrics.horizontalAdvance(unit) + FOLDED_ITEM_GAP;
	for (const FoldedItem &item : items) {
		if (x + 8 + metrics.horizontalAdvance(QStringLiteral("…")) > right) { /* no room for even its dot */
			if (x < right) p.drawText(QRectF(x, strip.top(), right - x, strip.height()), Qt::AlignVCenter | Qt::AlignLeft,
					QStringLiteral("…"));
			break;
		}
		p.setPen(Qt::NoPen);
		p.setBrush(item.color);
		p.drawEllipse(QPointF(x + 4, strip.center().y()), 4, 4);
		p.setPen(c.text);
		const double textX = x + 12, width = metrics.horizontalAdvance(item.text);
		p.drawText(QRectF(textX, strip.top(), right - textX, strip.height()), Qt::AlignVCenter | Qt::AlignLeft,
				metrics.elidedText(item.text, Qt::ElideRight, right - textX));
		x = textX + width + FOLDED_ITEM_GAP;
		if (x > right) break;
	}
	p.end();
	return image;
}

/* the folded strips in view, on the CPU (the card shows the same pictures) */
void ChartView::drawFolded(QPainter &p, const QVector<Lane> &plots) const {
	if (!lanes_) return;
	const QRectF plot = plotRect();
	const qreal dpr = p.device()->devicePixelRatioF();
	for (const Lane &lane : plots) {
		const QRectF shown = laneVisible(lane.axes.rect, plot);
		if (!lane.folded || shown.isEmpty()) continue;
		/* on a whole device pixel, as the card places it */
		const QPointF device = p.deviceTransform().map(shown.topLeft());
		const QPointF at = p.deviceTransform().inverted().map(QPointF(std::round(device.x()), std::round(device.y())));
		p.drawImage(at, foldedPicture(lane, shown, lane.axes.t0, lane.axes.t1, dpr));
	}
}

/* the lanes' scroll bar, when they do not fit: a track as tall as the plot in its right pad, and the handle */
void ChartView::drawLaneBar(QPainter &p) const {
	const QRectF track = laneScrollBarRect();
	if (track.isEmpty()) return;
	const ThemeColors &c = Theme::colors();
	const double radius = LANE_BAR_W / 2;
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(c.surface2);
	p.drawRoundedRect(track, radius, radius);
	p.setBrush(hoverBar_ ? c.text : c.muted); /* under the mouse: brighter */
	p.drawRoundedRect(laneScrollHandleRect(), radius, radius);
	p.restore();
}

/* the cursors: a dashed line each, its letter in a tag at the top */
void ChartView::drawCursors(QPainter &p, const Axes &axes, Marks part) const {
	const ThemeColors &c = Theme::colors();
	const double times[2] = { cursorA_, cursorB_ };
	for (int k = 0; k < 2; k++) {
		const double t = times[k];
		if (!std::isfinite(t) || t < axes.t0 || t > axes.t1) continue;
		const double x = axes.x(t);
		if (part == Marks::Lines) {
			p.setPen(QPen(c.accent, 1.2, Qt::DashLine));
			p.drawLine(QPointF(x, axes.rect.top()), QPointF(x, axes.rect.bottom()));
			continue;
		}
		const QRectF tag(x - 9, axes.rect.top() - 2, 18, 16);
		p.setPen(Qt::NoPen);
		p.setBrush(c.accentFill); /* white text on it */
		p.drawRoundedRect(tag, 4, 4);
		p.setPen(Qt::white);
		p.setFont(labelFont());
		p.drawText(tag, Qt::AlignCenter, k == 0 ? QStringLiteral("A") : QStringLiteral("B"));
	}
	if (part == Marks::Lines) return;
	spanBar_ = spanBar(axes);
	drawSpanBar(p, spanBar_);
}

/* The trigger's level line in its line's plot (kept within it, so it can be dragged when the range does not reach it),
 * and its marker at the crossing, at the top of the plot, when the crossing is in view */
bool ChartView::triggerGeometry(const QVector<Lane> &plots, const QVector<BinnedLine> &lines, double &levelY,
		QRectF &lane, QRectF &tag) const {
	triggerLineY_ = NAN;
	triggerTag_ = QRectF();
	tag = QRectF();
	if (!trigger_.on) return false;
	int index = 0;
	for (auto it = series_.constBegin(); it != series_.constEnd() && it.key() != trigger_.key; ++it) index++;
	if (index >= lines.size()) return false;
	const QRectF all = plotRect();
	for (const Lane &plot : plots) {
		if (!plot.lines.contains(index)) continue;
		const Axes &a = plot.axes;
		/* lanes: only while its lane is open and in view, within the part in view */
		const QRectF shown = lanes_ ? laneVisible(a.rect, all) : a.rect;
		if (plot.folded || shown.isEmpty()) return false;
		double lo = lines[index].lo, hi = lines[index].hi;
		if (normalized_) widenFlatRange(lo, hi);
		levelY = std::clamp(normalized_ ? a.y((trigger_.level - lo) / (hi - lo)) : a.y(trigger_.level), shown.top(),
				shown.bottom());
		lane = shown;
		triggerLineY_ = levelY;
		triggerLane_ = lane;
		triggerAxes_ = a;
		triggerLo_ = lo;
		triggerHi_ = hi;
		if (std::isfinite(trigger_.at) && trigger_.at >= a.t0 && trigger_.at <= a.t1) {
			tag = QRectF(a.x(trigger_.at) - 7, all.top() - 2, 14, 16);
			triggerTag_ = tag;
		}
		return true;
	}
	return false;
}

void ChartView::drawTrigger(QPainter &p, const QVector<Lane> &plots, const QVector<BinnedLine> &lines, Marks part) const {
	double levelY;
	QRectF lane, tag;
	if (!triggerGeometry(plots, lines, levelY, lane, tag)) return;
	p.save();
	p.setPen(QPen(series_.value(trigger_.key).color, 1.2, Qt::DashLine));
	if (part == Marks::Lines) p.drawLine(QPointF(lane.left(), levelY), QPointF(lane.right(), levelY));
	else if (!tag.isEmpty()) p.drawImage(tag.topLeft(), triggerPicture(p.device()->devicePixelRatioF()));
	p.restore();
}

/* the marker: a "T" in a tag of the line's colour */
const QImage &ChartView::triggerPicture(qreal dpr) const {
	const QColor color = series_.value(trigger_.key).color;
	const QString key = QStringLiteral("%1|%2").arg(dpr).arg(color.name());
	if (key == triggerImageKey_) return triggerImage_;
	triggerImageKey_ = key;
	triggerImage_ = QImage((QSizeF(14, 16) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
	triggerImage_.setDevicePixelRatio(dpr);
	triggerImage_.fill(Qt::transparent);
	QPainter p(&triggerImage_);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(QRectF(0, 0, 14, 16), 4, 4);
	p.setPen(Qt::white);
	p.setFont(labelFont());
	p.drawText(QRectF(0, 0, 14, 16), Qt::AlignCenter, QStringLiteral("T"));
	return triggerImage_;
}

/* a note's tag at the bottom of the plot, its left edge on the note's line (its right edge when the plot ends) */
QRectF ChartView::noteTagRect(const Axes &axes, int index) const {
	const ChartNote &note = notes_[index];
	if (note.time < axes.t0 || note.time > axes.t1) return QRectF();
	const QFontMetricsF metrics(labelFont());
	const QString text = metrics.elidedText(note.text.isEmpty() ? QStringLiteral(" ") : note.text, Qt::ElideRight, NOTE_TEXT_MAX);
	const double w = metrics.horizontalAdvance(text) + 2 * NOTE_PAD, x = axes.x(note.time);
	const double left = x + w <= axes.rect.right() ? x : x - w;
	return QRectF(left, axes.rect.bottom() - NOTE_TAG_BOTTOM - NOTE_TAG_H, w, NOTE_TAG_H);
}

void ChartView::drawNoteTag(QPainter &p, const QRectF &tag, int index) const {
	const ThemeColors &c = Theme::colors();
	const bool chosen = index == selectedNote_;
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(chosen ? c.accent : c.warn, chosen ? 2 : 1.2));
	p.setBrush(c.surface2);
	p.drawRoundedRect(tag.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
	p.setPen(c.text);
	p.setFont(labelFont());
	p.drawText(tag.adjusted(NOTE_PAD, 0, -NOTE_PAD + 1, 0), Qt::AlignVCenter | Qt::AlignLeft,
			QFontMetricsF(labelFont()).elidedText(notes_[index].text, Qt::ElideRight, NOTE_TEXT_MAX));
	p.restore();
}

/* a dashed line in the warning colour at each note, its tag at the bottom of the plot */
void ChartView::drawNotes(QPainter &p, const Axes &axes, Marks part) const {
	noteTags_.resize(notes_.size());
	for (int i = 0; i < notes_.size(); i++) {
		noteTags_[i] = noteTagRect(axes, i);
		if (noteTags_[i].isEmpty()) continue;
		if (part == Marks::Tags) {
			drawNoteTag(p, noteTags_[i], i);
			continue;
		}
		const double x = axes.x(notes_[i].time);
		p.setPen(QPen(Theme::colors().warn, 1.2, Qt::DashLine));
		p.drawLine(QPointF(x, axes.rect.top()), QPointF(x, axes.rect.bottom()));
	}
}

const QImage &ChartView::notePicture(int index, const QRectF &tag, qreal dpr) const {
	const QString key = QStringLiteral("%1|%2|%3|%4|%5").arg(notes_[index].text).arg(tag.width(), 0, 'f', 2).arg(dpr)
			.arg(Theme::isDark()).arg(index == selectedNote_);
	auto it = notePictures_.find(key);
	if (it != notePictures_.end()) return *it;
	if (notePictures_.size() > 64) notePictures_.clear(); /* texts edited away */
	QImage image((tag.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(dpr);
	image.fill(Qt::transparent);
	QPainter p(&image);
	drawNoteTag(p, QRectF(QPointF(0, 0), tag.size()), index);
	p.end();
	return *notePictures_.insert(key, image);
}

namespace {
constexpr double TAG_HALF = 9;   /* a cursor's tag: 18 x 16, centred on it, its top 2 px above the plot (drawCursors) */
constexpr double SPAN_GAP = 2;   /* between a tag and the bar */
constexpr double SPAN_PAD = 6;   /* the text's margin each side */
}

ChartView::SpanBar ChartView::spanBar(const Axes &axes) const {
	SpanBar out;
	if (!std::isfinite(cursorA_) || !std::isfinite(cursorB_)) return out;
	const double t0 = std::min(cursorA_, cursorB_), t1 = std::max(cursorA_, cursorB_);
	if (t1 < axes.t0 || t0 > axes.t1) return out;
	const QRectF &plot = axes.rect;
	const bool leftShown = t0 >= axes.t0, rightShown = t1 <= axes.t1;
	const double left = leftShown ? axes.x(t0) + TAG_HALF + SPAN_GAP : plot.left();
	const double right = rightShown ? axes.x(t1) - TAG_HALF - SPAN_GAP : plot.right();
	const double top = plot.top() - 2, height = 16;
	out.text = durationText(t1 - t0);
	const double textWidth = QFontMetricsF(labelFont()).horizontalAdvance(out.text) + 2 * SPAN_PAD;
	/* the bar only with its text inside: a span too narrow for it has the text's tag beside the tags, and no sliver
	 * of a bar between them */
	if (right - left >= textWidth) {
		out.bar = QRectF(left, top, right - left, height);
		out.inside = true;
		out.textRect = QRectF(left + (right - left - textWidth) / 2, top, textWidth, height);
		return out;
	}
	const double afterRight = (rightShown ? axes.x(t1) + TAG_HALF : plot.right()) + SPAN_GAP;
	const double beforeLeft = (leftShown ? axes.x(t0) - TAG_HALF : plot.left()) - SPAN_GAP;
	const double x = afterRight + textWidth <= plot.right() ? afterRight : std::max(plot.left(), beforeLeft - textWidth);
	out.textRect = QRectF(x, top, textWidth, height);
	return out;
}

void ChartView::drawSpanBar(QPainter &p, const SpanBar &bar) const {
	if (bar.text.isEmpty()) return;
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(Theme::colors().accentFill); /* white text on it, as the tags */
	if (!bar.bar.isEmpty()) p.drawRoundedRect(bar.bar, 4, 4);
	if (!bar.inside) p.drawRoundedRect(bar.textRect, 4, 4);
	p.setPen(Qt::white);
	p.setFont(labelFont());
	p.drawText(bar.textRect, Qt::AlignCenter, bar.text);
	p.restore();
}

const QImage &ChartView::spanBarPicture(const SpanBar &bar, const QRectF &area, qreal dpr) const {
	const QRectF b = bar.bar.translated(-area.topLeft()), t = bar.textRect.translated(-area.topLeft());
	const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8").arg(bar.text).arg(dpr).arg(Theme::isDark())
			.arg(b.left(), 0, 'f', 2).arg(b.width(), 0, 'f', 2).arg(t.left(), 0, 'f', 2).arg(area.width(), 0, 'f', 2)
			.arg(bar.inside);
	if (key != spanBarKey_) {
		spanBarKey_ = key;
		spanBarImage_ = QImage((area.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
		spanBarImage_.setDevicePixelRatio(dpr);
		spanBarImage_.fill(Qt::transparent);
		QPainter p(&spanBarImage_);
		SpanBar local = bar;
		local.bar = b;
		local.textRect = t;
		drawSpanBar(p, local);
	}
	return spanBarImage_;
}

/* a cursor's tag for the card, drawn as drawCursors draws it */
const QImage &ChartView::tagPicture(int k, qreal dpr) const {
	const QString key = QStringLiteral("%1|%2").arg(dpr).arg(Theme::isDark());
	if (key != tagsKey_) {
		tagsKey_ = key;
		for (int i = 0; i < 2; i++) {
			QImage &tag = tags_[i];
			tag = QImage((QSizeF(18, 16) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
			tag.setDevicePixelRatio(dpr);
			tag.fill(Qt::transparent);
			QPainter p(&tag);
			p.setRenderHint(QPainter::Antialiasing);
			p.setPen(Qt::NoPen);
			p.setBrush(Theme::colors().accentFill); /* white text on it */
			p.drawRoundedRect(QRectF(0, 0, 18, 16), 4, 4);
			p.setPen(Qt::white);
			p.setFont(labelFont());
			p.drawText(QRectF(0, 0, 18, 16), Qt::AlignCenter, i == 0 ? QStringLiteral("A") : QStringLiteral("B"));
		}
	}
	return tags_[k];
}

/* The memory strip: the whole memory depth, as an oscilloscope's, the data
 * filling it from the right; the view is marked on it, and while it fills it
 * says how much is kept so far. */
void ChartView::drawMemoryStrip(QPainter &p, const Axes &axes) {
	const ThemeColors &c = Theme::colors();
	const QRectF box = overviewRect();
	p.setPen(QPen(c.border, 1));
	p.setBrush(c.surface2);
	p.drawRoundedRect(box, 5, 5);
	Axes strip;
	strip.rect = box.adjusted(0, 3, 0, -3); /* the lines keep 3 px off its top and bottom */
	strip.t1 = liveEnd();
	strip.t0 = strip.t1 - memory_;
	strip.span = strip.t1 - strip.t0;
	strip.columns = std::max(1.0, box.width());
	if (strip.t1 > strip.t0) {
		/* the lines: drawn again when the data has moved a pixel on the strip, or the lines or its size changed */
		const QRect device = p.deviceTransform().mapRect(box).toAlignedRect();
		if (stripImage_.size() != device.size() || stripImage_.devicePixelRatio() != p.device()->devicePixelRatioF()
				|| stripGeneration_ != seriesGeneration_ || stripMemory_ != memory_ || stripLog_ != logShown() || strip.t1 < stripEnd_
				|| strip.t1 - stripEnd_ >= strip.columnSeconds()) {
			QTransform world;
			prepareTile(p, device, stripImage_, world, stripAt_);
			QPainter ip(&stripImage_);
			ip.setRenderHint(QPainter::Antialiasing);
			ip.setWorldTransform(world);
			ip.setClipRect(box.adjusted(1, 1, -1, -1)); /* inside the border */
			drawMemoryLines(ip, strip);
			ip.end();
			stripImage_.setDevicePixelRatio(p.device()->devicePixelRatioF());
			stripEnd_ = strip.t1;
			stripMemory_ = memory_;
			stripLog_ = logShown();
			stripGeneration_ = seriesGeneration_;
		}
		p.drawImage(stripAt_, stripImage_);
		/* the view on it */
		const double va = std::max(strip.x(axes.t0), box.left()), vb = std::min(strip.x(axes.t1), box.right());
		QColor fill = c.accent;
		fill.setAlpha(45);
		p.setPen(QPen(c.accent, 1.2));
		p.setBrush(fill);
		p.drawRoundedRect(QRectF(va, box.top() + 1, std::max(3.0, vb - va), box.height() - 2), 3, 3);
		/* still filling: how much is kept, in the empty part when there is room */
		double k0, k1;
		memorySpan(k0, k1);
		const double emptyW = strip.x(k0) - box.left();
		if (k1 - k0 < memory_ * 0.98 && emptyW > 150) {
			p.setFont(smallFont());
			p.setPen(c.muted);
			p.drawText(QRectF(box.left() + 8, box.top(), emptyW - 16, box.height()), Qt::AlignLeft | Qt::AlignVCenter,
					capped_ ? tr("memory full: %1 of %2 kept (%3 lines)")
									.arg(formatDuration(k1 - k0), formatDuration(memory_)).arg(series_.size())
							: tr("filling: %1 of %2 kept").arg(formatDuration(k1 - k0), formatDuration(memory_)));
		}
	}
	p.setFont(smallFont());
	p.setPen(c.muted);
	p.drawText(QRectF(2, box.top(), axes.rect.left() - 8, box.height()), Qt::AlignRight | Qt::AlignVCenter,
			tr("memory"));
}

/* the lines on the memory strip, each in its own range, thin and faded */
void ChartView::drawMemoryLines(QPainter &p, const Axes &strip) const {
	QVector<const Series *> lines;
	lines.reserve(series_.size());
	for (const Series &s : series_) lines << &s;
	QVector<BinnedLine> all(lines.size());
	inParallel(lines.size(), [&](qsizetype i) { binSeries(*lines[i], strip.t0, strip.t1, strip.columns, true, all[i]); });
	for (const BinnedLine &binned : std::as_const(all)) {
		const Series &s = *binned.series;
		if (binned.bins.isEmpty()) continue;
		Axes line = strip;
		if (logShown() && binned.hi > 0) {
			/* Log: the line's positive values, at most MAX_DECADES, and a decade at least (as the plot's Auto) */
			const double top = std::log10(binned.hi), bottom = std::max(std::log10(binned.posLo), top - MAX_DECADES);
			const double middle = (top + bottom) / 2, half = std::max(0.5, (top - bottom) / 2);
			line.setRange(std::pow(10.0, middle - half), std::pow(10.0, middle + half), true);
		} else {
			double lo = binned.lo, hi = binned.hi;
			widenFlatRange(lo, hi);
			line.setRange(lo, hi, false);
		}
		QColor color = s.color;
		color.setAlpha(STRIP_ALPHA);
		const auto x = [&line](double t) { return line.x(t); };
		const auto y = [&line](double v) { return line.y(v); };
		strokePolyline(p, toPolyline(binned.bins, strip.columnSeconds(), x, y), color, true, 1);
	}
}

/* The legend: a chip per line across the top with its latest value (taken at
 * the values' pace by frame()), each at a fixed place beside the state. Chips
 * that do not fit are reached with the
 * scroll bar under them; with no line yet, a hint in the plot. */
void ChartView::drawLegend(QPainter &p, const Axes &axes) const {
	p.setFont(labelFont());
	if (series_.isEmpty()) {
		p.setPen(Theme::colors().muted);
		p.drawText(axes.rect, Qt::AlignCenter, tr("Tick \"Plot\" on any register to chart it"));
		return;
	}
	const LegendLayout legend = legendLayout(axes.rect);
	const double offset = legendOffset(legend);
	/* a picture of the chips and their bar, made again only when what it shows changes: its digits change at the
	 * values' pace, not at every frame (drawn at every frame it took 1.3 ms at 4K) */
	const qreal dpr = p.device()->devicePixelRatioF();
	const QRectF area(legend.viewport.left(), LEGEND_TOP, legend.viewport.width(), LEGEND_BAR_Y + LEGEND_BAR_H + 1 - LEGEND_TOP);
	const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7").arg(seriesGeneration_).arg(valuesTick_).arg(offset)
			.arg(area.width()).arg(area.left()).arg(dpr).arg(Theme::isDark());
	if (key != legendKey_ || legendImage_.isNull()) {
		legendKey_ = key;
		legendBuilds_++;
		legendImage_ = QImage((area.size() * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
		legendImage_.setDevicePixelRatio(dpr);
		legendImage_.fill(Qt::transparent);
		QPainter lp(&legendImage_);
		lp.setRenderHint(QPainter::Antialiasing);
		lp.setFont(p.font());
		lp.translate(-area.topLeft());
		/* a chip cut by the scroll's edge is cut before the arrow there, not drawn under it */
		const bool more = legend.maxScroll() > 0;
		lp.setClipRect(legend.viewport.adjusted(more && offset > 0 ? LEGEND_ARROW_W : 0, 0,
				more && offset < legend.maxScroll() ? -LEGEND_ARROW_W : 0, 0));
		qsizetype i = 0;
		for (const Series &s : series_) {
			const QRectF chip = legend.chips[i++].translated(-offset, 0);
			if (chip.right() >= legend.viewport.left() && chip.left() <= legend.viewport.right())
				drawChip(lp, s, chip, legend.valueRoom);
		}
		lp.setClipping(false);
		if (legend.maxScroll() > 0) drawLegendBar(lp, legend, offset);
	}
	p.drawImage(area.topLeft(), legendImage_);
}

/* one chip: the line's dot and name on the left; its value right-aligned in
 * the room every value gets, then the unit, so only the digits change */
void ChartView::drawChip(QPainter &p, const Series &s, const QRectF &chip, double valueRoom) const {
	const ThemeColors &c = Theme::colors();
	p.setPen(Qt::NoPen);
	p.setBrush(c.surface2);
	p.drawRoundedRect(chip, LEGEND_ROW_H / 2, LEGEND_ROW_H / 2);
	p.setBrush(s.color);
	p.drawEllipse(QPointF(chip.left() + 11, chip.center().y()), 4, 4);
	p.setPen(c.text);
	const QRectF text = chip.adjusted(CHIP_TEXT_LEFT, 0, -CHIP_PAD_RIGHT, 0);
	p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, s.name);
	if (!s.hasShown) return;
	const QString unit = s.unit.isEmpty() ? QString() : QLatin1Char(' ') + s.unit;
	const double unitW = QFontMetricsF(p.font()).horizontalAdvance(unit);
	const QRectF value(text.right() - unitW - valueRoom, chip.top(), valueRoom, chip.height());
	p.drawText(value, Qt::AlignVCenter | Qt::AlignRight, chartNumber(s.shown));
	if (!unit.isEmpty())
		p.drawText(QRectF(value.right(), chip.top(), unitW, chip.height()), Qt::AlignVCenter | Qt::AlignLeft, unit);
}

/* the chips overflow: a thin bar under them with a thumb for the part in view,
 * and a mark at each end of the row where more chips lie */
void ChartView::drawLegendBar(QPainter &p, const LegendLayout &legend, double offset) const {
	const ThemeColors &c = Theme::colors();
	const double radius = LEGEND_BAR_H / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(c.surface2);
	p.drawRoundedRect(legendTrack(legend), radius, radius);
	p.setBrush(c.muted);
	p.drawRoundedRect(legendThumb(legend, offset), radius, radius);
	for (const bool right : { false, true }) {
		if (right ? offset >= legend.maxScroll() : offset <= 0) continue;
		const QRectF mark = legendArrow(legend, right);
		p.setBrush(c.surface); /* the chart's own background, over the chip cut at the edge */
		p.drawRect(mark);
		const double cx = mark.center().x(), cy = mark.center().y(), d = right ? 3.5 : -3.5;
		const QPointF tip[3] = { { cx + d, cy }, { cx - d, cy - 5 }, { cx - d, cy + 5 } };
		p.setBrush(c.text);
		p.drawPolygon(tip, 3);
	}
}

/* the crosshair: a dashed line at the mouse, a dot on every line near it, and
 * their values in a box */
bool ChartView::crosshair(const Axes &axes, const QVector<Lane> &plots, const QVector<BinnedLine> &lines, qreal dpr,
		Crosshair &out) const {
	const QRectF &plot = axes.rect;
	if (drag_ != Drag::None || mouseX_ < plot.left() || mouseX_ > plot.right() || series_.isEmpty()) return false;
	const double t = axes.t0 + (mouseX_ - plot.left()) / plot.width() * window_;
	out.x = mouseX_;
	/* the box's texts only when it is made again: at the values' pace (the legend's), as the mouse moves (at most
	 * every READOUT_FOLLOW_MS: made at every frame while the mouse moved, its 4.5 ms at 4K held the chart near 46
	 * frames a second), or the plot, scaling or theme changed. 64 values laid out and drawn at every frame took 7 ms
	 * at 4K; digits that change at every frame cannot be read anyway. The box and the dots follow the mouse at every
	 * frame. */
	const bool moved = readoutMouseX_ != mouseX_ && (!readoutMade_.isValid() || readoutMade_.elapsed() >= READOUT_FOLLOW_MS);
	const bool remake = readoutTick_ != valuesTick_ || moved || readoutPlotHeight_ != plot.height() || readoutDpr_ != dpr
			|| readoutDark_ != Theme::isDark();
	/* each line's dot in its plot (lanes: its lane; none on a folded one, whose lines are not in the box either) */
	QVector<const Axes *> axesOf(lines.size(), &axes);
	for (const Lane &plot : plots)
		for (int i : plot.lines) axesOf[i] = plot.folded ? nullptr : &plot.axes;
	QVector<ReadoutRow> rows;
	for (qsizetype i = 0; i < lines.size(); i++) {
		const BinnedLine &line = lines[i];
		if (!axesOf[i]) continue;
		const Axes &lineAxes = *axesOf[i];
		const Series &s = *line.series;
		if (s.times.isEmpty()) continue;
		const qsizetype k = nearestIndex(s.times, t);
		if (std::fabs(s.times[k] - t) > window_ / READOUT_REACH) continue;
		const double v = s.values[k];
		if (remake && hoverValues_) rows.push_back({ s.name, chartNumber(v), s.unit, s.color });
		double lo = line.lo, hi = line.hi; /* Normalize: the line's own range, as drawLines scales it */
		widenFlatRange(lo, hi);
		const double y = normalized_ ? lineAxes.y((v - lo) / (hi - lo)) : lineAxes.y(v);
		if (lanes_ && (y < plot.top() - 1 || y > plot.bottom() + 1)) continue; /* scrolled away: in the box, no dot */
		out.dots.push_back({ QPointF(axes.x(s.times[k]), y), s.color });
	}
	if (remake) {
		const QString timeText = QStringLiteral("%1   -%2 s").arg(
				wallClock(epochMs_, t).toString(QStringLiteral("HH:mm:ss.zzz")), chartNumber(clockNow() - t));
		readout_ = rows.isEmpty() ? QImage() : readoutPicture(plot.height(), timeText, rows, dpr);
		readoutTick_ = valuesTick_;
		readoutMouseX_ = mouseX_;
		readoutPlotHeight_ = plot.height();
		readoutDpr_ = dpr;
		readoutDark_ = Theme::isDark();
		readoutBuilds_++;
		readoutMade_.restart();
	}
	if (!readout_.isNull()) {
		/* right of the mouse, or left of it when there is no room; inside the plot */
		const double w = readout_.deviceIndependentSize().width();
		double left = mouseX_ + 14;
		if (left + w > plot.right()) left = mouseX_ - 14 - w;
		out.boxAt = QPointF(std::max(left, plot.left()), plot.top() + 8);
	}
	return true;
}

void ChartView::drawCrosshair(QPainter &p, const Axes &axes, const QVector<Lane> &plots,
		const QVector<BinnedLine> &lines) const {
	const qreal dpr = p.device()->devicePixelRatioF();
	Crosshair hair;
	if (!crosshair(axes, plots, lines, dpr, hair)) return;
	const QRectF &plot = axes.rect;
	p.setPen(QPen(Theme::colors().muted, 1, Qt::DashLine));
	p.drawLine(QPointF(hair.x, plot.top()), QPointF(hair.x, plot.bottom()));
	/* stamped: one small picture per colour (64 antialiased circles took 1.4 ms a frame) */
	for (const auto &dot : std::as_const(hair.dots))
		p.drawImage(dot.first - QPointF(DOT_PICTURE, DOT_PICTURE) / 2, dotPicture(dot.second, dpr));
	if (!readout_.isNull()) drawBoxPicture(p, hair.boxAt, readout_, 8);
}

/* The crosshair's box: the time, then each line's value by a dot in its colour, in as many columns as the plot's
 * height needs (64 lines do not fit one). Every column has room for the longest name, the widest number (as the
 * legend gives) and the longest unit, the number right-aligned in its room: the box and its columns keep their
 * places while the digits change (it moved by the widest value of the moment). The base is kept; only the time
 * and the numbers are written, on a copy of it. */
QImage ChartView::readoutPicture(double plotHeight, const QString &timeText, const QVector<ReadoutRow> &rows,
		qreal dpr) const {
	const ReadoutBase &base = readoutBase(plotHeight, rows, dpr);
	/* into the last box's memory when it is the base's size: a new 6 MB picture each time (4K) cost more in
	 * fresh memory than the copy itself */
	QImage image;
	std::swap(image, readout_);
	if (image.size() != base.image.size() || image.format() != base.image.format())
		image = QImage(base.image.size(), base.image.format());
	std::memcpy(image.bits(), base.image.constBits(), size_t(base.image.sizeInBytes()));
	image.setDevicePixelRatio(dpr);
	QPainter p(&image);
	p.setFont(labelFont());
	const QFontMetricsF metrics(p.font());
	const double baseline = (READOUT_ROW_H + metrics.ascent() - metrics.descent()) / 2;
	p.setPen(Theme::colors().muted);
	p.drawText(QPointF(20.5, 5.5 + baseline), timeText);
	p.setPen(Theme::colors().text);
	for (int i = 0; i < rows.size(); i++) {
		const double x = 0.5 + (i / base.perColumn) * base.columnW + 20;
		const double y = 5.5 + (i % base.perColumn + 1) * READOUT_ROW_H;
		const double right = x + base.nameW + base.gap + base.valueRoom;
		p.drawText(QPointF(right - metrics.horizontalAdvance(rows[i].value), y + baseline), rows[i].value);
	}
	return image;
}

const ChartView::ReadoutBase &ChartView::readoutBase(double plotHeight, const QVector<ReadoutRow> &rows, qreal dpr) const {
	QString key = QStringLiteral("%1|%2|%3").arg(plotHeight).arg(dpr).arg(Theme::isDark());
	for (const ReadoutRow &row : rows) key += QLatin1Char('|') + row.name + QLatin1Char('|') + row.unit
			+ QLatin1Char('|') + row.color.name();
	if (key == readoutBase_.key) return readoutBase_;
	ReadoutBase &base = readoutBase_;
	base.key = key;
	const ThemeColors &c = Theme::colors();
	const QFont font = labelFont();
	const QFontMetricsF metrics(font);
	base.perColumn = readoutRowsPerColumn(plotHeight);
	const int columns = int((rows.size() + base.perColumn - 1) / base.perColumn);
	const int rowsShown = int(std::min<qsizetype>(rows.size(), base.perColumn));
	base.nameW = base.unitW = 0;
	for (const ReadoutRow &row : rows) {
		base.nameW = std::max(base.nameW, metrics.horizontalAdvance(row.name));
		if (!row.unit.isEmpty()) base.unitW = std::max(base.unitW, metrics.horizontalAdvance(QLatin1Char(' ') + row.unit));
	}
	base.gap = metrics.horizontalAdvance(QStringLiteral("  "));
	base.valueRoom = metrics.horizontalAdvance(widestChartNumber());
	base.columnW = 20 + base.nameW + base.gap + base.valueRoom + base.unitW + 10;
	/* the time row as wide as it can get, so it does not move the box either */
	const double timeW = metrics.horizontalAdvance(QStringLiteral("00:00:00.000   -%1 s").arg(widestChartNumber()));
	base.width = std::max(timeW + 30, columns * base.columnW + 4);
	const double h = (rowsShown + 1) * READOUT_ROW_H + 10;

	base.image = QImage((QSizeF(base.width + 1, h + 1) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
	base.image.setDevicePixelRatio(dpr);
	base.image.fill(Qt::transparent);
	QPainter p(&base.image);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(font);
	const QRectF box(0.5, 0.5, base.width, h);
	p.setPen(QPen(c.border, 1));
	p.setBrush(c.surface2);
	p.drawRoundedRect(box, 8, 8);
	for (int i = 0; i < rows.size(); i++) {
		const ReadoutRow &row = rows[i];
		const double x = box.left() + (i / base.perColumn) * base.columnW + 20;
		const double y = box.top() + 5 + (i % base.perColumn + 1) * READOUT_ROW_H;
		p.setPen(Qt::NoPen);
		p.setBrush(row.color);
		p.drawEllipse(QPointF(x - 9, y + READOUT_ROW_H / 2), 3.5, 3.5);
		p.setPen(c.text);
		p.drawText(QRectF(x, y, base.nameW, READOUT_ROW_H), Qt::AlignVCenter | Qt::AlignLeft, row.name);
		if (!row.unit.isEmpty())
			p.drawText(QRectF(x + base.nameW + base.gap + base.valueRoom, y, base.unitW, READOUT_ROW_H),
					Qt::AlignVCenter | Qt::AlignLeft, QLatin1Char(' ') + row.unit);
	}
	return base;
}

/* A picture of a rounded box (opaque inside): the rows of its corners and its side edges blended, the rest
 * copied, a third of a blend's cost (the crosshair's box is 1.6 M pixels at 4K). The pieces share the picture's
 * memory; at goes on a whole device pixel, so they meet exactly. */
void ChartView::drawBoxPicture(QPainter &p, QPointF at, const QImage &image, double radius) {
	const QTransform toDevice = p.deviceTransform();
	const QPointF device = toDevice.map(at);
	at = toDevice.inverted().map(QPointF(std::round(device.x()), std::round(device.y())));
	const qreal dpr = image.devicePixelRatio();
	const int w = image.width(), h = image.height();
	const int band = std::min(h / 2, int(std::ceil(radius * dpr)) + 1);
	const int edge = std::min(w / 2, int(std::ceil(2 * dpr)) + 1);
	auto piece = [&](int x, int y, int pw, int ph) {
		if (pw <= 0 || ph <= 0) return;
		QImage part(image.constBits() + qsizetype(y) * image.bytesPerLine() + x * 4, pw, ph, image.bytesPerLine(),
				image.format());
		part.setDevicePixelRatio(dpr);
		p.drawImage(QPointF(at.x() + x / dpr, at.y() + y / dpr), part);
	};
	piece(0, 0, w, band);
	piece(0, h - band, w, band);
	piece(0, band, edge, h - 2 * band);
	piece(w - edge, band, edge, h - 2 * band);
	const QPainter::CompositionMode mode = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	piece(edge, band, w - 2 * edge, h - 2 * band);
	p.setCompositionMode(mode);
}

const QImage &ChartView::dotPicture(const QColor &color, qreal dpr) const {
	if (dpr != dotsDpr_) {
		dots_.clear();
		dotsDpr_ = dpr;
	}
	auto it = dots_.find(color.rgba());
	if (it != dots_.end()) return *it;
	QImage dot((QSizeF(DOT_PICTURE, DOT_PICTURE) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
	dot.setDevicePixelRatio(dpr);
	dot.fill(Qt::transparent);
	QPainter p(&dot);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawEllipse(QPointF(DOT_PICTURE / 2, DOT_PICTURE / 2), 3.5, 3.5);
	p.end();
	return *dots_.insert(color.rgba(), dot);
}

int ChartView::readoutRowsPerColumn(double plotHeight) {
	/* the box's top margin, the time row and the box's padding off the plot's height */
	return std::max(1, int((plotHeight - 8 - 10) / READOUT_ROW_H) - 1);
}

/* The state corner's texts: the whole, then shortened in turn until one fits (fitState): "Live to follow" goes, then
 * the cursors' "click / drag", then "manual" of a manual Log Y; the time held and the trigger's state stay. With
 * measuring, the time held is written as the widest number, so the room kept for it does not change with its digits
 * (the legend's end would follow them). */
QStringList ChartView::stateVariants(bool measuring) const {
	QString held[2], y[2], cursors[2], trigger;
	if (!live_ && !recording_) { /* a trigger holds a view that ends after now: it fills as the samples come */
		const double behind = clockNow() - viewEnd();
		const QString number = measuring ? QStringLiteral("0.000e+00") : chartNumber(std::fabs(behind));
		held[0] = behind >= 0 ? tr("held: -%1 s · Live to follow").arg(number)
				: tr("held: filling, %1 s to come · Live to follow").arg(number);
		held[1] = behind >= 0 ? tr("held: -%1 s").arg(number) : tr("held: filling");
	}
	if (lanes_) {
		/* each lane its own Y range; their buttons show the fold */
	} else if (logShown()) {
		y[0] = y_.autoRange ? tr("Y log") : tr("Y log, manual");
		y[1] = tr("Y log");
	} else if (!y_.autoRange && !normalized_) {
		y[0] = y[1] = tr("Y manual");
	}
	if (cursorMode_) {
		cursors[0] = tr("cursors: click / drag");
		cursors[1] = tr("cursors");
	}
	if (trigger_.on) trigger = trigger_.armed ? tr("trigger: armed") : std::isfinite(trigger_.at) ? tr("triggered")
			: tr("trigger: Arm");
	QStringList variants;
	for (int stage = 0; stage < 4; stage++) {
		QStringList parts{ held[stage >= 1], y[stage >= 3], cursors[stage >= 2], trigger };
		parts.removeAll(QString());
		variants << parts.join(QStringLiteral("  ·  "));
	}
	return variants;
}

/* the first of the state's texts that fits its room: at most STATE_SHARE of the plot; the shortest may take more
 * rather than be cut, but never so much that the legend loses its first chip and its arrows; -1: no state */
void ChartView::fitState(double plotWidth, int &variant, double &width) const {
	variant = -1;
	width = 0;
	const QStringList variants = stateVariants(true);
	if (variants.first().isEmpty()) return;
	const QFontMetricsF metrics(labelFont());
	const double legendMin = (chipWidths_.isEmpty() ? 0 : chipWidths_.first()) + 2 * LEGEND_ARROW_W + STATE_GAP;
	const double most = std::max(0.0, plotWidth - legendMin);
	const double room = std::min(plotWidth * STATE_SHARE, most);
	for (qsizetype i = 0; i < variants.size(); i++) {
		width = std::ceil(metrics.horizontalAdvance(variants[i]));
		variant = int(i);
		if (width <= room) return;
	}
	if (width <= most) return; /* the shortest, whole, past its share */
	width = most;              /* a chart too narrow even for that: it ends with "…" (drawState) */
}

/* the state, top right: held, manual Y, cursor mode, the trigger; as much of it as fits, the whole in its tooltip */
void ChartView::drawState(QPainter &p, const Axes &axes) const {
	const QStringList texts = stateVariants(false);
	int variant = -1;
	double width = 0;
	fitState(axes.rect.width(), variant, width);
	stateFull_ = texts.first();
	stateText_.clear();
	stateRect_ = QRectF();
	if (variant < 0) return;
	const QFontMetricsF metrics(labelFont());
	stateText_ = texts[variant];
	if (metrics.horizontalAdvance(stateText_) > width) stateText_ = metrics.elidedText(stateText_, Qt::ElideRight, width);
	stateRect_ = QRectF(axes.rect.right() - width, LEGEND_TOP, width, LEGEND_ROW_H);
	const ThemeColors &c = Theme::colors();
	p.save();
	p.setFont(labelFont());
	p.setPen(!live_ ? c.warn : c.muted);
	/* words, not the chart's time: read in the language's direction (Arabic from the right, its first part rightmost),
	 * still at the chart's right end; right to left, a mark either side of each dot keeps a part's Latin end ("s") and
	 * the next part's Latin start ("Y") from running together into one left-to-right run */
	const bool rightToLeft = QGuiApplication::layoutDirection() == Qt::RightToLeft;
	p.setLayoutDirection(QGuiApplication::layoutDirection());
	const QString dot = QStringLiteral("  ·  ");
	p.drawText(stateRect_, Qt::AlignRight | Qt::AlignAbsolute | Qt::AlignVCenter,
			rightToLeft ? QString(stateText_).replace(dot, QChar(0x200F) + dot + QChar(0x200F)) : stateText_);
	p.restore();
}

/* the paint time as a running average; the frames counted over each second */
void ChartView::updatePaintStats(double paintMs) {
	paintMs_ = paintMs_ * 0.9 + paintMs * 0.1;
	fpsFrames_++;
	if (fpsClock_.elapsed() >= 1000) {
		fps_ = fpsFrames_ * 1000.0 / double(fpsClock_.elapsed());
		fpsFrames_ = 0;
		fpsClock_.restart();
	}
}

/* -------------------------------------------------------------- ChartWidget */

ChartWidget::ChartWidget(QWidget *parent) : QWidget(parent), view_(new ChartView(this)) {
	setMinimumHeight(260);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(view_);
	connect(view_, &ChartView::windowChangedByUser, this, &ChartWidget::windowChangedByUser);
	connect(view_, &ChartView::yChangedByUser, this, &ChartWidget::yChangedByUser);
}

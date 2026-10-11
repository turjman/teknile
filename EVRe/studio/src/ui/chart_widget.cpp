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
#include <QStyleHints>
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
/* The trigger on (the user's): a margin right of the plot for the level's tab, a column left of it (between the value
 * labels and the plot) for the level's marker and a strip above it, under the legend, for the position's flag, so
 * none lies over the data (the owner: a handle over the newest samples and a mark between the plot and the time
 * labels read badly). The tab starts past the lanes' scroll bar (LANE_BAR_X + LANE_BAR_W), so the two never meet; all
 * lie outside the card's layer (2 px around the plot), their points on its edge or beyond */
constexpr double TRIGGER_TAB_X = 15;       /* the tab's left (its point), right of the plot */
constexpr double TRIGGER_TAB_W = 80;       /* "0.4 A", "-2.5 V", "100 mA" whole before the edge's part */
constexpr double TRIGGER_TAB_H = 24;       /* the grab area of the tab and the marker, about three times the old handle's */
constexpr double TRIGGER_TAB_BUTTON = 18;  /* its edge part, at its right end */
constexpr double TRIGGER_PAD = TRIGGER_TAB_X + TRIGGER_TAB_W + 3; /* the right pad with the trigger on */
/* The marks are one family (the owner: one finished look): boxes of one height, corners, border and font, each joined
 * to a pointer of one size in the line's colour */
constexpr double TRIGGER_MARK_H = 18;      /* a mark's box: the chart's small font with even room above and below */
constexpr double TRIGGER_MARK_W = 16;      /* a box with a T: the flag and the level's marker */
constexpr double TRIGGER_POINT = 5;        /* a pointer's depth */
constexpr double TRIGGER_POINT_W = 10;     /* and its base */
constexpr double TRIGGER_LEFT_W = TRIGGER_MARK_W + TRIGGER_POINT; /* the level's marker, its point 2 px left of the plot */
constexpr double TRIGGER_STRIP_H = 26;     /* the flag's strip: 1 px under the legend, its box, its point, 2 px to the plot */
constexpr double TIME_AXIS_H = 30;         /* the time labels, under the plot */
constexpr double OVERVIEW_H = 30;          /* the memory strip, under the time labels */
constexpr double BOTTOM_PAD = 8;
constexpr double LANE_FIT_SLACK = 0.01;   /* px: lanes that fill the plot fit, their sum's rounding past it ignored */
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
constexpr double RANGE_TAG_H = 14;         /* a lane's range tag ("Manual", "Log") at the top of its value labels */
constexpr double BADGE_PAD = 6;            /* the short window's lock badge: its text's margin either side */
constexpr double BADGE_GAP = 8;            /* between the state's other words and the badge */
constexpr double DIVISION_PAD = 6;         /* the time/div readout's box: its text's margin either side */
constexpr double DIVISION_GAP = 10;        /* between the time/div readout and the state's text */
constexpr double LANE_WHEEL_STEP = 40;     /* pixels per wheel notch over the lanes' value labels */
constexpr double LANE_BAR_X = 6;           /* the lanes' scroll bar: this far right of the plot, in its right pad */
constexpr double LANE_BAR_W = 6;
constexpr double LANE_BAR_GRIP = 4;        /* the bar takes clicks this far either side of it */
constexpr double LANE_HANDLE_MIN = 24;
constexpr double FOLDED_ITEM_GAP = 14;     /* between the items of a folded strip */
constexpr double CHIP_GAP = 6;             /* between two chips */
constexpr double CHIP_TEXT_LEFT = 20;      /* a chip's text starts after its dot */
constexpr double CHIP_PAD_RIGHT = 8;
constexpr double CHIP_BUTTON_W = 16;       /* the chip's menu button ("▾") at its right end, after CHIP_PAD_RIGHT */
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
constexpr qint64 DIVISION_CLOCK_MS = 500;  /* the divisions' readout: its clock time while live, at most this often */

/* the view (the longest: ChartView::MAX_SPAN) */
constexpr double MIN_WINDOW = 1e-5;   /* seconds: 10 us, a fast line's single records */
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
constexpr qint64 RELEASE_NS = 3000000; /* a frame's time for freeing what the fast stores' trims let go */
constexpr qsizetype POINTS_PER_STRIPE = 20000; /* the lines' points that make drawing on threads worth it */
constexpr int STRIPE_OVERLAP = 8; /* device pixels each stripe draws past its edges: an image's own edge pixels are
                                     antialiased a little differently, and are never shown */
constexpr double FRAME_SHARE = 0.6;            /* the chart's frames take at most this share of the thread */
constexpr qsizetype TRIM_PER_FRAME = 4000000; /* samples moved by trims in a frame at most (about 5 ms) */
constexpr double BUDGET_SAMPLE_MS = 30;        /* a slow paint spends at most this (or twice the frames' average) */
constexpr int LAYER_AFTER_FRAMES = 2;          /* the card's layer shown after this many frames under it (paintFrame) */
constexpr qint64 FRAMES_STOPPED_MS = 250;      /* no frame() this long: a change is painted at once (refresh) */
constexpr int DROPPED_AGAIN_MS = 16;            /* a held view's card frame let go: painted again after this */

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

/* a length of time for the memory strip: seconds, minutes or hours (one piece in Arabic: ltrPiece) */
QString formatDuration(double seconds) {
	if (seconds < 120) return ltrPiece(QStringLiteral("%1 s").arg(seconds, 0, 'f', 0));
	if (seconds < 7200) return ltrPiece(QStringLiteral("%1 min").arg(seconds / 60, 0, 'f', 1));
	return ltrPiece(QStringLiteral("%1 h").arg(seconds / 3600, 0, 'f', 1));
}

QDateTime wallClock(qint64 epochMsAtZero, double t) {
	return QDateTime::fromMSecsSinceEpoch(epochMsAtZero + qint64(std::llround(t * 1000)));
}

/* a time grid label, as precise as the grid step needs: below a millisecond (a fast line's records) the microseconds
 * after the milliseconds, "14:03:12.345678" */
QString timeLabel(qint64 epochMsAtZero, double t, double step) {
	if (step < 1e-3) {
		const qint64 micro = epochMsAtZero * 1000 + qint64(std::llround(t * 1e6));
		const qint64 ms = micro >= 0 ? micro / 1000 : (micro - 999) / 1000;
		const QString label = QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"))
				+ QStringLiteral("%1").arg(micro - ms * 1000, 3, 10, QLatin1Char('0'));
		return step >= 1e-5 ? label.chopped(1) : label; /* tens of microseconds: five decimals */
	}
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

/* a lane's range tag: smaller than the value labels, so "Manual" fits their column beside the lane's buttons */
QFont tagFont() {
	QFont font = QGuiApplication::font();
	font.setPointSizeF(7.5);
	return font;
}

/* the Log scale's lines at 2..9 of a decade: the grid's colour, fainter */
QColor faintGrid() {
	QColor faint = Theme::colors().grid;
	faint.setAlphaF(faint.alphaF() * 0.45);
	return faint;
}

/* the trigger's "now" edge: muted text, half seen, so it reads over the grid and under the lines in both themes */
QColor nowEdgeColor() {
	QColor edge = Theme::colors().muted;
	edge.setAlphaF(0.55);
	return edge;
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
	framesStopped_ = new QTimer(this);
	framesStopped_->setSingleShot(true);
	connect(framesStopped_, &QTimer::timeout, this, &ChartView::refresh);
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
	if (isFastKey(key)) { /* a fast stream's channel: its records are its stream's */
		const int stream = (key - FIRST_FAST_KEY) / 256;
		s.fast = fastStores_.value(stream);
		s.channel = (key - FIRST_FAST_KEY) % 256;
		if (!s.fast || s.channel >= s.fast->channels()) return;
		s.totalTo = s.fast->dropped() + s.fast->size(); /* its total from the records that come now */
		if (s.fast->mapped()) { /* a recording's: its total over all of it */
			s.total = 0;
			s.totalT = NAN;
			s.totalTo = 0;
			sumFast(s);
		}
	}
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
	/* the engine stops looking for the crossings of a fast line that is gone (fastTriggerWatch: none) */
	if (trigger_.on && key == trigger_.key) postWatch();
	forgetRanges();
	refresh();
}

void ChartView::clearSeries() {
	const bool watched = trigger_.on && series_.contains(trigger_.key);
	series_.clear();
	if (watched) postWatch();
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
	for (const auto &store : std::as_const(fastStores_))
		if (!store->mapped()) store->clear(); /* a recording's stays: nothing comes after the file */
	for (Series &s : series_) {
		s.totalTo = 0;
		if (s.fast && s.fast->mapped()) sumFast(s);
	}
	keptTotals_.clear();
	totalsSince_ = NAN; /* the first sample from now on */
	/* the trigger: a crossing waiting for its view lay in what went. The engine is armed again: its next crossing may
	 * pair a block's first record with one cleared here, which the view cannot hold on (fastCrossings) */
	if (trigger_.on) {
		trigger_.pending = NAN;
		postWatch();
	}
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
	double samples = 0, fastBytes = 0;
	QSet<const fast::Store *> counted;
	for (const Series &s : series_) {
		if (s.fast) { /* a fast stream once, all its channels in its records */
			const fast::Store &store = *s.fast;
			if (counted.contains(&store) || store.size() < 2 || !store.hasTime()) continue;
			counted.insert(&store);
			const double rate = double(store.size() - 1) / std::max(1e-9, store.timeAt(store.size() - 1) - store.timeAt(0));
			fastBytes += rate * memory_ * store.bytesPerRecord();
			continue;
		}
		const qsizetype n = s.times.size();
		if (n < 2 || s.times.back() <= s.times.front()) continue;
		const double rate = double(n - 1) / (s.times.back() - s.times.front());
		samples += std::min(rate * memory_, double(MAX_POINTS));
	}
	return qint64(samples * BYTES_PER_SAMPLE + fastBytes);
}

qint64 ChartView::bytesHeld() const {
	qint64 bytes = 0;
	for (const Series &s : series_) {
		bytes += qint64(s.times.capacity() + s.values.capacity()) * qint64(sizeof(double));
		for (const QVector<Chunk> &level : s.chunks) bytes += qint64(level.capacity()) * qint64(sizeof(Chunk));
	}
	for (const auto &store : fastStores_) bytes += store->bytes();
	return bytes;
}

qsizetype ChartView::pointsKept(int key) const {
	const auto it = series_.find(key);
	if (it != series_.end() && it->fast) return it->fast->size();
	return it == series_.end() ? 0 : it->times.size();
}

void ChartView::setFastSummaries(bool on) {
	if (fastSummaries_ == on) return;
	fastSummaries_ = on;
	capped_ = false; /* the lines past their new share say so again at their next records */
	refresh();
}

ChartView::Tiers ChartView::tiers(double rate, double memory, double share, double perRecord, double perSummary) {
	Tiers out;
	if (rate <= 0 || memory <= 0 || perRecord <= perSummary) return out;
	out.kept = out.samples = memory;
	if (rate * memory * perRecord <= share) return out; /* every record whole */
	out.kept = std::min(memory, share / perSummary / rate);
	out.summaryBytes = out.kept * rate * perSummary;
	out.samples = std::max(0.0, (share - out.summaryBytes) / (perRecord - perSummary) / rate);
	return out;
}

bool ChartView::fastTiers(double &kept, double &samples) const {
	kept = samples = memory_;
	if (!fastSummaries_) return false;
	bool any = false;
	QSet<const fast::Store *> counted;
	for (const Series &s : series_) {
		if (!s.fast) continue;
		const fast::Store &store = *s.fast;
		if (counted.contains(&store) || store.mapped() || store.size() < 2 || !store.hasTime()) continue;
		counted.insert(&store);
		int lines = 0;
		for (const Series &t : series_) lines += t.fast == s.fast;
		const double rate = double(store.size() - 1) / std::max(1e-9, store.timeAt(store.size() - 1) - store.timeAt(0));
		const double share = double(ramInUse()) * 1024 * 1024 / double(series_.size()) * lines;
		const Tiers t = tiers(rate, memory_, share, store.bytesPerRecord(), store.bytesPerSummary());
		if (t.samples >= memory_) continue;
		any = true;
		kept = std::min(kept, t.kept);
		samples = std::min(samples, t.samples);
	}
	return any;
}

bool ChartView::summariesBefore(double &t) const {
	bool any = false;
	for (const Series &s : series_) {
		if (!s.fast || s.fast->recordsFrom() <= 0 || s.fast->recordsFrom() >= s.fast->size() || !s.fast->hasTime()) continue;
		const double from = s.fast->timeAt(s.fast->recordsFrom());
		t = any ? std::max(t, from) : from;
		any = true;
	}
	return any;
}

bool ChartView::summariesIn(int key, double t0, double t1, bool *only) const {
	if (only) *only = false;
	const auto it = series_.constFind(key);
	if (it == series_.constEnd() || !it->fast || it->fast->recordsFrom() <= 0) return false;
	const fast::Store &store = *it->fast;
	if (store.lowerBound(t0) >= store.recordsFrom()) return false;
	if (only) *only = store.upperBound(t1) <= store.recordsFrom();
	return true;
}

void ChartView::setRamBudget(int megabytes) {
	ramMB_ = std::max(megabytes, MIN_RAM_MB);
	capped_ = false; /* the lines past their new share say so again at their next sample */
	refresh();
}

void ChartView::setRamLimit(int megabytes) {
	megabytes = std::max(megabytes, 0);
	if (limitMB_ == megabytes) return;
	const int before = ramInUse();
	limitMB_ = megabytes;
	if (ramInUse() == before) return;
	capped_ = false; /* as for a budget changed: the lines past their new share say so again at their next sample */
	refresh();
}

qint64 ChartView::bytesReleasing() const {
	qint64 bytes = 0;
	for (const QByteArray &piece : released_.pieces) bytes += piece.capacity();
	for (const QVector<double> &summary : released_.summaries) bytes += qint64(summary.capacity()) * qint64(sizeof(double));
	return bytes;
}

void ChartView::setRecordingOn(bool on) {
	if (recordingOn_ == on) return;
	recordingOn_ = on;
	refresh();
}

/* the memory strip's tooltip: what it is and does, and with the RAM budget reached what that means */
QString ChartView::memoryStripTip() const {
	QString tip = tr("The memory: all the time the chart keeps (Memory), the view a box on it. Click or drag: the view "
			"goes there · Wheel: a window earlier or later");
	double since = 0;
	if (summariesBefore(since)) /* the shaded part */
		tip += QStringLiteral("\n\n") + tr("Older samples: summaries. The shaded part keeps only the lowest and highest "
				"value of each 256 samples of the fast lines (drawn as bars when zoomed in; their mean, RMS and area read "
				"\"—\"): samples for the newest %1.").arg(formatDuration(std::max(0.0, clockNow() - since)));
	if (!capped_) return tip;
	double k0, k1;
	memorySpan(k0, k1);
	tip += QStringLiteral("\n\n") + tr("RAM budget reached: the chart keeps its samples within the RAM set on the first "
			"row, so it keeps the last %1 of the Memory's %2 and lets the oldest go.")
			.arg(formatDuration(std::max(0.0, k1 - k0)), formatDuration(memory_));
	if (ramInUse() < ramMB_) { /* one piece each: "MB 512" in Arabic without it */
		const auto size = [](int megabytes) {
			return ltrPiece(megabytes < 1024 ? QStringLiteral("%1 MB").arg(megabytes)
							 : QStringLiteral("%1 GB").arg(megabytes / 1024.0, 0, 'f', 1));
		};
		tip += QLatin1Char('\n') + tr("The free memory limits the budget now: the chart keeps within %1 of the %2 set, so "
				"the computer does not page to disk.").arg(size(ramInUse()), size(ramMB_));
	}
	tip += QLatin1Char('\n') + (recordingOn_ ? tr("The recording running keeps every sample: its file holds them all.")
			: tr("A recording keeps every sample: its file holds what the chart lets go."));
	return tip;
}

qsizetype ChartView::pointsPerLine() const {
	const qsizetype lines = std::max<qsizetype>(1, series_.size());
	const qsizetype total = qsizetype(ramInUse()) * 1024 * 1024 / BYTES_PER_SAMPLE;
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
		const TriggerSettings watched = watchedSettings(); /* a short window's lock: its own */
		const double pt = s.times.back(), pv = s.values.back(), level = watched.level;
		const bool up = pv < level && v >= level, down = pv > level && v <= level;
		const TriggerEdge edge = watched.edge;
		const double at = v != pv ? pt + (level - pv) / (v - pv) * (t - pt) : t;
		if (((edge != TriggerEdge::Falling && up) || (edge != TriggerEdge::Rising && down)) && at > trigger_.armedFrom)
			crossed(at, t);
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

/* ----------------------------------------------------------- the fast lines */

void ChartView::setFastStream(int stream, const StreamDef &def) {
	const std::shared_ptr<fast::Store> kept = fastStores_.value(stream);
	if (kept && kept->def().name == def.name && kept->def().addr == def.addr && kept->recordSize() == def.recordSize()
			&& kept->channels() == def.channels.size())
		return;
	auto store = std::make_shared<fast::Store>(def);
	fastStores_.insert(stream, store);
	for (auto it = series_.begin(); it != series_.end(); ++it)
		if (isFastKey(it.key()) && (it.key() - FIRST_FAST_KEY) / 256 == stream) it->fast = store;
	/* a watched line's records start again in the new store: the engine is armed again from them */
	if (trigger_.on && isFastKey(trigger_.key) && (trigger_.key - FIRST_FAST_KEY) / 256 == stream) {
		trigger_.pending = NAN;
		postWatch();
	}
	seriesGeneration_++;
	refresh();
}

/* A stream gone from the map leaves no store behind: the same stream in a map loaded later starts afresh, not on the
 * records and times of a link that has long gone */
void ChartView::setFastStopped(int stream, bool stopped, const QString &name) {
	if (stopped ? fastStopped_.value(stream, QStringLiteral("\n")) == name : !fastStopped_.contains(stream)) return;
	if (stopped) fastStopped_.insert(stream, name);
	else fastStopped_.remove(stream);
	fastStoppedGen_++;
	refresh();
}

bool ChartView::lineStopped(int key) const {
	return !recording_ && isFastKey(key) && fastStopped_.contains(streamOf(key));
}

bool ChartView::lineResting(int key) const {
	if (!isFastKey(key)) return false;
	if (lineStopped(key)) return true;
	const double newest = newestTime(key);
	return std::isfinite(newest) && clockNow() - newest > std::max(REST_AFTER, 2 * window_);
}

double ChartView::fastNewest(int stream) const {
	const auto it = fastStores_.constFind(stream);
	if (it == fastStores_.constEnd() || !*it || (*it)->size() == 0 || !(*it)->hasTime()) return NAN;
	return (*it)->timeAt((*it)->size() - 1);
}

QString ChartView::triggerStoppedName() const {
	if (!trigger_.on || trigger_.automatic || !lineStopped(trigger_.key)) return QString();
	return fastStopped_.value(streamOf(trigger_.key));
}

QString ChartView::stoppedTip(int key) const {
	if (!lineStopped(key)) return QString();
	const double newest = fastNewest(streamOf(key));
	const QString name = fastStopped_.value(streamOf(key));
	if (!std::isfinite(newest)) return tr("%1 stopped: no record yet").arg(name);
	return tr("%1 stopped at %2: the value is its last record's").arg(name, ltrPiece(timeLabel(epochMs_, newest, 1e-3)));
}

/* one part per stream (a fast math line's stream says its source's name, once), with the time of its newest record
 * kept; a stream none of whose lines is on the chart, or with no record, says nothing */
QStringList ChartView::stoppedParts(bool whole) const {
	QStringList names;
	QVector<double> newest;
	for (auto it = series_.constBegin(); it != series_.constEnd() && !recording_; ++it) {
		if (!lineStopped(it.key())) continue;
		const int stream = streamOf(it.key());
		const double t = fastNewest(stream);
		if (!std::isfinite(t)) continue;
		const QString name = fastStopped_.value(stream);
		const qsizetype at = names.indexOf(name);
		if (at < 0) {
			names << name;
			newest << t;
		} else {
			newest[at] = std::max(newest[at], t);
		}
	}
	QStringList parts;
	for (qsizetype i = 0; i < names.size(); i++)
		parts << (whole ? tr("%1 stopped · last record %2", "a fast stream's name; the time of its newest record")
								  .arg(names[i], ltrPiece(timeLabel(epochMs_, newest[i], 1e-3)))
						: tr("%1 stopped", "a fast stream's name").arg(names[i]));
	return parts;
}

void ChartView::removeFastStream(int stream) {
	if (!fastStores_.remove(stream)) return;
	seriesGeneration_++;
	refresh();
}

void ChartView::clearFastStreams() {
	const bool watched = trigger_.on && isFastKey(trigger_.key) && series_.contains(trigger_.key);
	for (auto it = series_.begin(); it != series_.end();) {
		if (it->fast) it = series_.erase(it);
		else ++it;
	}
	fastStores_.clear();
	if (watched) postWatch();
	seriesGeneration_++;
	refresh();
}

const fast::Store *ChartView::fastStore(int stream) const { return fastStores_.value(stream).get(); }

void ChartView::setFastStore(int stream, std::shared_ptr<fast::Store> store) {
	fastStores_.insert(stream, store);
	for (auto it = series_.begin(); it != series_.end(); ++it) {
		if (!isFastKey(it.key()) || (it.key() - FIRST_FAST_KEY) / 256 != stream) continue;
		it->fast = store;
		it->total = 0;
		it->totalT = NAN;
		it->totalTo = 0;
		sumFast(*it);
	}
	seriesGeneration_++;
	forgetRanges();
	refresh();
}

QVector<ChartView::BinInfo> ChartView::lastBins(int key) const {
	QVector<BinInfo> out;
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return out;
	for (const BinnedLine &line : lastBinned_) {
		if (line.series != &*it) continue;
		for (const Bin &bin : line.bins) out.push_back({ bin.t0, bin.t1, bin.min, bin.max, bin.count, bin.gap, bin.bar });
	}
	return out;
}

QVector<ChartView::BinInfo> ChartView::freshBins(int key) const {
	QVector<BinInfo> out;
	const auto it = series_.constFind(key);
	if (it == series_.constEnd() || !it->fast || lastBinKey_.size() < 3) return out;
	return freshBins(key, lastBinKey_[0], lastBinKey_[1], lastBinKey_[2]);
}

QVector<ChartView::BinInfo> ChartView::freshBins(int key, double t0, double t1, double columns) const {
	QVector<BinInfo> out;
	const auto it = series_.constFind(key);
	if (it == series_.constEnd() || !it->fast) return out;
	BinnedLine line;
	binFast(*it, t0, t1, columns, line, nullptr);
	for (const Bin &bin : line.bins) out.push_back({ bin.t0, bin.t1, bin.min, bin.max, bin.count, bin.gap, bin.bar });
	return out;
}

/* A block's records into its stream's store, kept while one of its lines is on the chart; the legend's values from
 * the newest record */
qint64 ChartView::appendFast(int stream, quint64 first, qsizetype count, const QByteArray &records, bool newStart,
		quint64 lost) {
	const std::shared_ptr<fast::Store> store = fastStores_.value(stream);
	if (!store || count <= 0 || records.size() < count * store->recordSize()) return -1;
	int lines = 0;
	for (const Series &s : std::as_const(series_)) lines += s.fast == store;
	if (lines == 0) return -1;
	const qint64 at = store->dropped() + store->size();
	store->append(first, count, records.constData(), newStart, lost);
	for (Series &s : series_)
		if (s.fast == store) sumFast(s); /* before a trim: every record counts */
	trimFast(*store, lines);
	const qsizetype newest = store->size() - 1;
	for (Series &s : series_) {
		if (s.fast != store || newest < 0) continue;
		s.last = store->value(s.channel, newest);
		s.hasLast = true;
	}
	return at;
}

void ChartView::markFast(int stream, quint64 record, double time, double period) {
	const std::shared_ptr<fast::Store> store = fastStores_.value(stream);
	if (!store) return;
	const double shift = store->newestShift();
	store->mark(record, time, period);
	for (Series &s : series_)
		if (s.fast == store) sumFast(s); /* the first block's records have times from the first mark on */
	/* a new start shifted after the one before: the engine's clock is not, so it is given the arm in its own terms */
	const auto watched = series_.constFind(trigger_.key);
	if (trigger_.on && store->newestShift() != shift && watched != series_.constEnd() && watched->fast == store) postWatch();
}

/* The crossings the engine found (fast::TriggerScan, with the same level, edge and re-arm as here), each at the time
 * this store gives its record: the view holds on them as on a polled line's crossing. One found for an older arm is
 * not used. One for this arm that the view cannot hold on (its record before is not in the store, as after a Clear,
 * or it lies at or before the crossing held on) is not dropped silently: the engine counted it (Single stopped there,
 * Normal waits its re-arm), so the engine is armed again */
void ChartView::fastCrossings(int stream, qint64 first, const QVector<fast::Crossing> &crossings) {
	const auto it = series_.constFind(trigger_.key);
	if (!trigger_.on || it == series_.constEnd() || !it->fast || it->fast != fastStores_.value(stream)) return;
	const fast::Store &store = *it->fast;
	bool unused = false;
	for (const fast::Crossing &crossing : crossings) {
		if (crossing.serial != watchSerial_ || !trigger_.armed) continue;
		const qsizetype i = qsizetype(first - store.dropped()) + crossing.record;
		if (first < 0 || i < 1 || i >= store.size() || !store.hasTime()) {
			unused = true;
			continue;
		}
		const double before = store.timeAt(i - 1), time = before + crossing.fraction * (store.timeAt(i) - before);
		if (std::isfinite(trigger_.at) && time <= trigger_.at) {
			unused = true;
			continue;
		}
		crossed(time, store.timeAt(store.size() - 1));
	}
	if (unused) postWatch();
}

fast::TriggerWatch ChartView::fastTriggerWatch(int &stream) const {
	stream = -1;
	fast::TriggerWatch watch;
	const auto it = series_.constFind(trigger_.key);
	if (!trigger_.on || it == series_.constEnd() || !it->fast) return watch;
	stream = (trigger_.key - FIRST_FAST_KEY) / 256;
	const TriggerSettings watched = watchedSettings();
	watch.on = trigger_.armed;
	watch.channel = it->channel;
	watch.level = watched.level;
	watch.edge = int(watched.edge);
	watch.from = trigger_.armedFrom - it->fast->newestShift(); /* on the stream's clock (TriggerWatch::from) */
	watch.rearm = trigger_.mode == TriggerMode::Single ? -1 : std::max(holdoffSeconds(), (1 - triggerPosition_) * window_);
	watch.serial = watchSerial_;
	return watch;
}

void ChartView::postWatch() {
	watchDue_ = false;
	watchSerial_++;
	emit fastTriggerChanged();
}

namespace {
/* the trapezoids of a fast line's records i0 .. i1 - 1, each segment apart (nothing across a gap): a segment's
 * records are evenly spaced, so its trapezoids are dt x (sum - (first + last) / 2), the sums from the summaries.
 * shifted: the same of (value - shift), for the standard deviation (statsOf's reason) */
struct Trapezoids {
	double area = 0, areaOfSquares = 0, shifted = 0, shiftedSquares = 0, span = 0;
};
Trapezoids trapezoids(const fast::Store &store, int channel, qsizetype i0, qsizetype i1, double shift) {
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
} // namespace

/* A fast line's total since Clear: the trapezoids from its last summed record to its newest with a time (the
 * memory's trims lose nothing: summed as they come), none across a gap */
void ChartView::sumFast(Series &s) {
	const fast::Store &store = *s.fast;
	if (!store.hasTime() || store.size() == 0) return;
	const qint64 end = store.dropped() + store.size();
	if (s.totalTo >= end) return;
	/* joined to the last; never from summaries only (a store given whole, as a recording's, has none) */
	const qsizetype from = qsizetype(std::max<qint64>(store.recordsFrom(), s.totalTo - 1 - store.dropped()));
	if (end - store.dropped() - from >= 2) s.total += trapezoids(store, s.channel, from, end - store.dropped(), 0).area;
	s.totalT = store.timeAt(store.size() - 1);
	s.totalV = store.value(s.channel, store.size() - 1);
	if (std::isnan(totalsSince_)) totalsSince_ = store.timeAt(from);
	s.totalTo = end;
}

/* What is older than `memory` goes, about a twentieth at a time (as a polled line's), and from a sixteenth short of
 * its lines' share of the RAM down to seven eighths of it; the store drops whole pieces. With older samples as
 * summaries (fastSummaries_), the share holds the summaries of all that is kept and the newest records whole: past it,
 * the oldest pieces' records go a piece at a time (a ring, not an eighth at once), their summaries kept; only when the
 * summaries themselves outgrow the share does the oldest go, as without. What they held is not freed
 * here but a slice at each frame (releaseSome): the RAM cut from 4 GB to 512 MB with a fast line filled let 3.5 GB
 * go, and freeing it in one go held the window's thread about 2 s. Freed on another thread instead, the frees held
 * the heap and the memory's pages while the chart's threads binned, and a paint took 30 to 50 ms. The store itself
 * is at its new size at once, the same for every frame after: only the freeing waits, of memory nothing reads */
void ChartView::trimFast(fast::Store &store, int lines) {
	if (store.size() == 0 || !store.hasTime()) return;
	fast::Store::Released gone;
	const double newest = store.timeAt(store.size() - 1);
	if (newest - store.timeAt(0) > memory_ * 1.05 + 0.5) store.dropFront(store.lowerBound(newest - memory_), &gone);
	const double share = double(ramInUse()) * 1024 * 1024 / double(std::max<qsizetype>(1, series_.size())) * lines;
	if (fastSummaries_ && !store.mapped()) {
		const double summary = store.bytesPerSummary();
		const double piece = double(fast::Store::PIECE) * (store.bytesPerRecord() - summary); /* a piece's records */
		/* two pieces of records kept whole at least: the newest, and the one filling */
		if (double(store.size()) * summary + 2 * piece >= share - share / 16) {
			const qsizetype most = qsizetype(std::max(0.0, (share * 7 / 8 - 2 * piece) / summary));
			store.dropFront(store.size() - most, &gone);
			capped_ = true;
		}
		/* by what it holds (its summaries' arrays keep the room freed at their front until refitted): the pieces of
		 * records over the share go */
		for (double over = double(store.bytes()) - share; over > 0; over = double(store.bytes()) - share) {
			const qsizetype before = store.recordsFrom();
			store.dropRecords(qsizetype(std::ceil(over / piece)) * fast::Store::PIECE, &gone);
			if (store.recordsFrom() == before) break; /* the newest piece alone: kept */
		}
	} else {
		const qsizetype most = qsizetype(share / store.bytesPerRecord());
		if (store.size() >= most - most / 16) {
			store.dropFront(store.size() - most + most / 8, &gone);
			capped_ = true;
		}
	}
	released_.pieces += std::move(gone.pieces);
	released_.summaries += std::move(gone.summaries);
}

/* what the trims let go, freed for at most RELEASE_NS a frame (a piece of 256 KB, a summary of up to tens of MB at a
 * time), before the frame is painted: gigabytes go over a second or so, never a frame over budget */
void ChartView::releaseSome() {
	if (released_.isEmpty()) return;
	QElapsedTimer clock;
	clock.start();
	while (!released_.summaries.isEmpty() && clock.nsecsElapsed() < RELEASE_NS) released_.summaries.removeLast();
	while (!released_.pieces.isEmpty() && clock.nsecsElapsed() < RELEASE_NS) released_.pieces.removeLast();
	/* emptied: their own arrays let go too (a list of thousands of pieces) */
	if (released_.pieces.isEmpty()) released_.pieces = {};
	if (released_.summaries.isEmpty()) released_.summaries = {};
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
	releaseSome();
	if (watchDue_) postWatch(); /* a drag's change to the engine, once a frame */
	updateShortLock();
	if (trigger_.on) firePending(newestTime(trigger_.key)); /* a crossing's view full now: shown */
	/* Auto: held by a crossing, and no other for as long as "triggered" lasts (triggeredSpan), by the samples' time as
	 * the arm (a fast line's records come a block late: by the clock, a short window ran live at every other frame):
	 * the view runs live, as the state says "free running" from then on. A view the user held or moved stays */
	if (trigger_.on && trigger_.mode == TriggerMode::Auto && trigger_.armed && !live_ && trigger_.holding
			&& triggerTime() >= std::max(trigger_.armedFrom + window_, lastCrossing() + triggeredSpan()))
		setLive(true);
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
	/* the next frame paints it; a recording's window has frames only while it is fed (and the frames may stop), so
	 * when none came by FRAMES_STOPPED_MS it is painted then: a change just after a feed (the samples a measurement
	 * held back, the card opened) stayed off the screen until something else painted */
	if (framesCome_.isValid() && framesCome_.elapsed() < FRAMES_STOPPED_MS) {
		if (!framesStopped_->isActive()) framesStopped_->start(int(FRAMES_STOPPED_MS - framesCome_.elapsed()) + 1);
		return;
	}
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
	const double window = std::clamp(seconds, MIN_WINDOW, MAX_SPAN);
	if (window > memory_) setMemory(window); /* the view must fit in the memory */
	putWindow(window);
	refresh();
}

/* The trigger's next crossing counts after the view's fill and the hold-off (the window's length by default), from the
 * last crossing: both follow the new length, here and in the engine. The wheel, a span shown and a smaller memory
 * change it too */
void ChartView::putWindow(double seconds) {
	if (seconds == window_) return;
	window_ = seconds;
	if (!trigger_.on) return;
	const double last = std::isfinite(trigger_.pending) ? trigger_.pending : trigger_.at;
	if (trigger_.armed && std::isfinite(last) && trigger_.mode != TriggerMode::Single)
		trigger_.armedFrom = last + std::max(holdoffSeconds(), (1 - triggerPosition_) * window_);
	postWatch();
}

void ChartView::setMemory(double seconds) {
	const double clamped = std::clamp(seconds, MIN_MEMORY, MAX_SPAN);
	if (clamped == memory_) return;
	memory_ = clamped;
	if (window_ > memory_) putWindow(memory_);
	emit memoryChanged(memory_);
	refresh();
}

void ChartView::setLive(bool on) {
	/* Hold over a short window's lock: the lock ends and the view stays as it is shown, held, until Live */
	if (!on && trigger_.automatic) {
		endShortLock();
		holdAsShown();
		emit liveChanged(false);
		refresh();
		return;
	}
	if (on == live_) return;
	if (!on) viewEnd_ = lastViewEnd_; /* hold what is shown */
	/* Live: a crossing waiting for its view must not pull the view back to it; held: by the user, not the trigger */
	if (on) trigger_.pending = NAN;
	trigger_.holding = false;
	live_ = on;
	emit liveChanged(live_);
	refresh();
}

void ChartView::showSpan(double t0, double t1) {
	const double span = std::clamp(t1 - t0, MIN_WINDOW, MAX_SPAN);
	if (span > memory_) setMemory(span);
	putWindow(std::min(span, memory_));
	const bool unlocked = trigger_.automatic; /* the user's view: a short window's lock ends, a trigger that runs stops */
	if (unlocked) endShortLock();
	else if (trigger_.on && trigger_.armed) stopRun();
	viewEnd_ = t1;
	trigger_.holding = false;
	if (live_ || unlocked) {
		live_ = false;
		emit liveChanged(false);
	}
	refresh();
}

void ChartView::setTrigger(int key, TriggerMode mode) {
	const QString name = lineName(key);
	if (!triggerSettings_.contains(name)) triggerSettings_.insert(name, triggerSettings(key)); /* its mid-range, kept */
	/* stopped: the row's line, edge or mode change what Run arms, not the picture, which stays until Run (as the edge
	 * symbol on the chart leaves it); only another line's T goes, as it marked a crossing of the line no longer watched */
	if (trigger_.on && trigger_.stopped) {
		if (key != trigger_.key) trigger_.at = NAN;
		trigger_.key = key;
		trigger_.mode = mode;
		postWatch();
		emit triggerRunChanged();
		refresh();
		return;
	}
	trigger_.on = true;
	trigger_.automatic = false; /* the user's trigger takes over from a short window's lock */
	trigger_.key = key;
	trigger_.mode = mode;
	trigger_.at = trigger_.pending = NAN;
	armTrigger();
}

void ChartView::setTrigger(int key, double level, TriggerEdge edge, TriggerMode mode) {
	setTriggerSettings(key, { level, edge });
	setTrigger(key, mode);
}

/* the middle of what the line shows in the view (its range as last binned), else its newest value: a level that
 * lies on the line, where a level of 0 may be far off it */
double ChartView::midRange(int key) const {
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return 0;
	for (const BinnedLine &line : lastBinned_)
		if (line.series == &*it && line.lo <= line.hi) return (line.lo + line.hi) / 2;
	return it->hasLast ? it->last : 0;
}

ChartView::TriggerSettings ChartView::triggerSettings(int key) const {
	const auto kept = triggerSettings_.constFind(lineName(key));
	if (kept != triggerSettings_.constEnd()) return *kept;
	return { midRange(key), TriggerEdge::Rising };
}

void ChartView::setTriggerSettings(int key, const TriggerSettings &settings) {
	triggerSettings_.insert(lineName(key), settings);
	if (trigger_.on && key == trigger_.key) {
		trigger_.pending = NAN; /* found by the settings before */
		postWatch();
	}
	refresh();
}

ChartView::TriggerSettings ChartView::watchedSettings() const {
	if (trigger_.automatic) return lockSettings_; /* a short window's lock: not the line's own, which stay the user's */
	return trigger_.on ? triggerSettings_.value(lineName(trigger_.key)) : TriggerSettings();
}

ChartView::TriggerSettings &ChartView::watchedSettingsRef() {
	return trigger_.automatic ? lockSettings_ : triggerSettings_[lineName(trigger_.key)];
}

QString ChartView::lineName(int key) const {
	const auto it = series_.constFind(key);
	return it != series_.constEnd() ? it->name : QString();
}

QStringList ChartView::triggerSettingsTexts() const {
	QStringList texts;
	for (auto it = triggerSettings_.constBegin(); it != triggerSettings_.constEnd(); ++it)
		if (!it.key().isEmpty())
			texts << QStringLiteral("%1\t%2\t%3").arg(it.key(), QString::number(it->level, 'g', QLocale::FloatingPointShortest))
					.arg(int(it->edge));
	texts.sort(); /* the same settings, the same text */
	return texts;
}

void ChartView::setTriggerSettingsTexts(const QStringList &texts) {
	for (const QString &text : texts) {
		const QStringList parts = text.split(QLatin1Char('\t'));
		bool ok = false;
		const double level = parts.value(1).toDouble(&ok);
		const int edge = parts.value(2).toInt();
		if (parts.size() < 3 || parts[0].isEmpty() || !ok || !std::isfinite(level) || edge < 0 || edge > 2) continue;
		triggerSettings_.insert(parts[0], { level, TriggerEdge(edge) });
	}
	refresh();
}

void ChartView::stopTrigger() {
	trigger_.on = trigger_.armed = trigger_.stopped = trigger_.automatic = false;
	trigger_.at = trigger_.pending = NAN;
	postWatch();
	emit triggerRunChanged();
	refresh();
}

/* From the line's newest sample on: a crossing already in the memory does not count. Normal and Auto keep the last
 * crossing's hold-off (an Arm reset it to now and took a crossing inside it), except after a Stop: a scope's Stop
 * clears its hold-off, so Run counts the first crossing at once. Only Auto rolls while it waits: Normal and Single
 * hold the picture they have (the last capture, or the view as it is), as a scope waiting in Normal shows its last
 * trace, so the chart says it waits instead of rolling as Auto does */
void ChartView::armTrigger(bool keepHoldoff) {
	if (!trigger_.on) return;
	trigger_.armed = true;
	trigger_.stopped = false;
	trigger_.pending = NAN;
	const auto it = series_.constFind(trigger_.key);
	trigger_.armedFrom = it != series_.constEnd() && !it->times.isEmpty() ? it->times.back()
			: it != series_.constEnd() && it->fast && it->fast->size() > 0 && it->fast->hasTime()
				? it->fast->timeAt(it->fast->size() - 1) : -std::numeric_limits<double>::infinity();
	trigger_.since = trigger_.armedFrom;
	if (keepHoldoff && trigger_.mode != TriggerMode::Single && std::isfinite(trigger_.at))
		trigger_.armedFrom = std::max(trigger_.armedFrom,
				trigger_.at + std::max(holdoffSeconds(), (1 - triggerPosition_) * window_));
	if (trigger_.mode == TriggerMode::Auto) {
		if (!live_) setLive(true);
	} else if (live_) {
		holdAsShown();
	}
	postWatch();
	emit triggerRunChanged();
	refresh();
}

/* the view held as it is shown (the smoothed end the last frame drew), not by a crossing */
void ChartView::holdAsShown() {
	viewEnd_ = isVisible() && lastViewEnd_ > 0 ? lastViewEnd_ : liveEnd();
	trigger_.holding = false;
	if (live_) {
		live_ = false;
		emit liveChanged(false);
	}
}

/* Stop: no crossing counts any more, the one waiting for its view is dropped (it would move the view later), and the
 * picture stays with its T, live or held, until Run */
void ChartView::stopRun() {
	if (!trigger_.on) return;
	trigger_.armed = false;
	trigger_.stopped = true;
	trigger_.pending = NAN;
	if (live_) holdAsShown();
	postWatch();
	emit triggerRunChanged();
	refresh();
}

void ChartView::runTrigger() { armTrigger(false); } /* from now: the hold-off went with the Stop */

/* at the trigger's now (the watched line's newest sample), as fireTrigger holds on a crossing: Normal counts the next
 * after it as after a crossing, Single is complete. A fast line's engine is armed again from there */
void ChartView::forceTrigger() {
	if (triggerPhase() != TriggerPhase::Waiting || trigger_.mode == TriggerMode::Auto) return;
	trigger_.pending = NAN;
	fireTrigger(triggerTime());
	postWatch();
}

ChartView::TriggerPhase ChartView::triggerPhase() const {
	if (!trigger_.on) return TriggerPhase::Off;
	if (trigger_.stopped) return TriggerPhase::Stopped;
	if (!trigger_.armed) return TriggerPhase::Done;
	/* its line's stream stopped: no crossing comes, and one on the old records is not "triggered" */
	if (!triggerStoppedName().isEmpty()) return TriggerPhase::Waiting;
	if (trigger_.mode == TriggerMode::Auto && live_) return TriggerPhase::FreeRunning;
	/* "triggered" until no crossing has come for triggeredSpan: not "waiting" for the moment between a crossing's
	 * hold-off and the next, which flipped the state at every crossing of a short window */
	const double last = lastCrossing();
	if (std::isfinite(last) && last > trigger_.since && triggerTime() < last + triggeredSpan()) return TriggerPhase::Triggered;
	return TriggerPhase::Waiting;
}

double ChartView::lastCrossing() const {
	if (std::isnan(trigger_.pending)) return trigger_.at;
	return std::isnan(trigger_.at) ? trigger_.pending : std::max(trigger_.at, trigger_.pending);
}

/* the next crossing counts after the hold-off and the view's fill; one window more without any and it has stopped
 * coming. At a short window that is a few ms, less than a polled line's gaps: a second at least */
double ChartView::triggeredSpan() const {
	return std::max(TRIGGERED_AT_LEAST, std::max(holdoffSeconds(), (1 - triggerPosition_) * window_) + window_);
}

/* the user's trigger only: a short window's lock shows no fill (nothing of the trigger is drawn for it) */
bool ChartView::triggerCapturing() const {
	return triggerMarked() && trigger_.holding && !live_ && std::isfinite(trigger_.at) && triggerTime() < viewEnd_;
}

void ChartView::setShortLock(bool on) {
	shortLockOn_ = on;
	updateShortLock();
	refresh();
}

qsizetype ChartView::samplesInWindow(const Series &s) const {
	if (s.fast) {
		const fast::Store &store = *s.fast;
		if (store.size() == 0 || !store.hasTime()) return 0;
		return store.size() - store.lowerBound(store.timeAt(store.size() - 1) - window_);
	}
	if (s.times.isEmpty()) return 0;
	return s.times.end() - std::lower_bound(s.times.begin(), s.times.end(), s.times.back() - window_);
}

/* the first fast line with SHORT_LOCK_SAMPLES in the window (the keys put fast lines after the others), else the line
 * with the most there; -1: none has that many */
int ChartView::busiestLine() const {
	int busiest = -1;
	qsizetype most = SHORT_LOCK_SAMPLES - 1;
	for (auto it = series_.constBegin(); it != series_.constEnd(); ++it) {
		if (lineResting(it.key())) continue; /* its records stopped: crossings of old ones would hold the view */
		const qsizetype n = samplesInWindow(*it);
		if (it->fast && n >= SHORT_LOCK_SAMPLES) return it.key();
		if (n > most) {
			most = n;
			busiest = it.key();
		}
	}
	return busiest;
}

/* The lock wanted: on, a live view (or one the lock holds), shorter than SHORT_LOCK_WINDOW, the user's trigger off and a
 * line to watch: the busiest (busiestLine), chosen again only when the lines change or the one watched has too few
 * samples in the window, so the lock does not move from line to line at each frame. A line at rest (lineResting: its
 * stream stopped) is not watched: the lock ends, the view runs live, and it locks again once records come. It is Auto's own work on a line
 * (the crossings as samples come), nothing binned of its own. Its level is taken again once a second while it runs
 * free, as a line drifting away from it would never lock again */
void ChartView::updateShortLock() {
	if (trigger_.on && !trigger_.automatic) return; /* the user's trigger */
	bool wanted = shortLockOn_ && !recording_ && (live_ || trigger_.automatic) && window_ < SHORT_LOCK_WINDOW
			&& !series_.isEmpty();
	if (wanted) {
		const auto watched = series_.constFind(lockKey_);
		if (lockGeneration_ != seriesGeneration_ || watched == series_.constEnd()
				|| samplesInWindow(*watched) < SHORT_LOCK_SAMPLES || lineResting(lockKey_)) {
			lockKey_ = busiestLine();
			lockGeneration_ = seriesGeneration_;
		}
		wanted = lockKey_ >= 0;
	}
	if (!wanted) {
		if (!trigger_.automatic) return;
		endShortLock();
		if (!live_) setLive(true); /* a longer window, the setting off, no line busy enough: live, as before the lock */
		return;
	}
	const int first = lockKey_;
	if (trigger_.automatic && trigger_.key == first) {
		if (live_ && triggerTime() >= lockTakenAt_ + TRIGGERED_AT_LEAST) {
			lockTakenAt_ = triggerTime();
			const double level = midRange(first);
			if (level != lockSettings_.level) {
				lockSettings_.level = level;
				postWatch();
			}
		}
		return;
	}
	trigger_ = Trigger(); /* on, or on another line (the one before removed, a busier one added) */
	trigger_.on = trigger_.automatic = true;
	trigger_.key = first;
	trigger_.mode = TriggerMode::Auto;
	lockSettings_ = { midRange(first), TriggerEdge::Rising };
	lockTakenAt_ = triggerTime();
	armTrigger(false);
}

void ChartView::endShortLock() {
	if (!trigger_.automatic) return;
	trigger_ = Trigger();
	postWatch();
	emit triggerRunChanged();
}

void ChartView::setTriggerLevel(double level) {
	if (!trigger_.on) return;
	watchedSettingsRef().level = level;
	trigger_.pending = NAN;
	postWatch();
	refresh();
}

/* A short window held on a full view shows the next crossing's view once it is full too: a repeating wave stands still,
 * as on a scope, where each crossing showed its view half drawn, filling, until the next replaced it (at 60 frames a
 * second and a 10 ms window, a new half-drawn picture at nearly every frame). The next crossing still counts from this
 * one (armedFrom: the engine's rule). The first crossing, Single, and a window of a second or more hold at once, the
 * view filling as the samples come. */
void ChartView::crossed(double time, double newest) {
	firePending(newest);
	const double fill = (1 - triggerPosition_) * window_;
	const bool steady = trigger_.mode != TriggerMode::Single && !live_ && std::isfinite(trigger_.at) && window_ < STEADY_WINDOW;
	if (steady && !(newest >= time + fill)) {
		if (std::isnan(trigger_.pending)) trigger_.pending = time;
		trigger_.armedFrom = std::max(trigger_.armedFrom, time + std::max(holdoffSeconds(), fill));
		return;
	}
	fireTrigger(time);
}

void ChartView::firePending(double newest) {
	if (std::isnan(trigger_.pending) || !(newest >= trigger_.pending + (1 - triggerPosition_) * window_)) return;
	const double time = trigger_.pending;
	trigger_.pending = NAN;
	fireTrigger(time);
}

double ChartView::newestTime(int key) const {
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return NAN;
	if (it->fast) return it->fast->size() > 0 && it->fast->hasTime() ? it->fast->timeAt(it->fast->size() - 1) : NAN;
	return it->times.isEmpty() ? NAN : it->times.back();
}

bool ChartView::triggerArmed() const { return trigger_.on && trigger_.armed && triggerTime() >= trigger_.armedFrom; }

/* the trigger's now: the watched line's newest sample's time, as its arm and hold-off are (the samples come a poll or
 * a block after their time); the clock's before the first */
double ChartView::triggerTime() const {
	const double newest = newestTime(trigger_.key);
	return std::isnan(newest) ? clockNow() : newest;
}

double ChartView::holdoffSeconds() const { return triggerHoldoff_ < 0 ? window_ : triggerHoldoff_; }

void ChartView::setTriggerHoldoff(double seconds) {
	triggerHoldoff_ = seconds < 0 ? -1 : std::min(seconds, MAX_HOLDOFF);
	trigger_.pending = NAN;
	if (trigger_.on && trigger_.armed && std::isfinite(trigger_.at) && trigger_.mode != TriggerMode::Single)
		trigger_.armedFrom = trigger_.at + std::max(holdoffSeconds(), (1 - triggerPosition_) * window_);
	if (trigger_.on) postWatch();
	refresh();
}

void ChartView::setTriggerPosition(double fraction) {
	if (placeTrigger(fraction) && trigger_.on) postWatch();
	refresh();
}

bool ChartView::placeTrigger(double fraction) {
	const double place = std::clamp(fraction, 0.0, TRIGGER_AT_MAX);
	if (place == triggerPosition_) return false;
	triggerPosition_ = place;
	trigger_.pending = NAN;
	if (trigger_.on && std::isfinite(trigger_.at)) {
		if (!live_) viewEnd_ = trigger_.at + (1 - triggerPosition_) * window_; /* held on it: the crossing moves along */
		if (trigger_.armed && trigger_.mode != TriggerMode::Single)
			trigger_.armedFrom = trigger_.at + std::max(holdoffSeconds(), (1 - triggerPosition_) * window_);
	}
	return true;
}

/* One state in the words of the row: no time held, no countdown, nothing that changes at each crossing or frame */
QString ChartView::triggerStateText() const {
	/* a short window's lock: an aid, not the user's trigger, so neither its mode's words nor a state to act on */
	if (trigger_.automatic)
		return !trigger_.on ? QString() : triggerPhase() == TriggerPhase::Triggered ? tr("Auto (short window)")
				: tr("Auto · free running");
	const QString mode = trigger_.mode == TriggerMode::Auto ? tr("Auto") : trigger_.mode == TriggerMode::Normal
			? tr("Normal") : tr("Single");
	/* Normal and Auto keep re-arming: "capturing after T" came and went at every crossing of a short window, so the
	 * text danced; the now edge shows the fill. Single captures once: its "capturing after T" stays until it is full */
	const bool capturing = triggerCapturing();
	switch (triggerPhase()) {
	case TriggerPhase::Off: return QString();
	case TriggerPhase::Stopped: return tr("Stopped · Run to arm");
	case TriggerPhase::FreeRunning: return tr("Auto · free running");
	case TriggerPhase::Waiting:
		if (const QString stopped = triggerStoppedName(); !stopped.isEmpty())
			return tr("%1 · waiting (%2 stopped)", "the trigger's mode; the fast stream of its line").arg(mode, stopped);
		return tr("%1 · waiting", "the trigger's mode, waiting for a crossing").arg(mode);
	case TriggerPhase::Triggered: return tr("%1 · triggered", "the trigger's mode").arg(mode);
	case TriggerPhase::Done:
		return capturing ? tr("Single · complete, capturing after T") : live_ ? tr("Single · complete · Arm to wait")
				: tr("Single · complete");
	}
	return QString();
}

void ChartView::setTriggerEdge(TriggerEdge edge) {
	if (!trigger_.on) return;
	watchedSettingsRef().edge = edge;
	trigger_.pending = NAN;
	postWatch();
	refresh();
}

/* the view holds with the crossing at its place in the window: what comes after fills its right part as it arrives.
 * Single waits for Arm; Normal and Auto count the next crossing once the hold-off has passed and the view is full */
void ChartView::fireTrigger(double time) {
	trigger_.at = time;
	trigger_.atLevel = watchedSettings().level;
	trigger_.atEdge = watchedSettings().edge;
	triggerHolds_++;
	const double fill = (1 - triggerPosition_) * window_;
	if (trigger_.mode == TriggerMode::Single) trigger_.armed = false;
	else trigger_.armedFrom = std::max(trigger_.armedFrom, time + std::max(holdoffSeconds(), fill));
	viewEnd_ = time + fill;
	if (live_) {
		live_ = false;
		emit liveChanged(false);
	}
	trigger_.holding = true;
	emit triggered(time);
	if (trigger_.mode == TriggerMode::Single) emit triggerRunChanged(); /* stopped by its crossing: Run */
	refresh();
}

bool ChartView::lineSamples(int key, double t0, double t1, QVector<double> &times, QVector<double> &values,
		bool withoutGap) const {
	times.clear();
	values.clear();
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return true;
	if (it->fast) return fastSamples(*it, t0, t1, MAX_POINTS, withoutGap, times, values);
	const qsizetype i0 = std::lower_bound(it->times.begin(), it->times.end(), t0) - it->times.begin();
	const qsizetype i1 = std::upper_bound(it->times.begin(), it->times.end(), t1) - it->times.begin();
	if (i1 <= i0) return true;
	times = it->times.mid(i0, i1 - i0);
	values = it->values.mid(i0, i1 - i0);
	return true;
}

bool ChartView::fastSamples(const Series &s, double t0, double t1, qsizetype most, bool withoutGap, QVector<double> &times,
		QVector<double> &values) {
	const fast::Store &store = *s.fast;
	if (store.size() == 0 || !store.hasTime()) return true;
	qsizetype i0 = store.lowerBound(t0), i1 = store.upperBound(t1);
	const qsizetype all = i1 - i0;
	i0 = std::clamp(i0, store.recordsFrom(), std::max(i1, store.recordsFrom())); /* kept as summaries: no values */
	if (withoutGap) { /* the longest segment's part in the range */
		qsizetype best0 = i0, best1 = i0;
		for (qsizetype k = i0; k < i1;) {
			const qsizetype e = std::min(store.segmentEnd(k), i1);
			if (e - k > best1 - best0) {
				best0 = k;
				best1 = e;
			}
			k = e;
		}
		i0 = best0;
		i1 = best1;
	}
	i1 = std::min(i1, i0 + most);
	const bool whole = i1 - i0 == all;
	if (i1 <= i0) return whole;
	times.resize(i1 - i0);
	values.resize(i1 - i0);
	for (qsizetype i = i0; i < i1; i++) {
		times[i - i0] = store.timeAt(i);
		values[i - i0] = store.value(s.channel, i);
	}
	return whole;
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

QRectF ChartView::chipButtonRect(int key) const {
	const qsizetype i = series_.keys().indexOf(key);
	const QVector<QRectF> chips = legendChips();
	if (i < 0 || i >= chips.size()) return QRectF();
	const QRectF &chip = chips[i];
	return QRectF(chip.right() - CHIP_BUTTON_W - 3, chip.center().y() - 8, CHIP_BUTTON_W, 16);
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
		recording::Line line;
		line.name = s.name;
		line.unit = s.unit;
		if (s.fast) { /* its records, each a row (two channels of a stream share their rows) */
			fastSamples(s, t0, t1, MAX_POINTS, false, line.times, line.values);
			out << line;
			continue;
		}
		const qsizetype i0 = std::lower_bound(s.times.begin(), s.times.end(), t0) - s.times.begin();
		const qsizetype i1 = std::upper_bound(s.times.begin(), s.times.end(), t1) - s.times.begin();
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

void ChartView::holdAt(double end, bool user) {
	double m0, m1;
	memorySpan(m0, m1);
	const double liveEdge = liveEnd();
	/* asked before the clamp: a view still capturing after T ends past now, and its clamp to now is not the user's */
	const bool moved = end != viewEnd();
	if (user && !moved && !live_) return; /* a press that does not move the view changes nothing */
	end = std::clamp(end, std::min(m0 + window_, liveEdge), liveEdge);
	/* the user moved the view: a trigger that runs would move it back at its next crossing, so it stops (a scope's
	 * Stop); a press that does not move it stops nothing */
	const bool unlocked = user && trigger_.automatic && moved; /* a short window's lock ends (as Hold) */
	if (unlocked) endShortLock();
	else if (user && trigger_.on && trigger_.armed && moved) stopRun();
	if (end >= liveEdge - window_ * LIVE_SNAP) { /* back at now: live again */
		setLive(true);
		return;
	}
	viewEnd_ = end;
	trigger_.holding = false; /* the user's: Auto leaves it */
	if (live_ || unlocked) {
		live_ = false;
		emit liveChanged(false);
	}
	refresh();
}

void ChartView::memorySpan(double &m0, double &m1) const {
	m1 = liveEnd();
	m0 = m1;
	for (const Series &s : series_) {
		if (!s.times.isEmpty()) m0 = std::min(m0, s.times.front());
		if (s.fast && s.fast->size() > 0 && s.fast->hasTime()) m0 = std::min(m0, s.fast->timeAt(0));
	}
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
	cursorPlace_[0] = cursorPlace_[1] = NAN;
	emit cursorsChanged();
	refresh();
}

void ChartView::setCursors(double a, double b) {
	cursorA_ = a;
	cursorB_ = b;
	/* their places in the picture shown: on the grid they stay there (a live view's next frame moves on under them) */
	const double start = shownStart();
	cursorPlace_[0] = (a - start) / window_;
	cursorPlace_[1] = (b - start) / window_;
	emit cursorsChanged();
	refresh();
}

double ChartView::shownStart() const { return (live_ && lastViewEnd_ != 0 ? lastViewEnd_ : viewEnd()) - window_; }

/* Each frame, before anything reads them: on the grid a cursor's time is the one at its place in this frame's view;
 * the frame after the grid gives way to clock times converts the same way once, and from then on the time is kept and
 * its place follows it (where a switch back to the grid finds it) */
void ChartView::placeCursors(const Axes &axes) {
	const bool grid = cursorsOnGrid();
	double *times[2] = { &cursorA_, &cursorB_ };
	for (int k = 0; k < 2; k++) {
		if (grid || cursorsGridPainted_) /* a place of 1 is the right edge exactly: its tag is drawn there */
			*times[k] = !std::isfinite(cursorPlace_[k]) ? NAN
					: cursorPlace_[k] == 1 ? axes.t1 : axes.t0 + cursorPlace_[k] * axes.span;
		else
			cursorPlace_[k] = (*times[k] - axes.t0) / axes.span;
	}
	cursorsGridPainted_ = grid;
}

QRectF ChartView::plotRect() const {
	const bool marked = triggerMarked(); /* the trigger's marks: room of their own, beside the data */
	return QRectF(rect()).adjusted(AXIS_W + (marked ? TRIGGER_LEFT_W : 0), LEGEND_H + (marked ? TRIGGER_STRIP_H : 0), -(marked ? TRIGGER_PAD : RIGHT_PAD),
			-(TIME_AXIS_H + OVERVIEW_H + BOTTOM_PAD));
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
	for (auto it = series_.constBegin(); it != series_.constEnd(); ++it) {
		const Series &s = *it;
		if (!s.times.isEmpty()) newest = std::max(newest, s.times.back());
		/* a stream stopped (or silent): the view follows the clock from where it was, at its pace, and the lines move
		 * out to the left, as polled lines after Disconnect; following its newest record (the blocks' delay hidden)
		 * held a stopped stream's last records in view as if they were live */
		if (s.fast && s.fast->size() > 0 && s.fast->hasTime() && !lineResting(it.key()))
			newest = std::max(newest, s.fast->timeAt(s.fast->size() - 1));
	}
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
	if (it->fast) result = statsOfFast(*it->fast, it->channel, t0, t1, cursorA_, cursorB_);
	else if (!it->times.isEmpty()) result = statsOf(it->times, it->values, t0, t1, cursorA_, cursorB_);
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

double ChartView::fastValueAt(const fast::Store &store, int channel, double t) {
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
ChartView::Stats ChartView::statsOfFast(const fast::Store &store, int channel, double t0, double t1, double a, double b) {
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
		if (it == series_.end()) return;
		if (it->fast) {
			all[i].total = std::isnan(it->totalT) ? NAN : it->total;
			all[i].atA = fastValueAt(*it->fast, it->channel, cursorA_);
			all[i].atB = fastValueAt(*it->fast, it->channel, cursorB_);
			return;
		}
		if (it->times.isEmpty()) return;
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
		if (s.fast) { /* its records in the range, counted since its store began; with the cursors one either side */
			const fast::Store &store = *s.fast;
			qsizetype i0 = store.lowerBound(t0), i1 = store.upperBound(t1);
			if (cursors) {
				i0 = std::max<qsizetype>(0, i0 - 1);
				i1 = std::min<qsizetype>(store.size(), i1 + 1);
			}
			key << double(store.dropped() + i0) << double(store.dropped() + i1);
			if (i0 < store.recordsFrom()) key << double(store.dropped() + store.recordsFrom()); /* records go to summaries */
			continue;
		}
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
		job->totals[i] = std::isnan(it->totalT) ? NAN : it->total;
		if (it->fast) { /* from its summaries, here: cheap, and its store is written by this thread between frames */
			job->out[i] = statsOfFast(*it->fast, it->channel, job->t0, job->t1, job->a, job->b);
			continue;
		}
		job->times[i] = it->times;
		job->values[i] = it->values;
	}
	job->request = std::move(request);
	job->clock.start();
	if (n == 0) { /* no arrays shared with the threads: the samples meanwhile go in (a recording's, as it opens) */
		QMetaObject::invokeMethod(this, [job] { job->request.done(job->out, 0); }, Qt::QueuedConnection);
		return;
	}
	measuring_ = true;
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
				/* a recording's window: no frames show the legend's values, they follow here (its feed showed them
				 * without these) */
				if (!held.isEmpty() && recording_) showLastValues();
				else if (!held.isEmpty()) refresh();
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
		hoverTag_ = -1;
		hoverBar_ = false;
		hoverSeparator_ = -1;
		hoverEdge_ = false;
		hoverMark_ = false;
		hoverMemoryHandle_ = false;
		hoverLevel_ = false;
		hoverChip_ = -1;
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
	/* a line's chip: its menu, under the chip (the right-click's menu, reached without a right-click) */
	const int chip = chipAt(pos);
	if (chip >= 0) {
		const QRectF rect = legendChips().value(series_.keys().indexOf(chip));
		emit lineMenuRequested(chip, mapToGlobal(QPoint(int(rect.left()), int(rect.bottom()) + 2)));
		return;
	}
	/* the trigger's level tag: its edge symbol takes the next edge; the rest of it, as the level's line, drags the
	 * level (held where it was taken: the level does not jump to the mouse) */
	if (trigger_.on && triggerEdgeButton_.contains(pos)) {
		TriggerSettings &watched = watchedSettingsRef();
		watched.edge = TriggerEdge((int(watched.edge) + 1) % 3);
		trigger_.pending = NAN;
		postWatch();
		emit triggerSettingsChanged();
		refresh();
		return;
	}
	/* the marker left of the plot, the tab right of it and the line between move the one level */
	if (trigger_.on && std::isfinite(triggerLineY_) && (triggerLevelTag_.contains(pos) || triggerLevelMark_.contains(pos)
			|| (std::fabs(pos.y() - triggerLineY_) <= 4 && pos.x() >= triggerLane_.left() && pos.x() <= triggerLane_.right()))) {
		drag_ = Drag::Level;
		levelGrab_ = pos.y() - triggerLineY_;
		/* an off-scale level is drawn on the lane's edge, whose value is not the level: it keeps its value until the
		 * mouse has moved a drag's distance up or down (a press to read its tag, a sideways jitter, changes nothing) */
		dragStartY_ = triggerOffScale_ != 0 ? pos.y() : NAN;
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
	/* the trigger's place in the window: its mark dragged along the time labels */
	if (trigger_.on && triggerMark_.contains(pos)) {
		drag_ = Drag::Position;
		positionGrab_ = pos.x() - triggerMark_.center().x(); /* moved from where it was taken: no jump to the mouse */
		return;
	}
	/* on (or just by) the memory strip: the view goes there; taken by its box or handle, it follows the mouse from
	 * where it was, no jump */
	if (overviewRect().adjusted(0, -4, 0, 4).contains(pos)) {
		drag_ = Drag::Overview;
		overviewGrab_ = memoryHandle_.adjusted(-2, -4, 2, 4).contains(pos) ? pos.x() - memoryViewX_ : 0;
		mouseMoveEvent(e);
		return;
	}
	if (pressLaneLabels(pos)) return; /* not the lanes' own press: a double-click on the labels sets Auto */
	if (pressLanes(pos)) { /* the lanes' scroll bar, a unit name, a folded strip: no cursor, no pan */
		pressedLanes_ = true;
		return;
	}
	if (!plotRect().contains(pos)) return;
	if (cursorMode_) {
		pickCursor(pos.x(), e->modifiers() & Qt::ShiftModifier);
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
	if (laneMenuButtonAt(pos) == lane) { /* its menu button: the lane's menu, under the button; the lane current */
		setCurrentLane(lane);
		const QRectF menu = laneMenuButtonRect(lane);
		emit laneMenuRequested(lane, mapToGlobal(QPoint(int(menu.left()), int(menu.bottom()) + 1)));
		return true;
	}
	setLaneFolded(lane, !folded);
	return true;
}

/* Lanes: a press on an open lane's value labels makes it the current lane (the toolbar's Y range shows and sets it);
 * one on its range tag sets it back to Auto too */
bool ChartView::pressLaneLabels(const QPointF &pos) {
	if (!lanes_) return false;
	const QRectF plot = plotRect();
	if (pos.x() < LANE_UNIT_W || pos.x() >= plot.left() || pos.y() < plot.top() || pos.y() > plot.bottom()) return false;
	const int lane = laneAtY(pos.y());
	if (lane < 0 || lanesShown_[lane].folded) return false;
	const QRectF shown = laneVisible(lanesShown_[lane].axes.rect, plot);
	if (pos.y() < shown.top() || pos.y() > shown.bottom()) return false; /* between two lanes */
	setCurrentLane(lane);
	if (rangeTagAt(pos) == lane) {
		laneScales_[lanesShown_[lane].key].log = false; /* the tag gone: Auto on the linear scale */
		hoverTag_ = -1;
		setLaneYAuto(lane);
	}
	return true;
}

/* cursor mode: a click places A, then B, then moves the nearer of the two */
void ChartView::pickCursor(double x, bool snap) {
	/* on the grid the nearer by where it is drawn: a live view's times moved on since */
	const QRectF plot = plotRect();
	const auto xOf = [&](int k) {
		return cursorsOnGrid() ? plot.left() + cursorPlace_[k] * plot.width() : xAtTime(k == 0 ? cursorA_ : cursorB_);
	};
	if (!std::isfinite(cursorA_)) drag_ = Drag::CurA;
	else if (!std::isfinite(cursorB_)) drag_ = Drag::CurB;
	else drag_ = std::fabs(xOf(0) - x) <= std::fabs(xOf(1) - x) ? Drag::CurA : Drag::CurB;
	putCursor(drag_ == Drag::CurA ? 0 : 1, x, snap);
	emit cursorsChanged();
	refresh();
}

/* On the grid a place in the picture shown (its time from it), Shift's snap counted from the grid's 0 (the right edge,
 * or T) as the division lines are, so a snapped cursor sits on a line or a tenth between two; on clock times a time,
 * as before, its place following at the next frame */
void ChartView::putCursor(int k, double x, bool snap) {
	double &time = k == 0 ? cursorA_ : cursorB_;
	if (!cursorsOnGrid()) {
		time = timeAtX(x);
		return;
	}
	const QRectF plot = plotRect();
	const double start = shownStart();
	double place = (std::clamp(x, plot.left(), plot.right()) - plot.left()) / plot.width();
	if (snap) {
		constexpr double STEP = 1.0 / (DIVISIONS * 10);
		const double zero = timesFromT(start, start + window_) ? (trigger_.at - start) / window_ : 1.0;
		place = zero + std::round((place - zero) / STEP) * STEP;
		if (place > 1 + 1e-9) place -= STEP; /* the step past an edge: the last one inside */
		if (place < -1e-9) place += STEP;
		place = std::clamp(place, 0.0, 1.0);
	}
	cursorPlace_[k] = place;
	time = place == 1 ? start + window_ : start + place * window_;
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
		const double t = m0 + std::clamp((pos.x() - overviewGrab_ - strip.left()) / strip.width(), 0.0, 1.0) * (m1 - m0);
		holdAt(t + window_ / 2); /* the view centred where the mouse is */
		break;
	}
	case Drag::CurA:
	case Drag::CurB:
		putCursor(drag_ == Drag::CurA ? 0 : 1, std::clamp(pos.x(), plot.left(), plot.right()),
				e->modifiers() & Qt::ShiftModifier);
		emit cursorsChanged();
		break;
	case Drag::Level: { /* the engine watches the new level from the next frame on, not after the release */
		if (std::isfinite(dragStartY_)) {
			if (std::fabs(pos.y() - dragStartY_) < QGuiApplication::styleHints()->startDragDistance()) break;
			dragStartY_ = NAN;
		}
		const double at = triggerAxes_.value(std::clamp(pos.y() - levelGrab_, triggerLane_.top(), triggerLane_.bottom()));
		const double level = normalized_ ? triggerLo_ + at * (triggerHi_ - triggerLo_) : at;
		if (level != watchedSettingsRef().level) {
			watchedSettingsRef().level = level;
			trigger_.pending = NAN;
			watchDue_ = true;
		}
		break;
	}
	case Drag::Position:
		if (placeTrigger((pos.x() - positionGrab_ - plot.left()) / plot.width())) watchDue_ = true;
		break;
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
		/* an open lane's value labels take a click (the lane current, its tag: Auto): a hand, the tag lit */
		const bool onLabels = lane >= 0 && !onLanes && !lanesShown_[lane].folded && pos.x() >= LANE_UNIT_W
				&& pos.x() < plot.left() && laneVisible(lanesShown_[lane].axes.rect, plot).contains(QPointF(plot.left(), pos.y()));
		hoverTag_ = onLabels ? rangeTagAt(pos) : -1;
		hoverBar_ = onLaneBar;
		hoverSeparator_ = separatorAt(pos); /* a drag there resizes: lit, and the resize cursor */
		/* the trigger's level tab and marker: a hand, both lit, the tab's edge part lit more */
		const bool onLevelTag = trigger_.on && (triggerLevelTag_.contains(pos) || triggerLevelMark_.contains(pos));
		hoverEdge_ = trigger_.on && triggerEdgeButton_.contains(pos);
		hoverMark_ = trigger_.on && triggerMark_.contains(pos);
		hoverMemoryHandle_ = memoryHandle_.adjusted(-2, -4, 2, 4).contains(pos);
		/* both lit over them and over the level's line (the line drags as they do) */
		const bool onLevelLine = trigger_.on && std::isfinite(triggerLineY_) && std::fabs(pos.y() - triggerLineY_) <= 4
				&& pos.x() >= triggerLane_.left() && pos.x() <= triggerLane_.right();
		hoverLevel_ = onLevelTag || onLevelLine;
		/* a line's chip: a hand, its "▾" lit (a click opens its menu) */
		hoverChip_ = onLegendBar ? -1 : chipAt(pos);
		if (hoverSeparator_ >= 0 && !onLevelTag) {
			setCursor(Qt::SizeVerCursor);
			break;
		}
		setCursor(overviewRect().contains(pos) || onLegendBar || onLanes || onLabels || onLaneBar || onLevelTag || hoverMark_
						|| hoverChip_ >= 0
						? Qt::PointingHandCursor
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
	if (watchDue_) postWatch(); /* the drag's last change, now */
	if (drag_ == Drag::Position) emit triggerPositionChanged(triggerPosition_);
	const bool borderLetGo = drag_ == Drag::LaneBorder;
	drag_ = Drag::None;
	if (borderLetGo) {
		dragSeparator_ = -1;
		emit laneHeightsChanged();
		return; /* the resize cursor stays while the mouse is on the separator */
	}
	if (noteLetGo) emit notesChanged();
	if (levelLetGo) emit triggerSettingsChanged();
	setCursor(cursorMode_ ? Qt::SizeHorCursor : Qt::OpenHandCursor);
	if (cursorLetGo) emit cursorsChanged(); /* measured in full now: while dragged, A and B alone followed it */
}

/* the wheel zooms the time; with Ctrl, the values; over a legend wider than
 * its row, it scrolls the legend */
void ChartView::wheelEvent(QWheelEvent *e) {
	if (wheelLegend(e)) return;
	const double notches = e->angleDelta().y() / 120.0;
	if (notches == 0) return;
	/* over the memory strip: the view a window earlier (up) or later (down), held, as a pan by the whole view */
	const QPointF pos = e->position();
	if (overviewRect().adjusted(0, -4, 0, 4).contains(pos)) {
		holdAt(viewEnd() - notches * window_);
		e->accept();
		return;
	}
	/* lanes: over their value labels (or the scroll bar), the wheel scrolls them; with Ctrl, it stays the lane's zoom */
	const QRectF plot = plotRect();
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
	} else if (divisionsShown() || (timeGrid_ == TimeGrid::Auto && window_ * factor < DIVISIONS_BELOW)) {
		/* divisions: a notch is the next window of 1, 2 or 5 per division, as a scope's time/div knob, so the readout
		 * and the labels stay round (a window typed is kept as typed). Part notches (a touchpad's) add up to one; Auto
		 * leaves the divisions at DIVISIONS_BELOW, and from there the wheel zooms as before */
		if (wheelNotches_ * notches < 0) wheelNotches_ = 0;
		wheelNotches_ += notches;
		const int steps = int(wheelNotches_);
		wheelNotches_ -= steps;
		if (steps != 0) {
			double target = window_;
			for (int i = 0; i < std::abs(steps); i++) {
				const bool stepping = timeGrid_ == TimeGrid::Divisions || target < DIVISIONS_BELOW
						|| (steps > 0 && target * std::pow(ZOOM_STEP, -1) < DIVISIONS_BELOW);
				target = stepping ? divisionWindow(target, steps > 0) : target * ZOOM_STEP;
			}
			zoomTime(target / window_, e->position().x());
			emit windowChangedByUser(window_);
		}
	} else {
		wheelNotches_ = 0;
		zoomTime(factor, e->position().x());
		emit windowChangedByUser(window_);
	}
	refresh();
	e->accept();
}

void ChartView::zoomTime(double factor, double mouseX) {
	const double newWindow = std::clamp(window_ * factor, MIN_WINDOW, memory_);
	if (live_) { /* live: the right edge stays at now */
		putWindow(newWindow);
		return;
	}
	/* held: zoom around the time under the mouse; held by the trigger, still the trigger's (Auto runs on) */
	const QRectF plot = plotRect();
	const double at = timeAtX(std::clamp(mouseX, plot.left(), plot.right()));
	const double fraction = (at - (viewEnd_ - window_)) / window_;
	const bool holding = trigger_.holding;
	putWindow(newWindow);
	if (holding && std::isfinite(trigger_.at)) {
		/* the trigger's view stays its own: a view still capturing after T ends past now, and holdAt's clamp to now
		 * would take it live (Normal rolling while armed, Single's capture gone). It may end as far past now as the
		 * crossing's own view at this window */
		double m0, m1;
		memorySpan(m0, m1);
		const double top = std::max(liveEnd(), trigger_.at + (1 - triggerPosition_) * newWindow);
		viewEnd_ = std::clamp(at + (1 - fraction) * newWindow, std::min(m0 + newWindow, top), top);
		refresh();
		return;
	}
	holdAt(at + (1 - fraction) * newWindow, false);
	trigger_.holding = holding && !live_;
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
	/* the crossing's place back to the default, as a scope's position knob pressed (the row's box is the other way) */
	if (trigger_.on && triggerMark_.contains(e->position())) {
		setTriggerPosition(TRIGGER_AT);
		emit triggerPositionChanged(triggerPosition_);
		return;
	}
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
					+ CHIP_PAD_RIGHT + CHIP_BUTTON_W;
		}
		chipsGeneration_ = seriesGeneration_;
		chipsFont_ = font.key();
		chipMeasures_++;
	}
	/* the chips end where the corner begins (the time/div readout, else the state's text), so none lies under another */
	int variant = -1;
	double stateWidth = 0;
	fitState(plot.width(), variant, stateWidth);
	const double readout = divisionReadoutWidth();
	double corner = variant >= 0 ? stateWidth : 0;
	if (readout > 0) corner += readout + (variant >= 0 ? DIVISION_GAP : 0);
	LegendLayout legend;
	legend.viewport = QRectF(plot.left(), LEGEND_TOP, std::max(0.0, plot.width() - (corner > 0 ? corner + STATE_GAP : 0)),
			LEGEND_ROW_H);
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
	double value = 0;
	return it != series_.constEnd() && chipValue(*it, viewEnd() - window_, viewEnd(), value) ? chartNumber(value)
			: QString();
}

bool ChartView::chipValue(const Series &s, double t0, double t1, double &value) const {
	if (recording_) return latestInView(s, t0, t1, value);
	value = s.shown;
	return s.hasShown;
}

bool ChartView::latestInView(const Series &s, double t0, double t1, double &value) const {
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
	/* a painter on a widget takes the application's direction, not the widget's: in Arabic every label right-aligned
	 * to the plot went to the window's left edge. Auto, as a painter on a picture has it: a text is placed as aligned,
	 * an Arabic one still reads right to left (the state corner sets its own) */
	p.setLayoutDirection(Qt::LayoutDirectionAuto);
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
	placeCursors(axes); /* on the grid: the times under them in this view */
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
	if (!live_) { /* held: the toolbar's Y boxes follow this frame's ranges (yRangesShown), only when they changed */
		QVector<double> ranges{ y_.lo, y_.hi };
		for (const YScale &scale : std::as_const(laneScales_)) ranges << scale.lo << scale.hi;
		if (ranges != rangesShown_) {
			rangesShown_ = ranges;
			QTimer::singleShot(0, this, &ChartView::yRangesShown);
		}
	}

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
	stage.restart();
	drawCard(p);
	drawGrid(p, plots, axes, !onCard);
	perf_.grid += stage.nsecsElapsed() / 1e6;
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
		/* the now edges, the marks' lines over them (the card: the edges' layer before the marks'), the folded strips
		 * over those, then the marks' tags (as the card: its pictures over all) */
		nowEdges_ = nowEdgeLines(plots);
		if (!nowEdges_.isEmpty()) {
			p.save();
			p.setPen(QPen(nowEdgeColor(), 1));
			p.drawLines(nowEdges_);
			p.restore();
		}
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
	stage.restart();
	drawTriggerTab(p); /* in the right pad, outside the card's layer, under the lanes' bar */
	drawLaneBar(p);
	perf_.grid += stage.nsecsElapsed() / 1e6;
	stage.restart();
	drawTriggerMark(p, axes); /* the flag, in the strip above the plot, outside the layer too */
	perf_.marks += stage.nsecsElapsed() / 1e6;
	stage.restart();
	drawMemoryStrip(p, axes);
	perf_.strip += stage.nsecsElapsed() / 1e6;
	stage.restart();
	drawLegend(p, axes);
	perf_.legend += stage.nsecsElapsed() / 1e6;
	stage.restart();
	if (!onCard) drawCrosshair(p, axes, plots, binned);
	perf_.marks += stage.nsecsElapsed() / 1e6;
	stage.restart();
	drawState(p, axes);
	perf_.grid += stage.nsecsElapsed() / 1e6;
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
	PerfStats taken = perf_;
	taken.fastColumns = fastColumnsBinned_.exchange(0);
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
		if (s.fast) { /* a fast line: its records in view, numbered since its store began */
			const fast::Store &store = *s.fast;
			const qsizetype i0 = std::max<qsizetype>(0, store.lowerBound(axes.t0) - 1);
			const qsizetype i1 = std::min(store.size(), store.upperBound(axes.t1) + 1);
			key << double(store.dropped() + i0) << double(store.dropped() + i1);
			if (i0 < store.recordsFrom()) key << double(store.dropped() + store.recordsFrom()); /* records go to summaries */
			continue;
		}
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
	if (lineReuse_ && key == lastBinKey_ && lastBinned_.size() == series_.size()) {
		binPending(axes);
		return lastBinned_;
	}
	/* the view a crossing waited for: its fast lines' bins made while it filled (binPending) are kept, all but the
	 * columns at its ends (binView takes the last bins given of a line) */
	QVector<BinnedLine> previous = lastBinned_;
	if (!pendingBinned_.isEmpty()
			&& pendingKey_ == QVector<double>{ axes.t0, axes.t1, axes.columns, double(seriesGeneration_) })
		previous += pendingBinned_;
	pendingBinned_.clear();
	pendingKey_.clear();
	lastBinned_ = binView(axes, previous);
	lastBinKey_ = key;
	binnings_++;
	binnedVersion_++;
	perf_.binnings++;
	return lastBinned_;
}

/* A short window holds a view only once it is full (crossed): the next crossing's view fills behind the one shown, and
 * the frame that showed it binned all of it at once (a whole view of each fast line, every 100 ms at a 100 ms window:
 * that frame came late). While the view shown stands, the waiting view's fast lines are binned here as their records
 * come, its complete columns kept from frame to frame as a held view filling keeps them (binFast), so the frame that
 * shows it bins only what came since. A polled line keeps one binning of its own (Series::viewBins), the view
 * shown's: binned when shown, as before (a few samples a column). */
void ChartView::binPending(const Axes &axes) {
	if (!trigger_.on || std::isnan(trigger_.pending)) return;
	Axes next = axes;
	next.t1 = trigger_.pending + (1 - triggerPosition_) * window_; /* as fireTrigger holds it */
	next.t0 = next.t1 - window_;
	const QVector<double> key{ next.t0, next.t1, next.columns, double(seriesGeneration_) };
	QVector<const Series *> lines;
	for (const Series &s : series_)
		if (s.fast) lines << &s;
	/* its bins of the frame before, or the view shown's: a column both views share is the same bin (binFast) */
	QVector<BinnedLine> previous = lastBinned_;
	if (key == pendingKey_) previous += pendingBinned_;
	QVector<BinnedLine> binned(lines.size());
	inParallel(lines.size(), [&](qsizetype i) {
		const BinnedLine *before = nullptr;
		for (const BinnedLine &line : std::as_const(previous))
			if (line.series == lines[i]) before = &line;
		binFast(*lines[i], next.t0, next.t1, next.columns, binned[i], before);
	});
	pendingBinned_ = binned;
	pendingKey_ = key;
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
	if (s.fast) { /* the memory strip: binned whole (a strip of 60 px) */
		binFast(s, t0, t1, columns, out, nullptr);
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

void ChartView::rangeAtEdges(BinnedLine &out, double t0, double t1) {
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
void ChartView::binViewSeries(const Series &s, double t0, double t1, double columns, BinnedLine &out,
		const BinnedLine *previous) const {
	if (s.fast) { /* from its summaries: what a column costs does not grow with its records */
		binFast(s, t0, t1, columns, out, previous);
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
	polledColumnsBinned_ += fresh.size();
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
void ChartView::binFast(const Series &s, double t0, double t1, double columns, BinnedLine &out, const BinnedLine *previous)
		const {
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
	fastColumnsBinned_ += binned;
	for (const Bin &bin : out.bins) {
		if (bin.t1 >= t0 && bin.t0 <= t1) { /* the range of what lies inside the span */
			out.lo = std::min(out.lo, bin.min);
			out.hi = std::max(out.hi, bin.max);
			out.posLo = std::min(out.posLo, smallestPositive(bin.min, bin.first, bin.last, bin.max));
		}
	}
	rangeAtEdges(out, t0, t1);
}

/* every line binned for the view, in the order of series_, on the chart's threads */
QVector<ChartView::BinnedLine> ChartView::binView(const Axes &axes, const QVector<BinnedLine> &previous) const {
	QVector<const Series *> lines;
	lines.reserve(series_.size());
	for (const Series &s : series_) lines << &s;
	QVector<BinnedLine> binned(lines.size());
	inParallel(lines.size(), [&](qsizetype i) {
		/* the last frame's bins of the same line, for the columns both frames share */
		const BinnedLine *before = nullptr;
		for (const BinnedLine &line : previous)
			if (line.series == lines[i]) before = &line;
		binViewSeries(*lines[i], axes.t0, axes.t1, axes.columns, binned[i], before);
	});
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
 * over 1, the one below it under), as equal shares do when all are 1. A lane whose share would be under LANE_MIN_H is
 * held there and the others share what is left, again by their weights, so the lanes still fill the plot exactly: a
 * lane held at the minimum took its 80 px on top of the shares, and the last lane ran below the plot, cut and scrolled.
 * Only when even LANE_MIN_H each does not fit are they all LANE_MIN_H, going on below the plot (scrolled). unit: the
 * pixels of a weight of 1 for the lanes not held (the drag of a border turns heights into weights by it) */
void ChartView::laneHeights(const QVector<Lane> &lanes, double plotHeight, QVector<double> &heights, double &unit) const {
	int folded = 0;
	for (const Lane &lane : lanes) folded += lane.folded;
	const int open = int(lanes.size()) - folded;
	const double gaps = LANE_GAP * std::max<qsizetype>(0, lanes.size() - 1);
	const double room = plotHeight - gaps - folded * LANE_FOLDED_H; /* what the open lanes share */
	const bool scrolled = open > 0 && room <= open * LANE_MIN_H;
	QVector<bool> held(lanes.size(), scrolled);
	unit = LANE_MIN_H;
	for (bool more = !scrolled && open > 0; more;) {
		double weights = 0, left = room;
		for (qsizetype k = 0; k < lanes.size(); k++) {
			if (lanes[k].folded) continue;
			if (held[k]) left -= LANE_MIN_H;
			else weights += laneWeights_.value(lanes[k].key, 1.0);
		}
		if (weights <= 0) break;
		unit = left / weights;
		more = false;
		for (qsizetype k = 0; k < lanes.size(); k++)
			if (!lanes[k].folded && !held[k] && unit * laneWeights_.value(lanes[k].key, 1.0) < LANE_MIN_H) held[k] = more = true;
	}
	heights.resize(lanes.size());
	for (qsizetype k = 0; k < lanes.size(); k++)
		heights[k] = lanes[k].folded ? LANE_FOLDED_H : held[k] ? LANE_MIN_H : unit * laneWeights_.value(lanes[k].key, 1.0);
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

/* the shares add up to the plot's height but for the rounding of their sum: under LANE_FIT_SLACK past it they fit */
double ChartView::maxLaneScroll() const {
	const double over = laneContentHeight() - plotRect().height();
	return over > LANE_FIT_SLACK ? over : 0.0;
}

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

QRectF ChartView::rangeTag(const Lane &lane, const QRectF &shown, QString *text) const {
	const YScale scale = laneScales_.value(lane.key);
	if (lane.folded || (scale.autoRange && !scale.log) || shown.height() < RANGE_TAG_H + 6) return QRectF();
	const QString words = scale.log ? tr("Log") : tr("Manual");
	if (text) *text = words;
	/* in the value labels' column (the trigger's marker keeps its own right of it), its text whole when it fits */
	const double left = LANE_UNIT_W + 1, right = plotRect().left() - 2 - (triggerMarked() ? TRIGGER_LEFT_W : 0);
	const double width = std::min(right - left, std::ceil(QFontMetricsF(tagFont()).horizontalAdvance(words)) + 6);
	return QRectF(left, shown.top() + 1, width, RANGE_TAG_H);
}

QRectF ChartView::laneRangeTagRect(int lane) const {
	const QVector<Lane> plots = plotLayout();
	if (!lanes_ || lane < 0 || lane >= plots.size()) return QRectF();
	const QRectF shown = laneVisible(plots[lane].axes.rect, plotRect());
	return shown.isEmpty() ? QRectF() : rangeTag(plots[lane], shown);
}

QString ChartView::laneRangeTagText(int lane) const {
	const QVector<Lane> plots = plotLayout();
	if (!lanes_ || lane < 0 || lane >= plots.size()) return QString();
	QString text;
	const QRectF shown = laneVisible(plots[lane].axes.rect, plotRect());
	return !shown.isEmpty() && !rangeTag(plots[lane], shown, &text).isEmpty() ? text : QString();
}

int ChartView::rangeTagAt(const QPointF &pos) const {
	const QRectF plot = plotRect();
	if (!lanes_ || pos.x() >= plot.left() || pos.y() < plot.top() || pos.y() > plot.bottom()) return -1;
	const int lane = laneAtY(pos.y());
	if (lane < 0) return -1;
	const QRectF shown = laneVisible(lanesShown_[lane].axes.rect, plot);
	return !shown.isEmpty() && rangeTag(lanesShown_[lane], shown).adjusted(-1, -1, 1, 1).contains(pos) ? lane : -1;
}

bool ChartView::allLanesYAuto() const {
	if (!lanes_) return true;
	for (const Lane &lane : plotLayout()) {
		const YScale scale = laneScales_.value(lane.key);
		if (!scale.autoRange || scale.log) return false;
	}
	return true;
}

void ChartView::setAllLanesYAuto() {
	if (!lanes_ || allLanesYAuto()) return;
	for (const Lane &lane : plotLayout()) {
		YScale &scale = laneScales_[lane.key];
		scale.autoRange = true;
		scale.log = false;
		scale.initialized = false;
	}
	emit laneYChanged();
	refresh();
}

int ChartView::currentLane() const {
	const QVector<Lane> plots = plotLayout();
	if (!lanes_ || plots.isEmpty()) return -1;
	for (int k = 0; k < plots.size(); k++)
		if (plots[k].key == currentLane_) return k;
	return 0; /* none chosen, or its unit gone: the first */
}

void ChartView::setCurrentLane(int lane) {
	const QString key = laneKey(lane);
	if (!lanes_ || lane < 0 || lane >= laneCount() || lane == currentLane()) return;
	currentLane_ = key;
	emit currentLaneChanged();
	refresh();
}

/* the track: as tall as the plot, in the right pad; the handle: the plot's share of the lanes, where the scroll is */
QRectF ChartView::laneScrollBarRect() const {
	const QRectF plot = plotRect();
	if (!lanes_ || laneContentHeight() <= plot.height() + LANE_FIT_SLACK) return QRectF();
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
	if (stateBadge_.contains(pos)) /* the short window's lock: what it is and where it is turned off */
		return tr("The view locks on the busiest line's crossings at windows under 100 ms · Display → Lock short "
				"windows turns it off");
	if (stateRect_.contains(pos)) return stateFull_; /* the state corner: its whole text */
	if (memoryHandle_.adjusted(-2, -4, 2, 4).contains(pos))
		return tr("The view: drag it along the memory · Wheel: a window earlier or later");
	if (overviewRect().contains(pos)) return memoryStripTip();
	if (divisionRect_.contains(pos))
		return (divisionFromT_ ? tr("A division of the grid (10 across the view) and the clock time at 0, the trigger's "
							 "crossing (T): the labels count from T.")
							   : tr("A division of the grid (10 across the view) and the clock time at 0, the right "
							 "edge: the labels count from there."))
				+ QLatin1Char('\n') + tr("Wheel over the chart: the next window of 1, 2 or 5 per division.");
	/* a cursor's tag: its time, and while the view is held on a trigger's crossing how far from T (U-7, as a scope's
	 * cursors read); the tag itself stays a letter, so the bar between the two keeps its room */
	for (int k = 0; k < 2; k++) {
		const double t = k == 0 ? cursorA_ : cursorB_;
		if (!std::isfinite(t) || t < lastAxes_.t0 || t > lastAxes_.t1) continue;
		const double x = lastAxes_.x(t);
		if (!QRectF(x - 9, lastAxes_.rect.top() - 2, 18, 16).contains(pos)) continue;
		const QString name = k == 0 ? QStringLiteral("A") : QStringLiteral("B");
		const QString at = timeLabel(epochMs_, t, window_ < 0.01 ? 1e-6 : 1e-3);
		/* on the grid its place first ("Cursor B at T +1.750 ms"), the time under it now after, and what keeps it there */
		const QString place = cursorPlaceText(k);
		if (!place.isEmpty())
			return tr("Cursor %1 at %2 · %3").arg(name, place, at) + QLatin1Char('\n')
					+ tr("On the grid: it keeps its place while the wave moves under it · Shift while dragging: a tenth "
						 "of a division");
		const QString fromT = fromTText(t);
		return fromT.isEmpty() ? tr("Cursor %1 at %2").arg(name, at) : tr("Cursor %1 at %2 · %3").arg(name, at, fromT);
	}
	if (const int chip = chipAt(pos); chip >= 0) { /* a recording's window has no trigger (nothing comes after the file) */
		const QString click = recording_ ? tr("Click or right-click: Histogram, Spectrum")
										 : tr("Click or right-click: Histogram, Spectrum, Trigger on this line");
		const QString stopped = stoppedTip(chip); /* its value greyed: why */
		return stopped.isEmpty() ? click : stopped + QLatin1Char('\n') + click;
	}
	/* the flag: the crossing it stands over in words, when there is one, then what it does */
	if (trigger_.on && triggerMark_.contains(pos)) {
		const QString point = triggerPointText();
		const QString drag = tr("Drag: where the crossing sits in the window · Double-click: back to %1 %")
				.arg(int(std::lround(TRIGGER_AT * 100)));
		return point.isEmpty() ? drag : point + QLatin1Char('\n') + drag;
	}
	/* the tab and the marker: the level's words first ("I_LOAD 0.4 A, rising", "(above range)" off scale), then what
	 * each does */
	if (trigger_.on && triggerLevelTag_.contains(pos))
		return triggerTagLabel() + QLatin1Char('\n')
				+ tr("Drag: the trigger level · Click the arrow: the edge (rising, falling, either)");
	if (trigger_.on && triggerLevelMark_.contains(pos))
		return triggerTagLabel() + QLatin1Char('\n') + tr("Drag: the trigger level · the edge: in the Trigger row");
	const QRectF plot = plotRect();
	if (laneScrollBarRect().adjusted(-LANE_BAR_GRIP, 0, LANE_BAR_GRIP, 0).contains(pos))
		return tr("Scroll the lanes: drag the handle, or click above or below it for a page");
	if (separatorAt(pos) >= 0) return tr("Drag: this lane's height · Double-click: equal heights");
	const QString gap = fastGapAt(pos);
	if (!gap.isEmpty()) return gap;
	if (!lanes_ || pos.y() < plot.top() || pos.y() > plot.bottom() || pos.x() > plot.right()) return QString();
	const int lane = laneAtY(pos.y());
	if (lane < 0) return QString();
	const QRectF shown = laneVisible(lanesShown_[lane].axes.rect, plot);
	if (pos.y() < shown.top() || pos.y() > shown.bottom()) return QString(); /* between two lanes */
	if (lanesShown_[lane].folded) return tr("Open lane");
	if (laneMenuButtonAt(pos) == lane) return tr("Y range and lane options");
	if (pos.x() < LANE_UNIT_W) return tr("Fold lane");
	if (pos.x() >= plot.left()) return QString();
	if (rangeTagAt(pos) == lane) { /* its range in words, and what the click does */
		const YScale scale = laneScales_.value(lanesShown_[lane].key);
		const QString unit = lanesShown_[lane].label;
		const QString range = tr("%1 to %2").arg(chartNumber(laneYLo(lane)), chartNumber(laneYHi(lane)))
				+ (unit.isEmpty() ? QString() : QLatin1Char(' ') + unit);
		return scale.log ? (scale.autoRange ? tr("This lane's Y scale is logarithmic · Click: back to Auto")
						: tr("This lane's Y scale is logarithmic, manual: %1 · Click: back to Auto").arg(range))
				: tr("This lane's Y range is manual: %1 · Click: back to Auto").arg(range);
	}
	QStringList parts;
	if (maxLaneScroll() > 0) parts << tr("Wheel: scroll the lanes");
	parts << tr("Ctrl + wheel: zoom this lane") << tr("Click: its Y range in the toolbar") << tr("Double-click: Auto")
			<< tr("Right-click: its Y range and Fold lane");
	return parts.join(QStringLiteral(" · "));
}

/* a fast line's gap under the mouse (3 px either side): how many records were lost there, or that the stream
 * started again; lanes: the lines of the lane under the mouse */
QString ChartView::fastGapAt(const QPointF &pos) const {
	const QRectF plot = plotRect();
	if (!plot.contains(pos) || lanesShown_.isEmpty()) return QString();
	QString unit;
	bool anyUnit = !lanes_;
	if (lanes_) {
		const int lane = laneAtY(pos.y());
		if (lane < 0 || lanesShown_[lane].folded) return QString();
		unit = lanesShown_[lane].key;
	}
	const Axes &axes = lastAxes_;
	const double perPixel = axes.span / std::max(1.0, axes.rect.width());
	const double t = axes.t0 + (pos.x() - axes.rect.left()) * perPixel, reach = 3 * perPixel;
	QStringList parts;
	for (const Series &s : series_) {
		if (!s.fast || (!anyUnit && s.unit != unit) || s.fast->size() < 2) continue;
		const fast::Store &store = *s.fast;
		const qsizetype j = store.lowerBound(t - reach);
		if (j >= store.size()) continue;
		const qsizetype k = j > 0 && store.startsAfterGap(j) ? j : store.segmentEnd(j);
		if (k <= 0 || k >= store.size() || store.timeAt(k - 1) > t + reach || store.timeAt(k) < t - reach) continue;
		const qint64 lost = store.lostBefore(k);
		parts << (lost < 0 ? tr("%1: the stream started again here").arg(s.name)
						   : tr("%1: %n sample(s) lost here", nullptr, int(std::min<qint64>(lost, INT_MAX))).arg(s.name));
	}
	return parts.join(QLatin1Char('\n'));
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

double ChartView::drawnTo(int key) const {
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return NAN;
	for (const BinnedLine &line : lastBinned_) {
		if (line.series != &it.value() || line.bins.isEmpty()) continue;
		const Bin &b = line.bins.last();
		if (b.count <= 2 && !b.bar) return b.count == 2 ? b.t1 : b.t0;
		return (double(b.column) + 0.5) * lastAxes_.columnSeconds(); /* as toPolyline draws it */
	}
	return NAN;
}

bool ChartView::drawnRange(int key, double &lo, double &hi) const {
	const auto it = series_.constFind(key);
	if (it == series_.constEnd()) return false;
	for (const BinnedLine &line : lastBinned_) {
		if (line.series != &it.value() || line.bins.isEmpty()) continue;
		lo = line.lo;
		hi = line.hi;
		return true;
	}
	return false;
}

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

bool ChartView::divisionsShown() const {
	return timeGrid_ == TimeGrid::Divisions || (timeGrid_ == TimeGrid::Auto && window_ < DIVISIONS_BELOW);
}

void ChartView::setTimeGrid(TimeGrid grid) {
	timeGrid_ = grid;
	wheelNotches_ = 0;
	refresh();
}

/* the next window of 1, 2 or 5 per division below (in) or above `window` */
double ChartView::divisionWindow(double window, bool in) {
	const double magnitude = std::pow(10.0, std::floor(std::log10(window / DIVISIONS)));
	double best = in ? 0 : INFINITY;
	for (double decade : { 0.1, 1.0, 10.0 })
		for (double m : { 1.0, 2.0, 5.0 }) {
			const double w = m * magnitude * decade * DIVISIONS;
			if (in && w < window * (1 - 1e-9)) best = std::max(best, w);
			if (!in && w > window * (1 + 1e-9)) best = std::min(best, w);
		}
	return best;
}

/* the times count from T while the view is held on a trigger's crossing in view (a scope's time 0 at the trigger);
 * live, held by the user or with the crossing out of view, from the right edge */
bool ChartView::timesFromT(double t0, double t1) const {
	return triggerMarked() && !live_ && std::isfinite(trigger_.at) && trigger_.at >= t0 && trigger_.at <= t1;
}

/* The value grid and its labels, then the time grid: at fixed wall-clock times, so it moves with the data, or
 * divisions that stand still (divisionsShown). Crisp 1 px lines: no antialiasing. */
ChartView::GridTicks ChartView::gridTicks(const Axes &axes) const {
	GridTicks ticks;
	if (divisionsShown()) {
		/* 0 at the right edge or at T, a line every tenth of the plot from there. The places as fractions of the plot
		 * from 0's, rounded to a millionth of a pixel: the view's times are large numbers of seconds, and their
		 * rounding would move a line on a pixel's edge by a pixel now and then */
		ticks.divisions = true;
		ticks.division = ticks.timeStep = window_ / DIVISIONS;
		ticks.fromT = timesFromT(axes.t0, axes.t1);
		ticks.zero = ticks.fromT ? trigger_.at : axes.t1;
		const auto stable = [](double x) { return std::round(x * 1e6) / 1e6; };
		const double perDivision = axes.rect.width() / DIVISIONS;
		const double zeroX = stable(axes.x(ticks.zero));
		const int first = int(std::ceil((axes.rect.left() - zeroX) / perDivision - 1e-6));
		const int last = int(std::floor((axes.rect.right() - zeroX) / perDivision + 1e-6));
		for (int k = first; k <= last; k++) {
			const double x = stable(zeroX + k * perDivision);
			ticks.times << ticks.zero + k * ticks.division;
			ticks.timeX << x;
			ticks.offsets << k;
			if (x > axes.rect.left() + 0.5 && x < axes.rect.right() - 0.5) ticks.lineX << x;
		}
	} else {
		int target = std::max(2, int(axes.columns / TIME_LABEL_SPACING));
		ticks.timeStep = niceTimeStep(window_, target);
		/* below a millisecond a label is wider ("14:03:12.34567"): fewer of them, a clear gap between two */
		const QFontMetricsF labels(smallFont());
		while (ticks.timeStep < 1e-3 && target > 2
				&& ticks.timeStep / window_ * axes.columns < labels.horizontalAdvance(timeLabel(epochMs_, axes.t1, ticks.timeStep)) + 24)
			ticks.timeStep = niceTimeStep(window_, --target);
		for (double t = std::ceil(axes.t0 / ticks.timeStep) * ticks.timeStep; t <= axes.t1; t += ticks.timeStep) {
			ticks.times << t;
			ticks.timeX << axes.x(t);
		}
		ticks.lineX = ticks.timeX;
	}
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
	const int current = lanes_ && plots.size() > 1 ? currentLane() : -1; /* lit: one lane of several */
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
		QString tagText;
		const QRectF tag = lanes_ ? rangeTag(lane, shown, &tagText) : QRectF();
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
			const double left = lanes_ ? LANE_UNIT_W : 2; /* lanes: their units up the left edge */
			/* the trigger on: right of them its level's marker has a column of its own, so it covers none */
			const double right = plot.left() - 6 - (triggerMarked() ? TRIGGER_LEFT_W : 0);
			/* none under the lane's range tag: a label there would be half covered */
			if (!tag.isEmpty() && top < tag.bottom() + 1
					&& right - QFontMetricsF(p.font()).horizontalAdvance(text) < tag.right() + 2)
				return;
			valueLabels_ << text;
			p.setPen(c.muted);
			valueLabelRects_ << QRectF(left, top, right - left, 16);
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
		for (double x : ticks.lineX) {
			if (!lines) break;
			p.setPen(QPen(c.grid, 1));
			p.drawLine(QPointF(x, shown.top()), QPointF(x, shown.bottom()));
		}
		if (lanes_) { /* its fold button and its menu's, then its unit up the left edge of its labels, in the part in view
		               * (a click on the unit name folds it too) */
			QRectF button, menu;
			laneButtons(shown, &button, &menu);
			foldButton(index, button, false);
			if (!menu.isEmpty()) menuButton(index, menu);
			const double nameTop = menu.isEmpty() ? button.bottom() : menu.bottom();
			const QRectF name(0, nameTop, LANE_UNIT_W, shown.bottom() - nameTop);
			if (!tag.isEmpty()) { /* Manual in the warn colour (a range that does not follow), Log in the accent */
				const QColor ink = laneScales_.value(lane.key).log ? c.accent : c.warn;
				QColor fill = ink;
				fill.setAlphaF(index == hoverTag_ ? 0.32 : 0.16);
				p.save();
				p.setRenderHint(QPainter::Antialiasing, true);
				p.setPen(Qt::NoPen);
				p.setBrush(fill);
				p.drawRoundedRect(tag, 4, 4);
				/* a longer word (Arabic's "Log") a little smaller rather than cut; cut only below 6 pt */
				QFont font = tagFont();
				while (font.pointSizeF() > 6 && QFontMetricsF(font).horizontalAdvance(tagText) > tag.width() - 4)
					font.setPointSizeF(font.pointSizeF() - 0.5);
				p.setFont(font);
				p.setPen(ink);
				p.drawText(tag, Qt::AlignCenter, QFontMetricsF(font).elidedText(tagText, Qt::ElideRight, tag.width() - 2));
				p.restore();
			}
			/* the current lane's unit lit: the one the toolbar's Y range shows (only with more than one lane) */
			p.setPen(index == current ? c.accent : c.text);
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
	timeLabels_.clear();
	timeLabelX_.clear();
	timeGridX_ = ticks.lineX;
	divisionText_.clear();
	divisionRect_ = QRectF();
	if (ticks.divisions) {
		drawDivisionLabels(p, ticks, axes);
	} else {
		const QFontMetricsF metrics(p.font());
		for (qsizetype i = 0; i < ticks.times.size(); i++) {
			const double x = ticks.timeX[i];
			const QString label = timeLabel(epochMs_, ticks.times[i], ticks.timeStep);
			const double half = metrics.horizontalAdvance(label) / 2;
			if (x - half < 0 || x + half > width()) continue; /* never cut at the chart's edge: that one left out */
			p.setPen(c.muted);
			timeLabels_ << label;
			timeLabelX_ << x;
			p.drawText(QRectF(x - 60, plot.bottom() + 6, 120, 16), Qt::AlignCenter, label);
		}
	}
	p.setRenderHint(QPainter::Antialiasing, true);
}

namespace {

/* an offset on the divisions' axis, in the unit the window is written in ("-8 ms", "+4 ms", "0"); one piece in
 * Arabic, its unit beside its number */
/* the unit a window's offsets are written in: µs below 1 ms, ms below 1 s, s below 2 min, then min and h */
QString windowUnit(double window, double &scale) {
	scale = window < 1e-3 ? 1e-6 : window < 1 ? 1e-3 : window < 120 ? 1 : window < 7200 ? 60 : 3600;
	return window < 1e-3 ? QStringLiteral("µs") : window < 1 ? QStringLiteral("ms")
			: window < 120 ? QStringLiteral("s") : window < 7200 ? QStringLiteral("min") : QStringLiteral("h");
}

QString offsetText(double seconds, double window) {
	if (std::fabs(seconds) < window * 1e-9) return QStringLiteral("0");
	double scale;
	const QString unit = windowUnit(window, scale);
	const double v = std::round(seconds / scale * 1e6) / 1e6; /* no 0.30000000004 from k times a division */
	return ltrPiece(QStringLiteral("%1%2 %3").arg(v > 0 ? QStringLiteral("+") : QString(), QString::number(v, 'g', 7), unit));
}

/* a division's length in its own unit ("1 ms", "200 µs", "2.5 s") */
QString divisionLength(double seconds) {
	const double scale = seconds < 1e-3 ? 1e-6 : seconds < 1 ? 1e-3 : seconds < 60 ? 1 : seconds < 3600 ? 60 : 3600;
	const QString unit = seconds < 1e-3 ? QStringLiteral("µs") : seconds < 1 ? QStringLiteral("ms")
			: seconds < 60 ? QStringLiteral("s") : seconds < 3600 ? QStringLiteral("min") : QStringLiteral("h");
	return QStringLiteral("%1 %2").arg(QString::number(seconds / scale, 'g', 4), unit);
}

} // namespace

double ChartView::timeOrigin() const {
	const double t1 = viewEnd();
	return timesFromT(t1 - window_, t1) ? trigger_.at : NAN;
}

/* three decimals in the window's unit: a cursor or the mouse a pixel apart reads apart at any width up to 4K */
QString ChartView::fromTText(double t) const {
	const double origin = timeOrigin();
	if (!std::isfinite(origin) || !std::isfinite(t)) return QString();
	double scale;
	const QString unit = windowUnit(window_, scale);
	/* a hair before T reads "T +0.000", not "T -0.000": a negative zero is no place */
	const double raw = (t - origin) / scale, v = std::fabs(raw) < 5e-4 ? 0.0 : raw;
	return ltrPiece(QStringLiteral("T %1%2 %3").arg(v >= 0 ? QStringLiteral("+") : QString(), QString::number(v, 'f', 3), unit));
}

/* on the grid: from T as fromTText, else from the right edge as the division labels count ("-3.200 ms"), from the
 * place itself, so a live view's text does not change from frame to frame */
QString ChartView::cursorPlaceText(int k) const {
	const double place = cursorPlace_[k & 1], t = (k & 1) == 0 ? cursorA_ : cursorB_;
	if (!cursorsOnGrid() || !std::isfinite(place) || !std::isfinite(t)) return QString();
	const QString fromT = fromTText(t);
	if (!fromT.isEmpty()) return fromT;
	double scale;
	const QString unit = windowUnit(window_, scale);
	const double v = (place - 1) * window_ / scale;
	return ltrPiece(QStringLiteral("%1 %2").arg(QString::number(std::fabs(v) < 5e-4 ? 0.0 : v, 'f', 3), unit));
}

/* "1 ms/div · 14:03:12.345": the division and the clock time at 0, as precise as the division needs. Live, its clock
 * time is written again at most every DIVISION_CLOCK_MS: a number rewritten at every frame reads as noise */
QString ChartView::divisionReadoutText(const GridTicks &ticks) const {
	const QString key = QStringLiteral("%1|%2").arg(ticks.division, 0, 'g', 17).arg(ticks.fromT);
	const bool following = live();
	if (!following || key != divisionClockKey_ || !divisionClockAge_.isValid()
			|| divisionClockAge_.elapsed() >= DIVISION_CLOCK_MS) {
		divisionClock_ = timeLabel(epochMs_, ticks.zero, ticks.division);
		divisionClockKey_ = key;
		divisionClockAge_.start();
	}
	return ltrPiece(QStringLiteral("%1/div · %2").arg(divisionLength(ticks.division), divisionClock_));
}

/* the readout's box in the state corner: as wide as its text with every digit a 0, so the legend's end does not move
 * when the clock time's digits change; 0 without divisions */
double ChartView::divisionReadoutWidth() const {
	if (!divisionsShown()) return 0;
	const double division = window_ / DIVISIONS;
	QString text = QStringLiteral("%1/div · %2").arg(divisionLength(division), timeLabel(epochMs_, viewEnd(), division));
	for (QChar &ch : text)
		if (ch.isDigit()) ch = QLatin1Char('0');
	return std::ceil(QFontMetricsF(labelFont()).horizontalAdvance(text)) + 2 * DIVISION_PAD;
}

/* The divisions' labels, each its offset from 0 (every 1, 2 or 5 divisions, 0 always, as many as have room). The
 * readout is only made here: drawState draws it in the row above the plot, so no label is left out for it (the owner:
 * it hid the "-2 ms" label) */
void ChartView::drawDivisionLabels(QPainter &p, const GridTicks &ticks, const Axes &axes) const {
	const ThemeColors &c = Theme::colors();
	const QRectF &plot = axes.rect;
	const QFontMetricsF metrics(p.font());
	QStringList texts;
	double widest = 0;
	for (int k : ticks.offsets) {
		texts << offsetText(k * ticks.division, window_);
		widest = std::max(widest, metrics.horizontalAdvance(texts.last()));
	}
	const double perDivision = plot.width() / DIVISIONS;
	int every = 1;
	for (int step : { 1, 2, 5, 10 }) {
		every = step;
		if (step * perDivision >= widest + 16) break;
	}
	QVector<qsizetype> shown;
	for (qsizetype i = 0; i < texts.size(); i++) {
		const double half = metrics.horizontalAdvance(texts[i]) / 2;
		if (ticks.offsets[i] % every != 0 || ticks.timeX[i] - half < 0 || ticks.timeX[i] + half > width()) continue;
		shown << i;
	}
	divisionText_ = divisionReadoutText(ticks);
	divisionFromT_ = ticks.fromT;
	const double top = plot.bottom() + 6;
	for (qsizetype i : std::as_const(shown)) {
		const double x = ticks.timeX[i];
		p.setPen(c.muted);
		timeLabels_ << texts[i];
		timeLabelX_ << x;
		p.drawText(QRectF(x - 60, top, 120, 16), Qt::AlignCenter, texts[i]);
	}
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

/* the pieces of a polyline between its breaks, each a stroke; a piece of one point (a lone record between two gaps) a
 * dot */
void ChartView::strokePieces(QPainter &p, const QPolygonF &poly, const QVector<qsizetype> &breaks, qsizetype from,
		qsizetype to, const QColor &color, bool thin, qreal dpr) {
	if (breaks.isEmpty()) {
		if (to - from >= 1) strokePolyline(p, from == 0 && to == poly.size() ? poly : QPolygonF(poly.mid(from, to - from)), color,
				thin, dpr);
		return;
	}
	qsizetype start = from;
	auto next = std::upper_bound(breaks.begin(), breaks.end(), from);
	while (start < to) {
		const qsizetype end = next == breaks.end() ? to : std::min(to, *next);
		QPolygonF piece = poly.mid(start, end - start);
		if (piece.size() == 1) piece << piece.first() + QPointF(0.01, 0); /* a dot: a stroke of no length draws nothing */
		strokePolyline(p, piece, color, thin, dpr);
		start = end;
		if (next != breaks.end()) ++next;
	}
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

/* a line's bars, plain fills, antialiased: an edge on half a pixel is the same partly covered pixel whether it is
 * drawn here or on a stripe (without, the two rounded it to different sides). crisp (a fast line, a bar in nearly
 * every column): each bar snapped to whole device pixels and filled without antialiasing, the same pixels here and on
 * a stripe (a stripe lies on whole device pixels), at a fraction of the cost (two such lines took 9 ms antialiased;
 * fillRect without antialiasing is the raster engine's fastest path, drawRects went through its general one) */
void ChartView::fillBands(QPainter &p, const QRectF *bands, qsizetype count, const QColor &color, bool crisp) {
	if (count <= 0) return;
	if (crisp) {
		/* snapped in the device's pixels, then back into p's coordinates: whole device pixels each way (the device's
		 * transform holds the widget's place in the window too, which drawing in it would count twice) */
		const QTransform toDevice = p.deviceTransform(), back = toDevice.inverted();
		QVector<QRectF> rects;
		rects.reserve(count);
		for (qsizetype k = 0; k < count; k++) {
			const QRectF d = toDevice.mapRect(bands[k]);
			const double x0 = std::round(d.left()), x1 = std::max(x0 + 1, std::round(d.right()));
			const double y0 = std::round(d.top()), y1 = std::max(y0 + 1, std::round(d.bottom()));
			rects.push_back(back.mapRect(QRectF(QPointF(x0, y0), QPointF(x1, y1))));
		}
		p.save();
		p.setRenderHint(QPainter::Antialiasing, false);
		for (const QRectF &r : std::as_const(rects)) p.fillRect(r, color);
		p.restore();
		return;
	}
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
	QVector<QVector<qsizetype>> breaks(lines.size()); /* a fast line's gaps */
	inParallel(lines.size(), [&](qsizetype i) {
		const BinnedLine &line = lines[i];
		if (line.bins.isEmpty() || !axesOf[i]) return;
		const Axes &axes = *axesOf[i];
		const auto x = [&axes](double t) { return axes.x(t); };
		double lo = line.lo, hi = line.hi; /* Normalize: the line's own range */
		if (normalized_) widenFlatRange(lo, hi);
		const auto y = [&](double v) { return normalized_ ? axes.y((v - lo) / (hi - lo)) : axes.y(v); };
		polys[i] = toPolyline(line.bins, axes.columnSeconds(), x, y, &bands[i], bandWidth, 1 / dpr, &breaks[i]);
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
			fillBands(p, bands[i].constData(), bands[i].size(), lines[i].series->color, lines[i].series->fast != nullptr);
			if (!polys[i].isEmpty()) strokePieces(p, polys[i], breaks[i], 0, polys[i].size(), lines[i].series->color, thin[i], dpr);
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
			fillBands(ip, bars.constData() + first, last - first, lines[i].series->color, lines[i].series->fast != nullptr);
			const QPolygonF &poly = polys[i]; /* its x never falls: the part in the stripe, a point either side */
			const qsizetype from = std::max<qsizetype>(
					0, std::lower_bound(poly.begin(), poly.end(), rect.left() - REACH, beforeX) - poly.begin() - 1);
			const qsizetype to = std::min<qsizetype>(
					poly.size(), std::upper_bound(poly.begin(), poly.end(), rect.right() + REACH, afterX) - poly.begin() + 1);
			if (to - from >= 1) strokePieces(ip, poly, breaks[i], from, to, lines[i].series->color, thin[i], dpr);
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
		for (double tx : ticks.lineX) {
			const float x = float(std::floor(map(QPointF(tx, 0)).x()) + grid.widthPx / 2);
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
		QVector<qsizetype> breaks; /* a fast line's gaps: no segment across them */
		const QPolygonF poly = toPolyline(line.bins, axes.columnSeconds(), x, y, nullptr, 0, 1 / dpr, &breaks);
		const quint32 rgba = gpuColor(line.series->color);
		QVector<GpuLines::Segment> &segments = parts[i];
		segments.resize(std::max<qsizetype>(1, poly.size() - 1));
		/* scale and shift as arithmetic, not a transform per point */
		float x0 = float(poly[0].x() * dpr + dx), y0 = float(poly[0].y() * dpr + dy);
		if (poly.size() == 1) segments[0] = { x0, y0, x0, y0, rgba };
		qsizetype made = 0, nextBreak = 0;
		for (qsizetype k = 1; k < poly.size(); k++) {
			const float x1 = float(poly[k].x() * dpr + dx), y1 = float(poly[k].y() * dpr + dy);
			if (nextBreak < breaks.size() && breaks[nextBreak] == k) {
				nextBreak++;
				/* a lone point between two gaps: a dot, as the CPU's */
				if (k + 1 >= poly.size() || (nextBreak < breaks.size() && breaks[nextBreak] == k + 1))
					segments[made++] = { x1, y1, x1 + 0.01f, y1, rgba };
			} else {
				segments[made++] = { x0, y0, x1, y1, rgba };
			}
			x0 = x1;
			y0 = y1;
		}
		if (poly.size() > 1) segments.resize(made);
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
	/* the now edges: a layer under the marks' (the cursors', the notes', the level's), as the CPU draws them */
	nowEdges_ = nowEdgeLines(plots);
	if (!nowEdges_.isEmpty()) {
		GpuLines::Layer edges;
		edges.widthPx = float(dpr);
		for (const QLineF &edge : std::as_const(nowEdges_)) {
			const QPointF a = map(edge.p1()), b = map(edge.p2());
			edges.segments.push_back({ float(a.x()), float(a.y()), float(b.x()), float(b.y()), gpuColor(nowEdgeColor()) });
		}
		frame.layers << edges;
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
	/* the trigger: its level dashed in its line's colour, its tag, its marker */
	double levelY;
	QRectF levelLane, tag, levelTag;
	if (triggerGeometry(plots, lines, levelY, levelLane, tag, levelTag)) {
		const QColor color = series_.value(trigger_.key).color;
		/* as the CPU's pen (1.2 px, Qt::DashLine: dashes of 4.8, gaps of 2.4), the cursors and the notes; off scale its
		 * Qt::DotLine with flat ends (dots of 1.2, gaps of 2.4) */
		const bool dotted = triggerOffScale_ != 0;
		dashes(marks.segments, map(QPointF(levelLane.left(), levelY)), map(QPointF(levelLane.right(), levelY)),
				(dotted ? 1.2 : 4.8) * dpr, 2.4 * dpr, gpuColor(color));
		/* its marker and tab lie outside the layer: the CPU draws them (drawTriggerTab), as the flag */
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
	/* A frame the system let go (still busy with the one before): the layer keeps the last that reached it. Live, the
	 * next frame comes anyway; held, none comes, and after a resize the layer stayed at its old size, the window's own
	 * plot beside it blank (the black bar of a recording's window made bigger): a frame again, at the next refresh */
	if (presented && gpu_->droppedFrames() != droppedSeen_) {
		droppedSeen_ = gpu_->droppedFrames();
		if (!live_) QTimer::singleShot(DROPPED_AGAIN_MS, this, [this] { paintSoon(); });
	}
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
		double v = 0;
		if (live_) {
			if (s.hasShown) value = chartNumber(s.shown);
		} else if (latestInView(s, t0, t1, v)) {
			value = chartNumber(v);
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
 * its tab in the margin right of the plot at the line's height, and its T at the crossing, on the level crossed (the
 * owner: the T belongs against the level's line, not at the plot's top), when the crossing is in view */
bool ChartView::triggerGeometry(const QVector<Lane> &plots, const QVector<BinnedLine> &lines, double &levelY,
		QRectF &lane, QRectF &tag, QRectF &levelTag) const {
	triggerLineY_ = NAN;
	triggerTag_ = triggerLevelTag_ = triggerEdgeButton_ = triggerTabFull_ = triggerLevelMark_ = triggerMarkFull_ = QRectF();
	triggerTagText_.clear();
	tag = levelTag = QRectF();
	triggerOffScale_ = triggerBeyond_ = 0;
	if (!triggerMarked()) return false; /* off, or a short window's lock: nothing of it drawn */
	int index = 0;
	for (auto it = series_.constBegin(); it != series_.constEnd() && it.key() != trigger_.key; ++it) index++;
	if (index >= lines.size()) return false;
	const QRectF all = plotRect();
	const double level = watchedSettings().level;
	/* a level the line does not reach in view: Normal waits for ever, and the row says why */
	if (lines[index].lo <= lines[index].hi) triggerBeyond_ = level > lines[index].hi ? 1 : level < lines[index].lo ? -1 : 0;
	for (const Lane &plot : plots) {
		if (!plot.lines.contains(index)) continue;
		const Axes &a = plot.axes;
		/* lanes: only while its lane is open and in view, within the part in view */
		const QRectF shown = lanes_ ? laneVisible(a.rect, all) : a.rect;
		if (plot.folded || shown.isEmpty()) return false;
		double lo = lines[index].lo, hi = lines[index].hi;
		if (normalized_) widenFlatRange(lo, hi);
		/* beyond the lane's range the line is pinned to its edge, so it can be dragged back, but drawn otherwise (dotted,
		 * ▲ or ▼): read as in range it hid why Normal never fired. The range is not widened for it */
		const double y = normalized_ ? a.y((level - lo) / (hi - lo)) : a.y(level);
		triggerOffScale_ = y < a.rect.top() - 0.5 ? 1 : y > a.rect.bottom() + 0.5 ? -1 : 0;
		levelY = std::clamp(y, shown.top(), shown.bottom());
		lane = shown;
		triggerLineY_ = levelY;
		triggerLane_ = lane;
		triggerAxes_ = a;
		triggerLo_ = lo;
		triggerHi_ = hi;
		/* where the line crossed (the flag's x; no longer drawn there: the flag above and the marks at the plot's sides
		 * show it): at the crossing's time on the level it crossed, kept inside the lane's part in view */
		if (std::isfinite(trigger_.at) && trigger_.at >= a.t0 && trigger_.at <= a.t1 && shown.height() >= 16) {
			const double at = normalized_ ? a.y((trigger_.atLevel - lo) / (hi - lo)) : a.y(trigger_.atLevel);
			const double top = std::round(std::clamp(at - 8, shown.top(), shown.bottom() - 16));
			tag = QRectF(a.x(trigger_.at) - 7, top, 14, 16);
			triggerTag_ = tag;
		}
		/* the level's tab in the margin right of the plot and its marker in the column left of it, at the line's
		 * height within its lane: cut as the lane is where the lane is scrolled partly out of view (levelTag: the
		 * part in view, which takes the mouse). On whole pixels, as every picture */
		const double top = std::round(std::clamp(levelY - TRIGGER_TAB_H / 2, a.rect.top(), a.rect.bottom() - TRIGGER_TAB_H));
		triggerMarkFull_ = QRectF(all.left() - 2 - TRIGGER_LEFT_W, top, TRIGGER_LEFT_W, TRIGGER_TAB_H);
		triggerLevelMark_ = triggerMarkFull_.intersected(QRectF(triggerMarkFull_.left(), shown.top(), TRIGGER_LEFT_W,
				shown.height()));
		triggerTabFull_ = QRectF(std::round(all.right() + TRIGGER_TAB_X), top, TRIGGER_TAB_W, TRIGGER_TAB_H);
		const QRectF band(triggerTabFull_.left(), shown.top(), TRIGGER_TAB_W, shown.height());
		levelTag = triggerTabFull_.intersected(band);
		if (!levelTag.isEmpty()) {
			triggerTagText_ = triggerTabLabel() + QLatin1Char(' ') + edgeSymbol();
			triggerLevelTag_ = levelTag;
			triggerEdgeButton_ = QRectF(triggerTabFull_.right() - TRIGGER_TAB_BUTTON, triggerTabFull_.top(), TRIGGER_TAB_BUTTON,
					TRIGGER_TAB_H).intersected(band);
		}
		return true;
	}
	return false;
}

/* A view held on a crossing fills as the samples come: its right part is empty until they do, which read as missing
 * data. A faint edge at the newest sample's time in every lane in view says where the data ends now (both paths draw
 * these lines) */
QVector<QLineF> ChartView::nowEdgeLines(const QVector<Lane> &plots) const {
	QVector<QLineF> edges;
	if (!triggerCapturing()) return edges;
	const double now = triggerTime();
	const QRectF all = plotRect();
	for (const Lane &plot : plots) {
		const Axes &a = plot.axes;
		if (plot.folded || now < a.t0 || now > a.t1) continue;
		const QRectF shown = lanes_ ? laneVisible(a.rect, all) : a.rect;
		if (shown.isEmpty()) continue;
		const double x = a.x(now);
		edges << QLineF(x, shown.top(), x, shown.bottom());
	}
	return edges;
}

void ChartView::drawTrigger(QPainter &p, const QVector<Lane> &plots, const QVector<BinnedLine> &lines, Marks part) const {
	double levelY;
	QRectF lane, tag, levelTag;
	if (!triggerGeometry(plots, lines, levelY, lane, tag, levelTag)) return;
	p.save();
	/* off scale dotted (flat ends: the card's dots are as long as the CPU's), else dashed */
	QPen pen(series_.value(trigger_.key).color, 1.2, triggerOffScale_ != 0 ? Qt::DotLine : Qt::DashLine);
	if (triggerOffScale_ != 0) pen.setCapStyle(Qt::FlatCap);
	p.setPen(pen);
	if (part == Marks::Lines) p.drawLine(QPointF(lane.left(), levelY), QPointF(lane.right(), levelY));
	p.restore();
}

namespace {
/* where a mark's pointer is: none (a level off scale), under its box (the flag), right of it (the level's marker) or
 * left of it (the tab) */
enum class MarkPointer { None, Down, Right, Left };

/* a mark's border in device pixels, whole ones (1 px at rest, 2 lit, as the cursors' tags) */
int markPen(qreal dpr, bool lit) {
	return std::max(1, int(std::lround((lit ? 2 : 1) * dpr)));
}

/* The trigger's marks are one family: the flag above the plot, the level's marker left of it and its tab right of it
 * (the owner: one finished look). A box TRIGGER_MARK_H high, its corners, border and fill alike (lit: the border
 * stronger on the theme's border colour), joined to a pointer of one size in the line's colour as one outline (solid:
 * filled in it; hollow: its outline only). Drawn in device pixels with the box's edges on whole ones, so the outlines
 * are crisp at 225 % as at 100 %; `at` is the tip along its side (device pixels from the picture's top, Right and
 * Left), kept where the side is straight. The caller writes in `box` (logical pixels) */
QImage markPicture(qreal dpr, const QColor &color, double boxWidth, MarkPointer pointer, double at, bool solid, bool lit,
		QRectF &box) {
	const ThemeColors &c = Theme::colors();
	const int w = 2 * int(std::lround(boxWidth * dpr / 2)), h = int(std::lround(TRIGGER_MARK_H * dpr));
	const int depth = int(std::lround(TRIGGER_POINT * dpr)), half = int(std::lround(TRIGGER_POINT_W / 2 * dpr));
	const int pen = markPen(dpr, lit);
	const bool side = pointer == MarkPointer::Right || pointer == MarkPointer::Left;
	const int left = pointer == MarkPointer::Left ? depth : 0;
	QImage image(QSize(w + (side ? depth : 0), h + (pointer == MarkPointer::Down ? depth : 0)),
			QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter p(&image);
		p.setRenderHint(QPainter::Antialiasing);
		const double edge = pen / 2.0, radius = 4 * dpr;
		QPainterPath outline;
		outline.addRoundedRect(QRectF(left, 0, w, h).adjusted(edge, edge, -edge, -edge), radius, radius);
		/* the triangle's base inside the border, so the union draws no line across it */
		const double tip = std::clamp(at, double(half + pen), double(h - half - pen));
		QPolygonF triangle;
		if (pointer == MarkPointer::Down)
			triangle << QPointF(w / 2.0 - half, h - pen) << QPointF(w / 2.0 + half, h - pen) << QPointF(w / 2.0, h + depth - edge);
		else if (pointer == MarkPointer::Right)
			triangle << QPointF(w - pen, tip - half) << QPointF(w - pen, tip + half) << QPointF(w + depth - edge, tip);
		else if (pointer == MarkPointer::Left)
			triangle << QPointF(depth + pen, tip - half) << QPointF(depth + pen, tip + half) << QPointF(edge, tip);
		QPainterPath pointed;
		pointed.addPolygon(triangle);
		pointed.closeSubpath();
		if (!triangle.isEmpty()) outline = outline.united(pointed);
		p.fillPath(outline, lit ? c.border : c.surface2);
		if (!triangle.isEmpty() && solid) p.fillPath(pointed, color);
		p.strokePath(outline, QPen(color, pen, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
	}
	image.setDevicePixelRatio(dpr);
	box = QRectF(left / dpr, 0, w / dpr, h / dpr);
	return image;
}

/* a level beyond its lane's range: ▲ or ▼ in the box where the pointer was (it points at no level), solid or hollow as
 * the pointer */
void drawOffScale(QPainter &p, QPointF centre, int offScale, bool solid, const QColor &color, const QColor &fill) {
	const double s = offScale > 0 ? -1 : 1;
	const QPointF triangle[3] = { { centre.x(), centre.y() + s * TRIGGER_POINT / 2 },
			{ centre.x() - 4, centre.y() - s * TRIGGER_POINT / 2 }, { centre.x() + 4, centre.y() - s * TRIGGER_POINT / 2 } };
	p.setPen(QPen(color, 1));
	p.setBrush(solid ? color : fill);
	p.drawPolygon(triangle, 3);
}
} // namespace

/* The level's marks outside the plot and outside the card's layer (the CPU draws them on both paths, from the rects
 * the frame's geometry gave): the "T▸" marker left of the plot and the tab right of it, each cut to its lane's part in
 * view, their points at the level's height, and the level's dashed line carried on to their points across the gaps */
void ChartView::drawTriggerTab(QPainter &p) const {
	if (!std::isfinite(triggerLineY_) || (triggerLevelTag_.isEmpty() && triggerLevelMark_.isEmpty())) return;
	const qreal dpr = p.device()->devicePixelRatioF();
	/* both pictures on whole device pixels, their boxes in the middle of their grab areas */
	const double top = std::round((triggerTabFull_.top() + (TRIGGER_TAB_H - TRIGGER_MARK_H) / 2) * dpr);
	const double at = triggerLineY_ * dpr - top;
	p.save();
	QPen pen(series_.value(trigger_.key).color, 1.2, triggerOffScale_ != 0 ? Qt::DotLine : Qt::DashLine);
	if (triggerOffScale_ != 0) pen.setCapStyle(Qt::FlatCap);
	if (!triggerLevelMark_.isEmpty()) {
		const QImage &mark = levelMarkPicture(dpr, at);
		const double right = std::round(triggerMarkFull_.right() * dpr);
		const QPointF corner((right - mark.width()) / dpr, top / dpr);
		p.setPen(pen);
		p.drawLine(QPointF(right / dpr, triggerLineY_), QPointF(triggerLane_.left(), triggerLineY_));
		p.setClipRect(triggerLevelMark_); /* a lane scrolled: the marker cut as the lane is */
		p.drawImage(corner, mark);
		p.setClipping(false);
	}
	if (!triggerLevelTag_.isEmpty()) {
		const double left = std::round(triggerTabFull_.left() * dpr);
		p.setPen(pen);
		p.drawLine(QPointF(triggerLane_.right(), triggerLineY_), QPointF(left / dpr, triggerLineY_));
		p.setClipRect(triggerLevelTag_);
		p.drawImage(QPointF(left / dpr, top / dpr), levelTagPicture(dpr, at));
	}
	p.restore();
}

/* The level's marker left of the plot: "T▸", a T in the family's box, its point at the line's height touching the
 * card's layer where the dashed line starts; lit with the tab (they move the one level). Solid while the view holds a
 * crossing of this level, hollow after a change of it; off scale ▲ or ▼ in the box */
const QImage &ChartView::levelMarkPicture(qreal dpr, double at) const {
	const QColor color = series_.value(trigger_.key).color;
	const bool solid = triggerHandleSolid(), lit = hoverLevel_ || drag_ == Drag::Level, off = triggerOffScale_ != 0;
	const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7").arg(dpr).arg(color.name()).arg(Theme::isDark()).arg(lit)
			.arg(solid).arg(triggerOffScale_).arg(at, 0, 'f', 2);
	if (key == levelMarkKey_) return levelMarkImage_;
	levelMarkKey_ = key;
	QRectF box;
	/* off scale the box takes its point's room: as wide as the marker always is */
	levelMarkImage_ = markPicture(dpr, color, off ? TRIGGER_LEFT_W : TRIGGER_MARK_W, off ? MarkPointer::None
			: MarkPointer::Right, at, solid, lit, box);
	const ThemeColors &c = Theme::colors();
	QPainter p(&levelMarkImage_);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(c.text);
	p.setFont(labelFont());
	p.drawText(QRectF(box.left(), box.top(), off ? 12 : TRIGGER_MARK_W, box.height()), Qt::AlignCenter, QStringLiteral("T"));
	if (off) drawOffScale(p, QPointF(box.right() - 6, box.center().y()), triggerOffScale_, solid, color, lit ? c.border : c.surface2);
	return levelMarkImage_;
}

/* the flag: a T in the family's box over its pointer, solid (it marks a place, not a level), lit under the mouse */
const QImage &ChartView::flagPicture(qreal dpr) const {
	const QColor color = series_.value(trigger_.key).color;
	const bool lit = hoverMark_ || drag_ == Drag::Position;
	const QString key = QStringLiteral("%1|%2|%3|%4").arg(dpr).arg(color.name()).arg(Theme::isDark()).arg(lit);
	if (key == triggerImageKey_) return triggerImage_;
	triggerImageKey_ = key;
	QRectF box;
	triggerImage_ = markPicture(dpr, color, TRIGGER_MARK_W, MarkPointer::Down, 0, true, lit, box);
	QPainter p(&triggerImage_);
	p.setPen(Theme::colors().text);
	p.setFont(labelFont());
	p.drawText(box, Qt::AlignCenter, QStringLiteral("T"));
	return triggerImage_;
}

QString ChartView::triggerPointText() const {
	const auto it = series_.constFind(trigger_.key);
	if (it == series_.constEnd() || !std::isfinite(trigger_.at)) return QString();
	const QString value = it->unit.isEmpty() ? levelText(trigger_.atLevel)
			: levelText(trigger_.atLevel) + QLatin1Char(' ') + it->unit;
	const QString edge = trigger_.atEdge == TriggerEdge::Rising ? tr("rising")
			: trigger_.atEdge == TriggerEdge::Falling ? tr("falling") : tr("rising or falling");
	const QString time = QDateTime::fromMSecsSinceEpoch(epochMs_ + qint64(std::llround(trigger_.at * 1000)))
			.toString(QStringLiteral("HH:mm:ss.zzz"));
	return tr("Trigger point: %1 crossed %2, %3, at %4", "the T's tooltip: the line, the level with its unit, the edge, "
			"the time").arg(it->name, value, edge, time);
}

bool ChartView::triggerHandleSolid() const {
	return trigger_.on && std::isfinite(trigger_.at) && watchedSettings().level == trigger_.atLevel;
}

QString ChartView::levelText(double level) {
	return QString::number(level, 'g', 6);
}

/* The crossing's place in the window: a flag in the strip above the plot, right over where the line crossed (with no
 * crossing in view, the place in the window): a T in the family's box over its pointer, the point on the card's
 * layer's top (2 px over the plot, so the layer hides none of it) at that x. A "T ▾" side by side did not point at
 * its place (the owner: the T over the arrow); a bare triangle between the plot and the time labels was not seen as a
 * control. The whole takes the mouse and is lit under it. On whole device pixels, its middle on the crossing's */
void ChartView::drawTriggerMark(QPainter &p, const Axes &axes) const {
	triggerMark_ = QRectF();
	if (!triggerMarked()) return;
	const qreal dpr = p.device()->devicePixelRatioF();
	const double x = !triggerTag_.isEmpty() ? triggerTag_.center().x() : axes.rect.left() + triggerPosition_ * axes.rect.width();
	const QImage &flag = flagPicture(dpr);
	const QSizeF size = flag.deviceIndependentSize();
	const double left = (std::round(x * dpr) - flag.width() / 2) / dpr; /* its width is even: the point on a pixel's edge */
	const double top = std::round((axes.rect.top() - 2) * dpr) / dpr - size.height();
	triggerMark_ = QRectF(QPointF(left, top), size);
	p.drawImage(triggerMark_.topLeft(), flag);
}

/* the level in words, the tab's tooltip: the line, the level in its unit and the edge ("(above range)" off scale) */
QString ChartView::triggerTagLabel() const {
	const auto it = series_.constFind(trigger_.key);
	if (it == series_.constEnd()) return QString();
	const TriggerSettings watched = watchedSettings();
	const QString value = it->unit.isEmpty() ? levelText(watched.level) : levelText(watched.level) + QLatin1Char(' ') + it->unit;
	const QString edge = watched.edge == TriggerEdge::Rising ? tr("rising")
			: watched.edge == TriggerEdge::Falling ? tr("falling") : tr("either");
	const QString label = tr("%1 %2, %3", "the trigger's level tag: the line, its level with its unit, the edge")
			.arg(it->name, value, edge);
	return triggerOffScale_ > 0 ? tr("▲ %1 (above range)", "the level's tag, the level above its lane's range").arg(label)
			: triggerOffScale_ < 0 ? tr("▼ %1 (below range)", "the level's tag, the level below its lane's range").arg(label)
			: label;
}

/* the level as one helper writes it (levelText: as set) in the line's unit: a number and a symbol, not words (the
 * marker left of the plot carries the T) */
QString ChartView::triggerTabLabel() const {
	const auto it = series_.constFind(trigger_.key);
	if (it == series_.constEnd()) return QString();
	const QString value = levelText(watchedSettings().level);
	return it->unit.isEmpty() ? value : value + QLatin1Char(' ') + it->unit;
}

QString ChartView::edgeSymbol() const {
	const TriggerEdge edge = watchedSettings().edge;
	return edge == TriggerEdge::Rising ? QStringLiteral("↑") : edge == TriggerEdge::Falling ? QStringLiteral("↓")
			: QStringLiteral("↕");
}

/* The level's tab: "0.4 A" and the edge (↑ ↓ ↕) in one shape of the family, its point at the line's height its left
 * side: the level in the middle of its part, the edge's part at the right end behind a thin divider, lit more under
 * the mouse as a button (the owner: no box in a box). The whole lit under the mouse and while dragged; the point solid
 * while the view holds a crossing of this level and hollow after a change of it (as a scope's level mark), off scale
 * ▲ or ▼ in the box. The same picture whichever draws the plot */
const QImage &ChartView::levelTagPicture(qreal dpr, double at) const {
	const ThemeColors &c = Theme::colors();
	const QColor color = series_.value(trigger_.key).color;
	const bool solid = triggerHandleSolid(), lit = hoverLevel_ || drag_ == Drag::Level, off = triggerOffScale_ != 0;
	const QString label = triggerTabLabel();
	const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9").arg(dpr).arg(color.name()).arg(label, edgeSymbol())
			.arg(hoverEdge_).arg(Theme::isDark()).arg(lit).arg(solid).arg(triggerOffScale_)
			+ QStringLiteral("|%1").arg(at, 0, 'f', 2);
	if (key == levelTagKey_) return levelTagImage_;
	levelTagKey_ = key;
	QRectF box;
	levelTagImage_ = markPicture(dpr, color, off ? TRIGGER_TAB_W : TRIGGER_TAB_W - TRIGGER_POINT, off ? MarkPointer::None
			: MarkPointer::Left, at, solid, lit, box);
	QPainter p(&levelTagImage_);
	p.setRenderHint(QPainter::Antialiasing);
	const double pen = markPen(dpr, lit) / dpr;
	/* the divider on whole device pixels, one device pixel per logical one */
	const int thin = std::max(1, int(std::lround(dpr)));
	const double divider = (std::round((box.right() - TRIGGER_TAB_BUTTON) * dpr) + (thin % 2 ? 0.5 : 0)) / dpr;
	const QRectF button(divider, box.top(), box.right() - divider, box.height());
	if (hoverEdge_) {
		QPainterPath inside, part;
		inside.addRoundedRect(box.adjusted(pen, pen, -pen, -pen), 4 - pen / 2, 4 - pen / 2);
		part.addRect(button);
		QColor strong = c.text;
		strong.setAlphaF(0.22);
		p.fillPath(inside.intersected(part), strong);
	}
	QColor line = color;
	line.setAlphaF(0.6);
	p.setPen(QPen(line, thin / dpr));
	p.drawLine(QPointF(divider, box.top() + pen + 2), QPointF(divider, box.bottom() - pen - 2));
	if (off) drawOffScale(p, QPointF(box.left() + 8, box.center().y()), triggerOffScale_, solid, color, lit ? c.border : c.surface2);
	p.setPen(c.text);
	p.setFont(labelFont());
	const QRectF text(box.left() + (off ? 14 : 4), box.top(), divider - box.left() - (off ? 14 : 4) - 4, box.height());
	p.drawText(text, Qt::AlignCenter, QFontMetricsF(labelFont()).elidedText(label, Qt::ElideRight, text.width() + 1));
	/* the edge drawn as lines, not as the font's arrow: a font's ↑ ↓ ↕ sits off the middle of its own box (the owner
	 * saw it right of the centre), so the stem and the heads are placed on the button's centre */
	const QPointF mid = button.center();
	const double half = std::min(button.height() / 2 - pen - 3, 6.5), head = 3;
	const TriggerEdge edge = watchedSettings().edge;
	QPainterPath arrow;
	arrow.moveTo(mid.x(), mid.y() - half);
	arrow.lineTo(mid.x(), mid.y() + half);
	if (edge != TriggerEdge::Falling) {
		arrow.moveTo(mid.x() - head, mid.y() - half + head);
		arrow.lineTo(mid.x(), mid.y() - half);
		arrow.lineTo(mid.x() + head, mid.y() - half + head);
	}
	if (edge != TriggerEdge::Rising) {
		arrow.moveTo(mid.x() - head, mid.y() + half - head);
		arrow.lineTo(mid.x(), mid.y() + half);
		arrow.lineTo(mid.x() + head, mid.y() + half - head);
	}
	p.setPen(QPen(c.text, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	p.drawPath(arrow);
	return levelTagImage_;
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
	/* a fast line's records kept as summaries only: that part of the memory shaded, so where its samples begin shows */
	double since = 0;
	const bool tiers = strip.t1 > strip.t0 && summariesBefore(since);
	if (tiers) {
		QColor shade = c.border;
		shade.setAlpha(150);
		double k0, k1; /* from the oldest kept */
		memorySpan(k0, k1);
		const double x0 = std::clamp(strip.x(k0), box.left() + 1, box.right() - 1);
		const double x1 = std::clamp(strip.x(since), x0, box.right() - 1);
		p.fillRect(QRectF(QPointF(x0, box.top() + 1), QPointF(x1, box.bottom() - 1)), shade);
		p.fillRect(QRectF(x1 - 0.5, box.top() + 1, 1, box.height() - 2), c.muted); /* where the samples begin */
	}
	if (strip.t1 > strip.t0) {
		/* the lines: drawn again when the data has moved a pixel on the strip but at most once a second (its lines
		 * bin the whole memory: 15 times a second for a minute's strip, a frame's worth each), or when the lines or
		 * its size changed */
		const QRect device = p.deviceTransform().mapRect(box).toAlignedRect();
		if (stripImage_.size() != device.size() || stripImage_.devicePixelRatio() != p.device()->devicePixelRatioF()
				|| stripGeneration_ != seriesGeneration_ || stripMemory_ != memory_ || stripLog_ != logShown() || strip.t1 < stripEnd_
				|| strip.t1 - stripEnd_ >= std::max(strip.columnSeconds(), STRIP_REDRAW_S)) {
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
		/* the view on it; at a short window (10 ms of an hour) the box is a sliver no mouse can take, so a handle
		 * MEMORY_HANDLE_W wide is drawn over it, centred on the view, with two grip lines: it drags as the box does.
		 * The mouse over either lights it */
		const double va = std::max(strip.x(axes.t0), box.left()), vb = std::min(strip.x(axes.t1), box.right());
		const QRectF view(va, box.top() + 1, std::max(3.0, vb - va), box.height() - 2);
		memoryViewX_ = strip.x((axes.t0 + axes.t1) / 2);
		QColor fill = c.accent;
		fill.setAlpha(hoverMemoryHandle_ ? 90 : 45);
		p.setPen(QPen(c.accent, 1.2));
		p.setBrush(fill);
		p.drawRoundedRect(view, 3, 3);
		memoryHandle_ = view;
		if (view.width() < MEMORY_HANDLE_W) {
			const double left = std::clamp(memoryViewX_ - MEMORY_HANDLE_W / 2, box.left(), box.right() - MEMORY_HANDLE_W);
			memoryHandle_ = QRectF(left, view.top(), MEMORY_HANDLE_W, view.height());
			fill.setAlpha(hoverMemoryHandle_ ? 150 : 90);
			p.setBrush(fill);
			p.drawRoundedRect(memoryHandle_, 3, 3);
			p.setPen(QPen(hoverMemoryHandle_ ? c.text : c.surface2, 1));
			const double mid = memoryHandle_.center().x(), y0 = memoryHandle_.center().y() - 5, y1 = y0 + 10;
			p.drawLine(QPointF(mid - 1.5, y0), QPointF(mid - 1.5, y1));
			p.drawLine(QPointF(mid + 1.5, y0), QPointF(mid + 1.5, y1));
		}
		/* still filling: how much is kept, in the empty part when there is room. The RAM budget reached: in the warn
		 * colour and in words that say what it is, the chart letting the oldest go while a recording's file keeps them
		 * all ("memory full" while recording read as data lost); the longest words that fit, whole */
		double k0, k1;
		memorySpan(k0, k1);
		const double emptyW = strip.x(k0) - box.left();
		stripText_.clear();
		if (k1 - k0 < memory_ * 0.98 && emptyW > 150) {
			const QString kept = formatDuration(k1 - k0), memory = formatDuration(memory_);
			QStringList texts;
			if (!capped_) {
				if (tiers)
					texts << tr("filling: %1 of %2 kept (samples for the newest %3)")
									 .arg(kept, memory, formatDuration(std::max(0.0, clockNow() - since)));
				texts << tr("filling: %1 of %2 kept").arg(kept, memory);
			} else {
				if (recordingOn_)
					texts << tr("RAM budget reached: keeping the last %1 of %2 · the recording keeps everything")
									 .arg(kept, memory);
				texts << tr("RAM budget reached: keeping the last %1 of %2").arg(kept, memory) << tr("RAM budget reached");
			}
			p.setFont(smallFont());
			const QFontMetricsF metrics(p.font());
			for (const QString &text : std::as_const(texts)) {
				if (metrics.horizontalAdvance(text) > emptyW - 16) continue;
				stripText_ = text;
				break;
			}
			stripTextColor_ = capped_ ? c.warn : c.muted;
			p.setPen(stripTextColor_);
			p.drawText(QRectF(box.left() + 8, box.top(), emptyW - 16, box.height()), Qt::AlignLeft | Qt::AlignVCenter,
					stripText_);
		} else if (tiers && !capped_) {
			/* full, older samples as summaries: the words on a label of their own (the lines under it covered, not
			 * written over), at the end of the strip the view's box is not at */
			const QString newest = formatDuration(std::max(0.0, clockNow() - since));
			const QStringList texts{ tr("keeps %1 (samples for the newest %2)").arg(formatDuration(k1 - k0), newest),
				tr("samples for the newest %1").arg(newest) };
			p.setFont(smallFont());
			const QFontMetricsF metrics(p.font());
			for (const QString &text : texts) {
				const double w = std::ceil(metrics.horizontalAdvance(text)) + 14;
				if (w > box.width() * 0.45) continue;
				QRectF label(box.left() + 6, box.top() + 3, w, box.height() - 6);
				if (label.adjusted(-6, 0, 6, 0).intersects(memoryHandle_)) label.moveRight(box.right() - 6);
				if (label.adjusted(-6, 0, 6, 0).intersects(memoryHandle_)) break;
				p.setPen(QPen(c.border, 1));
				p.setBrush(c.surface2);
				p.drawRoundedRect(label, 4, 4);
				stripText_ = text;
				stripTextColor_ = c.muted;
				p.setPen(stripTextColor_);
				p.drawText(label, Qt::AlignCenter, text);
				break;
			}
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
		QVector<qsizetype> breaks; /* a fast line's gaps */
		const QPolygonF poly = toPolyline(binned.bins, strip.columnSeconds(), x, y, nullptr, 0, 0, &breaks);
		strokePieces(p, poly, breaks, 0, poly.size(), color, true, 1);
	}
}

/* The legend: a chip per line across the top with its latest value (taken at
 * the values' pace by frame(); a recording's, the latest in the view), each at a fixed place beside the state. Chips
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
	QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9").arg(seriesGeneration_).arg(valuesTick_).arg(offset)
			.arg(area.width()).arg(area.left()).arg(dpr).arg(Theme::isDark()).arg(hoverChip_).arg(fastStoppedGen_);
	/* a recording's values are the view's: made again when the view moves */
	if (recording_) key += QLatin1Char('|') + QString::number(axes.t0, 'g', 17) + QLatin1Char('|')
			+ QString::number(axes.t1, 'g', 17);
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
		for (auto it = series_.constBegin(); it != series_.constEnd(); ++it) {
			const QRectF chip = legend.chips[i++].translated(-offset, 0);
			if (chip.right() >= legend.viewport.left() && chip.left() <= legend.viewport.right())
				drawChip(lp, *it, chip, legend.valueRoom, it.key() == hoverChip_, lineStopped(it.key()), axes.t0, axes.t1);
		}
		lp.setClipping(false);
		if (legend.maxScroll() > 0) drawLegendBar(lp, legend, offset);
	}
	p.drawImage(area.topLeft(), legendImage_);
}

/* one chip: the line's dot and name on the left; its value right-aligned in
 * the room every value gets, then the unit, so only the digits change; at the right end its menu button ("▾", the
 * lanes' fold button's shape), stronger while the mouse is on the chip: the whole chip opens the menu. A stopped
 * stream's line: its value (the last record's) greyed, as nothing comes after it */
void ChartView::drawChip(QPainter &p, const Series &s, const QRectF &chip, double valueRoom, bool hovered,
		bool stopped, double t0, double t1) const {
	const ThemeColors &c = Theme::colors();
	p.setPen(Qt::NoPen);
	p.setBrush(c.surface2);
	p.drawRoundedRect(chip, LEGEND_ROW_H / 2, LEGEND_ROW_H / 2);
	p.setBrush(s.color);
	p.drawEllipse(QPointF(chip.left() + 11, chip.center().y()), 4, 4);
	const QRectF button(chip.right() - CHIP_BUTTON_W - 3, chip.center().y() - 8, CHIP_BUTTON_W, 16);
	const double cx = button.center().x(), cy = button.center().y();
	const QPointF open[3] = { { cx - 4, cy - 2 }, { cx + 4, cy - 2 }, { cx, cy + 3 } };
	p.setBrush(hovered ? c.border : c.surface);
	p.drawRoundedRect(button, 6, 6);
	p.setBrush(hovered ? c.text : c.muted);
	p.drawPolygon(open, 3);
	p.setPen(c.text);
	const QRectF text = chip.adjusted(CHIP_TEXT_LEFT, 0, -CHIP_PAD_RIGHT - CHIP_BUTTON_W, 0);
	p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, s.name);
	double shown = 0;
	if (!chipValue(s, t0, t1, shown)) return;
	if (stopped) p.setPen(c.muted);
	const QString unit = s.unit.isEmpty() ? QString() : QLatin1Char(' ') + s.unit;
	const double unitW = QFontMetricsF(p.font()).horizontalAdvance(unit);
	const QRectF value(text.right() - unitW - valueRoom, chip.top(), valueRoom, chip.height());
	p.drawText(value, Qt::AlignVCenter | Qt::AlignRight, chartNumber(shown));
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
		double sampleTime, v;
		if (s.fast) { /* a fast line: the record nearest the mouse */
			const fast::Store &store = *s.fast;
			if (store.size() == 0 || !store.hasTime()) continue;
			qsizetype k = std::min(store.lowerBound(t), store.size() - 1);
			if (k > 0 && std::fabs(store.timeAt(k - 1) - t) < std::fabs(store.timeAt(k) - t)) k--;
			sampleTime = store.timeAt(k);
			v = store.value(s.channel, k);
			if (k < store.recordsFrom()) { /* kept as its summary: the range of the 256 records it lies in, no dot */
				double lo, hi;
				store.minMax(s.channel, k, k + 1, lo, hi);
				if (std::fabs(sampleTime - t) <= window_ / READOUT_REACH && remake && hoverValues_)
					rows.push_back({ s.name, chartNumber(lo) + QStringLiteral(" … ") + chartNumber(hi), s.unit, s.color });
				continue;
			}
		} else {
			if (s.times.isEmpty()) continue;
			const qsizetype k = nearestIndex(s.times, t);
			sampleTime = s.times[k];
			v = s.values[k];
		}
		if (std::fabs(sampleTime - t) > window_ / READOUT_REACH) continue;
		if (remake && hoverValues_) rows.push_back({ s.name, chartNumber(v), s.unit, s.color });
		double lo = line.lo, hi = line.hi; /* Normalize: the line's own range, as drawLines scales it */
		widenFlatRange(lo, hi);
		const double y = normalized_ ? lineAxes.y((v - lo) / (hi - lo)) : lineAxes.y(v);
		if (lanes_ && (y < plot.top() - 1 || y > plot.bottom() + 1)) continue; /* scrolled away: in the box, no dot */
		out.dots.push_back({ QPointF(axes.x(sampleTime), y), s.color });
	}
	if (remake) {
		/* a view under 10 ms (fast lines): the microseconds too. Held on a trigger's crossing, beside the clock time how
		 * far it is from T (U-7: a scope reads times from its trigger point), not how long ago */
		const QString clock = timeLabel(epochMs_, t, window_ < 0.01 ? 1e-6 : 1e-3);
		const QString fromT = fromTText(t);
		const QString timeText = fromT.isEmpty() ? QStringLiteral("%1   -%2 s").arg(clock, chartNumber(clockNow() - t))
												 : QStringLiteral("%1   %2").arg(clock, fromT);
		readoutTime_ = timeText;
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
	/* the time row as wide as it can get, so it does not move the box either: how long ago, or from T (to the
	 * microsecond, three decimals in the window's unit) */
	const double timeW = std::max(metrics.horizontalAdvance(QStringLiteral("00:00:00.000   -%1 s").arg(widestChartNumber())),
			metrics.horizontalAdvance(QStringLiteral("00:00:00.000000   T -000.000 µs")));
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
 * the cursors' "click / drag", then "manual" of a manual Log Y, then the summaries' span ("summaries: samples for the
 * newest 79 s", a view reaching back into a fast line's records kept as summaries only, drawn as bars when zoomed
 * in), then a stopped stream's last record ("ADC stopped · last record 14:03:12.345" to "ADC stopped"); the time held,
 * the stopped streams' names and the trigger's state stay. With
 * measuring, the time held is written as the widest number, so the room kept for it does not change with its digits
 * (the legend's end would follow them). */
QStringList ChartView::stateVariants(bool measuring) const {
	QString held[2], summaries[2], y[2], cursors[2], trigger;
	const QString dot = QStringLiteral("  ·  ");
	/* a stopped stream's lines look live no longer: said first, muted */
	const QString stopped[2] = { stoppedParts(true).join(dot), stoppedParts(false).join(dot) };
	/* the trigger on: its state alone (a held time, "filling" or "Live to follow" read as a hold of the user's while the
	 * trigger holds the view, and the time held rewrote itself at every frame), after the streams stopped */
	if (triggerMarked()) {
		const QString state = triggerStateText();
		if (stopped[0].isEmpty()) return QStringList(4, state);
		return { stopped[0] + dot + state, stopped[1] + dot + state, stopped[1] + dot + state, stopped[1] + dot + state };
	}
	/* a short window's lock holds the view on its crossings, yet follows now: no "held" (its words come last) */
	if (!live_ && !recording_ && !trigger_.automatic) { /* a trigger holds a view that ends after now: it fills as the samples come */
		const double behind = clockNow() - viewEnd();
		const QString number = measuring ? QStringLiteral("0.000e+00") : chartNumber(std::fabs(behind));
		held[0] = behind >= 0 ? tr("held: -%1 s · Live to follow").arg(number)
				: tr("held: filling, %1 s to come · Live to follow").arg(number);
		held[1] = behind >= 0 ? tr("held: -%1 s").arg(number) : tr("held: filling");
	}
	double since = 0;
	if (summariesBefore(since) && viewEnd() - window_ < since) {
		summaries[0] = tr("summaries: samples for the newest %1").arg(formatDuration(std::max(0.0, clockNow() - since)));
		summaries[1] = tr("summaries");
	}
	if (lanes_) {
		/* each lane its own Y range; their buttons show the fold */
	} else if (logShown()) {
		y[0] = y_.autoRange ? tr("Y log") : tr("Y log, manual");
		y[1] = tr("Y log");
	} else if (!y_.autoRange && !normalized_) {
		y[0] = y[1] = tr("Y manual");
	}
	if (cursorMode_) { /* on the grid Shift's snap is said too (U-19) */
		cursors[0] = cursorsOnGrid() ? tr("cursors: click / drag, Shift snaps") : tr("cursors: click / drag");
		cursors[1] = tr("cursors");
	}
	trigger = triggerStateText();
	QStringList variants;
	for (int stage = 0; stage < 6; stage++) {
		QStringList parts{ held[stage >= 1], stopped[stage >= 5], summaries[stage >= 4], y[stage >= 3], cursors[stage >= 2],
			trigger };
		parts.removeAll(QString());
		variants << parts.join(dot);
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
	/* the time/div readout keeps its place left of the state, inside the same share: the state's parts go first */
	const double readout = divisionReadoutWidth();
	const double readoutRoom = readout > 0 ? readout + DIVISION_GAP : 0;
	const double legendMin = (chipWidths_.isEmpty() ? 0 : chipWidths_.first()) + 2 * LEGEND_ARROW_W + STATE_GAP;
	const double most = std::max(0.0, plotWidth - legendMin - readoutRoom);
	const double room = std::min(plotWidth * STATE_SHARE - readoutRoom, most);
	for (qsizetype i = 0; i < variants.size(); i++) {
		width = std::ceil(stateWidth(variants[i]));
		variant = int(i);
		if (width <= room) return;
	}
	if (width <= most) return; /* the shortest, whole, past its share */
	width = most;              /* a chart too narrow even for that: it ends with "…" (drawState) */
}

double ChartView::stateWidth(const QString &text) const {
	const QFontMetricsF metrics(labelFont());
	const QString badge = trigger_.automatic ? triggerStateText() : QString();
	if (badge.isEmpty() || !text.endsWith(badge)) return metrics.horizontalAdvance(text);
	QString rest = text.chopped(badge.size());
	if (rest.endsWith(QStringLiteral("  ·  "))) rest.chop(5);
	return (rest.isEmpty() ? 0 : metrics.horizontalAdvance(rest) + BADGE_GAP) + metrics.horizontalAdvance(badge)
			+ 2 * BADGE_PAD;
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
	stateBadge_ = QRectF();
	const ThemeColors &c = Theme::colors();
	/* the time/div readout (made with the time labels): in this row, left of the state, alone at the right end without
	 * one; one left-to-right piece in Arabic too */
	const double readout = divisionReadoutWidth();
	divisionRect_ = QRectF();
	if (readout > 0 && !divisionText_.isEmpty()) {
		const double right = variant >= 0 ? axes.rect.right() - width - DIVISION_GAP : axes.rect.right();
		divisionRect_ = QRectF(std::round(right - readout), LEGEND_TOP + 2, readout, LEGEND_ROW_H - 4);
		p.save();
		p.setFont(labelFont());
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(QPen(c.border, 1));
		p.setBrush(c.surface2);
		p.drawRoundedRect(divisionRect_.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
		p.setPen(c.text);
		p.drawText(divisionRect_, Qt::AlignCenter, divisionText_);
		p.restore();
	}
	if (variant < 0) return;
	const QFontMetricsF metrics(labelFont());
	stateText_ = texts[variant];
	const bool whole = stateWidth(stateText_) <= width + 0.5;
	if (metrics.horizontalAdvance(stateText_) > width) stateText_ = metrics.elidedText(stateText_, Qt::ElideRight, width);
	stateRect_ = QRectF(axes.rect.right() - width, LEGEND_TOP, width, LEGEND_ROW_H);
	p.save();
	p.setFont(labelFont());
	const bool rightToLeft = QGuiApplication::layoutDirection() == Qt::RightToLeft;
	/* the short window's lock: the view's state, not the user's trigger nor a stop; a badge in the accent colour (the
	 * Live button's), on a tint of it, at the end the words are read to (the right; in Arabic the left) */
	const QString badge = trigger_.automatic ? triggerStateText() : QString();
	QRectF textRoom = stateRect_;
	if (!badge.isEmpty() && whole && stateText_.endsWith(badge)) {
		const double badgeWidth = metrics.horizontalAdvance(badge) + 2 * BADGE_PAD;
		stateBadge_ = QRectF(rightToLeft ? stateRect_.left() : stateRect_.right() - badgeWidth, stateRect_.top() + 2,
				badgeWidth, stateRect_.height() - 4);
		QColor tint = c.accent;
		tint.setAlphaF(Theme::isDark() ? 0.22 : 0.14);
		p.save();
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(tint);
		p.drawRoundedRect(stateBadge_, 4, 4);
		p.setPen(c.accent);
		p.drawText(stateBadge_, Qt::AlignCenter, badge);
		p.restore();
		stateText_.chop(badge.size());
		if (stateText_.endsWith(QStringLiteral("  ·  "))) stateText_.chop(5);
		textRoom = rightToLeft ? stateRect_.adjusted(badgeWidth + BADGE_GAP, 0, 0, 0)
				: stateRect_.adjusted(0, 0, -(badgeWidth + BADGE_GAP), 0);
		if (stateText_.isEmpty()) {
			stateText_ = badge;
			p.restore();
			return;
		}
	}
	p.setPen((trigger_.on ? trigger_.stopped : !live_) ? c.warn : c.muted); /* the trigger on: amber for Stopped only */
	/* words, not the chart's time: read in the language's direction (Arabic from the right, its first part rightmost),
	 * still at the chart's right end; right to left, a mark either side of each dot keeps a part's Latin end ("s") and
	 * the next part's Latin start ("Y") from running together into one left-to-right run */
	p.setLayoutDirection(QGuiApplication::layoutDirection());
	const QString dot = QStringLiteral("  ·  ");
	p.drawText(textRoom, Qt::AlignRight | Qt::AlignAbsolute | Qt::AlignVCenter,
			rightToLeft ? QString(stateText_).replace(dot, QChar(0x200F) + dot + QChar(0x200F)) : stateText_);
	if (!stateBadge_.isEmpty()) stateText_ += dot + badge; /* tests read the words, the badge's too */
	p.restore();
}

/* the paint time as a running average; the frames of the last second */
void ChartView::updatePaintStats(double paintMs) {
	paintMs_ = paintMs_ * 0.9 + paintMs * 0.1;
	const qint64 now = fpsClock_.elapsed();
	paintTimes_.push_back(now);
	while (paintTimes_.front() < now - 1000) paintTimes_.pop_front();
}

double ChartView::fps() const {
	const qint64 since = fpsClock_.elapsed() - 1000;
	int frames = 0;
	for (auto it = paintTimes_.rbegin(); it != paintTimes_.rend() && *it >= since; ++it) frames++;
	return frames;
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

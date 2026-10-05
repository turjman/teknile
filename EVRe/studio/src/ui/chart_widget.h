/* SPDX-License-Identifier: Apache-2.0 */
/* A live strip chart, drawn by hand, that works like an oscilloscope:
 *
 *  - memory and view apart: it keeps the last `memory` seconds of every line
 *    and shows `window` of them. Live, the view follows now; drag the chart
 *    (or the overview strip under it) to look back through the memory, wheel
 *    to zoom the time around the mouse; Live follows now again.
 *  - the overview strip shows the whole memory, the view marked on it.
 *  - two cursors, A and B (Cursors on: click / drag them): the measurements
 *    (value at A and B, min, max, mean, RMS, the area under the line) cover
 *    A..B, or the view without cursors.
 *  - a legend with the latest values, a crosshair reading every line. Each
 *    chip of the legend has a fixed place and width (the value changes inside
 *    it, the chips never move); when they do not all fit, a scroll bar under
 *    them (or the wheel over them) brings the others in. The values change at
 *    the pace chosen for them (value_pace.h), the lines at every frame.
 *
 * Smooth at any poll rate: the right edge is read from the clock at every
 * frame, grid lines are fixed to wall-clock times and move with the data,
 * points sit at their exact (sub-pixel) times, a small measured delay keeps the
 * newest point at the edge, samples are binned per pixel column on absolute
 * time (no shimmer), and every 8, 64, 512 and 4096 samples are also kept as min/max
 * chunks, so a long view, or a fast line, costs what a short one does: the
 * work follows the pixels, not the samples.
 *
 * Many lines: the lines are binned on several threads, and drawn on several
 * threads too, the plot cut into vertical stripes, one image each. The memory
 * strip is kept as an image and drawn again when the data moved a pixel on it.
 * Frames that cost much skip a frame now and then, as many as it takes for the
 * chart to stay within about 60 % of the window's thread (FrameBudget), so the
 * window answers at any number of lines. The samples kept have a budget for
 * all the lines together (setRamBudget, in MB), which their arrays keep to: with
 * many fast lines the memory holds less than asked, and says so.
 *
 * Y axis: Auto (grows at once, shrinks gently) or Manual, on a linear or a
 * logarithmic scale (Log: decades, values <= 0 on the bottom edge). Ctrl +
 * wheel zooms Y around the mouse (Manual), a double-click goes back to Auto.
 *
 * Totals: every line's area since the chart's Clear, summed from each sample as
 * it comes (append), not from the memory, so a trim loses nothing.
 *
 * Notes: labelled markers at a time, a dashed line with a tag at the bottom of
 * the plot (drawn by the CPU, and as a picture on the card like the cursors'
 * tags); drag a tag to move it, double-click it to edit (noteEditRequested),
 * Delete removes the one clicked last. A right-click asks for the chart's menu
 * (menuRequested): the Chart tab makes it.
 *
 * Trigger: a line crossing a level (rising, falling or either), as an
 * oscilloscope's: the view holds with the crossing at 20 % of the window and a
 * marker there, the level a dashed line that can be dragged. Single holds on
 * the first crossing; Normal holds on each, armed again once its view is full.
 *
 * Fast on a 4K screen: a line is drawn as a few 1-device-pixel antialiased
 * cosmetic polylines side by side (Qt's fast path) instead of one wide
 * antialiased stroke (20+ ms a frame for four lines at 225 % scaling). OpenGL
 * was faster to draw, but with an OpenGL widget in the window Qt composes the
 * whole window on the GPU, and every label update then cost a frame or two.
 * With a graphics card (setDrawing), the plot is a layer of the window over
 * the chart, which the card draws and shows (gpu_lines.h): the chart then
 * paints only what is around the plot. A picture of the chart (grab()) is drawn
 * by the CPU, the plot too. */
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QImage>
#include <QRectF>
#include <QStringList>
#include <QThreadPool>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>

#include "model/recording_file.h"
#include "ui/gpu_lines.h"
#include "ui/value_pace.h"

class QPainter;
class QPolygonF;

/* a value axis label: every label of the axis with the decimals its step needs (14, 12 … 6; 0.2, 0.4), a
 * percentage when normalized */
QString chartAxisLabel(double value, double step, bool percent);
/* a value axis label on a logarithmic scale: 1, 10, 100, 1000, 10000 as written, smaller and larger with an SI
 * prefix (100 m, 1 µ, 100 k, 1 M) */
QString chartLogLabel(double value);

/* ChartView: the chart itself, a raster widget that paints every pixel at each display frame */
class ChartView : public QWidget {
	Q_OBJECT
public:
	static constexpr double MAX_SPAN = 86400; /* a day: the longest view and memory, seconds */

	explicit ChartView(QWidget *parent = nullptr);

	void addSeries(int key, const QString &name, const QString &unit, const QColor &color);
	void removeSeries(int key);
	void clearSeries();
	void clearData();
	void append(int key, double t, double v);

	/* the time base, seconds (read at every frame), and the wall-clock time of
	 * its zero (ms since the epoch) for the axis labels */
	void setClock(std::function<double()> clock, qint64 epochMsAtZero);
	void frame();                       /* once per display frame: repaint if shown */
	/* a change to show: painted by the next frame while frames come (frame(), at the display's refresh), at once
	 * otherwise; not the plot while the card shows it (its layer: the card presents it at every frame painted) */
	void refresh();
	void setValuesPerSecond(int perSecond); /* how often the legend's values change (value_pace.h) */

	void setWindow(double seconds);     /* the view; the memory grows to hold it */
	double window() const { return window_; }
	void setMemory(double seconds);     /* how much is kept */
	double memory() const { return memory_; }
	void setLive(bool on);              /* follow now, or hold the view where it is */
	bool live() const { return live_; }
	/* held on t0..t1 (a recording: its whole span), the memory grown to hold it */
	void showSpan(double t0, double t1);
	void showLastValues(); /* the legend's values now (no frames come: a recording's chart) */
	/* a recording's chart: nothing comes after its end, so the state says nothing of Live */
	void setRecording(bool on) { recording_ = on; }
	/* the view as last painted (Export to CSV: the view's samples) */
	void viewSpan(double &t0, double &t1) const {
		t1 = lastViewEnd_;
		t0 = t1 - window_;
	}
	void setNormalized(bool on) {
		normalized_ = on;
		forgetRanges();
		refresh();
	}
	void setSmooth(bool on);
	/* the crosshair's box of values (the line and its dots stay without it) */
	void setHoverValues(bool on);
	bool hoverValues() const { return hoverValues_; }
	void setYAuto();
	/* false: not a range (hi <= lo, or lo <= 0 on the Log scale), nothing changed */
	bool setYManual(double lo, double hi);
	bool yAuto() const { return y_.autoRange; }
	/* the logarithmic scale (Auto: the positive values in view, at most MAX_DECADES); it has no effect while
	 * normalised. Manual keeps its range when it is positive, else Auto */
	void setYLog(bool on);
	bool yLog() const { return y_.log; }
	static constexpr double MAX_DECADES = 9;

	/* The trigger (see the top). setTrigger arms it: only crossings after the line's newest sample count. */
	enum class TriggerEdge { Rising, Falling, Either };
	enum class TriggerMode { Single, Normal };
	static constexpr double TRIGGER_AT = 0.2; /* of the window, from its left */
	void setTrigger(int key, double level, TriggerEdge edge, TriggerMode mode);
	void stopTrigger();
	void armTrigger();                   /* waits for the next crossing (Single: once more) */
	void setTriggerLevel(double level);
	bool triggerOn() const { return trigger_.on; }
	bool triggerArmed() const { return trigger_.on && trigger_.armed; }
	double triggeredAt() const { return trigger_.at; } /* the last crossing; NaN: none since armed first */
	double triggerLevel() const { return trigger_.level; }
	int triggerKey() const { return trigger_.on ? trigger_.key : -1; }
	QRectF triggerTag() const { return triggerTag_; } /* tests: the marker as last drawn; empty: not in view */
	double triggerLineY() const { return triggerLineY_; } /* tests: the level's line as last drawn; NaN: none */

	/* Lanes: a plot per unit, stacked, of equal height, MAX_LANES at most (the units after share the last), on one
	 * time axis; the cursors, the A-B bar and the notes across them, one crosshair box. Each lane has its own Y range
	 * (kept by its unit), set by a right-click on its value labels (laneMenuRequested), Ctrl + wheel over it, and a
	 * double-click (Auto). The Y range above (setYAuto ...) is the plot's without lanes. */
	static constexpr int MAX_LANES = 8;
	void setLanes(bool on);
	bool lanes() const { return lanes_; }
	int laneCount() const;               /* the lanes now (0 without) */
	QString laneLabel(int lane) const;   /* its units: "V", or "W · Ω" for the last one shared */
	QRectF laneRect(int lane) const;
	QVector<int> laneLines(int lane) const; /* the keys of its lines */
	bool laneYAuto(int lane) const;
	bool laneYLog(int lane) const;
	double laneYLo(int lane) const;      /* as last painted */
	double laneYHi(int lane) const;
	void setLaneYAuto(int lane);
	bool setLaneYManual(int lane, double lo, double hi); /* false: not a range (lo <= 0 on the Log scale) */
	void setLaneYLog(int lane, bool on);
	/* the lanes' ranges for the settings, one text per unit ("unit\tauto\tlog\tlo\thi"), and back */
	QStringList laneScales() const;
	void setLaneScales(const QStringList &texts);
	double laneYOfValue(int lane, double value) const; /* tests: where a value lies in a lane, as last painted */
	double yLo() const { return y_.lo; }  /* the range shown now */
	double yHi() const { return y_.hi; }

	/* cursors: with cursor mode on, a click places / drags the nearest */
	void setCursorMode(bool on) { cursorMode_ = on; refresh(); }
	void clearCursors();
	void setCursors(double a, double b);     /* times; NaN: none */
	double cursorA() const { return cursorA_; } /* NaN: not set */
	double cursorB() const { return cursorB_; }
	double timeNow() const { return clockNow(); } /* the time base now (for tests) */

	/* the legend (for tests): each line's chip where it is drawn now, the part
	 * of the row the chips show in, and the scroll in pixels (kept within what
	 * the chips need: 0 when they all fit) */
	QVector<QRectF> legendChips() const;
	QRectF legendViewport() const;
	double legendScroll() const;
	void setLegendScroll(double pixels);
	QString legendValue(int key) const; /* the value a line's chip shows now; empty: none yet */

	/* measurements of one line over A..B, or over the view */
	struct Stats {
		bool ok = false;
		/* integral: unit x s; std: the standard deviation, time-weighted as the mean; p2p: max - min */
		double atA = NAN, atB = NAN, min = 0, max = 0, mean = 0, rms = 0, std = 0, p2p = 0, integral = 0;
		double total = NAN; /* the area since Clear (totalsSince), unit x s; NaN: no sample yet */
		int n = 0;
	};
	struct Info {
		int key;
		QString name, unit;
		QColor color;
	};
	QVector<Info> lines() const;
	void range(double &t0, double &t1, bool &cursors) const; /* what the measurements cover */
	Stats stats(int key) const;
	/* several lines at once, on the chart's threads; cursorsOnly: the values at A and B alone (ok false) */
	QVector<Stats> stats(const QVector<int> &keys, bool cursorsOnly = false) const;
	bool draggingCursor() const { return drag_ == Drag::CurA || drag_ == Drag::CurB; }
	/* The totals (Stats::total): each line's value x dt by trapezoids from every sample as it is appended, a gap over
	 * TOTAL_GAP not bridged; reset by clearData (the chart's Clear) and a new set of lines (clearSeries) only: a line
	 * taken off and put back keeps its total. totalsSince: the time base of the first sample since then, NaN none. */
	static constexpr double TOTAL_GAP = 1.0;
	double total(int key) const;
	double totalsSince() const { return totalsSince_; }
	qint64 epochMs() const { return epochMs_; } /* the wall-clock time of the time base's zero, ms since the epoch */

	/* for the status line: frames per second, average paint time, delay */
	double fps() const { return fps_; }
	double paintMs() const { return paintMs_; }
	double delayMs() const { return delay_ * 1000.0; }
	/* The memory the samples may take, all the lines together, in MB (RAM on the Chart tab): with many fast lines
	 * the memory holds less than asked, and the strip says so. */
	static constexpr int DEFAULT_RAM_MB = 2048;
	/* a sample's memory: its time and value (16 bytes) and its share of the chunks (48 bytes per 8, per 64, ...) */
	static constexpr int BYTES_PER_SAMPLE = 23;
	static constexpr int MIN_RAM_MB = 256;
	void setRamBudget(int megabytes);
	int ramBudget() const { return ramMB_; }
	/* the most samples a line keeps now: the RAM shared by the lines, at least 16 of the largest chunks, at most
	 * MAX_POINTS */
	qsizetype pointsPerLine() const;
	qsizetype pointsKept(int key) const;     /* the samples a line keeps now */
	/* the memory the Memory set would take at the rates the lines' samples come now, bytes (0: no line has two
	 * samples yet) */
	qint64 bytesNeeded() const;
	bool memoryFull() const { return capped_; } /* the budget, not the memory, limits what is kept */
	/* tests: the memory the lines' arrays hold now, bytes (their room, not only the samples in it) */
	qint64 bytesHeld() const;
	void setDrawThreads(int threads) { drawThreads_ = threads; } /* for tests: 1 = this thread alone; 0 = all */
	/* the crosshair's box: values a column holds in a plot this tall (more lines: more columns, never past the plot) */
	static int readoutRowsPerColumn(double plotHeight);
	/* The frame budget, a credit of the window thread's time: each frame() earns FRAME_SHARE of the time since the
	 * last, a paint spends what it took (one much slower than the frames before counts BUDGET_SAMPLE_MS or twice
	 * their average at most: it holds back a frame or two), and a frame is painted while the credit is not short; the
	 * credit kept is one frame's share at most. Public for tests. */
	struct FrameBudget {
		double creditMs = 0, averageMs = 0;
		bool due(double sinceLastMs); /* a frame came: the credit earned, and whether to paint it */
		void spent(double paintMs);
	};
	/* tests: how many times the crosshair's box was made (at the values' pace or a mouse move, not every frame), and
	 * its size now (steady while the same lines are read) */
	int readoutBuilds() const { return readoutBuilds_; }
	int legendBuilds() const { return legendBuilds_; } /* tests: the legend's picture made (not at every frame) */
	int legendMeasures() const { return chipMeasures_; } /* tests: the legend's chips measured (not at every frame) */
	int paints() const { return paints_; }             /* tests: the frames painted so far */
	QSizeF readoutSize() const { return readout_.isNull() ? QSizeF() : readout_.deviceIndependentSize(); }
	/* the notes: a time and a text each. Changes made with the mouse (moved, removed) emit notesChanged; editing
	 * the text is the Chart tab's (noteEditRequested on a double-click) */
	const QVector<ChartNote> &notes() const { return notes_; }
	void setNotes(const QVector<ChartNote> &notes);
	int addNote(double time, const QString &text); /* its index */
	void setNoteText(int index, const QString &text);
	void removeNote(int index);
	int selectedNote() const { return selectedNote_; } /* -1: none; Delete removes it */
	QRectF noteTag(int index) const;   /* where a note's tag was last drawn; empty: not in view (tests) */
	double timeAt(double x) const { return timeAtX(x); }
	/* the samples of the lines over t0..t1 (Export to CSV) */
	QVector<recording::Line> samples(double t0, double t1) const;
	/* one line's samples over t0..t1 (its histogram, its spectrum) */
	void lineSamples(int key, double t0, double t1, QVector<double> &times, QVector<double> &values) const;
	int chipAt(const QPointF &pos) const; /* the key of the legend's chip there; -1: none */
	/* tests: the bar between the cursors as last painted: its text (empty: none), the bar, and the text's box (inside
	 * the bar, or beside a tag when the bar is too short for it) */
	QString spanBarText() const { return spanBar_.text; }
	/* tests: the value labels of the last frame painted, and where a value lies on its Y axis */
	QStringList valueLabels() const { return valueLabels_; }
	double yOfValue(double value) const { return lastAxes_.y(value); }
	QRectF lastPlot() const { return lastAxes_.rect; }
	QRectF spanBarRect() const { return spanBar_.bar; }
	QRectF spanBarTextRect() const { return spanBar_.textRect; }

	/* who draws the lines (gpu_lines.h): Auto = a dedicated card if there is one, else the CPU (the processor's
	 * graphics draws slower than the CPU at 4K); a choice that cannot be had falls back to the CPU, and says why
	 * (drawingFailed). A card is opened on a thread of its own, the CPU drawing meanwhile (openingGpu), and takes
	 * over when it is ready (drawingChanged) */
	enum class Drawing { Auto, Dedicated, Internal, Cpu };
	void setDrawing(Drawing drawing);
	Drawing drawing() const { return drawing_; }
	QString drawingName() const; /* "GPU: <adapter>", "CPU, opening the GPU: <adapter>" or "CPU" */
	bool drawsOnGpu() const { return gpu_ != nullptr; }
	bool openingGpu() const { return opening_; }
	/* the card's layer is over the window (the plot is the card's then); tests: the card's last frame read back, with
	 * where it lies in the window's pixels */
	bool plotOnCard() const { return gpu_ && gpu_->shown(); }
	QImage gpuPicture(QRect *inWindow = nullptr) const;

signals:
	void windowChangedByUser(double seconds);
	void yChangedByUser(); /* wheel zoom or double-click: yAuto() / yLo() / yHi() */
	void liveChanged(bool live);
	void memoryChanged(double seconds);
	void cursorsChanged();
	void drawingFailed(const QString &why); /* the GPU asked for could not draw: the CPU does */
	void drawingChanged();                  /* a card opened (or failed to) after setDrawing: who draws now */
	void notesChanged();
	void noteEditRequested(int index);      /* a double-click on a note's tag */
	void menuRequested(const QPoint &globalPos, double time); /* a right-click on the chart: the time under it */
	void laneMenuRequested(int lane, const QPoint &globalPos); /* a right-click on a lane's value labels */
	void lineMenuRequested(int key, const QPoint &globalPos); /* a right-click on a line's chip in the legend */
	void triggered(double time);             /* the view holds on a crossing */
	void triggerLevelChanged(double level);  /* the level's line dragged and let go */
	void laneYChanged();                     /* a lane's Y range changed (the mouse, or its menu): to be saved */

protected:
	void paintEvent(QPaintEvent *) override;
	void hideEvent(QHideEvent *) override;
	bool event(QEvent *e) override; /* the mouse leaving */
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void mouseDoubleClickEvent(QMouseEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	/* Samples per min/max chunk, four levels, each LEVEL_STEP chunks of the one
	 * below. Chunks of a level start at multiples of its size in a line's samples
	 * (the memory drops whole chunks of the largest to keep it so), and a span with
	 * many samples per pixel column bins the largest chunks that still fit it. */
	static constexpr int LEVELS = 4;
	static constexpr int LEVEL_STEP = 8;
	static constexpr int CHUNK_SIZE[LEVELS] = { 8, 64, 512, 4096 };
	/* per series, whatever the memory and the RAM: trimming moves the line's arrays, 16 M samples (256 MB) at most */
	static constexpr qsizetype MAX_POINTS = 16000000;

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
	};
	/* one line's samples in a span, binned per pixel column, with the range of
	 * what lies inside the span */
	struct BinnedLine {
		const Series *series = nullptr;
		QVector<Bin> bins;
		double lo = 0, hi = 0;
		double posLo = 0; /* the smallest positive value of what lies inside the span (the Log scale); +inf: none */
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
	/* A Y range: Auto, Manual or Log (with Auto or a typed range). The plot has one; each lane its own. */
	struct YScale {
		bool autoRange = true, initialized = false, log = false;
		double lo = 0, hi = 1;
	};
	/* A plot of a frame: all of it with every line, or one lane (Lanes: a lane per unit). lines: indices into
	 * series_'s order (and the frame's binned lines) */
	struct Lane {
		QString key, label; /* key: its first unit (its Y range is kept by it); label: its units */
		QVector<int> lines;
		Axes axes;
	};
	/* The legend's chips at fixed places: a chip's width comes from its name,
	 * its unit and room for the widest number the legend writes, never from the
	 * value, so a changing value cannot push the chips after it. */
	struct LegendLayout {
		QRectF viewport;       /* the chips' part of the top row, left of the state */
		QVector<QRectF> chips; /* one per line in series order, not yet scrolled */
		double valueRoom = 0;  /* the value's room in every chip, right-aligned */
		double content = 0;    /* the chips' total width */
		double maxScroll() const { return std::max(0.0, content - viewport.width()); }
	};
	enum class Drag { None, Pan, Overview, CurA, CurB, LegendBar, Note, Level };

	/* the samples; limit: the line's share (pointsPerLine) */
	void dropExpired(Series &s, double t, qsizetype limit);
	static void addChunks(Series &s, qsizetype limit);

	/* job(0) .. job(count - 1) on the chart's threads and this one, at least jobsPerThread jobs a thread; returns
	 * when all are done */
	void inParallel(qsizetype count, const std::function<void(qsizetype)> &job, qsizetype jobsPerThread = 4) const;

	/* time and geometry */
	double clockNow() const;
	double liveEnd() const { return clockNow() - (smooth_ ? delay_ : 0); }
	double viewEnd() const { return live_ ? liveEnd() : viewEnd_; }
	void memorySpan(double &m0, double &m1) const; /* what is kept, as times */
	void holdAt(double end);                       /* not live: the view ends at `end` */
	void updateDelay(double frameDt);
	QRectF plotRect() const;
	QRectF overviewRect() const;
	double timeAtX(double x) const;
	double xAtTime(double t) const;

	/* the mouse */
	void pickCursor(double x);
	void zoomTime(double factor, double mouseX);
	void zoomY(double factor, double mouseY);
	bool pressLegend(const QPointF &pos);
	bool wheelLegend(QWheelEvent *e);
	void scrollLegendTo(double pixels, const LegendLayout &legend);

	/* the legend's geometry */
	LegendLayout legendLayout(const QRectF &plot) const;
	double legendOffset(const LegendLayout &legend) const;
	static QRectF legendTrack(const LegendLayout &legend);
	static QRectF legendThumb(const LegendLayout &legend, double offset);
	static QRectF legendArrow(const LegendLayout &legend, bool right);

	/* the drawing, in paint order */
	/* overview: a chunk may fill a whole column (the memory strip); otherwise at most half of one */
	void binSeries(const Series &s, double t0, double t1, double columns, bool overview, BinnedLine &out) const;
	/* the same for the view, with the bins kept from the frame before (Series::viewBins) */
	void binViewSeries(const Series &s, double t0, double t1, double columns, BinnedLine &out) const;
	/* samples i0 .. i1 - 1 into bins, chunks of level `top` at most where they start and end inside and lie in one
	 * column */
	static void binRange(const Series &s, qsizetype i0, qsizetype i1, double columnSeconds, int top, QVector<Bin> &bins);
	QVector<BinnedLine> binView(const Axes &axes) const;
	/* the Y range of this frame for a plot's lines (which: indices into lines) */
	void updateYRange(YScale &scale, const QVector<BinnedLine> &lines, const QVector<int> &which, double frameDt,
			double &lo, double &hi);
	void followData(YScale &scale, const QVector<BinnedLine> &lines, const QVector<int> &which, double frameDt);
	/* a frame: the plot on the card (onScreen: into the window, a card open, lines to show) or on the CPU */
	void paintFrame(QPainter &p, bool onScreen);
	void drawCard(QPainter &p) const;
	/* the grid's places: the values and times it marks, and their steps (labels) */
	struct GridTicks {
		double valueStep = 1, timeStep = 1;
		QVector<double> values, times;
		QVector<double> minor; /* Log: the faint lines at 2..9 of each decade */
		bool labelMinor = false; /* Log over less than two decades: the faint lines labelled too */
	};
	GridTicks gridTicks(const Axes &axes) const;
	/* the grid's lines (not when the card drew them) and its labels: each plot's values, the time under them all */
	void drawGrid(QPainter &p, const QVector<Lane> &plots, const Axes &axes, bool lines = true) const;
	void drawCursorSpan(QPainter &p, const Axes &axes) const;
	void drawLines(QPainter &p, const QVector<Lane> &plots, const QVector<BinnedLine> &lines) const;
	/* the card's layer: over the plot and 2 px around it (the lines' antialiasing) */
	QRect layerRect() const;
	/* the plot drawn by the card into its layer, all that lies on it; false: the card failed (closed, said) */
	bool plotOnGpu(const Axes &axes, const QVector<Lane> &plots, const QVector<BinnedLine> &lines);
	/* the layer shown or taken away (once the window holds what goes under it); a failure closes the card */
	void showLayer(bool shown);
	void drawCursors(QPainter &p, const Axes &axes) const;
	/* the notes: a dashed line and a tag at the bottom of the plot each (the tags' places kept for the mouse) */
	void drawNotes(QPainter &p, const Axes &axes) const;
	QRectF noteTagRect(const Axes &axes, int index) const; /* empty: not in view */
	void drawNoteTag(QPainter &p, const QRectF &tag, int index) const;
	const QImage &notePicture(int index, const QRectF &tag, qreal dpr) const; /* for the card */
	int noteAtPoint(const QPointF &pos) const; /* the note whose tag is there; -1: none */
	/* a cursor's tag (k 0: A, 1: B) as a picture, for the card */
	const QImage &tagPicture(int k, qreal dpr) const;
	/* The bar between the cursors' tags at the top of the plot, with the time between them: a cursor off the view
	 * ends it at the plot's edge; text too wide for it goes beside the right tag (the left one when the plot ends
	 * there). Empty text: no bar (a cursor not placed, or both off the view on one side). */
	struct SpanBar {
		QRectF bar, textRect;
		QString text;
		bool inside = false;
	};
	SpanBar spanBar(const Axes &axes) const;
	void drawSpanBar(QPainter &p, const SpanBar &bar) const;
	/* the same as a picture over `area` (the bar and its text), for the card */
	const QImage &spanBarPicture(const SpanBar &bar, const QRectF &area, qreal dpr) const;
	void drawMemoryStrip(QPainter &p, const Axes &axes);
	void drawMemoryLines(QPainter &p, const Axes &strip) const;
	void drawLegend(QPainter &p, const Axes &axes) const;
	void drawChip(QPainter &p, const Series &s, const QRectF &chip, double valueRoom) const;
	void drawLegendBar(QPainter &p, const LegendLayout &legend, double offset) const;
	/* the crosshair: its x, a dot on every line read and the box's place (readout_ made again when due) */
	struct Crosshair {
		double x = 0;
		QVector<QPair<QPointF, QColor>> dots; /* centres */
		QPointF boxAt;                         /* readout_'s top left; readout_ null: no box */
	};
	bool crosshair(const Axes &axes, const QVector<Lane> &plots, const QVector<BinnedLine> &lines, qreal dpr,
			Crosshair &out) const;
	void drawCrosshair(QPainter &p, const Axes &axes, const QVector<Lane> &plots, const QVector<BinnedLine> &lines) const;
	/* one line's row in the crosshair's box */
	struct ReadoutRow {
		QString name, value, unit;
		QColor color;
	};
	/* the crosshair's box as a picture (dpr: the device's); drawCrosshair puts it beside the mouse */
	QImage readoutPicture(double plotHeight, const QString &timeText, const QVector<ReadoutRow> &rows, qreal dpr) const;
	/* the box without the time and the numbers, and where they go: made again only when the lines read, the plot's
	 * height, the scaling or the theme change */
	struct ReadoutBase {
		QString key;
		QImage image;
		int perColumn = 1;
		double nameW = 0, gap = 0, valueRoom = 0, unitW = 0, columnW = 0, width = 0;
	};
	const ReadoutBase &readoutBase(double plotHeight, const QVector<ReadoutRow> &rows, qreal dpr) const;
	/* a picture of a rounded box, opaque inside, drawn mostly as a copy */
	static void drawBoxPicture(QPainter &p, QPointF at, const QImage &image, double radius);
	/* a dot in a line's colour (the crosshair's), drawn once per colour and scaling */
	const QImage &dotPicture(const QColor &color, qreal dpr) const;
	void drawState(QPainter &p, const Axes &axes) const;
	/* dpr: device pixels per unit of p's coordinates (the copies' count and shift) */
	static void strokePolyline(QPainter &p, const QPolygonF &poly, const QColor &color, bool thin, qreal dpr);
	static void prepareTile(const QPainter &p, const QRect &device, QImage &image, QTransform &world, QPointF &at);
	template <typename MapX, typename MapY>
	/* bands: given, a full column's min..max is a bar there (bandWidth wide) and the polyline only goes through its
	 * first and last value; not given (the memory strip), the polyline draws the whole stroke */
	static QPolygonF toPolyline(const QVector<Bin> &bins, double columnSeconds, MapX x, MapY y,
			QVector<QRectF> *bands = nullptr, double bandWidth = 0, double pixel = 0);
	static void fillBands(QPainter &p, const QRectF *bands, qsizetype count, const QColor &color);
	void updatePaintStats(double paintMs);

	QMap<int, Series> series_;
	quint64 seriesGeneration_ = 0; /* a line added or removed, or the samples cleared */
	quint64 seriesAdded_ = 0;      /* lines added so far: each line's spread */

	/* the memory strip's lines, drawn again when the data has moved a pixel on it */
	QImage stripImage_;
	double stripEnd_ = 0;          /* the strip's end when it was drawn */
	double stripMemory_ = 0;
	QPointF stripAt_;              /* where its image goes, on whole device pixels */
	quint64 stripGeneration_ = ~quint64(0);

	mutable QThreadPool pool_;
	mutable QVector<QImage> stripeImages_; /* the plot's stripes, kept for the next frame */
	bool capped_ = false;                  /* the samples' budget, not the memory, limits what is kept */
	qsizetype movedThisFrame_ = 0;         /* samples moved by trims since the last frame() (dropExpired) */
	int ramMB_ = DEFAULT_RAM_MB;
	int drawThreads_ = 0;                  /* the stripes at most; 0: one per thread */
	Drawing drawing_ = Drawing::Cpu;
	std::unique_ptr<GpuLines> gpu_;        /* drawing the plot; null: the CPU does */
	/* a card being opened on a thread of its own (setDrawing); a later choice lets it go when it is ready. Destroyed
	 * before gpu_, waiting for an open still going */
	QThreadPool opener_;
	bool opening_ = false;
	QString openingName_;
	quint64 openGeneration_ = 0;
	void gpuOpened(std::unique_ptr<GpuLines> gpu, bool ok, const QString &error, quint64 generation);
	QRect layerPixels_;                    /* the layer, in the window's pixels (its client area), at the last frame */
	QPointF layerOrigin_;                  /* the chart's top left there */
	int framesUnder_ = 0;                  /* frames drawn by the card while its layer is not shown (read back under it) */
	mutable QImage tags_[2];               /* the cursors' tags for the card, at tagsKey_ */
	mutable QString tagsKey_;
	mutable SpanBar spanBar_;              /* the bar between the cursors as last painted (tests) */
	mutable QImage spanBarImage_;          /* its picture for the card, at spanBarKey_ */
	mutable QString spanBarKey_;
	std::function<double()> clock_;
	qint64 epochMs_ = 0;

	/* the view and the memory, seconds */
	double window_ = 30, memory_ = 60;
	double viewEnd_ = 0;      /* held: where the view ends */
	double lastViewEnd_ = 0;  /* where the view ended in the last frame drawn */
	bool live_ = true, normalized_ = false, smooth_ = true, hoverValues_ = true, recording_ = false;
	double delay_ = 0, peakGap_ = 0; /* Smooth: the display delay, and the gap it covers */

	/* the Y range of the plot (lanes: of each lane, by its key); initialized: Auto has a range to move from */
	YScale y_;
	QHash<QString, YScale> laneScales_;
	bool lanes_ = false;
	mutable QVector<Lane> lanesShown_; /* the plots as last painted: one, or the lanes */
	bool logOf(const YScale &scale) const { return scale.log && !normalized_; } /* the Log scale drawn now */
	bool logShown() const { return logOf(y_); }
	/* the plots of a frame: the whole plot with every line, or a lane per unit (their rects, lines and keys; their
	 * axes' times and ranges are set by paintFrame) */
	QVector<Lane> plotLayout() const;
	YScale &scaleOf(const Lane &lane) { return lanes_ ? laneScales_[lane.key] : y_; }
	int laneAtY(double y) const; /* the lane last painted at that height (the nearest); -1: no lanes */
	QString laneKey(int lane) const; /* its unit, by which its Y range is kept */
	void forgetRanges(); /* the lines changed: every Auto range jumps to them at the next frame */
	mutable Axes lastAxes_;          /* the plot's axes at the last frame painted (tests) */
	mutable QStringList valueLabels_;
	bool stripLog_ = false;          /* the memory strip's image drawn on the Log scale */

	/* the totals since Clear: of the lines taken off the chart, by key, kept for when they come back (with their
	 * name and unit: another line under that key starts at 0) */
	struct KeptTotal {
		QString name, unit;
		double total = 0, t = NAN, v = 0;
	};
	QHash<int, KeptTotal> keptTotals_;
	double totalsSince_ = NAN;

	/* the cursors and the mouse */
	bool cursorMode_ = false;
	double cursorA_ = NAN, cursorB_ = NAN;
	Drag drag_ = Drag::None;
	double dragStartX_ = 0, dragStartEnd_ = 0; /* Pan: where the drag began, and the view's end then */
	double dragStartScroll_ = 0;               /* LegendBar: the legend's scroll when the drag began */
	int mouseX_ = -1;                          /* -1: the mouse is not over the chart */
	/* the crosshair's box: made again when the values move on (valuesTick_), the mouse moves (at most every
	 * READOUT_FOLLOW_MS, readoutMade_), or the plot, the scaling or the theme changes; in between the same picture
	 * beside the mouse */
	quint64 valuesTick_ = 0;
	mutable QImage readout_;
	mutable quint64 readoutTick_ = ~quint64(0);
	mutable int readoutMouseX_ = -1;
	mutable double readoutPlotHeight_ = 0;
	mutable qreal readoutDpr_ = 0;
	mutable bool readoutDark_ = false;
	mutable int readoutBuilds_ = 0;
	mutable QElapsedTimer readoutMade_;
	mutable ReadoutBase readoutBase_;
	/* the legend as a picture: made again when its key changes (the lines, the values' tick, the scroll, the size,
	 * the scaling, the theme) */
	mutable QImage legendImage_;
	mutable QString legendKey_;
	mutable int legendBuilds_ = 0;
	/* the chips' widths (legendLayout): measured again only when the lines or the font change */
	mutable QVector<double> chipWidths_;
	mutable double chipValueRoom_ = 0;
	mutable quint64 chipsGeneration_ = ~quint64(0);
	mutable QString chipsFont_;
	mutable int chipMeasures_ = 0;
	mutable QHash<QRgb, QImage> dots_; /* by colour, at dotsDpr_ */
	/* the trigger: what it watches, whether it waits for a crossing, and the last one */
	struct Trigger {
		bool on = false, armed = false;
		int key = -1;
		double level = 0;
		TriggerEdge edge = TriggerEdge::Rising;
		TriggerMode mode = TriggerMode::Normal;
		double at = NAN;        /* the last crossing */
		double armedFrom = 0;   /* crossings after this time count */
	} trigger_;
	void fireTrigger(double time);
	/* the level's line and the marker at the crossing (the CPU's; the card's in plotOnGpu): where, in the frame's
	 * plots; false: nothing to draw */
	bool triggerGeometry(const QVector<Lane> &plots, const QVector<BinnedLine> &lines, double &levelY, QRectF &lane,
			QRectF &tag) const;
	void drawTrigger(QPainter &p, const QVector<Lane> &plots, const QVector<BinnedLine> &lines) const;
	const QImage &triggerPicture(qreal dpr) const;
	mutable QRectF triggerTag_;
	mutable double triggerLineY_ = NAN;
	mutable QRectF triggerLane_;      /* the plot the level's line is in, for the drag */
	mutable Axes triggerAxes_;        /* its axes (the level from the mouse) */
	mutable double triggerLo_ = 0, triggerHi_ = 1; /* Normalise: the line's own range */
	mutable QImage triggerImage_;
	mutable QString triggerImageKey_;

	/* the notes, the one clicked last, and their tags as drawn last (and as pictures for the card, by their key) */
	QVector<ChartNote> notes_;
	int selectedNote_ = -1;
	mutable QVector<QRectF> noteTags_;
	mutable QHash<QString, QImage> notePictures_;
	mutable qreal dotsDpr_ = 0;

	/* the legend's scroll, pixels; may be past what the chips need after a
	 * resize or a line removed: legendOffset() reads it within range */
	double legendScroll_ = 0;

	/* the legend's values: taken from the lines at their own pace (value_pace.h) */
	ValuePacer valuePacer_;
	QElapsedTimer valueClock_;

	/* the status line's numbers */
	QElapsedTimer frameClock_, fpsClock_;
	int fpsFrames_ = 0, paints_ = 0;
	double fps_ = 0, paintMs_ = 0;

	FrameBudget budget_;
	QElapsedTimer framesCome_; /* since frame() was called last: a change waits for the next frame (refresh()) */
	void paintSoon();          /* update(), not of the plot while the card shows it */
};

/* ChartWidget: the chart as the window uses it, a ChartView and its API */
class ChartWidget : public QWidget {
	Q_OBJECT
public:
	explicit ChartWidget(QWidget *parent = nullptr);
	ChartView *view() const { return view_; }

	void addSeries(int key, const QString &name, const QString &unit, const QColor &color) {
		view_->addSeries(key, name, unit, color);
	}
	void removeSeries(int key) { view_->removeSeries(key); }
	void clearSeries() { view_->clearSeries(); }
	void clearData() { view_->clearData(); }
	void append(int key, double t, double v) { view_->append(key, t, v); }
	void setClock(std::function<double()> clock, qint64 epochMsAtZero) {
		view_->setClock(std::move(clock), epochMsAtZero);
	}
	void frame() { view_->frame(); }
	void setValuesPerSecond(int perSecond) { view_->setValuesPerSecond(perSecond); }
	void update() { view_->refresh(); }
	void setWindow(double seconds) { view_->setWindow(seconds); }
	double window() const { return view_->window(); }
	void setMemory(double seconds) { view_->setMemory(seconds); }
	double memory() const { return view_->memory(); }
	void setLive(bool on) { view_->setLive(on); }
	bool live() const { return view_->live(); }
	void setNormalized(bool on) { view_->setNormalized(on); }
	void setSmooth(bool on) { view_->setSmooth(on); }
	void setYAuto() { view_->setYAuto(); }
	bool setYManual(double lo, double hi) { return view_->setYManual(lo, hi); }
	void setYLog(bool on) { view_->setYLog(on); }
	bool yLog() const { return view_->yLog(); }
	bool yAuto() const { return view_->yAuto(); }
	double yLo() const { return view_->yLo(); }
	double yHi() const { return view_->yHi(); }
	double fps() const { return view_->fps(); }
	double paintMs() const { return view_->paintMs(); }
	double delayMs() const { return view_->delayMs(); }

signals:
	void windowChangedByUser(double seconds);
	void yChangedByUser();

private:
	ChartView *view_;
};

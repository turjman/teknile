/* SPDX-License-Identifier: Apache-2.0 */
/* The Chart tab: the live chart (chart_widget.h), two rows of controls over
 * it and the measurements under it.
 *
 *  - the axes: Window (the time shown) and Memory (the time kept), a preset
 *    or any length typed; the RAM for the samples; the Y range, Auto or
 *    Manual with its min and max.
 *  - what to do: Hold / Live, Measure, Cursors and Clear cursors, the math
 *    lines (ƒ Math: a formula over registers, model/math_lines.h), Display
 *    (Normalise, Smooth, Hover values and who draws the lines), the info
 *    line (whole parts dropped when narrow), Clear and Remove all.
 *  - the measurements (Measure on), in a splitter under the chart: the value
 *    at cursors A and B, min, max, mean, RMS, standard deviation, peak to
 *    peak and the area under each line, over the cursors or the view, and
 *    each line's total since Clear; a right-click on the header shows or
 *    hides columns.
 *
 *  - Lanes (Display): a plot per unit, stacked, scrolling when they do not
 *    fit; each lane's Y range by a right-click on its value labels (Auto,
 *    Manual, Log, Fold lane / Open lane), a ▾ on each lane to fold it,
 *    Fold all / Open all lanes in Display; the folds kept by unit.
 *  - a right-click on a line's chip in the legend: its Histogram or Spectrum
 *    over A -> B (or the view), in a small window (analysis_window.h).
 *  - Trigger (Display): a row under the actions: a line, its edge, the level,
 *    Single or Normal, Arm; the chart holds on each crossing (ChartView).
 *  - a right-click on the chart: Copy picture, Save picture (painted by the
 *    CPU, the card's plot too), Export to CSV (the view, or A -> B; on a
 *    thread, with progress and Cancel), Add note here, Open recording.
 *
 * The window says which registers are plotted (plotRegister) and hands over
 * the samples of every display frame (frame()); the tab adds the math lines'
 * points from them. What the user sets here is kept in the settings under
 * "chart/..." and comes back at the next start; a recording's chart
 * (recording_window.h) is a second tab, its settings under a group of its own. */
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include "model/device_map.h"
#include "model/math_lines.h"
#include "ui/chart_widget.h" /* its Info and Stats in the measurements' functions */
#include "model/recording_file.h"
#include "model/register_model.h"
#include "ui/analysis_window.h"

class ChartView;
class ChartWidget;
class QAction;
class QActionGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressDialog;
class QPushButton;
class QTableWidget;
enum class LogLevel;

class ChartTab : public QWidget {
	Q_OBJECT
public:
	/* clock: the time base of the samples, in seconds, read at every display frame; settingsGroup: where its settings
	 * are kept ("chart": the live chart's) */
	explicit ChartTab(std::function<double()> clock, QWidget *parent = nullptr,
			const QString &settingsGroup = QStringLiteral("chart"));
	~ChartTab() override; /* an export still running is cancelled and waited for */
	ChartView *view() const;

	/* the note right of RAM: the memory the lines need for the Memory set ("needs 1.4 GB"); over the RAM, what fits
	 * too ("needs 2.8 GB, keeps 22 min") and over = true. Empty: nothing measured yet. */
	static QString ramNeedText(qint64 bytesNeeded, int ramMB, double memorySeconds, bool &over);
	/* a Y box's text: Manual six digits; Auto four, but never fewer than the whole part (17420, not 1.742e+04) */
	static QString yFieldText(double value, bool manual);
	/* the measurement table's columns; every one but the line's can be hidden (a right-click on the header) */
	enum MeasureColumn { ColLine, ColAtA, ColAtB, ColDiff, ColMin, ColMax, ColMean, ColRms, ColStd, ColP2p, ColArea,
		ColAreaHours, ColTotal, MEASURE_COLUMNS };
	/* the Y range list: Auto, Manual, Log (its range Auto or typed) */
	enum YMode { YAuto, YManual, YLog };

	/* the map's registers, again after every change of the map: the math lines are compiled against them */
	void setRegisters(const QVector<RegDef> &registers);
	/* a register's line on or off the chart: keyed by regKey (its device and address), drawn in the next colour of
	 * the palette */
	void plotRegister(const RegDef &def, bool on);
	/* one bit field of a register on the chart: a math line "REG.FIELD" = bits(REG, lsb, width),
	 * made if there is none (a register shown scaled has no raw bits to take: not offered) */
	void plotField(const RegDef &def, const BitField &field);
	/* every line off the chart, and the colours from the first again: for a new map (the math
	 * lines come back with the next setRegisters) */
	void clearLines();
	/* the registers the math lines shown read: they must be sampled too, plotted or not */
	QVector<RegKey> mathRegisters() const;

	/* once per display frame: the samples of the polls since the last frame, then the chart moves on */
	void frame(const MathLines::Samples &samples);
	/* how often the legend's values change (value_pace.h); the lines move at every frame */
	void setValuesPerSecond(int perSecond);
	/* the registers the chart may hold now (RegisterModel::plotLimit): the info line says "N/LIMIT plotted" */
	void setRegisterLimit(int limit) { registerLimit_ = limit; }
	void logDrawing(); /* the Log: who draws the lines (at start, once the window listens, and at each change) */
	/* the info line's text: the lines on the chart, then frames, paint time, delay (for tests too). width >= 0: what
	 * fits that many pixels of the line's font, whole parts dropped (the paint time, the word "plotted", the delay,
	 * then the frames, who draws, the math lines), never letters cut */
	QString infoText(int width = -1) const;
	QString infoTip() const; /* what the info line's numbers are */
	/* the Display menu's state in words: "Normalise off · Smooth on · Hover values on · drawn by the GPU: <name>" (its
	 * tooltip; tests) */
	QString displayState() const;
	/* this tab is the one shown, or no longer: the measurements and the info line are only kept up to date then */
	void setShown(bool shown);
	/* with the window's statistics, twice a second: the info line, and the Y fields while Auto moves the range */
	void refreshStatus();
	/* the theme changed: the chart is drawn again in its colours */
	void themeChanged();
	/* A recording's chart (recording_window.h): no Live, Memory, RAM, Clear or Remove all; Smooth off; the labels'
	 * clock from the file (epochMs: its time base's zero); RAM ramMB; the memory as long as t0..t1. The samples come
	 * once (frame), then the view shows t0..t1 (ChartView::showSpan). */
	void setRecording(qint64 epochMs, double t0, double t1, int ramMB, int columns);
	bool isRecording() const { return recording_; }
	void showSpan(double t0, double t1); /* the view on t0..t1, held; the Window box says how long */

	/* the right-click's actions, for the menu and the tests */
	QImage picture() const;                 /* the chart as shown, painted by the CPU */
	void copyPicture() const;
	bool savePicture(const QString &file) const;
	/* the samples of the view, or A -> B with both cursors placed, in the recording's format, on a thread of its own
	 * (a progress dialog with Cancel when it takes long); the notes in that span beside it. Done: exported(). False:
	 * another export still runs */
	bool exportCsv(const QString &file);
	bool exporting() const { return job_ != nullptr; }
	void cancelExport();
	/* a note at `time`: its text asked for (empty: none) */
	void addNoteAt(double time);
	void editNote(int index); /* its text asked for again; emptied: removed */
	/* the chart's menu at a place on the screen, `time` under it (a right-click); tests: the menu, built at each
	 * right-click */
	void showChartMenu(const QPoint &globalPos, double time);
	QMenu *chartMenu() const { return chartMenu_; }
	/* a lane's menu (a right-click on its value labels or its folded strip): Auto, Manual…, Log, Fold lane / Open
	 * lane; tests: the menu */
	void showLaneMenu(int lane, const QPoint &globalPos);
	QMenu *laneMenu() const { return laneMenu_; }
	void editLaneRange(int lane); /* Manual…: its min and max asked */
	/* a line's menu (a right-click on its chip): Histogram, Spectrum; tests: the menu */
	void showLineMenu(int key, const QPoint &globalPos);
	QMenu *lineMenu() const { return lineMenu_; }
	/* a line's histogram or spectrum over A -> B (or the view), in a window of its own over this one */
	AnalysisWindow *openAnalysis(AnalysisWindow::Kind kind, int key);
	/* the trigger row's state in words ("armed", "triggered at 14:03:12.345", ...): tests */
	QString triggerState() const;

	int measureUpdates() const { return measureUpdates_; } /* tests: the measurements made again so far */
	int measureFullUpdates() const { return measureFullUpdates_; } /* tests: of those, all of the table */
	int measureInfoChanges() const { return measureInfoChanges_; } /* tests: the line over the table written anew */
	int measureFills() const { return measureFills_; } /* tests: the table filled with all of it (from the threads) */

signals:
	/* the math lines read other registers now (shown, hidden, edited, removed, or the map changed) */
	void mathRegistersChanged();
	/* Remove all: every register off the chart (the window unticks every Plot) */
	void unplotAllRequested();
	/* for the event log */
	void logged(LogLevel level, const QString &text);
	/* Open recording: a file, or empty to choose one */
	void openRecordingRequested(const QString &file);
	/* an export ended: rows written, or error (cancelled: error says so) */
	void exported(const QString &file, qint64 rows, const QString &error);
	void notesChanged();

private:
	QString settingKey(const char *name) const { return group_ + QLatin1Char('/') + QLatin1String(name); }

	/* building the tab */
	QHBoxLayout *buildAxesRow();
	QHBoxLayout *buildActionsRow();
	QWidget *buildMeasurements();
	void connectControls();
	void restoreSettings();

	/* the axes */
	void applyWindowText();  /* Window typed or picked */
	void applyMemoryText();  /* Memory typed or picked */
	void applyRamText();     /* RAM typed or picked */
	void applyDrawing(int drawing); /* a ChartView::Drawing picked (or restored): the renderer, the menu, the Log */
	void showDisplayState();        /* the Display button's tooltip: what is on now */
	void showYRange(bool save = true); /* the Y fields follow the chart; save: into the settings */
	void applyYFields();     /* the Y fields typed: Manual */

	/* the measurements under the chart; measureSoon: at once, then at most every MEASURE_FOLLOW_MS while the cursors
	 * move (a drag moves them at every mouse move) */
	void updateMeasures(bool cursorsOnly = false); /* cursorsOnly: A, B and B - A (a cursor dragged) */
	void measureTick(); /* the timer's: all of it again only when it changed, the totals each time */
	QVector<double> measureKeyNow(const QVector<int> &keys) const;
	void fillMeasures(const QVector<ChartView::Info> &lines, const QVector<ChartView::Stats> &stats, bool cursorsOnly,
			const QElapsedTimer &timed);
	QVector<double> lastMeasureKey_; /* what the last full measurement was of */
	double measureThreadMs_ = 0;     /* the timing aid: the threads' part of the measurements */
	int measureFills_ = 0;
	void measureSoon();
	QString measuredRangeText() const;
	void showMeasureColumns(); /* the columns hidden as saved (chart/measureColumns) */

	/* the math lines */
	void rebuildMath();           /* formulas -> registers, chart lines, the menu */
	void drawMathLines();         /* the chart's lines for them */
	void rebuildMathMenu();       /* the ƒ Math button's menu and label */
	void editMathLine(int line);  /* -1: a new one */

	QString group_;               /* the settings' group: "chart", or a recording's */
	std::function<double()> clock_;
	bool recording_ = false;
	ChartWidget *chart_;
	bool shown_ = false;
	int nextColor_ = 0;           /* the palette's colour of the next register plotted */
	MathLines mathLines_;
	QVector<RegDef> registers_;   /* the map's, for the math lines */

	/* the axes row */
	QComboBox *window_, *memory_, *yMode_;
	QString yModeTip_;
	QLineEdit *yMin_, *yMax_;
	int registerLimit_ = RegisterModel::MAX_PLOTTED; /* the registers the chart may hold at the rate now */
	QString drawingFailure_;      /* a card's failure before the window listened: logged by logDrawing */

	/* the actions row */
	QPushButton *holdButton_, *measureButton_, *cursorsButton_, *clearCursorsButton_, *mathButton_;
	QPushButton *clearButton_, *removeAllButton_;
	QComboBox *ram_;              /* the samples' RAM, all the lines together */
	QLabel *chartInfo_;           /* the lines on the chart, frames per second, time to draw one, the smoothing delay */
	QPushButton *displayButton_;  /* how the lines are drawn; its menu: Normalise, Smooth, Hover values, Drawing */
	QAction *normalize_, *smooth_, *hoverValues_, *lanes_, *trigger_;
	QAction *foldAll_, *openAll_; /* Fold all lanes, Open all lanes: shown with Lanes on */
	QActionGroup *drawingChoices_; /* the Drawing part of the Display menu: Auto, the adapters by name, CPU */
	QLabel *ramNeed_;             /* what the lines need for the Memory set; amber when more than the RAM */

	/* the measurements */
	QWidget *measurePanel_;
	QLabel *measureInfo_;         /* what the measurements cover */
	QTableWidget *measures_;
	QTimer measureTimer_;         /* the measurements follow the lines while they are shown */
	QTimer measureFollow_;        /* the cursors moved: the measurements again at its end, not before (measureSoon) */
	bool measurePending_ = false;
	int measureUpdates_ = 0, measureFullUpdates_ = 0, measureInfoChanges_ = 0;
	/* the timing aid (EVRE_PERF_LOG=<file>, a test aid like EVRE_SHOT): every PERF_LOG_MS a line of the paint's cost,
	 * the measurements' and the polls' since the last one, appended to the file */
	void writePerfLine();
	QString perfLogPath_;
	QElapsedTimer perfClock_;
	double measureMs_ = 0;
	int measuresTimed_ = 0;
	qint64 pollsSince_ = 0;
	QVector<int> measuredKeys_;   /* the lines measured last: while the same, the columns only grow */
	QMenu *measureColumns_;       /* the header's right-click: a tick per column */

	/* the right-click on the chart */
	QMenu *chartMenu_ = nullptr;
	QMenu *laneMenu_ = nullptr;
	QMenu *lineMenu_ = nullptr;

	/* the trigger's row (Display -> Trigger) */
	QWidget *buildTriggerRow();
	void applyTrigger();          /* the row's choices to the chart, armed again */
	void fillTriggerLines();      /* the lines it can watch (registers and math lines), the one chosen kept */
	void showTriggerState();
	QWidget *triggerRow_ = nullptr;
	QComboBox *triggerLine_ = nullptr, *triggerEdge_ = nullptr, *triggerMode_ = nullptr;
	QLineEdit *triggerLevel_ = nullptr;
	QPushButton *triggerArm_ = nullptr;
	QLabel *triggerState_ = nullptr;
	QVector<int> triggerKeys_;    /* the lines in the list, by key */
	void showLaneActions(); /* Fold all / Open all: shown with Lanes on, each enabled when it has something to do */
	void showYControls(); /* the Y range row: the plot's, or (lanes) disabled: each lane has its own */
	QLabel *memoryLabel_ = nullptr, *ramLabel_ = nullptr;
	/* an export on a thread: its progress (per mille), cancel, and whether it is done; shared with the thread */
	struct ExportJob {
		std::atomic<bool> cancel{ false }, done{ false };
		std::atomic<int> permille{ 0 };
		QString file, error;
		qint64 rows = 0;
	};
	std::shared_ptr<ExportJob> job_;
	QProgressDialog *exportProgress_ = nullptr;
	QTimer exportTimer_;          /* the progress followed, the end seen */
	QVector<ChartNote> exportNotes_; /* the notes of the span exported, saved beside it at the end */
	void exportDone();
};

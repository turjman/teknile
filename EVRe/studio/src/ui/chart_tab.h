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
 *    line, Clear and Remove all.
 *  - the measurements (Measure on), in a splitter under the chart: the value
 *    at cursors A and B, min, max, mean, RMS and the area under each line,
 *    over the cursors or the view.
 *
 * The window says which registers are plotted (plotRegister) and hands over
 * the samples of every display frame (frame()); the tab adds the math lines'
 * points from them. What the user sets here is kept in the settings under
 * "chart/..." and comes back at the next start. */
#pragma once

#include <QTimer>
#include <QVector>
#include <QWidget>
#include <cstdint>
#include <functional>

#include "model/device_map.h"
#include "model/math_lines.h"
#include "model/register_model.h"

class ChartWidget;
class QAction;
class QActionGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
enum class LogLevel;

class ChartTab : public QWidget {
	Q_OBJECT
public:
	/* clock: the time base of the samples, in seconds, read at every display frame */
	explicit ChartTab(std::function<double()> clock, QWidget *parent = nullptr);

	/* the note right of RAM: the memory the lines need for the Memory set ("needs 1.4 GB"); over the RAM, what fits
	 * too ("needs 2.8 GB, keeps 22 min") and over = true. Empty: nothing measured yet. */
	static QString ramNeedText(qint64 bytesNeeded, int ramMB, double memorySeconds, bool &over);
	/* a Y box's text: Manual six digits; Auto four, but never fewer than the whole part (17420, not 1.742e+04) */
	static QString yFieldText(double value, bool manual);

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
	/* the info line's text: the lines on the chart, then frames, paint time, delay (for tests too) */
	QString infoText() const;
	/* the Display menu's state in words: "Normalise off · Smooth on · Hover values on · drawn by the GPU: <name>" (its
	 * tooltip; tests) */
	QString displayState() const;
	/* this tab is the one shown, or no longer: the measurements and the info line are only kept up to date then */
	void setShown(bool shown);
	/* with the window's statistics, twice a second: the info line, and the Y fields while Auto moves the range */
	void refreshStatus();
	/* the theme changed: the chart is drawn again in its colours */
	void themeChanged();
	int measureUpdates() const { return measureUpdates_; } /* tests: the measurements made again so far */
	int measureFullUpdates() const { return measureFullUpdates_; } /* tests: of those, all of the table */

signals:
	/* the math lines read other registers now (shown, hidden, edited, removed, or the map changed) */
	void mathRegistersChanged();
	/* Remove all: every register off the chart (the window unticks every Plot) */
	void unplotAllRequested();
	/* for the event log */
	void logged(LogLevel level, const QString &text);

private:
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
	void measureSoon();
	QString measuredRangeText() const;

	/* the math lines */
	void rebuildMath();           /* formulas -> registers, chart lines, the menu */
	void drawMathLines();         /* the chart's lines for them */
	void rebuildMathMenu();       /* the ƒ Math button's menu and label */
	void editMathLine(int line);  /* -1: a new one */

	ChartWidget *chart_;
	bool shown_ = false;
	int nextColor_ = 0;           /* the palette's colour of the next register plotted */
	MathLines mathLines_;
	QVector<RegDef> registers_;   /* the map's, for the math lines */

	/* the axes row */
	QComboBox *window_, *memory_, *yMode_;
	QLineEdit *yMin_, *yMax_;
	int registerLimit_ = RegisterModel::MAX_PLOTTED; /* the registers the chart may hold at the rate now */
	QString drawingFailure_;      /* a card's failure before the window listened: logged by logDrawing */

	/* the actions row */
	QPushButton *holdButton_, *measureButton_, *cursorsButton_, *clearCursorsButton_, *mathButton_;
	QPushButton *clearButton_, *removeAllButton_;
	QComboBox *ram_;              /* the samples' RAM, all the lines together */
	QLabel *chartInfo_;           /* the lines on the chart, frames per second, time to draw one, the smoothing delay */
	QPushButton *displayButton_;  /* how the lines are drawn; its menu: Normalise, Smooth, Hover values, Drawing */
	QAction *normalize_, *smooth_, *hoverValues_;
	QActionGroup *drawingChoices_; /* the Drawing part of the Display menu: Auto, the adapters by name, CPU */
	QLabel *ramNeed_;             /* what the lines need for the Memory set; amber when more than the RAM */

	/* the measurements */
	QWidget *measurePanel_;
	QLabel *measureInfo_;         /* what the measurements cover */
	QTableWidget *measures_;
	QTimer measureTimer_;         /* the measurements follow the lines while they are shown */
	QTimer measureFollow_;        /* the cursors moved: the measurements again at its end, not before (measureSoon) */
	bool measurePending_ = false;
	int measureUpdates_ = 0, measureFullUpdates_ = 0;
	QVector<int> measuredKeys_;   /* the lines measured last: while the same, the columns only grow */
};

/* SPDX-License-Identifier: Apache-2.0 */
/* A recording opened: a window of its own holding a second Chart tab, fed from
 * the file (model/recording_file.h) instead of the device. The live chart goes
 * on meanwhile, and several recordings can be open.
 *
 *  - Opening: the file's head and tail tell its size; a file whose samples
 *    need more than the chart's RAM asks to keep the last part. It is read on a
 *    thread, a progress dialog with Cancel over the window that asked.
 *  - The chart: held on the whole recording (no Live), titled with the file's
 *    name and span, its settings under "recording/..." (its own math lines).
 *    Each column is a line, keyed by its place in the file; with a map loaded,
 *    a column named as one of its registers takes that register's definition
 *    (value names, fields: the Lines menu plots a field), a byte array's column
 *    is left out. The window keeps the file's values: a math line or a field
 *    added later is computed from them.
 *  - Fast streams' recordings (.evrs, model/fast_recording.h): opened alone, or with the CSV they were recorded
 *    beside (run.csv with run.ADC.evrs): the file mapped into memory, only the summaries made, on the same thread;
 *    a fast line per channel, on the CSV's clock (the TIME pieces are the Studio's time_s, as the CSV's rows).
 *  - Notes: "<file>.notes.json" read at the start and saved at every change.
 *  - The last 8 recordings opened or exported (Recent recordings), and every
 *    window, closed with the main window (closeAll). */
#pragma once

#include <QPointer>
#include <QWidget>
#include <functional>

#include "model/device_map.h"
#include "model/fast_recording.h"
#include "model/recording_file.h"

class ChartTab;
class QAction;
class QLabel;
class QMenu;
class QPushButton;

class RecordingWindow : public QWidget {
	Q_OBJECT
public:
	static constexpr int MAX_RECENT = 8;
	/* what the window keeps of a sample (its time and value) besides the chart's: the RAM asked for */
	static constexpr int KEPT_BYTES_PER_SAMPLE = 16;

	/* Opens `file`: read on a thread (progress and Cancel over dialogParent), map: the registers the columns are
	 * matched with, ramMB: the chart's RAM. done: called with the window once it is shown (not when cancelled or the
	 * file cannot be read: said in a message box). */
	static void open(QWidget *dialogParent, const QString &file, const QVector<RegDef> &map, int ramMB,
			const std::function<void(RecordingWindow *)> &done = {});
	/* the same, the file chosen in a dialog first */
	static void choose(QWidget *dialogParent, const QVector<RegDef> &map, int ramMB,
			const std::function<void(RecordingWindow *)> &done = {});
	/* the recordings opened or exported last, newest first ("recording/recent"), and the menu of them */
	static QStringList recentFiles();
	static void remember(const QString &file);
	static void fillRecentMenu(QMenu *menu, const std::function<void(const QString &)> &openFile);
	static QList<RecordingWindow *> windows(); /* open now */
	static void closeAll();

	/* fast: the streams' recordings (the CSV's beside it, or a .evrs alone: data then empty) */
	RecordingWindow(const QString &file, recording::Data data, const QVector<RegDef> &map, int ramMB,
			QVector<fast::Recording> fast = {});
	~RecordingWindow() override;

	ChartTab *chartTab() const { return tab_; }
	QString file() const { return file_; }
	QVector<RegDef> definitions() const { return defs_; } /* a line per column, keyed by its place */
	double firstTime() const { return t0_; }
	double lastTime() const { return t1_; }
	int skipped() const { return skipped_; } /* columns left out: not numbers, or a byte array of the map */
	const QVector<fast::Recording> &fastRecordings() const { return fast_; }

signals:
	void logged(int level, const QString &text); /* LogLevel; the main window's Log */

private:
	void feed();                         /* every plotted line and math line again from the file's values */
	void feedColumn(int column);         /* one column's samples onto its line */
	void rebuildLinesMenu();
	void saveNotes();
	bool roomForLine(QAction *tick);     /* under the chart's cap of lines; if not, the tick taken back */

	QString file_;
	recording::Data data_;
	QVector<RegDef> defs_;
	QVector<RegDef> map_;
	QVector<bool> plotted_;
	int ramMB_ = 0;
	int skipped_ = 0;
	QVector<fast::Recording> fast_;
	double t0_ = 0, t1_ = 0;
	ChartTab *tab_ = nullptr;
	QLabel *info_ = nullptr;
	QPushButton *lines_ = nullptr;
	bool feeding_ = false;
};

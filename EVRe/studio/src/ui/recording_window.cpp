/* SPDX-License-Identifier: Apache-2.0 */
/* A recording opened: see recording_window.h. */
#include "ui/recording_window.h"

#include <QCheckBox>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolTip>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "model/fast_store.h"
#include "model/register_model.h"
#include "ui/chart_tab.h"
#include "ui/chart_widget.h"
#include "ui/event_log.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

constexpr qint64 SAMPLES_PER_FEED = 500000; /* the file's values go to the chart in parts of about this many */
constexpr double STREAM_END_SAID_S = 0.001; /* a stream's samples that end this much before the CSV's rows: said */
const QString RECENT_KEY = QStringLiteral("recording/recent");
const QString HIDDEN_KEY = QStringLiteral("recording/linesHidden"); /* the lines unticked in Lines, by name */
constexpr int LINES_LIST_ROWS = 18;    /* the Lines list scrolls beyond this many rows (lines and group titles) */
constexpr int LINES_LIST_MAX_H = 440;  /* then this high */
enum LineKind { RegisterLine, ChannelLine, FormulaLine }; /* a line of the Lines list: a column, a fast channel, a math line */

/* a line's dot in the Lines list: its colour on the chart, or a ring while it is not on it (it takes the palette's next
 * colour when ticked, as on the live chart) */
QIcon lineDot(const QColor &color, bool onChart, qreal ratio) {
	QPixmap pixmap(QSize(10, 10) * ratio);
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	if (onChart) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(color);
		painter.drawEllipse(QRectF(1, 1, 8, 8));
	} else {
		painter.setPen(QPen(Theme::colors().muted, 1.2));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(QRectF(1.5, 1.5, 7, 7));
	}
	return QIcon(pixmap);
}

QList<QPointer<RecordingWindow>> &registry() {
	static QList<QPointer<RecordingWindow>> all;
	return all;
}

/* a recording read on a thread: shared with it */
struct LoadJob {
	std::atomic<bool> cancel{ false }, done{ false };
	std::atomic<int> permille{ 0 };
	bool ok = false;
	recording::Data data;
	QVector<fast::Recording> fast;
	QString error;
};

bool isFastRecording(const QString &file) { return file.endsWith(QLatin1String(".evrs"), Qt::CaseInsensitive); }

/* a count in groups of three, "1 024 000", one left-to-right number in a right-to-left line too (as the sidebar's) */
QString groupedNumber(qint64 n) {
	QString count = QString::number(n);
	for (int at = int(count.size()) - 3; at > (count.startsWith(QLatin1Char('-')) ? 1 : 0); at -= 3) count.insert(at, QChar(0x00A0));
	if (count.size() > 3) count = QChar(0x2066) + count + QChar(0x2069);
	return count;
}

QString clockText(qint64 epochMs, double t, const QString &format) {
	return QDateTime::fromMSecsSinceEpoch(epochMs + qint64(std::llround(t * 1000))).toString(format);
}

} // namespace

/* ------------------------------------------------------------------ opening */

void RecordingWindow::open(QWidget *dialogParent, const QString &file, const QVector<RegDef> &map, int ramMB,
		const std::function<void(RecordingWindow *)> &done) {
	const QString where = QDir::toNativeSeparators(file);
	recording::Estimate estimate;
	QString error;
	/* a fast stream's recording alone, or the CSV with the streams' recordings beside it */
	const bool alone = isFastRecording(file);
	const QStringList fastFiles = alone ? QStringList{ file } : fast::recordingsBeside(file);
	if (alone) {
		estimate = recording::Estimate();
	} else if (!recording::estimate(file, estimate, error)) {
		QMessageBox::warning(dialogParent, tr("Cannot open the recording"), tr("%1:\n%2").arg(where, error));
		return;
	}
	/* more than the RAM: the last part, if the user wants it */
	const double needed = double(estimate.samples()) * (ChartView::BYTES_PER_SAMPLE + KEPT_BYTES_PER_SAMPLE);
	const double budget = double(ramMB) * 1024 * 1024;
	qint64 from = 0;
	if (needed > budget) {
		const double part = budget / needed;
		const double span = std::max(0.0, estimate.lastTime - estimate.firstTime);
		QMessageBox ask(QMessageBox::Question, tr("Open recording"),
				tr("%1 holds about %2 samples: about %3 MB of memory, more than the chart's RAM (%4 MB).\n\n"
						"Keep the last part: about the last %5 of %6?")
						.arg(QFileInfo(file).fileName()).arg(estimate.samples())
						.arg(std::llround(needed / (1024 * 1024))).arg(ramMB)
						.arg(durationText(span * part), durationText(span)),
				QMessageBox::Cancel, dialogParent);
		QPushButton *keep = ask.addButton(tr("Keep the last part"), QMessageBox::AcceptRole);
		ask.setDefaultButton(keep);
		ask.exec();
		if (ask.clickedButton() != keep) return;
		from = estimate.headerBytes + qint64((1 - part) * double(estimate.bytes - estimate.headerBytes));
	}

	/* read on a thread; the dialog follows it and makes the window at the end */
	auto job = std::make_shared<LoadJob>();
	auto *progress = new QProgressDialog(tr("Reading %1…").arg(QFileInfo(file).fileName()), tr("Cancel"), 0, 1000,
			dialogParent);
	progress->setObjectName(QStringLiteral("recordingProgress"));
	progress->setWindowTitle(tr("Open recording"));
	progress->setWindowModality(Qt::WindowModal);
	progress->setMinimumDuration(300);
	progress->setAutoClose(false);
	progress->setAutoReset(false);
	QObject::connect(progress, &QProgressDialog::canceled, progress, [job] { job->cancel = true; });
	QObject::connect(progress, &QObject::destroyed, [job] { job->cancel = true; }); /* its window went first */
	QThreadPool::globalInstance()->start([job, file, from, alone, fastFiles] {
		/* the CSV's part of the bar, then each stream's recording an equal part */
		const double parts = double(fastFiles.size() + (alone ? 0 : 1));
		job->ok = alone || recording::read(file, from, job->cancel,
						[&job, parts](double part) { job->permille = int(part * 1000 / parts); }, job->data, job->error);
		for (qsizetype i = 0; job->ok && i < fastFiles.size(); i++) {
			fast::Recording one;
			const double before = double(i + (alone ? 0 : 1));
			job->ok = fast::readRecording(fastFiles[i], job->cancel,
					[&job, before, parts](double part) { job->permille = int((before + part) * 1000 / parts); }, one,
					job->error);
			if (!job->ok && !job->error.isEmpty())
				job->error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(fastFiles[i]), job->error);
			if (job->ok) job->fast << std::move(one);
		}
		job->done = true;
	});
	auto *follow = new QTimer(progress);
	QObject::connect(follow, &QTimer::timeout, progress, [=] {
		progress->setValue(job->permille.load());
		if (!job->done.load()) return;
		follow->stop();
		progress->deleteLater();
		if (!job->ok) {
			if (!job->cancel.load())
				QMessageBox::warning(dialogParent, tr("Cannot open the recording"), tr("%1:\n%2").arg(where, job->error));
			return;
		}
		auto *window = new RecordingWindow(file, std::move(job->data), map, ramMB, std::move(job->fast));
		window->show();
		remember(file);
		if (done) done(window);
	});
	follow->start(50);
}

void RecordingWindow::choose(QWidget *dialogParent, const QVector<RegDef> &map, int ramMB,
		const std::function<void(RecordingWindow *)> &done) {
	const QStringList recent = recentFiles();
	const QString start = recent.isEmpty() ? QDir::homePath() : QFileInfo(recent.first()).absolutePath();
	const QString file = QFileDialog::getOpenFileName(dialogParent, tr("Open recording"), start,
			tr("Recordings (*.csv *.evrs);;All files (*)"));
	if (!file.isEmpty()) open(dialogParent, file, map, ramMB, done);
}

QStringList RecordingWindow::recentFiles() { return QSettings().value(RECENT_KEY).toStringList(); }

void RecordingWindow::remember(const QString &file) {
	const QString path = QFileInfo(file).absoluteFilePath();
	QStringList recent = recentFiles();
	recent.removeAll(path);
	recent.prepend(path);
	while (recent.size() > MAX_RECENT) recent.removeLast();
	QSettings().setValue(RECENT_KEY, recent);
}

void RecordingWindow::forget(const QString &file) {
	QStringList recent = recentFiles();
	recent.removeAll(file);
	QSettings().setValue(RECENT_KEY, recent);
}

/* each entry checked as the menu opens: a file deleted or moved since is greyed with "(not found)" but still takes a
 * click (a disabled entry did nothing, and said nothing), which takes it off the list */
void RecordingWindow::fillRecentMenu(QMenu *menu, const std::function<void(const QString &)> &openFile,
		const std::function<void(const QString &)> &said) {
	menu->clear();
	const QStringList recent = recentFiles();
	if (recent.isEmpty()) menu->addAction(tr("No recordings opened yet"))->setEnabled(false);
	for (const QString &path : recent) {
		const QFileInfo info(path);
		const QString where = QDir::toNativeSeparators(info.absolutePath());
		if (info.exists()) {
			QAction *action = menu->addAction(noMnemonic(QStringLiteral("%1   %2").arg(info.fileName(), where)));
			action->setToolTip(QDir::toNativeSeparators(path));
			QObject::connect(action, &QAction::triggered, menu, [openFile, path] { openFile(path); });
			continue;
		}
		/* a button in the menu's item shape, in the muted colour, lit under the mouse (theme.cpp) */
		auto *missing = new QPushButton(noMnemonic(tr("%1 (not found)   %2").arg(info.fileName(), where)));
		missing->setObjectName(QStringLiteral("recentMissing"));
		missing->setProperty("path", path);
		missing->setCursor(Qt::PointingHandCursor);
		missing->setToolTip(tr("%1 is not there any more (deleted or moved): a click takes it off this list")
				.arg(QDir::toNativeSeparators(path)));
		auto *action = new QWidgetAction(menu);
		action->setDefaultWidget(missing);
		menu->addAction(action);
		QObject::connect(missing, &QPushButton::clicked, menu, [menu, path, said] {
			forget(path);
			menu->close();
			if (said) said(tr("%1 taken off the recent recordings: the file is not there any more")
					.arg(QDir::toNativeSeparators(path)));
		});
	}
	if (!recent.isEmpty()) {
		menu->addSeparator();
		QAction *clear = menu->addAction(tr("Clear the list"));
		clear->setObjectName(QStringLiteral("recentClear"));
		clear->setToolTip(tr("Every recording off this list; the files stay where they are"));
		QObject::connect(clear, &QAction::triggered, menu, [said] {
			QSettings().remove(RECENT_KEY);
			if (said) said(tr("The recent recordings list cleared"));
		});
	}
	menu->setToolTipsVisible(true);
}

QList<RecordingWindow *> RecordingWindow::windows() {
	QList<RecordingWindow *> out;
	for (const QPointer<RecordingWindow> &window : std::as_const(registry()))
		if (window) out << window.data();
	return out;
}

void RecordingWindow::closeAll() {
	for (RecordingWindow *window : windows()) delete window;
}

/* ------------------------------------------------------------------ the window */

RecordingWindow::RecordingWindow(const QString &file, recording::Data data, const QVector<RegDef> &map, int ramMB,
		QVector<fast::Recording> fast)
	: file_(file), data_(std::move(data)), map_(map), ramMB_(ramMB), skipped_(data_.skipped), fast_(std::move(fast)) {
	setAttribute(Qt::WA_DeleteOnClose);
	setObjectName(QStringLiteral("recordingWindow"));
	registry() << this;

	/* a line per column: the map's register of that name (its value names and fields), or one made from the title;
	 * a byte array of the map is no line. Keyed by the column's place: the file's values are no device's */
	QVector<recording::Column> kept;
	for (recording::Column &column : data_.columns) {
		RegDef def;
		bool matched = false;
		for (const RegDef &reg : map_) {
			if (reg.name != column.name) continue;
			def = reg;
			matched = true;
			break;
		}
		if (matched && !def.isNumeric()) {
			skipped_++;
			continue;
		}
		if (!matched) {
			def = RegDef();
			def.name = column.name;
			def.type = RegType::F32;
			def.size = 4;
		}
		def.unit = column.unit; /* as recorded */
		def.slave = 1;
		def.addr = uint16_t(kept.size());
		def.plottable = true;
		defs_ << def;
		kept << std::move(column);
	}
	data_.columns = std::move(kept);
	t0_ = std::numeric_limits<double>::infinity();
	t1_ = -t0_;
	for (const recording::Column &column : std::as_const(data_.columns)) {
		if (column.times.isEmpty()) continue;
		t0_ = std::min(t0_, column.times.first());
		t1_ = std::max(t1_, column.times.last());
	}
	for (const fast::Recording &stream : std::as_const(fast_)) {
		if (!stream.store || stream.store->size() == 0) continue;
		t0_ = std::min(t0_, stream.firstTime);
		t1_ = std::max(t1_, stream.lastTime);
	}
	if (t0_ > t1_) t0_ = t1_ = 0;
	/* a stream's recording alone: its clock from its head (the writer's clock at its start) */
	if (data_.columns.isEmpty() && data_.rows == 0 && !fast_.isEmpty()) data_.epochMs = fast_.first().epochMs();

	/* the title: the file and its span */
	const QString span = tr("%1 – %2 (%3)").arg(clockText(data_.epochMs, t0_, QStringLiteral("yyyy-MM-dd HH:mm:ss")),
			clockText(data_.epochMs, t1_, QStringLiteral("HH:mm:ss")), durationText(t1_ - t0_));
	setWindowTitle(tr("%1 · %2 — EVRe Studio").arg(QFileInfo(file_).fileName(), span));
	info_ = mutedLabel(QString());
	info_->setObjectName(QStringLiteral("recordingInfo"));
	QString info = isFastRecording(file_) ? span : tr("%1 · %2 lines · %3 rows").arg(span).arg(defs_.size()).arg(data_.rows);
	if (skipped_ > 0) info += tr(" · columns left out (not numbers): %1").arg(skipped_);
	/* the CSV's rows and each stream's samples end where each was written last: the rows at each poll, a stream's blocks
	 * as they come, so the two stop a few ms apart (the device sends its newest samples in its next block). Said, so a
	 * fast line that ends before the view's end at a short window is read as the data's end, not as a line cut */
	double rows0 = std::numeric_limits<double>::infinity(), rows1 = -rows0;
	for (const recording::Column &column : std::as_const(data_.columns)) {
		if (column.times.isEmpty()) continue;
		rows0 = std::min(rows0, column.times.first());
		rows1 = std::max(rows1, column.times.last());
	}
	const QString clock = QStringLiteral("HH:mm:ss.zzz");
	QString tip = QDir::toNativeSeparators(file_);
	if (rows0 <= rows1)
		tip += QLatin1Char('\n') + tr("The CSV's rows: %1 – %2").arg(clockText(data_.epochMs, rows0, clock),
				clockText(data_.epochMs, rows1, clock));
	for (const fast::Recording &stream : std::as_const(fast_)) { /* each stream: its samples, the lost, a cut */
		const qint64 records = stream.store ? stream.store->size() : 0;
		info += stream.lost > 0 ? tr(" · %1: %2 samples, %3 lost").arg(stream.stream.name, groupedNumber(records),
												groupedNumber(qint64(stream.lost)))
								: tr(" · %1: %2 samples").arg(stream.stream.name, groupedNumber(records));
		if (rows0 <= rows1 && records > 0 && rows1 - stream.lastTime >= STREAM_END_SAID_S)
			info += tr(", its last %1 before the CSV's last row").arg(secondsText(rows1 - stream.lastTime));
		if (stream.cut) info += tr(" (the file ends cut off: read up to its last whole piece)");
		if (records > 0)
			tip += QLatin1Char('\n') + tr("%1's samples (%2): %3 – %4").arg(stream.stream.name,
					QFileInfo(stream.file).fileName(), clockText(data_.epochMs, stream.firstTime, clock), clockText(data_.epochMs, stream.lastTime, clock));
	}
	if (rows0 <= rows1 && !fast_.isEmpty())
		tip += QLatin1Char('\n') + tr("The rows are written at each poll, a stream's blocks as they come: the two end a "
				"few ms apart, and a fast line ends where its samples end.");
	info_->setText(info);
	info_->setToolTip(tip);
	lines_ = new QPushButton(tr("Lines"));
	lines_->setObjectName(QStringLiteral("recordingLines"));
	lines_->setCursor(Qt::PointingHandCursor);
	lines_->setToolTip(tr("What the chart shows: every column of the file, each fast channel and the math lines, ticked "
			"on or off (All, None, a search); a register's bit fields with the map loaded. The lines unticked stay "
			"off in the next recording opened"));
	auto *menu = new QMenu(lines_);
	setButtonMenu(lines_, menu);
	connect(menu, &QMenu::aboutToShow, this, &RecordingWindow::rebuildLinesMenu);

	tab_ = new ChartTab([end = t1_] { return end; }, this, QStringLiteral("recording"));
	auto *top = new QHBoxLayout;
	top->addWidget(info_, 1);
	top->addWidget(lines_);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 12, 16, 12);
	layout->addLayout(top);
	layout->addWidget(tab_, 1);
	resize(1280, 800);

	tab_->setRecording(data_.epochMs, t0_, t1_, ramMB_, int(defs_.size()));
	tab_->setRegisters(defs_);
	plotted_.fill(false, defs_.size());
	hidden_ = QSettings().value(HIDDEN_KEY).toStringList();
	int lines = 0;
	for (int c = 0; c < defs_.size() && lines < RegisterModel::MAX_PLOTTED; c++) {
		if (hidden_.contains(defs_[c].name)) continue;
		plotted_[c] = true;
		tab_->plotRegister(defs_[c], true);
		lines++;
	}
	/* the streams' lines: their stores laid in, each channel a line while there is room */
	QVector<StreamDef> streams;
	for (const fast::Recording &stream : std::as_const(fast_)) streams << stream.stream;
	tab_->setFastStreams(streams);
	for (int i = 0; i < fast_.size(); i++) {
		tab_->view()->setFastStore(i, fast_[i].store);
		for (int c = 0; c < fast_[i].stream.channels.size() && lines < RegisterModel::MAX_PLOTTED; c++) {
			if (hidden_.contains(fast_[i].stream.name + QLatin1Char('.') + fast_[i].stream.channels[c].name)) continue;
			tab_->plotFastChannel(i, c, true);
			lines++;
		}
	}
	feed();
	tab_->showSpan(t0_, t1_);
	QVector<ChartNote> notes;
	QString error;
	if (recording::loadNotes(file_, notes, error)) tab_->view()->setNotes(notes);
	else if (!error.isEmpty()) info_->setText(info_->text() + tr(" · notes not read: %1").arg(error));

	/* a math line or a field added: its points from the file's values (its line made again empty) */
	connect(tab_, &ChartTab::mathRegistersChanged, this, [this] {
		if (!feeding_) feed();
		showLinesCount();
	});
	showLinesCount();
	connect(tab_, &ChartTab::notesChanged, this, &RecordingWindow::saveNotes);
	connect(tab_, &ChartTab::statusMessage, this,
			[this](const QString &text, int) { QToolTip::showText(QCursor::pos(), text, this); });
	connect(tab_, &ChartTab::logged, this, [this](LogLevel level, const QString &text) { emit logged(int(level), text); });
	connect(tab_, &ChartTab::openRecordingRequested, this, [this](const QString &other) {
		if (other.isEmpty()) choose(this, map_, ramMB_);
		else open(this, other, map_, ramMB_);
	});
	tab_->setShown(true);
	/* the keys to the chart (Delete removes a note clicked), not a text cursor blinking in the Window box */
	tab_->view()->setFocus();
	/* the info line and the Y boxes as the main window's status keeps the live chart's */
	auto *status = new QTimer(this);
	connect(status, &QTimer::timeout, tab_, &ChartTab::refreshStatus);
	status->start(500);
	tab_->refreshStatus();
}

RecordingWindow::~RecordingWindow() {
	registry().removeAll(QPointer<RecordingWindow>(this));
	registry().removeAll(QPointer<RecordingWindow>());
}

/* every column (the math lines read the unplotted ones too) in parts of time, so a poll's values stay together for the
 * math lines and no copy of the whole file is made at once */
void RecordingWindow::feed() {
	feeding_ = true;
	tab_->view()->clearData();
	qint64 total = 0;
	for (const recording::Column &column : std::as_const(data_.columns)) total += column.times.size();
	const qint64 parts = std::max<qint64>(1, total / SAMPLES_PER_FEED);
	QVector<qsizetype> next(data_.columns.size(), 0);
	for (qint64 k = 1; k <= parts; k++) {
		const double end = k == parts ? std::numeric_limits<double>::infinity() : t0_ + (t1_ - t0_) * double(k) / double(parts);
		MathLines::Samples samples;
		for (qsizetype c = 0; c < data_.columns.size(); c++) {
			const recording::Column &column = data_.columns[c];
			QVector<QPointF> points;
			for (; next[c] < column.times.size() && column.times[next[c]] < end; next[c]++)
				points << QPointF(column.times[next[c]], column.values[next[c]]);
			if (!points.isEmpty()) samples.insert(regKey(defs_[c]), points);
		}
		tab_->frame(samples);
	}
	tab_->fillFastMath(); /* a fast math line: from its stream's records, its registers held from the samples above */
	tab_->view()->showLastValues();
	feeding_ = false;
}

void RecordingWindow::feedColumn(int column) {
	const recording::Column &values = data_.columns[column];
	const int key = int(regKey(defs_[column]));
	for (qsizetype i = 0; i < values.times.size(); i++) tab_->view()->append(key, values.times[i], values.values[i]);
	tab_->view()->showLastValues();
}

/* The Lines list: a check box per line, under its group (the file's columns, each stream's channels, the math lines),
 * with its colour dot and unit; All and None for the lines listed, a search when they are many. A check box in a menu:
 * a tick does not close it. The fields of a register matched in the map under it, as on the Registers tab */
void RecordingWindow::rebuildLinesMenu() {
	QMenu *menu = lines_->menu();
	menu->clear();
	auto *panel = new QWidget(menu); /* in the menu from the start: its style sheet rules (QMenu ...) size it */
	panel->setObjectName(QStringLiteral("recordingLinesList"));
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(10, 6, 10, 6);
	layout->setSpacing(6);
	auto *all = new QPushButton(tr("All"));
	all->setObjectName(QStringLiteral("recordingLinesAll"));
	all->setToolTip(tr("Tick every line listed (those the search finds), up to the chart's 64"));
	auto *none = new QPushButton(tr("None"));
	none->setObjectName(QStringLiteral("recordingLinesNone"));
	none->setToolTip(tr("Untick every line listed (those the search finds)"));
	for (QPushButton *button : { all, none }) button->setCursor(Qt::PointingHandCursor);
	auto *buttons = new QHBoxLayout;
	buttons->addWidget(all);
	buttons->addWidget(none);
	buttons->addStretch();
	layout->addLayout(buttons);
	auto *search = new QLineEdit;
	search->setObjectName(QStringLiteral("recordingLinesSearch"));
	search->setPlaceholderText(tr("Search lines"));
	search->setToolTip(tr("Only the lines whose name holds this"));
	search->setClearButtonEnabled(true);
	search->setVisible(linesInFile() >= LINES_SEARCH_FROM);
	layout->addWidget(search);

	auto *list = new QWidget;
	auto *rows = new QVBoxLayout(list);
	rows->setContentsMargins(0, 0, 0, 0);
	rows->setSpacing(2);
	struct Group {
		QLabel *title;
		QVector<QCheckBox *> boxes;
	};
	auto groups = std::make_shared<QVector<Group>>();
	QHash<int, QColor> colors;
	for (const ChartView::Info &line : tab_->view()->lines()) colors.insert(line.key, line.color);
	const qreal ratio = devicePixelRatioF();
	const auto addGroup = [&](const QString &title) {
		if (!groups->isEmpty()) rows->addSpacing(6); /* a gap between the groups */
		auto *label = new QLabel(title);
		label->setObjectName(QStringLiteral("linesGroup"));
		rows->addWidget(label);
		groups->push_back({ label, {} });
	};
	const auto addBox = [&](const QString &name, const QString &unit, LineKind kind, int index, int key, bool on,
								const QString &why) {
		auto *box = new QCheckBox(noMnemonic(recording::title(name, unit)));
		box->setObjectName(QStringLiteral("recordingLine"));
		box->setProperty("lineName", name);
		box->setProperty("lineKind", int(kind));
		box->setProperty("lineIndex", index);
		box->setProperty("lineKey", key);
		box->setCursor(Qt::PointingHandCursor);
		box->setIcon(lineDot(colors.value(key), colors.contains(key), ratio));
		box->setChecked(on);
		box->setEnabled(why.isEmpty());
		box->setToolTip(why.isEmpty() ? tr("%1 on the chart, or not").arg(name) : why);
		box->setContentsMargins(4, 1, 4, 1);
		connect(box, &QCheckBox::toggled, this, [this, box](bool ticked) { lineTicked(box, ticked); });
		rows->addWidget(box);
		groups->last().boxes << box;
	};
	if (!defs_.isEmpty()) addGroup(tr("Registers"));
	for (int c = 0; c < defs_.size(); c++)
		addBox(defs_[c].name, defs_[c].unit, RegisterLine, c, int(regKey(defs_[c])), plotted_[c], QString());
	for (int i = 0; i < fast_.size(); i++) {
		addGroup(tr("Fast: %1").arg(fast_[i].stream.name));
		for (int c = 0; c < fast_[i].stream.channels.size(); c++) {
			const StreamChannel &channel = fast_[i].stream.channels[c];
			addBox(fast_[i].stream.name + QLatin1Char('.') + channel.name, channel.unit, ChannelLine, 256 * i + c,
					ChartView::fastKey(i, c), tab_->fastPlotted(i, c), QString());
		}
	}
	const QVector<MathLine> &math = tab_->mathLines().lines();
	if (!math.isEmpty()) addGroup(tr("Math"));
	for (int i = 0; i < math.size(); i++)
		addBox(math[i].name, math[i].unit, FormulaLine, i, math[i].fast() ? ChartTab::fastMathKey(i) : MathLines::chartKey(i),
				math[i].active(), math[i].error.isEmpty() ? QString() : tr("Not drawn: %1").arg(math[i].error));
	rows->addStretch();
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("recordingLinesScroll"));
	scroll->setWidget(list);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->viewport()->setAutoFillBackground(false);
	list->setAutoFillBackground(false);
	/* as tall as its lines (measured when the menu lays it out, in the menu's style), or LINES_LIST_MAX_H with its scroll
	 * bar beside them, never over them, when they are many */
	int rowsShown = int(groups->size());
	for (const Group &group : std::as_const(*groups)) rowsShown += int(group.boxes.size());
	const bool scrolls = rowsShown > LINES_LIST_ROWS;
	scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
	scroll->setVerticalScrollBarPolicy(scrolls ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
	if (scrolls) scroll->setMaximumHeight(LINES_LIST_MAX_H);
	layout->addWidget(scroll);

	/* the search: the lines whose name holds it, a group's title only with a line under it */
	connect(search, &QLineEdit::textChanged, panel, [groups](const QString &text) {
		for (Group &group : *groups) {
			bool any = false;
			for (QCheckBox *box : std::as_const(group.boxes)) {
				const bool match = text.isEmpty() || box->property("lineName").toString().contains(text, Qt::CaseInsensitive);
				box->setHidden(!match);
				any = any || match;
			}
			group.title->setHidden(!any);
		}
	});
	/* All: in their order until the cap refuses one; None: every one listed */
	connect(all, &QPushButton::clicked, panel, [groups] {
		for (const Group &group : std::as_const(*groups))
			for (QCheckBox *box : group.boxes) {
				if (box->isHidden() || !box->isEnabled() || box->isChecked()) continue;
				box->setChecked(true);
				if (!box->isChecked()) return; /* the chart is full */
			}
	});
	connect(none, &QPushButton::clicked, panel, [groups] {
		for (const Group &group : std::as_const(*groups))
			for (QCheckBox *box : group.boxes)
				if (!box->isHidden() && box->isChecked()) box->setChecked(false);
	});
	auto *action = new QWidgetAction(menu);
	action->setDefaultWidget(panel);
	menu->addAction(action);

	/* the fields of a register matched in the map, as on the Registers tab (not of a scaled one: no raw bits) */
	bool title = false;
	for (const RegDef &def : std::as_const(defs_)) {
		if (def.fields.isEmpty() || def.scale != 1.0 || def.offset != 0.0) continue;
		if (!title) {
			menu->addSeparator();
			title = true;
		}
		QMenu *fields = menu->addMenu(noMnemonic(tr("Fields of %1").arg(def.name)));
		for (const BitField &field : def.fields)
			fields->addAction(noMnemonic(field.name), this, [this, def, field] { tab_->plotField(def, field); });
	}
	QTimer::singleShot(0, search, [search] {
		if (search->isVisible()) search->setFocus();
	});
}

/* a line ticked on or off in the list: on the chart (a column's samples fed from the file), its dot in its colour now,
 * the choice kept by name (a math line keeps its own, in recording/math) */
void RecordingWindow::lineTicked(QCheckBox *box, bool on) {
	if (on && !roomForLine(box)) return;
	const int index = box->property("lineIndex").toInt();
	const QString name = box->property("lineName").toString();
	switch (LineKind(box->property("lineKind").toInt())) {
	case RegisterLine:
		plotted_[index] = on;
		tab_->plotRegister(defs_[index], on);
		if (on) feedColumn(index);
		break;
	case ChannelLine:
		tab_->plotFastChannel(index / 256, index % 256, on);
		break;
	case FormulaLine:
		if (!tab_->setMathLineShown(index, on)) {
			const QSignalBlocker blocker(box);
			box->setChecked(!on);
		}
		break;
	}
	if (LineKind(box->property("lineKind").toInt()) != FormulaLine) {
		hidden_.removeAll(name);
		if (!on) hidden_ << name;
		QSettings().setValue(HIDDEN_KEY, hidden_);
	}
	const int key = box->property("lineKey").toInt();
	QColor color;
	bool onChart = false;
	for (const ChartView::Info &line : tab_->view()->lines())
		if (line.key == key) {
			color = line.color;
			onChart = true;
		}
	box->setIcon(lineDot(color, onChart, devicePixelRatioF()));
	showLinesCount();
}

int RecordingWindow::linesInFile() const {
	int n = int(defs_.size()) + int(tab_->mathLines().lines().size());
	for (const fast::Recording &stream : fast_) n += int(stream.stream.channels.size());
	return n;
}

void RecordingWindow::showLinesCount() {
	lines_->setText(tr("Lines %1/%2").arg(tab_->lineCount()).arg(linesInFile()));
}

/* one cap for every line, as on the live chart: a line past it is refused, the list's tick taken back, and why said
 * beside the mouse (this window has no status bar) */
bool RecordingWindow::roomForLine(QAbstractButton *tick) {
	if (tab_->lineCount() < RegisterModel::MAX_PLOTTED) return true;
	const QSignalBlocker blocker(tick);
	tick->setChecked(false);
	QToolTip::showText(QCursor::pos(), ChartTab::lineCapText(), this);
	return false;
}

void RecordingWindow::saveNotes() {
	QString error;
	if (recording::saveNotes(file_, tab_->view()->notes(), data_.epochMs, error)) return;
	emit logged(int(LogLevel::Warning), tr("notes not saved beside %1: %2").arg(QDir::toNativeSeparators(file_), error));
}

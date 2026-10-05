/* SPDX-License-Identifier: Apache-2.0 */
/* A recording opened: see recording_window.h. */
#include "ui/recording_window.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "model/register_model.h"
#include "ui/chart_tab.h"
#include "ui/chart_widget.h"
#include "ui/event_log.h"
#include "ui/ui_helpers.h"

namespace {

constexpr qint64 SAMPLES_PER_FEED = 500000; /* the file's values go to the chart in parts of about this many */
const QString RECENT_KEY = QStringLiteral("recording/recent");

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
	QString error;
};

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
	if (!recording::estimate(file, estimate, error)) {
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
	QThreadPool::globalInstance()->start([job, file, from] {
		job->ok = recording::read(file, from, job->cancel, [&job](double part) { job->permille = int(part * 1000); },
				job->data, job->error);
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
		auto *window = new RecordingWindow(file, std::move(job->data), map, ramMB);
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
			tr("Recordings (*.csv);;All files (*)"));
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

void RecordingWindow::fillRecentMenu(QMenu *menu, const std::function<void(const QString &)> &openFile) {
	menu->clear();
	const QStringList recent = recentFiles();
	if (recent.isEmpty()) menu->addAction(tr("No recordings opened yet"))->setEnabled(false);
	for (const QString &path : recent) {
		const QFileInfo info(path);
		QAction *action = menu->addAction(noMnemonic(QStringLiteral("%1   %2").arg(info.fileName(),
				QDir::toNativeSeparators(info.absolutePath()))));
		action->setToolTip(QDir::toNativeSeparators(path));
		action->setEnabled(info.exists());
		QObject::connect(action, &QAction::triggered, menu, [openFile, path] { openFile(path); });
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

RecordingWindow::RecordingWindow(const QString &file, recording::Data data, const QVector<RegDef> &map, int ramMB)
	: file_(file), data_(std::move(data)), map_(map), ramMB_(ramMB), skipped_(data_.skipped) {
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
	if (t0_ > t1_) t0_ = t1_ = 0;

	/* the title: the file and its span */
	const QString span = tr("%1 – %2 (%3)").arg(clockText(data_.epochMs, t0_, QStringLiteral("yyyy-MM-dd HH:mm:ss")),
			clockText(data_.epochMs, t1_, QStringLiteral("HH:mm:ss")), durationText(t1_ - t0_));
	setWindowTitle(tr("%1 · %2 — EVRe Studio").arg(QFileInfo(file_).fileName(), span));
	info_ = mutedLabel(QString());
	info_->setObjectName(QStringLiteral("recordingInfo"));
	QString info = tr("%1 · %2 lines · %3 rows").arg(span).arg(defs_.size()).arg(data_.rows);
	if (skipped_ > 0) info += tr(" · columns left out (not numbers): %1").arg(skipped_);
	info_->setText(info);
	info_->setToolTip(QDir::toNativeSeparators(file_));
	lines_ = new QPushButton(tr("Lines"));
	lines_->setObjectName(QStringLiteral("recordingLines"));
	lines_->setToolTip(tr("The file's columns on the chart or not; a register's bit fields (with the map loaded)"));
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
	for (int c = 0; c < defs_.size() && c < RegisterModel::MAX_PLOTTED; c++) {
		plotted_[c] = true;
		tab_->plotRegister(defs_[c], true);
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
	});
	connect(tab_, &ChartTab::notesChanged, this, &RecordingWindow::saveNotes);
	connect(tab_, &ChartTab::logged, this, [this](LogLevel level, const QString &text) { emit logged(int(level), text); });
	connect(tab_, &ChartTab::openRecordingRequested, this, [this](const QString &other) {
		if (other.isEmpty()) choose(this, map_, ramMB_);
		else open(this, other, map_, ramMB_);
	});
	tab_->setShown(true);
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
	tab_->view()->showLastValues();
	feeding_ = false;
}

void RecordingWindow::feedColumn(int column) {
	const recording::Column &values = data_.columns[column];
	const int key = int(regKey(defs_[column]));
	for (qsizetype i = 0; i < values.times.size(); i++) tab_->view()->append(key, values.times[i], values.values[i]);
	tab_->view()->showLastValues();
}

void RecordingWindow::rebuildLinesMenu() {
	QMenu *menu = lines_->menu();
	menu->clear();
	for (int c = 0; c < defs_.size(); c++) {
		const RegDef &def = defs_[c];
		QAction *shown = menu->addAction(noMnemonic(recording::title(def.name, def.unit)));
		shown->setCheckable(true);
		shown->setChecked(plotted_[c]);
		connect(shown, &QAction::toggled, this, [this, c](bool on) {
			plotted_[c] = on;
			tab_->plotRegister(defs_[c], on);
			if (on) feedColumn(c);
		});
	}
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
}

void RecordingWindow::saveNotes() {
	QString error;
	if (recording::saveNotes(file_, tab_->view()->notes(), data_.epochMs, error)) return;
	emit logged(int(LogLevel::Warning), tr("notes not saved beside %1: %2").arg(QDir::toNativeSeparators(file_), error));
}

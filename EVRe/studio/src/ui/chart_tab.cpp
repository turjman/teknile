/* SPDX-License-Identifier: Apache-2.0 */
/* The Chart tab: see chart_tab.h. */
#include "ui/chart_tab.h"

#include <QActionGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMetaMethod>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ui/chart_widget.h"
#include "ui/event_log.h"
#include "ui/math_line_dialog.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

/* the shortest Window that can be typed; shorter views are only reached with the wheel */
constexpr double MIN_TYPED_WINDOW = 0.01;
/* the measurements while the cursors move: at most this often (ChartTab::measureSoon) */
constexpr int MEASURE_FOLLOW_MS = 100;

/* a measurement as the table shows it: a few significant digits, "—" for none */
QString measureText(double value) {
	if (!std::isfinite(value)) return QStringLiteral("—");
	const double size = std::fabs(value);
	if (size == 0) return QStringLiteral("0");
	if (size >= 1e6 || size < 1e-3) return QString::number(value, 'g', 5);
	return QString::number(value, 'f', size >= 1000 ? 1 : size >= 100 ? 2 : size >= 1 ? 3 : 4);
}

/* the unit of the area under a line of `unit` over seconds, and over hours */
QString areaUnit(const QString &unit, bool hours) {
	if (unit == QLatin1String("W")) return hours ? QStringLiteral("Wh") : QStringLiteral("J");
	if (unit == QLatin1String("A")) return hours ? QStringLiteral("Ah") : QStringLiteral("A·s");
	if (unit == QLatin1String("mA")) return hours ? QStringLiteral("mAh") : QStringLiteral("mA·s");
	if (unit.isEmpty()) return hours ? QStringLiteral("·h") : QStringLiteral("·s");
	return unit + (hours ? QStringLiteral("·h") : QStringLiteral("·s"));
}

/* the measurement columns' keys, as saved (chart/measureColumns: the hidden ones), in ChartTab::MeasureColumn order */
const char *const MEASURE_KEYS[ChartTab::MEASURE_COLUMNS] = { "line", "atA", "atB", "diff", "min", "max", "mean", "rms",
	"std", "p2p", "area", "areaHours", "total" };

/* this computer's memory, MB; 0: not known */
qint64 physicalMemoryMB() {
#ifdef Q_OS_WIN
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	return GlobalMemoryStatusEx(&status) ? qint64(status.ullTotalPhys / (1024 * 1024)) : 0;
#else
	const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGE_SIZE);
	return pages > 0 && pageSize > 0 ? qint64(pages) * pageSize / (1024 * 1024) : 0;
#endif
}

/* the most the chart's samples may take: three quarters of this computer's memory (16 GB when not known) */
int maxRamMB() {
	const qint64 physical = physicalMemoryMB();
	return int(std::clamp<qint64>(physical > 0 ? physical * 3 / 4 : 16384, ChartView::MIN_RAM_MB, 1 << 20));
}

/* 2048 -> "2 GB", 1536 -> "1536 MB" */
QString ramText(int megabytes) {
	return megabytes % 1024 == 0 ? QStringLiteral("%1 GB").arg(megabytes / 1024) : QStringLiteral("%1 MB").arg(megabytes);
}

/* "3000", "3000 MB", "3 GB", "1.5 GB" -> MB; 0: not a size */
int parseRam(const QString &text) {
	static const QRegularExpression size(QStringLiteral("^\\s*([0-9]+(?:\\.[0-9]+)?)\\s*(mb|gb|m|g)?\\s*$"),
			QRegularExpression::CaseInsensitiveOption);
	const QRegularExpressionMatch m = size.match(text);
	if (!m.hasMatch()) return 0;
	const double value = m.captured(1).toDouble();
	const bool gigabytes = m.captured(2).startsWith(QLatin1Char('g'), Qt::CaseInsensitive);
	return int(std::llround(gigabytes ? value * 1024 : value));
}

/* a box for Window or Memory: the presets, or any length typed */
QComboBox *lengthBox(const QList<double> &presets) {
	auto *box = new QComboBox;
	box->setEditable(true);
	box->setInsertPolicy(QComboBox::NoInsert);
	for (double seconds : presets) box->addItem(secondsText(seconds), seconds);
	box->setMinimumWidth(96);
	return box;
}

} // namespace

ChartTab::ChartTab(std::function<double()> clock, QWidget *parent) : QWidget(parent) {
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 12, 0, 0);
	layout->setSpacing(8);
	/* two rows over the chart: the axes, then what to do */
	layout->addLayout(buildAxesRow());
	layout->addLayout(buildActionsRow());

	chart_ = new ChartWidget;
	/* the chart reads the clock at every frame; its zero, in wall-clock time, for the labels */
	const qint64 epoch = QDateTime::currentMSecsSinceEpoch() - qint64(std::llround(clock() * 1000.0));
	chart_->setClock(std::move(clock), epoch);

	/* the measurements under the chart: the splitter between them moves */
	auto *split = new QSplitter(Qt::Vertical);
	split->addWidget(chart_);
	split->addWidget(buildMeasurements());
	split->setStretchFactor(0, 4);
	split->setStretchFactor(1, 1);
	split->setChildrenCollapsible(false);
	split->setSizes({ 600, 170 });
	layout->addWidget(split, 1);

	connectControls();
	restoreSettings();
	mathLines_.load(); /* compiled and drawn once the map's registers come (setRegisters) */
}

/* ----------------------------------------------------------------- building */

QHBoxLayout *ChartTab::buildAxesRow() {
	/* the view (Window) and what is kept (Memory) */
	window_ = lengthBox({ 1.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0 });
	window_->setToolTip(tr("View: the time shown. Pick one or type any length: 45, 2.5 s, 500 ms, 3 min, 1 h.\n"
			"Mouse wheel on the chart: zoom it (around the mouse when held)."));
	memory_ = lengthBox({ 10.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0, 7200.0 });
	memory_->setToolTip(tr("Memory depth: how much is kept, as on an oscilloscope. The view shows part of it;\n"
			"drag the chart or the memory strip under it to look back, Live to follow now again."));

	/* Y: Auto or Manual */
	yMode_ = new QComboBox;
	yMode_->setObjectName(QStringLiteral("yMode"));
	yMode_->addItem(tr("Auto"));
	yMode_->addItem(tr("Manual"));
	yMode_->addItem(tr("Log"));
	yMode_->setToolTip(tr("Auto: follows what is shown (grows at once, shrinks gently).\n"
			"Manual: the min and max typed here. Ctrl + wheel on the chart zooms Y, a double-click goes back to "
			"Auto.\nLog: a logarithmic scale, a line at each decade: Auto spans the positive values shown (9 decades at "
			"most), or the min and max typed (both above 0); values of 0 or less sit on the bottom edge. Log and "
			"Normalise exclude each other."));
	yMin_ = new QLineEdit;
	yMax_ = new QLineEdit;
	yMin_->setObjectName(QStringLiteral("yMin"));
	yMax_->setObjectName(QStringLiteral("yMax"));
	for (QLineEdit *field : { yMin_, yMax_ }) {
		auto *validator = new QDoubleValidator(field);
		validator->setLocale(QLocale::c());
		field->setValidator(validator);
		field->setFixedWidth(76); /* six digits; narrower keeps the tab's row inside a 1280-wide window */
		field->setAlignment(Qt::AlignRight);
	}
	yMin_->setToolTip(tr("Y range: the bottom of the chart (typing it sets Manual)"));
	yMax_->setToolTip(tr("Y range: the top of the chart (typing it sets Manual)"));
	yMin_->setPlaceholderText(tr("min"));
	yMax_->setPlaceholderText(tr("max"));

	/* it takes the room the row leaves, elided there (refreshStatus): its text changes twice a second
	 * and must not change the tab's minimum width */
	chartInfo_ = mutedLabel(QString());
	chartInfo_->setObjectName(QStringLiteral("chartInfo"));
	chartInfo_->setToolTip(infoTip());
	chartInfo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	chartInfo_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

	ram_ = new QComboBox;
	ram_->setObjectName(QStringLiteral("chartRam"));
	ram_->setEditable(true);
	ram_->setInsertPolicy(QComboBox::NoInsert);
	for (int megabytes : { 512, 1024, 2048, 4096, 8192, 16384 })
		if (megabytes <= maxRamMB()) ram_->addItem(ramText(megabytes), megabytes);
	ram_->setMinimumWidth(88); /* "1536 MB" */
	/* it takes the room the row leaves, elided there (refreshStatus): its text changes and must not move the row */
	ramNeed_ = new QLabel;
	ramNeed_->setObjectName(QStringLiteral("ramNeed"));
	ramNeed_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	ramNeed_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	ramNeed_->setToolTip(tr("The memory the chart's samples need to keep the Memory set, at the rates the lines come "
			"now.\nMore than RAM: the oldest go sooner, and the memory strip says \"memory full\"."));
	ram_->setToolTip(tr("The most memory the chart's samples take, all the lines together (2 GB by default). Pick one "
			"or type any size: 3000, 3000 MB, 3 GB.\nWith many fast lines the Memory holds less than asked, and the "
			"memory strip says \"memory full\". At most three quarters of this computer's memory (%1 GB).")
			.arg(maxRamMB() / 1024.0, 0, 'f', 1));

	/* the first row: what is shown and kept (Window, Memory, RAM and what the lines need), the Y range */
	auto *row = new QHBoxLayout;
	row->setSpacing(6);
	row->addWidget(mutedLabel(tr("Window")));
	row->addWidget(window_);
	row->addSpacing(6);
	row->addWidget(mutedLabel(tr("Memory")));
	row->addWidget(memory_);
	row->addSpacing(6);
	row->addWidget(mutedLabel(tr("RAM")));
	row->addWidget(ram_);
	row->addSpacing(6);
	row->addWidget(ramNeed_, 1);
	row->addSpacing(12);
	row->addWidget(mutedLabel(tr("Y range")));
	row->addWidget(yMode_);
	row->addSpacing(4);
	row->addWidget(mutedLabel(tr("min")));
	row->addWidget(yMin_);
	row->addWidget(mutedLabel(tr("max")));
	row->addWidget(yMax_);
	return row;
}

QHBoxLayout *ChartTab::buildActionsRow() {
	holdButton_ = new QPushButton(tr("Hold"));
	holdButton_->setObjectName(QStringLiteral("hold"));
	holdButton_->setProperty("live", true);
	holdButton_->setIconSize(QSize(14, 14));
	holdButton_->setIcon(mediaIcon(MediaIcon::Pause, Theme::colors().text));
	{
		/* as wide as the longer of its two labels, whatever the state */
		holdButton_->setText(tr("Live"));
		const QSize live = holdButton_->sizeHint();
		holdButton_->setText(tr("Hold"));
		const QSize hold = holdButton_->sizeHint();
		holdButton_->setFixedSize(std::max(live.width(), hold.width()) + 8, std::max(live.height(), hold.height()));
	}
	holdButton_->setToolTip(tr("Hold the view where it is (the memory keeps filling). Live: follow now again.\n"
			"Dragging the chart holds it too."));
	cursorsButton_ = new QPushButton(tr("Cursors"));
	cursorsButton_->setObjectName(QStringLiteral("cursors"));
	cursorsButton_->setCheckable(true);
	cursorsButton_->setToolTip(tr("On: a click on the chart places cursor A, then B; drag them.\n"
			"The measurements under the chart then cover A → B instead of the view."));
	clearCursorsButton_ = new QPushButton(tr("Clear cursors"));
	measureButton_ = new QPushButton(tr("Measure"));
	measureButton_->setObjectName(QStringLiteral("measure"));
	measureButton_->setCheckable(true);
	measureButton_->setToolTip(tr("Show the measurements under the chart: at A / B, min, max, mean, RMS and the area "
			"under\neach line (W -> J / Wh, A -> Ah), over the cursors or the view. Off: nothing is computed."));
	mathButton_ = new QPushButton(tr("ƒ  Math"));
	mathButton_->setObjectName(QStringLiteral("math"));
	mathButton_->setToolTip(tr("Math lines: a formula over registers, e.g. SUPPLY_V * SUPPLY_I (power), drawn and "
			"measured like the others"));
	setButtonMenu(mathButton_, new QMenu(mathButton_));
	clearButton_ = new QPushButton(tr("Clear"));
	clearButton_->setObjectName(QStringLiteral("chartClear"));
	clearButton_->setToolTip(tr("Empty the lines and the memory (they go on from now)"));
	removeAllButton_ = new QPushButton(tr("Remove all"));
	removeAllButton_->setToolTip(tr("Take every register off the chart (untick every Plot)"));
	/* how the lines are drawn, in one menu: Normalise, Smooth, Hover values and who draws them. Ticks and an
	 * adapter's full name in the row would widen it past a 1280-wide window; what is on shows in the chart, the info
	 * line (delay, GPU or CPU) and the button's tooltip */
	displayButton_ = new QPushButton(tr("Display"));
	displayButton_->setObjectName(QStringLiteral("chartDisplay"));
	auto *displayMenu = new QMenu(displayButton_);
	displayMenu->setToolTipsVisible(true);
	normalize_ = displayMenu->addAction(tr("Normalise"));
	normalize_->setObjectName(QStringLiteral("chartNormalise"));
	normalize_->setCheckable(true);
	normalize_->setToolTip(tr("Scale every series to its own range, to compare shapes of different units"));
	smooth_ = displayMenu->addAction(tr("Smooth"));
	smooth_->setObjectName(QStringLiteral("chartSmooth"));
	smooth_->setCheckable(true);
	smooth_->setToolTip(tr("Delay the picture a few ms, as measured from how late samples arrive,\n"
			"so the line always reaches the right edge and scrolls without steps."));
	hoverValues_ = displayMenu->addAction(tr("Hover values"));
	hoverValues_->setObjectName(QStringLiteral("chartHoverValues"));
	hoverValues_->setCheckable(true);
	hoverValues_->setToolTip(tr("The box of every line's value beside the mouse over the chart.\n"
			"Off: the crosshair's line and its dots only (the box covers the cursors' tags)."));
	/* who draws the lines: the adapters found, by name (none without Direct3D). Under a title of its own: a
	 * section's text is not drawn by the style (only its line), and "CPU" alone says little */
	displayMenu->addSeparator();
	auto *drawingTitle = new QWidgetAction(displayMenu);
	auto *drawingLabel = new QLabel(tr("Drawing"));
	drawingLabel->setObjectName(QStringLiteral("menuTitle"));
	drawingTitle->setDefaultWidget(drawingLabel);
	displayMenu->addAction(drawingTitle);
	drawingChoices_ = new QActionGroup(displayMenu);
	auto addChoice = [&](const QString &text, ChartView::Drawing drawing) {
		QAction *action = displayMenu->addAction(text);
		action->setCheckable(true);
		action->setData(int(drawing));
		drawingChoices_->addAction(action);
	};
	addChoice(tr("Auto (a dedicated GPU if there is one, else the CPU)"), ChartView::Drawing::Auto);
	for (const GpuLines::Adapter &adapter : GpuLines::adapters())
		addChoice(adapter.dedicated ? tr("Dedicated GPU: %1").arg(noMnemonic(adapter.name))
						: tr("Internal GPU: %1").arg(noMnemonic(adapter.name)),
				adapter.dedicated ? ChartView::Drawing::Dedicated : ChartView::Drawing::Internal);
	addChoice(tr("CPU"), ChartView::Drawing::Cpu);
	setButtonMenu(displayButton_, displayMenu);
	auto *row = new QHBoxLayout;
	row->setSpacing(6);
	row->addWidget(holdButton_);
	row->addWidget(measureButton_);
	row->addWidget(cursorsButton_);
	row->addWidget(clearCursorsButton_);
	row->addSpacing(12);
	row->addWidget(mathButton_);
	row->addSpacing(12);
	row->addWidget(displayButton_);
	row->addSpacing(12);
	row->addWidget(chartInfo_, 1); /* the room the buttons leave */
	row->addSpacing(12);
	row->addWidget(clearButton_);
	row->addWidget(removeAllButton_);
	return row;
}

QWidget *ChartTab::buildMeasurements() {
	measurePanel_ = new QWidget;
	auto *layout = new QVBoxLayout(measurePanel_);
	layout->setContentsMargins(0, 4, 0, 0);
	layout->setSpacing(4);
	measureInfo_ = mutedLabel(QString());
	measureInfo_->setObjectName(QStringLiteral("measureInfo"));
	measureInfo_->setWordWrap(true);
	layout->addWidget(measureInfo_);
	measures_ = new QTableWidget(0, MEASURE_COLUMNS);
	measures_->setObjectName(QStringLiteral("measures"));
	measures_->setHorizontalHeaderLabels({ tr("Line"), tr("at A"), tr("at B"), tr("B − A"), tr("Min"), tr("Max"),
			tr("Mean"), tr("RMS"), tr("Std dev"), tr("Peak-peak"), tr("Area ∫ dt"), tr("Area / 3600"), tr("Since Clear") });
	measures_->horizontalHeaderItem(ColStd)->setToolTip(tr("The standard deviation over the range, time-weighted as the "
			"mean: √(mean of (value − mean)²). The ripple on a line, whatever its level."));
	measures_->horizontalHeaderItem(ColP2p)->setToolTip(tr("Peak to peak: Max − Min over the range"));
	measures_->horizontalHeaderItem(ColArea)->setToolTip(
			tr("The area under the line over the range: value × seconds (W → J, A → A·s)"));
	measures_->horizontalHeaderItem(ColAreaHours)->setToolTip(tr("The same in hours: W → Wh, A → Ah"));
	measures_->horizontalHeaderItem(ColTotal)->setToolTip(tr("The area under the line since the chart's Clear, in hours "
			"(W → Wh, A → Ah), from every sample as it came: what the memory let go is still in it. A gap of more "
			"than 1 s between two samples is not bridged."));
	/* a right-click on the header: a tick per column, the choice kept */
	measureColumns_ = new QMenu(measures_);
	measureColumns_->setObjectName(QStringLiteral("measureColumns"));
	for (int column = ColAtA; column < MEASURE_COLUMNS; column++) {
		QAction *shown = measureColumns_->addAction(measures_->horizontalHeaderItem(column)->text());
		shown->setCheckable(true);
		shown->setData(column);
		connect(shown, &QAction::toggled, this, [this, column](bool on) {
			measures_->setColumnHidden(column, !on);
			QStringList hidden;
			for (int c = ColAtA; c < MEASURE_COLUMNS; c++)
				if (measures_->isColumnHidden(c)) hidden << QLatin1String(MEASURE_KEYS[c]);
			QSettings().setValue(QStringLiteral("chart/measureColumns"), hidden);
		});
	}
	measures_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(measures_->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint &at) {
		measureColumns_->popup(measures_->horizontalHeader()->mapToGlobal(at));
	});
	measures_->verticalHeader()->hide();
	measures_->verticalHeader()->setDefaultSectionSize(26);
	measures_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	measures_->setSelectionMode(QAbstractItemView::ContiguousSelection);
	measures_->setShowGrid(false);
	measures_->setAlternatingRowColors(true);
	measures_->setWordWrap(false);
	/* fitted to the contents by updateMeasures, once per update: a column left to fit itself is measured again at
	 * every cell changed (98 lines took the window's whole thread) */
	measures_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	measures_->horizontalHeader()->setStretchLastSection(false);
	layout->addWidget(measures_, 1);
	return measurePanel_;
}

void ChartTab::connectControls() {
	ChartView *view = chart_->view();

	/* the axes: typed or picked here, or changed on the chart with the mouse */
	connect(window_, &QComboBox::activated, this, [this] { applyWindowText(); });
	connect(window_->lineEdit(), &QLineEdit::editingFinished, this, &ChartTab::applyWindowText);
	connect(memory_, &QComboBox::activated, this, [this] { applyMemoryText(); });
	connect(memory_->lineEdit(), &QLineEdit::editingFinished, this, &ChartTab::applyMemoryText);
	connect(ram_, &QComboBox::activated, this, [this] { applyRamText(); });
	connect(drawingChoices_, &QActionGroup::triggered, this, [this](QAction *action) {
		QSettings().setValue(QStringLiteral("chart/drawing"), action->data().toInt());
		applyDrawing(action->data().toInt());
	});
	connect(view, &ChartView::drawingFailed, this, [this](const QString &why) {
		/* a card that fails while the settings are restored fails before the window listens: told then, in
		 * logDrawing */
		if (isSignalConnected(QMetaMethod::fromSignal(&ChartTab::logged)))
			emit logged(LogLevel::Warning, tr("chart: the GPU does not draw (%1): the CPU does").arg(why));
		else
			drawingFailure_ = why;
		showDisplayState();
	});
	connect(view, &ChartView::drawingChanged, this, [this] { /* a card opened (or failed to) a moment after the choice */
		showDisplayState();
		logDrawing();
	});
	connect(ram_->lineEdit(), &QLineEdit::editingFinished, this, &ChartTab::applyRamText);
	connect(chart_, &ChartWidget::windowChangedByUser, this, [this](double seconds) {
		window_->setEditText(secondsText(seconds));
		QSettings().setValue(QStringLiteral("chart/window"), seconds);
	});
	connect(view, &ChartView::memoryChanged, this, [this](double seconds) {
		memory_->setEditText(secondsText(seconds));
		QSettings().setValue(QStringLiteral("chart/memory"), seconds);
	});
	connect(yMode_, &QComboBox::activated, this, [this](int mode) {
		if (mode == YLog) {
			normalize_->setChecked(false); /* Log and Normalise exclude each other */
			chart_->setYLog(true);         /* a Manual range kept when it is above 0, else Auto */
		} else {
			chart_->setYLog(false);
			if (mode == YAuto) chart_->setYAuto();
			else chart_->setYManual(chart_->yLo(), chart_->yHi()); /* from what is shown now */
		}
		showYRange();
	});
	for (QLineEdit *field : { yMin_, yMax_ })
		connect(field, &QLineEdit::editingFinished, this, &ChartTab::applyYFields);
	connect(chart_, &ChartWidget::yChangedByUser, this, [this] { showYRange(); });
	connect(normalize_, &QAction::toggled, this, [this](bool on) {
		if (on && chart_->yLog()) { /* Log and Normalise exclude each other: the scale linear, its range kept */
			chart_->setYLog(false);
			showYRange();
		}
		chart_->setNormalized(on);
		/* the list stays: picking Log from it turns Normalise off */
		yMin_->setEnabled(!on);
		yMax_->setEnabled(!on);
		showDisplayState();
	});
	connect(smooth_, &QAction::toggled, this, [this](bool on) {
		chart_->setSmooth(on);
		QSettings().setValue(QStringLiteral("chart/smooth"), on);
		showDisplayState();
	});
	connect(hoverValues_, &QAction::toggled, this, [this](bool on) {
		chart_->view()->setHoverValues(on);
		QSettings().setValue(QStringLiteral("chart/hoverValues"), on);
		showDisplayState();
	});

	/* Hold and Live: the same button, the same size; only its text and colour change */
	connect(holdButton_, &QPushButton::clicked, this, [this] { chart_->setLive(!chart_->live()); });
	connect(view, &ChartView::liveChanged, this, [this](bool live) {
		/* the icons: the same size, drawn (the font's pause and play glyphs are not) */
		holdButton_->setText(live ? tr("Hold") : tr("Live"));
		holdButton_->setIcon(live ? mediaIcon(MediaIcon::Pause, Theme::colors().text)
				: mediaIcon(MediaIcon::Play, QColor(Qt::white)));
		holdButton_->setProperty("live", live);
		repolish(holdButton_);
	});

	/* the measurements are optional: off by default, remembered. The cursors are for
	 * measuring: ticking them shows the measurements, hiding those takes the cursors away. */
	connect(measureButton_, &QPushButton::toggled, this, [this](bool on) {
		measurePanel_->setVisible(on);
		QSettings().setValue(QStringLiteral("chart/measure"), on);
		if (on) updateMeasures();
		else if (cursorsButton_->isChecked()) cursorsButton_->setChecked(false);
	});
	connect(cursorsButton_, &QPushButton::toggled, view, &ChartView::setCursorMode);
	connect(cursorsButton_, &QPushButton::toggled, this, [this, view](bool on) {
		if (on && !measureButton_->isChecked()) measureButton_->setChecked(true);
		if (!on) view->clearCursors(); /* off: A and B go too, as Clear cursors */
	});
	connect(clearCursorsButton_, &QPushButton::clicked, view, &ChartView::clearCursors);
	connect(view, &ChartView::cursorsChanged, this, [this] {
		if (measureButton_->isChecked()) measureSoon();
	});
	measureFollow_.setSingleShot(true);
	measureFollow_.setInterval(MEASURE_FOLLOW_MS);
	connect(&measureFollow_, &QTimer::timeout, this, [this] {
		if (!measurePending_) return;
		measurePending_ = false;
		if (measureButton_->isChecked()) measureSoon();
	});
	measureTimer_.setInterval(250);
	connect(&measureTimer_, &QTimer::timeout, this, [this] {
		if (shown_ && measureButton_->isChecked()) updateMeasures();
	});
	measureTimer_.start();

	/* the lines */
	connect(clearButton_, &QPushButton::clicked, chart_, &ChartWidget::clearData);
	connect(removeAllButton_, &QPushButton::clicked, this, &ChartTab::unplotAllRequested);
}

void ChartTab::restoreSettings() {
	QSettings settings;
	chart_->setMemory(settings.value(QStringLiteral("chart/memory"), 60.0).toDouble());
	memory_->setEditText(secondsText(chart_->memory()));
	applyDrawing(settings.value(QStringLiteral("chart/drawing"), int(ChartView::Drawing::Auto)).toInt());
	const int ram = settings.value(QStringLiteral("chart/ramMB"), ChartView::DEFAULT_RAM_MB).toInt();
	chart_->view()->setRamBudget(std::clamp(ram, int(ChartView::MIN_RAM_MB), maxRamMB()));
	ram_->setEditText(ramText(chart_->view()->ramBudget()));
	chart_->setWindow(settings.value(QStringLiteral("chart/window"), 30.0).toDouble());
	window_->setEditText(secondsText(chart_->window()));
	smooth_->setChecked(settings.value(QStringLiteral("chart/smooth"), true).toBool());
	chart_->setSmooth(smooth_->isChecked());
	hoverValues_->setChecked(settings.value(QStringLiteral("chart/hoverValues"), true).toBool());
	chart_->view()->setHoverValues(hoverValues_->isChecked());
	chart_->setYLog(settings.value(QStringLiteral("chart/yLog"), false).toBool());
	if (!settings.value(QStringLiteral("chart/yAuto"), true).toBool()) {
		chart_->setYManual(settings.value(QStringLiteral("chart/yMin"), 0.0).toDouble(),
				settings.value(QStringLiteral("chart/yMax"), 1.0).toDouble());
	}
	showYRange();
	showMeasureColumns();
	/* last: measured at once (the button's toggle), over the view restored above */
	measureButton_->setChecked(settings.value(QStringLiteral("chart/measure"), false).toBool());
	measurePanel_->setVisible(measureButton_->isChecked());
}

/* ----------------------------------------------------- what the window asks */

void ChartTab::setRegisters(const QVector<RegDef> &registers) {
	registers_ = registers;
	rebuildMath();
}

void ChartTab::plotRegister(const RegDef &def, bool on) {
	if (!on) {
		chart_->removeSeries(int(regKey(def)));
		return;
	}
	const QVector<QColor> &palette = Theme::colors().series;
	chart_->addSeries(int(regKey(def)), def.name, def.unit, palette[nextColor_++ % palette.size()]);
}

void ChartTab::plotField(const RegDef &def, const BitField &field) {
	const QString name = def.name + QLatin1Char('.') + field.name;
	const QVector<MathLine> &lines = mathLines_.lines();
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].name != name) continue;
		mathLines_.setOn(i, true);
		rebuildMath();
		return;
	}
	MathLine line;
	line.name = name;
	line.formula = QStringLiteral("bits(%1, %2, %3)").arg(def.name).arg(field.lsb).arg(field.width);
	mathLines_.add(line);
	rebuildMath();
	emit logged(LogLevel::Info, tr("math line %1 = %2").arg(line.name, line.formula));
}

void ChartTab::clearLines() {
	chart_->clearSeries();
	nextColor_ = 0;
}

QVector<RegKey> ChartTab::mathRegisters() const { return mathLines_.registersRead(); }

void ChartTab::frame(const MathLines::Samples &samples) {
	/* every poll's samples, not one per frame */
	for (auto it = samples.begin(); it != samples.end(); ++it)
		for (const QPointF &point : it.value()) chart_->append(int(it.key()), point.x(), point.y());
	if (!mathLines_.isEmpty())
		mathLines_.evaluate(samples, [this](int key, double time, double value) { chart_->append(key, time, value); });
	chart_->frame();
}

void ChartTab::setValuesPerSecond(int perSecond) { chart_->setValuesPerSecond(perSecond); }

/* shown: the measurements at once, with the lines plotted meanwhile (the timer only runs them while shown) */
void ChartTab::setShown(bool shown) {
	shown_ = shown;
	if (shown_ && measureButton_->isChecked()) updateMeasures();
}

QString ChartTab::infoText(int width) const {
	int registers = 0, math = 0;
	for (const ChartView::Info &line : chart_->view()->lines())
		(line.key >= MathLines::FIRST_CHART_KEY ? math : registers)++;
	/* the parts in their places, and the order they go in when the line is narrow: a part cut in the middle ("32/64
	 * plotted · 60 fp…") said less than the parts left whole */
	enum Part { Count, Plotted, Math, Fps, PaintTime, Delay, Drawer, PARTS };
	QString parts[PARTS];
	parts[Count] = QStringLiteral("%1/%2").arg(registers).arg(registerLimit_);
	parts[Plotted] = tr(" plotted");
	if (math > 0) parts[Math] = tr(" · %1 math").arg(math);
	parts[Fps] = tr(" · %1 fps").arg(chart_->fps(), 0, 'f', 0);
	parts[PaintTime] = tr(" · %1 ms").arg(chart_->paintMs(), 0, 'f', 1);
	if (smooth_->isChecked()) parts[Delay] = tr(" · delay %1 ms").arg(chart_->delayMs(), 0, 'f', 0);
	parts[Drawer] = chart_->view()->drawsOnGpu() ? tr(" · GPU") : tr(" · CPU");
	const auto joined = [&parts] {
		QString text;
		for (const QString &part : parts) text += part;
		return text;
	};
	if (width < 0) return joined();
	const QFontMetrics metrics = chartInfo_->fontMetrics();
	for (const Part drop : { PaintTime, Plotted, Delay, Fps, Drawer, Math, Count }) {
		if (metrics.horizontalAdvance(joined()) <= width) break;
		parts[drop].clear();
	}
	return joined();
}

QString ChartTab::infoTip() const {
	return tr("Plotted: the registers on the chart / as many as it may hold at the rate the samples come (64,000 samples "
			"a second: 64 up to 1000 Hz, 32 at 2000 Hz, 16 at 4000 Hz); the math lines; frames drawn per second, time to "
			"draw one, the smoothing delay; and who draws the lines (GPU or CPU). When the line is narrow, the time to "
			"draw, the word \"plotted\" and the delay go first.");
}

void ChartTab::refreshStatus() {
	/* narrow: whole parts go (infoText), the count stays longest; all of it in the tooltip */
	const QString info = shown_ ? infoText(chartInfo_->contentsRect().width()) : QString();
	chartInfo_->setText(info);
	const QString tip = (shown_ ? infoText() + QStringLiteral("\n\n") : QString()) + infoTip();
	if (chartInfo_->toolTip() != tip) chartInfo_->setToolTip(tip);
	if (chart_->yAuto()) showYRange(false);
	bool over = false;
	const QString need = shown_ ? ramNeedText(chart_->view()->bytesNeeded(), chart_->view()->ramBudget(),
			chart_->memory(), over) : QString();
	ramNeed_->setText(ramNeed_->fontMetrics().elidedText(need, Qt::ElideRight, ramNeed_->contentsRect().width()));
	if (ramNeed_->property("warn").toBool() != over) {
		ramNeed_->setProperty("warn", over);
		ramNeed_->style()->unpolish(ramNeed_);
		ramNeed_->style()->polish(ramNeed_);
	}
}

QString ChartTab::ramNeedText(qint64 bytesNeeded, int ramMB, double memorySeconds, bool &over) {
	over = false;
	if (bytesNeeded <= 0) return {};
	constexpr double MB = 1024.0 * 1024.0;
	const double megabytes = double(bytesNeeded) / MB;
	const QString size = megabytes < 1024 ? QStringLiteral("%1 MB").arg(std::max(1.0, std::round(megabytes)), 0, 'f', 0)
			: QStringLiteral("%1 GB").arg(megabytes / 1024, 0, 'f', 1);
	over = megabytes > ramMB;
	if (!over) return tr("needs %1").arg(size);
	const double kept = memorySeconds * ramMB / megabytes; /* in whole minutes from 2 min, else whole seconds */
	return tr("needs %1, keeps %2").arg(size, secondsText(kept >= 120 ? std::round(kept / 60) * 60 : std::round(kept)));
}

void ChartTab::themeChanged() {
	chart_->update();
	showYRange(false); /* the Auto boxes' grey */
	/* the pause icon is drawn in the theme's text colour */
	if (chart_->live()) holdButton_->setIcon(mediaIcon(MediaIcon::Pause, Theme::colors().text));
}

/* ----------------------------------------------------------------- the axes */

void ChartTab::applyWindowText() {
	const int preset = window_->findText(window_->currentText());
	double seconds = preset >= 0 ? window_->itemData(preset).toDouble() : parseSeconds(window_->currentText());
	if (!(seconds > 0)) {
		window_->setEditText(secondsText(chart_->window())); /* not a length: back to what is shown */
		return;
	}
	seconds = std::clamp(seconds, MIN_TYPED_WINDOW, ChartView::MAX_SPAN);
	chart_->setWindow(seconds);
	window_->setEditText(secondsText(seconds));
	QSettings().setValue(QStringLiteral("chart/window"), seconds);
}

QString ChartTab::yFieldText(double value, bool manual) {
	/* Auto: four digits, but the whole part always (17420, not 1.742e+04, which the six-digit field cut to ".742e+04") */
	const int whole = std::abs(value) >= 1 ? int(std::floor(std::log10(std::abs(value)))) + 1 : 1;
	return QString::number(value, 'g', manual ? 6 : std::clamp(whole, 4, 6));
}

void ChartTab::applyDrawing(int choice) {
	const auto drawing = ChartView::Drawing(std::clamp(choice, int(ChartView::Drawing::Auto), int(ChartView::Drawing::Cpu)));
	ChartView *view = chart_->view();
	view->setDrawing(drawing);
	/* the choice ticked in the menu (the first of its kind); who draws now in the button's tooltip */
	bool ticked = false;
	for (QAction *action : drawingChoices_->actions()) {
		const bool it = !ticked && action->data().toInt() == int(drawing);
		action->setChecked(it);
		ticked = ticked || it;
	}
	/* the card saved is no longer there: the CPU draws, and says so */
	if (!ticked) drawingChoices_->actions().last()->setChecked(true);
	showDisplayState();
	logDrawing();
}

QString ChartTab::displayState() const {
	const QString onOff[] = { tr("off"), tr("on") };
	return tr("Normalise %1 · Smooth %2 · Hover values %3 · drawn by the %4").arg(onOff[normalize_->isChecked()],
			onOff[smooth_->isChecked()], onOff[hoverValues_->isChecked()], chart_->view()->drawingName());
}

void ChartTab::showDisplayState() {
	displayButton_->setToolTip(tr("Display: how the lines are drawn. Now: %1.\nDrawing: Auto takes a dedicated "
			"graphics card when there is one, else the CPU (the processor's graphics draws slower than the CPU on a large "
			"screen). A card draws many fast lines at the display's rate; if it fails, the CPU takes over and the Log "
			"says why.").arg(displayState()));
}

void ChartTab::logDrawing() {
	if (!drawingFailure_.isEmpty())
		emit logged(LogLevel::Warning, tr("chart: the GPU does not draw (%1): the CPU does").arg(drawingFailure_));
	drawingFailure_.clear();
	if (chart_->view()->openingGpu()) return; /* said once the card is ready (drawingChanged) */
	emit logged(LogLevel::Info, tr("chart: the lines drawn by the %1").arg(chart_->view()->drawingName()));
}

/* a size within MIN_RAM_MB .. maxRamMB(); anything else: back to what is set */
void ChartTab::applyRamText() {
	const int preset = ram_->findText(ram_->currentText());
	const int typed = preset >= 0 ? ram_->itemData(preset).toInt() : parseRam(ram_->currentText());
	if (typed > 0) {
		chart_->view()->setRamBudget(std::clamp(typed, int(ChartView::MIN_RAM_MB), maxRamMB()));
		QSettings().setValue(QStringLiteral("chart/ramMB"), chart_->view()->ramBudget());
	}
	ram_->setEditText(ramText(chart_->view()->ramBudget()));
}

void ChartTab::applyMemoryText() {
	const int preset = memory_->findText(memory_->currentText());
	const double seconds = preset >= 0 ? memory_->itemData(preset).toDouble() : parseSeconds(memory_->currentText());
	if (!(seconds > 0)) {
		memory_->setEditText(secondsText(chart_->memory()));
		return;
	}
	chart_->setMemory(seconds);
	memory_->setEditText(secondsText(chart_->memory()));
	window_->setEditText(secondsText(chart_->window())); /* the view fits in the memory */
	QSettings().setValue(QStringLiteral("chart/memory"), chart_->memory());
}

void ChartTab::showYRange(bool save) {
	const bool manual = !chart_->yAuto();
	const int mode = chart_->yLog() ? YLog : manual ? YManual : YAuto;
	if (yMode_->currentIndex() != mode) yMode_->setCurrentIndex(mode);
	if (!yMin_->hasFocus()) yMin_->setText(yFieldText(chart_->yLo(), manual));
	if (!yMax_->hasFocus()) yMax_->setText(yFieldText(chart_->yHi(), manual));
	/* Auto: the fields in grey, they only show what the chart does */
	const QString look = manual ? QString() : QStringLiteral("color:%1").arg(Theme::colors().muted.name());
	if (yMin_->styleSheet() != look) {
		yMin_->setStyleSheet(look);
		yMax_->setStyleSheet(look);
	}
	if (!save) return;
	QSettings settings;
	settings.setValue(QStringLiteral("chart/yAuto"), !manual);
	settings.setValue(QStringLiteral("chart/yLog"), chart_->yLog());
	if (manual) {
		settings.setValue(QStringLiteral("chart/yMin"), chart_->yLo());
		settings.setValue(QStringLiteral("chart/yMax"), chart_->yHi());
	}
}

void ChartTab::applyYFields() {
	bool lowOk = false, highOk = false;
	double low = QLocale::c().toDouble(yMin_->text().trimmed(), &lowOk);
	double high = QLocale::c().toDouble(yMax_->text().trimmed(), &highOk);
	if (!lowOk || !highOk) {
		showYRange();
		return;
	}
	const bool manual = !chart_->yAuto();
	if (yMin_->text().trimmed() == yFieldText(chart_->yLo(), manual)
			&& yMax_->text().trimmed() == yFieldText(chart_->yHi(), manual))
		return; /* no change */
	if (low > high) std::swap(low, high);
	if (high - low < 1e-12) high = chart_->yLog() ? low * 10 : low + 1;
	chart_->setYManual(low, high); /* refused on the Log scale at 0 or below: the fields back to the range shown */
	showYRange();
}

/* --------------------------------------------------------- the measurements */

/* At once, then not again before MEASURE_FOLLOW_MS: a cursor dragged moves at every mouse move, and 64 lines over
 * minutes of samples measured at each held the chart near 17 frames a second. The last place is always measured.
 * While a cursor is dragged only A, B and B - A follow it: the rest over A..B (every sample between them, and the
 * columns fitted) comes once it is let go (cursorsChanged at the release), and meanwhile at the timer's pace; all of
 * it at every step held the chart near 51 frames a second with 64 lines */
void ChartTab::measureSoon() {
	if (measureFollow_.isActive()) {
		measurePending_ = true;
		return;
	}
	updateMeasures(chart_->view()->draggingCursor());
	measureFollow_.start();
}

void ChartTab::updateMeasures(bool cursorsOnly) {
	const ChartView *view = chart_->view();
	const QVector<ChartView::Info> lines = view->lines();
	QVector<int> lineKeys;
	for (const ChartView::Info &line : lines) lineKeys << line.key;
	if (lineKeys != measuredKeys_ || measures_->rowCount() != lines.size()) cursorsOnly = false; /* other lines: all */
	measureUpdates_++;
	if (!cursorsOnly) measureFullUpdates_++;
	measureInfo_->setText(measuredRangeText());
	const QVector<ChartView::Stats> stats = view->stats(lineKeys, cursorsOnly);
	const QString none = QStringLiteral("—");
	measures_->setRowCount(int(lines.size()));
	for (int row = 0; row < lines.size(); row++) {
		const ChartView::Info &line = lines[row];
		const ChartView::Stats &s = stats[row];
		const QString unit = line.unit.isEmpty() ? QString() : QStringLiteral(" ") + line.unit;
		const QStringList cells = {
			line.name,
			std::isfinite(s.atA) ? measureText(s.atA) + unit : none,
			std::isfinite(s.atB) ? measureText(s.atB) + unit : none,
			std::isfinite(s.atA) && std::isfinite(s.atB) ? measureText(s.atB - s.atA) + unit : none,
			s.ok ? measureText(s.min) + unit : none,
			s.ok ? measureText(s.max) + unit : none,
			s.ok ? measureText(s.mean) + unit : none,
			s.ok ? measureText(s.rms) + unit : none,
			s.ok ? measureText(s.std) + unit : none,
			s.ok ? measureText(s.p2p) + unit : none,
			s.ok ? measureText(s.integral) + QStringLiteral(" ") + areaUnit(line.unit, false) : none,
			s.ok ? measureText(s.integral / 3600.0) + QStringLiteral(" ") + areaUnit(line.unit, true) : none,
			std::isfinite(s.total) ? measureText(s.total / 3600.0) + QStringLiteral(" ") + areaUnit(line.unit, true) : none,
		};
		for (int column = 0; column < cells.size(); column++) {
			if (cursorsOnly && (column < ColAtA || column > ColDiff)) continue; /* A, B, B - A */
			QTableWidgetItem *item = measures_->item(row, column);
			if (!item) {
				item = new QTableWidgetItem;
				item->setTextAlignment(column == 0 ? (Qt::AlignLeft | Qt::AlignVCenter)
						: (Qt::AlignRight | Qt::AlignVCenter));
				measures_->setItem(row, column, item);
			}
			if (item->text() != cells[column]) item->setText(cells[column]);
			if (column == 0 && item->foreground().color() != line.color) item->setForeground(line.color);
		}
	}
	if (cursorsOnly) return; /* the columns fitted when all of it is measured */
	/* the columns fit their contents, measured once; they only grow while the lines are the same, so the table does
	 * not jump as the values change */
	const bool sameLines = lineKeys == measuredKeys_;
	measuredKeys_ = lineKeys;
	QHeaderView *header = measures_->horizontalHeader();
	const QAbstractItemView *table = measures_; /* QTableView keeps its sizeHintForColumn protected */
	for (int column = 0; column < measures_->columnCount(); column++) {
		if (measures_->isColumnHidden(column)) continue;
		const int fit = std::max(table->sizeHintForColumn(column), header->sectionSizeHint(column));
		if (fit != header->sectionSize(column) && (!sameLines || fit > header->sectionSize(column)))
			header->resizeSection(column, fit);
	}
}

/* over what the measurements run: the cursors, or the view (with a hint on placing the cursors); then since when the
 * totals run, by the clock and how long */
QString ChartTab::measuredRangeText() const {
	const ChartView *view = chart_->view();
	double t0, t1;
	bool cursors;
	view->range(t0, t1, cursors);
	QString text;
	if (cursors) {
		text = tr("Measured between the cursors: A → B = %1 s").arg(measureText(std::fabs(view->cursorB() - view->cursorA())));
	} else {
		const QString hint = cursorsButton_->isChecked()
				? tr(" (place cursor %1 on the chart)")
						.arg(std::isfinite(view->cursorA()) ? QStringLiteral("B") : QStringLiteral("A"))
				: tr(" (Cursors: measure between two points)");
		text = tr("Measured over the view: %1 s%2").arg(measureText(t1 - t0), hint);
	}
	const double since = view->totalsSince();
	if (std::isfinite(since) && !measures_->isColumnHidden(ColTotal)) {
		const QDateTime at = QDateTime::fromMSecsSinceEpoch(view->epochMs() + qint64(std::llround(since * 1000)));
		text += tr(" · totals since %1 (%2)").arg(at.toString(QStringLiteral("HH:mm:ss")),
				durationText(std::max(0.0, view->timeNow() - since)));
	}
	return text;
}

void ChartTab::showMeasureColumns() {
	const QStringList hidden = QSettings().value(QStringLiteral("chart/measureColumns")).toStringList();
	for (QAction *action : measureColumns_->actions()) {
		const int column = action->data().toInt();
		const QSignalBlocker quiet(action); /* not saved again column by column, the later ones not yet shown */
		action->setChecked(!hidden.contains(QLatin1String(MEASURE_KEYS[column])));
		measures_->setColumnHidden(column, !action->isChecked());
	}
}

/* ----------------------------------------------------------- the math lines */

void ChartTab::rebuildMath() {
	mathLines_.compile(registers_);
	drawMathLines();
	rebuildMathMenu();
	emit mathRegistersChanged(); /* the registers the formulas read are sampled too */
}

void ChartTab::drawMathLines() {
	for (int i = 0; i < MathLines::MAX_DRAWN; i++) chart_->removeSeries(MathLines::chartKey(i));
	const QVector<MathLine> &lines = mathLines_.lines();
	const QVector<QColor> &palette = Theme::colors().series;
	for (int i = 0; i < lines.size() && i < MathLines::MAX_DRAWN; i++) {
		if (!lines[i].active()) continue;
		/* colours from the palette's end: the registers take them from its start */
		const QColor color = palette[palette.size() - 1 - i % palette.size()];
		chart_->addSeries(MathLines::chartKey(i), QStringLiteral("ƒ %1").arg(lines[i].name), lines[i].unit, color);
	}
}

void ChartTab::rebuildMathMenu() {
	QMenu *menu = mathButton_->menu();
	menu->clear();
	menu->addAction(tr("New math line…"), this, [this] { editMathLine(-1); });
	const QVector<MathLine> &lines = mathLines_.lines();
	if (!lines.isEmpty()) menu->addSeparator();
	for (int i = 0; i < lines.size(); i++) {
		const MathLine &line = lines[i];
		const QString title = noMnemonic(line.error.isEmpty() ? QStringLiteral("%1 = %2").arg(line.name, line.formula)
				: tr("%1 = %2  (%3)").arg(line.name, line.formula, line.error));
		QMenu *sub = menu->addMenu(title);
		QAction *shown = sub->addAction(tr("Shown"));
		shown->setCheckable(true);
		shown->setChecked(line.on);
		shown->setEnabled(line.error.isEmpty());
		connect(shown, &QAction::toggled, this, [this, i](bool on) {
			mathLines_.setOn(i, on);
			rebuildMath();
		});
		sub->addAction(tr("Edit…"), this, [this, i] { editMathLine(i); });
		sub->addAction(tr("Remove"), this, [this, i] {
			mathLines_.remove(i);
			rebuildMath();
		});
	}
	const int active = mathLines_.activeCount();
	mathButton_->setText(active ? tr("ƒ  Math (%1)").arg(active) : tr("ƒ  Math"));
}

void ChartTab::editMathLine(int line) {
	MathLine start; /* a new one: the power of a supply, say */
	start.name = QStringLiteral("P");
	start.unit = QStringLiteral("W");
	if (line >= 0) start = mathLines_.lines()[line];
	MathLineDialog dialog(start, line >= 0, registers_, window()); /* over the window, as its other dialogs */
	if (dialog.exec() != QDialog::Accepted) return;
	const MathLine edited = dialog.result();
	if (line >= 0) mathLines_.replace(line, edited);
	else mathLines_.add(edited);
	rebuildMath();
	emit logged(LogLevel::Info, tr("math line %1 = %2").arg(edited.name, edited.formula));
}

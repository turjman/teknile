/* SPDX-License-Identifier: Apache-2.0 */
/* The Registers tab: see registers_tab.h. */
#include "ui/registers_tab.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>
#include <functional>
#include <initializer_list>

#include "model/map_document.h"
#include "model/register_model.h"
#include "ui/quick_write_panel.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"
#include "ui/value_delegate.h"

RegistersTab::RegistersTab(RegisterModel *model, MapDocument *doc, QWidget *parent)
	: QWidget(parent), model_(model), doc_(doc) {
	filter_ = new RegisterFilter(this);
	filter_->setSourceModel(model_);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 12, 0, 0);
	layout->setSpacing(10);
	layout->addLayout(buildToolbar());
	layout->addWidget(buildTable(), 1);
	layout->addWidget(buildDetail());
	quickWrite_ = new QuickWritePanel(model_);
	layout->addWidget(quickWrite_);

	connectModel();
	updateDetail();
}

void RegistersTab::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (Theme::switched(event, drawnDark_)) setHighlighted(allowWrites_, allowWrites_->isChecked(), Theme::colors().warn);
}

void RegistersTab::setConnected(bool connected) { quickWrite_->setConnected(connected); }

void RegistersTab::setDevice(int slave) { filter_->setDevice(slave); }

void RegistersTab::setDevices(const QVector<PickerDevice> &devices, int shown) {
	device_->setVisible(!devices.isEmpty());
	if (devices.isEmpty()) return;
	const QVector<PickerDevice> rows = QVector<PickerDevice> { { tr("All devices"), -1, QColor(),
			tr("Every device's registers") } } + devices;
	fillDevicePicker(device_, rows, shown);
}

void RegistersTab::setBroadcastRule(std::function<QString(const RegDef &)> rule) {
	quickWrite_->setBroadcastRule(std::move(rule));
}

void RegistersTab::setShown(bool shown) {
	shown_ = shown;
	/* values grew wider while another tab was shown (the columns are fitted only while this one is):
	 * the room now, before the first paint, not half a second later as a visible stretch */
	if (shown) fitColumns(false);
}

void RegistersTab::refreshStatus() {
	if (!shown_) return;
	fitColumns(false); /* values grew wider: make room */
	if (!model_->rows().isEmpty()) table_->viewport()->update(); /* let a value that changed stop glowing */
}

/* ----------------------------------------------------------------- building */

QHBoxLayout *RegistersTab::buildToolbar() {
	search_ = new QLineEdit;
	search_->setPlaceholderText(tr("Search name, address, unit, description…"));
	search_->setClearButtonEnabled(true);
	connect(search_, &QLineEdit::textChanged, filter_, &RegisterFilter::setText);

	device_ = new QComboBox;
	device_->setObjectName(QStringLiteral("registersDevice"));
	device_->setToolTip(tr("The device whose registers the table shows. All devices: every device's, so a search "
			"finds a register on each of them (D1_SPEED, D2_SPEED, …)"));
	device_->hide();
	connect(device_, &QComboBox::activated, this, [this] { emit deviceChosen(device_->currentData().toInt()); });

	groupButton_ = new QPushButton;
	groupButton_->setObjectName(QStringLiteral("groups"));
	groupButton_->setFixedWidth(240);
	groupButton_->setToolTip(tr("The groups shown: tick one or several"));
	groupMenu_ = new QMenu(groupButton_);
	setButtonMenu(groupButton_, groupMenu_);

	plotShownButton_ = new QPushButton(tr("Plot shown"));
	plotShownButton_->setObjectName(QStringLiteral("plotShown"));
	plotShownButton_->setToolTip(
			tr("Chart every numeric register the table shows now (after the search and the groups).\n"
			   "Right-click the table to remove them again."));
	connect(plotShownButton_, &QPushButton::clicked, this,
			[this] { plotShown(plotShownButton_->property("plot").toBool()); });
	/* its label follows what is shown and plotted */
	for (auto signal : { &QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved })
		connect(filter_, signal, this, &RegistersTab::updatePlotButton);
	connect(filter_, &QAbstractItemModel::modelReset, this, &RegistersTab::updatePlotButton);
	connect(filter_, &QAbstractItemModel::layoutChanged, this, &RegistersTab::updatePlotButton);
	connect(model_, &RegisterModel::plotChanged, this, &RegistersTab::updatePlotButton);

	allowWrites_ = new QCheckBox(tr("Allow writes"));
	setHighlighted(allowWrites_, false, Theme::colors().warn); /* its bold width from the start: no jump at the first tick */
	allowWrites_->setToolTip(
			tr("Off: values cannot be edited. On: double-click an rw value to write it. ⚠ registers ask first."));
	connect(allowWrites_, &QCheckBox::toggled, this, [this](bool on) {
		model_->setWritesEnabled(on);
		setHighlighted(allowWrites_, on, Theme::colors().warn);
		emit writesAllowedChanged(on);
	});
	drawnDark_ = Theme::isDark();
	auto *addButton = new QPushButton(tr("+ Register"));
	connect(addButton, &QPushButton::clicked, this, &RegistersTab::addRegister);

	auto *bar = new QHBoxLayout;
	bar->addWidget(search_, 1);
	bar->addWidget(device_);
	bar->addWidget(groupButton_);
	bar->addWidget(plotShownButton_);
	bar->addSpacing(8);
	bar->addWidget(allowWrites_);
	bar->addWidget(addButton);
	return bar;
}

QTableView *RegistersTab::buildTable() {
	table_ = new QTableView;
	table_->setObjectName(QStringLiteral("registers"));
	table_->setModel(filter_);
	table_->setItemDelegateForColumn(RegisterModel::ColValue, new ValueDelegate(table_));
	table_->setAlternatingRowColors(true);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	table_->verticalHeader()->hide();
	table_->verticalHeader()->setDefaultSectionSize(30);
	table_->setShowGrid(false);
	table_->setWordWrap(false);
	QHeaderView *header = table_->horizontalHeader();
	header->setHighlightSections(false);
	header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	header->setSectionResizeMode(QHeaderView::Interactive);
	/* the widths to start from: fitColumns() makes them fit the map's content */
	table_->setColumnWidth(RegisterModel::ColPlot, 46);
	table_->setColumnWidth(RegisterModel::ColLog, 42);
	table_->setColumnWidth(RegisterModel::ColAddr, 80);
	table_->setColumnWidth(RegisterModel::ColName, 170);
	table_->setColumnWidth(RegisterModel::ColValue, 120);
	table_->setColumnWidth(RegisterModel::ColUnit, 60);
	table_->setColumnWidth(RegisterModel::ColType, 80);
	table_->setColumnWidth(RegisterModel::ColAccess, 64);
	table_->setColumnWidth(RegisterModel::ColGroup, 130);
	/* Decoded as wide as its longest text (a scroll bar if the window is
	 * narrower: nothing is cut off); Group, the last, takes the rest */
	header->setStretchLastSection(true);
	/* Decoded: off by default (most registers have none; the (i) beside a value
	 * and the line under the table show it); right-click to show it */
	table_->setColumnHidden(RegisterModel::ColDecoded,
			!QSettings().value(QStringLiteral("ui/decodedColumn"), false).toBool());
	header->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(header, &QHeaderView::customContextMenuRequested, this, &RegistersTab::showHeaderMenu);
	table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	table_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(table_, &QTableView::customContextMenuRequested, this, &RegistersTab::showTableMenu);
	return table_;
}

QLabel *RegistersTab::buildDetail() {
	/* the selected register in full: nothing is cut off here */
	detail_ = new QLabel;
	detail_->setObjectName(QStringLiteral("detail"));
	detail_->setWordWrap(true);
	detail_->setTextFormat(Qt::RichText);
	detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	detail_->setMinimumHeight(44);
	detail_->setContentsMargins(10, 6, 10, 6);
	return detail_;
}

void RegistersTab::connectModel() {
	/* writes from the table and from the panel leave the same way */
	connect(model_, &RegisterModel::writeRequested, this, &RegistersTab::writeRequested);
	connect(quickWrite_, &QuickWritePanel::writeRequested, this, &RegistersTab::writeRequested);
	connect(quickWrite_, &QuickWritePanel::broadcastRequested, this, &RegistersTab::broadcastRequested);
	/* the register selected, under the table */
	connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
			[this](const QModelIndex &current) {
				detailRow_ = current.isValid() ? filter_->mapToSource(current).row() : -1;
				updateDetail();
				quickWrite_->showRegister(detailRow_);
			});
	connect(model_, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &first, const QModelIndex &last) {
		if (detailRow_ >= first.row() && detailRow_ <= last.row()) updateDetail();
	});
	connect(model_, &QAbstractItemModel::modelReset, this, [this] {
		detailRow_ = -1;
		updateDetail();
	});
	/* another map, or registers added or removed: its groups, and the columns once the table shows the rows */
	connect(model_, &RegisterModel::structureChanged, this, [this] {
		refreshGroups();
		QTimer::singleShot(0, this, [this] { fitColumns(true); });
	});
}

/* ---------------------------------------------------------------- the menus */

void RegistersTab::showTableMenu(const QPoint &pos) {
	const QModelIndex clicked = table_->indexAt(pos);
	const int row = clicked.isValid() ? filter_->mapToSource(clicked).row() : -1;
	QMenu menu(this);
	if (row >= 0) {
		/* the register under the mouse */
		const RegisterModel::Row &reg = model_->rows()[row];
		if (reg.def.canPlot()) {
			menu.addAction(reg.plot ? tr("Remove from chart") : tr("Plot"), this,
					[this, row] { model_->setPlot(row, !model_->rows()[row].plot); });
		}
		/* a field alone, as a line of its own: a register shown raw only (scaled, its bits are not the value) */
		if (!reg.def.fields.isEmpty() && !reg.def.isFloat()) {
			QMenu *fields = menu.addMenu(tr("Plot a field"));
			for (int i = 0; i < reg.def.fields.size(); i++)
				fields->addAction(noMnemonic(reg.def.fields[i].name), this, [this, row, i] { emit plotFieldRequested(row, i); });
		}
		menu.addAction(tr("Read now"), this, [this, row] {
			const RegDef &def = model_->rows()[row].def;
			emit readRequested(def.addr, quint16(def.size)); /* the table follows through the engine */
		});
		menu.addAction(tr("Copy value"), this, [reg] { /* the value when the menu opened */
			QGuiApplication::clipboard()->setText(reg.valid ? formatValue(reg.def, reg.raw) : QString());
		});
		menu.addSeparator();
		menu.addAction(tr("Edit definition…"), this, [this, row] {
			emit editDefinitionRequested(model_->rows()[row].def.uid, model_->rows()[row].def.slave);
		});
		menu.addAction(tr("Remove"), this, &RegistersTab::removeRegisters);
		menu.addSeparator();
	}
	/* what the table shows, and the table itself */
	menu.addAction(tr("Plot all shown (%1)").arg(filter_->rowCount()), this, [this] { plotShown(true); });
	menu.addAction(tr("Remove shown from the chart"), this, [this] { plotShown(false); });
	menu.addAction(tr("Remove all from the chart"), this, &RegistersTab::unplotAllRequested);
	menu.addSeparator();
	menu.addAction(tr("Fit columns"), this, [this] { fitColumns(true); });
	addDecodedColumnAction(menu);
	menu.addSeparator();
	menu.addAction(tr("Log all"), this, [this] { model_->setAllLog(true); });
	menu.addAction(tr("Log none"), this, [this] { model_->setAllLog(false); });
	menu.addAction(tr("Add register…"), this, &RegistersTab::addRegister);
	menu.exec(table_->viewport()->mapToGlobal(pos));
}

void RegistersTab::showHeaderMenu(const QPoint &pos) {
	QMenu menu(this);
	addDecodedColumnAction(menu);
	menu.addAction(tr("Fit columns"), this, [this] { fitColumns(true); });
	menu.exec(table_->horizontalHeader()->mapToGlobal(pos));
}

void RegistersTab::addDecodedColumnAction(QMenu &menu) {
	QAction *action = menu.addAction(tr("Decoded column"));
	action->setCheckable(true);
	action->setChecked(!table_->isColumnHidden(RegisterModel::ColDecoded));
	connect(action, &QAction::toggled, this, &RegistersTab::showDecodedColumn);
}

void RegistersTab::showDecodedColumn(bool shown) {
	table_->setColumnHidden(RegisterModel::ColDecoded, !shown);
	QSettings().setValue(QStringLiteral("ui/decodedColumn"), shown);
	if (shown) fitDecoded();
}

/* --------------------------------------------------------- the groups shown */

QStringList RegistersTab::groupNames() const {
	QStringList groups;
	for (const RegisterModel::Row &row : model_->rows())
		if (!groups.contains(row.def.group)) groups << row.def.group;
	return groups;
}

void RegistersTab::refreshGroups() {
	const QStringList groups = groupNames();
	/* the groups ticked stay ticked, when the map still has them */
	QStringList kept;
	for (const QString &group : groupsShown_)
		if (groups.contains(group)) kept << group;
	groupsShown_ = kept;
	groupMenu_->clear();
	QAction *all = groupMenu_->addAction(tr("All groups"));
	connect(all, &QAction::triggered, this, [this] {
		groupsShown_.clear();
		for (QCheckBox *box : groupMenu_->findChildren<QCheckBox *>()) {
			const QSignalBlocker blocker(box);
			box->setChecked(false);
		}
		updateGroupButton();
	});
	groupMenu_->addSeparator();
	for (const QString &group : groups) {
		/* a check box in the menu: ticking it does not close the menu */
		auto *box = new QCheckBox(group.isEmpty() ? tr("(no group)") : noMnemonic(group));
		box->setChecked(groupsShown_.contains(group));
		box->setContentsMargins(10, 3, 10, 3);
		connect(box, &QCheckBox::toggled, this, [this, group](bool on) {
			if (on && !groupsShown_.contains(group)) groupsShown_ << group;
			if (!on) groupsShown_.removeAll(group);
			updateGroupButton();
		});
		auto *action = new QWidgetAction(groupMenu_);
		action->setDefaultWidget(box);
		groupMenu_->addAction(action);
	}
	updateGroupButton();
}

void RegistersTab::updateGroupButton() {
	filter_->setGroups(groupsShown_);
	/* the names themselves, shortened only when they do not fit (a fixed
	 * width: the search box does not move); all of them in the tooltip */
	const QString names = groupsShown_.join(QStringLiteral(", "));
	QString text = groupsShown_.isEmpty() ? tr("All groups") : names;
	const int room = groupButton_->width() - 44; /* the padding and the menu arrow */
	const QFontMetrics metrics(groupButton_->font());
	if (metrics.horizontalAdvance(text) > room && groupsShown_.size() > 1) {
		const QString count = tr(" (%1)").arg(groupsShown_.size());
		text = metrics.elidedText(text, Qt::ElideRight, room - metrics.horizontalAdvance(count)) + count;
	} else {
		text = metrics.elidedText(text, Qt::ElideRight, room);
	}
	groupButton_->setText(noMnemonic(text));
	groupButton_->setToolTip(groupsShown_.isEmpty() ? tr("All groups shown: tick one or several")
			: tr("Shown: %1").arg(names));
}

/* ---------------------------------------------------------------- the chart */

void RegistersTab::updatePlotButton() {
	int numeric = 0, plotted = 0;
	for (int i = 0; i < filter_->rowCount(); i++) {
		const RegisterModel::Row &row = model_->rows()[filter_->mapToSource(filter_->index(i, 0)).row()];
		if (!row.def.canPlot()) continue;
		numeric++;
		plotted += row.plot ? 1 : 0;
	}
	/* Plot shown while none of them is on the chart, or some are not and there is room; Unplot shown once all are on
	 * it, or the chart is full (more shown than it may hold: Plot shown could never become Unplot shown) */
	const bool full = model_->plottedCount() >= model_->plotLimit();
	const bool plot = numeric == 0 || plotted == 0 || (plotted < numeric && !full);
	plotShownButton_->setProperty("plot", plot);
	plotShownButton_->setText(plot ? tr("Plot shown") : tr("Unplot shown"));
	plotShownButton_->setToolTip(!plot ? tr("Remove the %1 registers shown from the chart").arg(plotted)
			: full ? tr("The chart is full: %1 registers at this rate. Untick some first").arg(model_->plotLimit())
			: tr("Chart every plottable register the table shows now (%1, after the search and the groups); at most %2 "
					"at this rate").arg(numeric).arg(model_->plotLimit()));
}

void RegistersTab::plotShown(bool on) {
	QVector<int> rows;
	for (int i = 0; i < filter_->rowCount(); i++) {
		const int row = filter_->mapToSource(filter_->index(i, 0)).row();
		if (model_->rows()[row].def.canPlot() && model_->rows()[row].plot != on) rows << row;
	}
	/* no question: as many as there is room for (RegisterModel::MAX_PLOTTED), in the table's order; the rest are
	 * left off and the status bar says how many */
	const int room = on ? std::max(0, model_->plotLimit() - model_->plottedCount()) : int(rows.size());
	const int done = std::min(room, int(rows.size()));
	for (int i = 0; i < done; i++) model_->setPlot(rows[i], on);
	updatePlotButton();
	const int left = int(rows.size()) - done;
	emit statusMessage(!on ? tr("%1 registers removed from the chart").arg(rows.size())
			: left == 0 ? tr("%1 registers added to the chart").arg(done)
			: tr("%1 registers added to the chart, %2 left off: %3 at most at this rate").arg(done).arg(left)
					.arg(model_->plotLimit()), left == 0 ? 3000 : 8000);
}

/* ---------------------------------------------------------- editing the map */

void RegistersTab::addRegister() {
	const QModelIndex current = table_->currentIndex();
	const int row = current.isValid() ? filter_->mapToSource(current).row() : -1;
	/* after the current register when it is of the map edited (all devices shown: perhaps another's) */
	const bool ofMap = row >= 0 && model_->rows()[row].def.slave == model_->selectedDevice();
	emit addRegisterRequested(ofMap ? model_->rows()[row].def.uid : 0);
}

void RegistersTab::removeRegisters() {
	/* from the map edited: the selected device's (all devices shown: the others' rows are left alone) */
	QVector<quint32> uids;
	int others = 0;
	for (const QModelIndex &index : table_->selectionModel()->selectedRows()) {
		const RegDef &def = model_->rows()[filter_->mapToSource(index).row()].def;
		if (def.slave == model_->selectedDevice()) uids << def.uid;
		else others++;
	}
	if (others > 0)
		emit statusMessage(tr("%n register(s) of other devices left: select a device to edit its map", nullptr, others), 5000);
	if (uids.isEmpty()) return;
	doc_->edit(tr("Remove %n register(s)", nullptr, int(uids.size())), [uids](DeviceMap &map) {
		map.regs.removeIf([&uids](const RegDef &def) { return uids.contains(def.uid); });
	});
	emit statusMessage(tr("%n register(s) removed (Undo on the Map editor tab brings them back)", nullptr,
			int(uids.size())), 4000);
}

/* -------------------------------------------------------------- the columns */

void RegistersTab::fitColumns(bool shrink) {
	QHeaderView *header = table_->horizontalHeader();
	for (int column : { RegisterModel::ColAddr, RegisterModel::ColName, RegisterModel::ColValue, RegisterModel::ColUnit,
			RegisterModel::ColType, RegisterModel::ColAccess }) {
		/* sizeHintForColumn: public in QAbstractItemView, protected in QTableView */
		const int need = std::max(static_cast<QAbstractItemView *>(table_)->sizeHintForColumn(column),
				header->sectionSizeHint(column)) + 4;
		const int min = column == RegisterModel::ColValue ? 110 : 0; /* values grow: room from the start */
		const int width = std::max(need, min);
		if (shrink ? width != header->sectionSize(column) : width > header->sectionSize(column))
			header->resizeSection(column, width);
	}
	/* the longest decoded text */
	const QFontMetrics metrics(table_->font());
	int need = header->sectionSizeHint(RegisterModel::ColDecoded);
	for (int i = 0; i < model_->rowCount(); i++) {
		const QString decoded = model_->data(model_->index(i, RegisterModel::ColDecoded), Qt::DisplayRole).toString();
		need = std::max(need, metrics.horizontalAdvance(decoded) + 16);
	}
	decodedWidth_ = shrink ? need : std::max(decodedWidth_, need);
	fitDecoded();
}

void RegistersTab::fitDecoded() {
	QHeaderView *header = table_->horizontalHeader();
	if (decodedWidth_ != header->sectionSize(RegisterModel::ColDecoded))
		header->resizeSection(RegisterModel::ColDecoded, std::max(decodedWidth_, 60));
}

/* ---------------------------------------------------------- the detail line */

void RegistersTab::updateDetail() {
	const ThemeColors &colors = Theme::colors();
	if (detailRow_ < 0 || detailRow_ >= model_->rows().size()) {
		detail_->setText(coloredSpan(
				tr("Select a register to see it here in full: value, decoded, raw bytes, description."), colors.muted));
		return;
	}
	const RegisterModel::Row &row = model_->rows()[detailRow_];
	const RegDef &def = row.def;
	const auto cellText = [&](int column) {
		return model_->data(model_->index(detailRow_, column), Qt::DisplayRole).toString();
	};
	/* the name and what it is; its value, raw and decoded; then what is wrong, and what it is for */
	const QString what = QStringLiteral("%1 · %2 · %3 · %4")
			.arg(addrText(def.addr), cellText(RegisterModel::ColType), cellText(RegisterModel::ColAccess),
					def.group.toHtmlEscaped());
	QString text = QStringLiteral("<b>%1</b> &nbsp;").arg(def.name.toHtmlEscaped()) + coloredSpan(what, colors.muted);
	text += QStringLiteral("<br>%1 <b>%2</b> %3")
			.arg(tr("Value"), cellText(RegisterModel::ColValue).toHtmlEscaped(), def.unit.toHtmlEscaped());
	if (row.valid) {
		const QString raw = QString::fromLatin1(row.raw.toHex(' ').toUpper());
		text += QStringLiteral(" &nbsp;") + coloredSpan(QStringLiteral("raw %1").arg(raw), colors.muted);
	}
	const QString decoded = row.valid ? formatDecoded(def, row.raw) : QString();
	if (!decoded.isEmpty()) {
		text += QStringLiteral("<br>%1 %2").arg(tr("Decoded"),
				decoded.toHtmlEscaped().replace(QLatin1String("  "), QStringLiteral(" &nbsp;·&nbsp; ")));
	}
	if (!row.error.isEmpty()) text += QStringLiteral("<br>") + coloredSpan(row.error.toHtmlEscaped(), colors.bad);
	if (!def.desc.isEmpty()) text += QStringLiteral("<br>") + coloredSpan(def.desc.toHtmlEscaped(), colors.muted);
	if (detail_->text() != text) detail_->setText(text);
}

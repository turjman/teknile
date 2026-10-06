/* SPDX-License-Identifier: Apache-2.0 */
/* The Map editor tab: see map_editor_tab.h. */
#include "ui/map_editor_tab.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextDocumentFragment>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>

#include "model/map_document.h"
#include "model/map_export.h"
#include "ui/elided_label.h"
#include "ui/map_table_model.h"
#include "ui/register_editor.h"
#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

const char *MIME_REGISTERS = "application/x-evre-registers";

/* the search: every column but # (a number typed would match each row with that digit in its number) */
class SearchFilter : public QSortFilterProxyModel {
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
		for (int column = MapTableModel::ColAddr; column < MapTableModel::ColCount; column++) {
			const QString text = sourceModel()->index(row, column, parent).data(filterRole()).toString();
			if (text.contains(filterRegularExpression())) return true;
		}
		return false;
	}
};

/* the cells with a list to pick from: type, access, write; the group's list is also typed into */
class CellDelegate : public QStyledItemDelegate {
public:
	CellDelegate(MapDocument *doc, QObject *parent) : QStyledItemDelegate(parent), doc_(doc) {}

	QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
		QStringList choices;
		switch (index.column()) {
		case MapTableModel::ColType: choices = MapTableModel::typeChoices(); break;
		case MapTableModel::ColAccess: choices = MapTableModel::accessChoices(); break;
		case MapTableModel::ColWrite: choices = MapTableModel::writeChoices(); break;
		case MapTableModel::ColGroup: choices = doc_->groups(); break;
		default: return QStyledItemDelegate::createEditor(parent, option, index);
		}
		auto *box = new QComboBox(parent);
		box->addItems(choices);
		box->setEditable(index.column() == MapTableModel::ColGroup);
		return box;
	}
	void setEditorData(QWidget *editor, const QModelIndex &index) const override {
		if (auto *box = qobject_cast<QComboBox *>(editor)) {
			const QString text = index.data(Qt::EditRole).toString();
			if (box->isEditable()) box->setEditText(text);
			else box->setCurrentIndex(std::max(0, box->findText(text)));
			return;
		}
		QStyledItemDelegate::setEditorData(editor, index);
	}
	void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
		if (auto *box = qobject_cast<QComboBox *>(editor)) {
			model->setData(index, box->currentText(), Qt::EditRole);
			return;
		}
		QStyledItemDelegate::setModelData(editor, model, index);
	}

private:
	MapDocument *doc_;
};

/* a name the map does not have yet: NAME, else NAME_2, NAME_3 ...; a name that ends in a number
 * counts on from it (a copy of CH_1 is CH_2, of NAME_2 is NAME_3) */
QString freeName(const QString &name, const QSet<QString> &taken) {
	if (!taken.contains(name)) return name;
	QString base = name;
	int n = 2;
	const int underscore = int(name.lastIndexOf(QLatin1Char('_')));
	bool numbered = false;
	const int number = underscore > 0 ? name.mid(underscore + 1).toInt(&numbered) : 0;
	if (numbered && number >= 0) {
		base = name.left(underscore);
		n = number + 1;
	}
	for (;; n++) {
		const QString candidate = QStringLiteral("%1_%2").arg(base).arg(n);
		if (!taken.contains(candidate)) return candidate;
	}
}

bool overlaps(int addr, int size, const RegDef &def) { return addr < def.addr + def.size && def.addr < addr + size; }

} // namespace

MapEditorTab::MapEditorTab(MapDocument *doc, QWidget *parent) : QWidget(parent), doc_(doc) {
	drawnDark_ = Theme::isDark();
	model_ = new MapTableModel(doc_, this);
	filter_ = new SearchFilter(this);
	filter_->setSourceModel(model_);
	filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
	/* a change of a selected row goes to all of them */
	model_->setTargets([this](int row) {
		const quint32 uid = model_->uidAt(row);
		const QVector<quint32> selected = selectedUids();
		return selected.contains(uid) ? selected : QVector<quint32>{ uid };
	});
	connect(model_, &MapTableModel::refused, this, [this](const QString &why) { emit statusMessage(why, 5000); });
	connect(model_, &QAbstractItemModel::modelAboutToBeReset, this, [this] { keptSelection_ = selectedUids(); });
	connect(model_, &QAbstractItemModel::modelReset, this, &MapEditorTab::onDocumentChanged);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 8, 0, 0);
	/* a bus: whose map this is, and whose live values the form shows */
	banner_ = new QWidget;
	banner_->setObjectName(QStringLiteral("mapDevices"));
	/* one line: cut to its room ("...") with the whole of it in the tooltip, so a long list of devices moves nothing */
	bannerText_ = new ElidedLabel;
	bannerText_->setObjectName(QStringLiteral("mapDevicesText"));
	liveDevice_ = new QComboBox;
	liveDevice_->setObjectName(QStringLiteral("liveDevice"));
	liveDevice_->setToolTip(tr("The device whose values the form's live line shows: every device listed has this map"));
	auto *bannerRow = new QHBoxLayout(banner_);
	bannerRow->setContentsMargins(0, 0, 0, 4);
	bannerRow->addWidget(bannerText_, 1);
	bannerRow->addWidget(mutedLabel(tr("Live values from")));
	bannerRow->addWidget(liveDevice_);
	banner_->hide();
	connect(liveDevice_, &QComboBox::activated, this, [this] { emit liveDeviceChosen(liveDevice_->currentData().toInt()); });
	layout->addWidget(banner_);
	buildToolbar();
	buildTable();
	buildIssues();
	editor_ = new RegisterEditor(doc_);
	connect(editor_, &RegisterEditor::refused, this, [this](const QString &why) { emit statusMessage(why, 5000); });

	auto *left = new QSplitter(Qt::Vertical);
	tableSplit_ = left;
	left->addWidget(table_);
	issuesBox_ = new QWidget;
	auto *issuesLayout = new QVBoxLayout(issuesBox_);
	issuesLayout->setContentsMargins(0, 6, 0, 0);
	issuesLayout->addWidget(issuesTitle_);
	issuesLayout->addWidget(issues_);
	left->addWidget(issuesBox_);
	left->setStretchFactor(0, 5);
	left->setStretchFactor(1, 1);
	left->setSizes({ 700, 130 });
	refreshIssues(); /* nothing found: the title alone, the table gets the room */
	auto *columns = new QSplitter(Qt::Horizontal);
	columns->setObjectName(QStringLiteral("mapEditorSplit"));
	columns->addWidget(left);
	columns->addWidget(editor_);
	columns->setStretchFactor(0, 1);
	columns->setStretchFactor(1, 0);
	editor_->setMinimumWidth(340);
	/* the table gets the room: the form only as wide as it needs to be */
	columns->setSizes({ 1100, 420 });
	const QByteArray saved = QSettings().value(QStringLiteral("ui/mapEditorSplit")).toByteArray();
	if (!saved.isEmpty()) columns->restoreState(saved);
	connect(columns, &QSplitter::splitterMoved, this, [columns] {
		QSettings().setValue(QStringLiteral("ui/mapEditorSplit"), columns->saveState());
	});
	layout->addWidget(columns, 1);

	/* the keys: undo and redo anywhere in the tab, the clipboard on the table */
	auto shortcut = [this](const QKeySequence &keys, QWidget *on, Qt::ShortcutContext context, auto slot) {
		auto *key = new QShortcut(keys, on);
		key->setContext(context);
		connect(key, &QShortcut::activated, this, slot);
	};
	shortcut(QKeySequence::Undo, this, Qt::WidgetWithChildrenShortcut, [this] { doc_->undoStack()->undo(); });
	shortcut(QKeySequence::Redo, this, Qt::WidgetWithChildrenShortcut, [this] { doc_->undoStack()->redo(); });
	shortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this, Qt::WidgetWithChildrenShortcut,
			[this] { doc_->undoStack()->redo(); });
	shortcut(QKeySequence::Copy, table_, Qt::WidgetShortcut, [this] { copySelected(); });
	shortcut(QKeySequence::Paste, table_, Qt::WidgetShortcut, [this] { paste(); });
	shortcut(QKeySequence(QStringLiteral("Ctrl+D")), table_, Qt::WidgetShortcut, [this] { duplicateSelected(); });
	shortcut(QKeySequence::Delete, table_, Qt::WidgetShortcut, [this] { deleteSelected(); });
	onDocumentChanged();
}

/* ------------------------------------------------------------------ building */

void MapEditorTab::buildToolbar() {
	search_ = new QLineEdit;
	search_->setPlaceholderText(tr("Search: name, address, group, description …"));
	search_->setClearButtonEnabled(true);
	search_->setMaximumWidth(340);
	connect(search_, &QLineEdit::textChanged, filter_, &QSortFilterProxyModel::setFilterFixedString);
	auto *add = new QPushButton(tr("+ Register"));
	add->setObjectName(QStringLiteral("primary"));
	add->setToolTip(tr("A new register after the selected one"));
	connect(add, &QPushButton::clicked, this, [this] {
		const QVector<quint32> selected = selectedUids();
		addRegister(selected.isEmpty() ? 0 : selected.last());
	});
	auto *duplicate = new QPushButton(tr("Duplicate"));
	duplicate->setToolTip(tr("Copies of the selected registers at the next free addresses (Ctrl+D)"));
	connect(duplicate, &QPushButton::clicked, this, &MapEditorTab::duplicateSelected);
	auto *remove = new QPushButton(tr("Delete"));
	remove->setToolTip(tr("The selected registers out of the map (Del; Undo brings them back)"));
	connect(remove, &QPushButton::clicked, this, &MapEditorTab::deleteSelected);
	undoButton_ = new QPushButton(tr("Undo"));
	redoButton_ = new QPushButton(tr("Redo"));
	QUndoStack *undo = doc_->undoStack();
	connect(undoButton_, &QPushButton::clicked, undo, &QUndoStack::undo);
	connect(redoButton_, &QPushButton::clicked, undo, &QUndoStack::redo);
	auto refreshUndo = [this, undo] {
		undoButton_->setEnabled(undo->canUndo());
		redoButton_->setEnabled(undo->canRedo());
		undoButton_->setToolTip(undo->canUndo() ? tr("Undo: %1 (Ctrl+Z)").arg(undo->undoText()) : tr("Nothing to undo"));
		redoButton_->setToolTip(undo->canRedo() ? tr("Redo: %1 (Ctrl+Y)").arg(undo->redoText()) : tr("Nothing to redo"));
	};
	connect(undo, &QUndoStack::indexChanged, this, refreshUndo);
	connect(undo, &QUndoStack::cleanChanged, this, refreshUndo);
	refreshUndo();
	QPushButton *exportButton = buildExportButton();
	auto *import = new QPushButton(tr("Import CSV…"));
	import->setToolTip(tr("Registers from a sheet (the CSV Export writes, or one with at least addr and name columns)"));
	connect(import, &QPushButton::clicked, this, &MapEditorTab::importAsked);
	auto *settings = new QPushButton(tr("Map settings…"));
	settings->setToolTip(tr("The device, its IDs, login, protocol, and notes on the map and its groups"));
	connect(settings, &QPushButton::clicked, this, &MapEditorTab::mapSettingsRequested);

	auto *row = new QHBoxLayout;
	row->addWidget(search_, 1);
	row->addSpacing(8);
	for (QPushButton *button : { add, duplicate, remove, undoButton_, redoButton_ }) row->addWidget(button);
	row->addStretch(1);
	row->addWidget(import);
	row->addWidget(exportButton);
	row->addWidget(settings);
	static_cast<QVBoxLayout *>(layout())->addLayout(row);
}

QPushButton *MapEditorTab::buildExportButton() {
	auto *button = new QPushButton(tr("Export"));
	button->setObjectName(QStringLiteral("mapExport"));
	button->setToolTip(tr("The map for those who implement or use the device"));
	auto *menu = new QMenu(button);
	menu->addAction(tr("Markdown specification…"), this, [this] { exportAsked(QStringLiteral("md")); });
	menu->addAction(tr("C header…"), this, [this] { exportAsked(QStringLiteral("h")); });
	menu->addAction(tr("Python module…"), this, [this] { exportAsked(QStringLiteral("py")); });
	menu->addAction(tr("CSV (a sheet)…"), this, [this] { exportAsked(QStringLiteral("csv")); });
	menu->addSeparator();
	menu->addAction(tr("Device table for the EVRe library (C++)…"), this, [this] { exportAsked(QStringLiteral("table")); });
	menu->addAction(tr("Device table for library 1.1, with EVRe Guard (C++)…"), this, [this] { exportAsked(QStringLiteral("table11")); });
	menu->addAction(tr("EVRe Guard table (C++: .h and .cpp)…"), this, [this] { exportAsked(QStringLiteral("guard")); });
	setButtonMenu(button, menu);
	return button;
}

void MapEditorTab::buildTable() {
	table_ = new QTableView;
	table_->setObjectName(QStringLiteral("mapTable"));
	table_->setModel(filter_);
	table_->setItemDelegate(new CellDelegate(doc_, table_));
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
			| QAbstractItemView::AnyKeyPressed);
	table_->verticalHeader()->hide();
	table_->verticalHeader()->setDefaultSectionSize(26);
	table_->setAlternatingRowColors(true);
	table_->setWordWrap(false);
	QHeaderView *header = table_->horizontalHeader();
	header->setStretchLastSection(true);
	header->setSectionResizeMode(QHeaderView::Interactive);
	/* narrow enough that the description shows on a 1366-wide screen: it was cut at its first words */
	const int widths[MapTableModel::ColCount] = { 56, 72, 160, 56, 44, 56, 56, 60, 110, 240, 200 };
	for (int column = 0; column < MapTableModel::ColCount; column++) table_->setColumnWidth(column, widths[column]);
	table_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(table_, &QWidget::customContextMenuRequested, this, &MapEditorTab::showTableMenu);
	connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
			[this] { editor_->setTargets(selectedUids()); });
}

void MapEditorTab::buildIssues() {
	issuesTitle_ = new QLabel;
	issuesTitle_->setObjectName(QStringLiteral("muted"));
	issues_ = new QListWidget;
	issues_->setObjectName(QStringLiteral("mapIssues"));
	connect(issues_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
		const quint32 uid = item->data(Qt::UserRole).toUInt();
		if (uid) selectRegister(uid);
		else emit mapSettingsRequested();
	});
}

void MapEditorTab::showDevices(const QString &html, const QString &allNames, const QVector<PickerDevice> &devices,
		int liveSlave) {
	banner_->setVisible(!devices.isEmpty());
	const QString whole = QTextDocumentFragment::fromHtml(html).toPlainText();
	bannerText_->setFullText(html, allNames.isEmpty() ? whole : whole + QLatin1Char('\n') + allNames);
	fillDevicePicker(liveDevice_, devices, liveSlave);
	liveDevice_->setEnabled(devices.size() > 1); /* one device has this map: nothing to choose */
}

void MapEditorTab::setLiveValues(std::function<bool(quint32, QByteArray &)> live) {
	editor_->setLiveValues(std::move(live));
}

/* ---------------------------------------------------------------- selection */

QVector<quint32> MapEditorTab::selectedUids() const {
	QVector<int> rows;
	for (const QModelIndex &index : table_->selectionModel()->selectedRows()) rows << filter_->mapToSource(index).row();
	std::sort(rows.begin(), rows.end());
	QVector<quint32> uids;
	for (int row : rows) uids << model_->uidAt(row);
	return uids;
}

void MapEditorTab::select(const QVector<quint32> &uids) {
	QItemSelection selection;
	QModelIndex first;
	for (quint32 uid : uids) {
		const int row = doc_->indexOf(uid);
		if (row < 0) continue;
		const QModelIndex index = filter_->mapFromSource(model_->index(row, MapTableModel::ColName));
		if (!index.isValid()) continue; /* hidden by the search */
		selection.select(index.siblingAtColumn(0), index.siblingAtColumn(MapTableModel::ColCount - 1));
		if (!first.isValid()) first = index;
	}
	table_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
	if (first.isValid()) {
		table_->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
		table_->scrollTo(first);
	}
	editor_->setTargets(selectedUids());
}

void MapEditorTab::selectRegister(quint32 uid) {
	search_->clear();
	select({ uid });
	table_->setFocus();
}

void MapEditorTab::onDocumentChanged() {
	/* the uids that are still in the map */
	QVector<quint32> kept;
	for (quint32 uid : keptSelection_)
		if (doc_->indexOf(uid) >= 0) kept << uid;
	select(kept);
	refreshIssues();
}

void MapEditorTab::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (Theme::switched(event, drawnDark_)) refreshIssues();
}

void MapEditorTab::refreshIssues() {
	const QVector<MapIssue> &issues = doc_->issues();
	issues_->clear();
	int errors = 0;
	/* errors first, then warnings, each in map order */
	for (bool wantError : { true, false }) {
		for (const MapIssue &issue : issues) {
			if (issue.error != wantError) continue;
			errors += issue.error;
			const quint32 uid = issue.reg >= 0 ? doc_->map().regs[issue.reg].uid : 0;
			auto *item = new QListWidgetItem(QStringLiteral("●  %1").arg(issue.text));
			item->setForeground(issue.error ? Theme::colors().bad : Theme::colors().warn);
			item->setData(Qt::UserRole, uid);
			item->setToolTip(issue.reg >= 0 ? tr("Click: select the register") : tr("Click: the map settings"));
			issues_->addItem(item);
		}
	}
	/* nothing found: no empty box, only the title (the table takes the room); found: the list back, as tall as before */
	if (issuesBox_) {
		const bool wasEmpty = issues_->isHidden();
		issues_->setVisible(!issues.isEmpty());
		issuesBox_->setMaximumHeight(issues.isEmpty() ? issuesBox_->sizeHint().height() : QWIDGETSIZE_MAX);
		if (wasEmpty && !issues.isEmpty()) {
			const QList<int> sizes = tableSplit_->sizes();
			const int total = sizes.value(0) + sizes.value(1);
			tableSplit_->setSizes({ std::max(0, total - 130), 130 });
		}
	}
	const int warnings = int(issues.size()) - errors;
	issuesTitle_->setText(issues.isEmpty() ? tr("CHECKS  ·  nothing found")
			: tr("CHECKS  ·  %1, %2").arg(tr("%n error(s)", nullptr, errors), tr("%n warning(s)", nullptr, warnings)));
}

void MapEditorTab::showTableMenu(const QPoint &pos) {
	const QVector<quint32> selected = selectedUids();
	QMenu menu(this);
	menu.addAction(tr("+ Register after"), this, [this, selected] { addRegister(selected.isEmpty() ? 0 : selected.last()); });
	if (!selected.isEmpty()) {
		menu.addAction(tr("Duplicate") + QStringLiteral("\tCtrl+D"), this, &MapEditorTab::duplicateSelected);
		menu.addAction(tr("Copy") + QStringLiteral("\tCtrl+C"), this, &MapEditorTab::copySelected);
	}
	menu.addAction(tr("Paste") + QStringLiteral("\tCtrl+V"), this, &MapEditorTab::paste);
	if (!selected.isEmpty()) {
		menu.addSeparator();
		menu.addAction(tr("Delete") + QStringLiteral("\tDel"), this, &MapEditorTab::deleteSelected);
	}
	menu.exec(table_->viewport()->mapToGlobal(pos));
}

/* ------------------------------------------------------------------ editing */

void MapEditorTab::addRegister(quint32 afterUid) {
	const RegDef *after = afterUid ? doc_->reg(afterUid) : nullptr;
	if (!after && !doc_->map().regs.isEmpty()) after = &doc_->map().regs.last();
	RegDef def;
	def.type = after && after->type != RegType::Bytes ? after->type : RegType::U16;
	def.size = typeSize(def.type);
	def.group = after ? after->group : tr("Registers");
	def.addr = doc_->freeAddressAfter(after ? uint16_t(std::min(0xFFFF, after->addr + after->size)) : 0xD000, def.size);
	QSet<QString> names;
	for (const RegDef &reg : doc_->map().regs) names.insert(reg.name);
	def.name = freeName(QStringLiteral("REG_%1").arg(def.addr, 4, 16, QLatin1Char('0')).toUpper(), names);
	def.uid = doc_->newUid();
	const quint32 uid = def.uid;
	doc_->edit(tr("Add %1").arg(def.name), [def](DeviceMap &map) { map.regs.push_back(def); });
	search_->clear();
	select({ uid });
}

void MapEditorTab::deleteSelected() {
	const QVector<quint32> uids = selectedUids();
	if (uids.isEmpty()) return;
	/* the row after the last one deleted is selected next */
	const int last = doc_->indexOf(uids.last());
	const quint32 next = model_->uidAt(last + 1);
	const RegDef *one = doc_->reg(uids.front());
	doc_->edit(uids.size() == 1 && one ? tr("Delete %1").arg(one->name)
			: tr("Delete %n registers", nullptr, int(uids.size())), [uids](DeviceMap &map) {
		map.regs.removeIf([&uids](const RegDef &def) { return uids.contains(def.uid); });
	});
	select(next ? QVector<quint32>{ next } : QVector<quint32>{});
	emit statusMessage(tr("%n register(s) deleted (Ctrl+Z brings them back)", nullptr, int(uids.size())), 4000);
}

void MapEditorTab::copySelected() {
	QVector<RegDef> regs;
	for (quint32 uid : selectedUids())
		if (const RegDef *def = doc_->reg(uid)) regs << *def;
	if (regs.isEmpty()) return;
	const QByteArray json = registersToJson(regs);
	auto *mime = new QMimeData;
	mime->setData(QLatin1String(MIME_REGISTERS), json);
	mime->setText(QString::fromUtf8(json));
	QApplication::clipboard()->setMimeData(mime);
	emit statusMessage(tr("%n register(s) copied, as JSON", nullptr, int(regs.size())), 3000);
}

void MapEditorTab::paste() {
	const QMimeData *mime = QApplication::clipboard()->mimeData();
	const QByteArray text = mime && mime->hasFormat(QLatin1String(MIME_REGISTERS))
			? mime->data(QLatin1String(MIME_REGISTERS)) : QApplication::clipboard()->text().toUtf8();
	QVector<RegDef> regs;
	QString err;
	if (text.trimmed().isEmpty() || !registersFromJson(text, regs, err)) {
		emit statusMessage(tr("Nothing to paste: the clipboard holds no registers (%1)").arg(err), 5000);
		return;
	}
	insertRegisters(regs, tr("Paste %n register(s)", nullptr, int(regs.size())));
}

void MapEditorTab::duplicateSelected() {
	QVector<RegDef> regs;
	for (quint32 uid : selectedUids())
		if (const RegDef *def = doc_->reg(uid)) regs << *def; /* source kept: the copy has the keys the file gave */
	if (regs.isEmpty()) return;
	insertRegisters(regs, tr("Duplicate %n register(s)", nullptr, int(regs.size())));
}

/* ---------------------------------------------------------- export, import */

bool MapEditorTab::exportTo(const QString &kind, const QString &file, const QString &prefix, QString &err) {
	ExportOptions options;
	options.source = doc_->map().path.isEmpty() ? QString() : QFileInfo(doc_->map().path).fileName();
	options.prefix = prefix;
	const DeviceMap &map = doc_->map();
	QByteArray text;
	if (kind == QLatin1String("guard")) {
		/* two files: the .h chosen, and the .cpp beside it */
		QByteArray header;
		QStringList problems;
		const QFileInfo info(file);
		const QString base = info.path() + QLatin1Char('/') + info.completeBaseName();
		if (!exportGuard(map, options, info.completeBaseName() + QStringLiteral(".h"), header, text, problems)) {
			err = tr("This map has no EVRe Guard table yet:") + QStringLiteral("\n\n- ") + problems.join(QStringLiteral("\n- "));
			return false;
		}
		for (const auto &one : { qMakePair(base + QStringLiteral(".h"), header), qMakePair(base + QStringLiteral(".cpp"), text) }) {
			QFile out(one.first);
			if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(one.second) != one.second.size()) {
				err = out.errorString();
				return false;
			}
		}
		return true;
	}
	if (kind == QLatin1String("table") || kind == QLatin1String("table11")) {
		QStringList problems;
		const bool lib11 = kind == QLatin1String("table11");
		if (!(lib11 ? exportDeviceTable11(map, options, text, problems) : exportDeviceTable(map, options, text, problems))) {
			err = tr("The EVRe library cannot serve this map as it is:") + QStringLiteral("\n\n- ")
					+ problems.join(QStringLiteral("\n- "));
			return false;
		}
	} else {
		text = kind == QLatin1String("md") ? exportMarkdown(map, options)
				: kind == QLatin1String("h") ? exportCHeader(map, options)
				: kind == QLatin1String("py") ? exportPython(map, options) : exportCsv(map);
	}
	QFile out(file);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(text) != text.size()) {
		err = out.errorString();
		return false;
	}
	return true;
}

void MapEditorTab::exportAsked(const QString &kind) {
	const DeviceMap &map = doc_->map();
	const QString folder = map.path.isEmpty() ? QDir::homePath() : QFileInfo(map.path).absolutePath();
	QString base = map.path.isEmpty() ? identifier(map.device).toLower() : QFileInfo(map.path).completeBaseName();
	if (kind == QLatin1String("py")) base = identifier(base).toLower(); /* a module name Python can import */
	const bool table = kind == QLatin1String("table") || kind == QLatin1String("table11"), guard = kind == QLatin1String("guard");
	if (table) base += QStringLiteral("_table");
	if (guard) base += QStringLiteral("_guard");
	const QString filter = kind == QLatin1String("md") ? tr("Markdown (*.md)") : kind == QLatin1String("h") ? tr("C header (*.h)")
			: table ? tr("C++ header (*.h)") : guard ? tr("C++ header, and its .cpp beside it (*.h)")
			: kind == QLatin1String("py") ? tr("Python (*.py)") : tr("CSV (*.csv)");
	const QString file = QFileDialog::getSaveFileName(this, tr("Export the map"),
			folder + QLatin1Char('/') + base + QLatin1Char('.') + (table || guard ? QStringLiteral("h") : kind), filter);
	if (file.isEmpty()) return;
	QString prefix;
	if (kind == QLatin1String("h") || kind == QLatin1String("py") || table || guard) {
		bool ok = false;
		prefix = QInputDialog::getText(this, tr("Export the map"),
				guard ? tr("A prefix for the names (MYDEV makes mydev_table and MYDEV_SPEED_RAW_MIN), or empty for the device's name:")
				: kind == QLatin1String("table11")
						? tr("A prefix for the names (MYDEV makes mydev_image_t and mydev_bind), or empty for the device's name:")
				: table ? tr("A prefix for the names (MYDEV makes mydev_rw_t and MYDEV_WRITE_MIN), or empty for the device's name:")
						: tr("A prefix for the names (MYDEV makes MYDEV_SPEED_ADDR), or empty for none:"),
				QLineEdit::Normal, QString(), &ok);
		if (!ok) return;
	}
	QString err;
	if (!exportTo(kind, file, prefix.trimmed(), err)) {
		QMessageBox::warning(this, tr("Not exported"), tr("%1\n\n%2").arg(QDir::toNativeSeparators(file), err));
		return;
	}
	emit statusMessage(tr("Exported: %1").arg(QDir::toNativeSeparators(file)), 5000);
}

bool MapEditorTab::importCsvFrom(const QString &file, bool replaceAll, QString &err) {
	QFile in(file);
	if (!in.open(QIODevice::ReadOnly)) {
		err = in.errorString();
		return false;
	}
	QVector<RegDef> regs;
	if (!importCsv(in.readAll(), regs, err)) return false;
	const QString name = QFileInfo(file).fileName();
	doc_->edit(replaceAll ? tr("Registers from %1").arg(name) : tr("Import %1").arg(name), [regs, replaceAll](DeviceMap &map) {
		if (replaceAll) {
			map.regs = regs;
			return;
		}
		for (const RegDef &def : regs) {
			/* the same address: the imported one instead */
			map.regs.removeIf([&def](const RegDef &old) { return old.addr == def.addr; });
			map.regs.push_back(def);
		}
	});
	emit statusMessage(tr("%n register(s) imported from %1", nullptr, int(regs.size())).arg(name), 5000);
	return true;
}

void MapEditorTab::importAsked() {
	const QString folder = doc_->map().path.isEmpty() ? QDir::homePath() : QFileInfo(doc_->map().path).absolutePath();
	const QString file = QFileDialog::getOpenFileName(this, tr("Import registers from CSV"), folder, tr("CSV (*.csv)"));
	if (file.isEmpty()) return;
	QMessageBox box(QMessageBox::Question, tr("Import CSV"),
			tr("The registers of %1:").arg(QFileInfo(file).fileName()), QMessageBox::Cancel, this);
	QAbstractButton *add = box.addButton(tr("Add them (the same address: replaced)"), QMessageBox::AcceptRole);
	QAbstractButton *replace = box.addButton(tr("Instead of the map's registers"), QMessageBox::DestructiveRole);
	box.setDefaultButton(QMessageBox::Cancel);
	box.exec();
	if (box.clickedButton() != add && box.clickedButton() != replace) return;
	QString err;
	if (!importCsvFrom(file, box.clickedButton() == replace, err))
		QMessageBox::warning(this, tr("Not imported"), tr("%1\n\n%2").arg(QDir::toNativeSeparators(file), err));
}

void MapEditorTab::insertRegisters(QVector<RegDef> regs, const QString &step) {
	const QVector<RegDef> &map = doc_->map().regs;
	auto freeAt = [&map](int addr, int size) {
		if (addr < 0 || addr + size > 0x10000) return false;
		return std::none_of(map.begin(), map.end(), [&](const RegDef &d) { return overlaps(addr, size, d); });
	};
	const bool allFree = std::all_of(regs.begin(), regs.end(), [&](const RegDef &d) { return freeAt(d.addr, d.size); });
	if (!allFree) {
		/* moved as a block, the gaps between them kept: to the first place after the selection they all fit */
		int base = 0x10000, end = 0;
		for (const RegDef &def : regs) {
			base = std::min(base, int(def.addr));
			end = std::max(end, def.addr + def.size);
		}
		const QVector<quint32> selected = selectedUids();
		const RegDef *after = selected.isEmpty() ? nullptr : doc_->reg(selected.last());
		int start = after ? after->addr + after->size : (map.isEmpty() ? 0xD000 : map.last().addr + map.last().size);
		for (bool moved = true; moved && start + (end - base) <= 0x10000;) {
			moved = false;
			for (const RegDef &def : regs) {
				const int addr = start + def.addr - base;
				for (const RegDef &taken : map) {
					if (overlaps(addr, def.size, taken)) {
						start = taken.addr + taken.size - (def.addr - base);
						moved = true;
						break;
					}
				}
				if (moved) break;
			}
		}
		if (start + (end - base) > 0x10000) {
			emit statusMessage(tr("No room for them after the selection"), 5000);
			return;
		}
		for (RegDef &def : regs) def.addr = uint16_t(start + def.addr - base);
	}
	QSet<QString> names;
	for (const RegDef &def : map) names.insert(def.name);
	QVector<quint32> uids;
	for (RegDef &def : regs) {
		def.name = freeName(def.name, names);
		names.insert(def.name);
		def.uid = doc_->newUid();
		uids << def.uid;
	}
	doc_->edit(step, [regs](DeviceMap &m) { m.regs += regs; });
	search_->clear();
	select(uids);
}

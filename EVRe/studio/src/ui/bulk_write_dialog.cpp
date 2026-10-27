/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog before a bulk write: see bulk_write_dialog.h. */
#include "ui/bulk_write_dialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "ui/theme.h"
#include "ui/ui_helpers.h"

namespace {

enum Column { ColRegister, ColAddress, ColDevice, ColNow, ColNew, ColNote, ColumnCount };

constexpr int LIST_ROWS = 8; /* rows shown before the list scrolls */

QTableWidgetItem *cell(const QString &text, const QColor &color = QColor()) {
	auto *item = new QTableWidgetItem(text);
	item->setFlags(Qt::ItemIsEnabled);
	if (color.isValid()) item->setForeground(color);
	return item;
}

} // namespace

QString BulkWriteDialog::shownText(const RegDef &def, const QByteArray &raw) {
	const QString value = formatValue(def, raw);
	QString name;
	if (def.isNumeric()) {
		name = specialName(def, decodeNumber(def, raw));
		if (name.isEmpty()) name = def.enumValues.value(decodeRaw(def, raw));
	}
	if (!name.isEmpty()) return QStringLiteral("%1 (%2)").arg(name, value);
	return def.unit.isEmpty() ? value : value + QLatin1Char(' ') + def.unit;
}

BulkWriteDialog::Entry BulkWriteDialog::evaluate(const RegDef &def, const QString &text) {
	Entry entry;
	if (!encodeValue(def, text, entry.bytes, entry.refused)) {
		entry.bytes.clear();
		return entry;
	}
	/* a special value is never outside them */
	if (def.isNumeric()) entry.outside = limitProblem(def, decodeNumber(def, entry.bytes));
	return entry;
}

BulkWriteDialog::BulkWriteDialog(const QVector<Target> &targets, const QStringList &leftOut, const QString &text,
		QWidget *parent)
	: QDialog(parent), targets_(targets), leftOut_(leftOut), text_(text.trimmed()) {
	setObjectName(QStringLiteral("bulkWrite"));
	setWindowTitle(tr("Write to %n registers", nullptr, int(targets_.size())));
	noWindowAnimation(this); /* the live chart waits behind it */
	auto *layout = new QVBoxLayout(this);

	/* the value: a box to type it in, or the one typed in the table */
	auto *valueRow = new QHBoxLayout;
	valueRow->addWidget(new QLabel(tr("Value")));
	if (text_.isEmpty()) {
		value_ = new QLineEdit;
		value_->setObjectName(QStringLiteral("bulkValue"));
		value_->setPlaceholderText(tr("one value for all: 12, 0x1F, 0b101, or a name"));
		value_->setMinimumWidth(260);
		value_->installEventFilter(this);
		connect(value_, &QLineEdit::textChanged, this, &BulkWriteDialog::refresh);
		valueRow->addWidget(value_, 1);
	} else {
		auto *typed = new QLabel(QStringLiteral("<b>%1</b>").arg(ltrPiece(text_).toHtmlEscaped()));
		typed->setObjectName(QStringLiteral("bulkValueTyped"));
		typed->setTextInteractionFlags(Qt::TextSelectableByMouse);
		valueRow->addWidget(typed);
		valueRow->addStretch(1); /* the value beside its label, in both directions */
	}
	layout->addLayout(valueRow);

	list_ = new QTableWidget(int(targets_.size()), ColumnCount);
	list_->setObjectName(QStringLiteral("bulkList"));
	list_->setHorizontalHeaderLabels({ tr("Register"), tr("Address"), tr("Device"), tr("Now"), tr("New"), tr("Note") });
	list_->verticalHeader()->hide();
	list_->verticalHeader()->setDefaultSectionSize(28);
	list_->setSelectionMode(QAbstractItemView::NoSelection);
	list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	list_->setFocusPolicy(Qt::NoFocus);
	list_->setShowGrid(false);
	list_->setWordWrap(false);
	list_->setAlternatingRowColors(true);
	list_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
	list_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	list_->horizontalHeader()->setHighlightSections(false);
	/* numbers, units and addresses one piece in Arabic, as the Registers tab's values */
	for (const int column : { int(ColAddress), int(ColNow), int(ColNew) })
		list_->setItemDelegateForColumn(column, ltrCells(list_));
	const bool onBus = std::any_of(targets_.begin(), targets_.end(), [](const Target &t) { return !t.device.isEmpty(); });
	list_->setColumnHidden(ColDevice, !onBus);
	layout->addWidget(list_, 1);

	notes_ = new QLabel;
	notes_->setObjectName(QStringLiteral("bulkNotes"));
	notes_->setTextFormat(Qt::RichText);
	notes_->setWordWrap(true);
	layout->addWidget(notes_);

	/* Cancel the default, with the focus: Enter and Space cancel, as "Confirm write" does */
	auto *buttons = new QDialogButtonBox;
	cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
	write_ = buttons->addButton(tr("Write"), QDialogButtonBox::AcceptRole);
	write_->setObjectName(QStringLiteral("bulkWriteButton"));
	write_->setAutoDefault(false); /* reached by Tab, Enter still cancels: Space or a click writes */
	cancel_->setDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	refresh();
	if (value_) value_->setFocus();
	else cancel_->setFocus();
}

QString BulkWriteDialog::text() const { return value_ ? value_->text().trimmed() : text_; }

int BulkWriteDialog::writable() const {
	return int(std::count_if(entries_.begin(), entries_.end(), [](const Entry &e) { return !e.bytes.isEmpty(); }));
}

bool BulkWriteDialog::eventFilter(QObject *watched, QEvent *event) {
	/* the list follows the typing already; Enter here would press the default, Cancel, and lose what was typed */
	if (watched == value_ && event->type() == QEvent::KeyPress) {
		const int key = static_cast<QKeyEvent *>(event)->key();
		if (key == Qt::Key_Return || key == Qt::Key_Enter) return true;
	}
	return QDialog::eventFilter(watched, event);
}

void BulkWriteDialog::refresh() {
	const ThemeColors &colors = Theme::colors();
	const QString typed = text();
	entries_.clear();
	int outside = 0;
	QStringList danger;
	for (int i = 0; i < targets_.size(); i++) {
		const Target &target = targets_[i];
		const RegDef &def = target.def;
		const Entry entry = typed.isEmpty() ? Entry() : evaluate(def, typed);
		entries_ << entry;
		const bool refused = !typed.isEmpty() && entry.bytes.isEmpty();
		if (!entry.bytes.isEmpty() && !entry.outside.isEmpty()) outside++;
		if (def.danger) danger << def.name;
		/* a register that would not take it: greyed, its reason in the note */
		const QColor rowColor = refused ? colors.muted : QColor();
		list_->setItem(i, ColRegister, cell(def.danger ? def.name + QStringLiteral(" ⚠") : def.name,
				refused ? colors.muted : def.danger ? colors.warn : QColor()));
		list_->setItem(i, ColAddress, cell(addrText(def.addr), rowColor));
		list_->setItem(i, ColDevice, cell(target.device, rowColor));
		/* "—" as the table writes a value not read yet; and for a value not taken */
		const QString now = target.raw.size() >= def.size ? shownText(def, target.raw) : QStringLiteral("—");
		list_->setItem(i, ColNow, cell(now, rowColor));
		list_->setItem(i, ColNew, cell(entry.bytes.isEmpty() ? QStringLiteral("—") : shownText(def, entry.bytes), rowColor));
		list_->setItem(i, ColNote, cell(refused ? tr("refused: %1").arg(entry.refused) : entry.outside,
				refused ? colors.bad : colors.warn));
	}
	const int total = int(targets_.size());
	const int ok = writable();

	/* the button says what happens: how many, and whether past a limit */
	if (typed.isEmpty() || ok == 0) write_->setText(tr("Write"));
	else if (ok < total) write_->setText(outside > 0 ? tr("Write %1 of %2 anyway").arg(ok).arg(total)
			: tr("Write %1 of %2").arg(ok).arg(total));
	else write_->setText(outside > 0 ? tr("Write %1 anyway").arg(ok) : tr("Write %1").arg(ok));
	write_->setEnabled(ok > 0);

	/* the names one left-to-right piece: in Arabic a line that starts with one still reads right to left */
	QStringList lines;
	const QString names = ltrPiece(leftOut_.join(QStringLiteral(", "))).toHtmlEscaped();
	if (leftOut_.size() == 1) lines << tr("%1 is read-only: left out.").arg(names);
	else if (leftOut_.size() > 1) lines << tr("%1 are read-only: left out.").arg(names);
	if (!typed.isEmpty() && ok == 0) lines << coloredSpan(tr("None of them takes this value."), colors.bad);
	if (!danger.isEmpty()) {
		const QString which = ltrPiece(danger.join(QStringLiteral(", "))).toHtmlEscaped();
		lines << coloredSpan(danger.size() == 1 ? tr("⚠ %1 moves, powers or changes something on the device.").arg(which)
				: tr("⚠ %1 move, power or change something on the device.").arg(which), colors.warn);
	}
	lines << coloredSpan(tr("There is no undo: the Log keeps the values replaced."), colors.muted);
	const QString text = lines.join(QStringLiteral("<br>"));
	notes_->setText(QGuiApplication::isRightToLeft() ? QStringLiteral("<div dir='rtl'>%1</div>").arg(text) : text);
	fitList();
}

void BulkWriteDialog::fitList() {
	list_->resizeColumnsToContents();
	QHeaderView *header = list_->horizontalHeader();
	for (int column = 0; column < ColumnCount; column++)
		if (!list_->isColumnHidden(column)) header->resizeSection(column, header->sectionSize(column) + 12);
	/* as wide as its columns, but no wider than the window it is opened over: past that a scroll bar, nothing cut */
	const int frame = 2 * list_->frameWidth();
	const bool scrolls = list_->rowCount() > LIST_ROWS;
	const int need = header->length() + frame + (scrolls ? list_->verticalScrollBar()->sizeHint().width() : 0);
	const QWidget *over = parentWidget() ? parentWidget()->window() : nullptr;
	const int screenWidth = screen() ? screen()->availableGeometry().width() : 1280;
	const int room = std::max(360, (over ? std::min(over->width(), screenWidth) : screenWidth) - 120);
	const bool cut = need > room;
	/* while a value is typed the list only grows: the dialog does not jump narrower and back at each key */
	const int width = std::max(std::min(need, room), list_->minimumWidth());
	list_->setFixedWidth(width);
	const int rows = std::min(list_->rowCount(), LIST_ROWS);
	list_->setFixedHeight(header->sizeHint().height() + rows * list_->verticalHeader()->defaultSectionSize() + frame
			+ (cut ? list_->horizontalScrollBar()->sizeHint().height() : 0));
	notes_->setMaximumWidth(std::max(width, 360));
	adjustSize();
}

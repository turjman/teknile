/* SPDX-License-Identifier: Apache-2.0 */
/* The quick-write panel under the register table: see quick_write_panel.h. */
#include "ui/quick_write_panel.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QLocale>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "model/register_model.h"
#include "ui/bit_view.h"
#include "ui/bulk_write_dialog.h"
#include "ui/ui_helpers.h"

namespace {

/* a register's raw bytes (little endian) as an unsigned integer */
quint64 rawBits(const QByteArray &raw) {
	quint64 bits = 0;
	for (int i = 0; i < raw.size() && i < 8; i++) bits |= quint64(uint8_t(raw[i])) << (8 * i);
	return bits;
}

bool isSigned(RegType type) { return type == RegType::I8 || type == RegType::I16 || type == RegType::I32; }

/* the register's bits as the text encodeValue takes: negative when the top bit of a signed register is set */
QString bitsText(const RegDef &def, quint64 bits) {
	const int width = 8 * def.size;
	bits &= bitMask(width);
	if (isSigned(def.type) && width < 64 && (bits >> (width - 1)) & 1)
		return QString::number(qint64(bits) - qint64(1ULL << width));
	return QString::number(bits);
}

/* a shown value as the text encodeValue takes: 30, -1, 3.65 */
QString numberText(double value) {
	if (value == std::floor(value) && std::fabs(value) < 1e15) return QString::number(qint64(value));
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

/* The list's entries: an enum name's data is its raw number; a special value's is "s" and its shown
 * value (a text, so the two kinds never match each other). listKey: the entry of the value held. */
QVariant listKey(const RegDef &def, const QByteArray &raw) {
	const double shown = decodeNumber(def, raw);
	if (!specialName(def, shown).isEmpty()) return QStringLiteral("s") + numberText(shown);
	return QVariant(qint64(decodeRaw(def, raw)));
}

/* the bit view is for integer registers whose value shown is the raw integer (no scale, no offset) */
bool bitsWritable(const RegDef &def) { return def.isNumeric() && def.type != RegType::F32 && !def.isFloat(); }

/* two registers whose names list would be the same: the same special values and the same value names */
bool sameNames(const RegDef &a, const RegDef &b) {
	if (a.enumValues != b.enumValues || a.special.size() != b.special.size()) return false;
	for (int i = 0; i < a.special.size(); i++)
		if (a.special[i].name != b.special[i].name || a.special[i].value != b.special[i].value) return false;
	return true;
}

constexpr int TARGET_ROWS = 4; /* several selected: the rows of their list shown before it scrolls */

} // namespace

QuickWritePanel::QuickWritePanel(const RegisterModel *model, QWidget *parent) : QFrame(parent), model_(model) {
	setObjectName(QStringLiteral("quickWrite"));
	name_ = new QLabel;
	name_->setObjectName(QStringLiteral("qwTitle"));
	value_ = new QLineEdit;
	value_->setObjectName(QStringLiteral("qwValue"));
	value_->setPlaceholderText(tr("value, 0x1F, 0b101, or a name"));
	value_->setMaximumWidth(260);
	namedValues_ = new QComboBox;
	namedValues_->setObjectName(QStringLiteral("qwEnum"));
	namedValues_->setMinimumWidth(160);
	write_ = new QPushButton(tr("Write"));
	write_->setObjectName(QStringLiteral("primary"));
	write_->setCursor(Qt::PointingHandCursor);
	default_ = new QPushButton(tr("Default"));
	default_->setObjectName(QStringLiteral("qwDefault"));
	broadcast_ = new QPushButton(tr("To all devices"));
	broadcast_->setObjectName(QStringLiteral("qwBroadcast"));
	broadcast_->hide();
	bits_ = new QCheckBox(tr("Bits"));
	bits_->setToolTip(tr("A button for every bit of the register: click one to flip it"));
	bits_->setChecked(QSettings().value(QStringLiteral("ui/quickBits"), false).toBool());
	hint_ = mutedLabel(QString());
	bitArea_ = new QWidget;
	auto *bitLayout = new QVBoxLayout(bitArea_);
	bitLayout->setContentsMargins(0, 0, 0, 0);
	/* several selected: each one and its value, a few rows high (a scroll bar for more) */
	targets_ = new QTableWidget(0, 2);
	targets_->setObjectName(QStringLiteral("qwTargets"));
	targets_->horizontalHeader()->hide();
	targets_->verticalHeader()->hide();
	targets_->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 8);
	targets_->setSelectionMode(QAbstractItemView::NoSelection);
	targets_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	targets_->setFocusPolicy(Qt::NoFocus);
	targets_->setShowGrid(false);
	targets_->setWordWrap(false);
	targets_->setAlternatingRowColors(true);
	targets_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); /* as wide as its columns: nothing to scroll */
	targets_->setItemDelegateForColumn(1, ltrCells(targets_)); /* "-1500 rpm" one piece in Arabic */
	targets_->setToolTip(tr("The registers selected, each with its value now. Write sends the value to all of them, "
			"after a list of what each one gets"));
	targets_->hide();

	auto *row = new QHBoxLayout;
	row->setSpacing(6);
	row->addWidget(name_);
	row->addWidget(value_, 1);
	row->addWidget(namedValues_);
	row->addWidget(write_);
	row->addWidget(default_);
	row->addWidget(broadcast_);
	row->addSpacing(8);
	row->addWidget(bits_);
	row->addStretch();
	row->addWidget(hint_);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 0, 10, 6);
	layout->setSpacing(6);
	layout->addLayout(row);
	layout->addWidget(targets_, 0, Qt::AlignLeading);
	layout->addWidget(bitArea_);

	connect(write_, &QPushButton::clicked, this, &QuickWritePanel::writeTyped);
	connect(value_, &QLineEdit::returnPressed, this, &QuickWritePanel::writeTyped);
	connect(value_, &QLineEdit::textChanged, this, &QuickWritePanel::updateBroadcast);
	connect(namedValues_, &QComboBox::activated, this, &QuickWritePanel::writeNamedValue);
	connect(broadcast_, &QPushButton::clicked, this, [this] {
		const QString text = value_->text().trimmed();
		if (row_ >= 0 && !text.isEmpty()) emit broadcastRequested(row_, text);
	});
	connect(default_, &QPushButton::clicked, this, [this] {
		if (row_ < 0 || row_ >= model_->rows().size()) return;
		const RegDef &def = model_->rows()[row_].def;
		if (def.hasDefault()) requestWrite(numberText(def.defaultValue));
	});
	connect(bits_, &QCheckBox::toggled, this, [this](bool on) {
		QSettings().setValue(QStringLiteral("ui/quickBits"), on);
		rebuild();
	});
	/* new values, and Allow writes switched (the model tells every value cell then) */
	connect(model_, &QAbstractItemModel::dataChanged, this, &QuickWritePanel::onValuesChanged);
	/* a new map: its rows are new, none is selected */
	connect(model_, &QAbstractItemModel::modelReset, this, [this] { showRegister(-1); });
	/* the map edited, the same rows kept: several selected, their title and names list anew (RegistersTab gives the
	 * rows again when their types changed) */
	connect(model_, &RegisterModel::structureChanged, this, [this] {
		if (!rows_.isEmpty()) rebuild();
	});
	showRegister(-1);
}

void QuickWritePanel::showRegister(int row) {
	row_ = row;
	rows_.clear();
	problem_.clear();
	rebuild();
}

void QuickWritePanel::showRegisters(const QVector<int> &rows, const QString &problem) {
	if (rows == rows_ && problem == problem_) return; /* another of them made current: the box keeps what is typed */
	row_ = -1;
	rows_ = rows;
	problem_ = problem;
	rebuild();
}

void QuickWritePanel::setConnected(bool connected) {
	connected_ = connected;
	refresh();
}

void QuickWritePanel::setBroadcastRule(BroadcastRule rule) {
	broadcastRule_ = std::move(rule);
	refresh();
}

void QuickWritePanel::rebuild() {
	delete bitView_;
	bitView_ = nullptr;
	if (!rows_.isEmpty()) {
		rebuildBulk();
		return;
	}
	targets_->hide();
	if (row_ < 0 || row_ >= model_->rows().size() || !model_->rows()[row_].def.rw) {
		hide();
		return;
	}
	show();
	const RegDef def = model_->rows()[row_].def;
	const QString dangerMark = def.danger ? QStringLiteral(" ⚠") : QString();
	name_->setText(tr("Write <b>%1</b>%2").arg(def.name.toHtmlEscaped(), dangerMark));
	value_->clear();
	/* the range the map gives, in the box until something is typed */
	QString range;
	const QString unit = def.unit.isEmpty() ? QString() : QStringLiteral(" ") + def.unit;
	if (def.hasMin() || def.hasMax())
		range = tr("%1 … %2%3").arg(def.hasMin() ? numberText(def.min) : QString(),
				def.hasMax() ? numberText(def.max) : QString(), unit);
	value_->setPlaceholderText(range.isEmpty() ? tr("value, 0x1F, 0b101, or a name")
			: tr("value (%1), or a name").arg(range));
	default_->setVisible(def.hasDefault());
	default_->setToolTip(def.hasDefault() ? tr("Write the default: %1%2").arg(numberText(def.defaultValue), unit)
			: QString());
	fillNamedValues(def);
	const bool withBits = bitsWritable(def);
	bits_->setVisible(withBits && def.fields.isEmpty()); /* with fields the bits are always shown */
	/* the register as in a datasheet: always for one with fields, with "Bits" for any other integer register */
	if (withBits && (!def.fields.isEmpty() || bits_->isChecked())) {
		bitView_ = new BitView;
		bitView_->setObjectName(QStringLiteral("bitView"));
		bitView_->setRegister(def);
		bitArea_->layout()->addWidget(bitView_);
		connect(bitView_, &BitView::writeField, this, &QuickWritePanel::writeField);
	}
	refresh();
}

/* the names: special values first (in shown units), then the enum's */
void QuickWritePanel::fillNamedValues(const RegDef &def) {
	namedValues_->clear();
	for (const SpecialValue &special : def.special)
		namedValues_->addItem(QStringLiteral("%1  (%2)").arg(special.name, numberText(special.value)),
				QStringLiteral("s") + numberText(special.value));
	for (auto it = def.enumValues.begin(); it != def.enumValues.end(); ++it)
		namedValues_->addItem(QStringLiteral("%1  (%2)").arg(it.value()).arg(it.key()), it.key());
	namedValues_->setVisible(namedValues_->count() > 0);
}

/* several selected: what the panel writes, the names they all have, and a row for each in the list; what belongs to
 * one register (Default, To all devices, the bits) is hidden */
void QuickWritePanel::rebuildBulk() {
	const int count = int(rows_.size());
	for (const int row : std::as_const(rows_))
		if (row < 0 || row >= model_->rows().size()) {
			hide();
			return;
		}
	show();
	const RegDef first = model_->rows()[rows_.first()].def;
	bool danger = false, shared = true;
	for (const int row : std::as_const(rows_)) {
		const RegDef &def = model_->rows()[row].def;
		danger = danger || def.danger;
		shared = shared && sameNames(def, first);
	}
	if (problem_.isEmpty()) {
		const QString type = first.type == RegType::Bytes ? QStringLiteral("bytes[%1]").arg(first.size) : typeName(first.type);
		name_->setText(tr("Write <b>%n selected registers</b> (%1)", nullptr, count).arg(type)
				+ (danger ? QStringLiteral(" ⚠") : QString()));
	} else {
		name_->setText(tr("<b>%n selected</b>: different types (%1)", nullptr, count).arg(problem_.toHtmlEscaped()));
	}
	value_->clear();
	value_->setPlaceholderText(tr("value, 0x1F, 0b101, or a name"));
	/* the names list when it would be the same for each of them: a name is then one value for all */
	if (shared && problem_.isEmpty()) {
		fillNamedValues(first);
	} else {
		namedValues_->clear();
		namedValues_->hide();
	}
	default_->hide();
	bits_->hide();
	targets_->setRowCount(count);
	for (int i = 0; i < count; i++) {
		const RegDef &def = model_->rows()[rows_[i]].def;
		targets_->setItem(i, 0, new QTableWidgetItem(def.danger ? def.name + QStringLiteral(" ⚠") : def.name));
		targets_->setItem(i, 1, new QTableWidgetItem);
	}
	targets_->setMinimumWidth(0); /* fitted anew to these registers */
	targets_->show();
	refresh();
}

void QuickWritePanel::refresh() {
	if (!rows_.isEmpty()) {
		refreshBulk();
		return;
	}
	if (row_ < 0 || row_ >= model_->rows().size()) return;
	const RegisterModel::Row &row = model_->rows()[row_];
	const bool writesAllowed = model_->writesEnabled();
	const bool enabled = writesAllowed && connected_;
	for (QWidget *control : std::initializer_list<QWidget *>{ value_, namedValues_, write_, default_, bitArea_ })
		control->setEnabled(enabled);
	hint_->setText(!writesAllowed ? tr("tick Allow writes to write") : !connected_ ? tr("not connected") : QString());
	updateBroadcast();
	if (!row.valid) return;
	/* the list shows the device's value, except while the user is choosing in it */
	if (!namedValues_->hasFocus() && !namedValues_->view()->isVisible()) {
		const QSignalBlocker blocker(namedValues_);
		namedValues_->setCurrentIndex(namedValues_->findData(listKey(row.def, row.raw)));
	}
	if (bitView_) bitView_->setValue(rawBits(row.raw), true);
}

/* several selected: each one's value as the polls bring it; the box on the value they all hold until something is
 * typed in it (empty, "(several values)", while they differ) */
void QuickWritePanel::refreshBulk() {
	const bool writesAllowed = model_->writesEnabled();
	const bool enabled = writesAllowed && connected_ && problem_.isEmpty();
	for (QWidget *control : std::initializer_list<QWidget *>{ value_, namedValues_, write_ })
		control->setEnabled(enabled);
	hint_->setText(!writesAllowed ? tr("tick Allow writes to write") : !connected_ ? tr("not connected") : QString());
	updateBroadcast();
	QStringList values; /* as the box would take them: the same text is one value for all */
	bool allRead = true;
	for (int i = 0; i < rows_.size() && i < targets_->rowCount(); i++) {
		if (rows_[i] >= model_->rows().size()) return; /* the map changed: the table selects again */
		const RegisterModel::Row &row = model_->rows()[rows_[i]];
		/* a value with its unit or its name; else as the table says it ("—" not read yet, "error") */
		const QString shown = row.valid && !row.unavailable && row.error.isEmpty()
				? BulkWriteDialog::shownText(row.def, row.raw)
				: model_->data(model_->index(rows_[i], RegisterModel::ColValue), Qt::DisplayRole).toString();
		if (targets_->item(i, 1)->text() != shown) targets_->item(i, 1)->setText(shown);
		allRead = allRead && row.valid;
		if (row.valid && !values.contains(formatValue(row.def, row.raw))) values << formatValue(row.def, row.raw);
	}
	const bool typed = value_->isModified() && !value_->text().isEmpty();
	if (!typed) {
		const QString common = allRead && values.size() == 1 ? values.first() : QString();
		if (value_->text() != common) value_->setText(common);
		value_->setPlaceholderText(values.size() > 1 ? tr("(several values)") : tr("value, 0x1F, 0b101, or a name"));
	}
	/* as wide as its columns, never narrower while the values change; a few rows high */
	targets_->resizeColumnsToContents();
	QHeaderView *header = targets_->horizontalHeader();
	for (int column = 0; column < 2; column++) header->resizeSection(column, header->sectionSize(column) + 16);
	const int shownRows = std::min(int(targets_->rowCount()), TARGET_ROWS);
	const bool scrolls = targets_->rowCount() > TARGET_ROWS;
	const int frame = 2 * targets_->frameWidth();
	const int width = header->length() + frame + (scrolls ? targets_->verticalScrollBar()->sizeHint().width() : 0);
	targets_->setMinimumWidth(std::max(width, targets_->minimumWidth()));
	targets_->setMaximumWidth(targets_->minimumWidth());
	targets_->setFixedHeight(shownRows * targets_->verticalHeader()->defaultSectionSize() + frame);
}

/* a bus: the same value to every device at once, when the rule allows it for this register, and once a value is
 * typed (there is nothing to send before); never for several selected */
void QuickWritePanel::updateBroadcast() {
	broadcast_->setVisible(bool(broadcastRule_) && rows_.isEmpty());
	if (!broadcastRule_ || row_ < 0 || row_ >= model_->rows().size()) return;
	const QString refusal = broadcastRule_(model_->rows()[row_].def);
	const bool typed = !value_->text().trimmed().isEmpty();
	broadcast_->setEnabled(model_->writesEnabled() && connected_ && refusal.isEmpty() && typed);
	broadcast_->setToolTip(!refusal.isEmpty() ? tr("No broadcast: %1").arg(refusal)
			: typed ? tr("The value typed, to every device on the link in one broadcast frame (slave 0). No device "
						 "answers a broadcast: each one is read back afterwards.")
					: tr("Type a value first: it goes to every device on the link in one broadcast frame (slave 0)."));
}

void QuickWritePanel::onValuesChanged(const QModelIndex &first, const QModelIndex &last) {
	const auto within = [&](int row) { return row >= first.row() && row <= last.row(); };
	if (within(row_) || std::any_of(rows_.begin(), rows_.end(), within)) refresh();
}

/* ------------------------------------------------------------------ writing */

void QuickWritePanel::writeTyped() {
	const QString text = value_->text().trimmed();
	if ((row_ < 0 && rows_.isEmpty()) || text.isEmpty()) return;
	requestWrite(text);
}

void QuickWritePanel::writeNamedValue(int listIndex) {
	if (row_ < 0 && rows_.isEmpty()) return;
	/* "s3.5": a special value, in shown units; a number: an enum's raw value */
	const QVariant data = namedValues_->itemData(listIndex);
	if (data.typeId() == QMetaType::QString) requestWrite(data.toString().mid(1));
	else if (data.toLongLong() >= 0) requestWrite(QStringLiteral("0x%1").arg(quint64(data.toLongLong()), 0, 16));
	else requestWrite(QString::number(data.toLongLong())); /* a negative raw value: as a number */
}

/* `width` bits from `lsb` up become value: the rest is what the device holds now (nothing read yet: no write) */
void QuickWritePanel::writeField(int lsb, int width, quint64 value) {
	if (row_ < 0) return;
	const RegisterModel::Row &row = model_->rows()[row_];
	if (!row.valid) return;
	const quint64 mask = bitMask(width) << lsb;
	requestWrite(bitsText(row.def, (rawBits(row.raw) & ~mask) | ((value << lsb) & mask)));
}

void QuickWritePanel::requestWrite(const QString &text) {
	if (!rows_.isEmpty()) {
		emit bulkWriteRequested(text); /* the dialog lists what each one gets */
		refresh();
		return;
	}
	const RegisterModel::Row &row = model_->rows()[row_];
	emit writeRequested(row_, text, row.valid ? row.raw : QByteArray());
	refresh(); /* refused or cancelled: the controls show the device's value again */
}

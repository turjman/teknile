/* SPDX-License-Identifier: Apache-2.0 */
/* The bit fields of one register, as the Map editor edits them: see field_editor.h. */
#include "ui/field_editor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "ui/bit_view.h"
#include "ui/name_table.h"
#include "ui/ui_helpers.h"

namespace {

enum Column { ColName, ColBits, ColAccess, ColDesc };

} // namespace

FieldEditor::FieldEditor(QWidget *parent) : QWidget(parent) {
	bits_ = new BitView;
	bits_->setObjectName(QStringLiteral("fieldStrip"));
	bits_->setEditMode(true);
	connect(bits_, &BitView::bitsChosen, this, &FieldEditor::addField);
	connect(bits_, &BitView::fieldPicked, this, &FieldEditor::pick);

	table_ = new QTableWidget(0, 4);
	table_->setObjectName(QStringLiteral("fieldTable"));
	table_->setHorizontalHeaderLabels({ tr("Name"), tr("Bits"), tr("Access"), tr("Description") });
	table_->horizontalHeader()->setStretchLastSection(true);
	table_->verticalHeader()->hide();
	table_->verticalHeader()->setDefaultSectionSize(26);
	table_->setColumnWidth(ColName, 150);
	table_->setColumnWidth(ColBits, 60);
	table_->setColumnWidth(ColAccess, 76);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(table_, &QTableWidget::itemChanged, this, [this] {
		if (!loading_) readTable();
	});
	connect(table_, &QTableWidget::currentCellChanged, this, [this](int row) {
		if (!loading_) pick(row);
	});

	auto *add = new QPushButton(tr("+ Field"));
	add->setToolTip(tr("A field on the lowest free bit (or drag across bits above)"));
	connect(add, &QPushButton::clicked, this, [this] {
		quint64 taken = 0;
		for (const BitField &f : fields_) taken |= bitMask(f.width) << f.lsb;
		int bit = 0;
		while (bit < 8 * def_.size && ((taken >> bit) & 1)) bit++;
		addField(std::min(bit, 8 * def_.size - 1), 1);
	});
	auto *remove = new QPushButton(tr("− Field"));
	connect(remove, &QPushButton::clicked, this, [this] {
		if (picked_ < 0 || picked_ >= fields_.size()) return;
		fields_.removeAt(picked_);
		picked_ = std::min(picked_, int(fields_.size()) - 1);
		emit edited(fields_);
	});
	auto *buttons = new QHBoxLayout;
	buttons->addWidget(add);
	buttons->addWidget(remove);
	buttons->addStretch(1);

	valuesTitle_ = mutedLabel(QString());
	values_ = new NameTable(NameTable::Keys::Integers, QStringLiteral("fieldValues"));
	connect(values_, &NameTable::edited, this, [this] {
		if (picked_ < 0 || picked_ >= fields_.size()) return;
		BitField &field = fields_[picked_];
		field.values.clear();
		for (const NameTable::Entry &entry : values_->entries()) field.values.insert(qint64(entry.value), entry.name);
		field.valuesHex = values_->hex();
		emit edited(fields_);
	});

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 6, 0, 0);
	layout->addWidget(bits_);
	layout->addWidget(table_, 2);
	layout->addLayout(buttons);
	layout->addWidget(valuesTitle_);
	layout->addWidget(values_, 1);
}

void FieldEditor::setRegister(const RegDef &def) {
	const bool same = def.uid == def_.uid;
	def_ = def;
	fields_ = def.fields;
	if (!same) picked_ = fields_.isEmpty() ? -1 : 0;
	picked_ = std::min(picked_, int(fields_.size()) - 1);
	bits_->setRegister(def_);
	fillTable();
	pick(picked_);
}

void FieldEditor::setLiveValue(quint64 value, bool valid) { bits_->setValue(value, valid); }

QString FieldEditor::bitsText(const BitField &field) {
	return field.width == 1 ? QString::number(field.lsb)
			: QStringLiteral("%1:%2").arg(field.lsb + field.width - 1).arg(field.lsb);
}

bool FieldEditor::parseBits(const QString &text, int &lsb, int &width) {
	const QStringList parts = text.trimmed().split(QLatin1Char(':'));
	bool okHigh = false, okLow = true;
	const int high = parts.value(0).trimmed().toInt(&okHigh);
	const int low = parts.size() > 1 ? parts.value(1).trimmed().toInt(&okLow) : high;
	if (!okHigh || !okLow || parts.size() > 2 || high < 0 || low < 0) return false;
	lsb = std::min(high, low);
	width = std::abs(high - low) + 1;
	return true;
}

void FieldEditor::fillTable() {
	loading_ = true;
	table_->setRowCount(int(fields_.size()));
	for (int row = 0; row < fields_.size(); row++) {
		const BitField &field = fields_[row];
		table_->setItem(row, ColName, new QTableWidgetItem(field.name));
		table_->setItem(row, ColBits, new QTableWidgetItem(bitsText(field)));
		table_->setItem(row, ColDesc, new QTableWidgetItem(field.desc));
		auto *access = new QComboBox;
		access->addItems({ QStringLiteral("—"), QStringLiteral("ro"), QStringLiteral("rw"), QStringLiteral("w1c") });
		access->setToolTip(tr("—: as the register; ro, rw, or w1c (a 1 written clears it)"));
		access->setCurrentIndex(int(field.access));
		connect(access, &QComboBox::activated, this, [this, row](int index) {
			if (row >= fields_.size()) return;
			fields_[row].access = FieldAccess(index);
			emit edited(fields_);
		});
		table_->setCellWidget(row, ColAccess, access);
	}
	if (picked_ >= 0) table_->setCurrentCell(picked_, ColName);
	loading_ = false;
}

void FieldEditor::pick(int index) {
	picked_ = index;
	bits_->setPickedField(index);
	const bool on = index >= 0 && index < fields_.size();
	values_->setEnabled(on);
	if (!on) {
		valuesTitle_->setText(tr("Pick a field to name its values"));
		values_->setEntries({}, false);
		return;
	}
	const BitField &field = fields_[index];
	valuesTitle_->setText(tr("Values of %1 (bits %2, 0 … %3)").arg(field.name, bitsText(field)).arg(bitMask(field.width)));
	QVector<NameTable::Entry> entries;
	for (auto it = field.values.begin(); it != field.values.end(); ++it) entries.push_back({ double(it.key()), it.value() });
	values_->setEntries(entries, field.valuesHex);
	if (table_->currentRow() != index) {
		loading_ = true;
		table_->setCurrentCell(index, ColName);
		loading_ = false;
	}
}

void FieldEditor::addField(int lsb, int width) {
	if (8 * def_.size <= 0) return;
	QStringList names;
	for (const BitField &f : fields_) names << f.name;
	QString name;
	for (int n = int(fields_.size()) + 1; name.isEmpty() || names.contains(name); n++)
		name = QStringLiteral("FIELD_%1").arg(n);
	BitField field;
	field.name = name;
	field.lsb = lsb;
	field.width = width;
	fields_.push_back(field);
	/* in bit order, lowest first, as the map lists them: the new one where it goes */
	std::stable_sort(fields_.begin(), fields_.end(), [](const BitField &a, const BitField &b) { return a.lsb < b.lsb; });
	for (int i = 0; i < fields_.size(); i++)
		if (fields_[i].name == name) picked_ = i;
	emit edited(fields_);
}

void FieldEditor::readTable() {
	for (int row = 0; row < table_->rowCount() && row < fields_.size(); row++) {
		BitField &field = fields_[row];
		if (const QTableWidgetItem *name = table_->item(row, ColName)) field.name = name->text().trimmed();
		if (const QTableWidgetItem *desc = table_->item(row, ColDesc)) field.desc = desc->text().trimmed();
		if (QTableWidgetItem *bits = table_->item(row, ColBits)) {
			int lsb, width;
			if (parseBits(bits->text(), lsb, width)) {
				field.lsb = lsb;
				field.width = width;
			} else {
				continue; /* not bits: the row stays as it was, the checks show nothing new */
			}
		}
	}
	emit edited(fields_);
}

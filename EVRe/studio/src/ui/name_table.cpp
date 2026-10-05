/* SPDX-License-Identifier: Apache-2.0 */
/* Names for values, as the Map editor edits them: see name_table.h. */
#include "ui/name_table.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ui/theme.h"

NameTable::NameTable(Keys keys, const QString &objectName, QWidget *parent) : QWidget(parent), keys_(keys) {
	drawnDark_ = Theme::isDark();
	setObjectName(objectName);
	table_ = new QTableWidget(0, 2);
	table_->setHorizontalHeaderLabels({ tr("Value"), tr("Name") });
	table_->horizontalHeader()->setStretchLastSection(true);
	table_->verticalHeader()->hide();
	table_->setColumnWidth(0, 90);
	table_->verticalHeader()->setDefaultSectionSize(24);
	connect(table_, &QTableWidget::itemChanged, this, [this] {
		if (loading_) return;
		markRows();
		emitIfChanged();
	});

	/* "+ Name" / "− Name", as the bit fields' "+ Field" / "− Field" beside them */
	auto *add = new QPushButton(tr("+ Name"));
	add->setToolTip(tr("A new row: the next value"));
	connect(add, &QPushButton::clicked, this, [this] {
		/* the next value after the highest one */
		double next = 0;
		for (const Entry &entry : entries()) next = std::max(next, entry.value + 1);
		addRow(keyText(next, hex()), QString());
		table_->editItem(table_->item(table_->rowCount() - 1, 1));
	});
	auto *remove = new QPushButton(tr("− Name"));
	remove->setToolTip(tr("The selected rows out"));
	connect(remove, &QPushButton::clicked, this, [this] {
		QList<int> rows;
		for (const QModelIndex &index : table_->selectionModel()->selectedRows()) rows << index.row();
		if (rows.isEmpty() && table_->currentRow() >= 0) rows << table_->currentRow();
		std::sort(rows.begin(), rows.end(), std::greater<int>());
		for (int row : rows) table_->removeRow(row);
		emitIfChanged();
	});
	auto *paste = new QPushButton(tr("Paste lines"));
	paste->setToolTip(tr("Rows from the clipboard: \"0 off\", \"1 = on\", \"0x10: boost\", or two columns from a sheet"));
	connect(paste, &QPushButton::clicked, this, &NameTable::pasteLines);

	auto *buttons = new QHBoxLayout;
	buttons->setContentsMargins(0, 0, 0, 0);
	buttons->addWidget(add);
	buttons->addWidget(remove);
	buttons->addWidget(paste);
	buttons->addStretch(1);
	if (keys_ == Keys::Integers) {
		hex_ = new QCheckBox(tr("Hex"));
		hex_->setToolTip(tr("The values as 0x.., in the list and in the file"));
		connect(hex_, &QCheckBox::toggled, this, [this](bool on) {
			if (loading_) return;
			setEntries(entries(), on);
			emit edited();
		});
		buttons->addWidget(hex_);
	}
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(table_, 1);
	layout->addLayout(buttons);
}

QString NameTable::keyText(double value, bool hex) const {
	if (keys_ == Keys::Integers && hex && value >= 0)
		return QStringLiteral("0x") + QString::number(qint64(value), 16).toUpper();
	if (value == std::floor(value) && std::fabs(value) < 1e15) return QString::number(qint64(value));
	return QString::number(value, 'g', QLocale::FloatingPointShortest);
}

bool NameTable::parseKey(const QString &typed, double &value) const {
	const QString text = typed.trimmed();
	bool ok = false;
	if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
		value = double(text.mid(2).toLongLong(&ok, 16));
		return ok;
	}
	if (keys_ == Keys::Integers) {
		value = double(text.toLongLong(&ok));
		return ok;
	}
	value = QLocale::c().toDouble(text, &ok);
	return ok;
}

void NameTable::setEntries(const QVector<Entry> &entries, bool hex) {
	/* what the list shows already (maybe with a row being filled in): left as it is */
	if (entries == this->entries() && hex == this->hex() && table_->rowCount() >= entries.size()) {
		last_ = entries;
		lastHex_ = hex;
		return;
	}
	last_ = entries;
	lastHex_ = hex;
	loading_ = true;
	const int current = table_->currentRow();
	table_->setRowCount(0);
	for (const Entry &entry : entries) addRow(keyText(entry.value, hex), entry.name);
	if (hex_) hex_->setChecked(hex);
	if (current >= 0 && current < table_->rowCount()) table_->setCurrentCell(current, 1);
	markRows();
	loading_ = false;
}

void NameTable::addRow(const QString &value, const QString &name) {
	const bool wasLoading = loading_;
	loading_ = true;
	const int row = table_->rowCount();
	table_->insertRow(row);
	table_->setItem(row, 0, new QTableWidgetItem(value));
	table_->setItem(row, 1, new QTableWidgetItem(name));
	loading_ = wasLoading;
}

QVector<NameTable::Entry> NameTable::entries() const {
	QVector<Entry> out;
	for (int row = 0; row < table_->rowCount(); row++) {
		const QTableWidgetItem *key = table_->item(row, 0), *name = table_->item(row, 1);
		double value;
		if (!key || !name || name->text().trimmed().isEmpty() || !parseKey(key->text(), value)) continue;
		out.push_back({ value, name->text().trimmed() });
	}
	std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.value < b.value; });
	return out;
}

bool NameTable::hex() const { return hex_ && hex_->isChecked(); }

void NameTable::markRows() {
	const bool wasLoading = loading_;
	loading_ = true;
	for (int row = 0; row < table_->rowCount(); row++) {
		QTableWidgetItem *key = table_->item(row, 0);
		double value;
		/* a good key: no colour of its own, the palette's text follows the look; a bad one: red, again on a switch */
		if (key) key->setData(Qt::ForegroundRole, parseKey(key->text(), value) ? QVariant() : QVariant(Theme::colors().bad));
	}
	loading_ = wasLoading;
}

void NameTable::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (Theme::switched(event, drawnDark_)) markRows();
}

void NameTable::pasteLines() {
	/* "value name", "value = name", "value: name", "value<TAB>name" */
	static const QRegularExpression line(QStringLiteral("^\\s*([-+]?(?:0[xX][0-9a-fA-F]+|[0-9.eE+-]+))\\s*[=:\\t,]?\\s*(.+?)\\s*$"));
	int added = 0;
	for (const QString &text : QApplication::clipboard()->text().split(QLatin1Char('\n'))) {
		const QRegularExpressionMatch match = line.match(text);
		double value;
		if (!match.hasMatch() || !parseKey(match.captured(1), value)) continue;
		addRow(match.captured(1), match.captured(2));
		added++;
	}
	if (added) {
		markRows();
		emitIfChanged();
	}
}

void NameTable::emitIfChanged() {
	const QVector<Entry> now = entries();
	if (now == last_ && hex() == lastHex_) return;
	last_ = now;
	lastHex_ = hex();
	emit edited();
}

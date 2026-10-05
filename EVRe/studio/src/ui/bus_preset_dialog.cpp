/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for a broadcast preset: see bus_preset_dialog.h. */
#include "ui/bus_preset_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <algorithm>

#include "ui/elided_label.h"
#include "ui/ui_helpers.h"

BusPresetDialog::BusPresetDialog(const QVector<RegDef> &registers, const BusPreset &start, QWidget *parent)
	: QDialog(parent), registers_(registers), start_(start) {
	setWindowTitle(start.name.isEmpty() ? tr("New broadcast") : tr("Broadcast %1").arg(start.name));
	name_ = new QLineEdit(start.name);
	name_->setObjectName(QStringLiteral("presetName"));
	name_->setPlaceholderText(tr("how the Broadcast menu lists it, e.g. Stop all"));
	register_ = new QComboBox;
	register_->setObjectName(QStringLiteral("presetRegister"));
	for (const RegDef &def : registers_)
		register_->addItem(QStringLiteral("%1  (%2)").arg(def.name, addrText(def.addr)), def.name);
	register_->setCurrentIndex(std::max(0, register_->findData(start.reg)));
	value_ = new QLineEdit(start.value);
	value_->setObjectName(QStringLiteral("presetValue"));
	value_->setPlaceholderText(tr("the value to send: 12, 0x1F, 0b101, or a name"));
	problem_ = new ElidedLabel;
	problem_->setObjectName(QStringLiteral("problem"));
	problem_->setTextFormat(Qt::PlainText);
	buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

	auto *form = new QFormLayout(this);
	form->addRow(tr("Name"), name_);
	form->addRow(tr("Register"), register_);
	form->addRow(tr("Value"), value_);
	form->addRow(mutedLabel(tr("Sent to every device at once, after a confirmation; each one is read back.")));
	form->addRow(problem_);
	form->addRow(buttons_);
	connect(name_, &QLineEdit::textChanged, this, &BusPresetDialog::validate);
	connect(value_, &QLineEdit::textChanged, this, &BusPresetDialog::validate);
	connect(register_, &QComboBox::currentIndexChanged, this, &BusPresetDialog::validate);
	connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
	validate();
}

BusPreset BusPresetDialog::result() const {
	BusPreset preset = start_;
	preset.name = name_->text().trimmed();
	preset.reg = register_->currentData().toString();
	preset.value = value_->text().trimmed();
	return preset;
}

/* a name, a register, and a value that register takes. A field not filled yet is no error: OK waits for it, its
 * placeholder says what goes there; red is only for what is wrong (a value the register does not take) */
void BusPresetDialog::validate() {
	const BusPreset preset = result();
	QString problem;
	if (registers_.isEmpty()) {
		problem = tr("No register can take a broadcast: the devices have different maps, and the map has no writable "
					 "register of the reserved bank (CONFIG, the messages).");
	} else if (!preset.value.isEmpty()) {
		QByteArray bytes;
		const RegDef &def = registers_[std::max(0, register_->currentIndex())];
		if (!encodeValue(def, preset.value, bytes, problem)) problem = tr("%1 does not take %2: %3").arg(def.name, preset.value, problem);
	}
	problem_->setFullText(problem);
	buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty() && !preset.name.isEmpty() && !preset.value.isEmpty());
}

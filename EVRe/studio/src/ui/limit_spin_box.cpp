/* SPDX-License-Identifier: Apache-2.0 */
/* The spin box that takes a number past its range: see limit_spin_box.h. */
#include "ui/limit_spin_box.h"

#include <algorithm>

#include "ui/ui_helpers.h"

LimitSpinBox::LimitSpinBox(QWidget *parent) : QSpinBox(parent) {
	setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
	/* Enter or leaving the box: the value is the range's again, and so is the edge */
	connect(this, &QSpinBox::editingFinished, this, [this] { markOutOfRange(false); });
}

void LimitSpinBox::setHelp(const QString &help) {
	help_ = help;
	setLimits(minimum(), maximum());
}

void LimitSpinBox::setLimits(int min, int max) {
	setRange(min, max);
	const QString range = tr("%1 to %2").arg(min).arg(max);
	setToolTip(help_.isEmpty() ? range : help_ + QStringLiteral("\n") + range);
}

bool LimitSpinBox::typedNumber(const QString &input, qlonglong &value) const {
	QString digits = input;
	digits.remove(prefix()).remove(suffix());
	bool number = false;
	value = digits.trimmed().toLongLong(&number);
	return number;
}

QValidator::State LimitSpinBox::validate(QString &input, int &pos) const {
	const QValidator::State state = QSpinBox::validate(input, pos);
	/* a number past the range (not a stray letter) stays as typed, the edge amber, until Enter or leaving the box */
	qlonglong value = 0;
	const bool out = typedNumber(input, value) && (value > maximum() || value < minimum());
	markOutOfRange(out);
	return out ? QValidator::Intermediate : state;
}

void LimitSpinBox::fixup(QString &input) const {
	qlonglong value = 0;
	if (typedNumber(input, value)) input = textFromValue(int(std::clamp<qlonglong>(value, minimum(), maximum())));
	else QSpinBox::fixup(input);
}

void LimitSpinBox::markOutOfRange(bool out) const {
	auto *self = const_cast<LimitSpinBox *>(this);
	if (property("outOfRange").toBool() == out) return;
	self->setProperty("outOfRange", out);
	repolish(self);
}

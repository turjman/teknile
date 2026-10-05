/* SPDX-License-Identifier: Apache-2.0 */
/* A spin box that takes a number typed past its range instead of dropping the
 * keystroke (a plain QSpinBox does, and nothing says why the digit did not
 * come): the number stays as typed, the box's edge turns amber
 * ([outOfRange="true"] in the theme) while it is out of range, and on Enter or
 * leaving the box it becomes the nearest value of the range (2000 -> 1024).
 * The range is in the box's tooltip, after the tooltip it is given. The
 * sidebar's Slave, Timeout and In flight and the Monitor's Slave are ones. */
#pragma once

#include <QSpinBox>
#include <QString>
#include <QValidator>

class LimitSpinBox : public QSpinBox {
	Q_OBJECT
public:
	explicit LimitSpinBox(QWidget *parent = nullptr);
	/* the box's own tooltip; the range is added after it, and again when the range changes */
	void setHelp(const QString &help);
	void setLimits(int min, int max);

protected:
	QValidator::State validate(QString &input, int &pos) const override;
	void fixup(QString &input) const override; /* a number past the range: the nearest end of it */

private:
	/* the number typed, prefix and suffix left out; false if it is none */
	bool typedNumber(const QString &input, qlonglong &value) const;
	void markOutOfRange(bool out) const;
	QString help_;
};

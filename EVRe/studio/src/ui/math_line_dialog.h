/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for a new or an edited math line (model/math_lines.h): its name,
 * unit and formula. The formula is checked against the map's registers while
 * it is typed: the registers it reads, or what is wrong. OK is only possible
 * with a valid formula and a name. */
#pragma once

#include <QDialog>

#include "model/device_map.h"
#include "model/math_lines.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;

class MathLineDialog : public QDialog {
	Q_OBJECT
public:
	/* start: the line to edit, or the proposal for a new one; registers: the map's, for the formula */
	MathLineDialog(const MathLine &start, bool editing, const QVector<RegDef> &registers, QWidget *parent = nullptr);
	MathLine result() const; /* start, with the name, unit and formula typed, and shown */
	/* the fast streams' channels (STREAM.CHANNEL): a formula naming one is told why it cannot read it */
	void setFastChannels(const QStringList &names);

private:
	void validate();

	MathLine start_;
	QVector<RegDef> registers_;
	QStringList fastChannels_;
	QLineEdit *name_, *unit_, *formula_;
	QLabel *state_;               /* "OK: reads ..." or the formula's error */
	QDialogButtonBox *buttons_;
};

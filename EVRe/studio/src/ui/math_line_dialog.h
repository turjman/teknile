/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for a new or an edited math line (model/math_lines.h): its name,
 * unit and formula. The formula is checked against the map's registers (and
 * its fast streams' channels) while it is typed: what it reads, or what is
 * wrong; a formula over one stream's channels is said to be a fast math line,
 * computed for every record of that stream. OK is only possible with a valid
 * formula and a name. */
#pragma once

#include <QDialog>

#include "model/device_map.h"
#include "model/math_lines.h"

class FormulaCompleter;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

class MathLineDialog : public QDialog {
	Q_OBJECT
public:
	/* start: the line to edit, or the proposal for a new one; registers: the map's, for the formula */
	MathLineDialog(const MathLine &start, bool editing, const QVector<RegDef> &registers, QWidget *parent = nullptr);
	MathLine result() const; /* start, with the name, unit and formula typed, and shown */
	/* the map's fast streams: their channels (STREAM.CHANNEL) offered and read, all of one stream (a fast math line) */
	void setFastStreams(const QVector<StreamDef> &streams);

private:
	void validate();

	MathLine start_;
	QVector<RegDef> registers_;
	QVector<StreamDef> streams_;
	FormulaCompleter *completer_;
	QLineEdit *name_, *unit_, *formula_;
	QLabel *state_;               /* "OK: reads ..." or the formula's error */
	QDialogButtonBox *buttons_;
};

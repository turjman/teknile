/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for a broadcast preset of a bus (model/bus_file.h): its name,
 * the register (one the broadcast rule allows, by its name in the map), and
 * the value, as typed in quick write. OK is offered once the name and the
 * value are given and the register takes the value; only what is wrong (a
 * value it does not take) is shown, in red, on a line under the fields kept
 * from the start. */
#pragma once

#include <QDialog>
#include <functional>

#include "model/bus_file.h"

class ElidedLabel;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;

class BusPresetDialog : public QDialog {
	Q_OBJECT
public:
	/* registers: the ones a broadcast may go to; start: the preset's first values */
	BusPresetDialog(const QVector<RegDef> &registers, const BusPreset &start, QWidget *parent = nullptr);
	BusPreset result() const;

private:
	void validate();

	QVector<RegDef> registers_;
	BusPreset start_;
	QLineEdit *name_, *value_;
	QComboBox *register_;
	ElidedLabel *problem_;     /* one line, there from the start: the dialog and OK never move */
	QDialogButtonBox *buttons_;
};

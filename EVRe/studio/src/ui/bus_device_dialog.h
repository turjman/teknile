/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog for one device of a bus (model/bus_file.h): its name (the prefix
 * of its registers' names), its slave address, its map, and whether it is
 * polled. OK is offered only when the bus would have nothing wrong with it
 * (checkBus): a free name and address, a map given; what is wrong is shown
 * under the fields, on one line kept from the start (the dialog and OK never
 * move), every problem in its tooltip. */
#pragma once

#include <QDialog>

#include "model/bus_file.h"

class ElidedLabel;
class QCheckBox;
class QDialogButtonBox;
class QLineEdit;
class QSpinBox;

class BusDeviceDialog : public QDialog {
	Q_OBJECT
public:
	/* bus: the bus as it is; index: the device edited, -1 for a new one (start: its first values) */
	BusDeviceDialog(const BusFile &bus, int index, const BusDevice &start, QWidget *parent = nullptr);
	BusDevice result() const;

private:
	void validate();

	BusFile bus_;
	int index_;
	BusDevice start_;
	QLineEdit *name_, *map_, *token_;
	QSpinBox *slave_;
	QCheckBox *poll_;
	ElidedLabel *problems_;
	QDialogButtonBox *buttons_;
};

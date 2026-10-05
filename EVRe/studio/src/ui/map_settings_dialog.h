/* SPDX-License-Identifier: Apache-2.0 */
/* Map settings: what the map says about the device, not about one register.
 *
 *   Device     name, description, device ID (checked when connecting), slave
 *              address, USB vendor and product ID (mark its serial port)
 *   Login      the register the token is written to, and its size
 *   Protocol   transport, baud rate, TCP port, answer timeout, notes
 *   Notes      on the whole map, and on each group
 *
 * OK makes all of it one step of the MapDocument's undo history. On a bus
 * the map's slave address is not used (each device's is in the bus file):
 * the box is disabled, and its tooltip says so. */
#pragma once

#include <QDialog>

class MapDocument;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
struct DeviceMap;

class MapSettingsDialog : public QDialog {
	Q_OBJECT
public:
	/* onBus: the map is a device's of a bus, whose slave address the bus file gives */
	MapSettingsDialog(MapDocument *doc, bool onBus = false, QWidget *parent = nullptr);

	void accept() override; /* checked, then applied as one undo step */

private:
	QWidget *buildDevice();
	QWidget *buildProtocol();
	QWidget *buildNotes();
	void load(const DeviceMap &map);
	/* the boxes into `map`; false with why if a box holds something that is not a value */
	bool store(DeviceMap &map, QString &why) const;

	MapDocument *doc_;
	QLineEdit *device_, *desc_, *deviceId_, *usbVid_, *usbPid_, *loginAddr_;
	QSpinBox *slave_, *loginSize_, *baud_, *tcpPort_, *timeout_;
	QCheckBox *login_;
	QComboBox *transport_;
	QPlainTextEdit *protocolNotes_, *notes_;
	QTableWidget *groupNotes_;
	QLabel *error_;
};

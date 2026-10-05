/* SPDX-License-Identifier: Apache-2.0 */
/* Map settings: what the map says about the device, not about one register.
 *
 *   Device     name, description, device ID (checked when connecting), slave
 *              address, USB vendor and product ID (mark its serial port)
 *   Login      the register the token is written to, and its size
 *   Protocol   transport, baud rate, TCP port, answer timeout, notes
 *   Notes      on the whole map, and on each group
 *   Streams    its fast streams (Fast EVRe): each its name, window (Address and Size), rate, the registers
 *              that switch it and give its rate, and the channels of one record; the map's checks of
 *              the streams shown live under them
 *
 * OK makes all of it one step of the MapDocument's undo history. On a bus
 * the map's slave address is not used (each device's is in the bus file):
 * the box is disabled, and its tooltip says so. */
#pragma once

#include <QDialog>
#include <QVector>

#include "model/device_map.h"

class MapDocument;
class QCheckBox;
class QComboBox;
class QListWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class MapSettingsDialog : public QDialog {
	Q_OBJECT
public:
	/* onBus: the map is a device's of a bus, whose slave address the bus file gives */
	MapSettingsDialog(MapDocument *doc, bool onBus = false, QWidget *parent = nullptr);

	void accept() override; /* checked, then applied as one undo step */

	/* the Streams page (tests) */
	QWidget *streamsPage() const { return streamsPage_; }
	void addStream();
	void removeStream();
	void addChannel();
	void removeChannel();
	QString streamChecks() const; /* the map's checks of the streams, as shown */

private:
	QWidget *buildDevice();
	QWidget *buildProtocol();
	QWidget *buildNotes();
	QWidget *buildStreams();
	void showStream(int index);   /* the form from streams_[index] */
	void takeStream();            /* the form into streams_[current_] */
	void refreshStreams();        /* the list's names, the record's size, the checks */
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
	/* the Streams page: a copy of the map's streams, edited here */
	QVector<StreamDef> streams_;
	int current_ = -1;
	bool showing_ = false;
	QWidget *streamsPage_ = nullptr, *streamForm_ = nullptr;
	QListWidget *streamList_ = nullptr;
	QLineEdit *streamName_ = nullptr, *streamAddr_ = nullptr, *streamRate_ = nullptr, *streamDesc_ = nullptr;
	QSpinBox *streamSize_ = nullptr;
	QComboBox *streamEnable_ = nullptr, *streamRateReg_ = nullptr;
	QTableWidget *channels_ = nullptr;
	QLabel *streamRecord_ = nullptr, *streamChecks_ = nullptr;
	QPushButton *removeStream_ = nullptr, *addChannel_ = nullptr, *removeChannel_ = nullptr;
};

/* SPDX-License-Identifier: Apache-2.0 */
/* The Monitor tab: the frames on the link, and single requests typed by hand.
 *
 * With "Log frames" ticked the engine keeps every frame sent and received
 * (off by default: at fast polling that is a lot of text), and the window
 * hands them over once per display frame (addFrames).
 *
 * The row over the frames sends one request: READ with a count, or WRITE,
 * with or without an answer, with hex bytes. The tab checks what was typed -
 * and, for a write, that Allow writes is on (RegisterModel::writesEnabled) -
 * then asks the window to send it; the answer comes back to showAnswer().
 *
 * The Slave box says which device it goes to: the selected one by default.
 * Slave 0 is the broadcast address: only a WRITE without acknowledge goes
 * there (the function follows, and comes back to the one chosen before when
 * a device is chosen again), every device takes it, none answers, and the
 * window sends it only when the broadcast rule allows it (model/bus_file.h). */
#pragma once

#include <QWidget>

#include "ui/ui_helpers.h"

class LimitSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class RegisterModel;

class MonitorTab : public QWidget {
	Q_OBJECT
public:
	explicit MonitorTab(const RegisterModel *model, QWidget *parent = nullptr);

	/* the frames logged since the last display frame; dropped: how many were left out (too many to show) */
	void addFrames(const QStringList &lines, int dropped);
	/* the answer to a request sent from here (a write's carries no data; ms < 0: not timed) */
	void showAnswer(quint16 addr, bool ok, const QByteArray &data, const QString &message, double ms);
	/* a WRITE without acknowledge went out: no answer comes, so nothing says it arrived */
	void showSent(quint16 addr, const QByteArray &bytes);

	/* "2C 01", "2c01", "0x2C 0x01", "2C,01": the bytes; false for anything else (a decimal 300 is not
	 * taken as 03 00). Public for the tests. */
	static bool parseHexBytes(const QString &text, QByteArray &bytes);
	/* the slave the requests go to: the selected device's (or the link's), set again when it changes */
	void setSlave(int slave);
	/* a bus: the devices by name in place of the Slave number (fillDevicePicker), the broadcast after them; none:
	 * one device, the number */
	void setDevices(const QVector<PickerDevice> &devices);
	/* a line of its own in the frames: a refusal, or what a broadcast did */
	void showNote(const QString &text);

signals:
	void logFramesToggled(bool on);
	void readRequested(quint8 slave, quint16 addr, quint16 count);
	void writeRequested(quint8 slave, quint16 addr, const QByteArray &bytes, bool ack);

private:
	void sendRequest(); /* the request typed: checked, then asked for */

	const RegisterModel *model_;
	LimitSpinBox *slave_;             /* 0: broadcast; on a bus hidden, set from device_ */
	QComboBox *device_;               /* a bus: its devices, then the broadcast */
	QComboBox *function_;             /* READ, WRITE + ack, WRITE (no ack) */
	int functionBeforeBroadcast_ = 0; /* the function chosen before slave 0 locked it to WRITE: back after it */
	QLineEdit *address_, *argument_;  /* argument: READ's count, or WRITE's hex bytes */
	QLabel *argumentLabel_;           /* the argument's name: Count or Bytes */
	QPlainTextEdit *frames_;
};

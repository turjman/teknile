/* SPDX-License-Identifier: Apache-2.0 */
/* The sidebar's "Devices on the link" card: one device (the map's), or the
 * devices of a bus (model/bus_file.h).
 *
 * One device: a line that says so, and New bus / Open bus. A bus: a list of
 * its devices (a dot for its state - answering, offline, not connected - its
 * name, slave address and map), the one selected being the one the Registers
 * tab shows and the Map editor edits; + Device, - Device, Edit, the Bus file
 * menu (Open, Save, Save as, Close bus) and the Broadcast menu: the presets
 * kept in the bus file, each sent after a confirmation (offered only while
 * connected with Allow writes on: setBroadcastBlocked), New broadcast, Remove.
 * A row reads "D1 · slave 1 · map", as the device pickers ("D1 · slave 1").
 *
 * The panel only shows and asks: the window keeps the bus and does the work
 * (the signals); showBus() says what to show. */
#pragma once

#include <QSet>
#include <QWidget>

#include "model/bus_file.h"
#include "ui/ui_helpers.h"

class ElidedLabel;
class QAction;
class QListWidget;
class QMenu;
class QPushButton;

class BusPanel : public QWidget {
	Q_OBJECT
public:
	explicit BusPanel(QWidget *parent = nullptr);

	/* bus: null for one device. selected: the device shown; offline: the slaves of the devices that stopped
	 * answering; connected: the link is up (else no device answers); modified: the bus has unsaved changes */
	void showBus(const BusFile *bus, int selected, const QSet<uint8_t> &offline, bool connected, bool modified);
	/* why the presets cannot be sent now (not connected, Allow writes off): their actions disabled, the reason in
	 * their tooltip; empty: they can */
	void setBroadcastBlocked(const QString &why);

	/* the devices of a bus as the pickers show them (fillDevicePicker), their state as this card shows it */
	static QVector<PickerDevice> pickerDevices(const BusFile &bus, const QSet<uint8_t> &offline, bool connected);
	/* names in a line of text: all of them up to four, else the first three and how many more ("D1, D2, D3 and 9
	 * more"): twelve devices made the Map editor's banner run off its line */
	static QString namesText(const QStringList &names);

signals:
	void newBusClicked();      /* a bus of the map's device, to add more to */
	void openBusClicked();
	void saveBusClicked(bool saveAs);
	void closeBusClicked();    /* back to one device: the selected one's map */
	void addDeviceClicked();
	void editDeviceClicked(int device);
	void removeDeviceClicked(int device);
	void deviceSelected(int device);
	void presetChosen(int preset);      /* a broadcast kept in the bus file: sent (after a confirmation) */
	void newPresetClicked();
	void removePresetClicked(int preset);

private:
	void blockPresets();   /* the presets' actions follow blocked_ */

	ElidedLabel *info_;    /* one line, cut to its room */
	QListWidget *list_;
	QWidget *oneDevice_, *busButtons_;
	QPushButton *removeButton_;
	QMenu *presetMenu_;
	QList<QAction *> presetActions_; /* the presets in the Broadcast menu, in their order */
	QString blocked_;                /* why they cannot be sent now; empty: they can */
	bool showing_ = false; /* showBus() is filling the list: a selection then is not the user's */
};

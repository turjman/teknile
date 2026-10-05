/* SPDX-License-Identifier: Apache-2.0 */
/* The Registers tab: the map's registers in a table, with their live values.
 *
 *  - over the table: the search (name, address, unit, description), the
 *    groups shown (a menu of check boxes: one, several, or all), Plot shown /
 *    Unplot shown (every numeric register the table shows, on or off the
 *    chart), Allow writes, and + Register.
 *  - the table (RegisterModel through RegisterFilter). ValueDelegate draws
 *    and edits the Value column. A right-click offers plotting, reading now,
 *    copying, editing the definition and logging; the header's menu shows or
 *    hides the Decoded column (saved in "ui/decodedColumn"). The columns fit
 *    their content, Decoded as wide as its longest text.
 *  - under it: the selected register in full (the detail line), and the
 *    quick-write panel for it (quick_write_panel.h).
 *
 * The map's definitions are made on the Map editor tab: + Register and "Edit
 * definition" here ask the window to go there (and remove goes through the
 * MapDocument, with undo). Writes, reads and the chart go through the window:
 * see the signals. */
#pragma once

#include <QWidget>
#include <functional>

#include "ui/ui_helpers.h"

class MapDocument;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPoint;
class QPushButton;
class QTableView;
class QuickWritePanel;
class RegisterFilter;
class RegisterModel;
struct RegDef;

class RegistersTab : public QWidget {
	Q_OBJECT
public:
	RegistersTab(RegisterModel *model, MapDocument *doc, QWidget *parent = nullptr);

	/* the link state, for the quick-write panel */
	void setConnected(bool connected);
	/* a bus: the device whose registers the table shows (RegDef::slave); -1: every row (one device, or All devices) */
	void setDevice(int slave);
	/* a bus: the devices the picker over the table offers (fillDevicePicker), after "All devices" (shown: -1 for
	 * all); none: one device, no picker */
	void setDevices(const QVector<PickerDevice> &devices, int shown);
	/* a bus: the quick-write panel's broadcast (QuickWritePanel::setBroadcastRule) */
	void setBroadcastRule(std::function<QString(const RegDef &)> rule);
	/* this tab is the one shown, or no longer: the columns and the glow of new values only follow while it is;
	 * shown, the columns make room at once for values that grew meanwhile */
	void setShown(bool shown);
	/* with the window's statistics, twice a second: room for values that grew, and a changed value stops glowing */
	void refreshStatus();

signals:
	/* a write asked for here: an edit in the table (RegisterModel::writeRequested) or the quick-write panel.
	 * Handled at once (a direct connection): afterwards, written or not, the panel shows the device's value. */
	void writeRequested(int row, const QString &text, const QByteArray &base);
	/* "To all devices" in the quick-write panel: a broadcast of the value typed */
	void broadcastRequested(int row, const QString &text);
	/* Read now: one register read at once, into the table */
	void readRequested(quint16 addr, quint16 count);
	/* Remove all from the chart (the window unticks every Plot) */
	void unplotAllRequested();
	/* one bit field of the register in `row` on the chart (a math line) */
	void plotFieldRequested(int row, int field);
	/* the definition of the register with this uid of this device (RegDef::slave), to edit on the Map editor tab */
	void editDefinitionRequested(quint32 uid, int slave);
	/* the device picker: one device's registers, or every device's (-1) */
	void deviceChosen(int slave);
	/* a new register after the one with this uid (0: after the last), on the Map editor tab */
	void addRegisterRequested(quint32 afterUid);
	/* Allow writes ticked or not (the Devices card's broadcasts follow it) */
	void writesAllowedChanged(bool on);
	/* a short note for the status bar, shown for ms */
	void statusMessage(const QString &text, int ms);

protected:
	void changeEvent(QEvent *event) override; /* the theme switched: its colours again (Theme::switched) */

private:
	/* building the tab */
	QHBoxLayout *buildToolbar();
	QTableView *buildTable();
	QLabel *buildDetail();
	void connectModel();

	/* the menus of the table and of its header */
	void showTableMenu(const QPoint &pos);
	void showHeaderMenu(const QPoint &pos);
	void addDecodedColumnAction(QMenu &menu);
	void showDecodedColumn(bool shown);

	/* the groups shown */
	void refreshGroups();     /* a check box per group of the map, again after every change of the map */
	void updateGroupButton(); /* the filter and the button follow the groups ticked */
	QStringList groupNames() const; /* the map's groups, in the order they first come */

	/* the chart */
	void plotShown(bool on);  /* every numeric register the table shows now */
	void updatePlotButton();  /* "Plot shown" or, when they all are, "Unplot shown" */

	/* the map */
	void addRegister();       /* after the selected register, on the Map editor tab */
	void removeRegisters();   /* the rows selected, as one undo step */

	/* the columns */
	void fitColumns(bool shrink); /* every column to its content; shrink: narrower too, not only wider */
	void fitDecoded();            /* Decoded: as wide as its longest text */

	void updateDetail();          /* the selected register, in full, under the table */

	RegisterModel *model_;
	MapDocument *doc_;
	RegisterFilter *filter_;      /* the search and the groups */
	bool shown_ = false;

	/* the toolbar */
	QLineEdit *search_;
	QPushButton *groupButton_;
	QComboBox *device_;          /* a bus: the device shown, or All devices */
	QMenu *groupMenu_;
	QStringList groupsShown_;     /* empty: all */
	QPushButton *plotShownButton_;
	QCheckBox *allowWrites_;
	bool drawnDark_ = true; /* the look Allow writes' highlight is drawn in */

	QTableView *table_;
	int decodedWidth_ = 0;        /* what the longest decoded text needs */

	/* under the table */
	QLabel *detail_;
	int detailRow_ = -1;          /* the selected register (a row of the model), -1 for none */
	QuickWritePanel *quickWrite_;
};

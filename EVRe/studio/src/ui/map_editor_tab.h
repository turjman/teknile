/* SPDX-License-Identifier: Apache-2.0 */
/* The Map editor tab: the whole map, made or changed here.
 *
 *   +---------------------------------------------------+------------------+
 *   | search  + Register  Duplicate  Delete  Undo Redo  |                  |
 *   |                                    Map settings.. |  the selected    |
 *   +---------------------------------------------------+  registers in    |
 *   | ! Address Name Type Size Unit Access Write Group..|  full (Register- |
 *   |   (MapTableModel: edited in place; a change of a  |  Editor)         |
 *   |    selected row goes to every selected row)       |                  |
 *   +---------------------------------------------------+                  |
 *   | the map's checks: errors, then warnings (a click  |                  |
 *   | selects the register)                             |                  |
 *   +---------------------------------------------------+------------------+
 *
 * Every change is a step of the MapDocument's undo history (Ctrl+Z, Ctrl+Y).
 * Ctrl+C copies the selected registers as JSON (an "evre-map/1" registers list,
 * also plain text), Ctrl+V pastes such a list (from here, another map or a text
 * editor): pasted where their addresses are free, else moved together to the
 * first free addresses after the selection.
 *
 * Export writes the map for others (model/map_export.h): a Markdown
 * specification, a C header, a Python module or CSV. Import CSV reads registers
 * from a sheet: they replace the map's registers, or join them (a register at
 * an address the map has replaces it); one undo step either way. */
#pragma once

#include <QVector>
#include <QWidget>
#include <functional>

#include "ui/ui_helpers.h"

class ElidedLabel;
class MapDocument;
class MapTableModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QTableView;
class RegisterEditor;
struct RegDef;

class MapEditorTab : public QWidget {
	Q_OBJECT
public:
	explicit MapEditorTab(MapDocument *doc, QWidget *parent = nullptr);

	/* where the form's live line gets its values (the Registers table): see RegisterEditor */
	void setLiveValues(std::function<bool(quint32 uid, QByteArray &raw)> live);
	/* A bus (model/bus_file.h): the banner over the toolbar says whose map is edited (html, on one line, cut to its
	 * room; its tooltip: the whole line, then allNames when the banner names only some), and the devices that share it offer whose live values the form shows
	 * (liveSlave). No devices: one device, no banner. */
	void showDevices(const QString &html, const QString &allNames, const QVector<PickerDevice> &devices, int liveSlave);

	/* the register with this uid selected and shown (from the Registers tab's "Edit definition") */
	void selectRegister(quint32 uid);
	/* a new register after the one with this uid (0: after the last), selected */
	void addRegister(quint32 afterUid = 0);

	/* the clipboard: the selected registers as JSON, and registers from it */
	void copySelected();
	void paste();
	void duplicateSelected();
	void deleteSelected();

	/* the map written as `kind`: "md", "h", "py" or "csv"; prefix: for the C and Python names */
	bool exportTo(const QString &kind, const QString &file, const QString &prefix, QString &err);
	/* the registers of a CSV file into the map: all of them instead of the map's, or added (the same
	 * address: replaced) */
	bool importCsvFrom(const QString &file, bool replaceAll, QString &err);

signals:
	void mapSettingsRequested();
	void liveDeviceChosen(int slave); /* a bus: the live values from another device that shares this map */
	void statusMessage(const QString &text, int ms);

protected:
	void changeEvent(QEvent *event) override; /* the theme switched: its colours again (Theme::switched) */

private:
	void buildToolbar();
	QPushButton *buildExportButton();
	void exportAsked(const QString &kind);  /* a file dialog, then exportTo */
	void importAsked();                     /* a file dialog, the question, then importCsvFrom */
	void buildTable();
	void buildIssues();

	QVector<quint32> selectedUids() const;          /* in address order */
	void select(const QVector<quint32> &uids);      /* those rows, the first one current */
	void onDocumentChanged();                       /* the table was reset: the selection back, the checks */
	void refreshIssues();
	void showTableMenu(const QPoint &pos);
	/* registers into the map (paste, duplicate): at their addresses if free, else moved as a block */
	void insertRegisters(QVector<RegDef> regs, const QString &step);

	MapDocument *doc_;
	QWidget *banner_;                 /* a bus: whose map this is */
	ElidedLabel *bannerText_;        /* one line, cut to its room; the whole of it in the tooltip */
	QComboBox *liveDevice_;
	MapTableModel *model_;
	QSortFilterProxyModel *filter_;
	QTableView *table_;
	RegisterEditor *editor_;
	QLineEdit *search_;
	QPushButton *undoButton_, *redoButton_;
	QListWidget *issues_;
	QLabel *issuesTitle_;
	QSplitter *tableSplit_ = nullptr; /* the table over the checks */
	QWidget *issuesBox_ = nullptr;    /* the checks' title and list; only the title when nothing is found */
	bool drawnDark_ = true;           /* the look the checks are drawn in */
	QVector<quint32> keptSelection_; /* the selection over a reset of the table */
};

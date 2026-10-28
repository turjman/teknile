/* SPDX-License-Identifier: Apache-2.0 */
/* The dialog before one value is written to several registers (STUDIO.md 6.6): every register selected, what it
 * holds now and what it would get, and why one would not take the value (it is left out: "Write 2 of 3") or is past
 * its map limits ("Write 3 anyway"). The text is encoded per register: "22.5" is scaled by each one's scale, a name
 * looked up in each one's names. It stands in for the questions of a single write (changed meanwhile, outside the
 * limits, a ⚠ register) asked one by one: one dialog, one confirmation for all.
 *
 * Opened without a value (the right-click menu), it has a value box at its top and the list follows the typing.
 * Cancel is the default and has the focus (with a value box: the box has it, and Enter there writes nothing), so
 * Enter and Space cancel; Write needs a click, or Tab and Space. Its values are the ones shown when it opened. */
#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

#include "model/device_map.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class BulkWriteDialog : public QDialog {
	Q_OBJECT
public:
	/* a register to write: its definition, its value when the dialog opened (empty: not read yet), and the device it
	 * is on, on a bus (empty: no Device column) */
	struct Target {
		RegDef def;
		QByteArray raw;
		QString device;
	};
	/* a register with the value typed: the bytes written, or why it would not take it; and past its map's limits? */
	struct Entry {
		QByteArray bytes;   /* empty: refused */
		QString refused;    /* encodeValue's reason */
		QString outside;    /* limitProblem: "above the maximum 100 %"; empty inside them */
	};
	static Entry evaluate(const RegDef &def, const QString &text);
	/* a value as the list (and the Log's "was") writes it: the table's text and the unit, or its name as well
	 * ("blink (2)") */
	static QString shownText(const RegDef &def, const QByteArray &raw);

	/* targets: two or more, of one type; leftOut: the read-only registers of the selection (named, not written);
	 * text: the value typed, empty for a value box */
	BulkWriteDialog(const QVector<Target> &targets, const QStringList &leftOut, const QString &text,
			QWidget *parent = nullptr);

	QString text() const;                         /* the value: as given, or as typed in the box */
	const QVector<Entry> &entries() const { return entries_; } /* one per target, in their order */
	int writable() const;                         /* the targets that take the value */

protected:
	bool eventFilter(QObject *watched, QEvent *event) override; /* Enter in the value box writes nothing */

private:
	void refresh();      /* the list, the sentences and the button, for the value now */
	void fitList();      /* the list as wide as its columns (a scroll bar past the window's width), a few rows high */

	QVector<Target> targets_;
	QStringList leftOut_;
	QString text_;
	QVector<Entry> entries_;
	QLineEdit *value_ = nullptr; /* only without a value given */
	QTableWidget *list_;
	QLabel *notes_;
	QPushButton *write_, *cancel_;
};

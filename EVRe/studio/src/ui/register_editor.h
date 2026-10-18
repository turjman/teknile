/* SPDX-License-Identifier: Apache-2.0 */
/* The right side of the Map editor tab: the registers selected there, in
 * full, as a form.
 *
 *   General   address, name, type, size, unit, access, write behaviour, group,
 *             description; persist, danger, hex, plot; scale, offset, decimals;
 *             min, max, default
 *   Values    names for values (the enum) and special values (NameTable)
 *   Bit fields the fields on a bit strip, with their value names (FieldEditor)
 *   Notes     the longer text
 *
 * Over the pages, a card: the register's name, its address, type and access,
 * and its LIVE value read with the definition as it is being edited (the scale,
 * the names, the fields): the preview of the edit. Its lines are single lines
 * whatever the value: what does not fit is cut short, and the whole text is the
 * tooltip (a long value would push the pages down).
 * Values and Bit fields are for one register at a time, Bit fields for an
 * integer one: otherwise the page is a note that says why, and its tab has a
 * warning sign and the reason as its tooltip (the tab stays where it is).
 *
 * None selected: instead of the form, a warning-coloured note in the middle.
 *
 * One register: every box is its own. Several (a bulk edit): the address and
 * the name are off, a box whose value differs between them is empty and says
 * "(several)", and a value typed or picked goes to all of them.
 *
 * A change goes to the MapDocument at once, as an undo step; typing into the
 * same box is one step (a merge key per box) until the box loses the focus.
 * When the document changes, the form shows it again, but a box being typed
 * into keeps what is typed. */
#pragma once

#include <QVector>
#include <QWidget>
#include <functional>

class FieldEditor;
class MapDocument;
class NameTable;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
struct RegDef;

class RegisterEditor : public QWidget {
	Q_OBJECT
public:
	explicit RegisterEditor(MapDocument *doc, QWidget *parent = nullptr);

	/* the registers to show and edit (uids); none: the form is off */
	void setTargets(const QVector<quint32> &uids);
	const QVector<quint32> &targets() const { return uids_; }
	/* where the live values come from (the Registers table): the bytes of the register with this
	 * uid, false if there is no value yet */
	using LiveValue = std::function<bool(quint32 uid, QByteArray &raw)>;
	void setLiveValues(LiveValue live) { live_ = std::move(live); }

signals:
	void refused(const QString &why); /* a value that cannot be taken, for the status bar */

private:
	QWidget *buildEmptyNote();
	QWidget *buildHeader();
	QWidget *buildGeneral();
	QWidget *buildValues();
	QWidget *buildNotes();
	/* a page the selection may not have: the page, or a note in its place that says why */
	QWidget *gate(QWidget *page, QStackedWidget *&gate, QLabel *&note);
	void showGates();                 /* valuesWhy_ / fieldsWhy_: the page or its note, the tab's sign and tooltip */
	void refreshLive();               /* the live value and the line under it, and the bit strip's value */
	void connectBoxes();
	void changeEvent(QEvent *event) override;

	void reload();                    /* the targets' values into the boxes (not the one being typed into) */
	/* a change to every target, as one undo step; key: typing into one box is one step */
	void apply(const QString &step, int key, const std::function<void(RegDef &)> &change);
	/* a number box: empty = none (NaN), else a number; false (and the box marked) if it is neither */
	bool readNumber(QLineEdit *box, double &value, bool emptyAllowed);
	void markInvalid(QWidget *box, bool invalid);

	MapDocument *doc_;
	QVector<quint32> uids_;
	bool loading_ = false;

	QStackedWidget *stack_;           /* the note "no register selected", or the form */
	QWidget *form_;
	/* the header card (buildHeader) */
	QLabel *heading_;                 /* the name, or how many registers are selected */
	QLabel *addrChip_, *typeChip_, *accessChip_;
	QLabel *liveDot_;                 /* its "state": ok, warn (a limit passed) or none (no value) */
	QLabel *liveLine_;                /* the value and its unit */
	QLabel *liveDetail_;              /* the decoded text and a limit passed, or why there is no value */
	LiveValue live_;
	QTabWidget *pages_ = nullptr;
	NameTable *enum_, *special_;
	FieldEditor *fields_;
	QLineEdit *address_, *name_, *unit_, *desc_, *scale_, *offset_, *min_, *max_, *default_;
	QComboBox *type_, *access_, *write_, *group_, *pastLimits_;
	QSpinBox *size_, *decimals_;
	QCheckBox *persist_, *danger_, *hex_, *plot_, *closed_, *reservedZero_;
	QPlainTextEdit *notes_;
	/* Values and Bit fields when the selection cannot have them: why (empty: it can), and the note in their place */
	QString valuesWhy_, fieldsWhy_;
	QStackedWidget *valuesGate_ = nullptr, *fieldsGate_ = nullptr;
	QLabel *valuesNote_ = nullptr, *fieldsNote_ = nullptr;
};

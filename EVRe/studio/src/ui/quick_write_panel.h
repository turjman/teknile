/* SPDX-License-Identifier: Apache-2.0 */
/* The quick-write panel under the register table, for the register selected
 * there when it is writable: a value box, a list of its named values (when
 * it has any), and for an integer register with bit fields - or any integer
 * register with "Bits" ticked - the register drawn bit by bit (BitView),
 * where a click flips a flag or offers a field's values.
 *
 * The panel only asks. Every write leaves as writeRequested(row, text, base),
 * the same request an edit in the table makes, so the danger confirmation and
 * the changed-meanwhile check stay in one place (the window). Allow writes is
 * enforced here: the controls are enabled only while it is on
 * (RegisterModel::writesEnabled) and the link is up. A field is written as the
 * whole register: what the device holds now, with that field's bits changed.
 * The controls follow the register's value as it changes.
 *
 * Several devices on the link (a bus): "To all devices" sends the value typed
 * as a broadcast (broadcastRequested), when the broadcast rule allows it for
 * the register (setBroadcastRule; its refusal is the button's tooltip) and a
 * value is typed.
 *
 * Several registers selected (showRegisters, STUDIO.md 5.1): one value for all
 * of them. The value box starts from the value they share ("(several values)"
 * when they differ), a list under it shows each one's value as the polls bring
 * it, the names list only when they all have the same names; Default, To all
 * devices and the bits are hidden. Write asks for a bulk write
 * (bulkWriteRequested), as the menu's entry and F2 do. */
#pragma once

#include <QFrame>
#include <QVector>
#include <functional>

struct RegDef;

class BitView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QTableWidget;
class RegisterModel;

class QuickWritePanel : public QFrame {
	Q_OBJECT
public:
	explicit QuickWritePanel(const RegisterModel *model, QWidget *parent = nullptr);

	/* the register selected in the table (a row of the model), -1 for none; not writable: the panel hides */
	void showRegister(int row);
	/* several selected (RegistersTab::bulkTargets): two or more rw rows of the model in the table's order; problem:
	 * their types when they differ ("f32, u8"), the controls then disabled. The same rows again change nothing */
	void showRegisters(const QVector<int> &rows, const QString &problem);
	bool isBulk() const { return !rows_.isEmpty(); }
	/* the link state: the controls follow it at once (disabled, "not connected", while it is down) */
	void setConnected(bool connected);
	/* a bus: why a register's value may not be broadcast (empty: it may); an empty function: one device, no
	 * broadcast button */
	using BroadcastRule = std::function<QString(const RegDef &)>;
	void setBroadcastRule(BroadcastRule rule);

signals:
	/* as RegisterModel::writeRequested; base: the raw value the text was chosen against. Handled at
	 * once (a direct connection): afterwards, written or not, the controls show the device's value. */
	void writeRequested(int row, const QString &text, const QByteArray &base);
	/* the value typed, to every device on the link at once (a broadcast) */
	void broadcastRequested(int row, const QString &text);
	/* several selected: the value typed (or a name picked) to all of them, through the bulk write's dialog */
	void bulkWriteRequested(const QString &text);

private:
	void rebuild(); /* the controls for the register: its name, its named values, the bit view */
	void refresh(); /* the controls follow its value, and whether a write can go */
	void rebuildBulk(); /* several selected: their title, the names they share, the list of them */
	void refreshBulk(); /* their values in the list, the box on the value they share, whether a write can go */
	void fillNamedValues(const RegDef &def); /* its special values (in shown units), then its value names */
	void updateBroadcast(); /* "To all devices": offered by the rule, connected, Allow writes on, a value typed */
	void onValuesChanged(const QModelIndex &first, const QModelIndex &last);

	/* the three ways to write */
	void writeTyped();
	void writeNamedValue(int listIndex);
	void writeField(int lsb, int width, quint64 value); /* from the bit view */
	void requestWrite(const QString &text);

	const RegisterModel *model_;
	int row_ = -1;              /* the register the controls were made for */
	QVector<int> rows_;         /* several selected: the registers (rows of the model); empty: one or none */
	QString problem_;           /* several selected of different types: the types */
	bool connected_ = false;

	QLabel *name_, *hint_;
	QLineEdit *value_;
	QComboBox *namedValues_;
	QPushButton *write_;
	QPushButton *default_;
	QPushButton *broadcast_;    /* "To all devices": a bus only */
	BroadcastRule broadcastRule_;
	QCheckBox *bits_;           /* "Bits": the bit view for a register without fields too */
	QWidget *bitArea_;          /* where the bit view goes */
	QTableWidget *targets_;     /* several selected: each one and its value now */
	BitView *bitView_ = nullptr;
};

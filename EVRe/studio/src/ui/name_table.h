/* SPDX-License-Identifier: Apache-2.0 */
/* A list of names for values, as the Map editor edits them: a register's
 * enum, its special values, or a bit field's values.
 *
 *   Value | Name       one row each, edited in place
 *   + - Paste lines    Paste lines takes text such as "0 off", "1 = on",
 *                      "0x10: boost" or two columns copied from a sheet
 *
 * Integer keys (enum, field values) may be written 0x..; with the Hex box
 * ticked the list shows (and the map saves) them that way. Number keys
 * (special values) are shown values, decimals allowed. A row whose value is not
 * one is marked, and left out of entries(). A row still being filled in (a value,
 * no name yet) changes nothing: edited() comes when the complete rows change, and
 * setEntries() with the entries the list already has leaves it as it is. */
#pragma once

#include <QVector>
#include <QWidget>

class QCheckBox;
class QTableWidget;

class NameTable : public QWidget {
	Q_OBJECT
public:
	enum class Keys { Integers, Numbers };
	struct Entry {
		double value = 0;
		QString name;
		bool operator==(const Entry &other) const { return value == other.value && name == other.name; }
	};

	NameTable(Keys keys, const QString &objectName, QWidget *parent = nullptr);

	void setEntries(const QVector<Entry> &entries, bool hex);
	/* the rows that are a value and a name, in value order */
	QVector<Entry> entries() const;
	bool hex() const;

signals:
	void edited(); /* a row changed, was added or removed, or the Hex box */

protected:
	void changeEvent(QEvent *event) override; /* the theme switched: its colours again (Theme::switched) */

private:
	void addRow(const QString &value, const QString &name);
	void pasteLines();
	QString keyText(double value, bool hex) const;
	bool parseKey(const QString &text, double &value) const;
	void markRows();
	void emitIfChanged(); /* edited() only when the complete rows are not what they were */
	bool drawnDark_ = true; /* the look the marks are drawn in */

	Keys keys_;
	QTableWidget *table_;
	QCheckBox *hex_ = nullptr;
	bool loading_ = false;
	QVector<Entry> last_; /* the entries as last set or reported */
	bool lastHex_ = false;
};

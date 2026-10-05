/* SPDX-License-Identifier: Apache-2.0 */
/* The bit fields of one register, as the Map editor edits them:
 *
 *   the register's bits (BitView in edit mode, with the live value in the
 *   fields): drag across bits to make a field of them, click a field to pick it
 *   Name | Bits | Access | Description     one row per field, edited in place
 *   + Field  − Field
 *   the picked field's value names (NameTable)
 *
 * edited() gives the whole list of fields as it is now; the register editor
 * makes it one undo step. A field keeps its `source` (where it was in the file),
 * so saving writes it as the file had it where nothing of it changed. */
#pragma once

#include <QVector>
#include <QWidget>

#include "model/device_map.h"

class BitView;
class NameTable;
class QLabel;
class QTableWidget;

class FieldEditor : public QWidget {
	Q_OBJECT
public:
	explicit FieldEditor(QWidget *parent = nullptr);

	void setRegister(const RegDef &def);
	void setLiveValue(quint64 value, bool valid);

signals:
	void edited(const QVector<BitField> &fields);

private:
	void fillTable();
	void pick(int index);                  /* the field shown below the table, -1 = none */
	void addField(int lsb, int width);
	void readTable();                      /* the table's cells into fields_, then edited() */
	static QString bitsText(const BitField &field);
	static bool parseBits(const QString &text, int &lsb, int &width);

	RegDef def_;
	QVector<BitField> fields_;
	int picked_ = -1;
	bool loading_ = false;
	BitView *bits_;
	QTableWidget *table_;
	QLabel *valuesTitle_;
	NameTable *values_;
};

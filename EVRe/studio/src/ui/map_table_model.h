/* SPDX-License-Identifier: Apache-2.0 */
/* The register list of the Map editor tab: one row per register of the
 * MapDocument, its definition in columns (no live values: those are on the
 * Registers tab).
 *
 * The cells are edited in place. An edit goes to the document as one undo step,
 * and to every register `targets` names for that row: the rows selected when
 * the edited row is one of them (a bulk edit), else only that row. The name and
 * the address are edited one register at a time. The first column (#) numbers
 * the rows in map order, and marks the registers the map's checks find something
 * on with a dot before the number (red: an error, amber: a warning). */
#pragma once

#include <QAbstractTableModel>
#include <QFont>
#include <functional>

class MapDocument;
struct RegDef;

class MapTableModel : public QAbstractTableModel {
	Q_OBJECT
public:
	enum Col { ColIssue, ColAddr, ColName, ColType, ColSize, ColUnit, ColAccess, ColWrite, ColGroup, ColDesc,
		ColMore, ColCount };
	/* the uids a change of `row` applies to */
	using Targets = std::function<QVector<quint32>(int row)>;

	explicit MapTableModel(MapDocument *doc, QObject *parent = nullptr);
	void setTargets(Targets targets) { targets_ = std::move(targets); }

	/* what a cell may be set to (the editors' lists): types, access words, write behaviours */
	static QStringList typeChoices();
	static QStringList accessChoices();
	static QStringList writeChoices();

	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColCount; }
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	Qt::ItemFlags flags(const QModelIndex &index) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role) override;

	quint32 uidAt(int row) const;

signals:
	/* an edit refused ("the address 0x10000 is past 0xFFFF"): for the status bar */
	void refused(const QString &why);

private:
	QString displayText(const RegDef &def, int column) const;
	/* the checks' findings on the register in `row`, one per line; error: any of them is one */
	QString issuesOf(int row, bool &error) const;

	MapDocument *doc_;
	Targets targets_;
	QFont monoFont_;
};

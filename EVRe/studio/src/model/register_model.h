/* SPDX-License-Identifier: Apache-2.0 */
/* The register table of the Registers tab: one row per register of the map,
 * its latest value, and whether it is plotted and logged.
 *
 * Values come from the I/O thread's snapshot (applyValue), possibly hundreds
 * of times a second: they are stored at once (the chart, the CSV and the API
 * read them) but the view is told at most every 50 ms. An edit of a writable
 * value does not change it here: it becomes writeRequested, and the window
 * decides. RegisterFilter below is the search box and group filter. */
#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTimer>

#include "model/device_map.h"

class RegisterModel : public QAbstractTableModel {
	Q_OBJECT
public:
	enum Col {
		ColPlot, ColLog, ColAddr, ColName, ColValue, ColUnit, ColDecoded, ColType, ColAccess, ColGroup, ColCount
	};

	struct Row {
		RegDef def;
		QByteArray raw;
		bool valid = false;
		QString error;              /* last read error, empty if fine */
		bool unavailable = false;   /* the device refused the address: not polled */
		bool plot = false;
		quint64 plotOrder = 0;      /* when it was plotted: a lower limit takes the newest off first */
		bool log = true;
		qint64 changedMs = -100000; /* the value last changed, or was first read since the link came up (glow clock) */
		qint64 litMs = -100000;     /* its glow began (a change after a rest): it glows GLOW_MS from here */
		int litDirection = 0;       /* that change: +1 up, -1 down, 0 no arrow (not a plain number) */
		bool settled = false;       /* read since the link came up: its next change may glow, the first read never */
		qint64 updatedMs = -100000; /* last successful read, changed or not */
	};

	/* setData role for an edit: QVariantMap { text, base } (base = the raw
	 * value when the edit started, so a change meanwhile can be caught) */
	static constexpr int WriteRole = Qt::UserRole + 1;
	static constexpr int RawRole = Qt::UserRole + 2; /* data(): the raw bytes now */
	/* data() of a Value cell: how strongly it glows now, 1 (just changed) .. 0 (plain); ValueDelegate draws it */
	static constexpr int GlowRole = Qt::UserRole + 3;
	/* data() of a Value cell: which way the glowing change went, +1 up, -1 down, 0 none (an enum, bits, hex) */
	static constexpr int GlowDirectionRole = Qt::UserRole + 4;

	explicit RegisterModel(QObject *parent = nullptr);

	/* the map's registers (MapDocument), or a bus's: every device's: a register already here (the same uid and
	 * device) keeps its value, its plot and its log tick; its value is dropped only if it is read differently now */
	void setDefinitions(const QVector<RegDef> &defs);
	const QVector<Row> &rows() const { return rows_; }
	/* the device whose map is edited (RegDef::slave; 0: the one device of a map): rowOfUid looks among its rows */
	void setSelectedDevice(uint8_t slave) { selected_ = slave; }
	uint8_t selectedDevice() const { return selected_; }
	int rowOfUid(quint32 uid) const; /* the selected device's register of the map editor's uid; -1 if none */
	QVector<RegDef> definitions() const; /* every row's register, in the table's order: the map as edited */
	/* a value as the I/O thread holds it; ageMs = how long ago it was read */
	void applyValue(int i, const QByteArray &raw, bool valid, const QString &error, bool unavailable, qint64 ageMs);

	void setWritesEnabled(bool on);
	bool writesEnabled() const { return writesEnabled_; }
	/* Listen is on: nothing is written (writes stay off) and a value never read says why it is "—" */
	void setListening(bool on);
	bool listening() const { return listening_; }
	/* the link is up: a Value cell is editable only then (with writes enabled), as the quick-write panel's controls */
	void setLinkUp(bool up);
	bool linkUp() const { return linkUp_; }
	/* The registers on the chart: PLOT_SAMPLES_PER_SECOND for all the lines together, at most MAX_PLOTTED (more
	 * cannot be followed by eye): 64 up to 1000 Hz, 32 at 2000 Hz, 16 at 4000 Hz. A Plot past the limit is refused
	 * (false, and plotLimitReached); a lower limit takes the newest lines off (plotsTakenOff). */
	static constexpr int MAX_PLOTTED = 64;
	static constexpr double PLOT_SAMPLES_PER_SECOND = 64000;
	static int plotLimitFor(double samplesPerSecond); /* each line's rate; 0: not known, MAX_PLOTTED */
	/* A value glows when it changes after holding still GLOW_REST_MS: one that changes at every poll (a noisy ADC)
	 * would flicker all the time, and a glow is to say "this just moved". It glows GLOW_MS, fading out
	 * (glowStrength); the Dump tab's bytes follow the same rule, so the two tabs pulse together. */
	static constexpr qint64 GLOW_MS = 1500;
	static constexpr qint64 GLOW_REST_MS = 2000;
	static double glowStrength(qint64 sinceMs); /* 1 at the change .. 0 at GLOW_MS, eased out: (1 - k)^2 */
	/* the glow's arrow: a plain number (not an enum, bit fields, hex or bytes) */
	static bool showsDirection(const RegDef &def);
	/* the setting registers/glow: off, nothing glows (and a glow under way ends) */
	void setGlowOn(bool on);
	bool glowOn() const { return glowOn_; }
	double glow(int row) const;     /* the row's glow now, 1 .. 0 */
	void shiftGlowClock(qint64 ms); /* tests: the glow's clock moves on as if ms had passed */
	void setPlotLimit(int limit);
	int plotLimit() const { return plotLimit_; }
	bool setPlot(int i, bool on);
	int plottedCount() const;
	void setAllLog(bool on);

	/* the text colours, the theme's (the model knows no theme): set at start and when it changes */
	struct Colors {
		QColor accent{ 79, 140, 255 };  /* rw (the glow is ValueDelegate's, from the theme) */
		QColor error{ 255, 92, 92 };    /* a value that could not be read */
		QColor warn{ 255, 176, 32 };    /* a danger register's access, "not refreshed" in the tooltip */
		QColor muted{ 139, 147, 161 };  /* unit, type, group, decoded; a value not refreshed */
	};
	void setColors(const Colors &colors) { colors_ = colors; }
	/* a value older than this is shown greyed out: not refreshed */
	void setStaleAfterMs(qint64 ms) { staleAfterMs_ = ms; }

	int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows_.size()); }
	int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : ColCount; }
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	Qt::ItemFlags flags(const QModelIndex &index) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role) override;

signals:
	void writeRequested(int row, const QString &text, const QByteArray &base);
	void plotChanged(int row, bool on);
	void plotLimitReached(); /* a Plot refused: plotLimit() are on the chart */
	void plotsTakenOff(const QStringList &names); /* a lower limit took these off the chart, the newest first */
	void structureChanged(); /* rows added / removed / redefined: the poller rebuilds */
	void glowOnChanged(bool on); /* setGlowOn: the Dump's bytes follow */

private:
	bool isStale(const Row &row) const;
	/* the pieces of data(), one per role */
	QVariant checkState(const Row &row, int column) const;
	QVariant displayText(const Row &row, int column) const;
	static Qt::Alignment alignment(int column); /* of a column's cells and of its header */
	QString toolTip(const Row &row) const;
	QVariant font(int column) const;
	QVariant foreground(const Row &row, int column) const;
	double glow(const Row &row) const;
	qint64 glowNow() const { return clock_.elapsed() + glowShift_; }

	/* the throttled repaint: rows changed since the last one */
	void markDirty(int row);
	void repaintDirtyRows();
	void repaintGlowing(); /* the glow's own repaint while a value fades: its Value cells only */
	void refreshColumns(int firstColumn, int lastColumn); /* every row */

	QVector<Row> rows_;
	uint8_t selected_ = 0;
	int plotLimit_ = MAX_PLOTTED;
	quint64 nextPlotOrder_ = 1;
	bool writesEnabled_ = false;
	bool listening_ = false;
	bool linkUp_ = false;
	QElapsedTimer clock_;       /* the time base of changedMs / updatedMs */
	qint64 staleAfterMs_ = 1000;
	Colors colors_;
	QFont monoFont_, monoBoldFont_;
	QTimer repaintTimer_;
	int dirtyFirst_ = -1, dirtyLast_ = -1;
	bool glowOn_ = true;
	qint64 glowShift_ = 0;   /* shiftGlowClock's */
	QTimer glowTimer_;       /* runs only while a value glows */
	QSet<int> glowing_;      /* the rows that glow (or just stopped: one more repaint draws them plain) */
};

/* The search box, the group filter and the device over the register table: a
 * row is shown when it is the device's (a bus: one device at a time), its
 * group is ticked (none ticked: every group) and the search text is in its
 * name, description, group, unit or address. */
class RegisterFilter : public QSortFilterProxyModel {
	Q_OBJECT
public:
	using QSortFilterProxyModel::QSortFilterProxyModel;
	void setText(const QString &text);
	void setGroups(const QStringList &groups); /* empty: all */
	void setDevice(int slave);                 /* RegDef::slave of the rows shown; -1: every row */

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
	/* Qt 6.10 replaced invalidateFilter() by a begin / end pair around the change */
	void beginRowFilterChange();
	void endRowFilterChange();

	QString text_;
	QSet<QString> groups_;
	int device_ = -1;
};

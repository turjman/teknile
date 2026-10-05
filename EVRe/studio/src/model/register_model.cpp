/* SPDX-License-Identifier: Apache-2.0 */
/* The register table and its filter: see register_model.h. */
#include "model/register_model.h"

#include <QHash>

#include <algorithm>

namespace {

constexpr int REPAINT_MS = 50;   /* the view is told of new values at most this often */
constexpr qint64 GLOW_MS = 700;  /* a changed value glows this long, fading out */
constexpr int GLOW_ALPHA = 90;   /* how strongly it starts */

/* "0x00ab": an address as the tooltip shows it and the search box matches it
 * (the Address column shows addrText's upper-case digits) */
QString lowerCaseAddrText(uint16_t addr) {
	return QStringLiteral("0x") + QStringLiteral("%1").arg(addr, 4, 16, QLatin1Char('0'));
}

/* a register not read yet */
RegisterModel::Row rowFor(const RegDef &def) {
	RegisterModel::Row row;
	row.def = def;
	return row;
}

/* no value to show: the device refused the register, or the last read failed */
bool showsError(const RegisterModel::Row &row) {
	return row.unavailable || (!row.error.isEmpty() && !row.valid);
}

} // namespace

RegisterModel::RegisterModel(QObject *parent) : QAbstractTableModel(parent) {
	clock_.start();
	monoFont_ = QFont(QStringLiteral("Cascadia Mono"));
	monoFont_.setStyleHint(QFont::Monospace);
	monoBoldFont_ = monoFont_;
	monoBoldFont_.setBold(true);
	repaintTimer_.setSingleShot(true);
	repaintTimer_.setInterval(REPAINT_MS);
	connect(&repaintTimer_, &QTimer::timeout, this, &RegisterModel::repaintDirtyRows);
}

/* ------------------------------------------------ the rows and their values */

namespace {

/* the bytes of a register still mean what they did: its value can stay */
bool readsTheSame(const RegDef &a, const RegDef &b) {
	return a.slave == b.slave && a.addr == b.addr && a.type == b.type && a.size == b.size && a.readable == b.readable;
}

/* a register's identity in the table: its uid in its map, and its device (two devices may share a map) */
quint64 identity(const RegDef &def) { return (quint64(def.slave) << 32) | def.uid; }

/* what a chart line shows of the register: a change here draws it again */
bool plotsTheSame(const RegDef &a, const RegDef &b) {
	return readsTheSame(a, b) && a.name == b.name && a.unit == b.unit && a.scale == b.scale && a.offset == b.offset;
}

} // namespace

void RegisterModel::setDefinitions(const QVector<RegDef> &defs) {
	QHash<quint64, int> before;
	for (int i = 0; i < rows_.size(); i++) before.insert(identity(rows_[i].def), i);
	/* the lines of plotted registers that go or change: off the chart first, while their rows are here */
	QSet<quint64> replot;
	for (int i = 0; i < rows_.size(); i++) {
		if (!rows_[i].plot) continue;
		const quint64 id = identity(rows_[i].def);
		const auto it = std::find_if(defs.begin(), defs.end(), [id](const RegDef &d) { return identity(d) == id; });
		if (it != defs.end() && plotsTheSame(rows_[i].def, *it)) continue;
		setPlot(i, false);
		if (it != defs.end() && it->isNumeric()) replot.insert(id);
	}

	QVector<Row> rows;
	rows.reserve(defs.size());
	bool sameRows = defs.size() == rows_.size();
	for (int i = 0; i < defs.size(); i++) {
		const RegDef &def = defs[i];
		const int was = def.uid ? before.value(identity(def), -1) : -1;
		if (was < 0) {
			rows.push_back(rowFor(def));
			sameRows = false;
			continue;
		}
		Row row = rows_[was];
		if (!readsTheSame(row.def, def)) {
			row.raw.clear();
			row.valid = false;
			row.unavailable = false;
			row.error.clear();
		}
		row.def = def;
		rows.push_back(row);
		if (was != i) sameRows = false;
	}
	if (sameRows) {
		/* the same registers in the same places: only what they show changed */
		rows_ = rows;
		if (!rows_.isEmpty()) emit dataChanged(index(0, 0), index(int(rows_.size()) - 1, ColCount - 1));
	} else {
		beginResetModel();
		dirtyFirst_ = dirtyLast_ = -1;
		rows_ = rows;
		endResetModel();
	}
	emit structureChanged();
	for (int i = 0; i < rows_.size(); i++)
		if (replot.contains(identity(rows_[i].def))) setPlot(i, true);
}

int RegisterModel::rowOfUid(quint32 uid) const {
	for (int i = 0; i < rows_.size(); i++)
		if (rows_[i].def.uid == uid && rows_[i].def.slave == selected_) return i;
	return -1;
}

QVector<RegDef> RegisterModel::definitions() const {
	QVector<RegDef> defs;
	defs.reserve(rows_.size());
	for (const Row &row : rows_) defs << row.def;
	return defs;
}

void RegisterModel::applyValue(int i, const QByteArray &raw, bool valid, const QString &error, bool unavailable,
		qint64 ageMs) {
	Row &row = rows_[i];
	if (valid) {
		const qint64 now = clock_.elapsed();
		if (row.valid && raw != row.raw) row.changedMs = now;
		row.raw = raw;
		row.updatedMs = now - ageMs;
	} else {
		row.raw.clear();
	}
	row.valid = valid;
	row.error = error;
	row.unavailable = unavailable;
	markDirty(i);
}

bool RegisterModel::isStale(const Row &row) const {
	return row.valid && clock_.elapsed() - row.updatedMs > staleAfterMs_;
}

void RegisterModel::setWritesEnabled(bool on) {
	writesEnabled_ = on;
	refreshColumns(ColValue, ColValue); /* the cells become editable or not */
}

int RegisterModel::plotLimitFor(double samplesPerSecond) {
	if (!(samplesPerSecond > 0)) return MAX_PLOTTED;
	return int(std::clamp(std::floor(PLOT_SAMPLES_PER_SECOND / samplesPerSecond), 1.0, double(MAX_PLOTTED)));
}

void RegisterModel::setPlotLimit(int limit) {
	plotLimit_ = std::clamp(limit, 1, MAX_PLOTTED);
	QStringList off;
	while (plottedCount() > plotLimit_) {
		int newest = -1;
		for (int i = 0; i < rows_.size(); i++)
			if (rows_[i].plot && (newest < 0 || rows_[i].plotOrder > rows_[newest].plotOrder)) newest = i;
		off << rows_[newest].def.name;
		setPlot(newest, false);
	}
	if (!off.isEmpty()) emit plotsTakenOff(off);
}

bool RegisterModel::setPlot(int i, bool on) {
	if (rows_[i].plot == on) return true;
	if (on && !rows_[i].def.canPlot()) return false; /* a bytes register, or one the map does not let plot */
	if (on && plottedCount() >= plotLimit_) {
		emit plotLimitReached();
		return false;
	}
	rows_[i].plot = on;
	if (on) rows_[i].plotOrder = nextPlotOrder_++;
	emit dataChanged(index(i, ColPlot), index(i, ColPlot));
	emit plotChanged(i, on);
	return true;
}

int RegisterModel::plottedCount() const {
	return int(std::count_if(rows_.begin(), rows_.end(), [](const Row &row) { return row.plot; }));
}

void RegisterModel::setAllLog(bool on) {
	for (Row &row : rows_) row.log = on;
	refreshColumns(ColLog, ColLog);
}

/* ---------------------------------------------------- the throttled repaint */

void RegisterModel::markDirty(int row) {
	dirtyFirst_ = dirtyFirst_ < 0 ? row : std::min(dirtyFirst_, row);
	dirtyLast_ = std::max(dirtyLast_, row);
	if (!repaintTimer_.isActive()) repaintTimer_.start();
}

void RegisterModel::repaintDirtyRows() {
	if (dirtyFirst_ < 0) return;
	const int first = dirtyFirst_;
	const int last = std::min(dirtyLast_, int(rows_.size()) - 1);
	dirtyFirst_ = dirtyLast_ = -1;
	if (first <= last) emit dataChanged(index(first, ColAddr), index(last, ColDecoded));
}

void RegisterModel::refreshColumns(int firstColumn, int lastColumn) {
	if (!rows_.isEmpty()) emit dataChanged(index(0, firstColumn), index(int(rows_.size()) - 1, lastColumn));
}

/* ------------------------------------------------------ what the view shows */

QVariant RegisterModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid()) return {};
	const Row &row = rows_[index.row()];
	const int column = index.column();
	switch (role) {
	case RawRole: return row.valid ? row.raw : QByteArray();
	case Qt::CheckStateRole: return checkState(row, column);
	case Qt::DisplayRole: return displayText(row, column);
	case Qt::EditRole:
		/* the value to edit: the number itself, never a placeholder */
		if (column == ColValue) return row.valid ? formatValue(row.def, row.raw) : QString();
		return displayText(row, column);
	case Qt::ToolTipRole:
		if (column == ColPlot && !row.def.isNumeric()) return tr("Not plotted: a bytes register is not a number");
		if (column == ColPlot && !row.def.plottable)
			return tr("Not plotted: the map marks it a fixed value (\"plot\": false, the Map editor's plot box)");
		return toolTip(row);
	case Qt::TextAlignmentRole: return int(alignment(column));
	case Qt::FontRole: return font(column);
	case Qt::ForegroundRole: return foreground(row, column);
	case Qt::BackgroundRole: return column == ColValue ? valueGlow(row) : QVariant();
	default: return {};
	}
}

QVariant RegisterModel::checkState(const Row &row, int column) const {
	if (column == ColPlot) return row.def.canPlot() ? QVariant(row.plot ? Qt::Checked : Qt::Unchecked) : QVariant();
	if (column == ColLog) return row.log ? Qt::Checked : Qt::Unchecked;
	return {};
}

QVariant RegisterModel::displayText(const Row &row, int column) const {
	const RegDef &def = row.def;
	switch (column) {
	case ColPlot: return def.canPlot() ? QVariant() : QVariant(QStringLiteral("—")); /* no box: why, in its tooltip */
	case ColAddr: return addrText(def.addr);
	case ColName: return def.name;
	case ColValue:
		if (row.unavailable) return tr("not available");
		if (showsError(row)) return tr("error");
		return row.valid ? formatValue(def, row.raw) : QStringLiteral("—");
	case ColUnit: return def.unit;
	case ColDecoded: return row.valid ? formatDecoded(def, row.raw) : QString();
	case ColType: return def.type == RegType::Bytes ? QStringLiteral("bytes[%1]").arg(def.size) : typeName(def.type);
	case ColAccess: /* as the map and the Map editor write it */
		return def.danger ? accessText(def) + QStringLiteral(" ⚠") : accessText(def);
	case ColGroup: return def.group;
	default: return {};
	}
}

QString RegisterModel::toolTip(const Row &row) const {
	const RegDef &def = row.def;
	QString tip = QStringLiteral("<b>%1</b> &nbsp; %2<br>").arg(def.name.toHtmlEscaped(), lowerCaseAddrText(def.addr));
	if (!def.desc.isEmpty()) tip += def.desc.toHtmlEscaped() + QStringLiteral("<br>");
	if (row.valid) {
		tip += tr("raw: %1").arg(QString::fromLatin1(row.raw.toHex(' ').toUpper()));
		/* the decoded fields in full, one per line: the cell may cut them */
		const QString decoded = formatDecoded(def, row.raw);
		if (!decoded.isEmpty())
			tip += QStringLiteral("<br>") + decoded.toHtmlEscaped().replace(QLatin1String("  "), QLatin1String("<br>"));
		const double ageS = double(clock_.elapsed() - row.updatedMs) / 1000.0;
		tip += isStale(row)
				? QStringLiteral("<br><span style='color:%1'>").arg(colors_.warn.name())
						+ tr("not refreshed: last read %1 s ago").arg(ageS, 0, 'f', 1) + QStringLiteral("</span>")
				: tr("<br>read %1 s ago").arg(ageS, 0, 'f', 1);
	}
	if (!row.error.isEmpty())
		tip += QStringLiteral("<br><span style='color:%1'>%2</span>").arg(colors_.error.name(), row.error.toHtmlEscaped());
	if (def.danger) tip += tr("<br>⚠ writes are confirmed");
	return tip;
}

QVariant RegisterModel::font(int column) const {
	if (column == ColValue) return monoBoldFont_;
	if (column == ColAddr || column == ColType) return monoFont_;
	return {};
}

QVariant RegisterModel::foreground(const Row &row, int column) const {
	const RegDef &def = row.def;
	if (column == ColValue && showsError(row)) return colors_.error;
	if ((column == ColValue || column == ColDecoded) && isStale(row)) return colors_.muted;
	if (column == ColAccess && def.danger) return colors_.warn;
	if (column == ColAccess && def.rw) return colors_.accent;
	if (column == ColPlot || column == ColDecoded || column == ColUnit || column == ColType || column == ColGroup)
		return colors_.muted;
	return {};
}

/* a value that just changed glows for a moment */
QVariant RegisterModel::valueGlow(const Row &row) const {
	const qint64 sinceChange = clock_.elapsed() - row.changedMs;
	if (sinceChange >= GLOW_MS) return {};
	QColor glow = colors_.accent;
	glow.setAlpha(int(GLOW_ALPHA * (1.0 - double(sinceChange) / double(GLOW_MS))));
	return glow;
}

/* a column's text: numbers (the value) at the right, the rest at the left; its header the same */
Qt::Alignment RegisterModel::alignment(int column) {
	return (column == ColValue ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
}

QVariant RegisterModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (orientation != Qt::Horizontal || section < 0 || section >= ColCount) return {};
	if (role == Qt::TextAlignmentRole) return int(alignment(section));
	if (role != Qt::DisplayRole) return {};
	static const char *const titles[ColCount] = { QT_TR_NOOP("Plot"), QT_TR_NOOP("Log"), QT_TR_NOOP("Address"),
		QT_TR_NOOP("Name"), QT_TR_NOOP("Value"), QT_TR_NOOP("Unit"), QT_TR_NOOP("Decoded"), QT_TR_NOOP("Type"),
		QT_TR_NOOP("Access"), QT_TR_NOOP("Group") };
	return tr(titles[section]);
}

Qt::ItemFlags RegisterModel::flags(const QModelIndex &index) const {
	if (!index.isValid()) return Qt::NoItemFlags;
	const Row &row = rows_[index.row()];
	const int column = index.column();
	Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	if (column == ColPlot && row.def.canPlot()) itemFlags |= Qt::ItemIsUserCheckable;
	if (column == ColLog) itemFlags |= Qt::ItemIsUserCheckable;
	if (column == ColValue && writesEnabled_ && row.def.rw) itemFlags |= Qt::ItemIsEditable;
	return itemFlags;
}

bool RegisterModel::setData(const QModelIndex &index, const QVariant &value, int role) {
	if (!index.isValid()) return false;
	const int i = index.row();
	const int column = index.column();
	if (role == Qt::CheckStateRole && column == ColPlot) return setPlot(i, value.toInt() == Qt::Checked);
	if (role == Qt::CheckStateRole && column == ColLog) {
		rows_[i].log = value.toInt() == Qt::Checked;
		emit dataChanged(index, index);
		return true;
	}
	/* an edit asks for a write: the value shown changes when the device says so, never here */
	if (column == ColValue && role == WriteRole) {
		const QVariantMap edit = value.toMap();
		emit writeRequested(i, edit.value(QStringLiteral("text")).toString(),
				edit.value(QStringLiteral("base")).toByteArray());
	} else if (column == ColValue && role == Qt::EditRole) {
		emit writeRequested(i, value.toString(), rows_[i].raw);
	}
	return false;
}

/* ----------------------------------------------------------- RegisterFilter */

void RegisterFilter::setText(const QString &text) {
	beginRowFilterChange();
	text_ = text;
	endRowFilterChange();
}

void RegisterFilter::setGroups(const QStringList &groups) {
	beginRowFilterChange();
	groups_ = QSet<QString>(groups.begin(), groups.end());
	endRowFilterChange();
}

void RegisterFilter::setDevice(int slave) {
	if (slave == device_) return;
	beginRowFilterChange();
	device_ = slave;
	endRowFilterChange();
}

void RegisterFilter::beginRowFilterChange() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	beginFilterChange();
#endif
}

void RegisterFilter::endRowFilterChange() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
	endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
	invalidateFilter();
#endif
}

bool RegisterFilter::filterAcceptsRow(int sourceRow, const QModelIndex &) const {
	const auto *model = static_cast<const RegisterModel *>(sourceModel());
	const RegDef &def = model->rows()[sourceRow].def;
	if (device_ >= 0 && def.slave != device_) return false;
	if (!groups_.isEmpty() && !groups_.contains(def.group)) return false;
	if (text_.isEmpty()) return true;
	for (const QString &field : { def.name, def.desc, def.group, def.unit, lowerCaseAddrText(def.addr) })
		if (field.contains(text_, Qt::CaseInsensitive)) return true;
	return false;
}

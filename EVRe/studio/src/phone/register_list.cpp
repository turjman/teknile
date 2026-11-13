/* SPDX-License-Identifier: Apache-2.0 */
/* The Registers screen's rows: see register_list.h. */
#include "phone/register_list.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHash>
#include <algorithm>

RegisterList::RegisterList(RegisterModel *model, QObject *parent) : QAbstractListModel(parent), model_(model) {
	/* the filter is asked only in rebuild(): a value's change never moves a row, so it need not look at changes */
	filter_.setDynamicSortFilter(false);
	filter_.setSourceModel(model_); /* before our own connections: it is reset first */
	connect(model_, &QAbstractItemModel::modelReset, this, &RegisterList::rebuild);
	connect(model_, &QAbstractItemModel::dataChanged, this, &RegisterList::forward);
	staleTimer_.setInterval(STALE_CHECK_MS);
	connect(&staleTimer_, &QTimer::timeout, this, &RegisterList::checkStale);
	staleTimer_.start();
	rebuild();
}

/* the rows the search keeps, each group's together in the order of its first register in the map */
void RegisterList::rebuild() {
	const QVector<RegisterModel::Row> &rows = model_->rows();
	QHash<QString, int> rank;
	for (const RegisterModel::Row &row : rows)
		if (!rank.contains(row.def.group)) rank.insert(row.def.group, int(rank.size()));
	QVector<int> shown;
	for (int i = 0; i < rows.size(); ++i)
		if (filter_.mapFromSource(model_->index(i, 0)).isValid()) shown << i;
	std::stable_sort(shown.begin(), shown.end(),
			[&](int a, int b) { return rank.value(rows[a].def.group) < rank.value(rows[b].def.group); });

	beginResetModel();
	shown_ = shown;
	listRow_.fill(-1, rows.size());
	for (int i = 0; i < shown_.size(); ++i) listRow_[shown_[i]] = i;
	lit_.resize(rows.size());
	stale_.resize(rows.size());
	for (int i = 0; i < rows.size(); ++i) {
		lit_[i] = rows[i].litMs;
		stale_[i] = isStale(i);
	}
	endResetModel();
	emit countChanged(); /* the rows shown, or the map's (another map) */
}

void RegisterList::setSearch(const QString &text) {
	if (text == search_) return;
	search_ = text;
	filter_.setText(text.trimmed());
	rebuild();
	emit searchChanged();
}

int RegisterList::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(shown_.size()); }

/* no value to show: the device refused the register, or the last read failed (RegisterModel's own rule) */
bool RegisterList::showsError(int source) const {
	const RegisterModel::Row &row = model_->rows()[source];
	return row.unavailable || (!row.error.isEmpty() && !row.valid);
}

/* RegisterModel greys a value that was not refreshed: its Value cell's colour is set then, or for an error */
bool RegisterList::isStale(int source) const {
	const RegisterModel::Row &row = model_->rows()[source];
	return row.valid && !showsError(source)
			&& model_->data(model_->index(source, RegisterModel::ColValue), Qt::ForegroundRole).isValid();
}

QVariant RegisterList::data(const QModelIndex &index, int role) const {
	const int source = index.isValid() ? sourceRow(index.row()) : -1;
	if (source < 0 || source >= model_->rows().size()) return {};
	const RegisterModel::Row &row = model_->rows()[source];
	const RegDef &def = row.def;
	switch (role) {
	case NameRole: return def.name;
	case GroupRole: return def.group;
	case AddrRole: return addrText(def.addr);
	case ValueRole: return model_->data(model_->index(source, RegisterModel::ColValue), Qt::DisplayRole);
	case UnitRole: return def.unit;
	case DecodedRole: return model_->data(model_->index(source, RegisterModel::ColDecoded), Qt::DisplayRole);
	case GlowRole: return model_->glow(source);
	case DirectionRole: return model_->glow(source) > 0 ? row.litDirection : 0;
	case WritableRole: return def.rw;
	case PlottedRole: return row.plot;
	case ErrorRole:
		if (!showsError(source)) return QString();
		return row.error.isEmpty() ? tr("the device refused this address") : row.error;
	case StaleRole: return isStale(source);
	case PlottableRole: return def.canPlot();
	case DangerRole: return def.danger;
	case SourceRole: return source;
	default: return {};
	}
}

QHash<int, QByteArray> RegisterList::roleNames() const {
	return { { NameRole, "name" }, { GroupRole, "group" }, { AddrRole, "addr" }, { ValueRole, "value" },
		{ UnitRole, "unit" }, { DecodedRole, "decoded" }, { GlowRole, "glow" }, { DirectionRole, "direction" },
		{ WritableRole, "writable" }, { PlottedRole, "plotted" }, { ErrorRole, "error" }, { StaleRole, "stale" },
		{ PlottableRole, "plottable" }, { DangerRole, "danger" }, { SourceRole, "source" } };
}

/* RegisterModel's change, row by row: its columns become the roles a card shows. Its glow's own repaints (GlowRole
 * alone, every 50 ms while a value fades) pass only when a new glow begins: the card fades it by itself. */
void RegisterList::forward(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
	if (!topLeft.isValid() || !bottomRight.isValid()) return;
	const bool glowOnly = roles.size() == 1 && roles.first() == RegisterModel::GlowRole;
	const int first = topLeft.column(), last = bottomRight.column();
	auto spans = [&](int column) { return first <= column && column <= last; };
	QList<int> changed;
	if (!glowOnly) {
		if (spans(RegisterModel::ColPlot)) changed << PlottedRole;
		if (spans(RegisterModel::ColName)) changed << NameRole << GroupRole << AddrRole << UnitRole << WritableRole
				<< PlottableRole << DangerRole;
		if (spans(RegisterModel::ColValue) || spans(RegisterModel::ColDecoded))
			changed << ValueRole << DecodedRole << ErrorRole << StaleRole;
	}
	const bool valueSpan = spans(RegisterModel::ColValue);
	for (int source = topLeft.row(); source <= bottomRight.row() && source < model_->rows().size(); ++source) {
		QList<int> rowRoles = changed;
		if (valueSpan && source < lit_.size() && model_->rows()[source].litMs != lit_[source]) {
			lit_[source] = model_->rows()[source].litMs;
			if (model_->glow(source) > 0) rowRoles << GlowRole << DirectionRole;
		}
		if (source < stale_.size()) stale_[source] = isStale(source);
		const int row = rowOf(source);
		if (row < 0 || rowRoles.isEmpty()) continue;
		emit dataChanged(index(row), index(row), rowRoles);
	}
}

/* time passes: a value not refreshed for a while greys, without a change of its own */
void RegisterList::checkStale() {
	const int rows = int(std::min(model_->rows().size(), qsizetype(stale_.size())));
	for (int source = 0; source < rows; ++source) {
		const bool stale = isStale(source);
		if (stale == stale_[source]) continue;
		stale_[source] = stale;
		const int row = rowOf(source);
		if (row >= 0) emit dataChanged(index(row), index(row), { StaleRole });
	}
}

QVariantMap RegisterList::entry(int source) const {
	QVariantMap map;
	const int row = rowOf(source);
	if (row < 0) return map;
	const QHash<int, QByteArray> names = roleNames();
	for (auto it = names.constBegin(); it != names.constEnd(); ++it)
		map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
	return map;
}

QString RegisterList::copy(int source) const {
	if (source < 0 || source >= model_->rows().size()) return QString();
	const RegDef &def = model_->rows()[source].def;
	const QString value = model_->data(model_->index(source, RegisterModel::ColValue), Qt::DisplayRole).toString();
	const QString text = def.unit.isEmpty() ? QStringLiteral("%1 = %2").arg(def.name, value)
			: QStringLiteral("%1 = %2 %3").arg(def.name, value, def.unit);
	if (QClipboard *clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
	return text;
}

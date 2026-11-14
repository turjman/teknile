/* SPDX-License-Identifier: Apache-2.0 */
/* A list model's rows filtered by one role: see row_filter.h. */
#include "phone/row_filter.h"

#include <QSettings>
#include <QStringList>
#include <algorithm>
#include <utility>

RowFilter::RowFilter(QObject *parent) : QSortFilterProxyModel(parent) {
	/* the role's number comes with the model (QML may set the model after the name) */
	connect(this, &QSortFilterProxyModel::sourceModelChanged, this, [this] {
		findRole();
		invalidateRowsFilter();
	});
	for (auto signal : { &QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved })
		connect(this, signal, this, &RowFilter::countChanged);
	connect(this, &QAbstractItemModel::modelReset, this, &RowFilter::countChanged);
	connect(this, &QAbstractItemModel::layoutChanged, this, &RowFilter::countChanged);
}

void RowFilter::findRole() {
	role_ = -1;
	if (!sourceModel()) return;
	const QHash<int, QByteArray> roles = sourceModel()->roleNames();
	for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
		if (QString::fromLatin1(it.value()) == roleName_) role_ = it.key();
}

void RowFilter::setRoleName(const QString &name) {
	if (name == roleName_) return;
	roleName_ = name;
	findRole();
	invalidateRowsFilter();
	emit filterChanged();
	emit countChanged();
}

QVariantList RowFilter::shown() const {
	QVariantList values;
	for (int value : shown_) values << value;
	return values;
}

void RowFilter::setShown(const QVariantList &values) {
	QList<int> fresh;
	for (const QVariant &value : values) fresh << value.toInt();
	std::sort(fresh.begin(), fresh.end());
	fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
	if (fresh == shown_) return;
	shown_ = fresh;
	/* as text, "0,2": a list of one would come back from an ini file as a plain string */
	if (complete_ && !settingsKey_.isEmpty()) {
		QStringList parts;
		for (int value : shown_) parts << QString::number(value);
		QSettings().setValue(settingsKey_, parts.join(QLatin1Char(',')));
	}
	invalidateRowsFilter();
	emit filterChanged();
	emit countChanged();
}

void RowFilter::setSettingsKey(const QString &key) {
	if (key == settingsKey_) return;
	settingsKey_ = key;
	if (complete_) loadSaved();
	emit filterChanged();
}

void RowFilter::componentComplete() {
	loadSaved();
	complete_ = true;
}

/* the saved choice, read and not written back */
void RowFilter::loadSaved() {
	const QVariant saved = settingsKey_.isEmpty() ? QVariant() : QSettings().value(settingsKey_);
	if (!saved.isValid()) return;
	QVariantList values;
	for (const QString &part : saved.toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) values << part.trimmed().toInt();
	const bool was = std::exchange(complete_, false);
	setShown(values);
	complete_ = was;
}

void RowFilter::setValueShown(int value, bool on) {
	QVariantList values = shown();
	values.removeAll(value);
	if (on) values << value;
	setShown(values);
}

bool RowFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
	if (role_ < 0 || !sourceModel()) return true;
	return shown_.contains(sourceModel()->index(sourceRow, 0, sourceParent).data(role_).toInt());
}

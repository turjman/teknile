/* SPDX-License-Identifier: Apache-2.0 */
/* A list model's rows filtered by one role (RowFilter, for QML): only the rows whose `roleName` value is one of `shown`.
 * The Log shows the levels chosen by its filter chips (LogModel's "level": Info, Warning, Error), the Monitor the sides
 * chosen (FrameLog's "side": sent, received; its notes always). With `settingsKey` the choice is kept in the settings
 * (phone/logLevels, phone/monitorSides) and comes back at the next start. count: the rows shown. */
#pragma once

#include <QQmlParserStatus>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class RowFilter : public QSortFilterProxyModel, public QQmlParserStatus {
	Q_OBJECT
	Q_INTERFACES(QQmlParserStatus)
	QML_ELEMENT
	Q_PROPERTY(QString roleName READ roleName WRITE setRoleName NOTIFY filterChanged)
	Q_PROPERTY(QVariantList shown READ shown WRITE setShown NOTIFY filterChanged)
	Q_PROPERTY(QString settingsKey READ settingsKey WRITE setSettingsKey NOTIFY filterChanged)
	Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
	explicit RowFilter(QObject *parent = nullptr);

	QString roleName() const { return roleName_; }
	void setRoleName(const QString &name);
	QVariantList shown() const;
	void setShown(const QVariantList &values);
	QString settingsKey() const { return settingsKey_; }
	/* the choice saved before, if any, replaces the one QML sets (read once every property is set: QML sets them in
	 * no fixed order); a choice made after that is saved */
	void setSettingsKey(const QString &key);
	void classBegin() override {}
	void componentComplete() override;
	int count() const { return rowCount(); }
	/* one value shown or not: a filter chip's tap */
	Q_INVOKABLE void setValueShown(int value, bool on);
	Q_INVOKABLE bool valueShown(int value) const { return shown_.contains(value); }

signals:
	void filterChanged();
	void countChanged();

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
	void findRole();
	QString roleName_;
	int role_ = -1;
	QList<int> shown_;
	QString settingsKey_;
	bool complete_ = false;
	void loadSaved();
};

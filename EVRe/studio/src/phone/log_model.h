/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Log (LogModel, a list model for QML): what happened on the link, newest last, the newest MAX_LINES kept.
 * PhoneSession writes it; the Log screen (LogScreen.qml) shows each line with its level's icon and colour, the levels
 * chosen by its filter chips (a RowFilter over the "level" role) with each level's count, Clear empties it and Copy
 * puts the lines shown on the clipboard as text, each with its level as the desktop's file writes it (INFO, WARN,
 * ERROR). */
#pragma once

#include <QAbstractListModel>
#include <QTime>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class LogModel : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the Log is PhoneSession's: PhoneSession.log")
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int infoCount READ infoCount NOTIFY countChanged)
	Q_PROPERTY(int warningCount READ warningCount NOTIFY countChanged)
	Q_PROPERTY(int errorCount READ errorCount NOTIFY countChanged)

public:
	/* the desktop's levels (event_log.h), as numbers QML compares */
	enum Level { Info = 0, Warning = 1, Error = 2 };
	Q_ENUM(Level)
	enum Role { LevelRole = Qt::UserRole + 1, TimeRole, TextRole };
	static constexpr int MAX_LINES = 2000;

	explicit LogModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;
	int count() const { return int(lines_.size()); }
	int infoCount() const { return counts_[Info]; }
	int warningCount() const { return counts_[Warning]; }
	int errorCount() const { return counts_[Error]; }

	void add(Level level, const QString &text);
	Q_INVOKABLE void clear();
	/* the lines of these levels (all: empty) as "14:03:12  WARN   text", a line each: what Copy puts on the clipboard */
	Q_INVOKABLE QString text(const QVariantList &levels = {}) const;
	Q_INVOKABLE void copy(const QVariantList &levels = {}) const;
	/* tests: some line holds this text */
	bool contains(const QString &part) const;

signals:
	void countChanged();

private:
	struct Line {
		Level level = Info;
		QTime time;
		QString text;
	};
	QVector<Line> lines_;
	int counts_[3] = { 0, 0, 0 };
};

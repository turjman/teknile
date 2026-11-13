/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Log (LogModel, a list model for QML): what happened on the link, newest last, the newest MAX_LINES kept.
 * PhoneSession writes it; the Log screen (LogScreen.qml) shows each line coloured by its level, Clear empties it and
 * Copy puts it on the clipboard as text. */
#pragma once

#include <QAbstractListModel>
#include <QTime>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class LogModel : public QAbstractListModel {
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("the Log is PhoneSession's: PhoneSession.log")
	Q_PROPERTY(int count READ count NOTIFY countChanged)

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

	void add(Level level, const QString &text);
	Q_INVOKABLE void clear();
	/* every line as "14:03:12  text", a line each: what Copy puts on the clipboard */
	Q_INVOKABLE QString text() const;
	Q_INVOKABLE void copy() const;
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
};

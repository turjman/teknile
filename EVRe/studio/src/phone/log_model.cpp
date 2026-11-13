/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Log: see log_model.h. */
#include "phone/log_model.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QStringList>

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(lines_.size()); }

QVariant LogModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= lines_.size()) return {};
	const Line &line = lines_[index.row()];
	switch (role) {
	case LevelRole: return int(line.level);
	case TimeRole: return line.time.toString(QStringLiteral("HH:mm:ss"));
	case TextRole:
	case Qt::DisplayRole: return line.text;
	default: return {};
	}
}

QHash<int, QByteArray> LogModel::roleNames() const {
	return { { LevelRole, "level" }, { TimeRole, "time" }, { TextRole, "text" } };
}

/* the oldest goes first, so the list never grows past MAX_LINES */
void LogModel::add(Level level, const QString &text) {
	if (lines_.size() >= MAX_LINES) {
		beginRemoveRows(QModelIndex(), 0, 0);
		lines_.removeFirst();
		endRemoveRows();
	}
	beginInsertRows(QModelIndex(), int(lines_.size()), int(lines_.size()));
	lines_.append({ level, QTime::currentTime(), text });
	endInsertRows();
	emit countChanged();
}

void LogModel::clear() {
	if (lines_.isEmpty()) return;
	beginResetModel();
	lines_.clear();
	endResetModel();
	emit countChanged();
}

QString LogModel::text() const {
	QStringList all;
	for (const Line &line : lines_) all << line.time.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + line.text;
	return all.join(QLatin1Char('\n'));
}

void LogModel::copy() const {
	if (QClipboard *clipboard = QGuiApplication::clipboard()) clipboard->setText(text());
}

bool LogModel::contains(const QString &part) const {
	for (const Line &line : lines_)
		if (line.text.contains(part)) return true;
	return false;
}

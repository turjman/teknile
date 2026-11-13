/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Monitor: see frame_log.h. */
#include "phone/frame_log.h"

#include <algorithm>

int FrameLog::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(lines_.size()); }

QVariant FrameLog::data(const QModelIndex &index, int role) const {
	if (!index.isValid() || index.row() >= lines_.size()) return {};
	const Line &line = lines_[index.row()];
	switch (role) {
	case TimeRole: return line.time;
	case DirectionRole: return line.direction;
	case BytesRole:
	case Qt::DisplayRole: return line.bytes;
	case NoteRole: return line.note;
	default: return {};
	}
}

QHash<int, QByteArray> FrameLog::roleNames() const {
	return { { TimeRole, "time" }, { DirectionRole, "direction" }, { BytesRole, "bytes" }, { NoteRole, "note" } };
}

void FrameLog::setPaused(bool paused) {
	if (paused == paused_) return;
	paused_ = paused;
	emit pausedChanged();
}

/* IoEngine::monitorLine's "HH:mm:ss.zzz  DIR  bytes   note", or monitorText's "HH:mm:ss.zzz  text" */
FrameLog::Line FrameLog::parse(const QString &text) {
	static const QStringList directions{ QStringLiteral("TX"), QStringLiteral("RX"), QStringLiteral("DEV->"),
		QStringLiteral("->DEV") };
	Line line;
	const qsizetype afterTime = text.indexOf(QLatin1String("  "));
	if (afterTime < 0) {
		line.bytes = text;
		return line;
	}
	line.time = text.left(afterTime);
	QString rest = text.mid(afterTime + 2);
	const qsizetype afterDirection = rest.indexOf(QLatin1String("  "));
	if (afterDirection > 0 && directions.contains(rest.left(afterDirection))) {
		line.direction = rest.left(afterDirection);
		rest = rest.mid(afterDirection + 2);
	}
	const qsizetype note = rest.indexOf(QLatin1String("   "));
	line.bytes = note < 0 ? rest : rest.left(note);
	if (note >= 0) line.note = rest.mid(note + 3).trimmed();
	return line;
}

/* the oldest go first, so the list never grows past MAX_LINES; a pile too big to show is said in a line of its own (the
 * desktop's words) */
void FrameLog::add(const QStringList &lines, int dropped) {
	if (paused_ || (lines.isEmpty() && dropped <= 0)) return;
	QVector<Line> fresh;
	fresh.reserve(lines.size() + 1);
	if (dropped > 0) {
		Line note;
		note.note = tr("… %1 frames not shown (too many to show)").arg(dropped);
		fresh << note;
	}
	for (const QString &text : lines) fresh << parse(text);
	added_ += lines.size();
	if (fresh.size() > MAX_LINES) fresh.remove(0, fresh.size() - MAX_LINES);
	const qsizetype over = lines_.size() + fresh.size() - MAX_LINES;
	if (over > 0) {
		beginRemoveRows(QModelIndex(), 0, int(over) - 1);
		lines_.remove(0, over);
		endRemoveRows();
	}
	beginInsertRows(QModelIndex(), int(lines_.size()), int(lines_.size() + fresh.size()) - 1);
	lines_ += fresh;
	endInsertRows();
	emit countChanged();
}

void FrameLog::clear() {
	if (lines_.isEmpty()) return;
	beginResetModel();
	lines_.clear();
	endResetModel();
	emit countChanged();
}

QString FrameLog::line(int row) const {
	if (row < 0 || row >= lines_.size()) return QString();
	const Line &l = lines_[row];
	QStringList parts;
	for (const QString &part : { l.time, l.direction, l.bytes, l.note })
		if (!part.isEmpty()) parts << part;
	return parts.join(QLatin1String("  "));
}

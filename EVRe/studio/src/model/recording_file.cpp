/* SPDX-License-Identifier: Apache-2.0 */
/* Recordings as files: see recording_file.h. */
#include "model/recording_file.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>

namespace recording {

namespace {

constexpr int HEAD_ROWS = 200;           /* the rows estimate() measures */
constexpr qint64 TAIL_BYTES = 1 << 16;   /* where estimate() looks for the last row */
constexpr int PROGRESS_EVERY = 4096;     /* rows between two progress calls (and cancel checks) */
constexpr int VALUE_DIGITS = 9;          /* as the recording writes them (chapter 12) */

/* a row's cells: commas only, no quotes (the titles' commas are semicolons, the values have none) */
QList<QByteArray> cells(QByteArray line) {
	while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
	return line.split(',');
}

bool parseTime(const QByteArray &cell, double &t) {
	bool ok = false;
	t = cell.trimmed().toDouble(&ok);
	return ok && std::isfinite(t);
}

/* the header's value columns; false: not a recording's header */
bool readHeader(QFile &file, QStringList &titles, QString &error) {
	const QList<QByteArray> head = cells(file.readLine());
	if (head.size() < 3 || head[0].trimmed() != "time_s" || head[1].trimmed() != "datetime") {
		error = QCoreApplication::translate("recording", "not a recording: its first line is not \"time_s,datetime,...\"");
		return false;
	}
	titles.clear();
	for (int i = 2; i < head.size(); i++) titles << QString::fromUtf8(head[i]).trimmed();
	return true;
}

} // namespace

void splitTitle(const QString &title, QString &name, QString &unit) {
	static const QRegularExpression withUnit(QStringLiteral("^(.*\\S)\\s*\\[([^\\]]*)\\]$"));
	const QRegularExpressionMatch m = withUnit.match(title.trimmed());
	name = m.hasMatch() ? m.captured(1) : title.trimmed();
	unit = m.hasMatch() ? m.captured(2).trimmed() : QString();
}

QString title(const QString &name, const QString &unit) {
	QString text = unit.isEmpty() ? name : QStringLiteral("%1 [%2]").arg(name, unit);
	return text.replace(QLatin1Char(','), QLatin1Char(';'));
}

bool estimate(const QString &path, Estimate &out, QString &error) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		error = file.errorString();
		return false;
	}
	out = Estimate();
	out.bytes = file.size();
	if (!readHeader(file, out.titles, error)) return false;
	out.headerBytes = file.pos();
	qint64 measured = 0;
	int rows = 0;
	bool first = true;
	while (rows < HEAD_ROWS && !file.atEnd()) {
		const QByteArray line = file.readLine();
		measured += line.size();
		rows++;
		double t;
		if (first && parseTime(cells(line).value(0), t)) {
			out.firstTime = out.lastTime = t;
			first = false;
		}
	}
	if (rows == 0) return true; /* a header alone */
	out.rows = file.atEnd() ? rows : qint64(double(out.bytes - out.headerBytes) / (double(measured) / rows));
	/* the last whole row from the tail */
	file.seek(std::max(out.headerBytes, out.bytes - TAIL_BYTES));
	const QList<QByteArray> tail = file.readAll().split('\n');
	for (qsizetype i = tail.size() - 1; i >= 0; i--) {
		double t;
		if (parseTime(cells(tail[i]).value(0), t)) {
			out.lastTime = t;
			break;
		}
	}
	return true;
}

bool read(const QString &path, qint64 from, const std::atomic<bool> &cancel,
		const std::function<void(double)> &progress, Data &out, QString &error) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		error = file.errorString();
		return false;
	}
	QStringList titles;
	if (!readHeader(file, titles, error)) return false;
	const qint64 start = file.pos(), size = file.size();
	if (from > start) { /* the first whole row after `from` */
		file.seek(from - 1);
		if (file.read(1) != "\n") file.readLine();
	}
	const qint64 begin = file.pos();
	out = Data();
	out.columns.resize(titles.size());
	for (qsizetype c = 0; c < titles.size(); c++) splitTitle(titles[c], out.columns[c].name, out.columns[c].unit);
	QVector<char> numeric(titles.size(), 1);
	double lastTime = -std::numeric_limits<double>::infinity();
	bool epochFound = false;
	while (!file.atEnd()) {
		const QList<QByteArray> row = cells(file.readLine());
		double t;
		if (row.isEmpty() || !parseTime(row[0], t) || t < lastTime) continue; /* not a row, or going back: skipped */
		lastTime = t;
		if (!epochFound && row.size() > 1) {
			const QDateTime at = QDateTime::fromString(QString::fromLatin1(row[1].trimmed()), Qt::ISODateWithMs);
			if (at.isValid()) {
				out.epochMs = at.toMSecsSinceEpoch() - qint64(std::llround(t * 1000));
				epochFound = true;
			}
		}
		for (qsizetype c = 0; c < titles.size() && c + 2 < row.size(); c++) {
			if (!numeric[c]) continue;
			const QByteArray cell = row[c + 2].trimmed();
			if (cell.isEmpty()) continue; /* no sample */
			bool ok = false;
			const double v = cell.toDouble(&ok);
			if (!ok) { /* a byte array's hex, or text: not a line */
				numeric[c] = 0;
				out.columns[c].times = {};
				out.columns[c].values = {};
				continue;
			}
			if (!std::isfinite(v)) continue;
			out.columns[c].times << t;
			out.columns[c].values << v;
		}
		if (++out.rows % PROGRESS_EVERY == 0) {
			if (cancel.load()) return false;
			if (progress) progress(double(file.pos() - begin) / double(std::max<qint64>(1, size - begin)));
		}
	}
	/* the columns that are lines; without a datetime, the time base's zero now less the last time */
	QVector<Column> kept;
	for (qsizetype c = 0; c < titles.size(); c++) {
		if (numeric[c]) kept << std::move(out.columns[c]);
		else out.skipped++;
	}
	out.columns = std::move(kept);
	if (!epochFound) out.epochMs = QDateTime::currentMSecsSinceEpoch() - qint64(std::llround(std::max(0.0, lastTime) * 1000));
	return true;
}

bool write(const QString &path, const QVector<Line> &lines, qint64 epochMs, const std::atomic<bool> &cancel,
		const std::function<void(double)> &progress, qint64 &rows, QString &error) {
	rows = 0;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		error = file.errorString();
		return false;
	}
	QTextStream out(&file);
	out << "time_s,datetime";
	for (const Line &line : lines) out << ',' << title(line.name, line.unit);
	out << '\n';
	/* each line's tolerance: a quarter of its own median interval (exact times always share) */
	const qsizetype n = lines.size();
	QVector<double> tolerance(n, 0);
	qint64 total = 0;
	for (qsizetype i = 0; i < n; i++) {
		const QVector<double> &t = lines[i].times;
		total += t.size();
		if (t.size() < 2) continue;
		QVector<double> gaps(t.size() - 1);
		for (qsizetype k = 1; k < t.size(); k++) gaps[k - 1] = t[k] - t[k - 1];
		std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
		tolerance[i] = ROW_SHARE * gaps[gaps.size() / 2];
	}
	QVector<qsizetype> next(n, 0);
	QStringList row(n);
	qint64 written = 0;
	for (;;) {
		double t = std::numeric_limits<double>::infinity();
		for (qsizetype i = 0; i < n; i++)
			if (next[i] < lines[i].times.size()) t = std::min(t, lines[i].times[next[i]]);
		if (!std::isfinite(t)) break;
		out << QString::number(t, 'f', 6) << ','
			<< QDateTime::fromMSecsSinceEpoch(epochMs + qint64(std::llround(t * 1000))).toString(Qt::ISODateWithMs);
		for (qsizetype i = 0; i < n; i++) {
			out << ',';
			const Line &line = lines[i];
			if (next[i] >= line.times.size() || line.times[next[i]] > t + tolerance[i]) continue;
			out << QString::number(line.values[next[i]], 'g', VALUE_DIGITS);
			next[i]++;
			written++;
		}
		out << '\n';
		if (++rows % PROGRESS_EVERY == 0) {
			if (cancel.load()) {
				file.close();
				file.remove();
				return false;
			}
			if (progress) progress(double(written) / double(std::max<qint64>(1, total)));
		}
	}
	out.flush();
	if (out.status() != QTextStream::Ok || file.error() != QFileDevice::NoError) {
		error = file.errorString();
		file.close();
		file.remove();
		return false;
	}
	return true;
}

QString notesPath(const QString &recordingPath) { return recordingPath + QStringLiteral(".notes.json"); }

bool loadNotes(const QString &recordingPath, QVector<ChartNote> &notes, QString &error) {
	notes.clear();
	QFile file(notesPath(recordingPath));
	if (!file.exists()) return false;
	if (!file.open(QIODevice::ReadOnly)) {
		error = file.errorString();
		return false;
	}
	QJsonParseError parse;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
	if (!doc.isObject() || doc.object().value(QStringLiteral("format")).toString() != QLatin1String("evre-notes/1")) {
		error = parse.error != QJsonParseError::NoError ? parse.errorString()
				: QCoreApplication::translate("recording", "not a notes file (\"format\": \"evre-notes/1\")");
		return false;
	}
	for (const QJsonValue &value : doc.object().value(QStringLiteral("notes")).toArray()) {
		const QJsonObject note = value.toObject();
		if (!note.value(QStringLiteral("time_s")).isDouble()) continue;
		notes << ChartNote{ note.value(QStringLiteral("time_s")).toDouble(), note.value(QStringLiteral("text")).toString() };
	}
	return true;
}

bool saveNotes(const QString &recordingPath, const QVector<ChartNote> &notes, qint64 epochMs, QString &error) {
	const QString path = notesPath(recordingPath);
	if (notes.isEmpty()) {
		if (QFile::exists(path) && !QFile::remove(path)) {
			error = QCoreApplication::translate("recording", "cannot remove %1").arg(path);
			return false;
		}
		return true;
	}
	QJsonArray list;
	for (const ChartNote &note : notes) {
		list << QJsonObject{ { QStringLiteral("time_s"), note.time },
			{ QStringLiteral("datetime"),
					QDateTime::fromMSecsSinceEpoch(epochMs + qint64(std::llround(note.time * 1000))).toString(Qt::ISODateWithMs) },
			{ QStringLiteral("text"), note.text } };
	}
	const QJsonObject doc{ { QStringLiteral("format"), QStringLiteral("evre-notes/1") }, { QStringLiteral("notes"), list } };
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(doc).toJson()) < 0 || !file.commit()) {
		error = file.errorString();
		return false;
	}
	return true;
}

} // namespace recording

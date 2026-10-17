/* SPDX-License-Identifier: Apache-2.0 */
/* A fast stream's recording, the writer (see fast_recording.h). */
#include "model/fast_recording.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstring>

#include "io/fast_stream.h"
#include "model/fast_store.h"

namespace fast {

namespace {

void put32(QByteArray &bytes, quint32 value) {
	for (int i = 0; i < 4; i++) bytes.append(char(value >> (8 * i)));
}

void put64(QByteArray &bytes, quint64 value) {
	for (int i = 0; i < 8; i++) bytes.append(char(value >> (8 * i)));
}

} // namespace

RecordingWriter::RecordingWriter() = default;
RecordingWriter::~RecordingWriter() { close(); }

bool RecordingWriter::open(const QString &file, const QString &device, const StreamDef &stream, const QDateTime &start,
		QString &err, double startClock) {
	close();
	file_ = std::make_unique<QFile>(file);
	if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		err = file_->errorString();
		file_.reset();
		return false;
	}
	bytes_ = 0;
	/* the stream's object as the map writes it, inside the head */
	const QJsonObject streamObject = QJsonDocument::fromJson(streamToJson(stream)).object();
	const QJsonObject head{ { QStringLiteral("format"), QLatin1String(RECORDING_FORMAT) },
		{ QStringLiteral("device"), device }, { QStringLiteral("stream"), streamObject },
		{ QStringLiteral("start"), start.toString(Qt::ISODateWithMs) }, { QStringLiteral("start_s"), startClock } };
	if (!piece("EVRS", QJsonDocument(head).toJson(QJsonDocument::Compact))) {
		err = file_->errorString();
		return false;
	}
	return true;
}

bool RecordingWriter::isOpen() const { return file_ && file_->isOpen(); }

bool RecordingWriter::mark(quint64 record, double time) {
	QByteArray body;
	put64(body, record);
	quint64 bits;
	std::memcpy(&bits, &time, 8);
	put64(body, bits);
	return piece("TIME", body);
}

bool RecordingWriter::block(const QByteArray &data) { return piece("BLK ", data); }

bool RecordingWriter::piece(const char name[4], const QByteArray &body) {
	if (!isOpen()) return false;
	QByteArray head(name, 4);
	put32(head, quint32(body.size()));
	if (file_->write(head) != head.size() || file_->write(body) != body.size()) return false;
	bytes_ += head.size() + body.size();
	return true;
}

bool RecordingWriter::flush() { return isOpen() && file_->flush(); }

void RecordingWriter::close() {
	if (file_) file_->close();
	file_.reset();
}

QString RecordingWriter::errorString() const { return file_ ? file_->errorString() : QString(); }

QString recordingFileFor(const QString &csv, const QString &stream) {
	const QFileInfo info(csv);
	const QString base = info.suffix().compare(QLatin1String("csv"), Qt::CaseInsensitive) == 0
			? info.completeBaseName() : info.fileName();
	return info.dir().filePath(base + QLatin1Char('.') + stream + QStringLiteral(".evrs"));
}

QStringList recordingsBeside(const QString &csv) {
	const QFileInfo info(csv);
	const QString base = info.completeBaseName();
	QStringList out;
	for (const QString &name : info.dir().entryList({ base + QStringLiteral(".*.evrs") }, QDir::Files, QDir::Name))
		out << info.dir().filePath(name);
	return out;
}

/* ------------------------------------------------------------------ reading */

namespace {

quint32 le32(const uchar *b) { return quint32(b[0]) | quint32(b[1]) << 8 | quint32(b[2]) << 16 | quint32(b[3]) << 24; }
quint64 le64(const uchar *b) { return quint64(le32(b)) | quint64(le32(b + 4)) << 32; }

struct Piece {
	char name[4];
	qint64 at = 0;   /* its body's place in the file */
	qint64 size = 0;
};

} // namespace

/* Two passes over the pieces: their places (and where the file is cut), then the blocks into the store. A TIME piece
 * says the time of a record "from there on"; its period comes from the next mark of the same start (a block with
 * START, or with a number that went back, between them begins another), else the one before, else the map's rate.
 * A mark is laid after the block that follows it, as the live chart lays a block's mark after its records: the TIME
 * before a start's first block belongs to that start. */
bool readRecording(const QString &path, const std::atomic<bool> &cancel, const std::function<void(double)> &progress,
		Recording &out, QString &error) {
	out = Recording();
	out.file = path;
	auto file = std::make_shared<QFile>(path);
	if (!file->open(QIODevice::ReadOnly)) {
		error = file->errorString();
		return false;
	}
	out.bytes = file->size();
	const uchar *data = out.bytes > 0 ? file->map(0, out.bytes) : nullptr;
	if (!data) {
		error = out.bytes > 0 ? file->errorString() : QCoreApplication::translate("fast::Recording", "the file is empty");
		return false;
	}
	QVector<Piece> pieces;
	qint64 at = 0;
	while (at < out.bytes) {
		if (at + 8 > out.bytes) {
			out.cut = true;
			break;
		}
		Piece piece;
		std::memcpy(piece.name, data + at, 4);
		piece.size = le32(data + at + 4);
		piece.at = at + 8;
		if (piece.at + piece.size > out.bytes) {
			out.cut = true;
			break;
		}
		pieces << piece;
		at = piece.at + piece.size;
	}
	out.bytesRead = at;
	if (pieces.isEmpty() || std::memcmp(pieces.first().name, "EVRS", 4) != 0) {
		error = QCoreApplication::translate("fast::Recording", "not a fast stream's recording: it does not begin with its head (EVRS)");
		return false;
	}
	const QJsonObject head = QJsonDocument::fromJson(QByteArray::fromRawData(reinterpret_cast<const char *>(data)
			+ pieces.first().at, int(pieces.first().size))).object();
	if (head.value(QStringLiteral("format")).toString() != QLatin1String(RECORDING_FORMAT)) {
		error = QCoreApplication::translate("fast::Recording", "not a recording of the format %1").arg(QLatin1String(RECORDING_FORMAT));
		return false;
	}
	QString why;
	if (!streamFromJson(QJsonDocument(head.value(QStringLiteral("stream")).toObject()).toJson(QJsonDocument::Compact),
				out.stream, why)) {
		error = QCoreApplication::translate("fast::Recording", "its stream cannot be read: %1").arg(why);
		return false;
	}
	out.device = head.value(QStringLiteral("device")).toString();
	out.start = QDateTime::fromString(head.value(QStringLiteral("start")).toString(), Qt::ISODateWithMs);
	out.startClock = head.value(QStringLiteral("start_s")).toDouble(0);

	/* the marks' periods: from the next mark of the same start */
	struct MarkAt {
		quint64 record = 0;
		double time = 0, period = 0;
	};
	QVector<MarkAt> marks;
	QVector<int> startsBefore; /* per mark: a start begins between it and the next */
	bool startSince = false;
	quint32 lastFirst = 0;
	bool anyBlock = false;
	int blocksSinceMark = 0;
	for (const Piece &piece : std::as_const(pieces)) {
		if (std::memcmp(piece.name, "TIME", 4) == 0 && piece.size >= 16) {
			if (!marks.isEmpty()) startsBefore << int(startSince);
			startSince = false;
			blocksSinceMark = 0;
			const quint64 bits = le64(data + piece.at + 8);
			double time;
			std::memcpy(&time, &bits, 8);
			marks << MarkAt{ le64(data + piece.at), time, 0 };
		} else if (std::memcmp(piece.name, "BLK ", 4) == 0 && piece.size >= HEADER) {
			const quint32 first = le32(data + piece.at);
			/* a start's first block right after its mark is that mark's own start */
			if (!marks.isEmpty() && blocksSinceMark > 0 && ((data[piece.at + 6] & FLAG_START) || (anyBlock && first < lastFirst)))
				startSince = true;
			lastFirst = first;
			anyBlock = true;
			blocksSinceMark++;
		}
	}
	startsBefore << 1;
	const double nominal = out.stream.rate > 0 ? 1.0 / out.stream.rate : 0;
	for (qsizetype k = 0; k < marks.size(); k++) {
		const bool next = k + 1 < marks.size() && !startsBefore[k] && marks[k + 1].record > marks[k].record
				&& marks[k + 1].time > marks[k].time;
		if (next) marks[k].period = (marks[k + 1].time - marks[k].time) / double(marks[k + 1].record - marks[k].record);
		else marks[k].period = k > 0 && !startsBefore[k - 1] && marks[k - 1].period > 0 ? marks[k - 1].period : nominal;
	}

	auto store = std::make_shared<Store>(out.stream);
	store->keep(file);
	FastStream state;
	state.reset(out.stream);
	QVector<MarkAt> pending;
	qsizetype mark = 0;
	const auto lay = [&] {
		for (const MarkAt &m : std::as_const(pending)) store->mark(m.record, m.time, m.period);
		pending.clear();
	};
	for (qsizetype i = 1; i < pieces.size(); i++) {
		if (cancel.load()) {
			error.clear();
			return false;
		}
		if (progress && i % 4096 == 0) progress(double(pieces[i].at) / double(std::max<qint64>(1, out.bytesRead)));
		const Piece &piece = pieces[i];
		if (std::memcmp(piece.name, "TIME", 4) == 0 && piece.size >= 16) {
			if (mark < marks.size()) pending << marks[mark++];
		} else if (std::memcmp(piece.name, "BLK ", 4) == 0) {
			const QByteArray block = QByteArray::fromRawData(reinterpret_cast<const char *>(data) + piece.at, int(piece.size));
			BlockTaken taken;
			if (state.take(block, 0, taken) != BlockCheck::Ok) {
				out.badBlocks++;
				continue;
			}
			store->appendMapped(taken.first, taken.count, block.constData() + HEADER, taken.newStart, taken.lost);
			out.blocks++;
			out.lost += taken.lost;
			lay();
		}
	}
	lay();
	if (store->size() > 0 && store->hasTime()) {
		out.firstTime = store->timeAt(0);
		out.lastTime = store->timeAt(store->size() - 1);
	}
	out.store = store;
	if (progress) progress(1);
	return true;
}

} // namespace fast

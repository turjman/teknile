/* SPDX-License-Identifier: Apache-2.0 */
/* A fast stream's recording, the writer (see fast_recording.h). */
#include "model/fast_recording.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

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
		QString &err) {
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
		{ QStringLiteral("start"), start.toString(Qt::ISODateWithMs) } };
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

} // namespace fast

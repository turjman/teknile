/* SPDX-License-Identifier: Apache-2.0 */
/* The phone's Monitor: see frame_log.h. */
#include "phone/frame_log.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QVariantMap>

#include <algorithm>

#include "evre/frame.h"
#include "model/device_map.h"

namespace {

constexpr int FRAME_HEAD = 7; /* 7B SLAVE FN OFF(2) CNT(2): the data, the CRC and the end byte follow */

/* the desktop's words for a frame's fields (DumpView's), its translations with them */
QString dumpWords(const char *text) { return QCoreApplication::translate("DumpView", text); }

/* a function's kind, for its label's colour: a read asked, a write, an answer, an error */
QString kindOf(uint8_t fn) {
	switch (fn) {
	case evre::READ: return QStringLiteral("read");
	case evre::WRITE:
	case evre::WRITE_ACK: return QStringLiteral("write");
	case evre::READ_RESP:
	case evre::WRITE_ACK_RESP: return QStringLiteral("answer");
	case evre::ERROR_RESP: return QStringLiteral("error");
	default: return QString();
	}
}

QString byteText(char byte) { return QStringLiteral("%1").arg(uchar(byte), 2, 16, QLatin1Char('0')).toUpper(); }

} // namespace

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
	case SideRole: return line.side;
	case TypeRole: return line.type;
	case KindRole: return line.kind;
	default: return {};
	}
}

QHash<int, QByteArray> FrameLog::roleNames() const {
	return { { TimeRole, "time" }, { DirectionRole, "direction" }, { BytesRole, "bytes" }, { NoteRole, "note" },
		{ SideRole, "side" }, { TypeRole, "type" }, { KindRole, "kind" } };
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
	if (line.direction.isEmpty()) return line;
	line.side = line.direction == QLatin1String("TX") || line.direction == QLatin1String("->DEV") ? Sent : Received;
	/* the function's byte, the third (a frame cut short or not EVRe has none) */
	const QByteArray raw = QByteArray::fromHex(line.bytes.left(9).toLatin1());
	if (raw.size() >= 3 && uchar(raw.at(0)) == 0x7B) {
		line.type = evre::fnName(uint8_t(raw.at(2)));
		line.kind = kindOf(uint8_t(raw.at(2)));
	}
	/* the note that only names the function again (the engine's RX "READ_RESP") goes: the label says it */
	if (line.note == line.type) line.note.clear();
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
		for (qsizetype i = 0; i < over; ++i) --sides_[lines_[i].side];
		lines_.remove(0, over);
		endRemoveRows();
	}
	beginInsertRows(QModelIndex(), int(lines_.size()), int(lines_.size() + fresh.size()) - 1);
	for (const Line &line : std::as_const(fresh)) ++sides_[line.side];
	lines_ += fresh;
	endInsertRows();
	emit countChanged();
}

void FrameLog::clear() {
	if (lines_.isEmpty()) return;
	beginResetModel();
	lines_.clear();
	sides_[Sent] = sides_[Received] = sides_[NoteSide] = 0;
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

/* The fields in the wire's order, with DumpView's names, meanings and words (its tooltips; on the phone a line under
 * each field), parsed by the engine's own parser: a frame it would drop (a wrong CRC, cut short) is not spelled out */
QVariantList FrameLog::decode(const QString &bytes, const QString &direction) const {
	const QByteArray raw = QByteArray::fromHex(bytes.toLatin1());
	evre::Parser parser;
	parser.feed(raw);
	evre::Frame frame;
	if (!parser.next(frame) || frame.raw.size() != raw.size() || raw.size() < FRAME_HEAD + 3) return {};
	const bool request = direction == QLatin1String("TX") || direction == QLatin1String("->DEV");
	const uint8_t fn = frame.fn;
	const int data = int(raw.size()) - FRAME_HEAD - 3;
	const auto hexOf = [&raw](int from, int count) { return evre::hex(raw.mid(from, count)); };
	QVariantList rows;
	const auto add = [&rows](const char *name, const QString &fieldBytes, const QString &meaning, const QString &words) {
		rows << QVariantMap{ { QStringLiteral("name"), QLatin1String(name) }, { QStringLiteral("bytes"), fieldBytes },
			{ QStringLiteral("meaning"), meaning }, { QStringLiteral("words"), words } };
	};
	add("START", hexOf(0, 1), QString(), dumpWords("Start byte 0x7B (\"{\"): a frame is recognisable in a terminal dump"));
	add("SLAVE", hexOf(1, 1),
			frame.slave == evre::BROADCAST ? QStringLiteral("0 ") + dumpWords("all") : QString::number(frame.slave),
			dumpWords("Slave ID %1; 0 is the broadcast, every device takes a WRITE to it").arg(frame.slave));
	QString words = evre::fnName(fn);
	if (fn == evre::READ) words = dumpWords("READ, answered with READ_RESP");
	else if (fn == evre::READ_RESP) words = dumpWords("READ_RESP, the answer to a READ: the bytes asked for");
	else if (fn == evre::ERROR_RESP) words = dumpWords("ERROR_RESP, the answer to a request that failed: one error code");
	else if (fn == evre::WRITE_ACK) words = dumpWords("WRITE_ACK, a write answered with WRITE_ACK_RESP");
	else if (fn == evre::WRITE) words = dumpWords("WRITE, a write that is never answered");
	else if (fn == evre::WRITE_ACK_RESP) words = dumpWords("WRITE_ACK_RESP, the answer to a WRITE_ACK: the write landed");
	add("FUNCTION", hexOf(2, 1), evre::fnName(fn), dumpWords("Function code 0x%1: %2").arg(byteText(char(fn)), words));
	/* the registers the frame covers, from the map */
	const QString names = namer_ ? namer_(frame.slave, frame.addr, std::max<int>(1, frame.cnt)) : QString();
	add("OFFSET", hexOf(3, 2), names.isEmpty() ? addrText(frame.addr) : addrText(frame.addr) + QStringLiteral(" · ") + names,
			dumpWords("Register offset, little endian: %1 is %2")
					.arg(QStringLiteral("0x%1 0x%2").arg(byteText(raw.at(3)), byteText(raw.at(4))), addrText(frame.addr)));
	add("COUNT", hexOf(5, 2), QString::number(frame.cnt),
			dumpWords("Number of registers, little endian: %1 (a register is one byte: the count is in bytes)").arg(frame.cnt));
	QString meaning, dataWords;
	if (fn == evre::ERROR_RESP && data > 0) {
		const uint8_t code = uint8_t(raw.at(FRAME_HEAD));
		meaning = dumpWords("ERROR %1 %2").arg(code).arg(evre::errorName(code));
		dataWords = dumpWords("Data: the error code %1, %2").arg(code).arg(evre::errorName(code));
	} else if (data == 0) {
		meaning = dumpWords("none");
		dataWords = fn == evre::WRITE_ACK_RESP ? dumpWords("Data: none, the answer carries no status")
											   : dumpWords("Data: none, a READ carries no data");
	} else {
		meaning = dumpWords("payload");
		dataWords = fn == evre::WRITE || fn == evre::WRITE_ACK ? tr("Data: the %n bytes written", nullptr, data)
															   : tr("Data: the %n bytes read (the payload)", nullptr, data);
	}
	add("DATA", data > 0 ? hexOf(FRAME_HEAD, data) : QString(), meaning, dataWords);
	const QString crc = QStringLiteral("0x")
			+ QStringLiteral("%1").arg(evre::littleEndian16(raw, int(raw.size()) - 3), 4, 16, QLatin1Char('0')).toUpper();
	add("CRC", hexOf(int(raw.size()) - 3, 2), request ? QString() : dumpWords("OK"),
			request ? dumpWords("CRC-16/X-25 over every byte before it, low byte first: %1; the Studio computed it").arg(crc)
					: tr("CRC-16/X-25 over every byte before it, low byte first: %1; checked when the frame came (a frame "
						 "whose CRC fails is dropped)").arg(crc));
	add("END", hexOf(int(raw.size()) - 1, 1), QString(), dumpWords("End byte 0x7D (\"}\")"));
	return rows;
}

void FrameLog::copyText(const QString &text) const {
	if (QClipboard *clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}

/* SPDX-License-Identifier: Apache-2.0 */
/* EVRe frame encoding and decoding, and the frame CRC (layout in frame.h). */
#include "evre/frame.h"

namespace evre {

namespace {

constexpr uint8_t START_BYTE = 0x7B;
constexpr uint8_t END_BYTE = 0x7D;
constexpr int HEADER_SIZE = 7;               /* 7B SLAVE FN OFF(2) CNT(2) */
constexpr int TRAILER_SIZE = 3;              /* CRC(2) 7D */
constexpr uint16_t CRC_POLYNOMIAL = 0x8408;  /* 0x1021, bit-reversed */

void appendLe16(QByteArray &out, uint16_t value) {
	out.append(char(value & 0xFF));
	out.append(char(value >> 8));
}

/* The DATA bytes between header and CRC for a frame with this function code,
 * or -1 when the byte is no function code: then the 7B before it started no
 * frame. Requests seen on a shared line are sized the same way. */
int dataLength(uint8_t fn, uint16_t cnt) {
	switch (fn) {
	case READ_RESP:
	case WRITE:
	case WRITE_ACK: return cnt;
	case ERROR_RESP: return 1;
	case READ:
	case WRITE_ACK_RESP: return 0;
	default: return -1;
	}
}

} // namespace

uint16_t crc16(const uint8_t *bytes, int length) {
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < length; i++) {
		crc ^= bytes[i];
		for (int bit = 0; bit < 8; bit++) crc = (crc & 1) ? uint16_t((crc >> 1) ^ CRC_POLYNOMIAL) : uint16_t(crc >> 1);
	}
	return uint16_t(crc ^ 0xFFFF);
}

QByteArray build(uint8_t slave, uint8_t fn, uint16_t addr, uint16_t cnt, const QByteArray &data) {
	QByteArray frame;
	frame.reserve(HEADER_SIZE + data.size() + TRAILER_SIZE);
	frame.append(char(START_BYTE));
	frame.append(char(slave));
	frame.append(char(fn));
	appendLe16(frame, addr);
	appendLe16(frame, cnt);
	frame.append(data);
	appendLe16(frame, crc16(reinterpret_cast<const uint8_t *>(frame.constData()), int(frame.size())));
	frame.append(char(END_BYTE));
	return frame;
}

bool Parser::next(Frame &out) {
	for (;;) {
		const int start = int(buffer_.indexOf(char(START_BYTE)));
		if (start < 0) {
			buffer_.clear(); /* no start byte: none of it can become a frame */
			return false;
		}
		if (start > 0) buffer_.remove(0, start);
		if (buffer_.size() < HEADER_SIZE) return false;

		const auto *bytes = reinterpret_cast<const uint8_t *>(buffer_.constData());
		const uint16_t cnt = littleEndian16(bytes + 5);
		const int length = dataLength(bytes[2], cnt);
		if (length < 0) {
			buffer_.remove(0, 1); /* no function code: that 7B was data */
			continue;
		}
		const int total = HEADER_SIZE + length + TRAILER_SIZE;
		if (buffer_.size() < total) return false;

		const uint16_t crc = crc16(bytes, total - TRAILER_SIZE);
		if (littleEndian16(bytes + total - TRAILER_SIZE) != crc || bytes[total - 1] != END_BYTE) {
			badFrames_++;
			buffer_.remove(0, 1); /* not a frame after all: the next 7B may be */
			continue;
		}
		out.slave = bytes[1];
		out.fn = bytes[2];
		out.addr = littleEndian16(bytes + 3);
		out.cnt = cnt;
		out.data = buffer_.mid(HEADER_SIZE, length);
		out.raw = buffer_.left(total);
		buffer_.remove(0, total);
		return true;
	}
}

QString errorName(uint8_t code) {
	switch (code) {
	case 0: return QStringLiteral("no error");
	case 1: return QStringLiteral("invalid packet");
	case 2: return QStringLiteral("unknown function code");
	case 3: return QStringLiteral("permission denied");
	case 4: return QStringLiteral("offset out of range");
	case 5: return QStringLiteral("count out of range");
	case 12: return QStringLiteral("length mismatch");
	default: return QStringLiteral("error %1").arg(code);
	}
}

QString fnName(uint8_t fn) {
	switch (fn) {
	case READ: return QStringLiteral("READ");
	case READ_RESP: return QStringLiteral("READ_RESP");
	case WRITE: return QStringLiteral("WRITE");
	case WRITE_ACK: return QStringLiteral("WRITE_ACK");
	case WRITE_ACK_RESP: return QStringLiteral("WRITE_ACK_RESP");
	case ERROR_RESP: return QStringLiteral("ERROR_RESP");
	default: return QStringLiteral("0x%1").arg(fn, 2, 16, QLatin1Char('0'));
	}
}

QString hex(const QByteArray &bytes) {
	return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

} // namespace evre

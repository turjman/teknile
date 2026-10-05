/* SPDX-License-Identifier: Apache-2.0 */
/* EVRe frames: build requests, find answers in a byte stream.
 *
 *   7B SLAVE FN OFF(2) CNT(2) DATA CRC(2) 7D     little endian
 *
 * FN is the function code, OFF the register offset, CNT a byte count. How much
 * DATA a frame carries follows from FN: a READ none (CNT is what it asks for),
 * its answer CNT bytes, a write CNT bytes, a write's acknowledge none, an error
 * answer one error code.
 * CRC-16/X-25 (reflected 0x1021, init 0xFFFF, final XOR 0xFFFF) over
 * everything before the CRC. Check value of "123456789": 0x906E.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace evre {

/* The slave address every device hears and none answers: a broadcast, for a WRITE only. */
constexpr uint8_t BROADCAST = 0;

/* The function codes. */
enum Fn : uint8_t {
	READ = 0xAA,           /* read CNT bytes at OFF */
	READ_RESP = 0xAB,      /* the answer to READ: the CNT bytes */
	WRITE = 0xEA,          /* write CNT bytes at OFF, no answer */
	WRITE_ACK = 0xEB,      /* write CNT bytes at OFF, answered */
	WRITE_ACK_RESP = 0xEC, /* the answer to WRITE_ACK: written */
	ERROR_RESP = 0xEE,     /* the answer to any request that failed: one error code */
};

uint16_t crc16(const uint8_t *bytes, int length);

/* EVRe's byte order, low byte first: the number in the bytes at `bytes` */
inline uint16_t littleEndian16(const uint8_t *bytes) { return uint16_t(bytes[0] | (bytes[1] << 8)); }
inline uint16_t littleEndian16(const QByteArray &bytes, int at) {
	return littleEndian16(reinterpret_cast<const uint8_t *>(bytes.constData()) + at);
}
inline uint32_t littleEndian32(const uint8_t *bytes) {
	return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

/* One whole frame, CRC and end byte included. */
QByteArray build(uint8_t slave, uint8_t fn, uint16_t addr, uint16_t cnt, const QByteArray &data = {});

/* One frame as the parser found it: its header fields, its data, and all its bytes. */
struct Frame {
	uint8_t slave = 0;
	uint8_t fn = 0;
	uint16_t addr = 0;
	uint16_t cnt = 0;
	QByteArray data;  /* READ_RESP: the values; ERROR_RESP: one error code */
	QByteArray raw;   /* the whole frame, for the monitor */
};

/* Answers out of a stream: start byte, header, length from the function
 * code, end byte, CRC. Anything else is skipped byte by byte, so noise and
 * half frames cost nothing but the bytes themselves. */
class Parser {
public:
	void feed(const QByteArray &bytes) { buffer_.append(bytes); }
	/* the next whole frame fed so far; false until one is complete */
	bool next(Frame &out);
	void clear() { buffer_.clear(); }
	/* frames dropped for a wrong CRC or end byte, since the parser was made */
	int badFrames() const { return badFrames_; }

private:
	QByteArray buffer_;
	int badFrames_ = 0;
};

/* Words for the log and the frame monitor. */
QString errorName(uint8_t code);
QString fnName(uint8_t fn);
QString hex(const QByteArray &bytes);  /* "7B 01 AA ..." */

} // namespace evre

/* SPDX-License-Identifier: Apache-2.0 */
/*
 * lib_test.cpp: the EVRe library on a PC, built by run_lib_tests.py.
 *
 *   -DTRANSCRIPT            thousands of frames (every function code, offsets
 *                           around each boundary, counts up to 300 and three
 *                           past 0x2000, slaves 1, 0 and 2, bad CRCs, wrong
 *                           lengths) and the encoder: one
 *                           line per frame with the answer and the memory after
 *                           it. Built for the 1.0 library with its pointer
 *                           table, and for 1.1 with the pointer table and with
 *                           ranges, each also as a mirror and with broadcasts
 *                           into the device bank, and as a mirror with
 *                           broadcasts. All must be the same, byte for byte,
 *                           but for the frames 1.1 changes on purpose, which
 *                           run_lib_tests.py names and checks.
 *   -DUSE_RANGES            (with TRANSCRIPT) the bank as two ranges
 *   -DMIRROR                (with TRANSCRIPT) a host's mirror: ACCEPT_READ_RESP 1
 *   -DBROADCAST_D000        (with TRANSCRIPT) ACCEPT_BROADCAST_D000 1: the
 *                           device bank takes broadcasts, as in 1.0, and
 *                           STATUS says so (D-24); with -DMIRROR too, STATUS
 *                           goes out as the mirror holds it, as in 1.0 (D-25)
 *   -DFEATURES              1.1 only: ranges, the handlers, EVRe Guard, a frame
 *                           for each order of steps the transcript cannot tell
 *                           apart, and one or more for each decision of stage 2
 *                           and after ("D-n:"). PASS/FAIL lines. With -DWRAP_CALLOC (and
 *                           -Wl,--wrap=calloc) it sees decodePacket's
 *                           allocations; with EVRE_CRC_TABLE_RUNTIME=1 and
 *                           -include lock_hooks.h it checks the RAM table and
 *                           the lock hooks too.
 */

#include <cstdio>
#include <cstring>

#include "EVRe.h"
#ifdef FEATURES
#include "evre_guard.h"
#endif

static int failed = 0;

static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failed++;
	}
}

/* a frame as a host sends it */
static uint16_t frame(uint8_t *out, uint8_t slave, uint8_t fn, uint16_t off, uint16_t cnt, const uint8_t *data, uint16_t len) {
	uint16_t n = 0;
	out[n++] = 0x7B;
	out[n++] = slave;
	out[n++] = fn;
	out[n++] = uint8_t(off);
	out[n++] = uint8_t(off >> 8);
	out[n++] = uint8_t(cnt);
	out[n++] = uint8_t(cnt >> 8);
	if (len) {
		std::memcpy(out + n, data, len);
	}
	n = uint16_t(n + len);
	const uint16_t crc = GetCrc16(out, n);
	out[n++] = uint8_t(crc);
	out[n++] = uint8_t(crc >> 8);
	out[n++] = 0x7D;
	return n;
}

static bool hasData(uint8_t fn) {
	return fn == WRITE || fn == WRITE_ACK || fn == READ_RESP;
}

/* ================================================================ TRANSCRIPT */
#ifdef TRANSCRIPT

static uint8_t ro[220];
static uint8_t rw[35];
static base_t dev;
static uint16_t status; /* as protocolInit left it */

static void resetMemory() {
	dev.DEVICE_ID = 0x2001; /* a READ_RESP to the reserved bank could change them (F1) */
	dev.STATUS = status;
	for (unsigned i = 0; i < sizeof ro; ++i) ro[i] = uint8_t(i * 3 + 1);
	for (unsigned i = 0; i < sizeof rw; ++i) rw[i] = uint8_t(0x80 + i * 5);
	dev.CONFIG = 0;
	dev.MSG_CNT = 3;
	std::memset(dev.MSG_BUFFER, 0, sizeof dev.MSG_BUFFER);
	dev.MSG_BUFFER[0] = 5;
	dev.MSG_BUFFER[1] = 6;
	dev.MSG_BUFFER[2] = 7;
}

/* FNV-1a over everything a frame can change but STATUS (m), and over all of
 * that but CONFIG and the head of the queue (r). A line shows those three as
 * they are (s, c, q): so the classifier in run_lib_tests.py can check a change
 * of them exactly. */
static const unsigned QUEUE_SHOWN = 8;

static uint32_t hash(bool all) {
	uint32_t h = 2166136261u;
	auto mix = [&h](const void *p, unsigned n) {
		const uint8_t *b = static_cast<const uint8_t *>(p);
		for (unsigned i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
	};
	mix(ro, sizeof ro);
	mix(rw, sizeof rw);
	mix(&dev.DEVICE_ID, sizeof dev.DEVICE_ID);
	if (all) {
		mix(&dev.CONFIG, sizeof dev.CONFIG);
		mix(&dev.MSG_CNT, sizeof dev.MSG_CNT);
		mix(dev.MSG_BUFFER, sizeof dev.MSG_BUFFER);
	} else {
		mix(dev.MSG_BUFFER + QUEUE_SHOWN, sizeof dev.MSG_BUFFER - QUEUE_SHOWN);
	}
	return h;
}

static void printAnswer(const char *what, unsigned a, unsigned b, unsigned c, unsigned d, uint8_t ret, const uint8_t *out, uint16_t len) {
	std::printf("%s %02X %04X %u %u -> %u %u:", what, a, b, c, d, ret, len);
	for (uint16_t i = 0; i < len; ++i) std::printf("%02X", out[i]);
	std::printf(" m%08X c%04X q%u:", hash(true), dev.CONFIG, dev.MSG_CNT);
	for (unsigned i = 0; i < QUEUE_SHOWN; ++i) std::printf("%02X", dev.MSG_BUFFER[i]);
	std::printf(" r%08X s%04X\n", hash(false), dev.STATUS);
}

int main() {
	dev.SALVE_ID_REG = 1;
	dev.DEVICE_ID = 0x2001;
	if (protocolInit(&dev) != NO_ERROR) return 2;
	status = dev.STATUS;
	dev.DEVICE_REG_READ_MAX = 0xD0FE;
	dev.DEVICE_REG_WRITE_MIN = 0xD0DC;
#ifdef MIRROR
	dev.ACCEPT_READ_RESP = 1; /* a host's mirror: READ_RESP stored as in 1.0 */
#endif
#ifdef BROADCAST_D000
	dev.ACCEPT_BROADCAST_D000 = 1; /* broadcasts into the device bank taken as in 1.0 */
#endif
#ifdef USE_RANGES
	static const evre_range_t ranges[] = { { 0xD000, 220, ro, 0 }, { 0xD0DC, 35, rw, 1 } };
	dev.D_RANGES = ranges;
	dev.D_RANGE_CNT = 2;
#else
	dev.D000 = new uint8_t *[255];
	for (unsigned i = 0; i < 220; ++i) dev.D000[i] = &ro[i];
	for (unsigned i = 0; i < 35; ++i) dev.D000[220 + i] = &rw[i];
#endif

	const uint8_t fns[] = { READ, READ_RESP, WRITE, WRITE_ACK, WRITE_ACK_RESP, ERROR_RESP, 0x55 };
	const uint16_t offs[] = { 0x0000, 0x9FFF, 0xA000, 0xA002, 0xA004, 0xA006, 0xA007, 0xA0FF, 0xA105, 0xA106, 0xCFFF,
		0xD000, 0xD001, 0xD0DA, 0xD0DB, 0xD0DC, 0xD0DD, 0xD0FD, 0xD0FE, 0xD0FF, 0xD100, 0xDFFF, 0xE000, 0xFFFF };
	/* the counts above 0x2000 make offset + count pass 0xFFFF: a limit check
	 * done in 16 bits would wrap and let them through */
	const uint16_t cnts[] = { 0, 1, 2, 3, 4, 34, 35, 36, 219, 220, 221, 255, 256, 300, 0x2001, 0x3001, 0x6001 };
	const uint8_t slaves[] = { 1, 0, 2 };
	static uint8_t data[0x6100], req[0x6200], out[1024];
	for (unsigned i = 0; i < sizeof data; ++i) data[i] = uint8_t(i * 7 + 3);

	unsigned frames = 0;
	for (uint8_t fn : fns)
		for (uint16_t off : offs)
			for (uint16_t cnt : cnts)
				for (uint8_t slave : slaves) {
					resetMemory();
					const uint16_t n = frame(req, slave, fn, off, cnt, data, hasData(fn) ? cnt : 0);
					uint16_t len = 0;
					const uint8_t ret = decodePacketInto(&dev, req, n, out, sizeof out, &len);
					printAnswer("D", fn, off, cnt, slave, ret, out, len);
					frames++;
					if (slave == 1 && (cnt == 4 || cnt == 35)) {
						resetMemory(); /* a bad CRC, then a length that disagrees with REG_CNT */
						req[n - 2] ^= 0x5A;
						len = 0;
						printAnswer("C", fn, off, cnt, slave, decodePacketInto(&dev, req, n, out, sizeof out, &len), out, len);
						resetMemory();
						const uint16_t m = frame(req, slave, fn, off, uint16_t(cnt + 1), data, hasData(fn) ? cnt : 0);
						len = 0;
						printAnswer("L", fn, off, cnt, slave, decodePacketInto(&dev, req, m, out, sizeof out, &len), out, len);
						frames += 2;
					}
				}
	/* the device's own frames: READ, and READ_RESP / WRITE from the bank (pData null) */
	for (uint8_t fn : fns)
		for (uint16_t off : offs)
			for (uint16_t cnt : cnts) {
				resetMemory();
				uint16_t len = 0;
				const uint8_t ret = encodePacketInto(&dev, 1, fn, off, cnt, nullptr, out, sizeof out, &len);
				printAnswer("E", fn, off, cnt, 1, ret, out, len);
				frames++;
			}
	/* a message acknowledged: the queue compacts */
	resetMemory();
	{
		const uint8_t ack[] = { 0, 1 };
		uint16_t len = 0;
		const uint16_t n = frame(req, 1, WRITE_ACK, 0xA007, 2, ack, 2);
		printAnswer("M", WRITE_ACK, 0xA007, 2, 1, decodePacketInto(&dev, req, n, out, sizeof out, &len), out, len);
		std::printf("MSG %u %u %u\n", dev.MSG_CNT, dev.MSG_BUFFER[0], dev.MSG_BUFFER[1]);
	}
	std::printf("frames %u\n", frames);
	return 0;
}

#endif /* TRANSCRIPT */

/* ================================================================== FEATURES */
#ifdef FEATURES

#include <cstdlib>

static uint8_t answer[1024];
static uint16_t answerLen;

static uint8_t ask(evre_base_t *dev, uint8_t fn, uint16_t off, uint16_t cnt, const void *data = nullptr, uint8_t slave = 1) {
	static uint8_t req[512];
	const uint16_t n = frame(req, slave, fn, off, cnt, static_cast<const uint8_t *>(data), hasData(fn) ? cnt : 0);
	answerLen = 0;
	return decodePacketInto(dev, req, n, answer, sizeof answer, &answerLen);
}

static bool refusedWith(uint8_t code) {
	return answerLen == 11 && answer[2] == ERROR_RESP && answer[7] == code;
}

/* ------------------------------------------------------------- ranges */

static void ranges() {
	static uint8_t a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 }, c[4] = { 9, 10, 11, 12 }, d[4] = { 13, 14, 15, 16 };
	/* read-only, read-write, read-only, a gap, read-write */
	static const evre_range_t table[] = { { 0xD000, 4, a, 0 }, { 0xD004, 4, b, 1 }, { 0xD008, 4, c, 0 }, { 0xD010, 4, d, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 4;
	check(protocolInit(&dev) == NO_ERROR, "ranges: a valid table passes protocolInit");

	check(ask(&dev, READ, 0xD000, 12) == NO_ERROR && answerLen == 22 && answer[7] == 1 && answer[11] == 5 && answer[18] == 12,
			"ranges: a read across three adjacent ranges, in address order");
	check(ask(&dev, READ, 0xD000, 16) == REG_CNT_OUT_OF_RANGE && refusedWith(5), "ranges: a read into the gap: code 5");
	check(ask(&dev, READ, 0xD00C, 1) == REG_OFFSET_OUT_OF_RANGE && refusedWith(4), "ranges: a read starting in the gap: code 4");
	check(ask(&dev, READ, 0xD010, 5) == REG_CNT_OUT_OF_RANGE, "ranges: a read past the last range: code 5");

	const uint8_t w[8] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xB1, 0xB2, 0xB3, 0xB4 };
	check(ask(&dev, WRITE_ACK, 0xD004, 4, w) == NO_ERROR && answer[2] == WRITE_ACK_RESP && b[0] == 0xA1 && b[3] == 0xA4,
			"ranges: a write to a writable range between two read-only ones");
	check(ask(&dev, WRITE_ACK, 0xD004, 8, w) == PERMISSION_DENIED && refusedWith(3) && b[0] == 0xA1 && c[0] == 9,
			"ranges: a write running into a read-only range: code 3, nothing stored");
	check(ask(&dev, WRITE_ACK, 0xD000, 4, w) == PERMISSION_DENIED && a[0] == 1, "ranges: a write to a read-only range: code 3");
	check(ask(&dev, WRITE_ACK, 0xD00C, 4, w) == PERMISSION_DENIED, "ranges: a write to the gap: code 3");
	check(ask(&dev, WRITE_ACK, 0xD010, 4, w + 4) == NO_ERROR && d[0] == 0xB1, "ranges: a write to the last range");

	uint8_t out[64];
	uint16_t len = 0;
	check(encodePacketInto(&dev, 1, READ_RESP, 0xD004, 8, nullptr, out, sizeof out, &len) == NO_ERROR && len == 18
			&& out[7] == 0xA1 && out[11] == 9, "ranges: the device's own READ_RESP (AUTO_SEND) reads the ranges");

	struct Bad { const char *what; evre_range_t table[2]; uint8_t n; };
	static uint8_t mem[8];
	const Bad bad[] = {
		{ "unsorted", { { 0xD010, 4, mem, 0 }, { 0xD000, 4, mem, 0 } }, 2 },
		{ "overlapping", { { 0xD000, 8, mem, 0 }, { 0xD004, 4, mem, 1 } }, 2 },
		{ "empty", { { 0xD000, 0, mem, 0 }, {} }, 1 },
		{ "no memory", { { 0xD000, 4, nullptr, 0 }, {} }, 1 },
		{ "below 0xD000", { { 0xC000, 4, mem, 0 }, {} }, 1 },
		{ "past 0xDFFF", { { 0xDFFE, 4, mem, 0 }, {} }, 1 },
	};
	for (const Bad &t : bad) {
		evre_base_t other;
		other.D_RANGES = t.table;
		other.D_RANGE_CNT = t.n;
		char what[96];
		std::snprintf(what, sizeof what, "ranges: a table %s: protocolInit says RANGE_TABLE_INVALID", t.what);
		check(protocolInit(&other) == RANGE_TABLE_INVALID, what);
	}
}

/* ------------------------------------------------------------- handlers */

static unsigned writeCalls, readCalls;
static uint16_t lastOffset, lastCount;
static uint8_t lastData[8];
static uint8_t lastRx = 0xEE; /* RX_SLAVE_ID as the handler saw it */
static uint8_t writeVerdict = NO_ERROR, readVerdict = NO_ERROR;

static uint8_t onWrite(evre_base_t *dev, uint16_t offset, const uint8_t *data, uint16_t count) {
	writeCalls++;
	lastOffset = offset;
	lastCount = count;
	lastRx = dev->RX_SLAVE_ID;
	std::memcpy(lastData, data, count < 8 ? count : 8);
	return writeVerdict;
}

static uint8_t onRead(evre_base_t *dev, uint16_t offset, uint16_t count) {
	readCalls++;
	lastOffset = offset;
	lastCount = count;
	lastRx = dev->RX_SLAVE_ID;
	return readVerdict;
}

static void handlers(bool withRanges) {
	static uint8_t ro[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rw[8];
	static const evre_range_t table[] = { { 0xD000, 8, ro, 0 }, { 0xD008, 8, rw, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	check(protocolInit(&dev) == NO_ERROR, "handlers: protocolInit");
	dev.DEVICE_REG_READ_MAX = 0xD00F;
	dev.DEVICE_REG_WRITE_MIN = 0xD008;
	if (withRanges) {
		dev.D_RANGES = table;
		dev.D_RANGE_CNT = 2;
	} else {
		dev.D000 = new uint8_t *[16];
		for (unsigned i = 0; i < 8; ++i) dev.D000[i] = &ro[i];
		for (unsigned i = 0; i < 8; ++i) dev.D000[8 + i] = &rw[i];
	}
	dev.WRITE_HANDLER = onWrite;
	dev.READ_HANDLER = onRead;
	const char *path = withRanges ? "ranges" : "pointers";
	char what[128];
#define CHECK(ok, text) do { std::snprintf(what, sizeof what, "handlers (%s): %s", path, text); check(ok, what); } while (0)

	const uint8_t w[4] = { 0x11, 0x22, 0x33, 0x44 };
	std::memset(rw, 0, sizeof rw);
	writeCalls = readCalls = 0;
	writeVerdict = NO_ERROR;
	CHECK(ask(&dev, WRITE_ACK, 0xD008, 4, w) == NO_ERROR && answer[2] == WRITE_ACK_RESP && rw[0] == 0x11 && writeCalls == 1
			&& lastOffset == 0xD008 && lastCount == 4 && lastData[3] == 0x44,
			"the write handler sees the offset, the count and the bytes; NO_ERROR stores them");

	writeVerdict = PERMISSION_DENIED;
	std::memset(rw, 0, sizeof rw);
	CHECK(ask(&dev, WRITE_ACK, 0xD008, 4, w) == PERMISSION_DENIED && refusedWith(3) && rw[0] == 0,
			"a refusal: ERROR_RESP with its code, nothing stored");
	CHECK(ask(&dev, WRITE, 0xD008, 4, w) == PERMISSION_DENIED && refusedWith(3) && rw[0] == 0,
			"a refused WRITE: ERROR_RESP, as every refused WRITE in 1.0; nothing stored");
	writeCalls = 0;
	dev.ACCEPT_BROADCAST_D000 = 1;
	CHECK(ask(&dev, WRITE, 0xD008, 4, w, 0) == PERMISSION_DENIED && answerLen == 0 && rw[0] == 0 && writeCalls == 1,
			"a broadcast WRITE the handler refuses (ACCEPT_BROADCAST_D000 1): silent, nothing stored");
	dev.ACCEPT_BROADCAST_D000 = 0;
	writeVerdict = 13;
	CHECK(ask(&dev, WRITE_ACK, 0xD008, 4, w) == 13 && refusedWith(13), "any code the handler returns is the answer");

	writeVerdict = EVRE_HANDLED;
	CHECK(ask(&dev, WRITE_ACK, 0xD008, 4, w) == NO_ERROR && answer[2] == WRITE_ACK_RESP && rw[0] == 0,
			"EVRE_HANDLED: acknowledged, nothing stored");

	dev.ACCEPT_READ_RESP = 1; /* a mirror */
	writeVerdict = PERMISSION_DENIED;
	writeCalls = 0;
	CHECK(ask(&dev, READ_RESP, 0xD000, 4, w) == PERMISSION_DENIED && answerLen == 0 && ro[0] == 1 && writeCalls == 1,
			"a mirror: an incoming READ_RESP (it stores data) is asked too: refused, nothing stored, no answer");
	writeVerdict = NO_ERROR;
	CHECK(ask(&dev, READ_RESP, 0xD008, 4, w) == NO_ERROR && rw[0] == 0x11, "a mirror: an incoming READ_RESP accepted: stored, as in 1.0");
	std::memset(rw, 0, sizeof rw);
	dev.ACCEPT_READ_RESP = 0;
	writeCalls = 0;
	CHECK(ask(&dev, READ_RESP, 0xD008, 4, w) == FUNCTION_CODE_ERR && answerLen == 0 && rw[0] == 0 && writeCalls == 0,
			"a device (the default): a READ_RESP sent to it is refused by the library, before the handler");

	writeVerdict = NO_ERROR;
	writeCalls = 0;
	CHECK(ask(&dev, WRITE_ACK, 0xD000, 4, w) == PERMISSION_DENIED && writeCalls == 0,
			"not asked when the library refuses first (a read-only range)");
	CHECK(ask(&dev, WRITE_ACK, 0xD00E, 4, w) == REG_CNT_OUT_OF_RANGE && writeCalls == 0,
			"not asked when the library refuses first (past the bank)");

	writeVerdict = PERMISSION_DENIED;
	dev.CONFIG = 0;
	const uint8_t reset[2] = { 0x02, 0x00 };
	CHECK(ask(&dev, WRITE_ACK, 0xA004, 2, reset) == PERMISSION_DENIED && writeCalls == 1 && lastOffset == 0xA004
			&& (dev.CONFIG & 0x02) == 0, "asked for the reserved bank too: CONFIG (SYSTEM_RESET) can be refused");
	writeVerdict = EVRE_HANDLED;
	CHECK(ask(&dev, WRITE_ACK, 0xA004, 2, reset) == NO_ERROR && (dev.CONFIG & 0x02) == 0,
			"EVRE_HANDLED on the reserved bank: acknowledged, CONFIG unchanged");
	writeVerdict = NO_ERROR;
	dev.CONFIG = 0;
	CHECK(ask(&dev, WRITE_ACK, 0xA004, 2, reset) == NO_ERROR && (dev.CONFIG & 0x02) != 0, "NO_ERROR: CONFIG written");

	readVerdict = NO_ERROR;
	readCalls = 0;
	CHECK(ask(&dev, READ, 0xD000, 8) == NO_ERROR && answerLen == 18 && answer[7] == 1 && readCalls == 1 && lastOffset == 0xD000
			&& lastCount == 8, "the read handler sees the offset and the count; NO_ERROR answers");
	readVerdict = PERMISSION_DENIED;
	CHECK(ask(&dev, READ, 0xD000, 8) == PERMISSION_DENIED && refusedWith(3), "a read refused: ERROR_RESP 3, no data");
	CHECK(ask(&dev, READ, 0xA000, 2) == PERMISSION_DENIED && readCalls == 3, "asked for reserved-bank reads too");
	readCalls = 0;
	CHECK(ask(&dev, READ, 0xD020, 1) == REG_OFFSET_OUT_OF_RANGE && readCalls == 0, "not asked when the library refuses first");
	uint8_t out[64];
	uint16_t len = 0;
	CHECK(encodePacketInto(&dev, 1, READ_RESP, 0xD000, 4, nullptr, out, sizeof out, &len) == NO_ERROR && len == 14 && readCalls == 0,
			"not asked for the device's own READ_RESP (AUTO_SEND)");
	readVerdict = NO_ERROR;
#undef CHECK
}

/* ------------------------------------------------------------ EVRe Guard */

static uint64_t clockMs = 1000;
static uint64_t fakeClock() { return clockMs; }
static const uint64_t DAY_MS = 86400000u;
static evre_guard_t guard;
static unsigned guardWrites; /* how often the library asked the guard to write */
static uint8_t guardRead(evre_base_t *, uint16_t off, uint16_t cnt) { return evre_guard_read(&guard, off, cnt); }
static uint8_t guardWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	guardWrites++;
	return evre_guard_write(&guard, d, off, data, cnt);
}

static uint8_t ro8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rw8[8], login16[16];
static const evre_range_t guardedTable[] = { { 0xD000, 8, ro8, 0 }, { 0xD008, 8, rw8, 1 }, { 0xD010, 16, login16, 1 } };
static const uint8_t token[16] = { 's', 'e', 'c', 'r', 'e', 't', '-', 't', 'o', 'k', 'e', 'n' };
static const uint8_t wrongToken[16] = { 's', 'e', 'c', 'r', 'e', 't', '-', 't', 'o', 'k', 'e', 'X' };
static const evre_guard_span_t openSpan[] = { { 0xD000, 4 } };

/* a device behind the guard: DEVICE_ID 0x1234, the three ranges above */
static void guardedDevice(evre_base_t &dev) {
	dev.SALVE_ID_REG = 1;
	dev.DEVICE_ID = 0x1234;
	dev.D_RANGES = guardedTable;
	dev.D_RANGE_CNT = 3;
	check(protocolInit(&dev) == NO_ERROR, "guard: protocolInit");
	dev.READ_HANDLER = guardRead;
	dev.WRITE_HANDLER = guardWrite;
}

static void guarded() {
	static const evre_guard_config_t cfg = { 0xD010, 16, token, openSpan, 1, 3, 1000, 8000, 5000, fakeClock };
	evre_base_t dev;
	guardedDevice(dev);
	clockMs = 1000;
	check(evre_guard_init(&guard, &cfg) == NO_ERROR, "guard: evre_guard_init with a good config: NO_ERROR");

	const uint8_t value[4] = { 9, 9, 9, 9 };

	check(ask(&dev, READ, 0xA000, 4) == NO_ERROR && answer[7] == 0x34, "guard: before a login DEVICE_ID and STATUS read");
	check(ask(&dev, READ, 0xD000, 4) == NO_ERROR && answer[7] == 1, "guard: before a login an open span reads");
	check(ask(&dev, READ, 0xD000, 8) == LOGIN_REQUIRED && refusedWith(LOGIN_REQUIRED),
			"guard: before a login the rest is refused: LOGIN_REQUIRED, log in and retry (D-18)");
	check(ask(&dev, READ, 0xA004, 2) == LOGIN_REQUIRED, "guard: before a login CONFIG is not readable");
	check(ask(&dev, WRITE_ACK, 0xD008, 4, value) == LOGIN_REQUIRED && refusedWith(LOGIN_REQUIRED) && rw8[0] == 0,
			"guard: before a login no write: LOGIN_REQUIRED");
	check(ask(&dev, WRITE_ACK, 0xA004, 2, value) == LOGIN_REQUIRED && dev.CONFIG != 0x0909,
			"guard: before a login CONFIG (reset, DFU) cannot be written");
	check(ask(&dev, READ_RESP, 0xD008, 4, value) == FUNCTION_CODE_ERR && rw8[0] == 0 && ro8[0] == 1,
			"guard: a READ_RESP sent to the device: refused by the library itself");
	dev.ACCEPT_READ_RESP = 1;
	check(ask(&dev, READ_RESP, 0xD008, 4, value) == LOGIN_REQUIRED && answerLen == 0 && rw8[0] == 0,
			"guard: even on a mirror, a READ_RESP cannot write before a login");
	dev.ACCEPT_READ_RESP = 0;

	check(ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken) == PERMISSION_DENIED && refusedWith(PERMISSION_DENIED) && guard.failures == 1
			&& !guard.locked, "guard: a wrong token: PERMISSION_DENIED, 1 failure, no lockout yet");
	ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	check(ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken) == PERMISSION_DENIED && guard.failures == 3 && guard.locked
			&& guard.lock_left == 1000, "guard: the third wrong token starts a 1 s lockout");
	check(ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED && !guard.logged_in && guard.failures == 3,
			"guard: during the lockout even the right token is refused (not compared): PERMISSION_DENIED");
	check(ask(&dev, READ, 0xD000, 8) == LOGIN_REQUIRED, "guard: during the lockout a read: LOGIN_REQUIRED, as without one");
	clockMs += 999;
	check(ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED && guard.locked && guard.lock_left == 1,
			"guard: 999 ms into the lockout: still refused, 1 ms to go");
	clockMs += 2;
	check(ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken) == PERMISSION_DENIED && guard.lock_left == 2000,
			"guard: a wrong token after the lockout doubles it (2 s)");
	clockMs += 2001;
	ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	check(guard.lock_left == 4000, "guard: then 4 s");
	clockMs += 4001;
	ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	check(guard.lock_left == 8000, "guard: then 8 s");
	clockMs += 8001;
	ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	check(guard.lock_left == 8000, "guard: and never more than lockout_max_ms (8 s)");
	clockMs += 8001;

	check(ask(&dev, WRITE_ACK, 0xD010, 8, token) == PERMISSION_DENIED && guard.failures == 8,
			"guard: a partial token write is a failed attempt: PERMISSION_DENIED");
	clockMs += 8001;
	check(ask(&dev, WRITE_ACK, 0xD010, 16, token) == NO_ERROR && answer[2] == WRITE_ACK_RESP && guard.logged_in
			&& guard.failures == 0, "guard: the right token after the lockout: logged in, the count cleared");
	bool stored = false;
	for (uint8_t b : login16) stored |= b != 0;
	check(!stored, "guard: the token is never stored in the login register's memory");
	check(ask(&dev, READ, 0xD010, 16) == NO_ERROR && answer[7] == 0 && answer[8] == 0, "guard: a read of the login register: not the token");

	check(ask(&dev, READ, 0xD000, 8) == NO_ERROR && answerLen == 18, "guard: logged in: every read");
	check(ask(&dev, WRITE_ACK, 0xD008, 4, value) == NO_ERROR && rw8[0] == 9, "guard: logged in: writes");
	clockMs += 4000;
	check(ask(&dev, READ, 0xD000, 8) == NO_ERROR, "guard: 4 s idle: still logged in");
	clockMs += 5001;
	check(ask(&dev, READ, 0xD000, 8) == LOGIN_REQUIRED && !guard.logged_in, "guard: more than 5 s idle: logged out, LOGIN_REQUIRED");

	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	evre_guard_logout(&guard);
	check(ask(&dev, WRITE_ACK, 0xD008, 4, value) == LOGIN_REQUIRED, "guard: after evre_guard_logout: LOGIN_REQUIRED");
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	check(ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken) == PERMISSION_DENIED && !guard.logged_in,
			"guard: a wrong token in a session ends it");
}

/* ---------------------------------------------- D2, F1, F2 on their own */

static void fixes() {
	static uint8_t ro[4] = { 1, 2, 3, 4 }, rw[4];
	static const evre_range_t table[] = { { 0xD000, 4, ro, 0 }, { 0xD004, 4, rw, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	dev.DEVICE_ID = 0xBEEF;
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 2;
	check(protocolInit(&dev) == NO_ERROR && dev.A000 == nullptr, "D2: protocolInit allocates no reserved-bank table");
	check(ask(&dev, READ, 0xA000, 4) == NO_ERROR && answer[7] == 0xEF && answer[8] == 0xBE && answer[9] == (dev.STATUS & 0xFF),
			"D2: DEVICE_ID and STATUS read in place, little endian");
	const uint8_t cfg[2] = { 0x08, 0x4F };
	check(ask(&dev, WRITE_ACK, 0xA004, 2, cfg) == NO_ERROR && (dev.CONFIG & 0xFF08) == 0x4F08, "D2: CONFIG written in place");
	check(addMsg(&dev, 9) == NO_ERROR && ask(&dev, READ, 0xA006, 2) == NO_ERROR && answer[7] == 1 && answer[8] == 9,
			"D2: MSG_CNT and MSG_BUFFER read in place");

	const uint8_t w[4] = { 7, 7, 7, 7 };
	check(ask(&dev, READ_RESP, 0xD000, 4, w) == FUNCTION_CODE_ERR && answerLen == 0 && ro[0] == 1,
			"F1: a device: a READ_RESP cannot overwrite a read-only register, and is not answered");
	check(ask(&dev, READ_RESP, 0xD004, 4, w) == FUNCTION_CODE_ERR && rw[0] == 0, "F1: nor a writable one");
	dev.ACCEPT_READ_RESP = 1;
	check(ask(&dev, READ_RESP, 0xD000, 4, w) == NO_ERROR && ro[0] == 7, "F1: a mirror stores it, as 1.0 did");
	dev.ACCEPT_READ_RESP = 0;

	check(ask(&dev, WRITE_ACK, 0xE000, 4, w) == PERMISSION_DENIED && refusedWith(3), "F2: a WRITE_ACK to an unknown bank: code 3");
	check(ask(&dev, WRITE, 0x0000, 4, w) == PERMISSION_DENIED && refusedWith(3), "F2: a WRITE to an unknown bank: code 3");
	check(ask(&dev, WRITE, 0xF000, 4, w, 0) == PERMISSION_DENIED && answerLen == 0, "F2: a broadcast WRITE there: refused, silent");
	uint8_t out[32];
	uint16_t len = 0;
	check(encodePacketInto(&dev, 1, WRITE, 0xE000, 4, (uint8_t*) w, out, sizeof out, &len) == PERMISSION_DENIED && len == 0,
			"F2: the encoder builds no WRITE to an unknown bank either");
}

/* ------------------------------------------------ the order of the steps */

/* Each frame here tells apart two orders of the same steps, each a plausible
 * clean-up of the library. The transcript cannot: it has no handlers, a large
 * buffer, bad CRCs for slave 1 only and data functions at their exact size.
 * Where stage 2 changed the order, the check says so ("D-n") and pins the new
 * one. */

/* a frame with every knob: its data apart from its count, the CRC, the room */
static uint8_t askRaw(evre_base_t *dev, uint8_t fn, uint16_t off, uint16_t cnt, const void *data, uint16_t dataLen,
		uint8_t slave = 1, uint16_t outMax = sizeof answer, bool badCrc = false) {
	static uint8_t req[64];
	const uint16_t n = frame(req, slave, fn, off, cnt, static_cast<const uint8_t *>(data), dataLen);
	if (badCrc) {
		req[n - 3] ^= 0x5A;
	}
	answerLen = 0;
	return decodePacketInto(dev, req, n, answer, outMax, &answerLen);
}

static evre_base_t *watched;
static uint8_t cntAtClear, codeAtAck;
static uint16_t configAtClear;
static void onClear() {
	cntAtClear = watched->MSG_CNT;
	configAtClear = watched->CONFIG;
}
static void onAck() { codeAtAck = watched->MSG_BUFFER[0]; }

/* handlers that give the device another slave id, as a layer above might */
static uint8_t renumberAndAnswer(evre_base_t *dev, uint16_t, uint16_t) {
	dev->SALVE_ID_REG = 9;
	return NO_ERROR;
}
static uint8_t renumberAndRefuse(evre_base_t *dev, uint16_t, const uint8_t *, uint16_t) {
	dev->SALVE_ID_REG = 9;
	return PERMISSION_DENIED;
}

static void orders() {
	static uint8_t ro[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rw[8];
	static uint8_t *table[16];
	for (unsigned i = 0; i < 8; ++i) {
		table[i] = &ro[i];
		table[8 + i] = &rw[i];
	}
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	check(protocolInit(&dev) == NO_ERROR, "order: protocolInit");
	dev.DEVICE_REG_READ_MAX = 0xD00F;
	dev.DEVICE_REG_WRITE_MIN = 0xD008;
	dev.D000 = table;
	dev.READ_HANDLER = onRead;
	dev.WRITE_HANDLER = onWrite;
	const uint8_t w[4] = { 0x11, 0x22, 0x33, 0x44 };

	/* the room is checked before the handlers are asked (D-2) */
	readVerdict = PERMISSION_DENIED;
	readCalls = 0;
	check(askRaw(&dev, READ, 0xD000, 8, nullptr, 0, 1, 12) == BUFFER_TOO_SMALL && refusedWith(BUFFER_TOO_SMALL) && readCalls == 0,
			"D-2: a read the handler would refuse, too big for the buffer: BUFFER_TOO_SMALL, the handler not asked (1.0: its code)");
	readVerdict = NO_ERROR;
	readCalls = 0;
	check(askRaw(&dev, READ, 0xD000, 8, nullptr, 0, 1, 12) == BUFFER_TOO_SMALL && refusedWith(BUFFER_TOO_SMALL) && readCalls == 0,
			"D-2: a read too big for the buffer: BUFFER_TOO_SMALL before the handler is asked");
	writeVerdict = NO_ERROR;
	writeCalls = 0;
	std::memset(rw, 0, sizeof rw);
	check(askRaw(&dev, WRITE_ACK, 0xD008, 4, w, 4, 1, 9) == BUFFER_TOO_SMALL && answerLen == 0 && writeCalls == 0 && rw[0] == 0,
			"D-2: a WRITE_ACK whose answer does not fit: the handler not asked, nothing stored");
	writeCalls = 0;
	check(askRaw(&dev, WRITE, 0xD008, 4, w, 4, 1, 0) == NO_ERROR && writeCalls == 1 && rw[0] == 0x11,
			"D-2: a WRITE needs no room: asked and stored with no buffer at all");
	std::memset(rw, 0, sizeof rw);
	writeCalls = 0;

	/* the CRC before the slave id */
	check(askRaw(&dev, READ, 0xD000, 1, nullptr, 0, 2, sizeof answer, true) == INVALID_PACKET_ERR && answerLen == 0,
			"order: a bad CRC for another slave: INVALID_PACKET_ERR, the CRC is checked first");

	/* the function code before the length (D-12) */
	check(askRaw(&dev, 0x55, 0xD000, 4, w, 4) == FUNCTION_CODE_ERR && refusedWith(FUNCTION_CODE_ERR),
			"D-12: an unknown function code with data: FUNCTION_CODE_ERR, answered (1.0: LENGTH_MISMATCH)");
	check(askRaw(&dev, 0x55, 0xD000, 4, nullptr, 0) == FUNCTION_CODE_ERR && refusedWith(FUNCTION_CODE_ERR),
			"order: an unknown function code in 10 bytes: FUNCTION_CODE_ERR");
	check(askRaw(&dev, 0x55, 0xD000, 4, w, 4, 0) == FUNCTION_CODE_ERR && answerLen == 0,
			"D-12: an unknown function code, broadcast: FUNCTION_CODE_ERR, silent");
	check(askRaw(&dev, READ, 0xD000, 4, w, 4) == LENGTH_MISMATCH && refusedWith(LENGTH_MISMATCH), "order: a READ with data: LENGTH_MISMATCH");
	check(askRaw(&dev, ERROR_RESP, 0xD000, 4, w, 1) == FUNCTION_CODE_ERR && answerLen == 0,
			"D-12: an incoming 11-byte ERROR_RESP: FUNCTION_CODE_ERR, not answered (1.0: LENGTH_MISMATCH)");
	check(askRaw(&dev, ERROR_RESP, 0xD000, 4, nullptr, 0) == FUNCTION_CODE_ERR && answerLen == 0,
			"order: a 10-byte ERROR_RESP: FUNCTION_CODE_ERR, not answered");
	check(askRaw(&dev, WRITE_ACK_RESP, 0xD008, 4, w, 2) == LENGTH_MISMATCH && answerLen == 0,
			"order: a WRITE_ACK_RESP with data: a known code, so the length is checked: LENGTH_MISMATCH, not answered");

	/* offset + count in 32 bits: 0xD000 + 0x3001 - 1 is 0x10000, 0 in 16 bits */
	check(askRaw(&dev, READ, 0xD000, 0x3001, nullptr, 0) == REG_CNT_OUT_OF_RANGE && refusedWith(REG_CNT_OUT_OF_RANGE),
			"order: READ 0xD000 x0x3001: REG_CNT_OUT_OF_RANGE, the last byte does not wrap");
	check(askRaw(&dev, READ, 0xA000, 0x6001, nullptr, 0) == REG_CNT_OUT_OF_RANGE, "order: READ 0xA000 x0x6001: REG_CNT_OUT_OF_RANGE");
	uint8_t out[32];
	uint16_t len = 0;
	check(encodePacketInto(&dev, 1, READ_RESP, 0xD000, 0x3001, nullptr, out, sizeof out, &len) == REG_CNT_OUT_OF_RANGE && len == 0,
			"order: the encoder, READ_RESP 0xD000 x0x3001: REG_CNT_OUT_OF_RANGE");
	check(encodePacketInto(&dev, 1, WRITE, 0xA004, 0xFFFF, nullptr, out, sizeof out, &len) == REG_CNT_OUT_OF_RANGE && len == 0,
			"order: the encoder, WRITE 0xA004 x0xFFFF: REG_CNT_OUT_OF_RANGE");

	/* outLen is 0 after every call, the earliest refusals included (D-3) */
	uint8_t runt[9] = { 0x7B, 1, READ, 0x00, 0xD0, 1, 0, 0, 0 };
	len = 0xBEEF;
	check(decodePacketInto(&dev, runt, sizeof runt, out, sizeof out, &len) == INVALID_PACKET_ERR && len == 0,
			"D-3: a runt frame: outLen 0 (1.0 left it alone)");
	len = 0xBEEF;
	check(decodePacketInto(nullptr, runt, sizeof runt, out, sizeof out, &len) == INSTANCE_IS_NULL && len == 0,
			"D-3: no device: outLen 0");
	len = 0xBEEF;
	check(decodePacketInto(&dev, nullptr, 10, out, sizeof out, &len) == INVALID_PACKET_ERR && len == 0, "D-3: no frame: outLen 0");
	len = 0xBEEF;
	check(encodePacketInto(nullptr, 1, READ, 0xD000, 1, nullptr, out, sizeof out, &len) == INSTANCE_IS_NULL && len == 0,
			"D-3: the encoder with no device: outLen 0");
	check(askRaw(&dev, READ, 0xD000, 1, nullptr, 0, 1, sizeof answer, true) == INVALID_PACKET_ERR && answerLen == 0,
			"order: a 10-byte frame with a bad CRC: outLen 0");

	/* silence is decided by the frame, never by the code (D-8) */
	readVerdict = INVALID_PACKET_ERR;
	check(askRaw(&dev, READ, 0xD000, 4, nullptr, 0) == INVALID_PACKET_ERR && refusedWith(INVALID_PACKET_ERR),
			"D-8: a read handler's INVALID_PACKET_ERR is answered with it (1.0: silent)");
	readVerdict = SLAVE_ID_MISMATCHED;
	check(askRaw(&dev, READ, 0xD000, 4, nullptr, 0) == SLAVE_ID_MISMATCHED && refusedWith(SLAVE_ID_MISMATCHED),
			"D-8: a read handler's SLAVE_ID_MISMATCHED is answered with it (1.0: silent)");
	readVerdict = EVRE_HANDLED;
	check(askRaw(&dev, READ, 0xD000, 4, nullptr, 0) == NO_ERROR && answerLen == 14 && answer[2] == READ_RESP && answer[7] == 1,
			"D-8: EVRE_HANDLED from the read handler counts as NO_ERROR: answered (1.0: a refusal, code 0xFF)");
	readVerdict = NO_ERROR;
	writeVerdict = SLAVE_ID_MISMATCHED;
	check(askRaw(&dev, WRITE_ACK, 0xD008, 4, w, 4) == SLAVE_ID_MISMATCHED && refusedWith(SLAVE_ID_MISMATCHED) && rw[0] == 0,
			"D-8: a write handler's SLAVE_ID_MISMATCHED is answered with it (1.0: silent)");
	writeVerdict = INVALID_PACKET_ERR;
	check(askRaw(&dev, WRITE_ACK, 0xD008, 4, w, 4) == INVALID_PACKET_ERR && refusedWith(INVALID_PACKET_ERR) && rw[0] == 0,
			"D-8: a write handler's INVALID_PACKET_ERR on a WRITE_ACK is answered with it (1.0: silent)");
	check(askRaw(&dev, WRITE, 0xD008, 4, w, 4) == INVALID_PACKET_ERR && refusedWith(INVALID_PACKET_ERR) && rw[0] == 0,
			"D-8: and on a WRITE (1.0: silent)");
	dev.ACCEPT_BROADCAST_D000 = 1;
	check(askRaw(&dev, WRITE, 0xD008, 4, w, 4, 0) == INVALID_PACKET_ERR && answerLen == 0 && rw[0] == 0,
			"D-8: but a broadcast stays silent, whatever the handler says");
	dev.ACCEPT_BROADCAST_D000 = 0;
	dev.ACCEPT_READ_RESP = 1;
	check(askRaw(&dev, READ_RESP, 0xD008, 4, w, 4) == INVALID_PACKET_ERR && answerLen == 0 && rw[0] == 0,
			"D-8: and so does a response a mirror refuses");
	dev.ACCEPT_READ_RESP = 0;
	writeVerdict = NO_ERROR;

	/* messages: the clear before its handler; the queue zeroed past MSG_CNT (D-13) */
	watched = &dev;
	dev.MSG_ACK_HANDLER[0] = onClear;
	addMsg(&dev, 1);
	addMsg(&dev, 2);
	addMsg(&dev, 7);
	cntAtClear = 0xEE;
	const uint8_t zero[1] = { 0 };
	check(askRaw(&dev, WRITE, 0xA006, 1, zero, 1) == NO_ERROR && dev.MSG_CNT == 0 && cntAtClear == 0,
			"order: a write to MSG_CNT: the count is 0 when MSG_ACK_HANDLER[0] runs");
	check(askRaw(&dev, READ, 0xA006, 4, nullptr, 0) == NO_ERROR && answer[7] == 0 && answer[8] == 0 && answer[9] == 0 && answer[10] == 0,
			"D-13: after a clear the whole queue reads 0 (1.0: the old codes past MSG_CNT)");
	dev.MSG_ACK_HANDLER[0] = nullptr;
	addMsg(&dev, 1);
	addMsg(&dev, 2);
	addMsg(&dev, 7);
	check(askRaw(&dev, WRITE, 0xA008, 1, zero, 1) == NO_ERROR && askRaw(&dev, READ, 0xA006, 4, nullptr, 0) == NO_ERROR
			&& answer[7] == 2 && answer[8] == 1 && answer[9] == 7 && answer[10] == 0,
			"D-13: message 2 of 1 2 7 acknowledged: 1 7 queued, 0 past MSG_CNT (1.0: the old 7)");
	dev.MSG_ACK_HANDLER[1] = onAck;
	codeAtAck = 0;
	check(askRaw(&dev, WRITE, 0xA007, 1, zero, 1) == NO_ERROR && codeAtAck == 1 && dev.MSG_BUFFER[0] == 7,
			"order: an ack handler runs before its slot is cleared: it still sees its code");
	dev.MSG_ACK_HANDLER[0] = onClear;
	dev.CONFIG = 0;
	configAtClear = 0xEEEE;
	const uint8_t cfgClearAck[4] = { 0x10, 0x20, 0, 0 };
	check(askRaw(&dev, WRITE, 0xA004, 4, cfgClearAck, 4) == NO_ERROR && configAtClear == 0x2010,
			"order: CONFIG, MSG_CNT and a message in one write: byte by byte, CONFIG is written when the clear runs");
	dev.MSG_ACK_HANDLER[0] = nullptr;
	dev.MSG_ACK_HANDLER[1] = nullptr;

	/* the answers carry the slave id as it is after the handler ran */
	dev.READ_HANDLER = renumberAndAnswer;
	check(askRaw(&dev, READ, 0xD000, 1, nullptr, 0) == NO_ERROR && answer[SLAVE_ID] == 9, "order: a READ_RESP: the id after the read handler");
	dev.SALVE_ID_REG = 1;
	dev.READ_HANDLER = onRead;
	dev.WRITE_HANDLER = renumberAndRefuse;
	check(askRaw(&dev, WRITE_ACK, 0xD008, 1, w, 1) == PERMISSION_DENIED && refusedWith(PERMISSION_DENIED) && answer[SLAVE_ID] == 9,
			"order: an ERROR_RESP: the id after the write handler, not the frame's");
	dev.SALVE_ID_REG = 1;
	dev.WRITE_HANDLER = onWrite;

	/* the WRITE_ACK answer is built after the store, with the id from before it (D-2) */
	static uint8_t block[4];
	evre_base_t self;
	self.SALVE_ID_REG = 1;
	const evre_range_t own[] = { { 0xD000, 1, &self.SALVE_ID_REG, 1 }, { 0xD001, 4, block, 1 } };
	self.D_RANGES = own;
	self.D_RANGE_CNT = 2;
	check(protocolInit(&self) == NO_ERROR, "order: a device with its slave id as a register");
	const uint8_t five[1] = { 5 };
	check(askRaw(&self, WRITE_ACK, 0xD000, 1, five, 1) == NO_ERROR && answer[SLAVE_ID] == 1 && self.SALVE_ID_REG == 5,
			"order: a WRITE_ACK to the slave id: the answer carries the id from before the write");
	uint8_t inPlace[32];
	const uint16_t n = frame(inPlace, 5, WRITE_ACK, 0xD001, 4, w, 4);
	len = 0;
	check(decodePacketInto(&self, inPlace, n, inPlace, sizeof inPlace, &len) == NO_ERROR && len == 10 && inPlace[FN_CODE] == WRITE_ACK_RESP
			&& block[0] == 0x11 && block[1] == 0x22 && block[2] == 0x33 && block[3] == 0x44,
			"D-2: a WRITE_ACK decoded in place stores the host's bytes (1.0 stored the answer's CRC and end byte)");

	/* the encoder, with pData already where the data goes */
	out[7] = 0xA1;
	out[8] = 0xA2;
	check(encodePacketInto(&dev, 1, WRITE, 0xD008, 2, &out[7], out, sizeof out, &len) == NO_ERROR && len == 12 && out[7] == 0xA1
			&& out[8] == 0xA2 && GetCrc16(out, 9) == (out[9] | (out[10] << 8)), "order: the encoder with pData inside outBuf");

	/* ranges: a read-only range is judged before the gap after it */
	static uint8_t a[4], b[4];
	evre_base_t gaps;
	const evre_range_t gapTable[] = { { 0xD000, 4, a, 0 }, { 0xD008, 4, b, 1 } };
	gaps.SALVE_ID_REG = 1;
	gaps.D_RANGES = gapTable;
	gaps.D_RANGE_CNT = 2;
	check(protocolInit(&gaps) == NO_ERROR, "order: a table with a gap");
	const uint8_t w8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	check(askRaw(&gaps, WRITE_ACK, 0xD000, 8, w8, 8) == PERMISSION_DENIED && refusedWith(PERMISSION_DENIED),
			"order: a write over a read-only range into a gap: PERMISSION_DENIED, not REG_CNT_OUT_OF_RANGE");
	check(askRaw(&gaps, WRITE_ACK, 0xD008, 8, w8, 8) == REG_CNT_OUT_OF_RANGE, "order: a write from a writable range past the table: REG_CNT_OUT_OF_RANGE");
	check(askRaw(&gaps, READ, 0xD000, 0x3001, nullptr, 0) == REG_CNT_OUT_OF_RANGE, "order: ranges, READ 0xD000 x0x3001: REG_CNT_OUT_OF_RANGE");
}

/* ------------------------------------------------ stage 2, one decision each */

/* D-5, run first: no protocolInit() has built a RAM table yet */
static void crcBeforeInit() {
	const uint8_t digits[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
	evre_base_t fresh;
	fresh.SALVE_ID_REG = 1;
	uint8_t out[16];
	uint16_t len = 0;
	const bool vector1 = encodePacketInto(&fresh, 1, READ, 0xA000, 2, nullptr, out, sizeof out, &len) == NO_ERROR && len == 10
			&& out[7] == 0x9E && out[8] == 0x75;
#if EVRE_CRC_TABLE_RUNTIME
	check(vector1 && GetCrc16(digits, 9) == 0x906E && crctab16[1] == 0x1189,
			"D-5: the CRC table in RAM, before any protocolInit(): built on first use, test vector 1 and CRC-16/X-25 right");
#else
	check(vector1 && GetCrc16(digits, 9) == 0x906E, "D-5: (the table in flash) test vector 1 and CRC-16/X-25 before any protocolInit()");
#endif
}

/* a frame whose start and end bytes are set by hand; the CRC made for them (it covers the start) */
static uint8_t askDelimited(evre_base_t *dev, uint8_t fn, uint16_t off, uint16_t cnt, const void *data, uint8_t start, uint8_t end) {
	static uint8_t req[64];
	const uint16_t n = frame(req, 1, fn, off, cnt, static_cast<const uint8_t *>(data), hasData(fn) ? cnt : 0);
	req[START] = start;
	const uint16_t crc = GetCrc16(req, n - 3);
	req[n - 3] = uint8_t(crc);
	req[n - 2] = uint8_t(crc >> 8);
	req[n - 1] = end;
	answerLen = 0;
	return decodePacketInto(dev, req, n, answer, sizeof answer, &answerLen);
}

static uint8_t loginFirst(evre_base_t *, uint16_t, uint16_t) { return LOGIN_REQUIRED; }

static void decisions() {
	static uint8_t ro[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rw[8];
	static const evre_range_t table[] = { { 0xD000, 8, ro, 0 }, { 0xD008, 8, rw, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 2;
	check(protocolInit(&dev) == NO_ERROR, "D-n: protocolInit");
	const uint8_t w[4] = { 0x11, 0x22, 0x33, 0x44 };
	uint8_t out[32];
	uint16_t len = 0;

	/* D-6 */
	{
		static uint8_t whole[0x1000];
		static uint8_t *pointers[0x1000];
		for (unsigned i = 0; i < 0x1000; ++i) {
			whole[i] = uint8_t(i);
			pointers[i] = &whole[i];
		}
		evre_base_t wide;
		wide.SALVE_ID_REG = 1;
		check(protocolInit(&wide) == NO_ERROR, "D-6: protocolInit");
		wide.DEVICE_REG_READ_MAX = 0xFFFF;
		wide.D000 = pointers;
		check(ask(&wide, READ, 0xDFF0, 0x20) == REG_CNT_OUT_OF_RANGE && refusedWith(REG_CNT_OUT_OF_RANGE),
				"D-6: DEVICE_REG_READ_MAX 0xFFFF is taken as 0xDFFF: a read past 0xDFFF is code 5 (before: the table's start again)");
		check(ask(&wide, READ, 0xDFF0, 0x10) == NO_ERROR && answerLen == 26 && answer[7] == 0xF0 && answer[22] == 0xFF,
				"D-6: up to 0xDFFF it reads");
		check(ask(&wide, WRITE_ACK, 0xDFFE, 4, w) == REG_CNT_OUT_OF_RANGE && whole[0] == 0 && whole[0xFFE] == 0xFE,
				"D-6: a write past 0xDFFF: code 5, nothing stored");
	}

	/* D-7 */
	{
		static uint8_t mem[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		static const evre_range_t unsorted[] = { { 0xD004, 4, mem + 4, 1 }, { 0xD000, 4, mem, 1 } };
		evre_base_t bad;
		bad.SALVE_ID_REG = 1;
		bad.D_RANGES = unsorted;
		bad.D_RANGE_CNT = 2;
		check(protocolInit(&bad) == RANGE_TABLE_INVALID && bad.D_RANGE_CNT == 0 && bad.D_RANGES == unsorted,
				"D-7: a table protocolInit refuses: RANGE_TABLE_INVALID, and D_RANGE_CNT 0");
		check(ask(&bad, READ, 0xD004, 4) == REG_OFFSET_OUT_OF_RANGE && refusedWith(REG_OFFSET_OUT_OF_RANGE),
				"D-7: then a read of the bank is refused, even by a caller that ignored the code (before: served)");
		check(ask(&bad, WRITE_ACK, 0xD000, 4, w) == PERMISSION_DENIED && mem[0] == 1, "D-7: and a write: refused, nothing stored");
		check(ask(&bad, READ, 0xA000, 2) == NO_ERROR, "D-7: the reserved bank still answers");
	}

	/* D-9, D-10 */
	dev.CONFIG = 0;
	check(ask(&dev, WRITE_ACK_RESP, 0xD008, 2) == FUNCTION_CODE_ERR && answerLen == 0 && dev.CONFIG == 0,
			"D-9: a device refuses a WRITE_ACK_RESP: FUNCTION_CODE_ERR, silent, no HEARTBEAT (before: accepted)");
	{
		evre_base_t mirror;
		mirror.SALVE_ID_REG = 1;
		mirror.D_RANGES = table;
		mirror.D_RANGE_CNT = 2;
		check(protocolInit(&mirror) == NO_ERROR, "D-9: a mirror");
		mirror.ACCEPT_READ_RESP = 1;
		check(ask(&mirror, WRITE_ACK_RESP, 0xA007, 3) == NO_ERROR && answerLen == 0,
				"D-9: a mirror with an empty queue takes the device's ack of three messages (before: code 5, from its own queue)");
		check(ask(&mirror, WRITE_ACK_RESP, 0xD000, 4) == NO_ERROR && answerLen == 0,
				"D-9: a mirror takes the ack of a write to what is read-only in its own table (before: code 3)");
		check(ask(&mirror, WRITE_ACK_RESP, 0xE000, 4) == PERMISSION_DENIED && answerLen == 0,
				"D-9: an ack for an unknown bank: PERMISSION_DENIED, silent");
		check(mirror.CONFIG == 0, "D-10: a mirror's CONFIG: no HEARTBEAT for a WRITE_ACK_RESP");
		const uint8_t reported[2] = { 0x00, 0x4F };
		check(ask(&mirror, READ_RESP, 0xA004, 2, reported) == NO_ERROR && mirror.CONFIG == 0x4F00,
				"D-10: a mirror's CONFIG is what the device reported, bit 0 too (before: 0x4F01)");
		uint8_t whole[0x106];
		for (unsigned i = 0; i < sizeof whole; ++i) whole[i] = uint8_t(0x30 + i);
		const bool taken = ask(&mirror, READ_RESP, 0xA000, 0x106, whole) == NO_ERROR && answerLen == 0;
		check(taken && mirror.DEVICE_ID == 0x3130 && mirror.STATUS == 0x3332 && mirror.CONFIG == 0x3534 && mirror.MSG_CNT == 0x36
				&& std::memcmp(mirror.MSG_BUFFER, whole + 7, sizeof mirror.MSG_BUFFER) == 0,
				"D-9, D-10: a mirror's READ_RESP of 0xA000 x0x106 stores every byte as it came: DEVICE_ID, STATUS, CONFIG, MSG_CNT, "
				"the whole queue");
	}
	dev.CONFIG = 0;
	check(ask(&dev, READ, 0xD000, 1) == NO_ERROR && dev.CONFIG == 1, "D-10: an accepted READ sets HEARTBEAT");
	dev.CONFIG = 0;
	check(ask(&dev, WRITE, 0xD008, 1, w) == NO_ERROR && dev.CONFIG == 1, "D-10: an accepted WRITE sets it");
	dev.CONFIG = 0;
	dev.ACCEPT_BROADCAST_D000 = 1;
	check(ask(&dev, WRITE, 0xD008, 1, w, 0) == NO_ERROR && dev.CONFIG == 1, "D-10: a broadcast WRITE too");
	dev.ACCEPT_BROADCAST_D000 = 0;
	dev.CONFIG = 0;
	const uint8_t cfgLow[1] = { 0x10 };
	check(ask(&dev, WRITE, 0xA004, 1, cfgLow, 0) == NO_ERROR && dev.CONFIG == 0x11,
			"D-10, D-21: a broadcast WRITE to CONFIG sets it, ACCEPT_BROADCAST_D000 0 or not");
	dev.CONFIG = 0;
	check(ask(&dev, WRITE_ACK, 0xD008, 1, w) == NO_ERROR && dev.CONFIG == 1, "D-10: an accepted WRITE_ACK too");
	dev.CONFIG = 0;
	check(ask(&dev, READ, 0xD00F, 2) == REG_CNT_OUT_OF_RANGE && dev.CONFIG == 0, "D-10: a refused request does not");

	/* D-11 */
	check(askDelimited(&dev, READ, 0xD000, 4, nullptr, 0x7B, 0x7D) == NO_ERROR && answerLen == 14, "D-11: 0x7B .. 0x7D: answered");
	check(askDelimited(&dev, READ, 0xD000, 4, nullptr, 0x00, 0x7D) == INVALID_PACKET_ERR && answerLen == 0,
			"D-11: a READ with start byte 0x00, its CRC made for it: INVALID_PACKET_ERR, silent (before: answered)");
	check(askDelimited(&dev, READ, 0xD000, 4, nullptr, 0x7B, 0x00) == INVALID_PACKET_ERR && answerLen == 0,
			"D-11: a READ with end byte 0x00: INVALID_PACKET_ERR, silent (before: answered)");
	std::memset(rw, 0, sizeof rw);
	check(askDelimited(&dev, WRITE_ACK, 0xD008, 4, w, 0x7B, 0x7C) == INVALID_PACKET_ERR && answerLen == 0 && rw[0] == 0,
			"D-11: a WRITE_ACK with a wrong end byte: nothing stored");

	/* D-14 */
	len = 0;
	check(encodePacketInto(&dev, BROADCAST_ID, READ, 0xD000, 4, nullptr, out, sizeof out, &len) == FUNCTION_CODE_ERR && len == 0,
			"D-14: the encoder builds no broadcast READ: FUNCTION_CODE_ERR (before: built)");
	check(encodePacketInto(&dev, BROADCAST_ID, WRITE_ACK, 0xD008, 2, (uint8_t*) w, out, sizeof out, &len) == FUNCTION_CODE_ERR && len == 0,
			"D-14: no broadcast WRITE_ACK");
	check(encodePacketInto(&dev, BROADCAST_ID, READ_RESP, 0xD000, 2, nullptr, out, sizeof out, &len) == FUNCTION_CODE_ERR && len == 0,
			"D-14: no broadcast READ_RESP");
	check(encodePacketInto(&dev, BROADCAST_ID, WRITE, 0xD008, 2, (uint8_t*) w, out, sizeof out, &len) == NO_ERROR && len == 12
			&& out[SLAVE_ID] == 0, "D-14: a broadcast WRITE is built");
	uint8_t *packet = (uint8_t*) out;
	uint16_t size = 0xBEEF;
	check(encodePacket(&dev, BROADCAST_ID, READ, 0xD000, 4, nullptr, &packet, &size) == FUNCTION_CODE_ERR && packet == nullptr && size == 0,
			"D-14: nor by the allocating encoder");

	/* D-16 */
	{
		evre_base_t fresh;
		check(fresh.ACCEPT_READ_RESP == 0,
				"D-16: ACCEPT_READ_RESP is 0 by default: a device takes no response; a host's mirror sets 1 (EVRe.h, MIGRATING FROM 1.0)");
	}

	/* D-17 */
	check(LOGIN_REQUIRED == 13 && RANGE_TABLE_INVALID == 14, "D-17: LOGIN_REQUIRED is 13, RANGE_TABLE_INVALID 14");
	dev.READ_HANDLER = loginFirst;
	check(ask(&dev, READ, 0xD000, 4) == LOGIN_REQUIRED && refusedWith(LOGIN_REQUIRED),
			"D-17: a handler's LOGIN_REQUIRED goes on the wire as 13");
	dev.READ_HANDLER = nullptr;
}

/* D-2: every function code, decoded in place (outBuf == PACKET), answers and
 * stores exactly as with two buffers */
static uint8_t placeRo[4] = { 1, 2, 3, 4 }, placeRw[8];

struct Outcome {
	uint8_t ret;
	uint16_t len;
	uint8_t bytes[32];
	uint8_t rw[8];
	uint16_t config;
	uint8_t msgCnt;
	uint8_t msg[4];
};

static void placeReset(evre_base_t &dev, uint8_t mirror) {
	std::memset(placeRw, 0, sizeof placeRw);
	dev.CONFIG = 0;
	dev.MSG_CNT = 0;
	std::memset(dev.MSG_BUFFER, 0, sizeof dev.MSG_BUFFER);
	addMsg(&dev, 5);
	addMsg(&dev, 6);
	dev.ACCEPT_READ_RESP = mirror;
}

static void record(Outcome &o, const uint8_t *answerBytes, const evre_base_t &dev) {
	std::memset(o.bytes, 0, sizeof o.bytes);
	std::memcpy(o.bytes, answerBytes, o.len < sizeof o.bytes ? o.len : sizeof o.bytes);
	std::memcpy(o.rw, placeRw, sizeof o.rw);
	o.config = dev.CONFIG;
	o.msgCnt = dev.MSG_CNT;
	std::memcpy(o.msg, dev.MSG_BUFFER, sizeof o.msg);
}

static bool sameOutcome(const Outcome &a, const Outcome &b) {
	return a.ret == b.ret && a.len == b.len && std::memcmp(a.bytes, b.bytes, sizeof a.bytes) == 0
			&& std::memcmp(a.rw, b.rw, sizeof a.rw) == 0 && a.config == b.config && a.msgCnt == b.msgCnt
			&& std::memcmp(a.msg, b.msg, sizeof a.msg) == 0;
}

static void inPlace() {
	static const evre_range_t table[] = { { 0xD000, 4, placeRo, 0 }, { 0xD004, 8, placeRw, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 2;
	check(protocolInit(&dev) == NO_ERROR, "D-2: in place: protocolInit");
	struct Case { const char *what; uint8_t fn; uint16_t off, cnt, dataLen; uint8_t mirror; };
	const Case cases[] = {
		{ "a READ", READ, 0xD000, 12, 0, 0 },
		{ "a READ of the reserved bank", READ, 0xA000, 9, 0, 0 },
		{ "a WRITE", WRITE, 0xD004, 8, 8, 0 },
		{ "a WRITE_ACK", WRITE_ACK, 0xD004, 8, 8, 0 },
		{ "a WRITE_ACK to CONFIG", WRITE_ACK, 0xA004, 2, 2, 0 },
		{ "a WRITE_ACK that acknowledges a message", WRITE_ACK, 0xA007, 1, 1, 0 },
		{ "a READ_RESP to a mirror", READ_RESP, 0xD000, 12, 12, 1 },
		{ "a READ_RESP to a device", READ_RESP, 0xD004, 4, 4, 0 },
		{ "a WRITE_ACK_RESP to a mirror", WRITE_ACK_RESP, 0xD004, 4, 0, 1 },
		{ "a refused READ", READ, 0xD00C, 4, 0, 0 },
		{ "a refused WRITE_ACK", WRITE_ACK, 0xD000, 4, 4, 0 },
		{ "an unknown function code", 0x55, 0xD000, 2, 2, 0 },
		{ "an ERROR_RESP", ERROR_RESP, 0xD000, 4, 1, 0 },
	};
	const uint8_t data[12] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC };
	for (const Case &t : cases) {
		Outcome apart, together;
		uint8_t buf[64], out[64];
		placeReset(dev, t.mirror);
		uint16_t n = frame(buf, 1, t.fn, t.off, t.cnt, data, t.dataLen);
		apart.len = 0;
		apart.ret = decodePacketInto(&dev, buf, n, out, sizeof out, &apart.len);
		record(apart, out, dev);
		placeReset(dev, t.mirror);
		n = frame(buf, 1, t.fn, t.off, t.cnt, data, t.dataLen);
		together.len = 0;
		together.ret = decodePacketInto(&dev, buf, n, buf, sizeof buf, &together.len);
		record(together, buf, dev);
		char what[128];
		std::snprintf(what, sizeof what, "D-2: %s decoded in place: the answer and the memory as with two buffers", t.what);
		check(sameOutcome(apart, together), what);
	}
}

#ifdef WRAP_CALLOC
/* D-4: decodePacket's allocations, seen through -Wl,--wrap=calloc */
static size_t lastCalloc;
static size_t callocLimit = (size_t) -1;
extern "C" void *__real_calloc(size_t count, size_t size);
extern "C" void *__wrap_calloc(size_t count, size_t size) {
	lastCalloc = count * size;
	return (count * size > callocLimit) ? nullptr : __real_calloc(count, size);
}

static void allocation() {
	static uint8_t regs[0x100];
	static uint8_t *table[0x100];
	for (unsigned i = 0; i < 0x100; ++i) table[i] = &regs[i];
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	check(protocolInit(&dev) == NO_ERROR, "D-4: protocolInit");
	dev.DEVICE_REG_READ_MAX = 0xD0FF;
	dev.D000 = table;
	uint8_t req[16];
	uint8_t *resp = nullptr;
	uint16_t rsize = 0;

	uint16_t n = frame(req, 1, READ, 0xD000, 0xFFFF, nullptr, 0);
	req[n - 3] ^= 0x5A;
	check(decodePacket(&dev, req, n, &resp, &rsize) == INVALID_PACKET_ERR && resp == nullptr && lastCalloc == 11,
			"D-4: a READ x0xFFFF with a bad CRC: 11 bytes of heap (before: 64 KB)");
	n = frame(req, 2, READ, 0xD000, 0xFFFF, nullptr, 0);
	check(decodePacket(&dev, req, n, &resp, &rsize) == SLAVE_ID_MISMATCHED && resp == nullptr && lastCalloc == 11,
			"D-4: a READ x0xFFFF for another slave: 11 bytes (before: 64 KB)");
	n = frame(req, 1, READ, 0xD000, 0x2000, nullptr, 0);
	check(decodePacket(&dev, req, n, &resp, &rsize) == REG_CNT_OUT_OF_RANGE && rsize == 11 && resp != nullptr
			&& resp[7] == REG_CNT_OUT_OF_RANGE && lastCalloc == 10 + 0x1000,
			"D-4: our READ x0x2000: one bank at most, 10 + 0x1000 bytes, and its code 5");
	std::free(resp);
	n = frame(req, 1, READ, 0xD000, 0x100, nullptr, 0);
	check(decodePacket(&dev, req, n, &resp, &rsize) == NO_ERROR && rsize == 0x10A && lastCalloc == 0x10A,
			"D-4: our READ x0x100: sized by its count");
	std::free(resp);

	callocLimit = 64;
	check(decodePacket(&dev, req, n, &resp, &rsize) == BUFFER_TOO_SMALL && rsize == 11 && resp != nullptr && resp[2] == ERROR_RESP
			&& resp[7] == BUFFER_TOO_SMALL && lastCalloc == 11,
			"D-4: no heap for the READ_RESP: 11 bytes after all, and the host hears BUFFER_TOO_SMALL (before: MEM_ALLOCATION_FAILED, silent)");
	std::free(resp);
	n = frame(req, 1, READ, 0xD000, 0x2000, nullptr, 0);
	check(decodePacket(&dev, req, n, &resp, &rsize) == REG_CNT_OUT_OF_RANGE && rsize == 11 && resp != nullptr
			&& resp[7] == REG_CNT_OUT_OF_RANGE, "D-4: on a small heap a READ x0x2000 still gets its code 5");
	std::free(resp);
	callocLimit = 8;
	check(decodePacket(&dev, req, n, &resp, &rsize) == MEM_ALLOCATION_FAILED && resp == nullptr && rsize == 0,
			"D-4: no heap at all: MEM_ALLOCATION_FAILED");
	callocLimit = (size_t) -1;
}
#endif

#ifdef EVRE_TEST_LOCK_HOOKS
/* D-15: lock_hooks.h makes EVRE_LOCK and EVRE_UNLOCK count */
static unsigned locks, unlocks, depth, deepest, depthInHandler;
void evre_test_lock(void) {
	++locks;
	++depth;
	if (depth > deepest) {
		deepest = depth;
	}
}
void evre_test_unlock(void) {
	++unlocks;
	--depth;
}
static evre_base_t *lockDevice;
static void queueFromHandler() {
	depthInHandler = depth;
	addMsg(lockDevice, 0x42);
}

static void lockHooks() {
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	check(protocolInit(&dev) == NO_ERROR, "D-15: protocolInit");
	lockDevice = &dev;
	locks = unlocks = deepest = 0;
	check(addMsg(&dev, 5) == NO_ERROR && locks == 1 && unlocks == 1 && depth == 0, "D-15: addMsg takes the lock once and gives it back");
	dev.MSG_CNT = 255;
	locks = unlocks = 0;
	check(addMsg(&dev, 6) == MSG_BUFFER_FULL && locks == 1 && unlocks == 1, "D-15: a full queue: the lock given back too");
	dev.MSG_CNT = 1;
	const uint8_t zero[1] = { 0 };
	dev.MSG_ACK_HANDLER[5] = queueFromHandler;
	locks = unlocks = deepest = 0;
	depthInHandler = 99;
	check(ask(&dev, WRITE_ACK, 0xA007, 1, zero) == NO_ERROR && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 0x42 && locks == 2
			&& unlocks == 2 && deepest == 1 && depthInHandler == 0,
			"D-15: an ack: the compaction under the lock, the handler outside it (its own addMsg kept), never nested");
	dev.MSG_ACK_HANDLER[0] = queueFromHandler;
	locks = unlocks = deepest = 0;
	depthInHandler = 99;
	check(ask(&dev, WRITE, 0xA006, 1, zero) == NO_ERROR && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 0x42 && locks == 2
			&& unlocks == 2 && deepest == 1 && depthInHandler == 0, "D-15: a clear under the lock, MSG_ACK_HANDLER[0] outside it");
	const uint8_t zeros[2] = { 0, 0 };
	locks = unlocks = deepest = 0;
	check(ask(&dev, WRITE, 0xA006, 2, zeros) == NO_ERROR && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 0x42 && locks == 2 && unlocks == 2
			&& deepest == 1, "D-19: MSG_CNT and slot 0 in one write: the clear's lock and the handler's addMsg, no compaction "
			"(before: 3 locks, and 0x42 acknowledged at once)");
	locks = 0;
	check(ask(&dev, READ, 0xA006, 2) == NO_ERROR && locks == 0, "D-15: a READ takes no lock");
}

static unsigned lockDepth() {
	return depth;
}
#else
static unsigned lockDepth() {
	return 0;
}

static void lockHooks() {
	EVRE_LOCK();
	EVRE_UNLOCK();
	check(true, "D-15: EVRE_LOCK() and EVRE_UNLOCK() are there, empty by default (the lock build checks where they are taken)");
}
#endif

/* D-18: EVRe Guard */
static void guardDecisions() {
	static const evre_guard_config_t base = { 0xD010, 16, token, openSpan, 1, 3, 1000, 8000, 5000, fakeClock };
	static const evre_guard_config_t anyLimit = { 0xD010, 16, token, openSpan, 1, 3, 1000, 0xFFFFFFFFu, 0xFFFFFFFFu, fakeClock };
	evre_base_t dev;
	guardedDevice(dev);
	const uint8_t value[4] = { 9, 9, 9, 9 };

	clockMs = 1000;
	check(evre_guard_init(&guard, &base) == NO_ERROR, "D-18: evre_guard_init: NO_ERROR for a good config");
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	clockMs += 0x80000000u + 100;
	check(ask(&dev, READ, 0xD000, 8) == LOGIN_REQUIRED && !guard.logged_in,
			"D-18: 2^31 ms and more without a frame: the session is over (before: it came back)");

	evre_guard_init(&guard, &base);
	for (int i = 0; i < 3; ++i) ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	clockMs += 0x80000000u + 5000;
	check(ask(&dev, WRITE_ACK, 0xD010, 16, token) == NO_ERROR && guard.logged_in,
			"D-18: a lockout long over stays over after 2^31 ms: the right token logs in (before: refused)");

	evre_guard_init(&guard, &anyLimit);
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	clockMs += 1;
	check(ask(&dev, READ, 0xD000, 8) == NO_ERROR && guard.logged_in,
			"D-18: idle_logout_ms 0xFFFFFFFF: the session goes on (before: over at once)");
	evre_guard_init(&guard, &anyLimit);
	bool waited = true;
	for (unsigned failure = 1; failure <= 40; ++failure) {
		/* the device looks once a day until the lockout is over (50 days at most) */
		for (unsigned day = 0; guard.locked && day < 60; ++day) {
			clockMs += DAY_MS;
			evre_guard_logged_in(&guard);
		}
		ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
		if (failure >= 3) {
			waited = waited && ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED && !guard.logged_in;
		}
	}
	check(waited && guard.failures == 40 && guard.lock_left == 0xFFFFFFFFu,
			"D-18: lockout_max_ms 0xFFFFFFFF: after each of 40 failures the next attempt waits (before: none from failure 25 on)");

	evre_guard_init(&guard, &base);
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	uint8_t block[24];
	std::memset(block, 0x33, sizeof block);
	std::memset(rw8, 0, sizeof rw8);
	check(ask(&dev, WRITE_ACK, 0xD008, 24, block) == PERMISSION_DENIED && refusedWith(PERMISSION_DENIED) && guard.logged_in
			&& guard.failures == 0 && rw8[0] == 0,
			"D-18: in a session, a block write over the login register: refused, the session goes on, no failure (before: logged out, 1 failure)");
	check(ask(&dev, WRITE_ACK, 0xD008, 4, value) == NO_ERROR && rw8[0] == 9, "D-18: and the next write is taken");
	evre_guard_logout(&guard);
	check(ask(&dev, WRITE_ACK, 0xD008, 24, block) == PERMISSION_DENIED && guard.failures == 1,
			"D-18: without a session the same write is a failed attempt, as before");

	evre_guard_init(&guard, &base);
	evre_guard_restore(&guard, 5, clockMs);
	check(guard.failures == 5 && ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED && !guard.logged_in,
			"D-18: evre_guard_restore(5 failures): the lockout they imply runs, the right token is refused");
	clockMs += 3999;
	check(ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED, "D-18: for 4 s (1 s doubled twice)");
	clockMs += 1;
	check(ask(&dev, WRITE_ACK, 0xD010, 16, token) == NO_ERROR && guard.logged_in && guard.failures == 0,
			"D-18: then the right token logs in and clears the count");
	evre_guard_init(&guard, &base);
	evre_guard_restore(&guard, 2, clockMs);
	check(guard.failures == 2 && !guard.locked && ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken) == PERMISSION_DENIED && guard.locked,
			"D-18: evre_guard_restore(2 of 3 free): no lockout yet, the next wrong token starts it");

	/* a write of no bytes inside the login register touches nothing */
	evre_guard_init(&guard, &base);
	check(ask(&dev, WRITE_ACK, 0xD011, 0) == LOGIN_REQUIRED && guard.failures == 0,
			"Guard: a write of 0 bytes inside the login register, no session: LOGIN_REQUIRED, no failure (before: a failed attempt)");
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	check(ask(&dev, WRITE_ACK, 0xD011, 0) == NO_ERROR && guard.logged_in,
			"Guard: in a session: taken, like any write of 0 bytes (before: refused)");
}

/* D-22: the guard's clock has 64 bits. The looks may be any time apart, a
 * value not newer than the newest one seen is no time, and a step
 * saturates. */
static bool rightTokenLogsIn(evre_base_t &dev) {
	return ask(&dev, WRITE_ACK, 0xD010, 16, token) == NO_ERROR && answer[2] == WRITE_ACK_RESP && guard.logged_in;
}

static bool rightTokenRefused(evre_base_t &dev) {
	return ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED && refusedWith(PERMISSION_DENIED) && !guard.logged_in;
}

static void guardTime() {
	static const evre_guard_config_t base = { 0xD010, 16, token, openSpan, 1, 3, 1000, 8000, 5000, fakeClock };
	static const evre_guard_config_t longest = { 0xD010, 16, token, openSpan, 1, 3, 1000, 0xFFFFFFFFu, 0xFFFFFFFFu, fakeClock };
	static const evre_guard_config_t zeros = { 0xD010, 16, token, openSpan, 1, 3, 0, 0, 0, fakeClock };
	const uint64_t WRAP = 0x100000000ULL; /* where a 32-bit clock wraps */
	const uint64_t HOURS2 = 7200000u;
	evre_base_t dev;
	guardedDevice(dev);

	clockMs = 50000000u;
	evre_guard_init(&guard, &base);
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	clockMs += 4000;
	ask(&dev, READ, 0xD000, 8);
	const uint64_t seen = clockMs;
	bool late = true;
	const uint64_t older[] = { seen - 3000, seen - 3600000u - 1u, seen - 1, 0 };
	for (uint64_t value : older) {
		clockMs = value;
		late = late && evre_guard_logged_in(&guard) && guard.idle_ms == 0 && guard.seen_ms == seen;
	}
	check(late, "D-22: a clock value older than the newest seen (by 3 s, by an hour and 1 ms, by 1 ms, back to 0) counts as no time, "
			"and seen_ms stays (before: more than an hour older read as 49 days ahead)");

	clockMs = seen - HOURS2;
	evre_guard_logged_in(&guard);
	clockMs = seen - 1000;
	const bool stood = evre_guard_logged_in(&guard) && guard.idle_ms == 0;
	clockMs = seen + 4999;
	const bool counted = evre_guard_logged_in(&guard) && guard.idle_ms == 4999;
	clockMs = seen + 5000;
	check(stood && counted && !evre_guard_logged_in(&guard) && ask(&dev, READ, 0xD000, 8) == LOGIN_REQUIRED
			&& refusedWith(LOGIN_REQUIRED),
			"D-22: a clock set back 2 hours stands still until it is back where it was, then counts on: the session ends 5000 ms "
			"after its last read, LOGIN_REQUIRED (before: set back more than an hour, it ended at once)");

	evre_guard_init(&guard, &base);
	clockMs = 70000000u;
	for (int i = 0; i < 3; ++i) ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	clockMs -= HOURS2;
	const bool setBack = rightTokenRefused(dev) && guard.lock_left == 1000;
	clockMs += HOURS2 + 999;
	const bool oneLeft = rightTokenRefused(dev) && guard.lock_left == 1;
	clockMs += 1;
	check(setBack && oneLeft && rightTokenLogsIn(dev),
			"D-22: a lockout, the clock set back 2 hours: no time off until the clock catches up, then over 1000 ms after it began "
			"(before: over at once)");

	evre_guard_init(&guard, &base);
	clockMs = WRAP - 500;
	ask(&dev, WRITE_ACK, 0xD010, 16, token);
	clockMs = WRAP + 4499;
	const bool acrossOpen = evre_guard_logged_in(&guard) && guard.idle_ms == 4999;
	clockMs = WRAP + 4500;
	const bool acrossOver = !evre_guard_logged_in(&guard);
	clockMs = 2 * WRAP - 300;
	evre_guard_restore(&guard, 3, clockMs);
	clockMs = 2 * WRAP + 699;
	const bool acrossLocked = rightTokenRefused(dev);
	clockMs = 2 * WRAP + 700;
	check(acrossOpen && acrossOver && acrossLocked && rightTokenLogsIn(dev),
			"D-22: across 2^32 and 2^33 ms nothing is special: a session idles out after 5000 ms, a lockout of 1000 ms ends after 1000 ms");

	evre_guard_init(&guard, &longest);
	clockMs = 1000;
	evre_guard_restore(&guard, 40, clockMs);
	const bool full = guard.locked && guard.lock_left == 0xFFFFFFFFu;
	clockMs += 0xFFFFFFFEu;
	const bool lockOn = rightTokenRefused(dev) && guard.lock_left == 1;
	clockMs += 1;
	const bool lockOver = rightTokenLogsIn(dev);
	clockMs += 0xFFFFFFFEu;
	const bool sessionOn = evre_guard_logged_in(&guard) && guard.idle_ms == 0xFFFFFFFEu;
	clockMs += 1;
	check(full && lockOn && lockOver && sessionOn && !evre_guard_logged_in(&guard) && guard.idle_ms == 0xFFFFFFFFu,
			"D-22: lockout_max_ms and idle_logout_ms 0xFFFFFFFF, one look each: on for 0xFFFFFFFE ms, over at 0xFFFFFFFF "
			"(before: a step that large was a late read)");

	evre_guard_init(&guard, &longest);
	evre_guard_restore(&guard, 40, clockMs);
	clockMs += 2 * WRAP; /* 2^33 ms in one step: 0 in 32 bits */
	const bool jumpedLock = evre_guard_logged_in(&guard) == 0 && !guard.locked && guard.lock_left == 0 && rightTokenLogsIn(dev);
	clockMs += 1ULL << 40;
	check(jumpedLock && !evre_guard_logged_in(&guard) && guard.idle_ms == 0xFFFFFFFFu && ask(&dev, READ, 0xD000, 8) == LOGIN_REQUIRED,
			"D-22: one step of 2^33 ms ends a lockout of 0xFFFFFFFF ms, one of 2^40 ms a session with idle_logout_ms 0xFFFFFFFF: "
			"the step saturates (before: 2^33 ms was a step of 0)");

	evre_guard_init(&guard, &longest);
	evre_guard_restore(&guard, 40, clockMs);
	clockMs += 50 * DAY_MS;
	check(rightTokenLogsIn(dev), "D-22: the looks may be any time apart: the first look 50 days after a lockout of 0xFFFFFFFF ms "
			"began finds it over (before: 50 days read as 7 hours)");

	evre_guard_init(&guard, &zeros);
	for (int i = 0; i < 3; ++i) ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	const bool zeroLock = guard.failures == 3 && guard.locked && guard.lock_left == 0;
	const bool sameMs = rightTokenLogsIn(dev);
	clockMs += 1ULL << 40;
	check(zeroLock && sameMs && evre_guard_logged_in(&guard) && ask(&dev, READ, 0xD000, 8) == NO_ERROR,
			"D-22: lockout_ms, lockout_max_ms and idle_logout_ms 0: a lockout of 0 ms is over at the next look, in the same ms; "
			"no idle logout after 2^40 ms");

	const uint64_t far = (1ULL << 40) + 7;
	evre_guard_init(&guard, &base);
	evre_guard_restore(&guard, 5, far);
	clockMs = far + 3999;
	const bool restoredOn = guard.seen_ms == far && rightTokenRefused(dev);
	clockMs = far + 4000;
	const bool restoredOver = rightTokenLogsIn(dev);
	evre_guard_logout(&guard);
	evre_guard_restore(&guard, 3, far - 10000);
	clockMs = far + 4999;
	const bool olderOn = guard.seen_ms == far + 4000 && rightTokenRefused(dev);
	clockMs = far + 5000;
	check(restoredOn && restoredOver && olderOn && rightTokenLogsIn(dev),
			"D-22: evre_guard_restore with a 64-bit now: the lockout (4 s for 5 failures) runs from it; given a now older than the "
			"newest value seen, from that value");
	evre_guard_logout(&guard);
}

/* A main loop's evre_guard_logged_in() and the decoder's interrupt. The
 * interrupt lands between the main loop's clock read and its update, a tick
 * later: simulated inside the clock. */
static int interruptDoes; /* 1: a wrong token, 2: the right one */
static unsigned depthAtClock;
static const evre_base_t *raceDevice; /* as the decoder leaves it for the handler: a unicast frame */

static uint64_t interruptedClock() {
	const uint64_t now = clockMs;
	const unsigned depth = lockDepth();
	if (interruptDoes != 0) {
		const int what = interruptDoes;
		interruptDoes = 0;
		++clockMs;
		evre_guard_write(&guard, raceDevice, 0xD010, what == 1 ? wrongToken : token, 16);
	}
	depthAtClock = depth; /* after the interrupt's own read */
	return now; /* the value the main loop read, before the interrupt */
}

static void guardRace() {
	static const evre_guard_config_t cfg = { 0xD010, 16, token, openSpan, 1, 3, 60000, 600000, 5000, interruptedClock };
	evre_base_t dev;
	guardedDevice(dev);
	raceDevice = &dev;
	clockMs = 1000;
	evre_guard_init(&guard, &cfg);
	ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken);
	interruptDoes = 1;
	evre_guard_logged_in(&guard);
	clockMs += 1;
	check(guard.locked && guard.lock_left == 60000 && ask(&dev, WRITE_ACK, 0xD010, 16, token) == PERMISSION_DENIED && !guard.logged_in,
			"Guard: a lockout the interrupt starts inside the main loop's evre_guard_logged_in() runs on: the right token is refused "
			"(before this round: the lockout was lost)");

	clockMs += 100000;
	interruptDoes = 2;
	check(evre_guard_logged_in(&guard) == 1 && guard.logged_in,
			"Guard: a login the interrupt makes inside it stays (before this round: logged out at once)");
	evre_guard_logout(&guard);

#ifdef EVRE_TEST_LOCK_HOOKS
	/* with the lock a real interrupt cannot land there at all */
	locks = unlocks = deepest = 0;
	depthAtClock = 99;
	evre_guard_logged_in(&guard);
	const bool loggedInLocked = depthAtClock == 1 && locks == 1 && unlocks == 1 && deepest == 1;
	evre_guard_logout(&guard);
	evre_guard_restore(&guard, 0, clockMs);
	const bool othersLocked = locks == 3 && unlocks == 3 && deepest == 1 && depth == 0;
	locks = 0;
	depthAtClock = 99;
	ask(&dev, READ, 0xD000, 4);
	check(loggedInLocked && othersLocked && locks == 0 && depthAtClock == 0,
			"Guard: evre_guard_logged_in reads the clock under EVRE_LOCK; logout and restore take it too, never nested; the "
			"handlers take none");
#endif
}

/* D-1. Late: in the layout from before it, the ack below calls the write
 * handler through the wrong type. */
static unsigned ffAcks, countedWrites;
static void onAckFF() { ffAcks++; }
/* only counts: called through a void (*)(void) its arguments would be garbage */
static uint8_t countWrite(evre_base_t *, uint16_t, const uint8_t *, uint16_t) {
	countedWrites++;
	return NO_ERROR;
}

static void messageFF() {
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	const unsigned slots = sizeof dev.MSG_ACK_HANDLER / sizeof dev.MSG_ACK_HANDLER[0];
	for (unsigned i = 0; i < slots; ++i) dev.MSG_ACK_HANDLER[i] = onAckFF;
	check(protocolInit(&dev) == NO_ERROR && slots == 256, "D-1: MSG_ACK_HANDLER has 256 slots, one for every code");
	bool cleared = true;
	for (unsigned i = 0; i < slots; ++i) cleared = cleared && dev.MSG_ACK_HANDLER[i] == nullptr;
	check(cleared, "D-1: protocolInit clears all of them");
	dev.MSG_ACK_HANDLER[slots - 1] = onAckFF;
	dev.WRITE_HANDLER = countWrite;
	ffAcks = countedWrites = 0;
	const uint8_t zero[1] = { 0 };
	check(addMsg(&dev, 0xFF) == NO_ERROR && ask(&dev, WRITE_ACK, 0xA007, 1, zero) == NO_ERROR && dev.MSG_CNT == 0 && ffAcks == 1
			&& countedWrites == 1,
			"D-1: acknowledging message 0xFF runs MSG_ACK_HANDLER[0xFF] once, the write handler once, for the write (before: twice)");
}

/* ------------------------------------------ round 3 decisions, one each */

/* D-19: a write that clears the queue and runs on into the slots is the clear
 * alone. */
static evre_base_t *clearing;
static unsigned clears, acks1, acks9;
static bool requeueOnClear;
static void onClearing() {
	clears++;
	if (requeueOnClear) {
		addMsg(clearing, 9); /* a fault that still stands, raised again */
	}
}
static void onAck1() { acks1++; }
static void onAck9() { acks9++; }

static void queueOf(evre_base_t &dev, const uint8_t *codes, unsigned n) {
	dev.MSG_CNT = 0;
	std::memset(dev.MSG_BUFFER, 0, sizeof dev.MSG_BUFFER);
	for (unsigned i = 0; i < n; ++i) {
		addMsg(&dev, codes[i]);
	}
	clears = acks1 = acks9 = 0;
}

struct AfterClear {
	uint8_t ret, cnt, queue[4];
	unsigned clears, acks1, acks9;
	uint16_t config;
};

static bool sameAfterClear(const AfterClear &a, const AfterClear &b) {
	return a.ret == b.ret && a.cnt == b.cnt && std::memcmp(a.queue, b.queue, sizeof a.queue) == 0 && a.clears == b.clears
			&& a.acks1 == b.acks1 && a.acks9 == b.acks9 && a.config == b.config;
}

static void clearWins() {
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	check(protocolInit(&dev) == NO_ERROR, "D-19: protocolInit");
	clearing = &dev;
	dev.MSG_ACK_HANDLER[0] = onClearing;
	dev.MSG_ACK_HANDLER[1] = onAck1;
	dev.MSG_ACK_HANDLER[9] = onAck9;
	const uint8_t oneTwo[2] = { 1, 2 }, oneTwoThree[3] = { 1, 2, 3 };
	const uint8_t bytes[4] = { 0x10, 0x20, 0xAA, 0xBB };

	queueOf(dev, oneTwo, 2);
	requeueOnClear = true;
	check(ask(&dev, WRITE_ACK, 0xA006, 2, bytes) == NO_ERROR && answer[2] == WRITE_ACK_RESP && answer[3] == 0x06 && answer[5] == 2
			&& clears == 1 && acks1 == 0 && acks9 == 0 && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 9 && dev.MSG_BUFFER[1] == 0,
			"D-19: MSG_CNT and slot 0 in one write, MSG_ACK_HANDLER[0] queues 9: [0] runs once, 9 is kept, the write is acknowledged "
			"as sent (before: the slot byte acknowledged 9 at once, and it was lost)");
	requeueOnClear = false;

	queueOf(dev, oneTwoThree, 3);
	check(ask(&dev, WRITE, 0xA006, 4, bytes) == NO_ERROR && clears == 1 && acks1 == 0 && dev.MSG_CNT == 0 && dev.MSG_BUFFER[0] == 0
			&& dev.MSG_BUFFER[2] == 0,
			"D-19: MSG_CNT and three slots: MSG_ACK_HANDLER[0] once, no other handler, the queue empty (before: [0] four times)");

	queueOf(dev, oneTwo, 1);
	dev.CONFIG = 0;
	check(ask(&dev, WRITE_ACK, 0xA004, 4, bytes) == NO_ERROR && dev.CONFIG == 0x2011 && clears == 1 && acks1 == 0 && dev.MSG_CNT == 0,
			"D-19: CONFIG, MSG_CNT and slot 0 in one write: CONFIG written, then the clear alone");

	AfterClear alone, together;
	for (int run = 0; run < 2; ++run) {
		AfterClear &after = run ? together : alone;
		queueOf(dev, oneTwoThree, 3);
		requeueOnClear = true;
		dev.CONFIG = 0;
		after.ret = ask(&dev, WRITE, 0xA006, run ? 4 : 1, bytes);
		after.cnt = dev.MSG_CNT;
		std::memcpy(after.queue, dev.MSG_BUFFER, sizeof after.queue);
		after.clears = clears;
		after.acks1 = acks1;
		after.acks9 = acks9;
		after.config = dev.CONFIG;
	}
	requeueOnClear = false;
	check(sameAfterClear(alone, together) && together.cnt == 1 && together.queue[0] == 9,
			"D-19: MSG_CNT and three slots leave what MSG_CNT alone leaves: the queue, CONFIG, the handlers that ran");

	queueOf(dev, oneTwo, 2);
	requeueOnClear = true;
	check(ask(&dev, WRITE, 0xA006, 2, bytes, 0) == NO_ERROR && answerLen == 0 && clears == 1 && acks9 == 0 && dev.MSG_CNT == 1
			&& dev.MSG_BUFFER[0] == 9, "D-19: the same by broadcast: the clear alone, 9 kept, silent");
	requeueOnClear = false;

	queueOf(dev, oneTwo, 2);
	check(ask(&dev, WRITE, 0xA007, 1, bytes) == NO_ERROR && acks1 == 1 && clears == 0 && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 2,
			"D-19: a write of slots alone still acknowledges them");

	dev.WRITE_HANDLER = onWrite;
	writeVerdict = NO_ERROR;
	writeCalls = 0;
	queueOf(dev, oneTwo, 2);
	check(ask(&dev, WRITE_ACK, 0xA006, 3, bytes) == NO_ERROR && writeCalls == 1 && lastOffset == 0xA006 && lastCount == 3
			&& clears == 1 && acks1 == 0, "D-19: the write handler is asked once, for the write as sent");
	dev.WRITE_HANDLER = nullptr;
}

/* D-21: a broadcast reaches the reserved bank only, unless the device takes
 * it into its bank too */
static void broadcastBank(bool withRanges) {
	static uint8_t ro[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rw[8];
	static const evre_range_t table[] = { { 0xD000, 8, ro, 0 }, { 0xD008, 8, rw, 1 } };
	static uint8_t *pointers[16];
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	check(protocolInit(&dev) == NO_ERROR, "D-21: protocolInit");
	if (withRanges) {
		dev.D_RANGES = table;
		dev.D_RANGE_CNT = 2;
	} else {
		dev.DEVICE_REG_READ_MAX = 0xD00F;
		dev.DEVICE_REG_WRITE_MIN = 0xD008;
		for (unsigned i = 0; i < 8; ++i) {
			pointers[i] = &ro[i];
			pointers[8 + i] = &rw[i];
		}
		dev.D000 = pointers;
	}
	dev.WRITE_HANDLER = onWrite;
	writeVerdict = NO_ERROR;
	const char *path = withRanges ? "ranges" : "pointers";
	char what[192];
#define CHECK(ok, text) do { std::snprintf(what, sizeof what, "D-21 (%s): %s", path, text); check(ok, what); } while (0)

	const uint8_t w[4] = { 0x11, 0x22, 0x33, 0x44 };
	std::memset(rw, 0, sizeof rw);
	writeCalls = 0;
	dev.CONFIG = 0;
	CHECK(dev.ACCEPT_BROADCAST_D000 == 0 && ask(&dev, WRITE, 0xD008, 4, w, 0) == PERMISSION_DENIED && answerLen == 0 && rw[0] == 0
			&& writeCalls == 0 && dev.CONFIG == 0,
			"ACCEPT_BROADCAST_D000 0, the default: a broadcast WRITE into the device bank is PERMISSION_DENIED, silent, the handler "
			"not asked, nothing stored, no HEARTBEAT (before: stored)");
	CHECK(ask(&dev, WRITE, 0xD008, 0, nullptr, 0) == PERMISSION_DENIED && writeCalls == 0, "one of 0 bytes too");
	CHECK(ask(&dev, WRITE, 0xD00E, 4, w, 0) == PERMISSION_DENIED && writeCalls == 0,
			"judged before the bank's limits: past the bank it is PERMISSION_DENIED too");
	CHECK(askRaw(&dev, WRITE, 0xD008, 4, w, 3, 0) == LENGTH_MISMATCH && answerLen == 0 && writeCalls == 0,
			"a frame of the wrong length: LENGTH_MISMATCH, the length is checked first");
	CHECK(ask(&dev, WRITE, 0xD008, 4, w) == NO_ERROR && rw[0] == 0x11 && writeCalls == 1, "a unicast WRITE there is taken");
	std::memset(rw, 0, sizeof rw);
	const uint8_t cfg[2] = { 0x10, 0x00 };
	dev.CONFIG = 0;
	writeCalls = 0;
	CHECK(ask(&dev, WRITE, 0xA004, 2, cfg, 0) == NO_ERROR && answerLen == 0 && dev.CONFIG == 0x11 && writeCalls == 1,
			"a broadcast WRITE to CONFIG is taken: the reserved bank takes broadcasts");
	addMsg(&dev, 5);
	addMsg(&dev, 6);
	CHECK(ask(&dev, WRITE, 0xA007, 1, cfg, 0) == NO_ERROR && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 6, "a broadcast ack is taken");
	CHECK(ask(&dev, WRITE, 0xA006, 1, cfg, 0) == NO_ERROR && dev.MSG_CNT == 0, "a broadcast clear is taken");
	uint8_t out[32];
	uint16_t len = 0;
	CHECK(encodePacketInto(&dev, BROADCAST_ID, WRITE, 0xD008, 4, (uint8_t*) w, out, sizeof out, &len) == NO_ERROR && len == 14,
			"the encoder still builds a broadcast WRITE into the device bank: it cannot know the target's setting");
	CHECK((dev.STATUS & CAP_BROADCAST) != 0, "STATUS still says CAP_BROADCAST");

	dev.ACCEPT_BROADCAST_D000 = 1;
	dev.CONFIG = 0;
	writeCalls = 0;
	lastRx = 0xEE;
	CHECK(ask(&dev, WRITE, 0xD008, 4, w, 0) == NO_ERROR && answerLen == 0 && rw[0] == 0x11 && rw[3] == 0x44 && writeCalls == 1
			&& lastRx == 0 && dev.CONFIG == 1,
			"ACCEPT_BROADCAST_D000 1: the device bank takes it as in 1.0: the handler asked, stored, HEARTBEAT, silent");
	CHECK(ask(&dev, WRITE, 0xD000, 4, w, 0) == PERMISSION_DENIED && ro[0] == 1, "with 1, a read-only register still refuses it");
	writeCalls = 0;
	CHECK(ask(&dev, WRITE, 0xD00E, 4, w, 0) == REG_CNT_OUT_OF_RANGE && writeCalls == 0, "with 1, past the bank: code 5, as in 1.0");
	dev.ACCEPT_BROADCAST_D000 = 0;
#undef CHECK
}

/* D-23: a handler knows the frame it runs for */
static void rxSlaveId() {
	static uint8_t rw[8];
	static const evre_range_t table[] = { { 0xD000, 8, rw, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 7;
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 1;
	check(protocolInit(&dev) == NO_ERROR, "D-23: protocolInit");
	dev.WRITE_HANDLER = onWrite;
	dev.READ_HANDLER = onRead;
	dev.ACCEPT_BROADCAST_D000 = 1;
	writeVerdict = readVerdict = NO_ERROR;
	const uint8_t w[2] = { 1, 2 };
	lastRx = 0xEE;
	const bool read = ask(&dev, READ, 0xD000, 2, nullptr, 7) == NO_ERROR && lastRx == 7;
	lastRx = 0xEE;
	const bool write = ask(&dev, WRITE_ACK, 0xD000, 2, w, 7) == NO_ERROR && lastRx == 7;
	lastRx = 0xEE;
	const bool broadcast = ask(&dev, WRITE, 0xD000, 2, w, 0) == NO_ERROR && lastRx == 0;
	lastRx = 0xEE;
	const bool config = ask(&dev, WRITE, 0xA004, 2, w, 0) == NO_ERROR && lastRx == 0;
	lastRx = 0xEE;
	const bool plain = ask(&dev, WRITE, 0xD000, 2, w, 7) == NO_ERROR && lastRx == 7;
	dev.ACCEPT_READ_RESP = 1;
	lastRx = 0xEE;
	const bool mirror = ask(&dev, READ_RESP, 0xD000, 2, w, 7) == NO_ERROR && lastRx == 7;
	check(read && write && broadcast && config && plain && mirror,
			"D-23: RX_SLAVE_ID while a handler runs: 7 for a READ, a WRITE_ACK, a WRITE and a READ_RESP to slave 7, 0 for a broadcast "
			"into either bank");
	evre_base_t fresh;
	check(fresh.RX_SLAVE_ID == 0 && fresh.ACCEPT_BROADCAST_D000 == 0, "D-21, D-23: ACCEPT_BROADCAST_D000 and RX_SLAVE_ID are 0 by default");
}

/* D-24: STATUS says whether the device bank takes a broadcast. A READ of
 * off x cnt answered; the high byte of STATUS as it went out. */
static int statusHighSent(evre_base_t *dev, uint16_t off, uint16_t cnt) {
	if (ask(dev, READ, off, cnt) != NO_ERROR || answerLen != 10 + cnt) {
		return -1;
	}
	return answer[7 + 0xA003 - off];
}

static void statusBit() {
	static uint8_t rw[4];
	static const evre_range_t table[] = { { 0xD000, 4, rw, 1 } };
	evre_base_t dev;
	dev.SALVE_ID_REG = 1;
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 1;
	check(protocolInit(&dev) == NO_ERROR, "D-24: protocolInit");
	dev.STATUS |= (uint16_t) CAP_AUTO_SEND; /* one of the device's own */
	const uint16_t plain = dev.STATUS;
	const uint8_t high = (uint8_t) (plain >> 8), withBit = (uint8_t) ((plain | CAP_BROADCAST_D000) >> 8);
	check(CAP_BROADCAST_D000 == 0x4000 && (plain & CAP_BROADCAST_D000) == 0, "D-24: CAP_BROADCAST_D000 is bit 14, and free");

	check(dev.ACCEPT_BROADCAST_D000 == 0 && statusHighSent(&dev, 0xA000, 4) == high && answer[9] == (uint8_t) plain
			&& dev.STATUS == plain, "D-24: ACCEPT_BROADCAST_D000 0: STATUS goes out without CAP_BROADCAST_D000, the other bits as they are");
	dev.STATUS |= (uint16_t) CAP_BROADCAST_D000;
	check(statusHighSent(&dev, 0xA002, 2) == high && dev.STATUS == plain,
			"D-24: the bit is the library's: set by the device without the setting, it goes out 0, and STATUS says 0 after");

	dev.ACCEPT_BROADCAST_D000 = 1;
	check(dev.STATUS == plain && statusHighSent(&dev, 0xA002, 2) == withBit && answer[7] == (uint8_t) plain
			&& dev.STATUS == (plain | CAP_BROADCAST_D000),
			"D-24: ACCEPT_BROADCAST_D000 set after protocolInit: the next READ of STATUS carries the bit, and STATUS holds it after");
	check(statusHighSent(&dev, 0xA003, 1) == withBit && statusHighSent(&dev, 0xA000, 0x106) == withBit,
			"D-24: in a READ of the high byte alone, and of the whole reserved bank");
	uint8_t out[32];
	uint16_t len = 0;
	dev.STATUS = plain;
	check(encodePacketInto(&dev, 1, READ_RESP, 0xA000, 4, nullptr, out, sizeof out, &len) == NO_ERROR && len == 14
			&& out[10] == withBit && dev.STATUS == (plain | CAP_BROADCAST_D000),
			"D-24: the device's own READ_RESP of STATUS from its registers (AUTO_SEND) carries it too");

	dev.STATUS = plain;
	check(ask(&dev, READ, 0xA002, 1) == NO_ERROR && ask(&dev, READ, 0xA004, 2) == NO_ERROR
			&& encodePacketInto(&dev, 1, READ, 0xA000, 4, nullptr, out, sizeof out, &len) == NO_ERROR
			&& encodePacketInto(&dev, 1, WRITE, 0xA004, 2, nullptr, out, sizeof out, &len) == NO_ERROR && dev.STATUS == plain,
			"D-24: bytes that do not hold it leave STATUS alone: a READ of its low byte, of CONFIG, a READ built, a WRITE of CONFIG "
			"built from the registers");
	dev.READ_HANDLER = onRead;
	readVerdict = PERMISSION_DENIED;
	check(ask(&dev, READ, 0xA000, 4) == PERMISSION_DENIED && refusedWith(3) && dev.STATUS == plain,
			"D-24: a READ the handler refuses sends no STATUS and leaves it alone");
	readVerdict = NO_ERROR;
	dev.READ_HANDLER = nullptr;

	const bool holds = statusHighSent(&dev, 0xA000, 4) == withBit && dev.STATUS == (plain | CAP_BROADCAST_D000);
	dev.ACCEPT_BROADCAST_D000 = 0;
	check(holds && statusHighSent(&dev, 0xA000, 4) == high && dev.STATUS == plain,
			"D-24: set back to 0 while STATUS holds the bit: the next READ says 0, and STATUS too");
	dev.ACCEPT_BROADCAST_D000 = 1;
	check(statusHighSent(&dev, 0xA000, 4) == withBit, "D-24: and 1 again");

	/* the host's mirror of such a device keeps what the device reported */
	evre_base_t mirror;
	mirror.SALVE_ID_REG = 1;
	check(protocolInit(&mirror) == NO_ERROR, "D-24: a mirror: protocolInit");
	mirror.ACCEPT_READ_RESP = 1;
	uint8_t resp[32];
	uint16_t respLen = 0;
	check(encodePacketInto(&dev, 1, READ_RESP, 0xA000, 4, nullptr, out, sizeof out, &len) == NO_ERROR
			&& decodePacketInto(&mirror, out, len, resp, sizeof resp, &respLen) == NO_ERROR
			&& (mirror.STATUS & CAP_BROADCAST_D000) != 0,
			"D-24: the device's READ_RESP tells the host's mirror (ACCEPT_BROADCAST_D000 0 itself) that the device takes broadcasts");
	check(encodePacketInto(&mirror, 1, READ, 0xA000, 4, nullptr, out, sizeof out, &len) == NO_ERROR
			&& encodePacketInto(&mirror, 1, WRITE, 0xA004, 2, nullptr, out, sizeof out, &len) == NO_ERROR
			&& (mirror.STATUS & CAP_BROADCAST_D000) != 0,
			"D-24: requests the host builds from the mirror (a READ of STATUS, a WRITE of CONFIG) leave the device's bit in it");

	evre_base_t early;
	early.SALVE_ID_REG = 1;
	early.ACCEPT_BROADCAST_D000 = 1;
	check(protocolInit(&early) == NO_ERROR && statusHighSent(&early, 0xA002, 2) == (uint8_t) ((early.STATUS | CAP_BROADCAST_D000) >> 8)
			&& (early.STATUS & CAP_BROADCAST_D000) != 0,
			"D-24: ACCEPT_BROADCAST_D000 set before protocolInit: the first READ of STATUS carries it");
}

/* D-25: a mirror's STATUS is its device's. The library never refreshes it, so
 * a mirror serves the bit 14 its device reported, whatever its own
 * ACCEPT_BROADCAST_D000. The high byte of STATUS in a READ_RESP the mirror
 * builds from its registers, -1 if it builds none. */
static int statusHighBuilt(evre_base_t *dev) {
	uint8_t out[32];
	uint16_t len = 0;
	if (encodePacketInto(dev, 1, READ_RESP, 0xA000, 4, nullptr, out, sizeof out, &len) != NO_ERROR || len != 14) {
		return -1;
	}
	return out[10];
}

static void mirrorStatus() {
	/* 0x7F01 and 0x3F01, little endian: the test-vector device with bit 14 and without */
	const uint8_t with[2] = { 0x01, 0x7F }, without[2] = { 0x01, 0x3F };
	evre_base_t mirror;
	mirror.SALVE_ID_REG = 1;
	check(protocolInit(&mirror) == NO_ERROR, "D-25: a mirror: protocolInit");
	const uint16_t initial = mirror.STATUS;
	const uint8_t initialHigh = (uint8_t) (initial >> 8);
	mirror.ACCEPT_READ_RESP = 1;

	mirror.ACCEPT_BROADCAST_D000 = 1;
	check((initial & CAP_BROADCAST_D000) == 0 && statusHighSent(&mirror, 0xA002, 2) == initialHigh
			&& statusHighBuilt(&mirror) == initialHigh && mirror.STATUS == initial,
			"D-25: a mirror that has stored no STATUS, its own ACCEPT_BROADCAST_D000 1: a READ it answers and a READ_RESP it builds "
			"carry the STATUS protocolInit wrote, without bit 14, and STATUS keeps it");

	mirror.ACCEPT_BROADCAST_D000 = 0;
	const bool stored = ask(&mirror, READ_RESP, 0xA002, 2, with) == NO_ERROR && answerLen == 0 && mirror.STATUS == 0x7F01;
	check(stored && statusHighSent(&mirror, 0xA002, 2) == 0x7F && answer[7] == 0x01 && mirror.STATUS == 0x7F01,
			"D-25: a mirror with its own ACCEPT_BROADCAST_D000 0 that stored a READ_RESP of STATUS with bit 14 set: a READ it answers "
			"carries bit 14, and STATUS keeps it");
	check(statusHighSent(&mirror, 0xA003, 1) == 0x7F && statusHighSent(&mirror, 0xA000, 0x106) == 0x7F && mirror.STATUS == 0x7F01,
			"D-25: the same in a READ of the high byte alone, and of the whole reserved bank");
	check(statusHighBuilt(&mirror) == 0x7F && mirror.STATUS == 0x7F01,
			"D-25: a READ_RESP the mirror builds from its registers carries bit 14 too");

	mirror.ACCEPT_BROADCAST_D000 = 1;
	const bool storedClear = ask(&mirror, READ_RESP, 0xA002, 2, without) == NO_ERROR && answerLen == 0 && mirror.STATUS == 0x3F01;
	check(storedClear && statusHighSent(&mirror, 0xA002, 2) == 0x3F && statusHighSent(&mirror, 0xA000, 4) == 0x3F
			&& mirror.STATUS == 0x3F01,
			"D-25: a mirror with its own ACCEPT_BROADCAST_D000 1 that stored a STATUS without bit 14: a READ it answers says 0, and "
			"STATUS keeps 0");
	check(statusHighBuilt(&mirror) == 0x3F && mirror.STATUS == 0x3F01, "D-25: a READ_RESP the mirror builds says 0 too");

	/* The same evre_base_t as a device again: D-24 exactly. */
	mirror.ACCEPT_READ_RESP = 0;
	check(statusHighSent(&mirror, 0xA002, 2) == 0x7F && mirror.STATUS == 0x7F01,
			"D-25: ACCEPT_READ_RESP set back to 0, a device: the next READ of STATUS carries bit 14 as its ACCEPT_BROADCAST_D000 1 says, "
			"and STATUS follows (D-24)");
	mirror.ACCEPT_BROADCAST_D000 = 0;
	check(statusHighBuilt(&mirror) == 0x3F && mirror.STATUS == 0x3F01 && statusHighSent(&mirror, 0xA003, 1) == 0x3F,
			"D-25: and with 0 its own READ_RESP and a READ say 0, and STATUS follows (D-24)");
}

/* D-23: EVRe Guard skips a login by broadcast */
static bool loginMemoryClean() {
	for (uint8_t b : login16) {
		if (b != 0) {
			return false;
		}
	}
	return true;
}

static void guardBroadcast() {
	static const evre_guard_config_t cfg = { 0xD010, 16, token, openSpan, 1, 3, 1000, 8000, 5000, fakeClock };
	evre_base_t dev;
	guardedDevice(dev);
	std::memset(login16, 0, sizeof login16);
	std::memset(rw8, 0, sizeof rw8);
	clockMs = 1000;
	check(evre_guard_init(&guard, &cfg) == NO_ERROR, "D-23: evre_guard_init");
	const uint8_t value[4] = { 9, 9, 9, 9 };

	guardWrites = 0;
	check(ask(&dev, WRITE, 0xD010, 16, token, 0) == PERMISSION_DENIED && answerLen == 0 && guardWrites == 0 && !guard.logged_in
			&& guard.failures == 0 && loginMemoryClean(),
			"D-23: ACCEPT_BROADCAST_D000 0: a broadcast of the right token is refused by the library before the guard is asked: no login, "
			"no failure");

	dev.ACCEPT_BROADCAST_D000 = 1;
	guardWrites = 0;
	check(ask(&dev, WRITE, 0xD010, 16, token, 0) == PERMISSION_DENIED && answerLen == 0 && guardWrites == 1 && !guard.logged_in
			&& guard.failures == 0 && guard.refused == 0 && loginMemoryClean(),
			"D-23: ACCEPT_BROADCAST_D000 1: a broadcast of the right token reaches the guard and does not log in: PERMISSION_DENIED, "
			"silent, not stored (before: logged in)");
	for (int i = 0; i < 5; ++i) {
		ask(&dev, WRITE, 0xD010, 16, wrongToken, 0);
	}
	const bool noCount = guard.failures == 0 && !guard.locked && guard.refused == 0 && loginMemoryClean();
	check(noCount && rightTokenLogsIn(dev) && guard.failures == 0,
			"D-23: five broadcasts of a wrong token (free_attempts 3): no failure counted, no lockout; a unicast login with the right "
			"token then logs in at once (before: locked out)");

	clockMs += 4000;
	ask(&dev, WRITE, 0xD010, 16, token, 0);
	ask(&dev, WRITE, 0xD010, 16, wrongToken, 0);
	ask(&dev, WRITE, 0xD010, 8, token, 0);
	const bool kept = guard.logged_in && guard.failures == 0 && !guard.locked;
	clockMs += 999;
	const bool open4999 = evre_guard_logged_in(&guard) == 1;
	clockMs += 1;
	check(kept && open4999 && !evre_guard_logged_in(&guard) && loginMemoryClean(),
			"D-23: in a session, broadcasts to the login register (the right token, a wrong one, half a one): the session goes on, and "
			"they are no activity: it ends 5000 ms after the login (before: the wrong token ended it)");

	check(ask(&dev, WRITE, 0xD008, 4, value, 0) == LOGIN_REQUIRED && answerLen == 0 && rw8[0] == 0,
			"D-23: without a session, any other broadcast write: LOGIN_REQUIRED, silent, nothing stored");
	const uint16_t configBefore = dev.CONFIG;
	check(ask(&dev, WRITE, 0xA004, 2, value, 0) == LOGIN_REQUIRED && answerLen == 0 && dev.CONFIG == configBefore,
			"D-23: a broadcast to CONFIG as well");
	uint8_t block[24];
	std::memset(block, 0x33, sizeof block);
	check(ask(&dev, WRITE, 0xD008, 24, block, 0) == PERMISSION_DENIED && answerLen == 0 && guard.failures == 0 && rw8[0] == 0
			&& loginMemoryClean(),
			"D-23: without a session, a broadcast block write over the login register: skipped, no failure (a unicast one is a failed "
			"attempt)");
	rightTokenLogsIn(dev);
	check(ask(&dev, WRITE, 0xD008, 4, value, 0) == NO_ERROR && answerLen == 0 && rw8[0] == 9,
			"D-23: in a session a broadcast write is taken, as a unicast one: the session belongs to the link");

	evre_guard_logout(&guard);
	check(ask(&dev, WRITE_ACK, 0xD010, 16, wrongToken) == PERMISSION_DENIED && refusedWith(PERMISSION_DENIED) && guard.failures == 1
			&& guard.refused == 1, "D-23: a unicast login counts as before: a wrong token, PERMISSION_DENIED answered, 1 failure");
	check(rightTokenLogsIn(dev) && guard.failures == 0 && loginMemoryClean(),
			"D-23: and a unicast login with the right token logs in as before; the token is never stored");
	check(evre_guard_write(&guard, nullptr, 0xD008, value, 4) == PERMISSION_DENIED && guard.logged_in,
			"D-23: evre_guard_write with no device: refused, the session left alone");
	evre_guard_logout(&guard);
}

/* D-18: a bad config fails closed. Last: the guard from before this check
 * crashes on some of them. */
static void guardConfigs() {
	evre_base_t dev;
	guardedDevice(dev);
	const evre_guard_config_t good = { 0xD010, 16, token, openSpan, 1, 3, 1000, 8000, 5000, fakeClock };
	struct Bad { const char *what; evre_guard_config_t cfg; bool none; };
	Bad bad[] = {
		{ "login_size 0", good, false },
		{ "login_size 33", good, false },
		{ "open_reads null, n_open_reads 1", good, false },
		{ "no token", good, false },
		{ "no clock", good, false },
		{ "no config", good, true },
	};
	bad[0].cfg.login_size = 0;
	bad[1].cfg.login_size = EVRE_GUARD_TOKEN_MAX + 1;
	bad[2].cfg.open_reads = nullptr;
	bad[3].cfg.token = nullptr;
	bad[4].cfg.now_ms = nullptr;
	const uint8_t zeros[4] = { 0, 0, 0, 0 };
	for (const Bad &t : bad) {
		char what[160];
		std::snprintf(what, sizeof what, "D-18: a config with %s: evre_guard_init says PERMISSION_DENIED, and every request is refused "
				"with it, a write across the login register too", t.what);
		const bool refused = evre_guard_init(&guard, t.none ? nullptr : &t.cfg) == PERMISSION_DENIED;
		const bool closed = ask(&dev, READ, 0xA000, 2) == PERMISSION_DENIED && ask(&dev, READ, 0xD000, 4) == PERMISSION_DENIED
				&& ask(&dev, WRITE_ACK, 0xD00E, 4, zeros) == PERMISSION_DENIED && !evre_guard_logged_in(&guard);
		check(refused && closed, what);
	}
}

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0); /* a crash still shows the last check that ran */
	crcBeforeInit(); /* first of all */
	fixes();
	ranges();
	handlers(false);
	handlers(true);
	guarded();
	orders();
	decisions();
	inPlace();
#ifdef WRAP_CALLOC
	allocation();
#endif
	lockHooks();
	guardDecisions();
	guardTime();
	guardRace();
	messageFF();
	clearWins();
	broadcastBank(false);
	broadcastBank(true);
	rxSlaveId();
	statusBit();
	mirrorStatus();
	guardBroadcast();
	guardConfigs();
	std::printf("%d failed\n", failed);
	return failed ? 1 : 0;
}

#endif /* FEATURES */

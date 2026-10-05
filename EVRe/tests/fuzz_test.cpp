/* SPDX-License-Identifier: Apache-2.0 */
/*
 * fuzz_test.cpp: a differential test of two builds of the library.
 *
 * The same deterministic stream of cases goes through each build. Every
 * operation prints one line: the return code, the output length, a hash of the
 * output, a hash of all the memory a frame can change, a hash of what the
 * handlers saw and when during that operation, the memory hash from before
 * it, the loose hash (l: the memory but for HEARTBEAT and the queue past
 * MSG_CNT, which two classes change on their own), and its tags. A frame
 * decoded in place also shows its outcome (i) and the outcome of the same
 * frame with an answer buffer of its own, on the same memory (s). A write that
 * clears the queue and runs on into the slots shows (x) whether it did what
 * the same frame cut after MSG_CNT does on the same memory: "x=" yes, "x!" no,
 * "x-" not such a write, or refused. An operation of a device that may send
 * STATUS's high byte (D24) shows (k) the outcome of the same call on the same
 * memory, but with STATUS bit 14 set to the case's ACCEPT_BROADCAST_D000
 * first: return code, length, answer, memory, handlers and loose hash. The
 * same operation of a mirror (D25) shows "k=" when the call left STATUS as it
 * was and any READ_RESP it built carries STATUS as it was, "k!" when not.
 * "k-" for any other.
 * Then whether an answer of 11 bytes is the request's ERROR_RESP, with the
 * return code in it (e1, e0, e- for another length), and the handlers' hash
 * without the queue bytes the ack handlers see (h). Two libraries that behave
 * the same print the same lines, so a refactor is checked by comparing the
 * two outputs.
 *
 * The tags (t...) name the classes of an intended change that an operation
 * falls in, worked out from its inputs alone before the call, so they are the
 * same in every build until the device's state diverges: D2room, D2place, D3,
 * D7, D8, D9 (a device), D9m and D9u (a mirror, a known bank or not), D10,
 * D11, D12 (answered) and D12s (a response: silent), D13, D14, D17, D19 (a
 * clear and slot bytes in one write), D21 (a broadcast into the device bank
 * of a device that does not take one), D24 (a READ, or the device's own
 * READ_RESP from its registers, that holds STATUS's high byte: on a device)
 * (FUZZ_CLASSES in run_lib_tests.py says what each is). D25 is the same
 * operation on a mirror, and no class: a mirror's STATUS is left alone, as
 * 1.0 left it. "room" is no class: an ERROR_RESP fits in the answer buffer. A
 * tag is a superset: run_lib_tests.py --fuzz accepts a line that differs only
 * if it carries a tag whose check the new line passes. "t-": no tag. Some are
 * exact, and checked on every line of the new run, the same as before or not:
 * a D21 frame is refused, a D9, D9m or D9u frame (a WRITE_ACK_RESP of 10
 * bytes) gets its code, no line shows "x!", a D24 operation that succeeds has
 * the outcome its k field shows, and a D25 operation shows "k=".
 *
 * Where the library has RX_SLAVE_ID, every handler checks it against the
 * frame's slave id; the program exits with 3 if one was wrong.
 *
 * It reaches what the transcript in lib_test.cpp does not: the handlers with
 * every verdict and with side effects, small and null output buffers, sizes
 * that disagree for functions without data, bad CRCs for other slaves, frames
 * shorter than 10 bytes, decoding in place, range tables with gaps and one-byte
 * ranges, a range over the device's own fields, a table set after
 * protocolInit(), message queues with holes, ack handlers that queue again or
 * change the slave id, counts above 0x2000, the allocating forms, the encoder
 * with pData inside outBuf, addMsg, protocolInit() on good and bad tables, and
 * several operations in a row on one device.
 *
 * Build it once per library, each against its own header, then compare:
 *
 *   g++ -std=c++17 -O1 -I A/lib tests/fuzz_test.cpp A/lib/EVRe.cpp -o fuzz_a
 *   g++ -std=c++17 -O1 -I B/lib tests/fuzz_test.cpp B/lib/EVRe.cpp -o fuzz_b
 *   ./fuzz_a 300000 1 > a.txt
 *   ./fuzz_b 300000 1 > b.txt
 *   cmp a.txt b.txt
 *
 * or let the suite do it, A against this tree's lib/:
 *
 *   python tests/run_lib_tests.py --fuzz A/lib [--fuzz-cases 300000]
 *
 * Usage: fuzz_test [cases [seed]]. The library must have the 1.1 fields
 * (ranges, handlers, ACCEPT_READ_RESP); ACCEPT_BROADCAST_D000 and RX_SLAVE_ID
 * are used where it has them. A library without ACCEPT_BROADCAST_D000 takes
 * every broadcast into its device bank, as a setting of 1 does. The stream
 * stays away from what is undefined in a build it is compared with:
 * acknowledging message 0xFF while a write handler is set (before 1.1,
 * MSG_ACK_HANDLER had no slot for it), and pData inside the header of the
 * frame being built.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "EVRe.h"

/* ------------------------------------------------ the stream */

static uint64_t rngState = 88172645463325252ULL;

static uint32_t rnd() {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 7;
	rngState ^= rngState << 17;
	return (uint32_t) (rngState >> 16);
}

static uint32_t below(uint32_t n) {
	return rnd() % n;
}

static bool chance(unsigned percent) {
	return below(100) < percent;
}

static const uint32_t FNV_START = 2166136261u;

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
	const uint8_t *b = (const uint8_t*) p;
	for (size_t i = 0; i < n; ++i) {
		h = (h ^ b[i]) * 16777619u;
	}
	return h;
}

/* What the handlers saw during one operation, in the order they saw it. And
 * the same without the queue's bytes that the ack handlers see (h): D13
 * changes those on purpose, but never which handlers run, in which order, on
 * which MSG_CNT. */
static uint32_t events, eventsNoQueue;

static void queueEvent(uint8_t kind, uint32_t a, uint32_t b) {
	events = fnv(events, &kind, 1);
	events = fnv(events, &a, 4);
	events = fnv(events, &b, 4);
}

static void event(uint8_t kind, uint32_t a, uint32_t b) {
	queueEvent(kind, a, b);
	eventsNoQueue = fnv(eventsNoQueue, &kind, 1);
	eventsNoQueue = fnv(eventsNoQueue, &a, 4);
	eventsNoQueue = fnv(eventsNoQueue, &b, 4);
}

static void resetEvents() {
	events = FNV_START;
	eventsNoQueue = FNV_START;
}

/* ------------------------------------------------ the device */

static uint8_t ro[0x400], rw[0x400], big[0x1000];
static uint8_t *d000[0x1000];
static uint8_t req[0x6100], out[0x6100];

static evre_base_t *current;
static uint8_t writeVerdict, readVerdict;
static unsigned writeEffect, readEffect;

/* The 1.1 members a library before them lacks: set and read only where they
 * exist. Without ACCEPT_BROADCAST_D000 a library takes every broadcast, as 1
 * does; without RX_SLAVE_ID there is nothing to check. */
template<typename T> static auto setAcceptBroadcast(T &d, uint8_t value, int) -> decltype((void) (d.ACCEPT_BROADCAST_D000 = value)) {
	d.ACCEPT_BROADCAST_D000 = value;
}
template<typename T> static void setAcceptBroadcast(T &, uint8_t, long) {
}
template<typename T> static auto rxSlaveOf(const T *d, uint8_t, int) -> decltype((uint8_t) d->RX_SLAVE_ID) {
	return d->RX_SLAVE_ID;
}
template<typename T> static uint8_t rxSlaveOf(const T *, uint8_t expected, long) {
	return expected;
}

static uint8_t acceptBroadcast; /* this case's ACCEPT_BROADCAST_D000, drawn in every build */
static uint8_t frameSlave;      /* the slave id of the frame being decoded */
static unsigned rxWrong;        /* handlers that saw another RX_SLAVE_ID */

/* While set, the write handler reports this write instead of the one it is
 * asked for: clearAlone() cuts the frame, and the handler must see the same
 * as for the whole one. */
static const uint8_t *wholeData;
static uint16_t wholeCount;

/* A handler that changes something itself: the slave id (which the answer
 * carries), CONFIG (which the heartbeat touches), or the registers (which a
 * READ answers with). Tells apart two orders of the same steps. */
static void sideEffect(evre_base_t *d, unsigned effect) {
	switch (effect) {
		case 1:
			d->SALVE_ID_REG ^= 0x01;
			break;
		case 2:
			d->CONFIG ^= 0x0100;
			break;
		case 3:
			ro[0]++;
			rw[0]++;
			big[0]++;
			if (d->MSG_BUFFER[0] < 0xFE) {
				d->MSG_BUFFER[0]++; /* never to 0xFF, see keepAcksDefined() */
			}
			break;
		default:
			break;
	}
}

static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	if (wholeData != nullptr) {
		data = wholeData;
		cnt = wholeCount;
	}
	rxWrong += rxSlaveOf(d, frameSlave, 0) != frameSlave;
	event('W', off, cnt);
	events = fnv(events, data, cnt);
	eventsNoQueue = fnv(eventsNoQueue, data, cnt);
	event('w', d->MSG_CNT, d->CONFIG);
	event('s', d->SALVE_ID_REG, 0);
	sideEffect(d, writeEffect);
	return writeVerdict;
}

static uint8_t onRead(evre_base_t *d, uint16_t off, uint16_t cnt) {
	rxWrong += rxSlaveOf(d, frameSlave, 0) != frameSlave;
	event('R', off, cnt);
	event('r', d->MSG_CNT, d->CONFIG);
	sideEffect(d, readEffect);
	return readVerdict;
}

template<int K> static void ackFn() {
	event('A', K, current->MSG_CNT);
	queueEvent('a', current->MSG_BUFFER[0], current->MSG_BUFFER[1]);
	event('c', current->CONFIG, current->SALVE_ID_REG); /* what a write stored before this act */
	if (K == 5) {
		event('q', addMsg(current, 0x42), current->MSG_CNT); /* queues again */
	}
	if (K == 6) {
		current->SALVE_ID_REG ^= 0x02;
	}
}

static void (*const ackFns[8])(void) = { ackFn<0>, ackFn<1>, ackFn<2>, ackFn<3>, ackFn<4>, ackFn<5>, ackFn<6>, ackFn<7> };

static const evre_range_t twoRanges[] = { { 0xD000, 220, ro, 0 }, { 0xD0DC, 35, rw, 1 } };
static const evre_range_t gapRanges[] = {
	{ 0xD000, 4, ro, 0 }, { 0xD004, 4, rw, 1 }, { 0xD008, 4, ro + 4, 0 }, { 0xD010, 4, rw + 4, 1 },
	{ 0xD014, 0x20, rw + 8, 1 }, { 0xD034, 0x0C, ro + 8, 0 }, { 0xD040, 0x10, ro + 0x20, 0 }, { 0xD050, 3, rw + 0x40, 1 },
	{ 0xDFF0, 0x10, rw + 0x80, 1 } };
static const evre_range_t wholeBank[] = { { 0xD000, 0x1000, big, 1 } };
static const evre_range_t manySmall[] = {
	{ 0xD000, 1, ro, 1 }, { 0xD001, 1, ro + 1, 1 }, { 0xD002, 1, ro + 2, 0 }, { 0xD003, 1, ro + 3, 1 },
	{ 0xD004, 2, ro + 4, 1 }, { 0xD006, 2, ro + 6, 1 }, { 0xD009, 1, ro + 9, 1 }, { 0xD00A, 6, ro + 10, 1 } };
static evre_range_t selfRanges[4]; /* over the device's own fields: filled per case */

static const uint16_t offs[] = { 0x0000, 0x0001, 0x9FFF, 0xA000, 0xA001, 0xA002, 0xA003, 0xA004, 0xA005, 0xA006, 0xA007,
	0xA008, 0xA009, 0xA0FF, 0xA104, 0xA105, 0xA106, 0xAFFF, 0xB000, 0xCFFF, 0xD000, 0xD001, 0xD002, 0xD003, 0xD004, 0xD007,
	0xD008, 0xD009, 0xD00B, 0xD00C, 0xD00F, 0xD010, 0xD013, 0xD014, 0xD033, 0xD034, 0xD03F, 0xD040, 0xD04F, 0xD050, 0xD052,
	0xD053, 0xD0DA, 0xD0DB, 0xD0DC, 0xD0DD, 0xD0FD, 0xD0FE, 0xD0FF, 0xD100, 0xD3FF, 0xD400, 0xDFEF, 0xDFF0, 0xDFFE, 0xDFFF,
	0xE000, 0xF000, 0xFFFF };
static const uint16_t cnts[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16, 17, 34, 35, 36, 219, 220, 221, 254, 255, 256, 262, 263,
	0x03FF, 0x0400, 0x0FFF, 0x1000, 0x1001, 0x2001, 0x3001, 0x6001, 0xFFF5, 0xFFF6, 0xFFFF };
static const uint8_t fns[] = { READ, READ, READ, READ_RESP, READ_RESP, WRITE, WRITE, WRITE, WRITE_ACK, WRITE_ACK, WRITE_ACK,
	WRITE_ACK_RESP, ERROR_RESP, 0x00, 0x55 };
static const uint8_t writeVerdicts[] = { NO_ERROR, NO_ERROR, NO_ERROR, EVRE_HANDLED, EVRE_HANDLED, PERMISSION_DENIED,
	INVALID_PACKET_ERR, SLAVE_ID_MISMATCHED, 13, FUNCTION_CODE_ERR, BUFFER_TOO_SMALL };
static const uint8_t readVerdicts[] = { NO_ERROR, NO_ERROR, NO_ERROR, PERMISSION_DENIED, INVALID_PACKET_ERR,
	SLAVE_ID_MISMATCHED, EVRE_HANDLED, 13 };

/* Everything a frame can change. Leaves events as it was. */
static uint32_t memoryHash(const evre_base_t *d) {
	uint32_t h = FNV_START;
	h = fnv(h, ro, sizeof ro);
	h = fnv(h, rw, sizeof rw);
	h = fnv(h, big, sizeof big);
	h = fnv(h, &d->SALVE_ID_REG, 1);
	h = fnv(h, &d->DEVICE_ID, 2);
	h = fnv(h, &d->STATUS, 2);
	h = fnv(h, &d->CONFIG, 2);
	h = fnv(h, &d->MSG_CNT, 1);
	h = fnv(h, d->MSG_BUFFER, sizeof d->MSG_BUFFER);
	return h;
}

/* The same, but for the two things a decided class changes on its own:
 * CONFIG's bit 0 (HEARTBEAT, D10) and the queue past MSG_CNT (D13). A line of
 * those classes must show this hash unchanged: anything else it changed is a
 * change outside the class. */
static uint32_t looseHash(const evre_base_t *d) {
	uint32_t h = FNV_START;
	h = fnv(h, ro, sizeof ro);
	h = fnv(h, rw, sizeof rw);
	h = fnv(h, big, sizeof big);
	h = fnv(h, &d->SALVE_ID_REG, 1);
	h = fnv(h, &d->DEVICE_ID, 2);
	h = fnv(h, &d->STATUS, 2);
	const uint16_t config = (uint16_t) (d->CONFIG & ~1U);
	h = fnv(h, &config, 2);
	h = fnv(h, &d->MSG_CNT, 1);
	h = fnv(h, d->MSG_BUFFER, d->MSG_CNT);
	return h;
}

/* One decode as a whole: its return code, its answer, the memory after it
 * (loose) and what the handlers saw. len 0xBEEF: left alone, no answer. */
static uint32_t outcomeHash(uint8_t ret, uint16_t len, const uint8_t *answer, const evre_base_t *d) {
	uint32_t h = fnv(FNV_START, &ret, 1);
	h = fnv(h, &len, 2);
	h = fnv(h, answer, (len == 0xBEEF) ? 0 : len);
	const uint32_t loose = looseHash(d);
	h = fnv(h, &loose, 4);
	return fnv(h, &events, 4);
}

/* What memoryHash() covers, so that a decode can be undone. Field by field:
 * evre_base_t has const members, and a copy over them is undefined. */
static struct {
	uint8_t readOnly[sizeof ro], readWrite[sizeof rw], whole[sizeof big];
	uint8_t slave, msgCnt, queue[255];
	uint16_t id, status, config;
} saved;

static void saveMemory(const evre_base_t &d) {
	std::memcpy(saved.readOnly, ro, sizeof ro);
	std::memcpy(saved.readWrite, rw, sizeof rw);
	std::memcpy(saved.whole, big, sizeof big);
	saved.slave = d.SALVE_ID_REG;
	saved.id = d.DEVICE_ID;
	saved.status = d.STATUS;
	saved.config = d.CONFIG;
	saved.msgCnt = d.MSG_CNT;
	std::memcpy(saved.queue, d.MSG_BUFFER, sizeof saved.queue);
}

static void restoreMemory(evre_base_t &d) {
	std::memcpy(ro, saved.readOnly, sizeof ro);
	std::memcpy(rw, saved.readWrite, sizeof rw);
	std::memcpy(big, saved.whole, sizeof big);
	d.SALVE_ID_REG = saved.slave;
	d.DEVICE_ID = saved.id;
	d.STATUS = saved.status;
	d.CONFIG = saved.config;
	d.MSG_CNT = saved.msgCnt;
	std::memcpy(d.MSG_BUFFER, saved.queue, sizeof saved.queue);
}

static uint16_t pickOffset(const evre_base_t *d) {
	switch (below(10)) {
		case 0:
			return (uint16_t) rnd();
		case 1:
			return (uint16_t) (0xD000 + below(0x1000));
		case 2:
			return (uint16_t) (0xA006 + d->MSG_CNT + below(3) - 1); /* around the last queued message */
		case 3:
			return (uint16_t) (0xA000 + below(0x106));
		default:
			return offs[below(sizeof offs / sizeof offs[0])];
	}
}

static uint16_t pickCount() {
	switch (below(8)) {
		case 0:
			return (uint16_t) rnd();
		case 1:
			return (uint16_t) below(0x1100);
		case 2:
			return (uint16_t) below(20);
		default:
			return cnts[below(sizeof cnts / sizeof cnts[0])];
	}
}

static bool carriesData(uint8_t fn) {
	return fn == WRITE || fn == WRITE_ACK || fn == READ_RESP;
}

static void configure(evre_base_t &d, unsigned layout) {
	switch (layout) {
		case 0: /* pointers, as the transcript */
			d.DEVICE_REG_READ_MAX = 0xD0FE;
			d.DEVICE_REG_WRITE_MIN = 0xD0DC;
			for (unsigned i = 0; i < 220; ++i) d000[i] = &ro[i];
			for (unsigned i = 0; i < 35; ++i) d000[220 + i] = &rw[i];
			d.D000 = d000;
			break;
		case 1:
			d.D_RANGES = twoRanges;
			d.D_RANGE_CNT = 2;
			break;
		case 2:
			d.D_RANGES = gapRanges;
			d.D_RANGE_CNT = sizeof gapRanges / sizeof gapRanges[0];
			break;
		case 3: /* pointers, WRITE_MIN left at 0: the whole bank writable */
			d.DEVICE_REG_READ_MAX = 0xD3FF;
			for (unsigned i = 0; i < 0x400; ++i) d000[i] = &rw[i];
			d.D000 = d000;
			break;
		case 4: /* no device bank at all */
			break;
		case 5: /* an empty table */
			d.D_RANGES = twoRanges;
			d.D_RANGE_CNT = 0;
			break;
		case 6:
			d.D_RANGES = wholeBank;
			d.D_RANGE_CNT = 1;
			break;
		case 7:
			d.D_RANGES = manySmall;
			d.D_RANGE_CNT = sizeof manySmall / sizeof manySmall[0];
			break;
		case 8: /* pointers, the whole bank */
			d.DEVICE_REG_READ_MAX = 0xDFFF;
			d.DEVICE_REG_WRITE_MIN = 0xD800;
			for (unsigned i = 0; i < 0x1000; ++i) d000[i] = &big[i];
			d.D000 = d000;
			break;
		case 9: /* the slave id and CONFIG as registers, then plain memory */
			selfRanges[0].start = 0xD000;
			selfRanges[0].len = 1;
			selfRanges[0].base = &d.SALVE_ID_REG;
			selfRanges[0].writable = 1;
			selfRanges[1].start = 0xD001;
			selfRanges[1].len = 2;
			selfRanges[1].base = (uint8_t*) &d.CONFIG;
			selfRanges[1].writable = 1;
			selfRanges[2].start = 0xD003;
			selfRanges[2].len = 8;
			selfRanges[2].base = ro;
			selfRanges[2].writable = 0;
			selfRanges[3].start = 0xD00B;
			selfRanges[3].len = 8;
			selfRanges[3].base = rw;
			selfRanges[3].writable = 1;
			d.D_RANGES = selfRanges;
			d.D_RANGE_CNT = 4;
			break;
	}
}

/* No 0xFF in the queue while a write handler is set: its ack is undefined. */
static void keepAcksDefined(evre_base_t &d) {
	if (d.WRITE_HANDLER == nullptr) {
		return;
	}
	for (unsigned i = 0; i < sizeof d.MSG_BUFFER; ++i) {
		if (d.MSG_BUFFER[i] == 0xFF) {
			d.MSG_BUFFER[i] = 0xFE;
		}
	}
}

/* ------------------------------------------------ the tags */

static char tags[64];

static void addTag(const char *tag) {
	if (tags[0] != 0) {
		std::strcat(tags, ",");
	}
	std::strcat(tags, tag);
}

static const char *tagField() {
	return tags[0] != 0 ? tags : "-";
}

static bool refusesSilently(uint8_t verdict) {
	return verdict == INVALID_PACKET_ERR || verdict == SLAVE_ID_MISMATCHED;
}

/* D24: the bytes off .. off + cnt - 1 hold 0xA003, where STATUS bit 14 lies. */
static bool holdsStatusHigh(uint16_t off, uint16_t cnt) {
	return off >= 0xA000 && off <= 0xA003 && (uint32_t) off + cnt > 0xA003;
}

/* A received frame, from its bytes and the device as it is before the call. */
static void receiveTags(const evre_base_t *dev, const uint8_t *packet, uint16_t size, const uint8_t *buf, uint16_t max,
		bool allocating) {
	tags[0] = 0;
	if (dev == nullptr || packet == nullptr || size < 10) {
		if (!allocating) {
			addTag("D3"); /* outLen 0 */
		}
		return;
	}
	if (GetCrc16(packet, size - 3) != (uint16_t) (packet[size - 3] | (packet[size - 2] << 8))) {
		return; /* a bad CRC: silent in every build */
	}
	if (packet[0] != 0x7B || packet[size - 1] != 0x7D) {
		addTag("D11");
		return;
	}
	const uint8_t slave = packet[1], fn = packet[2];
	const uint16_t off = (uint16_t) (packet[3] | (packet[4] << 8)), cnt = (uint16_t) (packet[5] | (packet[6] << 8));
	const bool broadcast = (slave == 0);
	if (!broadcast && slave != dev->SALVE_ID_REG) {
		return;
	}
	const bool known = fn == READ || fn == READ_RESP || fn == WRITE || fn == WRITE_ACK || fn == WRITE_ACK_RESP;
	if (!broadcast && !known && size != 10) {
		addTag(fn == ERROR_RESP ? "D12s" : "D12"); /* a response is never answered */
	}
	if (!broadcast && fn == WRITE_ACK_RESP && size == 10U) {
		/* exact, of the right length: run_lib_tests.py checks every such line */
		const uint16_t bank = (uint16_t) (off & 0xF000);
		if (!dev->ACCEPT_READ_RESP) {
			addTag("D9"); /* a device: refused */
		} else {
			addTag((bank == 0xA000 || bank == 0xD000) ? "D9m" : "D9u"); /* a mirror: the bank alone */
		}
	}
	if (!broadcast && (fn == WRITE_ACK_RESP || (fn == READ_RESP && dev->ACCEPT_READ_RESP))) {
		addTag("D10");
	}
	if (!broadcast && fn == READ && dev->READ_HANDLER != nullptr) {
		if (!allocating && !(buf != nullptr && max >= 10U + cnt)) {
			addTag("D2room");
		}
		if (refusesSilently(readVerdict) || readVerdict == EVRE_HANDLED) {
			addTag("D8");
		}
	}
	if (!broadcast && fn == WRITE_ACK && !allocating) {
		if (dev->WRITE_HANDLER != nullptr && !(buf != nullptr && max >= 10U)) {
			addTag("D2room");
		}
		if (buf == packet) {
			addTag("D2place");
		}
	}
	if (!broadcast && (fn == WRITE || fn == WRITE_ACK) && dev->WRITE_HANDLER != nullptr && refusesSilently(writeVerdict)) {
		addTag("D8");
	}
	if ((fn == WRITE || (fn == WRITE_ACK && !broadcast)) && off >= 0xA000 && off <= 0xA105 && (uint32_t) off + cnt > 0xA006) {
		/* A clear or an ack. A clear and slot bytes in one write: 1.0 went on
		 * and acknowledged the slots after the clear, 1.1 does the clear
		 * alone. */
		addTag((off <= 0xA006 && (uint32_t) off + cnt > 0xA007) ? "D19" : "D13");
	}
	if (broadcast && fn == WRITE && (off & 0xF000) == 0xD000 && !acceptBroadcast && size == 10U + cnt) {
		addTag("D21"); /* refused by 1.1 before a handler is asked; exact: run_lib_tests.py checks it on every line */
	}
	if (!broadcast && fn == READ && holdsStatusHigh(off, cnt)) {
		addTag(dev->ACCEPT_READ_RESP ? "D25" : "D24"); /* a mirror's STATUS is its device's */
	}
	/* not a class: an ERROR_RESP fits (the allocating form sizes its own buffer) */
	if (tags[0] != 0 && (allocating || (buf != nullptr && max >= 11U))) {
		addTag("room");
	}
}

static void sendTags(const evre_base_t *dev, uint8_t slaveId, uint8_t fn, uint16_t off, uint16_t cnt, const uint8_t *pData,
		bool allocating) {
	tags[0] = 0;
	if (dev == nullptr) {
		if (!allocating) {
			addTag("D3");
		}
		return;
	}
	if (slaveId == 0 && (fn == READ || fn == READ_RESP || fn == WRITE_ACK)) {
		addTag("D14");
	}
	if (fn == READ_RESP && pData == nullptr && holdsStatusHigh(off, cnt)) {
		addTag(dev->ACCEPT_READ_RESP ? "D25" : "D24");
	}
}

/* ------------------------------------------------ D19: the clear alone */

/* The return code, all the memory, what the handlers saw. */
static uint32_t effectHash(uint8_t ret, const evre_base_t *d) {
	uint32_t h = fnv(FNV_START, &ret, 1);
	const uint32_t memory = memoryHash(d);
	h = fnv(h, &memory, 4);
	return fnv(h, &events, 4);
}

/* A D19 frame of the right length, cut after MSG_CNT and decoded on the
 * memory as it is, which is then put back: what the whole frame must do if
 * its slot bytes do nothing. */
static uint32_t clearAlone(evre_base_t &d, const uint8_t *packet) {
	static uint8_t cut[32], answer[32];
	const uint16_t off = (uint16_t) (packet[3] | (packet[4] << 8));
	const uint16_t keep = (uint16_t) (0xA006 - off + 1);
	uint32_t n = 0;
	cut[n++] = 0x7B;
	cut[n++] = packet[1];
	cut[n++] = packet[2];
	cut[n++] = (uint8_t) off;
	cut[n++] = (uint8_t) (off >> 8);
	cut[n++] = (uint8_t) keep;
	cut[n++] = (uint8_t) (keep >> 8);
	std::memcpy(&cut[n], &packet[7], keep);
	n += keep;
	const uint16_t crc = GetCrc16(cut, (int) n);
	cut[n++] = (uint8_t) crc;
	cut[n++] = (uint8_t) (crc >> 8);
	cut[n++] = 0x7D;
	saveMemory(d);
	resetEvents();
	wholeData = &packet[7];
	wholeCount = (uint16_t) (packet[5] | (packet[6] << 8));
	uint16_t len = 0;
	const uint8_t ret = decodePacketInto(&d, cut, (uint16_t) n, answer, sizeof answer, &len);
	wholeData = nullptr;
	const uint32_t h = effectHash(ret, &d);
	restoreMemory(d);
	resetEvents();
	return h;
}

/* ------------------------------------------------ D24: STATUS with the setting */

static uint8_t reqKept[sizeof req], outKept[sizeof out];

/* Before the same call on a STATUS whose bit 14 (0x4000, CAP_BROADCAST_D000:
 * a library before 1.1 lacks the name) is the case's setting already, which is
 * what 1.1 must do. All the call can change is kept, to be put back after. */
static void withSettingBegin(evre_base_t &d) {
	saveMemory(d);
	std::memcpy(reqKept, req, sizeof req);
	std::memcpy(outKept, out, sizeof out);
	d.STATUS = (uint16_t) ((d.STATUS & ~0x4000U) | (acceptBroadcast ? 0x4000U : 0U));
	resetEvents();
}

/* Its outcome into field (the answer's hash taken before), then all as it was. */
static void withSettingEnd(evre_base_t &d, char *field, size_t size, uint8_t ret, uint16_t len, uint32_t answer) {
	std::snprintf(field, size, "k%u/%u/%08X/%08X/%08X/%08X", ret, len, answer, memoryHash(&d), events, looseHash(&d));
	restoreMemory(d);
	std::memcpy(req, reqKept, sizeof req);
	std::memcpy(out, outKept, sizeof out);
	resetEvents();
}

/* D25: a mirror's STATUS left alone. "k=" when the call kept STATUS as it was
 * before it, and a READ_RESP it built (len bytes at frame, 0xBEEF: none)
 * carries STATUS's bytes as they were; "k!" when not. */
static const char *statusKept(const evre_base_t &d, uint16_t before, const uint8_t *frame, uint16_t len, uint16_t off,
		uint16_t cnt) {
	bool kept = d.STATUS == before;
	if (frame != nullptr && len != 0xBEEF && len == 10U + cnt && frame[2] == READ_RESP) {
		for (uint32_t addr = 0xA002; addr <= 0xA003; ++addr) {
			if (addr >= off && addr < (uint32_t) off + cnt) {
				kept = kept && frame[7 + addr - off] == (uint8_t) (before >> (8 * (addr - 0xA002)));
			}
		}
	}
	return kept ? "k=" : "k!";
}

/* An answer of 11 bytes that is this request's ERROR_RESP: its code byte is
 * the return code, its offset and count the request's. "e1" yes, "e0" an
 * 11-byte answer that is not, "e-" any other length. */
static const char *errorField(const uint8_t *answer, uint16_t len, uint8_t ret, uint16_t off, uint16_t cnt) {
	if (len != 11 || answer == nullptr) {
		return "e-";
	}
	const bool ok = answer[2] == ERROR_RESP && answer[7] == ret && (answer[3] | (answer[4] << 8)) == off
			&& (answer[5] | (answer[6] << 8)) == cnt;
	return ok ? "e1" : "e0";
}

static bool tagged(const char *tag) {
	const size_t n = std::strlen(tag);
	for (const char *at = std::strstr(tags, tag); at != nullptr; at = std::strstr(at + 1, tag)) {
		if ((at == tags || at[-1] == ',') && (at[n] == 0 || at[n] == ',')) {
			return true;
		}
	}
	return false;
}

/* ------------------------------------------------ the operations */

static void receive(evre_base_t &d, unsigned c, unsigned op) {
	const uint8_t fn = chance(3) ? (uint8_t) rnd() : fns[below(sizeof fns)];
	uint8_t slave;
	switch (below(10)) {
		case 0:
		case 1:
			slave = 0;
			break;
		case 2:
			slave = (uint8_t) rnd();
			break;
		case 3:
			slave = 2;
			break;
		case 4:
			slave = 1;
			break;
		default:
			slave = d.SALVE_ID_REG;
			break;
	}
	const uint16_t off = pickOffset(&d);
	const uint16_t cnt = pickCount();
	uint32_t dataLen;
	if (carriesData(fn)) {
		switch (below(10)) {
			case 0:
				dataLen = cnt + 1U;
				break;
			case 1:
				dataLen = cnt ? cnt - 1U : 0;
				break;
			case 2:
				dataLen = below(40);
				break;
			default:
				dataLen = cnt;
				break;
		}
	} else {
		dataLen = chance(15) ? 1 + below(8) : 0;
	}
	if (dataLen > sizeof req - 16) {
		dataLen = below(64);
	}
	uint32_t n = 0;
	req[n++] = 0x7B;
	req[n++] = slave;
	req[n++] = fn;
	req[n++] = (uint8_t) off;
	req[n++] = (uint8_t) (off >> 8);
	req[n++] = (uint8_t) cnt;
	req[n++] = (uint8_t) (cnt >> 8);
	for (uint32_t i = 0; i < dataLen; ++i) {
		req[n++] = (uint8_t) rnd();
	}
	if (chance(3)) {
		req[0] = (uint8_t) rnd();
	}
	const uint16_t crc = GetCrc16(req, (int) n);
	req[n++] = (uint8_t) crc;
	req[n++] = (uint8_t) (crc >> 8);
	req[n++] = chance(3) ? (uint8_t) rnd() : 0x7D;
	if (chance(4)) {
		req[n - 3 + below(2)] ^= (uint8_t) (1 + below(255)); /* a bad CRC */
	}
	uint16_t size = (uint16_t) n;
	if (chance(2)) {
		size = (uint16_t) below(10); /* shorter than any frame */
	}

	uint8_t *buf = out;
	uint16_t max = sizeof out;
	switch (below(12)) {
		case 0:
			max = (uint16_t) below(16);
			break;
		case 1:
			max = (uint16_t) (10 + cnt - 1);
			break;
		case 2:
			max = (uint16_t) (10 + cnt);
			break;
		case 3:
			max = (uint16_t) below(64);
			break;
		case 4:
			if (chance(30)) {
				buf = nullptr;
			}
			break;
		default:
			break;
	}
	if (chance(6)) {
		buf = req; /* in place */
		max = sizeof req;
	}
	evre_base_t *dev = chance(1) ? nullptr : &d;
	uint8_t *packet = chance(1) ? nullptr : req;
	const uint32_t before = memoryHash(&d);
	frameSlave = slave;
	const bool allocating = chance(10);
	receiveTags(dev, packet, size, buf, max, allocating);
	/* D19: what the clear alone does, first, on the memory before the call */
	const bool cut = tagged("D19") && dev != nullptr && packet != nullptr && size == 10U + cnt;
	const uint32_t alone = cut ? clearAlone(d, packet) : 0;
	char clearField[4] = "x-";
	/* D24: the same call on a STATUS whose bit 14 is the setting, first, on
	 * the memory before the call */
	char settingField[64] = "k-";
	if (tagged("D24") && dev != nullptr && packet != nullptr) {
		withSettingBegin(d);
		if (allocating) {
			uint8_t *resp = (uint8_t*) 0x1;
			uint16_t rsize = 0xBEEF;
			const uint8_t ret = decodePacket(dev, packet, size, &resp, &rsize);
			withSettingEnd(d, settingField, sizeof settingField, ret, rsize, resp ? fnv(FNV_START, resp, rsize) : 0u);
			std::free(resp);
		} else {
			uint16_t len = 0xBEEF;
			const uint8_t ret = decodePacketInto(dev, packet, size, buf, max, &len);
			withSettingEnd(d, settingField, sizeof settingField, ret, len,
					(len != 0xBEEF && buf != nullptr) ? fnv(FNV_START, buf, len) : 0u);
		}
	}
	const uint16_t statusBefore = d.STATUS; /* D25: a mirror's must stay so */

	if (allocating) {
		uint8_t *resp = (uint8_t*) 0x1;
		uint16_t rsize = 0xBEEF;
		const uint8_t ret = decodePacket(dev, packet, size, &resp, &rsize);
		if (cut && ret == NO_ERROR) {
			std::strcpy(clearField, effectHash(ret, &d) == alone ? "x=" : "x!");
		}
		if (tagged("D25")) {
			std::strcpy(settingField, statusKept(d, statusBefore, resp, rsize, off, cnt));
		}
		std::printf("%u.%u a %u %u %08X %08X %08X b%08X l%08X i- s- %s %s %s h%08X t%s\n", c, op, ret, rsize,
				resp ? fnv(FNV_START, resp, rsize) : 0u, memoryHash(&d), events, before, looseHash(&d), clearField, settingField,
				errorField(resp, rsize, ret, off, cnt), eventsNoQueue, tagField());
		std::free(resp);
	} else {
		/* In place: the same frame first with an answer buffer of its own, on a
		 * copy of the memory, then for real. 1.1 must do both alike (D-2). */
		const bool inPlace = buf == req && dev != nullptr && packet != nullptr;
		char apartField[16] = "s-", inPlaceField[16] = "i-";
		if (inPlace) {
			static uint8_t apart[sizeof req];
			saveMemory(d);
			uint16_t apartLen = 0xBEEF;
			const uint8_t apartRet = decodePacketInto(dev, packet, size, apart, max, &apartLen);
			std::snprintf(apartField, sizeof apartField, "s%08X", outcomeHash(apartRet, apartLen, apart, &d));
			restoreMemory(d);
			resetEvents();
		}
		uint16_t len = 0xBEEF; /* 1.0 left it alone on the early refusals */
		const uint8_t ret = decodePacketInto(dev, packet, size, buf, max, &len);
		const uint32_t h = (len != 0xBEEF && buf != nullptr) ? fnv(FNV_START, buf, len) : 0u;
		if (inPlace) {
			std::snprintf(inPlaceField, sizeof inPlaceField, "i%08X", outcomeHash(ret, len, buf, &d));
		}
		if (cut && ret == NO_ERROR) {
			std::strcpy(clearField, effectHash(ret, &d) == alone ? "x=" : "x!");
		}
		if (tagged("D25")) {
			std::strcpy(settingField, statusKept(d, statusBefore, buf, len, off, cnt));
		}
		std::printf("%u.%u d %u %u %08X %08X %08X b%08X l%08X %s %s %s %s %s h%08X t%s\n", c, op, ret, len, h, memoryHash(&d),
				events, before, looseHash(&d), inPlaceField, apartField, clearField, settingField, errorField(buf, len, ret, off, cnt),
				eventsNoQueue, tagField());
	}
}

static void send(evre_base_t &d, unsigned c, unsigned op) {
	static uint8_t src[0x6100];
	const uint8_t fn = chance(10) ? (uint8_t) rnd() : fns[below(sizeof fns)];
	const uint16_t off = pickOffset(&d);
	const uint16_t cnt = pickCount();
	uint8_t *pData = nullptr;
	switch (below(4)) {
		case 0:
			if (cnt <= sizeof src) {
				for (unsigned i = 0; i < cnt; ++i) src[i] = (uint8_t) rnd();
				pData = src;
			}
			break;
		case 1: {
			/* the data already in outBuf: at DATAx(0), or a little after it */
			static const unsigned at[] = { 7, 7, 8, 9, 12 };
			const unsigned start = at[below(sizeof at / sizeof at[0])];
			if (cnt + start <= sizeof out) {
				for (unsigned i = 0; i < cnt; ++i) out[start + i] = (uint8_t) rnd();
				pData = &out[start];
			}
			break;
		}
		default:
			break;
	}
	uint16_t max = sizeof out;
	uint8_t *buf = out;
	switch (below(8)) {
		case 0:
			max = (uint16_t) below(16);
			break;
		case 1:
			max = (uint16_t) (10 + cnt - 1);
			break;
		case 2:
			if (chance(30)) {
				buf = nullptr;
			}
			break;
		default:
			break;
	}
	evre_base_t *dev = chance(1) ? nullptr : &d;
	const uint32_t before = memoryHash(&d);
	/* the order of the draws as before: the form first, then the slave id */
	const bool allocating = chance(10);
	const uint8_t slaveId = (uint8_t) rnd();
	sendTags(dev, slaveId, fn, off, cnt, pData, allocating);
	/* D24: the same call on a STATUS whose bit 14 is the setting, first */
	char settingField[64] = "k-";
	if (tagged("D24")) {
		withSettingBegin(d);
		if (allocating) {
			uint8_t *pk = (uint8_t*) 0x1;
			uint16_t ps = 0xBEEF;
			const uint8_t ret = encodePacket(dev, slaveId, fn, off, cnt, pData, &pk, &ps);
			withSettingEnd(d, settingField, sizeof settingField, ret, ps, pk ? fnv(FNV_START, pk, ps) : 0u);
			std::free(pk);
		} else {
			uint16_t len = 0xBEEF;
			const uint8_t ret = encodePacketInto(dev, slaveId, fn, off, cnt, pData, buf, max, &len);
			withSettingEnd(d, settingField, sizeof settingField, ret, len,
					(len != 0xBEEF && buf != nullptr) ? fnv(FNV_START, buf, len) : 0u);
		}
	}
	const uint16_t statusBefore = d.STATUS; /* D25: a mirror's must stay so */
	if (allocating) {
		uint8_t *pk = (uint8_t*) 0x1;
		uint16_t ps = 0xBEEF;
		const uint8_t ret = encodePacket(dev, slaveId, fn, off, cnt, pData, &pk, &ps);
		if (tagged("D25")) {
			std::strcpy(settingField, statusKept(d, statusBefore, pk, ps, off, cnt));
		}
		std::printf("%u.%u f %u %u %08X %08X %08X b%08X l%08X i- s- x- %s e- h%08X t%s\n", c, op, ret, ps,
				pk ? fnv(FNV_START, pk, ps) : 0u, memoryHash(&d), events, before, looseHash(&d), settingField, eventsNoQueue, tagField());
		std::free(pk);
	} else {
		uint16_t len = 0xBEEF;
		const uint8_t ret = encodePacketInto(dev, slaveId, fn, off, cnt, pData, buf, max, &len);
		const uint32_t h = (len != 0xBEEF && buf != nullptr) ? fnv(FNV_START, buf, len) : 0u;
		if (tagged("D25")) {
			std::strcpy(settingField, statusKept(d, statusBefore, buf, len, off, cnt));
		}
		std::printf("%u.%u e %u %u %08X %08X %08X b%08X l%08X i- s- x- %s e- h%08X t%s\n", c, op, ret, len, h, memoryHash(&d), events,
				before, looseHash(&d), settingField, eventsNoQueue, tagField());
	}
}

static void queue(evre_base_t &d, unsigned c, unsigned op) {
	uint8_t m = chance(10) ? 0 : (uint8_t) rnd();
	if (m == 0xFF && d.WRITE_HANDLER != nullptr) {
		m = 0xFE;
	}
	const uint8_t ret = addMsg(chance(1) ? nullptr : &d, m);
	std::printf("%u.%u m %u %08X\n", c, op, ret, memoryHash(&d));
}

/* protocolInit() on a table of its own, often a bad one */
static void init(unsigned c, unsigned op) {
	static evre_range_t table[5];
	const unsigned n = below(6);
	uint32_t next = 0xD000 - 2 + below(4);
	for (unsigned i = 0; i < n && i < 5; ++i) {
		table[i].start = (uint16_t) (chance(80) ? next + below(3) : 0xC000 + below(0x2100));
		table[i].len = (uint16_t) (chance(90) ? below(0x300) : 0x1000 + below(0x10));
		table[i].base = chance(10) ? nullptr : rw;
		table[i].writable = (uint8_t) below(2);
		next = (uint32_t) table[i].start + table[i].len;
	}
	evre_base_t other;
	other.D_RANGES = chance(10) ? nullptr : table;
	other.D_RANGE_CNT = (uint8_t) n;
	for (unsigned i = 0; i < 255; ++i) {
		other.MSG_ACK_HANDLER[i] = ackFns[i % 8];
	}
	other.STATUS = 0xFFFF;
	evre_base_t *dev = chance(2) ? nullptr : &other;
	/* A table protocolInit() refuses: 1.1 names that code 14 (it was 13) and
	 * empties the table. */
	tags[0] = 0;
	if (dev != nullptr && other.D_RANGES != nullptr) {
		addTag("D7");
		addTag("D17");
	}
	const uint8_t ret = protocolInit(dev);
	uint32_t slots = 0;
	for (unsigned i = 0; i < 255; ++i) {
		slots += other.MSG_ACK_HANDLER[i] != nullptr;
	}
	std::printf("%u.%u p %u %04X %u %u r%u t%s\n", c, op, ret, other.STATUS, other.A000 == nullptr, slots, other.D_RANGE_CNT,
			tagField());
}

int main(int argc, char **argv) {
	const unsigned cases = argc > 1 ? (unsigned) std::strtoul(argv[1], nullptr, 10) : 100000;
	if (argc > 2) {
		rngState ^= std::strtoull(argv[2], nullptr, 10) * 0x9E3779B97F4A7C15ULL;
	}

	for (unsigned c = 0; c < cases; ++c) {
		/* What the buffers line hashes at the end of this case, cleared first: a
		 * case shows what it wrote itself, not what an earlier one left. */
		const size_t seen = (c % 64 == 0) ? sizeof out : 0x1100;
		std::memset(out, 0, seen);
		std::memset(req, 0, seen);
		evre_base_t d;
		current = &d;
		d.SALVE_ID_REG = chance(3) ? 0 : (chance(90) ? 1 : (uint8_t) (2 + below(250)));
		const unsigned layout = below(10);
		const bool late = chance(25); /* the table set after protocolInit(), as some devices do */
		if (!late) {
			configure(d, layout);
		}
		const uint8_t initRet = protocolInit(&d);
		if (late) {
			configure(d, layout);
		}

		for (unsigned i = 0; i < sizeof ro; ++i) ro[i] = (uint8_t) rnd();
		for (unsigned i = 0; i < sizeof rw; ++i) rw[i] = (uint8_t) rnd();
		for (unsigned i = 0; i < sizeof big; i += 4) {
			const uint32_t v = rnd();
			std::memcpy(&big[i], &v, 4);
		}
		d.DEVICE_ID = (uint16_t) rnd();
		d.STATUS = (uint16_t) rnd();
		d.CONFIG = (uint16_t) rnd();
		d.ACCEPT_READ_RESP = chance(40) ? 1 : 0;
		acceptBroadcast = chance(50) ? 1 : 0;
		setAcceptBroadcast(d, acceptBroadcast, 0);
		if (chance(40)) {
			d.WRITE_HANDLER = onWrite;
			writeVerdict = writeVerdicts[below(sizeof writeVerdicts)];
			writeEffect = chance(30) ? 1 + below(3) : 0;
		}
		if (chance(40)) {
			d.READ_HANDLER = onRead;
			readVerdict = readVerdicts[below(sizeof readVerdicts)];
			readEffect = chance(30) ? 1 + below(3) : 0;
		}
		d.MSG_CNT = (uint8_t) (chance(20) ? 0 : (chance(10) ? 255 : below(12)));
		for (unsigned i = 0; i < sizeof d.MSG_BUFFER; ++i) {
			d.MSG_BUFFER[i] = chance(15) ? 0 : (uint8_t) (1 + below(255));
		}
		for (unsigned i = 0; i < 255; ++i) {
			if (chance(30)) {
				d.MSG_ACK_HANDLER[i] = ackFns[i % 8];
			}
		}
		std::printf("%u layout %u late %u init %u broadcast %u mirror %u\n", c, layout, late, initRet, acceptBroadcast,
				d.ACCEPT_READ_RESP);

		const unsigned ops = 1 + below(3);
		for (unsigned op = 0; op < ops; ++op) {
			keepAcksDefined(d);
			resetEvents();
			const unsigned what = below(100);
			if (what < 68) {
				receive(d, c, op);
			} else if (what < 90) {
				send(d, c, op);
			} else if (what < 97) {
				queue(d, c, op);
			} else {
				init(c, op);
			}
		}
		/* Anything written outside the answer shows here. The largest answer
		 * (a READ of the whole bank) fits in the first 0x1100 bytes; every 64th
		 * case hashes the whole buffers. */
		std::printf("%u buffers %08X %08X\n", c, fnv(FNV_START, out, seen), fnv(FNV_START, req, seen));
	}
	return rxWrong != 0 ? 3 : 0; /* a handler saw a wrong RX_SLAVE_ID */
}

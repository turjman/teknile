/* SPDX-License-Identifier: Apache-2.0 */
/*
 * guard_desc_test.cpp: EVRe Guard part 2, the register checks, on a PC, built
 * by run_lib_tests.py with the library, part 1 and part 2.
 *
 * One check or more for each decision of part 2 ("D-26:" onward, GUARD_PLAN.md
 * section 11: G-1 is D-26 ... G-25 is D-50) and one for each row of the wire
 * table (GUARD_PLAN.md section 4, "wire:"). The frames go through
 * decodePacketInto, as a device decodes them, and each refusal is checked for
 * its code, its answer (or its silence), the echoed offset and count, the
 * memory unchanged byte for byte and HEARTBEAT left alone.
 *
 *   -DNO_CHECK      the four functions of part 2 as a device without it would
 *                   have them (every write passes): the runner counts which
 *                   decisions' checks fail there, as they must.
 *   -include lock_hooks.h   the lock build: evre_guard_check_write takes no
 *                   lock, evre_guard_check_init and evre_guard_check_last one
 *                   each.
 */

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#define HAS_MXCSR 1
#endif

#include "EVRe.h"
#include "evre_guard.h"
#include "evre_guard_desc.h"

static int failed = 0;

static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failed++;
	}
}

#ifdef NO_CHECK
/* A device without part 2: the table is kept and never asked, every write passes. */
uint8_t evre_guard_check_init(evre_guard_check_t *check, const evre_guard_table_t *table, const evre_base_t *) {
	if (check == nullptr) {
		return PERMISSION_DENIED;
	}
	check->table = table;
	check->refused = 0;
	check->last_addr = 0;
	check->last_why = 0;
	return NO_ERROR;
}
uint8_t evre_guard_check_write(evre_guard_check_t *, const evre_base_t *, uint16_t, const uint8_t *, uint16_t) {
	return NO_ERROR;
}
uint8_t evre_guard_write_checked(evre_guard_t *guard, evre_guard_check_t *, const evre_base_t *device, uint16_t offset,
		const uint8_t *data, uint16_t count) {
	return evre_guard_write(guard, device, offset, data, count);
}
uint32_t evre_guard_check_last(evre_guard_check_t *check, uint16_t *addr, uint8_t *why) {
	if (addr != nullptr) {
		*addr = check->last_addr;
	}
	if (why != nullptr) {
		*why = check->last_why;
	}
	return check->refused;
}
#endif

#ifdef EVRE_TEST_LOCK_HOOKS
static unsigned locks, unlocks, depth, deepest;
void evre_test_lock(void) {
	++locks;
	if (++depth > deepest) {
		deepest = depth;
	}
}
void evre_test_unlock(void) {
	++unlocks;
	--depth;
}
#endif

/* ------------------------------------------------------------ the frames */

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

static uint8_t answer[600];
static uint16_t answerLen;

/* One frame through decodePacketInto. at: the frame's place in its buffer (1, 2, 3 put the data at every
 * alignment); inPlace: the answer built over the frame itself. */
static uint8_t ask(evre_base_t *dev, uint8_t fn, uint16_t off, uint16_t cnt, const void *data = nullptr, uint8_t slave = 1,
		uint16_t outMax = sizeof answer, unsigned at = 0, bool inPlace = false) {
	static uint8_t buffer[700];
	uint8_t *req = buffer + at;
	const uint16_t n = frame(req, slave, fn, off, cnt, static_cast<const uint8_t *>(data), hasData(fn) ? cnt : 0);
	answerLen = 0;
	if (inPlace) {
		const uint8_t ret = decodePacketInto(dev, req, n, req, uint16_t(sizeof buffer - at), &answerLen);
		std::memcpy(answer, req, answerLen);
		return ret;
	}
	return decodePacketInto(dev, req, n, answer, outMax, &answerLen);
}

/* an ERROR_RESP with this code that echoes the request's offset and count */
static bool answeredWith(uint8_t code, uint16_t off, uint16_t cnt) {
	return answerLen == 11 && answer[2] == ERROR_RESP && answer[7] == code && answer[3] == uint8_t(off)
			&& answer[4] == uint8_t(off >> 8) && answer[5] == uint8_t(cnt) && answer[6] == uint8_t(cnt >> 8);
}

/* bits of a float, and little-endian bytes */
static uint32_t bitsOf(float f) {
	uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}
static void put(uint8_t *at, uint32_t value, unsigned size) {
	for (unsigned ind = 0; ind < size; ++ind) {
		at[ind] = uint8_t(value >> (8U * ind));
	}
}

/* ------------------------------------------------------------ the device */

/* 0xD000..0xD007 read-only, 0xD008..0xD03F read-write (the registers below), 0xD040..0xD04F the login register */
static uint8_t ro[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, rw[56], login[16];
static const evre_range_t ranges[] = { { 0xD000, 8, ro, 0 }, { 0xD008, 56, rw, 1 }, { 0xD040, 16, login, 1 } };

enum : uint16_t {
	U8 = 0xD008,     /* 5 .. 200, or 0 "off" */
	I8 = 0xD009,     /* -100 .. 100 */
	U16 = 0xD00A,    /* 10 .. 60000 */
	I16 = 0xD00C,    /* -1000 .. 1000, or -32768 "unset" */
	GAP = 0xD00E,    /* two bytes no entry covers */
	U32 = 0xD010,    /* 100 .. 0xF0000000 */
	I32 = 0xD014,    /* the type's ends */
	F32 = 0xD018,    /* 0 .. 24.0 */
	F32ALL = 0xD01C, /* the type's ends, -FLT_MAX .. FLT_MAX: a register the device clamps (D-36) */
	BYTES = 0xD020,  /* 8 bytes, a name */
	RO = 0xD028,     /* a read-only register inside the writable range: no entry */
	U8ALL = 0xD029,  /* 0 .. 255 */
	U16ALL = 0xD02A, /* 0 .. 0xFFFF */
	F32LIST = 0xD02C, /* 1.0 .. 10.0, or 0 */
	PAST = 0xD030    /* past the last entry, in the writable range */
};

static const uint32_t values[] = { 0x00000000UL, 0xFFFF8000UL, 0x00000000UL };

static evre_guard_desc_t regs[] = {
	/* addr, size, type, flags, n_values, spare1, first_value, spare2, min, max, zero_bits */
	{ U8, 1, EVRE_GUARD_U8, 0, 1, 0, 0, 0, 5UL, 200UL, 0 },
	{ I8, 1, EVRE_GUARD_I8, 0, 0, 0, 0, 0, 0xFFFFFF9CUL, 100UL, 0 },
	{ U16, 2, EVRE_GUARD_U16, 0, 0, 0, 0, 0, 10UL, 60000UL, 0 },
	{ I16, 2, EVRE_GUARD_I16, 0, 1, 0, 1, 0, 0xFFFFFC18UL, 1000UL, 0 },
	{ U32, 4, EVRE_GUARD_U32, 0, 0, 0, 0, 0, 100UL, 0xF0000000UL, 0 },
	{ I32, 4, EVRE_GUARD_I32, 0, 0, 0, 0, 0, 0x80000000UL, 0x7FFFFFFFUL, 0 },
	{ F32, 4, EVRE_GUARD_F32, 0, 0, 0, 0, 0, 0x00000000UL, 0x41C00000UL, 0 },
	{ F32ALL, 4, EVRE_GUARD_F32, 0, 0, 0, 0, 0, 0xFF7FFFFFUL, 0x7F7FFFFFUL, 0 },
	{ BYTES, 8, EVRE_GUARD_BYTES, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ U8ALL, 1, EVRE_GUARD_U8, 0, 0, 0, 0, 0, 0UL, 0xFFUL, 0 },
	{ U16ALL, 2, EVRE_GUARD_U16, 0, 0, 0, 0, 0, 0UL, 0xFFFFUL, 0 },
	{ F32LIST, 4, EVRE_GUARD_F32, 0, 1, 0, 2, 0, 0x3F800000UL, 0x41200000UL, 0 },
};
static const uint16_t N_REGS = sizeof regs / sizeof regs[0];
static evre_guard_table_t table = { regs, values, N_REGS, 3 };

static uint64_t clockMs = 1000;
static uint64_t fakeClock() { return clockMs; }
static const uint8_t token[16] = { 's', 'e', 'c', 'r', 'e', 't' };
static const evre_guard_config_t loginConfig = { 0xD040, 16, token, nullptr, 0, 3, 1000, 8000, 5000, fakeClock };

static evre_guard_t guard;
static evre_guard_check_t chk;
static unsigned asked; /* how often the library asked the write handler */

static uint8_t checkOnly(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	++asked;
	return evre_guard_check_write(&chk, d, off, data, cnt);
}
static uint8_t withLogin(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	++asked;
	return evre_guard_write_checked(&guard, &chk, d, off, data, cnt);
}
static uint8_t readLogin(evre_base_t *, uint16_t off, uint16_t cnt) {
	return evre_guard_read(&guard, off, cnt);
}

/* the device of every check: the ranges above, the check alone (the firmware's wiring), a good table */
static void device(evre_base_t &dev, bool broadcasts = false) {
	dev.SALVE_ID_REG = 1;
	dev.DEVICE_ID = 0x1234;
	dev.D_RANGES = ranges;
	dev.D_RANGE_CNT = 3;
	dev.ACCEPT_BROADCAST_D000 = broadcasts ? 1 : 0;
	protocolInit(&dev);
	evre_guard_check_init(&chk, &table, &dev);
	dev.WRITE_HANDLER = checkOnly;
	std::memset(rw, 0, sizeof rw);
	asked = 0;
}

/* memory before a frame, to see that a refusal stored nothing */
static uint8_t before[sizeof rw];
static uint16_t configBefore;
static void snap(evre_base_t &dev) {
	std::memcpy(before, rw, sizeof rw);
	dev.CONFIG = 0; /* HEARTBEAT clear, so a frame that sets it shows */
	configBefore = dev.CONFIG;
}
static bool untouched(const evre_base_t &dev) {
	return std::memcmp(before, rw, sizeof rw) == 0 && dev.CONFIG == configBefore;
}

/* one whole register written with WRITE_ACK: the code (NO_ERROR when it was stored and acknowledged) */
static uint8_t writeValue(evre_base_t &dev, uint16_t addr, uint32_t value, unsigned size) {
	uint8_t data[4];
	put(data, value, size);
	snap(dev);
	const uint8_t ret = ask(&dev, WRITE_ACK, addr, uint16_t(size), data);
	if (ret == NO_ERROR) {
		return answerLen == 10 && answer[2] == WRITE_ACK_RESP && std::memcmp(rw + (addr - 0xD008), data, size) == 0
				&& (dev.CONFIG & 1) ? NO_ERROR : 0xEE;
	}
	return answeredWith(ret, addr, uint16_t(size)) && untouched(dev) ? ret : 0xEF;
}

/* ------------------------------------------------- the wire table, row by row */

static void wire() {
	evre_base_t dev;
	device(dev);
	uint8_t good[12];
	put(good, 50, 1);            /* U8 */
	put(good + 1, 0xF6, 1);      /* I8: -10 */
	put(good + 2, 1000, 2);      /* U16 */
	put(good + 4, 0xFC18, 2);    /* I16: -1000 */
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 6, good) == NO_ERROR && answerLen == 10 && answer[2] == WRITE_ACK_RESP
			&& std::memcmp(rw, good, 6) == 0 && (dev.CONFIG & 1),
			"wire: a unicast WRITE_ACK, every register good: acknowledged, all stored, HEARTBEAT set");
	std::memset(rw, 0, sizeof rw);
	snap(dev);
	check(ask(&dev, WRITE, U8, 6, good) == NO_ERROR && answerLen == 0 && std::memcmp(rw, good, 6) == 0 && (dev.CONFIG & 1),
			"wire: a unicast WRITE, every register good: silent, all stored, HEARTBEAT set");

	uint8_t bad[6];
	std::memcpy(bad, good, 6);
	put(bad + 2, 5, 2); /* U16 below 10 */
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 6, bad) == VALUE_REFUSED && answeredWith(VALUE_REFUSED, U8, 6) && untouched(dev),
			"wire: a unicast WRITE_ACK, one value bad: ERROR_RESP(15) echoing the request's offset and count, none stored, "
			"no HEARTBEAT");
	snap(dev);
	check(ask(&dev, WRITE, U8, 6, bad) == VALUE_REFUSED && answeredWith(VALUE_REFUSED, U8, 6) && untouched(dev),
			"wire: a plain WRITE with a bad value is answered too: ERROR_RESP(15), none stored");

	snap(dev);
	check(ask(&dev, WRITE_ACK, U16, 1, good) == PERMISSION_DENIED && answeredWith(PERMISSION_DENIED, U16, 1) && untouched(dev),
			"wire: only part of a number register: ERROR_RESP(3), none stored");
	snap(dev);
	check(ask(&dev, WRITE_ACK, GAP, 2, good) == PERMISSION_DENIED && answeredWith(PERMISSION_DENIED, GAP, 2) && untouched(dev),
			"wire: a byte no entry covers: ERROR_RESP(3), none stored");
	const uint8_t name[3] = { 'a', 'b', 'c' };
	snap(dev);
	check(ask(&dev, WRITE_ACK, BYTES + 2, 3, name) == NO_ERROR && answerLen == 10 && std::memcmp(rw + 0x1A, name, 3) == 0,
			"wire: part of a bytes register, the rest good: acknowledged, all stored");
	check(evre_guard_check_write(&chk, &dev, U16, good, 0) == NO_ERROR && evre_guard_check_write(&chk, &dev, GAP, nullptr, 0) == NO_ERROR,
			"wire: a count of 0 passes, wherever it points, and its data is not read");

	const uint8_t config[2] = { 0x04, 0x00 };
	check(ask(&dev, WRITE_ACK, 0xA004, 2, config) == NO_ERROR && dev.CONFIG == 0x0005,
			"wire: the reserved bank is not checked: CONFIG written as today");

	/* with a login */
	evre_guard_init(&guard, &loginConfig);
	dev.WRITE_HANDLER = withLogin;
	dev.READ_HANDLER = readLogin;
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 6, bad) == LOGIN_REQUIRED && answeredWith(LOGIN_REQUIRED, U8, 6) && untouched(dev),
			"wire: with a login and no session: 13, part 1 answers and no value is looked at");
	check(ask(&dev, WRITE_ACK, 0xD040, 16, token) == NO_ERROR && answerLen == 10 && guard.logged_in && login[0] == 0,
			"wire: a login: part 1 answers (EVRE_HANDLED, acknowledged, the token not stored)");
	snap(dev);
	check(ask(&dev, WRITE_ACK, 0xD03E, 4, good) == PERMISSION_DENIED && untouched(dev),
			"wire: a write over the login register in a session: part 1 answers 3");
	dev.WRITE_HANDLER = checkOnly;
	dev.READ_HANDLER = nullptr;

	/* broadcasts */
	evre_base_t all;
	device(all, true);
	snap(all);
	check(ask(&all, WRITE, U8, 6, good, BROADCAST_ID) == NO_ERROR && answerLen == 0 && std::memcmp(rw, good, 6) == 0,
			"wire: a broadcast (ACCEPT_BROADCAST_D000 1), every register good: silent, all stored");
	snap(all);
	check(ask(&all, WRITE, U8, 6, bad, BROADCAST_ID) == VALUE_REFUSED && answerLen == 0 && untouched(all),
			"wire: a broadcast, one register bad: silent, none stored for the whole broadcast");
	snap(all);
	check(ask(&all, WRITE, GAP, 2, good, BROADCAST_ID) == PERMISSION_DENIED && answerLen == 0 && untouched(all),
			"wire: a broadcast over a gap: 3, silent, none stored");

	/* a bad table */
	evre_guard_check_t none;
	std::memset(&none, 0, sizeof none);
	evre_guard_check_t keep = chk;
	chk = none;
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 1, good) == PERMISSION_DENIED && answeredWith(PERMISSION_DENIED, U8, 1) && untouched(dev),
			"wire: no good table: every write to the device bank refused, ERROR_RESP(3)");
	check(ask(&dev, WRITE_ACK, 0xA004, 2, config) == NO_ERROR && dev.CONFIG == 0x0005,
			"wire: no good table: the reserved bank works as today (CONFIG lands)");
	snap(all);
	all.WRITE_HANDLER = checkOnly;
	check(ask(&all, WRITE, U8, 1, good, BROADCAST_ID) == PERMISSION_DENIED && answerLen == 0 && untouched(all),
			"wire: no good table: a broadcast into the device bank is refused silently");
	chk = keep;

	/* a mirror */
	evre_base_t mirror;
	device(mirror);
	mirror.ACCEPT_READ_RESP = 1;
	evre_guard_check_t mirrorCheck;
	check(evre_guard_check_init(&mirrorCheck, &table, &mirror) == PERMISSION_DENIED && mirrorCheck.table == nullptr,
			"wire: a mirror: init refuses it");
	snap(mirror);
	check(ask(&mirror, READ_RESP, U8, 1, good) == PERMISSION_DENIED && answerLen == 0 && untouched(mirror),
			"wire: a mirror: a READ_RESP refused (3), silent, not stored");
	snap(mirror);
	check(ask(&mirror, WRITE_ACK, U8, 1, good) == PERMISSION_DENIED && answeredWith(PERMISSION_DENIED, U8, 1) && untouched(mirror),
			"wire: a mirror: every write refused (3)");

	/* the room for an answer */
	device(dev);
	snap(dev);
	check(ask(&dev, WRITE_ACK, U16, 2, bad + 2, 1, 10) == VALUE_REFUSED && answerLen == 0 && untouched(dev),
			"wire: a refused WRITE_ACK with an output buffer of exactly 10 B: silent (an ERROR_RESP needs 11 B), none stored");

	/* what part 2 is never asked about */
	asked = 0;
	check(ask(&dev, READ, U8, 6) == NO_ERROR && answerLen == 16 && asked == 0, "wire: a READ: part 2 is not asked");
	check(ask(&dev, READ_RESP, U8, 1, good) == FUNCTION_CODE_ERR && answerLen == 0 && asked == 0,
			"wire: a READ_RESP sent to a device: refused by the library (2), silent, never asked");
	check(ask(&dev, WRITE_ACK_RESP, U8, 1) == FUNCTION_CODE_ERR && answerLen == 0 && asked == 0,
			"wire: a WRITE_ACK_RESP sent to a device: 2, silent, never asked");
	snap(dev);
	check(ask(&dev, WRITE_ACK, 0xD000, 2, good) == PERMISSION_DENIED && asked == 0 && untouched(dev)
			&& ask(&dev, WRITE_ACK, 0xE000, 2, good) == PERMISSION_DENIED && asked == 0
			&& ask(&dev, WRITE_ACK, 0xD04E, 4, good) == REG_CNT_OUT_OF_RANGE && asked == 0
			&& ask(&dev, WRITE_ACK, U8, 2, good, 1, 9) == BUFFER_TOO_SMALL && asked == 0,
			"wire: a frame the library refuses (a read-only range, an unknown bank, past a range, no room): never asked");
}

/* ------------------------------------------------------- the decisions */

static void values_() {
	evre_base_t dev;
	device(dev);
	/* every integer type at min - 1, min, max, max + 1 */
	check(writeValue(dev, U8, 4, 1) == VALUE_REFUSED && writeValue(dev, U8, 5, 1) == NO_ERROR && writeValue(dev, U8, 200, 1) == NO_ERROR
			&& writeValue(dev, U8, 201, 1) == VALUE_REFUSED, "D-26: u8 5 .. 200: 4 and 201 refused (15, never clamped), 5 and 200 pass");
	check(writeValue(dev, I8, 0x9B, 1) == VALUE_REFUSED && writeValue(dev, I8, 0x9C, 1) == NO_ERROR && writeValue(dev, I8, 100, 1) == NO_ERROR
			&& writeValue(dev, I8, 101, 1) == VALUE_REFUSED, "D-26: i8 -100 .. 100: -101 and 101 refused, -100 and 100 pass");
	check(writeValue(dev, U16, 9, 2) == VALUE_REFUSED && writeValue(dev, U16, 10, 2) == NO_ERROR && writeValue(dev, U16, 60000, 2) == NO_ERROR
			&& writeValue(dev, U16, 60001, 2) == VALUE_REFUSED, "D-26: u16 10 .. 60000: 9 and 60001 refused, 10 and 60000 pass");
	check(writeValue(dev, I16, 0xFC17, 2) == VALUE_REFUSED && writeValue(dev, I16, 0xFC18, 2) == NO_ERROR
			&& writeValue(dev, I16, 1000, 2) == NO_ERROR && writeValue(dev, I16, 1001, 2) == VALUE_REFUSED,
			"D-26: i16 -1000 .. 1000: -1001 and 1001 refused, -1000 and 1000 pass");
	check(writeValue(dev, I16, 0xFFFF, 2) == NO_ERROR, "D-26: -1 as i16 (0xFFFF) is -1, inside -1000 .. 1000: a signed register is not compared unsigned");
	check(writeValue(dev, U32, 99, 4) == VALUE_REFUSED && writeValue(dev, U32, 100, 4) == NO_ERROR
			&& writeValue(dev, U32, 0x80000000UL, 4) == NO_ERROR && writeValue(dev, U32, 0xF0000000UL, 4) == NO_ERROR
			&& writeValue(dev, U32, 0xF0000001UL, 4) == VALUE_REFUSED && writeValue(dev, U32, 0xFFFFFFFFUL, 4) == VALUE_REFUSED,
			"D-26: u32 100 .. 0xF0000000: above 0x7FFFFFFF compared unsigned; 0xF0000001 and UINT32_MAX refused");
	check(writeValue(dev, I32, 0x80000000UL, 4) == NO_ERROR && writeValue(dev, I32, 0x7FFFFFFFUL, 4) == NO_ERROR
			&& writeValue(dev, I32, 0xFFFFFFFFUL, 4) == NO_ERROR,
			"D-26: i32 at the type's ends (INT32_MIN, INT32_MAX, -1): pass");
	check(writeValue(dev, U8ALL, 0xFF, 1) == NO_ERROR && writeValue(dev, U8ALL, 0, 1) == NO_ERROR
			&& writeValue(dev, U16ALL, 0xFFFF, 2) == NO_ERROR, "D-26: a register without limits (the type's ends): every value passes");

	check(VALUE_REFUSED == 15 && writeValue(dev, U8, 201, 1) == VALUE_REFUSED && writeValue(dev, GAP, 0, 2) == PERMISSION_DENIED,
			"D-27: VALUE_REFUSED is 15, for a value; a place is 3");
	snap(dev);
	check(ask(&dev, WRITE_ACK, GAP, 2, "\0\0") == PERMISSION_DENIED && answeredWith(PERMISSION_DENIED, GAP, 2),
			"D-27: a byte that cannot be written this way: 3, not 15");

	/* specials */
	check(writeValue(dev, U8, 0, 1) == NO_ERROR, "D-33: a special outside the limits passes (u8 0 \"off\", min 5)");
	check(writeValue(dev, I16, 0x8000, 2) == NO_ERROR && writeValue(dev, I16, 0x8001, 2) == VALUE_REFUSED,
			"D-33: an i16 special (-32768, sign-extended in the list) passes; -32767 does not");
	check(writeValue(dev, U8, 1, 1) == VALUE_REFUSED, "D-33: a value next to a special is still refused");
}

static void floats() {
	evre_base_t dev;
	device(dev);
	const float max = 24.0f;
	check(writeValue(dev, F32, bitsOf(0.0f), 4) == NO_ERROR && writeValue(dev, F32, bitsOf(max), 4) == NO_ERROR
			&& writeValue(dev, F32, bitsOf(12.5f), 4) == NO_ERROR, "D-49: f32 0 .. 24: 0, 12.5 and 24 pass");
	check(writeValue(dev, F32, bitsOf(std::nextafter(max, 100.0f)), 4) == VALUE_REFUSED
			&& writeValue(dev, F32, bitsOf(std::nextafter(0.0f, -1.0f)), 4) == VALUE_REFUSED,
			"D-49: the next float above the max, and the negative subnormal below 0: refused");
	check(writeValue(dev, F32, 0x80000000UL, 4) == NO_ERROR, "D-49: -0.0 against a min of 0: it is 0, it passes");
	check(writeValue(dev, F32, 0x00000001UL, 4) == NO_ERROR && writeValue(dev, F32, 0x007FFFFFUL, 4) == NO_ERROR,
			"D-49: positive subnormals inside 0 .. 24 pass");
	check(writeValue(dev, F32, bitsOf(-1.0f), 4) == VALUE_REFUSED && writeValue(dev, F32, bitsOf(-FLT_MAX), 4) == VALUE_REFUSED
			&& writeValue(dev, F32, bitsOf(FLT_MAX), 4) == VALUE_REFUSED, "D-49: -1, -FLT_MAX and FLT_MAX against 0 .. 24: refused");
	check(writeValue(dev, F32ALL, bitsOf(FLT_MAX), 4) == NO_ERROR && writeValue(dev, F32ALL, bitsOf(-FLT_MAX), 4) == NO_ERROR,
			"D-49: FLT_MAX and -FLT_MAX against the type's ends: pass");
#ifdef HAS_MXCSR
	const unsigned saved = _mm_getcsr();
	_mm_setcsr(saved | 0x8040U); /* flush-to-zero and denormals-are-zero on: a float compare would read subnormals as 0 */
	const bool ftz = writeValue(dev, F32, bitsOf(std::nextafter(0.0f, -1.0f)), 4) == VALUE_REFUSED
			&& writeValue(dev, F32, 0x80000001UL, 4) == VALUE_REFUSED && writeValue(dev, F32, 0x00000001UL, 4) == NO_ERROR;
	_mm_setcsr(saved);
	check(ftz, "D-49: with the FPU's flush-to-zero on, a negative subnormal is still refused against a min of 0 (no float compare)");
#else
	check(true, "D-49: flush-to-zero (no MXCSR on this machine: the compare uses no float type anyway)");
#endif

	/* NaN and the infinities, always */
	const uint32_t nans[] = { 0x7FC00000UL, 0x7F800001UL, 0xFFC00000UL, 0x7FFFFFFFUL, 0xFF800001UL };
	bool refusedAll = true;
	for (uint32_t nan : nans) {
		refusedAll = refusedAll && writeValue(dev, F32, nan, 4) == VALUE_REFUSED && writeValue(dev, F32ALL, nan, 4) == VALUE_REFUSED;
	}
	check(refusedAll, "D-32: quiet, signalling and negative NaN: refused (15), with limits and without");
	check(writeValue(dev, F32ALL, 0x7F800000UL, 4) == VALUE_REFUSED && writeValue(dev, F32ALL, 0xFF800000UL, 4) == VALUE_REFUSED
			&& writeValue(dev, F32, 0x7F800000UL, 4) == VALUE_REFUSED,
			"D-32: +infinity and -infinity: refused, also on a register with the type's full range");
	uint16_t addr = 0;
	uint8_t why = 0;
	writeValue(dev, F32, 0x7FC00000UL, 4);
	evre_guard_check_last(&chk, &addr, &why);
	check(addr == F32 && why == EVRE_GUARD_WHY_NOT_FINITE, "D-32: the reason is NOT_FINITE, the register F32's");

	check(writeValue(dev, F32ALL, bitsOf(1e30f), 4) == NO_ERROR && writeValue(dev, F32ALL, bitsOf(-1e-30f), 4) == NO_ERROR,
			"D-36: a register the device clamps (the type's full range in its entry): every finite value passes");

	check(writeValue(dev, F32LIST, 0x00000000UL, 4) == NO_ERROR && writeValue(dev, F32LIST, 0x80000000UL, 4) == NO_ERROR,
			"D-33: a listed 0 takes -0.0 too (1.0 .. 10.0, or 0)");
	check(writeValue(dev, F32LIST, bitsOf(0.5f), 4) == VALUE_REFUSED && writeValue(dev, F32LIST, bitsOf(1.0f), 4) == NO_ERROR,
			"D-33: 0.5, not listed and below 1.0: refused; 1.0 passes");
}

static void spans() {
	evre_base_t dev;
	device(dev);
	uint8_t data[40] = { 0 };
	put(data, 50, 1);
	put(data + 2, 1000, 2);
	snap(dev);
	check(ask(&dev, WRITE_ACK, U16 + 1, 1, data) == PERMISSION_DENIED && untouched(dev), "D-30: the last byte of a u16: 3");
	snap(dev);
	check(ask(&dev, WRITE_ACK, U32 + 1, 3, data) == PERMISSION_DENIED && untouched(dev), "D-30: the last three bytes of a u32: 3");
	snap(dev);
	check(ask(&dev, WRITE_ACK, U32, 2, data) == PERMISSION_DENIED && untouched(dev), "D-30: the first two bytes of a u32: 3");
	snap(dev);
	check(ask(&dev, WRITE_ACK, U16 + 1, 3, data) == PERMISSION_DENIED && untouched(dev),
			"D-30: a span that starts inside a register: 3, none stored");
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 3, data) == PERMISSION_DENIED && untouched(dev),
			"D-30: a span that ends inside the next register: 3, none stored");
	uint16_t addr = 0;
	uint8_t why = 0;
	evre_guard_check_last(&chk, &addr, &why);
	check(addr == U16 && why == EVRE_GUARD_WHY_PART, "D-30: the reason is PART, the register the one cut");
	const uint8_t name[8] = { 'n', 'a', 'm', 'e', '1', '2', '3', '4' };
	check(ask(&dev, WRITE_ACK, BYTES + 7, 1, name) == NO_ERROR && ask(&dev, WRITE_ACK, BYTES, 1, name) == NO_ERROR
			&& ask(&dev, WRITE_ACK, BYTES + 3, 4, name) == NO_ERROR && ask(&dev, WRITE_ACK, BYTES, 8, name) == NO_ERROR
			&& std::memcmp(rw + 0x18, name, 8) == 0, "D-30: any part of a bytes register may be written");

	snap(dev);
	check(ask(&dev, WRITE_ACK, GAP, 1, data) == PERMISSION_DENIED && untouched(dev) && evre_guard_check_last(&chk, &addr, &why)
			&& addr == GAP && why == EVRE_GUARD_WHY_NOT_WRITABLE, "D-31: a gap: 3, the reason NOT_WRITABLE at the gap");
	snap(dev);
	check(ask(&dev, WRITE_ACK, I16, 4, data) == PERMISSION_DENIED && untouched(dev),
			"D-31: a write across the gap after a good register: 3, none stored");
	snap(dev);
	check(ask(&dev, WRITE_ACK, RO, 1, data) == PERMISSION_DENIED && untouched(dev) && evre_guard_check_last(&chk, &addr, &why)
			&& addr == RO && why == EVRE_GUARD_WHY_NOT_WRITABLE,
			"D-31: a read-only register inside a writable range (the library cannot see it): 3, NOT_WRITABLE");

	snap(dev);
	check(ask(&dev, WRITE_ACK, PAST, 4, data) == PERMISSION_DENIED && untouched(dev), "D-31: a span past the last entry: 3");
	evre_guard_check_last(&chk, &addr, &why);
	check(addr == PAST && why == EVRE_GUARD_WHY_NOT_WRITABLE, "D-31: the reason is NOT_WRITABLE, at the first byte no entry covers");
	check(evre_guard_check_write(&chk, &dev, U8, nullptr, 1) == PERMISSION_DENIED, "D-31: a null data pointer: 3");
	check(evre_guard_check_write(&chk, &dev, 0xCFFF, data, 2) == PERMISSION_DENIED
			&& evre_guard_check_write(&chk, &dev, 0xDFFF, data, 2) == PERMISSION_DENIED,
			"D-31: a span outside 0xD000..0xDFFF (the library never hands one in): 3");

	/* several registers in one frame */
	uint8_t block[24];
	put(block, 50, 1);
	put(block + 1, 10, 1);
	put(block + 2, 500, 2);
	put(block + 4, 7, 2);
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 6, block) == NO_ERROR && std::memcmp(rw, block, 6) == 0,
			"D-26: several registers, all good: all stored");
	std::memset(rw, 0, sizeof rw);
	const struct {
		unsigned at;
		uint32_t value;
		unsigned size;
		const char *what;
	} bad[] = { { 0, 201, 1, "D-26: several registers, the first bad: 15, none of them stored" },
		{ 2, 5, 2, "D-26: several registers, one in the middle bad: 15, none stored" },
		{ 4, 1001, 2, "D-26: several registers, the last bad: 15, none stored" } };
	for (const auto &one : bad) {
		uint8_t frame6[6];
		std::memcpy(frame6, block, 6);
		put(frame6 + one.at, one.value, one.size);
		snap(dev);
		check(ask(&dev, WRITE_ACK, U8, 6, frame6) == VALUE_REFUSED && answeredWith(VALUE_REFUSED, U8, 6) && untouched(dev), one.what);
	}
	/* a long block: every register from U32 to the bytes register */
	uint8_t longer[24] = { 0 };
	put(longer, 100, 4);
	put(longer + 8, bitsOf(3.5f), 4);
	put(longer + 12, bitsOf(-7.0f), 4);
	snap(dev);
	check(ask(&dev, WRITE_ACK, U32, 24, longer) == NO_ERROR && std::memcmp(rw + 8, longer, 24) == 0,
			"D-26: a block of six registers, a bytes register last: all stored");
	put(longer + 12, 0x7FC00000UL, 4);
	snap(dev);
	check(ask(&dev, WRITE_ACK, U32, 24, longer) == VALUE_REFUSED && untouched(dev), "D-26: the same block with a NaN in it: 15, none stored");
}

/* the frame at every alignment, and decoded in place: no unaligned load can hide (UBSan's alignment check in
 * the Linux sanitizer build sees a cast of the data pointer) */
static void alignment() {
	evre_base_t dev;
	device(dev);
	uint8_t data[4];
	bool all = true;
	for (unsigned at = 0; at < 4; ++at) {
		put(data, bitsOf(20.0f), 4);
		snap(dev);
		all = all && ask(&dev, WRITE_ACK, F32, 4, data, 1, sizeof answer, at) == NO_ERROR && rw[0x10] == data[0];
		put(data, bitsOf(30.0f), 4);
		snap(dev);
		all = all && ask(&dev, WRITE_ACK, F32, 4, data, 1, sizeof answer, at) == VALUE_REFUSED && untouched(dev);
		put(data, 0xFFFFFC17UL, 4);
		snap(dev);
		all = all && ask(&dev, WRITE_ACK, U32 + 4, 4, data, 1, sizeof answer, at) == NO_ERROR;
		snap(dev);
		all = all && ask(&dev, WRITE_ACK, F32, 4, data, 1, sizeof answer, at, true) == VALUE_REFUSED && untouched(dev)
				&& answer[2] == ERROR_RESP && answer[7] == VALUE_REFUSED;
		put(data, bitsOf(1.0f), 4);
		all = all && ask(&dev, WRITE_ACK, F32, 4, data, 1, sizeof answer, at, true) == NO_ERROR && answer[2] == WRITE_ACK_RESP
				&& rw[0x13] == data[3];
	}
	check(all, "D-30: a frame at buffer offsets +0 .. +3, and decoded in place: the same verdicts, the values read byte by byte");
	/* the frame at the very end of a buffer of its exact size: the check reads no byte past the data (ASan) */
	uint8_t *exact = new uint8_t[6];
	put(exact, 50, 1);
	put(exact + 1, 10, 1);
	put(exact + 2, 500, 2);
	put(exact + 4, 1000, 2);
	const bool good = evre_guard_check_write(&chk, &dev, U8, exact, 6) == NO_ERROR;
	put(exact + 4, 1001, 2);
	check(good && evre_guard_check_write(&chk, &dev, U8, exact, 6) == VALUE_REFUSED,
			"D-30: data of exactly count bytes on the heap, the last register at its end: read to data[count - 1] and no further");
	delete[] exact;
}

static void order() {
	evre_base_t dev;
	device(dev);
	evre_guard_init(&guard, &loginConfig);
	dev.WRITE_HANDLER = withLogin;
	dev.READ_HANDLER = readLogin;
	clockMs = 1000;
	evre_guard_check_init(&chk, &table, &dev);
	uint8_t bad[2] = { 201, 0 };
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 1, bad) == LOGIN_REQUIRED && untouched(dev) && evre_guard_check_last(&chk, nullptr, nullptr) == 0,
			"D-28: no session and a bad value: 13, never 15; the refusal count stays 0 (nothing learnt of the limits)");
	check(ask(&dev, WRITE_ACK, 0xD040, 16, token) == NO_ERROR && guard.logged_in && evre_guard_check_last(&chk, nullptr, nullptr) == 0,
			"D-28: a login: EVRE_HANDLED from part 1, and its register is never value-checked (it has no entry)");
	snap(dev);
	check(ask(&dev, WRITE_ACK, U8, 1, bad) == VALUE_REFUSED && untouched(dev) && evre_guard_check_last(&chk, nullptr, nullptr) == 1,
			"D-28: in a session, the bad value: 15");
	check(ask(&dev, WRITE_ACK, 0xD03F, 2, bad) == PERMISSION_DENIED && guard.logged_in,
			"D-28: a write over the login register in a session: 3 from part 1, the session goes on");
	clockMs += 4000;
	ask(&dev, WRITE_ACK, U8, 1, bad);
	clockMs += 4000;
	check(ask(&dev, READ, U8, 1) == NO_ERROR && guard.logged_in,
			"D-29: a refused value in a session counts as activity: 8 s after the login, 4 s after the refusal, still in (idle 5 s)");
	clockMs += 6000;
	check(ask(&dev, WRITE_ACK, U8, 1, bad) == LOGIN_REQUIRED && !guard.logged_in, "D-29: then 6 s idle: logged out, 13");
	evre_guard_t noGuard;
	evre_guard_init(&noGuard, nullptr);
	check(evre_guard_write_checked(&noGuard, &chk, &dev, U8, bad, 1) == PERMISSION_DENIED,
			"D-28: a guard with a bad config: its answer passed on (3)");
	evre_guard_logout(&guard);
	dev.WRITE_HANDLER = checkOnly;
	dev.READ_HANDLER = nullptr;
}

/* a table that breaks one init rule, and the check that refuses with it */
static bool refusedTable(const evre_guard_table_t &bad, const char *what) {
	evre_base_t dev;
	device(dev);
	evre_guard_check_t c;
	c.table = &table;
	c.refused = 7;
	const bool refused = evre_guard_check_init(&c, &bad, &dev) == PERMISSION_DENIED && c.table == nullptr && c.refused == 0;
	chk = c;
	const uint8_t config[2] = { 0x02, 0x00 };
	const uint8_t one[1] = { 50 };
	snap(dev);
	const bool closed = ask(&dev, WRITE_ACK, U8, 1, one) == PERMISSION_DENIED && untouched(dev)
			&& ask(&dev, WRITE_ACK, 0xA004, 2, config) == NO_ERROR && dev.CONFIG == 0x0003;
	if (!(refused && closed)) {
		std::printf("     the table: %s\n", what);
	}
	return refused && closed;
}

static void init() {
	evre_base_t dev;
	device(dev);
	check(evre_guard_check_init(&chk, &table, &dev) == NO_ERROR && chk.table == &table, "D-38: a good table: NO_ERROR");

	struct Change {
		const char *what;
		void (*apply)(evre_guard_desc_t *, uint32_t *);
	};
	static uint32_t vals[3];
	static evre_guard_desc_t copy[N_REGS];
	const Change changes[] = {
		{ "a type of 0", [](evre_guard_desc_t *r, uint32_t *) { r[0].type = 0; } },
		{ "a type of 9", [](evre_guard_desc_t *r, uint32_t *) { r[0].type = 9; } },
		{ "a size that is not the type's", [](evre_guard_desc_t *r, uint32_t *) { r[2].size = 1; } },
		{ "a u8 entry of 2 bytes (its limits fit 16 bits)", [](evre_guard_desc_t *r, uint32_t *) { r[2].type = EVRE_GUARD_U8; } },
		{ "a bytes register of 0 bytes", [](evre_guard_desc_t *r, uint32_t *) { r[8].size = 0; } },
		{ "a bytes register with limits", [](evre_guard_desc_t *r, uint32_t *) { r[8].max = 1; } },
		{ "a bytes register with a list", [](evre_guard_desc_t *r, uint32_t *) { r[8].n_values = 1; } },
		{ "an entry before 0xD000", [](evre_guard_desc_t *r, uint32_t *) { r[0].addr = 0xC000; } },
		{ "an entry out of order", [](evre_guard_desc_t *r, uint32_t *) { r[1].addr = 0xD007; } },
		{ "two entries that overlap", [](evre_guard_desc_t *r, uint32_t *) { r[3].addr = 0xD00B; } },
		{ "a flag bit set", [](evre_guard_desc_t *r, uint32_t *) { r[0].flags = 0x01; } },
		{ "a high flag bit set", [](evre_guard_desc_t *r, uint32_t *) { r[0].flags = 0x80; } },
		{ "spare1 not 0", [](evre_guard_desc_t *r, uint32_t *) { r[0].spare1 = 1; } },
		{ "spare2 not 0", [](evre_guard_desc_t *r, uint32_t *) { r[0].spare2 = 1; } },
		{ "zero_bits not 0", [](evre_guard_desc_t *r, uint32_t *) { r[0].zero_bits = 0x80; } },
		{ "min above max", [](evre_guard_desc_t *r, uint32_t *) { r[0].min = 201; } },
		{ "a signed min above max (as signed)", [](evre_guard_desc_t *r, uint32_t *) { r[3].min = 1001; } },
		{ "a u8 limit past the type", [](evre_guard_desc_t *r, uint32_t *) { r[0].max = 0x100; } },
		{ "an i8 limit not sign-extended", [](evre_guard_desc_t *r, uint32_t *) { r[1].min = 0x9C; } },
		{ "an f32 limit that is NaN", [](evre_guard_desc_t *r, uint32_t *) { r[6].max = 0x7FC00000UL; } },
		{ "an f32 limit that is infinite", [](evre_guard_desc_t *r, uint32_t *) { r[7].max = 0x7F800000UL; } },
		{ "an f32 min above max", [](evre_guard_desc_t *r, uint32_t *) { r[6].min = 0x41C00001UL; } },
		{ "a list past the value list", [](evre_guard_desc_t *r, uint32_t *) { r[11].first_value = 3; } },
		{ "a list not ascending", [](evre_guard_desc_t *r, uint32_t *v) { r[3].n_values = 2; v[2] = 0x00000000UL; } },
		{ "a list value past the type", [](evre_guard_desc_t *, uint32_t *v) { v[0] = 0x100; } },
		{ "an f32 list with -0.0", [](evre_guard_desc_t *, uint32_t *v) { v[2] = 0x80000000UL; } },
		{ "an f32 list with NaN", [](evre_guard_desc_t *, uint32_t *v) { v[2] = 0x7FC00000UL; } },
		{ "an entry past 0xDFFF", [](evre_guard_desc_t *r, uint32_t *) { r[8].size = 0x1000; } },
	};
	bool all = true, later = true;
	for (const Change &change : changes) {
		std::memcpy(copy, regs, sizeof regs);
		std::memcpy(vals, values, sizeof values);
		change.apply(copy, vals);
		const evre_guard_table_t bad = { copy, vals, N_REGS, 3 };
		const bool spare = std::strstr(change.what, "spare") != nullptr || std::strstr(change.what, "flag") != nullptr
				|| std::strstr(change.what, "zero_bits") != nullptr;
		(spare ? later : all) = refusedTable(bad, change.what) && (spare ? later : all);
	}
	check(all, "D-38: every init rule on its own (25 tables, one field changed each): init refuses it, and then every write to "
			"the device bank is refused (3) while CONFIG still lands");
	check(later, "D-42: a spare member, a flag bit or zero_bits not 0 (kept for later parts): init refuses the table, as an "
			"older Guard refuses a newer one");
	const evre_guard_table_t noRegs = { nullptr, values, 1, 3 };
	const evre_guard_table_t zeroRegs = { regs, values, 0, 3 };
	const evre_guard_table_t tooMany = { regs, values, 0x1001, 3 };
	const evre_guard_table_t noValues = { regs, nullptr, N_REGS, 3 };
	check(refusedTable(noRegs, "no entries") && refusedTable(zeroRegs, "n_regs 0") && refusedTable(tooMany, "n_regs 0x1001")
			&& refusedTable(noValues, "values null with n_values 3"), "D-38: no entries, n_regs 0 or above 0x1000, no value list: refused");

	device(dev);
	evre_guard_check_t c;
	check(evre_guard_check_init(&c, nullptr, &dev) == PERMISSION_DENIED && c.table == nullptr
			&& evre_guard_check_init(&c, &table, nullptr) == PERMISSION_DENIED && evre_guard_check_init(nullptr, &table, &dev) == PERMISSION_DENIED,
			"D-38: a null table, device or check: refused");
	const uint8_t one[1] = { 50 };
	check(evre_guard_check_write(nullptr, &dev, U8, one, 1) == PERMISSION_DENIED && evre_guard_check_write(&c, nullptr, U8, one, 1) == PERMISSION_DENIED,
			"D-38: a null check or device at the write: 3");
	evre_guard_check_t zeroed;
	std::memset(&zeroed, 0, sizeof zeroed);
	chk = zeroed;
	check(evre_guard_check_write(&chk, &dev, U8, one, 1) == PERMISSION_DENIED, "D-38: a zeroed check (init never ran): every write to the device bank refused");
	evre_guard_check_init(&chk, &table, &dev);
	const evre_guard_table_t empty = { regs, values, 0, 3 };
	check(evre_guard_check_init(&chk, &empty, &dev) == PERMISSION_DENIED && evre_guard_check_write(&chk, &dev, U8, one, 1) == PERMISSION_DENIED,
			"D-38: a failed re-init after a good one leaves the check refusing");

	/* against the device (D-41) */
	static uint8_t a[16], b[16];
	static evre_range_t order[2] = { { 0xD010, 16, b, 1 }, { 0xD000, 16, a, 1 } };
	static const evre_range_t adjacent[2] = { { 0xD000, 0x0C, a, 1 }, { 0xD00C, 16, b, 1 } };
	static const evre_range_t readOnly[1] = { { 0xD000, 0x40, a, 0 } };
	static const evre_range_t whole[1] = { { 0xD000, 0x40, a, 1 } };
	static evre_guard_desc_t two[2] = { { 0xD004, 1, EVRE_GUARD_U8, 0, 0, 0, 0, 0, 0, 0xFF, 0 },
		{ 0xD00A, 4, EVRE_GUARD_U32, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFUL, 0 } };
	static const evre_guard_table_t twoTable = { two, nullptr, 2, 0 };
	evre_base_t other;
	other.SALVE_ID_REG = 1;
	other.D_RANGES = whole;
	other.D_RANGE_CNT = 1;
	check(protocolInit(&other) == NO_ERROR && evre_guard_check_init(&c, &twoTable, &other) == NO_ERROR, "D-41: entries inside one writable range: good");
	other.D_RANGES = order;
	other.D_RANGE_CNT = 2;
	check(evre_guard_check_init(&c, &twoTable, &other) == PERMISSION_DENIED,
			"D-41: a range table set after protocolInit() and out of order: init refuses it");
	static const evre_range_t overlapping[2] = { { 0xD000, 0x10, a, 1 }, { 0xD008, 16, b, 0 } };
	other.D_RANGES = overlapping;
	check(evre_guard_check_init(&c, &twoTable, &other) == PERMISSION_DENIED,
			"D-41: a range table set after protocolInit() whose ranges overlap (a read-only one over a writable one): init "
			"refuses it, though each entry lies in a writable range");
	other.D_RANGES = adjacent;
	check(evre_guard_check_init(&c, &twoTable, &other) == PERMISSION_DENIED,
			"D-41: an entry across two adjacent writable ranges: refused (a register lies in one block of memory)");
	other.D_RANGES = readOnly;
	other.D_RANGE_CNT = 1;
	check(evre_guard_check_init(&c, &twoTable, &other) == PERMISSION_DENIED, "D-41: an entry in a read-only range: refused");
	evre_base_t refusedRanges;
	refusedRanges.SALVE_ID_REG = 1;
	refusedRanges.D_RANGES = order;
	refusedRanges.D_RANGE_CNT = 2;
	check(protocolInit(&refusedRanges) == RANGE_TABLE_INVALID && refusedRanges.D_RANGE_CNT == 0
			&& evre_guard_check_init(&c, &twoTable, &refusedRanges) == PERMISSION_DENIED,
			"D-41: a range table protocolInit() refused (D_RANGE_CNT 0): nothing is writable, init fails");
	/* a pointer table */
	static uint8_t cells[0x20];
	static uint8_t *pointers[0x20];
	for (unsigned ind = 0; ind < 0x20; ++ind) {
		pointers[ind] = &cells[ind];
	}
	evre_base_t old;
	old.SALVE_ID_REG = 1;
	old.D000 = pointers;
	old.DEVICE_REG_READ_MAX = 0xD01F;
	old.DEVICE_REG_WRITE_MIN = 0xD004;
	protocolInit(&old);
	check(evre_guard_check_init(&c, &twoTable, &old) == NO_ERROR, "D-41: a pointer table: entries inside WRITE_MIN .. READ_MAX");
	old.DEVICE_REG_WRITE_MIN = 0xD005;
	check(evre_guard_check_init(&c, &twoTable, &old) == PERMISSION_DENIED, "D-41: a pointer table: an entry below WRITE_MIN: refused");
	old.DEVICE_REG_WRITE_MIN = 0xD000;
	old.DEVICE_REG_READ_MAX = 0xD00C;
	check(evre_guard_check_init(&c, &twoTable, &old) == PERMISSION_DENIED, "D-41: a pointer table: an entry past READ_MAX: refused");

	device(dev);
	dev.ACCEPT_READ_RESP = 1;
	check(evre_guard_check_init(&c, &table, &dev) == PERMISSION_DENIED, "D-39: a mirror: init refuses it");
	dev.ACCEPT_READ_RESP = 0;
	check(evre_guard_check_init(&c, &table, &dev) == NO_ERROR, "D-39: the same device, not a mirror: good");
	dev.ACCEPT_READ_RESP = 1;
	check(evre_guard_check_write(&c, &dev, U8, one, 1) == PERMISSION_DENIED,
			"D-39: the mirror flag set after init: the write refused all the same (read at every write)");
	dev.ACCEPT_READ_RESP = 0;
	evre_guard_check_init(&chk, &table, &dev);
}

static void reservedBank() {
	evre_base_t dev;
	device(dev);
	const uint8_t config[2] = { 0x04, 0x00 };
	check(ask(&dev, WRITE_ACK, 0xA004, 2, config) == NO_ERROR && dev.CONFIG == 0x0005, "D-38: with a table, CONFIG lands");
	dev.CONFIG = 0;
	addMsg(&dev, 7);
	addMsg(&dev, 8);
	const uint8_t ack[1] = { 0 };
	check(ask(&dev, WRITE_ACK, 0xA007, 1, ack) == NO_ERROR && dev.MSG_CNT == 1 && dev.MSG_BUFFER[0] == 8,
			"D-38: with a table, an ack lands");
	const uint8_t clear[1] = { 0 };
	check(ask(&dev, WRITE, 0xA006, 1, clear) == NO_ERROR && dev.MSG_CNT == 0, "D-38: with a table, the queue's clear lands");
	evre_base_t all;
	device(all, true);
	check(ask(&all, WRITE, 0xA004, 2, config, BROADCAST_ID) == NO_ERROR && all.CONFIG == 0x0005,
			"D-40: a broadcast of CONFIG passes the check as a unicast write does");
}

static void diagnostics() {
	evre_base_t dev;
	device(dev);
	uint16_t addr = 1;
	uint8_t why = 9;
	check(evre_guard_check_last(&chk, &addr, &why) == 0 && addr == 0 && why == EVRE_GUARD_WHY_NONE,
			"D-43: after init: 0 refusals, no register, no reason");
	writeValue(dev, U16, 9, 2);
	writeValue(dev, GAP, 0, 2);
	check(evre_guard_check_last(&chk, &addr, &why) == 2 && addr == GAP && why == EVRE_GUARD_WHY_NOT_WRITABLE,
			"D-43: two refusals: the count, the last one's register and reason");
	writeValue(dev, U16, 9, 2);
	check(evre_guard_check_last(&chk, &addr, &why) == 3 && addr == U16 && why == EVRE_GUARD_WHY_LIMIT, "D-43: a limit: the reason LIMIT");
	check(evre_guard_check_last(&chk, nullptr, nullptr) == 3 && evre_guard_check_last(nullptr, &addr, &why) == 0,
			"D-43: addr and why may be null; a null check gives 0");
	writeValue(dev, U16, 500, 2);
	check(evre_guard_check_last(&chk, nullptr, nullptr) == 3, "D-43: an accepted write counts nothing");
	chk.refused = 0xFFFFFFFEUL;
	writeValue(dev, U16, 9, 2);
	writeValue(dev, U16, 9, 2);
	check(evre_guard_check_last(&chk, nullptr, nullptr) == 0xFFFFFFFFUL, "D-43: the count stops at 0xFFFFFFFF");
	evre_guard_check_t none;
	std::memset(&none, 0, sizeof none);
	const uint8_t one[1] = { 1 };
	check(evre_guard_check_write(&none, &dev, 0xD123, one, 1) == PERMISSION_DENIED && evre_guard_check_last(&none, &addr, &why) == 1
			&& addr == 0xD123 && why == EVRE_GUARD_WHY_SETUP, "D-43: no table: counted, the frame's offset as the register, the reason SETUP");
}

/* the check stays below the library: it never answers for it */
static void layering() {
	evre_base_t dev;
	device(dev);
	uint8_t data[4];
	bool neverHandled = true;
	for (uint32_t v = 0; v < 300; v += 7) {
		put(data, v, 1);
		const uint8_t ret = evre_guard_check_write(&chk, &dev, U8, data, 1);
		neverHandled = neverHandled && ret != EVRE_HANDLED && (ret == NO_ERROR || ret == VALUE_REFUSED);
	}
	check(neverHandled, "D-26: the check returns NO_ERROR or 15 for a value, never EVRE_HANDLED: it never stores");
	check(sizeof(evre_guard_desc_t) == 24 && EVRE_GUARD_TABLE_FORMAT == 1, "D-42: the entry is 24 B, the table format 1");
}

#ifdef EVRE_TEST_LOCK_HOOKS
static void lockBuild() {
	evre_base_t dev;
	device(dev);
	locks = unlocks = deepest = 0;
	writeValue(dev, U16, 9, 2);
	writeValue(dev, U16, 500, 2);
	writeValue(dev, GAP, 0, 2);
	check(locks == 0 && unlocks == 0, "D-43 lock: evre_guard_check_write takes no lock (accepted and refused writes)");
	evre_guard_check_t c;
	locks = unlocks = deepest = 0;
	evre_guard_check_init(&c, &table, &dev);
	check(locks == 1 && unlocks == 1 && deepest == 1 && depth == 0, "D-43 lock: evre_guard_check_init takes EVRE_LOCK once");
	const evre_guard_table_t empty = { regs, values, 0, 3 };
	locks = unlocks = 0;
	evre_guard_check_init(&c, &empty, &dev);
	check(locks == 1 && unlocks == 1, "D-43 lock: a failed init takes it once too (the nullptr stored under it)");
	locks = unlocks = deepest = 0;
	evre_guard_check_last(&c, nullptr, nullptr);
	check(locks == 1 && unlocks == 1 && deepest == 1, "D-43 lock: evre_guard_check_last takes EVRE_LOCK once, never nested");
}
#endif

int main() {
	std::setvbuf(stdout, nullptr, _IONBF, 0); /* every line out before a crash (a mutant that reads past the frame) */
	wire();
	values_();
	floats();
	spans();
	alignment();
	order();
	init();
	reservedBank();
	diagnostics();
	layering();
#ifdef EVRE_TEST_LOCK_HOOKS
	lockBuild();
#endif
	std::printf("%d failed\n", failed);
	return failed == 0 ? 0 : 1;
}

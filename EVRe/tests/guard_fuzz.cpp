/* SPDX-License-Identifier: Apache-2.0 */
/*
 * guard_fuzz.cpp: a differential fuzz of EVRe Guard part 2, the register
 * checks, against an independent oracle. Built by run_lib_tests.py.
 *
 * Each case makes a device from the seed: a few ranges of the device bank
 * (read-only and read-write, with gaps, some adjacent), a login register, and a
 * random valid table over the writable ranges (every type, limits leaning to
 * the type's ends, value lists, closed sets, bits that must be 0, bytes
 * registers, gaps, the type's full range as a register the device clamps). Sometimes the table is made bad by one field
 * (init must refuse it), and sometimes the device is a mirror, from the start
 * or set after init. Then the same frames go through four devices, in step:
 *
 *   A  part 1 alone (the login), the write handler evre_guard_write
 *   B  parts 1 and 2, the write handler evre_guard_write_checked
 *   C  part 2 alone (the firmware's wiring), evre_guard_check_write
 *   N  no handler
 *
 * B is compared with A, and C with N. Where A's login said NO_ERROR (or the
 * library asked C's handler), the oracle gives the verdict from the inputs
 * alone: a map of which entry owns each byte of the bank, a linear walk, and the
 * values compared as int64_t or double, decoded the way a host decodes them. It
 * is not the engine's code. When the oracle passes the frame, B is A and C is
 * N, byte for byte: the answer, the code, the memory. When it refuses, B (or C)
 * has the oracle's code, answers it in an ERROR_RESP that echoes the request
 * (silent for a broadcast, a response, or too small a buffer), stores nothing,
 * sets no HEARTBEAT, and its diagnostics name the oracle's register and reason.
 * Where A's login did not say NO_ERROR, B is A and no value is looked at (the
 * refusal count stays). The two logins stay equal (a refused value still
 * counts as activity, D-29). After every accepted write, each whole register it
 * touched holds a value its entry allows; the library never returns 15.
 *
 * Every refusal is counted in its class (GUARD_CLASSES in run_lib_tests.py):
 * G-VALUE, G-PART, G-UNCOVERED, G-SETUP, G-BCAST, G-MIRROR. The values lean to
 * limit - 1, limit, limit + 1, the listed values, NaN patterns, 0x80000000 and
 * 0x7F800000, and spans start or end inside a register.
 *
 *   guard_fuzz [cases [seed]]      exit 0: every check held, 1: one failed
 *
 * It prints a line for each of the first failures, then one summary line:
 * "cases N ops N G-VALUE n ... bad-tables n failed n".
 */

#include <cmath>
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "EVRe.h"
#include "evre_guard.h"
#include "evre_guard_desc.h"

/* ------------------------------------------------------------ the stream */

static uint64_t rngState = 88172645463325252ULL;

static uint32_t rnd() {
	rngState ^= rngState << 13;
	rngState ^= rngState >> 7;
	rngState ^= rngState << 17;
	return (uint32_t) (rngState >> 16);
}

static uint32_t below(uint32_t n) {
	return n == 0 ? 0 : rnd() % n;
}

static bool chance(unsigned percent) {
	return below(100) < percent;
}

static uint32_t rnd32() {
	return (rnd() << 16) ^ rnd();
}

/* ------------------------------------------------------------ the results */

static unsigned failures, shown;
static unsigned long cases, ops;
enum { G_VALUE, G_PART, G_UNCOVERED, G_SETUP, G_BCAST, G_MIRROR, CLASSES };
static const char *const CLASS_NAMES[CLASSES] = { "G-VALUE", "G-PART", "G-UNCOVERED", "G-SETUP", "G-BCAST", "G-MIRROR" };
static unsigned long classCount[CLASSES], badTables, accepted;

static void fail(const char *what, unsigned long op, uint8_t fn, uint16_t off, uint16_t cnt, unsigned got, unsigned want) {
	++failures;
	if (shown++ < 8) {
		std::printf("FAIL case %lu op %lu: %s (fn %02X off %04X cnt %u: got %u, want %u)\n", cases, op, what, fn, off, cnt, got, want);
	}
}

/* ------------------------------------------------------------ the device */

static const uint16_t BANK = 0xD000, SPAN = 0x100; /* the ranges lie in 0xD000..0xD0EF */
static const uint16_t LOGIN = 0xD0F0, LOGIN_SIZE = 16;
static const uint8_t TOKEN[LOGIN_SIZE] = { 'f', 'u', 'z', 'z' };

struct Device {
	evre_base_t base;
	uint8_t mem[SPAN];
	evre_range_t ranges[8];
	evre_guard_t guard;
	evre_guard_check_t check;
	bool asked;  /* the library asked the write handler during the last frame */
	uint8_t said; /* what part 1 said, for A */
};

static Device devA, devB, devC, devN;

static uint64_t clockMs;
static uint64_t now() { return clockMs; }
static const evre_guard_config_t loginConfig = { LOGIN, LOGIN_SIZE, TOKEN, nullptr, 0, 3, 100, 800, 2000, now };

static uint8_t writeA(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	devA.asked = true;
	devA.said = evre_guard_write(&devA.guard, d, off, data, cnt);
	return devA.said;
}
static uint8_t writeB(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	devB.asked = true;
	return evre_guard_write_checked(&devB.guard, &devB.check, d, off, data, cnt);
}
static uint8_t writeC(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	devC.asked = true;
	return evre_guard_check_write(&devC.check, d, off, data, cnt);
}
static uint8_t readA(evre_base_t *, uint16_t off, uint16_t cnt) { return evre_guard_read(&devA.guard, off, cnt); }
static uint8_t readB(evre_base_t *, uint16_t off, uint16_t cnt) { return evre_guard_read(&devB.guard, off, cnt); }

/* ------------------------------------------------------------ the table */

struct Reg {
	uint16_t addr, size;
	uint8_t type;
	uint32_t min, max;
	uint32_t list[6];
	uint8_t n;
	bool closed;        /* only the listed values pass (D-34) */
	uint32_t zeroBits;  /* bits that must be 0, raw (D-35) */
};

static Reg regs[64];
static unsigned nRegs;
static evre_guard_desc_t descs[64];
static uint32_t values[64 * 6];
static evre_guard_table_t table;
static int owner[SPAN]; /* the oracle's map: which reg owns each byte of 0xD000..0xD0FF, -1 none */

static uint16_t sizeOfType(uint8_t type) {
	return type <= EVRE_GUARD_I8 ? 1 : type <= EVRE_GUARD_I16 ? 2 : 4;
}
static bool signedType(uint8_t type) {
	return type == EVRE_GUARD_I8 || type == EVRE_GUARD_I16 || type == EVRE_GUARD_I32;
}

/* a value of the type as the table stores it (signed sign-extended, f32 bits) */
static uint32_t asStored(uint8_t type, uint32_t raw) {
	const uint16_t size = sizeOfType(type);
	if (signedType(type)) {
		const int64_t v = size == 1 ? (int8_t) raw : size == 2 ? (int16_t) raw : (int32_t) raw;
		return (uint32_t) (int32_t) v;
	}
	if (type == EVRE_GUARD_F32) {
		return raw;
	}
	return size == 4 ? raw : raw & ((1UL << (8 * size)) - 1);
}

static float floatOf(uint32_t bits) {
	float f;
	std::memcpy(&f, &bits, 4);
	return f;
}
static uint32_t bitsOf(float f) {
	uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}

/* a raw value of the type, leaning to its ends and to small numbers; for f32 always finite */
static uint32_t someValue(uint8_t type) {
	if (type == EVRE_GUARD_F32) {
		static const float picks[] = { 0.0f, 1.0f, -1.0f, 24.0f, 3.65f, -273.15f, 1e-40f, -1e-40f, 1e30f, -1e30f };
		switch (below(4)) {
			case 0: return bitsOf(picks[below(sizeof picks / sizeof picks[0])]);
			case 1: return chance(50) ? 0x7F7FFFFFUL : 0xFF7FFFFFUL;
			case 2: return bitsOf((float) ((int) below(2001) - 1000) / 8.0f);
			default: {
				uint32_t bits = rnd32();
				if ((bits & 0x7F800000UL) == 0x7F800000UL) bits &= 0xBFFFFFFFUL;
				return bits;
			}
		}
	}
	const uint16_t size = sizeOfType(type);
	const uint32_t mask = size == 4 ? 0xFFFFFFFFUL : (1UL << (8 * size)) - 1;
	uint32_t raw;
	switch (below(5)) {
		case 0: raw = 0; break;
		case 1: raw = mask; break;
		case 2: raw = signedType(type) ? (mask >> 1) + (chance(50) ? 1 : 0) : mask >> 1; break;
		case 3: raw = below(200); break;
		default: raw = rnd32() & mask;
	}
	return asStored(type, raw);
}

/* the stored values compared as the type orders them */
static bool lessThan(uint8_t type, uint32_t a, uint32_t b) {
	if (type == EVRE_GUARD_F32) return floatOf(a) < floatOf(b);
	if (signedType(type)) return (int32_t) a < (int32_t) b;
	return a < b;
}

static bool makeReg(Reg &r, uint16_t addr, uint16_t room) {
	r.addr = addr;
	if (chance(12)) {
		r.type = EVRE_GUARD_BYTES;
		r.size = (uint16_t) (1 + below(room < 8 ? room : 8));
		r.min = r.max = 0;
		r.n = 0;
		r.closed = false;
		r.zeroBits = 0;
		return true;
	}
	r.type = (uint8_t) (1 + below(7));
	r.size = sizeOfType(r.type);
	if (r.size > room) return false;
	if (chance(15)) { /* the type's full range: a register the device clamps */
		r.min = r.type == EVRE_GUARD_F32 ? 0xFF7FFFFFUL : signedType(r.type) ? asStored(r.type, 1UL << (8 * r.size - 1)) : 0;
		r.max = r.type == EVRE_GUARD_F32 ? 0x7F7FFFFFUL : signedType(r.type) ? asStored(r.type, (r.size == 4 ? 0xFFFFFFFFUL : (1UL << (8 * r.size)) - 1) >> 1)
				: (r.size == 4 ? 0xFFFFFFFFUL : (1UL << (8 * r.size)) - 1);
	} else {
		r.min = someValue(r.type);
		r.max = someValue(r.type);
		if (lessThan(r.type, r.max, r.min)) {
			const uint32_t t = r.min;
			r.min = r.max;
			r.max = t;
		}
		if (r.type == EVRE_GUARD_F32 && r.min == 0x80000000UL) r.min = 0; /* -0.0 as a limit is 0 to the check too */
		if (r.type == EVRE_GUARD_F32 && r.max == 0x80000000UL) r.max = 0;
	}
	r.n = 0;
	if (chance(40)) {
		if (chance(30)) r.list[r.n++] = 0; /* 0 listed: an f32 -0.0 must take it */
		const unsigned want = 1 + below(5);
		for (unsigned k = 0; k < want; ++k) {
			uint32_t v = someValue(r.type);
			if (r.type == EVRE_GUARD_F32 && v == 0x80000000UL) v = 0;
			bool dup = false;
			for (unsigned j = 0; j < r.n; ++j) dup = dup || r.list[j] == v;
			if (!dup) r.list[r.n++] = v;
		}
		for (unsigned i = 1; i < r.n; ++i) /* ascending as uint32_t */
			for (unsigned j = i; j > 0 && r.list[j - 1] > r.list[j]; --j) {
				const uint32_t t = r.list[j];
				r.list[j] = r.list[j - 1];
				r.list[j - 1] = t;
			}
	}
	r.closed = r.n > 0 && chance(30);
	r.zeroBits = 0;
	if (r.type != EVRE_GUARD_F32 && chance(20)) { /* some bits of the register's width */
		const uint32_t width = r.size == 4 ? 0xFFFFFFFFUL : (1UL << (8 * r.size)) - 1;
		r.zeroBits = rnd32() & rnd32() & width;
	}
	return true;
}

/* ranges over 0xD000..0xD0EF, some read-only, some adjacent, with gaps; the login range at 0xD0F0 */
static evre_range_t layout[8];
static unsigned nRanges;

static void makeLayout() {
	nRanges = 0;
	uint16_t at = BANK;
	while (nRanges < 6 && at + 2 <= LOGIN) {
		if (chance(30)) at = (uint16_t) (at + 1 + below(6)); /* a gap between two ranges */
		if (at + 2 > LOGIN) break;
		const uint16_t room = (uint16_t) (LOGIN - at);
		const uint16_t len = (uint16_t) (1 + below(room < 48 ? room : 48));
		layout[nRanges].start = at;
		layout[nRanges].len = len;
		layout[nRanges].base = nullptr;
		layout[nRanges].writable = (uint8_t) (chance(70) ? 1 : 0);
		nRanges++;
		at = (uint16_t) (at + len);
	}
	bool anyWritable = false;
	for (unsigned i = 0; i < nRanges; ++i) anyWritable = anyWritable || layout[i].writable;
	if (!anyWritable) layout[0].writable = 1;
	layout[nRanges].start = LOGIN;
	layout[nRanges].len = LOGIN_SIZE;
	layout[nRanges].base = nullptr;
	layout[nRanges].writable = 1;
	nRanges++;
}

/* a valid table over the writable ranges (not the login's): registers with gaps between them */
static uint16_t nValues;

static void makeTable() {
	nRegs = 0;
	for (unsigned i = 0; i + 1 < nRanges; ++i) {
		if (!layout[i].writable) continue;
		const uint16_t end = (uint16_t) (layout[i].start + layout[i].len);
		uint16_t pos = layout[i].start;
		while (pos < end && nRegs < 64) {
			if (chance(12)) {
				pos = (uint16_t) (pos + 1 + below(3)); /* a gap: bytes no entry covers */
				continue;
			}
			Reg r;
			if (!makeReg(r, pos, (uint16_t) (end - pos))) {
				pos++;
				continue;
			}
			regs[nRegs++] = r;
			pos = (uint16_t) (pos + r.size);
		}
	}
	if (nRegs == 0) { /* every writable byte fell in a gap: one full-range u8 at the first writable byte */
		for (unsigned i = 0; i < nRanges; ++i) {
			if (layout[i].writable) {
				Reg &r = regs[nRegs++];
				r.addr = layout[i].start;
				r.size = 1;
				r.type = EVRE_GUARD_U8;
				r.min = 0;
				r.max = 0xFF;
				r.n = 0;
				r.closed = false;
				r.zeroBits = 0;
				break;
			}
		}
	}
	nValues = 0;
	for (int &o : owner) o = -1;
	for (unsigned i = 0; i < nRegs; ++i) {
		const Reg &r = regs[i];
		evre_guard_desc_t &d = descs[i];
		d.addr = r.addr;
		d.size = r.size;
		d.type = r.type;
		d.flags = r.closed ? EVRE_GUARD_CLOSED : 0;
		d.n_values = r.n;
		d.spare1 = 0;
		d.first_value = nValues;
		d.spare2 = 0;
		d.min = r.min;
		d.max = r.max;
		d.zero_bits = r.zeroBits;
		for (unsigned k = 0; k < r.n; ++k) values[nValues++] = r.list[k];
		for (unsigned k = 0; k < r.size; ++k) owner[r.addr - BANK + k] = (int) i;
	}
	table.regs = descs;
	table.values = values;
	table.n_regs = (uint16_t) nRegs;
	table.n_values = nValues;
}

/* One field of the good table changed so that the table breaks a rule of init for sure. */
static void breakTable() {
	const unsigned last = nRegs - 1;
	int withList = -1, numberAt = -1, wide = -1;
	for (unsigned i = 0; i < nRegs; ++i) {
		if (regs[i].n >= 2 && withList < 0) withList = (int) i;
		if (regs[i].type != EVRE_GUARD_BYTES && numberAt < 0) numberAt = (int) i;
		if ((regs[i].type == EVRE_GUARD_F32 || regs[i].type == EVRE_GUARD_BYTES) && wide < 0) wide = (int) i;
	}
	switch (below(13)) {
		case 0: descs[below(nRegs)].type = 0; break;
		case 1: descs[below(nRegs)].type = 9; break;
		case 2:
			if (numberAt >= 0) descs[numberAt].size = descs[numberAt].size == 1 ? 2 : 1;
			else descs[0].size = 0;
			break;
		case 3: descs[below(nRegs)].spare1 = (uint8_t) (1 + below(255)); break;
		case 4: descs[below(nRegs)].spare2 = (uint16_t) (1 + below(0xFFFF)); break;
		case 5: descs[below(nRegs)].flags = 0x80; break;
		case 6:
			if (numberAt >= 0 && descs[numberAt].min != descs[numberAt].max) {
				const uint32_t t = descs[numberAt].min;
				descs[numberAt].min = descs[numberAt].max;
				descs[numberAt].max = t;
			} else {
				descs[0].type = 0;
			}
			break;
		case 7:
			if (nRegs >= 2) descs[1].addr = descs[0].addr; /* overlaps the first */
			else descs[0].addr = 0xC000;
			break;
		case 8:
			if (withList >= 0) {
				const uint16_t at = descs[withList].first_value;
				const uint32_t t = values[at];
				values[at] = values[at + 1];
				values[at + 1] = t;
			} else {
				descs[0].type = 0;
			}
			break;
		case 9: descs[last].addr = 0xD100; break; /* past every range */
		case 10: table.n_regs = 0; break;
		case 11:
			if (numberAt >= 0) { /* a closed set with no value */
				descs[numberAt].flags = EVRE_GUARD_CLOSED;
				descs[numberAt].n_values = 0;
			} else {
				descs[0].type = 0;
			}
			break;
		default:
			if (wide >= 0) descs[wide].zero_bits = 1; /* never allowed on f32 or bytes */
			else descs[0].spare1 = 1;
			break;
	}
}

/* ------------------------------------------------------------ the oracle */

struct Verdict {
	uint8_t code;    /* NO_ERROR: it passes */
	uint8_t why;
	uint16_t addr;
	int cls;
};

/* a whole register's bytes, decoded as a host decodes them, compared in int64_t or double: 0 or the reason */
static uint8_t notAllowed(const Reg &r, const uint8_t *bytes) {
	uint32_t raw = 0;
	for (unsigned k = 0; k < r.size; ++k) raw |= (uint32_t) bytes[k] << (8 * k);
	if (raw & r.zeroBits) return EVRE_GUARD_WHY_BITS;
	if (r.type == EVRE_GUARD_F32) {
		const double v = floatOf(raw);
		if (std::isnan(v) || std::isinf(v)) return EVRE_GUARD_WHY_NOT_FINITE;
		for (unsigned k = 0; k < r.n; ++k)
			if ((double) floatOf(r.list[k]) == v) return 0;
		if (r.closed) return EVRE_GUARD_WHY_NOT_LISTED;
		return v >= (double) floatOf(r.min) && v <= (double) floatOf(r.max) ? 0 : EVRE_GUARD_WHY_LIMIT;
	}
	int64_t v, low, high;
	if (signedType(r.type)) {
		v = r.size == 1 ? (int8_t) raw : r.size == 2 ? (int16_t) raw : (int32_t) raw;
		low = (int32_t) r.min;
		high = (int32_t) r.max;
	} else {
		v = raw;
		low = r.min;
		high = r.max;
	}
	for (unsigned k = 0; k < r.n; ++k) {
		const int64_t listed = signedType(r.type) ? (int64_t) (int32_t) r.list[k] : (int64_t) r.list[k];
		if (listed == v) return 0;
	}
	if (r.closed) return EVRE_GUARD_WHY_NOT_LISTED;
	return v >= low && v <= high ? 0 : EVRE_GUARD_WHY_LIMIT;
}

static Verdict oracle(bool mirror, bool good, uint16_t off, uint16_t cnt, const uint8_t *data) {
	const Verdict pass = { NO_ERROR, 0, 0, -1 };
	const uint32_t end = (uint32_t) off + cnt;
	if (mirror) return { PERMISSION_DENIED, EVRE_GUARD_WHY_SETUP, off, G_MIRROR };
	if (cnt == 0) return pass;
	if (off >= 0xA000 && end <= 0xA106) return pass;
	if (!good) return { PERMISSION_DENIED, EVRE_GUARD_WHY_SETUP, off, G_SETUP };
	if (off < 0xD000 || end > 0xE000) return { PERMISSION_DENIED, EVRE_GUARD_WHY_NOT_WRITABLE, off, G_UNCOVERED };
	uint32_t pos = off;
	while (pos < end) {
		const int idx = pos - BANK < SPAN ? owner[pos - BANK] : -1;
		if (idx < 0) return { PERMISSION_DENIED, EVRE_GUARD_WHY_NOT_WRITABLE, (uint16_t) pos, G_UNCOVERED };
		const Reg &r = regs[idx];
		if (r.type != EVRE_GUARD_BYTES) {
			if (r.addr < off || (uint32_t) r.addr + r.size > end) return { PERMISSION_DENIED, EVRE_GUARD_WHY_PART, r.addr, G_PART };
			const uint8_t why = notAllowed(r, data + (r.addr - off));
			if (why) return { VALUE_REFUSED, why, r.addr, G_VALUE };
		}
		pos = (uint32_t) r.addr + r.size;
	}
	return pass;
}

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
	if (len) std::memcpy(out + n, data, len);
	n = uint16_t(n + len);
	const uint16_t crc = GetCrc16(out, n);
	out[n++] = uint8_t(crc);
	out[n++] = uint8_t(crc >> 8);
	out[n++] = 0x7D;
	return n;
}

/* a value for one register: its limits and their neighbours, its listed values, NaN patterns and the like */
static uint32_t biased(const Reg &r) {
	const bool f32 = r.type == EVRE_GUARD_F32;
	switch (below(10)) {
		case 0: return r.min;
		case 1: return r.max;
		case 2: return f32 ? bitsOf(std::nextafter(floatOf(r.min), -INFINITY)) : r.min - 1;
		case 3: return f32 ? bitsOf(std::nextafter(floatOf(r.max), INFINITY)) : r.max + 1;
		case 4: return r.n ? r.list[below(r.n)] : someValue(r.type);
		case 5:
			if (f32 && r.n && r.list[0] == 0 && chance(50)) return 0x80000000UL; /* -0.0 against a listed 0 */
			return r.n ? r.list[below(r.n)] + (chance(50) ? 1 : 0xFFFFFFFFUL) : r.min + 1;
		case 6: {
			static const uint32_t odd[] = { 0x7FC00000UL, 0x7F800001UL, 0xFFC00000UL, 0x7F800000UL, 0xFF800000UL, 0x80000000UL,
				0x00000001UL, 0x80000001UL };
			return odd[below(sizeof odd / sizeof odd[0])];
		}
		case 7: return someValue(r.type);
		default: return rnd32();
	}
}

/* ------------------------------------------------------------ one case */

static void setUp(Device &d, const uint8_t *memory, bool broadcasts, evre_write_handler_t onWrite, evre_read_handler_t onRead) {
	d.base.~evre_base_t();
	new (&d.base) evre_base_t();
	std::memcpy(d.mem, memory, SPAN);
	for (unsigned i = 0; i < nRanges; ++i) {
		d.ranges[i] = layout[i];
		d.ranges[i].base = d.mem + (layout[i].start - BANK);
	}
	d.base.SALVE_ID_REG = 1;
	d.base.DEVICE_ID = 0x4242;
	d.base.D_RANGES = d.ranges;
	d.base.D_RANGE_CNT = (uint8_t) nRanges;
	d.base.ACCEPT_BROADCAST_D000 = broadcasts ? 1 : 0;
	if (protocolInit(&d.base) != NO_ERROR) fail("protocolInit refused the layout", 0, 0, 0, 0, 1, 0);
	d.base.WRITE_HANDLER = onWrite;
	d.base.READ_HANDLER = onRead;
}

static bool sameState(const Device &x, const Device &y) {
	return std::memcmp(x.mem, y.mem, SPAN) == 0 && x.base.CONFIG == y.base.CONFIG && x.base.MSG_CNT == y.base.MSG_CNT
			&& std::memcmp(x.base.MSG_BUFFER, y.base.MSG_BUFFER, sizeof x.base.MSG_BUFFER) == 0 && x.base.STATUS == y.base.STATUS;
}

static void copyState(Device &to, const Device &from) {
	std::memcpy(to.mem, from.mem, SPAN);
	to.base.CONFIG = from.base.CONFIG;
	to.base.MSG_CNT = from.base.MSG_CNT;
	std::memcpy(to.base.MSG_BUFFER, from.base.MSG_BUFFER, sizeof to.base.MSG_BUFFER);
	to.base.STATUS = from.base.STATUS;
}

struct Outcome {
	uint8_t ret;
	uint16_t len;
	uint8_t out[700];
};

static bool sameOutcome(const Outcome &x, const Outcome &y) {
	return x.ret == y.ret && x.len == y.len && std::memcmp(x.out, y.out, x.len) == 0;
}

static uint8_t req[1024];

static void decode(Device &d, uint16_t n, uint16_t outMax, Outcome &o) {
	static uint8_t copy[1024];
	std::memcpy(copy, req, n);
	d.asked = false;
	o.len = 0;
	o.ret = decodePacketInto(&d.base, copy, n, o.out, outMax, &o.len);
}

/* a refusal exactly as the verdict says: the code, the answer or the silence, nothing stored, the diagnostics */
static void refusedAsSaid(const Device &d, const Outcome &o, const Device &before, uint32_t refusedBefore, const Verdict &v,
		uint8_t fn, uint8_t slave, uint16_t off, uint16_t cnt, uint16_t outMax) {
	const bool answered = (fn == WRITE || fn == WRITE_ACK) && slave != BROADCAST_ID && outMax >= 11;
	const bool answerOk = answered ? o.len == 11 && o.out[2] == ERROR_RESP && o.out[7] == v.code && o.out[3] == uint8_t(off)
			&& o.out[4] == uint8_t(off >> 8) && o.out[5] == uint8_t(cnt) && o.out[6] == uint8_t(cnt >> 8) : o.len == 0;
	if (o.ret != v.code) fail("the code", ops, fn, off, cnt, o.ret, v.code);
	else if (!answerOk) fail("answered or silent, the echo", ops, fn, off, cnt, o.len, answered ? 11 : 0);
	else if (!sameState(d, before)) fail("a refused write stored something (or set HEARTBEAT)", ops, fn, off, cnt, 1, 0);
	else if (d.check.refused != refusedBefore + 1 || d.check.last_addr != v.addr || d.check.last_why != v.why)
		fail("the diagnostics (count, register, reason)", ops, fn, off, cnt, d.check.last_why, v.why);
}

static Device beforeB, beforeC;

static void runCase() {
	makeLayout();
	makeTable();
	const bool good = !chance(8);
	if (!good) {
		breakTable();
		badTables++;
	}
	const bool mirrorFirst = chance(3), mirrorLater = !mirrorFirst && chance(3);
	const bool broadcasts = chance(30);
	uint8_t memory[SPAN];
	for (uint8_t &b : memory) b = (uint8_t) rnd();
	clockMs = 1000 + below(100000);

	setUp(devA, memory, broadcasts, writeA, readA);
	setUp(devB, memory, broadcasts, writeB, readB);
	setUp(devC, memory, broadcasts, writeC, nullptr);
	setUp(devN, memory, broadcasts, nullptr, nullptr);
	/* a range table changed after protocolInit() so that two ranges overlap: never checked by the library, so init
	 * must see it */
	const bool overlap = nRanges >= 3 && chance(3);
	if (overlap) {
		devA.ranges[1].start = devB.ranges[1].start = devC.ranges[1].start = devN.ranges[1].start = layout[0].start;
	}
	evre_guard_init(&devA.guard, &loginConfig);
	evre_guard_init(&devB.guard, &loginConfig);
	if (mirrorFirst) {
		devA.base.ACCEPT_READ_RESP = devB.base.ACCEPT_READ_RESP = devC.base.ACCEPT_READ_RESP = devN.base.ACCEPT_READ_RESP = 1;
	}
	const uint8_t wantInit = good && !mirrorFirst && !overlap ? NO_ERROR : PERMISSION_DENIED;
	const uint8_t initB = evre_guard_check_init(&devB.check, &table, &devB.base);
	const uint8_t initC = evre_guard_check_init(&devC.check, &table, &devC.base);
	if (initB != wantInit || initC != wantInit) fail(good ? "init of a good table on a mirror" : "init of a bad table", 0, 0, 0, 0, initB, wantInit);
	if (mirrorLater) {
		devA.base.ACCEPT_READ_RESP = devB.base.ACCEPT_READ_RESP = devC.base.ACCEPT_READ_RESP = devN.base.ACCEPT_READ_RESP = 1;
	}
	const bool mirror = mirrorFirst || mirrorLater;
	const bool tableGood = good && !mirrorFirst && !overlap;

	const unsigned nOps = 1 + below(12);
	for (unsigned k = 0; k < nOps; ++k) {
		++ops;
		static uint8_t data[400];
		uint8_t fn = WRITE_ACK, slave = 1;
		uint16_t off, cnt;
		const unsigned kind = below(100);
		if (kind < 10) { /* a login, the right token or a wrong one */
			off = LOGIN;
			cnt = LOGIN_SIZE;
			std::memcpy(data, TOKEN, LOGIN_SIZE);
			if (chance(25)) data[below(LOGIN_SIZE)] ^= 0x5A;
		} else if (kind < 14) { /* the reserved bank: CONFIG, the queue's clear */
			fn = chance(50) ? WRITE : WRITE_ACK;
			off = chance(50) ? 0xA004 : 0xA006;
			cnt = off == 0xA004 ? 2 : 1;
			for (unsigned i = 0; i < cnt; ++i) data[i] = (uint8_t) (rnd() & 0xE5); /* no reset, no DFU, no auto send */
		} else if (kind < 17) { /* part 2 is never asked */
			fn = READ;
			off = (uint16_t) (BANK + below(SPAN));
			cnt = (uint16_t) (1 + below(16));
		} else if (kind < 20) { /* a mirror's READ_RESP */
			fn = READ_RESP;
			off = (uint16_t) (BANK + below(SPAN));
			cnt = (uint16_t) (1 + below(16));
			for (unsigned i = 0; i < cnt; ++i) data[i] = (uint8_t) rnd();
		} else {
			fn = chance(40) ? WRITE : WRITE_ACK;
			if (chance(10)) {
				slave = BROADCAST_ID;
				fn = WRITE;
			}
			if (chance(75)) { /* from a register to a register, often cut */
				const unsigned i = below(nRegs);
				const unsigned step = below(3);
				const unsigned j = i + step < nRegs ? i + step : nRegs - 1;
				int start = regs[i].addr, end = regs[j].addr + regs[j].size;
				if (chance(15)) start += (int) below(regs[i].size + 1);
				if (chance(10)) start -= (int) below(3);
				if (chance(15)) end -= (int) below(regs[j].size + 1);
				if (chance(10)) end += (int) below(3);
				if (end <= start) end = start + 1;
				off = (uint16_t) start;
				cnt = (uint16_t) (end - start);
			} else {
				off = (uint16_t) (BANK + below(SPAN + 16));
				cnt = (uint16_t) (chance(3) ? 0 : 1 + below(24));
			}
			for (unsigned i = 0; i < cnt && i < sizeof data; ++i) data[i] = (uint8_t) rnd();
			for (unsigned i = 0; i < nRegs; ++i) {
				const Reg &r = regs[i];
				if (r.type == EVRE_GUARD_BYTES || r.addr < off || (uint32_t) r.addr + r.size > (uint32_t) off + cnt || !chance(75)) continue;
				uint32_t v = biased(r);
				if (r.zeroBits && chance(70)) v &= ~r.zeroBits; /* mostly clear of the bits that must be 0 */
				for (unsigned b = 0; b < r.size; ++b) data[r.addr - off + b] = (uint8_t) (v >> (8 * b));
			}
		}
		if (cnt > 300) cnt = 300;
		const uint16_t outMax = chance(2) ? 10 : chance(2) ? 11 : 700;
		clockMs += chance(5) ? 2500 : below(600);
		if (chance(3)) {
			evre_guard_logout(&devA.guard);
			evre_guard_logout(&devB.guard);
		}
		const bool withData = fn == WRITE || fn == WRITE_ACK || fn == READ_RESP;
		const uint16_t n = frame(req, slave, fn, off, cnt, data, withData ? cnt : 0);

		copyState(beforeB, devB);
		copyState(beforeC, devC);
		const uint32_t refusedB = devB.check.refused, refusedC = devC.check.refused;
		static Outcome oA, oB, oC, oN;
		decode(devA, n, outMax, oA);
		decode(devB, n, outMax, oB);
		decode(devC, n, outMax, oC);
		decode(devN, n, outMax, oN);
		if (oA.ret == VALUE_REFUSED || oN.ret == VALUE_REFUSED) fail("the library returned 15 by itself", ops, fn, off, cnt, 15, 0);

		/* B against A */
		if (devA.asked != devB.asked) {
			fail("the library asked one handler and not the other", ops, fn, off, cnt, devB.asked, devA.asked);
		} else if (!devA.asked || devA.said != NO_ERROR) {
			if (!sameOutcome(oA, oB) || !sameState(devA, devB)) fail("without the login's NO_ERROR, B is not A", ops, fn, off, cnt, oB.ret, oA.ret);
			else if (devB.check.refused != refusedB) fail("a value looked at without a session", ops, fn, off, cnt, devB.check.refused, refusedB);
		} else {
			Verdict v = oracle(mirror, tableGood, off, cnt, data);
			if (v.code == NO_ERROR) {
				if (!sameOutcome(oA, oB) || !sameState(devA, devB)) fail("the oracle passes it, B is not A", ops, fn, off, cnt, oB.ret, oA.ret);
				else if (fn != READ_RESP) {
					++accepted;
					for (unsigned i = 0; i < nRegs; ++i) { /* every whole register it wrote holds a value its entry allows */
						const Reg &r = regs[i];
						if (tableGood && r.type != EVRE_GUARD_BYTES && r.addr >= off && (uint32_t) r.addr + r.size <= (uint32_t) off + cnt
								&& notAllowed(r, devB.mem + (r.addr - BANK)))
							fail("an accepted write left a value its entry does not allow", ops, fn, off, cnt, r.addr, 0);
					}
				}
			} else {
				refusedAsSaid(devB, oB, beforeB, refusedB, v, fn, slave, off, cnt, outMax);
				classCount[slave == BROADCAST_ID ? G_BCAST : v.cls]++;
			}
		}
		const evre_guard_t &ga = devA.guard, &gb = devB.guard;
		if (ga.logged_in != gb.logged_in || ga.failures != gb.failures || ga.locked != gb.locked || ga.lock_left != gb.lock_left
				|| ga.idle_ms != gb.idle_ms || ga.seen_ms != gb.seen_ms)
			fail("the two logins went apart (a refused value is activity too)", ops, fn, off, cnt, gb.idle_ms, ga.idle_ms);
		copyState(devA, devB);

		/* C against N */
		if (!devC.asked) {
			if (!sameOutcome(oC, oN) || !sameState(devC, devN)) fail("not asked, C is not N", ops, fn, off, cnt, oC.ret, oN.ret);
		} else {
			const Verdict v = oracle(mirror, tableGood, off, cnt, data);
			if (v.code == NO_ERROR) {
				if (!sameOutcome(oC, oN) || !sameState(devC, devN)) fail("the oracle passes it, C is not N", ops, fn, off, cnt, oC.ret, oN.ret);
			} else {
				refusedAsSaid(devC, oC, beforeC, refusedC, v, fn, slave, off, cnt, outMax);
				classCount[slave == BROADCAST_ID ? G_BCAST : v.cls]++;
			}
		}
		copyState(devN, devC);
	}
}

int main(int argc, char **argv) {
	const unsigned long total = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 300000UL;
	const unsigned long seed = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 1UL;
	rngState ^= (uint64_t) seed * 0x9E3779B97F4A7C15ULL;
	for (int i = 0; i < 8; ++i) rnd();
	for (cases = 0; cases < total; ++cases) runCase();
	std::printf("cases %lu ops %lu accepted %lu", cases, ops, accepted);
	for (int c = 0; c < CLASSES; ++c) std::printf(" %s %lu", CLASS_NAMES[c], classCount[c]);
	std::printf(" bad-tables %lu failed %u\n", badTables, failures);
	return failures == 0 ? 0 : 1;
}

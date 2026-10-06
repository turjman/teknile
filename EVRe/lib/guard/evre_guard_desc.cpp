/* SPDX-License-Identifier: Apache-2.0 */
/*
 * evre_guard_desc.cpp: EVRe Guard part 2, the register checks. See
 * evre_guard_desc.h.
 *
 * Every address sum is done in uint32_t, so offset + count never wraps at
 * 0x10000; every shift is on a uint32_t, so a 16-bit int never overflows. The
 * frame's bytes are read one at a time, never through a cast pointer: they lie
 * at frame byte 7, at any alignment, and an unaligned word load faults on a
 * Cortex-M7.
 */

#include "evre_guard_desc.h"

static const uint32_t BANK_START = 0xD000UL;
static const uint32_t BANK_END = 0xE000UL;     /* one past 0xDFFF */
static const uint32_t RESERVED_START = 0xA000UL;
static const uint32_t RESERVED_END = 0xA106UL; /* one past 0xA105, the last byte of MSG_BUFFER */
static const uint16_t MAX_REGS = 0x1000U;      /* a bank of one-byte registers */
static const uint32_t SIGN = 0x80000000UL;
static const uint32_t F32_EXPONENT = 0x7F800000UL;
static const uint32_t F32_MINUS_ZERO = 0x80000000UL;

/* ------------------------------------------------------------- the types */

static bool isNumber(uint8_t type) {
	return type >= EVRE_GUARD_U8 && type <= EVRE_GUARD_F32;
}

static bool isSigned(uint8_t type) {
	return type == EVRE_GUARD_I8 || type == EVRE_GUARD_I16 || type == EVRE_GUARD_I32;
}

/* the size a number type has; 0 for BYTES and for no type */
static uint16_t sizeOf(uint8_t type) {
	switch (type) {
		case EVRE_GUARD_U8:
		case EVRE_GUARD_I8:
			return 1U;
		case EVRE_GUARD_U16:
		case EVRE_GUARD_I16:
			return 2U;
		case EVRE_GUARD_U32:
		case EVRE_GUARD_I32:
		case EVRE_GUARD_F32:
			return 4U;
		default:
			return 0U;
	}
}

/* the bits a register of `size` bytes has: 0xFF, 0xFFFF or 0xFFFFFFFF */
static uint32_t widthMask(uint16_t size) {
	return size >= 4U ? 0xFFFFFFFFUL : (1UL << (8U * size)) - 1UL;
}

/* -------------------------------------------------------------- the values */

/* size bytes, little endian, one at a time */
static uint32_t readLE(const uint8_t *bytes, uint16_t size) {
	uint32_t raw = 0;
	for (uint16_t ind = size; ind > 0; --ind) {
		raw = (raw << 8) | bytes[ind - 1U];
	}
	return raw;
}

/* A signed value of `size` bytes sign-extended to 32 bits, as bits: the way
 * the table stores a signed limit, so the two compare as they are. */
static uint32_t toSigned(uint32_t raw, uint16_t size) {
	const uint32_t mask = widthMask(size);
	const uint32_t top = (mask >> 1) + 1UL;
	return (raw & top) != 0 ? (raw | ~mask) : raw;
}

/* A bit test, so -ffast-math cannot remove it: the exponent all ones is NaN or
 * an infinity. */
static bool isFinite(uint32_t f32) {
	return (f32 & F32_EXPONENT) != F32_EXPONENT;
}

/* A key that sorts as the value does, compared as uint32_t, so every type takes
 * the same compare and no float type is used:
 *   - unsigned: the value itself;
 *   - signed (sign-extended): the sign bit flipped, so -1 (0xFFFFFFFF) sorts
 *     below 0 (0x00000000), and no signed conversion is needed;
 *   - f32 (finite): a negative one has all its bits inverted, a positive one
 *     its top bit set. -0.0 is +0.0 here, as in the float compare.
 * The integer compare of the keys equals the float compare for every finite
 * pair, subnormals included, whatever the FPU's flush-to-zero mode. */
static uint32_t key(uint8_t type, uint32_t value) {
	if (type == EVRE_GUARD_F32) {
		if (value == F32_MINUS_ZERO) {
			value = 0;
		}
		return (value & SIGN) != 0 ? ~value : (value | SIGN);
	}
	return isSigned(type) ? (value ^ SIGN) : value;
}

/* the value is one the register lists: a halving search, 8 steps at most */
static bool listed(const evre_guard_table_t *table, const evre_guard_desc_t *reg, uint32_t value) {
	uint32_t low = reg->first_value;
	uint32_t high = low + reg->n_values; /* one past the last */
	while (low < high) {
		const uint32_t mid = low + (high - low) / 2U;
		const uint32_t here = table->values[mid];
		if (here == value) {
			return true;
		}
		if (here < value) {
			low = mid + 1U;
		} else {
			high = mid;
		}
	}
	return false;
}

static bool inLimits(const evre_guard_desc_t *reg, uint32_t value) {
	const uint32_t at = key(reg->type, value);
	return at >= key(reg->type, reg->min) && at <= key(reg->type, reg->max);
}

/* One whole number register, its bytes as they came: NO_ERROR, or the reason
 * it is refused. raw is read before any sign extension. */
static uint8_t valueOk(const evre_guard_table_t *table, const evre_guard_desc_t *reg, uint32_t raw) {
	uint32_t value = raw;
	if (reg->type == EVRE_GUARD_F32) {
		if (!isFinite(value)) {
			return EVRE_GUARD_WHY_NOT_FINITE;
		}
		if (value == F32_MINUS_ZERO) {
			value = 0; /* so a listed 0 takes -0.0 too */
		}
	} else if (isSigned(reg->type)) {
		value = toSigned(value, reg->size);
	}
	if (listed(table, reg, value)) {
		return EVRE_GUARD_WHY_NONE; /* a special: it passes, even outside min..max */
	}
	if (!inLimits(reg, value)) {
		return EVRE_GUARD_WHY_LIMIT;
	}
	return EVRE_GUARD_WHY_NONE;
}

/* ---------------------------------------------------------------- the walk */

static uint32_t endOf(const evre_guard_desc_t *reg) {
	return (uint32_t) reg->addr + reg->size;
}

/* The first entry that ends past offset (n_regs when none does): a halving
 * search, 13 steps at most for 0x1000 entries. */
static uint16_t firstAt(const evre_guard_table_t *table, uint32_t offset) {
	uint16_t low = 0;
	uint16_t high = table->n_regs;
	while (low < high) {
		const uint16_t mid = (uint16_t) (low + (high - low) / 2U);
		if (endOf(&table->regs[mid]) <= offset) {
			low = (uint16_t) (mid + 1U);
		} else {
			high = mid;
		}
	}
	return low;
}

static void noteRefusal(evre_guard_check_t *check, uint16_t addr, uint8_t why) {
	if (check->refused != 0xFFFFFFFFUL) {
		++check->refused;
	}
	check->last_addr = addr;
	check->last_why = why;
}

static uint8_t refuse(evre_guard_check_t *check, uint8_t code, uint32_t addr, uint8_t why) {
	noteRefusal(check, (uint16_t) addr, why);
	return code;
}

/* Every register the span offset .. end - 1 touches, in address order. The
 * first refusal ends it, so the verdict is the first bad register's. i grows
 * on every pass and stays below n_regs, so the loop ends whatever the table
 * holds; its reads stay inside the entries and the list for a table that
 * passed init. data[0 .. count - 1] is read, nothing past it. */
static uint8_t walk(evre_guard_check_t *check, uint32_t offset, const uint8_t *data, uint32_t end) {
	const evre_guard_table_t *table = check->table;
	uint16_t ind = firstAt(table, offset);
	uint32_t pos = offset;
	while (pos < end) {
		if (ind >= table->n_regs || table->regs[ind].addr > pos) {
			return refuse(check, PERMISSION_DENIED, pos, EVRE_GUARD_WHY_NOT_WRITABLE);
		}
		const evre_guard_desc_t *reg = &table->regs[ind];
		if (isNumber(reg->type)) {
			if (reg->addr < pos || endOf(reg) > end) {
				return refuse(check, PERMISSION_DENIED, reg->addr, EVRE_GUARD_WHY_PART);
			}
			const uint8_t why = valueOk(table, reg, readLE(data + (reg->addr - offset), reg->size));
			if (why != EVRE_GUARD_WHY_NONE) {
				return refuse(check, VALUE_REFUSED, reg->addr, why);
			}
		}
		pos = endOf(reg);
		++ind;
	}
	return NO_ERROR;
}

uint8_t evre_guard_check_write(evre_guard_check_t *check, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count) {
	if (check == nullptr) {
		return PERMISSION_DENIED;
	}
	/* A mirror is checked here too, not only at init: the flag can change after it. */
	if (device == nullptr || device->ACCEPT_READ_RESP != 0) {
		return refuse(check, PERMISSION_DENIED, offset, EVRE_GUARD_WHY_SETUP);
	}
	if (count == 0) {
		return NO_ERROR; /* no register touched; data points at the CRC and is not read */
	}
	const uint32_t end = (uint32_t) offset + count;
	/* CONFIG, the queue's clear and the acks are the library's: never the
	 * table's, so they work on a device whose table is bad (reset, DFU). */
	if (offset >= RESERVED_START && end <= RESERVED_END) {
		return NO_ERROR;
	}
	if (check->table == nullptr) {
		return refuse(check, PERMISSION_DENIED, offset, EVRE_GUARD_WHY_SETUP);
	}
	/* the library never hands these in, so they are refused */
	if (offset < BANK_START || end > BANK_END) {
		return refuse(check, PERMISSION_DENIED, offset, EVRE_GUARD_WHY_NOT_WRITABLE);
	}
	if (data == nullptr) {
		return refuse(check, PERMISSION_DENIED, offset, EVRE_GUARD_WHY_SETUP);
	}
	return walk(check, offset, data, end);
}

uint8_t evre_guard_write_checked(evre_guard_t *guard, evre_guard_check_t *check, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count) {
	const uint8_t login = evre_guard_write(guard, device, offset, data, count);
	if (login != NO_ERROR) {
		return login; /* EVRE_HANDLED, LOGIN_REQUIRED or PERMISSION_DENIED: no value looked at */
	}
	return evre_guard_check_write(check, device, offset, data, count);
}

uint32_t evre_guard_check_last(evre_guard_check_t *check, uint16_t *addr, uint8_t *why) {
	if (check == nullptr) {
		return 0;
	}
	EVRE_LOCK();
	const uint32_t refused = check->refused;
	const uint16_t lastAddr = check->last_addr;
	const uint8_t lastWhy = check->last_why;
	EVRE_UNLOCK();
	if (addr != nullptr) {
		*addr = lastAddr;
	}
	if (why != nullptr) {
		*why = lastWhy;
	}
	return refused;
}

/* ---------------------------------------------------------------- the init */

/* a raw value, as the table stores it, that the type can hold */
static bool inType(uint8_t type, uint16_t size, uint32_t value) {
	if (type == EVRE_GUARD_F32) {
		return isFinite(value);
	}
	if (isSigned(type)) {
		return toSigned(value & widthMask(size), size) == value;
	}
	return (value & ~widthMask(size)) == 0;
}

/* the entry on its own: type, size, place, the members this version does not use */
static bool entryOk(const evre_guard_desc_t *reg) {
	if (reg->spare1 != 0 || reg->spare2 != 0 || reg->flags != 0 || reg->zero_bits != 0) {
		return false;
	}
	if (reg->type == EVRE_GUARD_BYTES) {
		if (reg->size == 0 || reg->size > MAX_REGS || reg->min != 0 || reg->max != 0 || reg->n_values != 0) {
			return false;
		}
	} else if (!isNumber(reg->type) || reg->size != sizeOf(reg->type)) {
		return false;
	}
	return reg->addr >= BANK_START && endOf(reg) <= BANK_END;
}

/* the limits: inside the type, f32 finite, min <= max in the type's order */
static bool limitsOk(const evre_guard_desc_t *reg) {
	if (reg->type == EVRE_GUARD_BYTES) {
		return true;
	}
	return inType(reg->type, reg->size, reg->min) && inType(reg->type, reg->size, reg->max)
			&& key(reg->type, reg->min) <= key(reg->type, reg->max);
}

/* the register's values: inside the list, inside the type, never -0.0, and
 * strictly ascending as uint32_t (the halving search relies on it) */
static bool listOk(const evre_guard_table_t *table, const evre_guard_desc_t *reg) {
	const uint32_t first = reg->first_value;
	if (first + reg->n_values > table->n_values) {
		return false;
	}
	for (uint32_t ind = first; ind < first + reg->n_values; ++ind) {
		const uint32_t value = table->values[ind];
		if (!inType(reg->type, reg->size, value) || (reg->type == EVRE_GUARD_F32 && value == F32_MINUS_ZERO)) {
			return false;
		}
		if (ind > first && value <= table->values[ind - 1U]) {
			return false;
		}
	}
	return true;
}

/* every entry and list, sorted and without overlap */
static bool tableOk(const evre_guard_table_t *table) {
	if (table->regs == nullptr || table->n_regs == 0 || table->n_regs > MAX_REGS
			|| (table->n_values != 0 && table->values == nullptr)) {
		return false;
	}
	for (uint16_t ind = 0; ind < table->n_regs; ++ind) {
		const evre_guard_desc_t *reg = &table->regs[ind];
		if (!entryOk(reg) || !limitsOk(reg) || !listOk(table, reg)) {
			return false;
		}
		if (ind > 0 && reg->addr < endOf(&table->regs[ind - 1U])) {
			return false;
		}
	}
	return true;
}

/* D_RANGES in order, without overlap. protocolInit() checks that, but a table
 * set after it returns was never checked, and the walk below needs a sorted
 * list. */
static bool rangesInOrder(const evre_base_t *device) {
	for (uint8_t ind = 1; ind < device->D_RANGE_CNT; ++ind) {
		const evre_range_t *before = &device->D_RANGES[ind - 1U];
		if ((uint32_t) before->start + before->len > device->D_RANGES[ind].start) {
			return false;
		}
	}
	return true;
}

/* Each entry inside one writable range: a merge walk of two sorted lists. An
 * entry across two adjacent ranges is refused, as a register lies in one block
 * of memory. A range table protocolInit() refused has D_RANGE_CNT 0: nothing is
 * writable. */
static bool insideRanges(const evre_guard_table_t *table, const evre_base_t *device) {
	uint8_t range = 0;
	for (uint16_t ind = 0; ind < table->n_regs; ++ind) {
		const evre_guard_desc_t *reg = &table->regs[ind];
		while (range < device->D_RANGE_CNT
				&& (uint32_t) device->D_RANGES[range].start + device->D_RANGES[range].len <= reg->addr) {
			++range;
		}
		if (range >= device->D_RANGE_CNT) {
			return false;
		}
		const evre_range_t *holder = &device->D_RANGES[range];
		if (holder->writable == 0 || reg->addr < holder->start || endOf(reg) > (uint32_t) holder->start + holder->len) {
			return false;
		}
	}
	return true;
}

/* A pointer table: every entry inside DEVICE_REG_WRITE_MIN .. DEVICE_REG_READ_MAX,
 * the bank ending at 0xDFFF whatever READ_MAX says, as the library takes it. */
static bool insidePointers(const evre_guard_table_t *table, const evre_base_t *device) {
	if (device->D000 == nullptr) {
		return false;
	}
	const uint32_t last = device->DEVICE_REG_READ_MAX < BANK_END - 1U ? device->DEVICE_REG_READ_MAX : BANK_END - 1U;
	for (uint16_t ind = 0; ind < table->n_regs; ++ind) {
		const evre_guard_desc_t *reg = &table->regs[ind];
		if (reg->addr < device->DEVICE_REG_WRITE_MIN || endOf(reg) > last + 1U) {
			return false;
		}
	}
	return true;
}

static bool fitsDevice(const evre_guard_table_t *table, const evre_base_t *device) {
	if (device->ACCEPT_READ_RESP != 0) {
		return false; /* a mirror: the checks are for devices */
	}
	if (device->D_RANGES != nullptr) {
		return rangesInOrder(device) && insideRanges(table, device);
	}
	return insidePointers(table, device);
}

uint8_t evre_guard_check_init(evre_guard_check_t *check, const evre_guard_table_t *table, const evre_base_t *device) {
	if (check == nullptr) {
		return PERMISSION_DENIED;
	}
	/* checked without the lock: the table is const, and the handler does not
	 * read check->table until it is stored below */
	const bool good = table != nullptr && device != nullptr && tableOk(table) && fitsDevice(table, device);
	EVRE_LOCK();
	check->table = good ? table : nullptr;
	check->refused = 0;
	check->last_addr = 0;
	check->last_why = EVRE_GUARD_WHY_NONE;
	EVRE_UNLOCK();
	return good ? NO_ERROR : PERMISSION_DENIED;
}

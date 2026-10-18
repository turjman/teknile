/* SPDX-License-Identifier: Apache-2.0 */
/*
 * evre_guard_desc.h: EVRe Guard part 2, the register checks.
 *
 * Part 1 (evre_guard.h) decides who may write. Part 2 decides which values: it
 * runs in the write handler, after the login and before the library stores a
 * byte, and reads a const table made from the device's map (evre export MAP
 * --to guard). For each register a host may write the table says where it is,
 * its size and type, its raw limits and the values that always pass. The check
 * walks every register a frame touches and gives one verdict for the whole
 * frame: one bad register refuses it, and nothing is stored.
 *
 * It is:
 *
 *   - refuse-only. It never clamps, never stores and never returns
 *     EVRE_HANDLED: the library stores every byte of the frame or none.
 *   - for the device bank, 0xD000..0xDFFF. A write inside the reserved bank
 *     (CONFIG, the queue's clear, the acks) is the library's and passes.
 *   - after the login: wired through evre_guard_write_checked, a host without
 *     a session gets LOGIN_REQUIRED and learns nothing about the limits.
 *   - a const table and a check with no state on its path, apart from the
 *     refusal counter below.
 *   - optional. A device without it works as before.
 *
 * It is not part of the protocol: the library learns no type, limit or name.
 * It checks no read (the answer is read from memory after the read handler
 * returns), no read-only bit inside a writable register, no state rule or
 * rule across registers, and no frame the device sends by itself. Those stay
 * the device's.
 *
 * The verdicts:
 *
 *   - VALUE_REFUSED (15): a whole register holds a value the device does not
 *     take: outside min..max, NaN or an infinity, a bit that must be 0, or (in
 *     a closed set) a value the register does not list. A value the register
 *     lists (a special, such as 0 "off" below a minimum of 5) always passes.
 *   - PERMISSION_DENIED (3): a byte that cannot be written this way: no entry
 *     covers it (a gap, a read-only register inside a writable range, past the
 *     last entry), or the frame writes only part of a number. Also every write
 *     to the device bank while there is no good table, and every write on a
 *     mirror. 3 is about where, 15 about what.
 *   - NO_ERROR: every register the frame touches passes. A bytes register (a
 *     name, a blob) may be written in any part; only its span is checked.
 *
 * The check flow, for a write (evre_guard_write_checked):
 *
 *   part 1, the login: EVRE_HANDLED, LOGIN_REQUIRED or PERMISSION_DENIED are
 *     returned as they are; on NO_ERROR (a session is open):
 *   part 2, evre_guard_check_write:
 *     a null check or device, a mirror ......................... 3
 *     count 0 .................................................. NO_ERROR
 *     inside the reserved bank (0xA000..0xA105) ................ NO_ERROR
 *     no good table ............................................ 3
 *     not inside 0xD000..0xDFFF, or no data .................... 3
 *     each register the frame touches, in address order:
 *       a byte no entry covers, part of a number ............... 3
 *       a bit that must be 0, NaN or an infinity, a value not
 *         in a closed set, not listed and outside min..max ..... 15
 *     NO_ERROR: the library stores every byte
 *
 * A bad table (init refused it, or never ran) refuses every write to the
 * device bank and lets the reserved bank through: a device whose release
 * carries a bad table can still be reset, and sent into DFU, over EVRe.
 *
 * Time: about log2(n_regs) + 1 steps to find the first register, then one pass
 * per register the frame touches, each with at most 8 steps in a value list.
 * The worst frame is one one-byte register per data byte. A refused frame
 * stops at its first bad register. The check uses no float type: an f32 is
 * compared in integers (see evre_guard_desc.cpp), so it is exact whatever the
 * FPU's flush-to-zero mode, saves no FPU state in the decoder's interrupt and
 * pulls in no soft-float code on an MCU without an FPU.
 *
 * Who writes what (the decoder's context is wherever decodePacket runs, often
 * an interrupt):
 *
 *   the table, the entries, the value list   nobody: const, in flash. A device
 *                                            that builds one in RAM never
 *                                            changes it after init.
 *   check.table                              evre_guard_check_init, in the main
 *                                            loop, under EVRE_LOCK (on a 16-bit
 *                                            MCU a pointer store is two bytes)
 *   check.refused, last_addr, last_why       evre_guard_check_write, in the
 *                                            decoder's context, no lock
 *   (a read of the three)                    evre_guard_check_last, in the main
 *                                            loop, under EVRE_LOCK
 *   device->ACCEPT_READ_RESP                 the device, before the handlers
 *                                            are wired
 *
 * One check per decoder: a device with two links whose decoders run at
 * different priorities gives each its own evre_guard_check_t (the table may be
 * shared). A device that calls evre_guard_check_write from its own main loop,
 * not through the decoder, holds EVRE_LOCK around the call.
 *
 * Wiring, with a login:
 *
 *   static evre_guard_t guard;           (part 1, the login)
 *   static evre_guard_check_t check;     (part 2, the values)
 *   static uint8_t onRead(evre_base_t *d, uint16_t off, uint16_t cnt) { (void) d; return evre_guard_read(&guard, off, cnt); }
 *   static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
 *       return evre_guard_write_checked(&guard, &check, d, off, data, cnt); }    (the login comes first)
 *   ...
 *   protocolInit(&dev);
 *   if (evre_guard_init(&guard, &guard_config) != NO_ERROR) { a bad config: every request refused }
 *   if (evre_guard_check_init(&check, &example_table, &dev) != NO_ERROR) { a bad table: the device bank takes no write }
 *   dev.READ_HANDLER = onRead;
 *   dev.WRITE_HANDLER = onWrite;       (set even when a check failed: without it every write would land)
 *
 * Without a login, the write handler is evre_guard_check_write(&check, d, off,
 * data, cnt) alone. evre_guard_write_checked calls part 1, so such a device
 * still builds evre_guard.cpp; with -ffunction-sections and --gc-sections the
 * linker drops what it never calls. In the main loop:
 *
 *   uint16_t addr;
 *   uint8_t why;
 *   const uint32_t refused = evre_guard_check_last(&check, &addr, &why);
 *
 * The entry and the table below are frozen: the generator writes them
 * positionally, so a new member would change every table. A later need goes
 * into the spare members and the free flag bits, which this version refuses
 * (a newer table on an older Guard fails closed). EVRE_GUARD_TABLE_FORMAT
 * names this layout; a generated file checks it with #error.
 *
 * What the table leaves out, and stays the device's: names and units (the host
 * has the map), scale and offset (the limits are raw), the default (a device
 * starts from its own state), persistence, the effects of an action and of a
 * write-1-to-clear (the main loop's), read-only bits inside a writable
 * register, limits changed at run time, state rules and rules across
 * registers.
 */

#ifndef EVRE_GUARD_DESC_H
#define EVRE_GUARD_DESC_H

#include <stdint.h>

#include "EVRe.h"
#include "evre_guard.h"

#define EVRE_GUARD_TABLE_FORMAT (1U)

/* A register's type. 0 is no type, so a zeroed entry fails init. */
enum EVRE_GUARD_TYPE_ENUM {
	EVRE_GUARD_U8 = 1U,
	EVRE_GUARD_I8 = 2U,
	EVRE_GUARD_U16 = 3U,
	EVRE_GUARD_I16 = 4U,
	EVRE_GUARD_U32 = 5U,
	EVRE_GUARD_I32 = 6U,
	EVRE_GUARD_F32 = 7U,
	EVRE_GUARD_BYTES = 8U
};

enum EVRE_GUARD_FLAG_ENUM {
	EVRE_GUARD_CLOSED = 0x01U /* only the listed values pass (the map's "closed"); any other bit: a bad table */
};

/* Why the last write was refused, for the device's log (evre_guard_check_last). */
enum EVRE_GUARD_WHY_ENUM {
	EVRE_GUARD_WHY_NONE = 0U,         /* nothing refused yet */
	EVRE_GUARD_WHY_SETUP = 1U,        /* no good table, a null pointer, a mirror */
	EVRE_GUARD_WHY_NOT_WRITABLE = 2U, /* a byte no entry covers, or outside the device bank */
	EVRE_GUARD_WHY_PART = 3U,         /* only part of a number register */
	EVRE_GUARD_WHY_BITS = 4U,         /* a bit that must be 0 (the map's "reserved_zero") */
	EVRE_GUARD_WHY_NOT_FINITE = 5U,   /* NaN or an infinity in an f32 register */
	EVRE_GUARD_WHY_NOT_LISTED = 6U,   /* not one of a closed set of values (the map's "closed") */
	EVRE_GUARD_WHY_LIMIT = 7U         /* outside min..max, and not a listed value */
};

/* One register a host may write, 24 B on every target, without padding.
 *
 * Limits are raw values, as bits in a uint32_t: an unsigned value as it is, a
 * signed one sign-extended to 32 bits (-1000 is 0xFFFFFC18), an f32 as its IEEE
 * 754 bits (24.0 is 0x41C00000). A limit the map leaves out is the type's end
 * (u8 0..0xFF, f32 -FLT_MAX..FLT_MAX), so every number takes the same compare.
 * A bytes register has min = max = 0, no list, no flags and no zero_bits: only
 * its span is checked.
 * The member order is fixed for good. */
typedef struct {
	uint16_t addr;        /* the first byte, 0xD000..0xDFFF */
	uint16_t size;        /* bytes: 1, 2 or 4 by the type; 1..0x1000 for BYTES */
	uint8_t type;         /* EVRE_GUARD_U8 .. EVRE_GUARD_BYTES; a uint8_t, so the entry's size never depends on an enum's */
	uint8_t flags;        /* EVRE_GUARD_CLOSED: only listed values pass. Any other bit: a bad table */
	uint8_t n_values;     /* how many values this register lists, 0..255: they always pass (with CLOSED, only they) */
	uint8_t spare1;       /* 0. Kept for a later part. Not 0: a bad table */
	uint16_t first_value; /* where this register's values start in the table's value list */
	uint16_t spare2;      /* 0. Kept for a later part. Not 0: a bad table */
	uint32_t min;         /* the raw low limit, as bits */
	uint32_t max;         /* the raw high limit, as bits */
	uint32_t zero_bits;   /* bits a write must leave 0, in the register's width; integers only; 0: no rule */
} evre_guard_desc_t;

/* The whole table. values is one array for every register: each register's
 * part (first_value, n_values) is sorted strictly ascending as uint32_t,
 * signed values sign-extended and f32 values as bits, never NaN, an infinity
 * or -0.0. n_regs is a uint16_t: a bank holds up to 4096 one-byte registers.
 * The member order is fixed for good. */
typedef struct {
	const evre_guard_desc_t *regs;
	const uint32_t *values;
	uint16_t n_regs;
	uint16_t n_values;
} evre_guard_table_t;

/* The device's check, in RAM: 12 B on a 32-bit MCU. Set up by
 * evre_guard_check_init only, never by an initialiser, so it may grow. */
typedef struct {
	const evre_guard_table_t *table; /* nullptr until a good init: no write to the device bank passes */
	uint32_t refused;                /* writes the check refused; stops at 0xFFFFFFFF */
	uint16_t last_addr;              /* the register of the last refusal (the frame's offset for a setup refusal) */
	uint8_t last_why;                /* EVRE_GUARD_WHY_... */
	/* later parts: new members only below this line */
} evre_guard_check_t;

/* After protocolInit() (the range table is checked by then), in the main loop,
 * before WRITE_HANDLER is set or with the decoder masked. NO_ERROR, or
 * PERMISSION_DENIED for a null pointer, a mirror (ACCEPT_READ_RESP not 0), a
 * bad table, a range table out of order, or an entry that does not lie inside
 * one writable range of the device (D_RANGES, or DEVICE_REG_WRITE_MIN ..
 * DEVICE_REG_READ_MAX for a pointer table). Then every write to the device
 * bank is refused. Either way the diagnostics start from 0. It takes EVRE_LOCK
 * once, to store the result. The table must stay in place, unchanged, while
 * the check is used. */
uint8_t evre_guard_check_init(evre_guard_check_t *check, const evre_guard_table_t *table, const evre_base_t *device);

/* The write handler's body for a device without a login: NO_ERROR,
 * VALUE_REFUSED or PERMISSION_DENIED, never EVRE_HANDLED. It reads only the
 * frame's bytes and the const table, takes no lock, and writes only the
 * diagnostics. Call it where the decoder runs. */
uint8_t evre_guard_check_write(evre_guard_check_t *check, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count);

/* The login first (evre_guard_write), then the values, only on the login's
 * NO_ERROR: without a session no value is looked at. Every other answer of the
 * login is passed on as it is. */
uint8_t evre_guard_write_checked(evre_guard_t *guard, evre_guard_check_t *check, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count);

/* From the main loop: the count of refusals, and the last one's register and
 * reason, read together under EVRE_LOCK (once). addr and why may be nullptr.
 * 0 for a null check. */
uint32_t evre_guard_check_last(evre_guard_check_t *check, uint16_t *addr, uint8_t *why);

#endif

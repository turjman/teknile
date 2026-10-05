/* SPDX-License-Identifier: Apache-2.0 */
/*
 * guard_device.cpp: a whole device with EVRe Guard parts 1 and 2, written the
 * way PROTOCOL.md's wiring and a generated table write it, for the build matrix
 * of run_lib_tests.py: every target and standard compiles it with -Wall
 * -Wextra -Wpedantic -Werror, and the Cortex-M7 row links it with -Oz -flto,
 * where inlining can move the stack numbers. It runs nothing on a PC: main
 * decodes one frame, so the linker keeps the whole guarded write path.
 */

#include "EVRe.h"
#include "evre_guard.h"
#include "evre_guard_desc.h"

#if !defined(EVRE_GUARD_TABLE_FORMAT) || EVRE_GUARD_TABLE_FORMAT != 1
#error "this file is for EVRe Guard table format 1"
#endif

/* as a generated table writes it: the value list, the entries, the table */
static const uint32_t example_values[] = {
	0x00000000UL, /* WATCHDOG_S: 0 "off" */
};

static const evre_guard_desc_t example_regs[] = {
	/* addr, size, type, flags, n_values, spare1, first_value, spare2, min, max, zero_bits */
	{ 0xD040u, 4u, EVRE_GUARD_F32, 0u, 0u, 0u, 0u, 0u, 0x00000000UL, 0x41C00000UL, 0x00000000UL }, /* OUTPUT_V: 0 .. 24 V */
	{ 0xD044u, 2u, EVRE_GUARD_I16, 0u, 0u, 0u, 0u, 0u, 0xFFFFFC18UL, 0x000003E8UL, 0x00000000UL }, /* SPEED: raw -1000 .. 1000 */
	{ 0xD046u, 1u, EVRE_GUARD_U8, 0u, 1u, 0u, 0u, 0u, 0x00000005UL, 0x000000FFUL, 0x00000000UL }, /* WATCHDOG_S: 5 .. 255, or 0 */
};

const evre_guard_table_t example_table = { example_regs, example_values, 3u, 1u };

static uint8_t readOnly[0x40], settings[8], login[16];
static const evre_range_t ranges[] = { { 0xD000, 0x40, readOnly, 0 }, { 0xD040, 8, settings, 1 }, { 0xD048, 16, login, 1 } };

static volatile uint32_t ticks;
static uint64_t now64(void) {
	return ticks;
}

static uint8_t token[16] = { 'd', 'e', 'v', 'i', 'c', 'e' };
static const evre_guard_span_t open_reads[] = { { 0xD000, 4 } };
static const evre_guard_config_t guard_config = {
	0xD048, 16, token,
	open_reads, 1,
	3, 1000, 60000,
	300000,
	now64,
};

static evre_base_t dev;
static evre_guard_t guard;
static evre_guard_check_t check;

static uint8_t onRead(evre_base_t *d, uint16_t off, uint16_t cnt) {
	(void) d;
	return evre_guard_read(&guard, off, cnt);
}

/* The login comes first: without a session no value is looked at. */
static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	return evre_guard_write_checked(&guard, &check, d, off, data, cnt);
}

static uint8_t frame[64] = { 0x7B, 0x01, 0xEB, 0x44, 0xD0, 0x02, 0x00, 0x10, 0x00 };
static uint8_t answer[64];
static volatile uint16_t answerLen;
static volatile uint32_t refused;

int main(void) {
	dev.SALVE_ID_REG = 1;
	dev.D_RANGES = ranges;
	dev.D_RANGE_CNT = 3;
	protocolInit(&dev);
	if (evre_guard_init(&guard, &guard_config) != NO_ERROR) { /* a bad config: every request refused */
	}
	if (evre_guard_check_init(&check, &example_table, &dev) != NO_ERROR) { /* a bad table: the device bank takes no write */
	}
	dev.READ_HANDLER = onRead;
	dev.WRITE_HANDLER = onWrite; /* set even when a check failed: without it every write would land */
	const uint16_t crc = GetCrc16(frame, 9);
	frame[9] = (uint8_t) crc;
	frame[10] = (uint8_t) (crc >> 8);
	frame[11] = 0x7D;
	uint16_t len = 0;
	decodePacketInto(&dev, frame, 12, answer, (uint16_t) sizeof answer, &len);
	answerLen = len;
	uint16_t addr = 0;
	uint8_t why = 0;
	refused = evre_guard_check_last(&check, &addr, &why);
	return (int) (refused + addr + why);
}

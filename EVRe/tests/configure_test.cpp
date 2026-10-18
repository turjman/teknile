/* SPDX-License-Identifier: Apache-2.0 */
/*
 * configure_test.cpp: protocolInit() with a device's own protocolConfigure(),
 * as the firmware has one. run_lib_tests.py builds it, links it with
 * lib/EVRe.cpp and runs it where an override of a weak function is reliable;
 * on MinGW it is only compiled (it says so). No other test here overrides
 * protocolConfigure, so only this one can make it fail.
 *
 * D-20: protocolInit checks the range table whatever protocolConfigure
 * returned, and a table that fails serves nothing. It returns
 * protocolConfigure's code when that failed, else RANGE_TABLE_INVALID for a
 * bad table, else NO_ERROR. And the override itself: it runs once, with the
 * device, after the library's own steps, so a handler it registers stays.
 *
 * D-24: ACCEPT_BROADCAST_D000 set in protocolConfigure, after the library has
 * set STATUS: the first READ of STATUS says CAP_BROADCAST_D000 all the same.
 */

#include <cstdio>
#include <cstring>

#include "EVRe.h"

static int failed = 0;

static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failed++;
	}
}

static unsigned calls;
static evre_base_t *configured;
static uint8_t verdict;
static const evre_range_t *table;
static uint8_t tableCnt;
static uint8_t takesBroadcast;

static void onClear() {
}

/* The device's own, word for word the way EVRe.h asks for it. */
uint8_t protocolConfigure(evre_base_t *device) {
	calls++;
	configured = device;
	device->D_RANGES = table;
	device->D_RANGE_CNT = tableCnt;
	device->MSG_ACK_HANDLER[0] = onClear;
	device->ACCEPT_BROADCAST_D000 = takesBroadcast;
	return verdict;
}

static uint8_t answer[32];

/* a READ of off x4 to slave 1: its return code, the answer in answer[] */
static uint8_t read4(evre_base_t *device, uint16_t off) {
	uint8_t frame[16];
	const uint8_t head[7] = { 0x7B, 1, READ, (uint8_t) off, (uint8_t) (off >> 8), 4, 0 };
	std::memcpy(frame, head, sizeof head);
	const uint16_t crc = GetCrc16(frame, 7);
	frame[7] = (uint8_t) crc;
	frame[8] = (uint8_t) (crc >> 8);
	frame[9] = 0x7D;
	uint16_t len = 0;
	return decodePacketInto(device, frame, 10, answer, sizeof answer, &len);
}

static uint8_t readBank(evre_base_t *device) {
	return read4(device, 0xD000);
}

int main() {
	static uint8_t mem[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	static const evre_range_t good[] = { { 0xD000, 4, mem, 0 }, { 0xD004, 4, mem + 4, 1 } };
	static const evre_range_t unsorted[] = { { 0xD004, 4, mem + 4, 1 }, { 0xD000, 4, mem, 0 } };

	struct Case {
		const char *what;
		uint8_t verdict;
		const evre_range_t *table;
		uint8_t n;
		uint8_t ret;
		uint8_t cnt;
	};
	const Case cases[] = {
		{ "protocolConfigure fails and sets a bad table: its own code, and the table serves nothing (before: served unchecked)",
			MEM_ALLOCATION_FAILED, unsorted, 2, MEM_ALLOCATION_FAILED, 0 },
		{ "protocolConfigure fails and sets a good table: its own code, the table kept", MEM_ALLOCATION_FAILED, good, 2,
			MEM_ALLOCATION_FAILED, 2 },
		{ "protocolConfigure succeeds and sets a bad table: RANGE_TABLE_INVALID, the table serves nothing", NO_ERROR, unsorted, 2,
			RANGE_TABLE_INVALID, 0 },
		{ "protocolConfigure succeeds and sets a good table: NO_ERROR", NO_ERROR, good, 2, NO_ERROR, 2 },
		{ "protocolConfigure fails and sets no table: its own code", PERMISSION_DENIED, nullptr, 0, PERMISSION_DENIED, 0 },
	};
	for (const Case &t : cases) {
		evre_base_t dev;
		dev.SALVE_ID_REG = 1;
		calls = 0;
		configured = nullptr;
		verdict = t.verdict;
		table = t.table;
		tableCnt = t.n;
		const uint8_t ret = protocolInit(&dev);
		if (calls == 0) {
			check(false, "the device's protocolConfigure did not run: the weak default won (this toolchain cannot run this test)");
			break;
		}
		const bool served = t.cnt != 0;
		const uint8_t read = readBank(&dev);
		char what[192];
		std::snprintf(what, sizeof what, "D-20: %s", t.what);
		check(calls == 1 && configured == &dev && dev.MSG_ACK_HANDLER[0] == onClear && ret == t.ret && dev.D_RANGE_CNT == t.cnt
				&& (served ? read == NO_ERROR : read == REG_OFFSET_OUT_OF_RANGE), what);
	}

	for (uint8_t setting = 0; setting < 2 && calls != 0; ++setting) {
		evre_base_t dev;
		dev.SALVE_ID_REG = 1;
		verdict = NO_ERROR;
		table = good;
		tableCnt = 2;
		takesBroadcast = setting;
		const bool init = protocolInit(&dev) == NO_ERROR;
		const bool sent = read4(&dev, 0xA000) == NO_ERROR && (answer[10] & 0x40) == (setting ? 0x40 : 0);
		check(init && sent && ((dev.STATUS & CAP_BROADCAST_D000) != 0) == (setting != 0), setting
				? "D-24: ACCEPT_BROADCAST_D000 1 set in protocolConfigure: the first READ of STATUS says CAP_BROADCAST_D000"
				: "D-24: ACCEPT_BROADCAST_D000 0 in protocolConfigure: STATUS says no CAP_BROADCAST_D000");
	}
	std::printf("%d failed\n", failed);
	return failed ? 1 : 0;
}

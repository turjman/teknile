/* SPDX-License-Identifier: Apache-2.0 */
/*
 * api_compat.cpp: every public name of EVRe.h, used the way the firmware and
 * the host program use it. First the 1.0 names, then the ones 1.1 adds.
 *
 * A header change that renames or removes a name, changes a value or a type,
 * moves a 1.0 member or makes evre_base_t more than a plain struct breaks this
 * file. run_lib_tests.py builds it with -std=c++11 and -std=c++17, -Wall
 * -Wextra -Werror, links it with lib/EVRe.cpp and runs it; on Windows once
 * more with <windows.h> included first (-DWITH_WINDOWS_H). It builds it a third
 * way with -DDEFINE_CONFIGURE -Wmissing-declarations, compile only: a device's
 * protocolConfigure() must still be the header's function. Only compiled,
 * because an override of a weak function is not reliable on every toolchain
 * this suite runs on (MinGW); configure_test.cpp runs one where it is.
 */

#if defined(_WIN32) && defined(WITH_WINDOWS_H)
#include <windows.h> /* first, as a Windows host may: its NO_ERROR macro must not break EVRe.h */
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

#include "EVRe.h"

static int failed = 0;

static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) {
		failed++;
	}
}

/* ================================================================ 1.0 names */

/* -------------------------------------------------- macros, used by value */

static_assert(PACKET_BASE_SIZE == 10U, "PACKET_BASE_SIZE");
static_assert(std::is_unsigned<decltype(PACKET_BASE_SIZE)>::value, "PACKET_BASE_SIZE stays unsigned (host arithmetic)");
static_assert(EVRE_PROTOCOL_VERSION == 1U && BROADCAST_ID == 0U, "version, broadcast id");
static_assert(START == 0U && SLAVE_ID == 1U && FN_CODE == 2U && OFFSET0 == 3U && OFFSET1 == 4U && REG_CNT0 == 5U && REG_CNT1 == 6U,
		"the frame index macros are wire offsets");
#if EVRE_PROTOCOL_VERSION < 1
#error "EVRE_PROTOCOL_VERSION must work in #if"
#endif
#ifndef EVRE_CRC_TABLE_RUNTIME
#error "EVRE_CRC_TABLE_RUNTIME must be defined by EVRe.h"
#endif

/* ------------------------------------------------ enums, by value, unscoped */

static_assert(INVALID_PACKET_ERR == 1 && FUNCTION_CODE_ERR == 2 && PERMISSION_DENIED == 3 && REG_OFFSET_OUT_OF_RANGE == 4
		&& REG_CNT_OUT_OF_RANGE == 5 && MEM_ALLOCATION_FAILED == 6 && SLAVE_ID_MISMATCHED == 7 && MSG_BUFFER_FULL == 8
		&& MSG_NULL == 9 && INSTANCE_IS_NULL == 10 && BUFFER_TOO_SMALL == 11 && LENGTH_MISMATCH == 12, "error codes go on the wire");
static_assert(NO_ERROR == 0, "NO_ERROR");
static_assert(READ == 0xAA && READ_RESP == 0xAB && WRITE == 0xEA && WRITE_ACK == 0xEB && WRITE_ACK_RESP == 0xEC && ERROR_RESP == 0xEE,
		"function codes");
static_assert(READ_ONLY == 0 && READ_WRITE == 1, "permissions");
static_assert(DEVICE_ID_BASE_ADDR == 0xA000 && STATUS_BASE_ADDR == 0xA002 && CONFIG_BASE_ADDR == 0xA004 && MSG_CNT_BASE_ADDR == 0xA006
		&& MSG_BUFFER_BASE_ADDR == 0xA007, "the reserved map");
static_assert(STATUS_VERSION_MASK == 0x00FF && CAP_ERROR_FRAME == 0x0100 && CAP_BROADCAST == 0x0200 && CAP_MSG == 0x0400
		&& CAP_AUTO_SEND == 0x0800 && CAP_DFU == 0x1000 && CAP_STATIC == 0x2000, "STATUS bits");
static_assert(HEARTBEAT == 0 && SYSTEM_RESET == 1 && MSG_ENABLE == 2 && AUTO_SEND == 3 && DFU_MODE == 4, "CONFIG bit numbers");
static_assert(BYTE0 == 0 && BYTE1 == 1 && BYTE2 == 2 && BYTE3 == 3 && BYTE4 == 4 && BYTE5 == 5 && BYTE6 == 6 && BYTE7 == 7, "byte indexes");
static_assert(std::is_enum<ERR_CODE_ENUM>::value && std::is_enum<FN_CODE_ENUM>::value && std::is_enum<CFG_ENUM>::value,
		"plain enums");
static_assert(std::is_convertible<FN_CODE_ENUM, uint8_t>::value && std::is_convertible<ERR_CODE_ENUM, int>::value,
		"unscoped: they convert to integers without a cast");

/* a positional table over the codes, as a host keeps one */
static const char *const errName[] = { "ok", "invalid packet", "function code", "permission denied", "offset out of range",
	"count out of range", "no memory", "slave id", "message buffer full", "null message", "no instance", "buffer too small",
	"length mismatch" };

/* the codes as case labels on a uint8_t, as a host's frame parser does */
static int dataLength(uint8_t fn, uint16_t count) {
	switch (fn) {
		case READ:
		case WRITE_ACK_RESP:
			return 0;
		case READ_RESP:
		case WRITE:
		case WRITE_ACK:
			return count;
		case ERROR_RESP:
			return 1;
		default:
			return -1;
	}
}

/* ------------------------------------------------ the struct and its type */

static_assert(std::is_same<base_t, evre_base_t>::value, "base_t is evre_base_t");
/* The 1.0 struct tag is the one name 1.1 changed (PROTOCOL.md, Migrating from
 * 1.0): `protocol_base` alone is a typedef now, `struct protocol_base` is gone. */
static_assert(std::is_same<protocol_base, evre_base_t>::value && std::is_same<struct evre_base, base_t>::value,
		"protocol_base, without struct, still names the type");
static uint8_t initTagged(protocol_base *dev) { return protocolInit(dev); }
static_assert(std::is_standard_layout<base_t>::value, "a plain struct: offsetof works");
static_assert(std::is_trivially_copyable<base_t>::value && std::is_copy_constructible<base_t>::value, "copied as bytes");
static_assert(std::is_same<decltype(base_t::MSG_ACK_HANDLER[0]), void (*&)(void)>::value, "MSG_ACK_HANDLER entries are void(*)(void)");
/* MSG_BUFFER as in 1.0. MSG_ACK_HANDLER has one slot more since 1.1 (0xFF
 * has one): it is the last 1.0 member, so no 1.0 offset moves, and code that
 * indexes it by a message code compiles and works as before. */
static_assert(sizeof(base_t::MSG_BUFFER) == 255 && sizeof(base_t::MSG_ACK_HANDLER) / sizeof(base_t::MSG_ACK_HANDLER[0]) == 256,
		"the array sizes");

/* the 1.0 struct, member for member: every 1.0 member keeps its 1.0 offset */
struct layout10 {
	uint8_t SALVE_ID_REG;
	uint16_t DEVICE_REG_READ_MAX;
	uint16_t DEVICE_REG_WRITE_MIN;
	const uint16_t RESERVED_REG_READ_MAX;
	const uint16_t RESERVED_REG_WRITE_MIN;
	uint8_t **A000;
	uint8_t **D000;
	uint16_t DEVICE_ID;
	uint16_t STATUS;
	uint16_t CONFIG;
	uint8_t MSG_CNT;
	uint8_t MSG_BUFFER[255];
	void (*MSG_ACK_HANDLER[255])(void);
};
#define SAME_OFFSET(member) static_assert(offsetof(base_t, member) == offsetof(layout10, member), #member " moved from its 1.0 offset")
SAME_OFFSET(SALVE_ID_REG);
SAME_OFFSET(DEVICE_REG_READ_MAX);
SAME_OFFSET(DEVICE_REG_WRITE_MIN);
SAME_OFFSET(RESERVED_REG_READ_MAX);
SAME_OFFSET(RESERVED_REG_WRITE_MIN);
SAME_OFFSET(A000);
SAME_OFFSET(D000);
SAME_OFFSET(DEVICE_ID);
SAME_OFFSET(STATUS);
SAME_OFFSET(CONFIG);
SAME_OFFSET(MSG_CNT);
SAME_OFFSET(MSG_BUFFER);
SAME_OFFSET(MSG_ACK_HANDLER);
#undef SAME_OFFSET

/* a global one is set up at compile time: valid before any constructor runs */
static constexpr base_t atCompileTime{};
static_assert(atCompileTime.RESERVED_REG_READ_MAX == 0xA105 && atCompileTime.RESERVED_REG_WRITE_MIN == 0xA004
		&& atCompileTime.SALVE_ID_REG == 0 && atCompileTime.D000 == nullptr, "constant initialisation");

/* ------------------------------------------------ the functions, as declared */

static_assert(std::is_same<decltype(&GetCrc16), uint16_t (*)(const uint8_t *, int)>::value, "GetCrc16");
static_assert(std::is_same<decltype(&decodePacket), uint8_t (*)(base_t *, uint8_t *, uint16_t, uint8_t **, uint16_t *)>::value,
		"decodePacket");
static_assert(std::is_same<decltype(&encodePacket),
		uint8_t (*)(base_t *, uint8_t, uint8_t, uint16_t, uint16_t, uint8_t *, uint8_t **, uint16_t *)>::value, "encodePacket");
static_assert(std::is_same<decltype(&decodePacketInto),
		uint8_t (*)(base_t *, uint8_t *, uint16_t, uint8_t *, uint16_t, uint16_t *)>::value, "decodePacketInto");
static_assert(std::is_same<decltype(&encodePacketInto),
		uint8_t (*)(base_t *, uint8_t, uint8_t, uint16_t, uint16_t, uint8_t *, uint8_t *, uint16_t, uint16_t *)>::value,
		"encodePacketInto");
static_assert(std::is_same<decltype(&addMsg), uint8_t (*)(base_t *, uint8_t)>::value, "addMsg");
static_assert(std::is_same<decltype(&protocolInit), uint8_t (*)(base_t *)>::value, "protocolInit");
static_assert(std::is_same<decltype(&protocolConfigure), uint8_t (*)(base_t *)>::value,
		"protocolConfigure: a device's override has exactly this type");
#if EVRE_CRC_TABLE_RUNTIME
static_assert(std::is_same<decltype(&EVRe_CrcTableInit), void (*)(void)>::value, "EVRe_CrcTableInit");
#endif
static_assert(sizeof(crctab16) == 512 && sizeof(crctab16[0]) == 2, "crctab16: 256 entries, sizeof works");

#ifdef DEFINE_CONFIGURE
/* A device's override, the way a device writes it. */
uint8_t protocolConfigure(base_t *dev) {
	if (dev == nullptr) {
		return NO_ERROR;
	}
	dev->SALVE_ID_REG = 1;
	dev->STATUS |= (uint16_t) (CAP_AUTO_SEND | CAP_DFU);
	dev->DEVICE_REG_READ_MAX = 0xD0FF;
	dev->DEVICE_REG_WRITE_MIN = 0xD080;
	dev->D000 = new uint8_t*[(0xD0FFU + 1U) & 0x0FFFU];
	return NO_ERROR;
}
#endif

/* ------------------------------------------------ at run time */

static base_t device; /* namespace scope, as a device has it */
static uint8_t registers[256];
static unsigned acked;
static void onAck() { acked++; }

/* What a device's protocolConfigure() does, done after protocolInit() here. */
static void configureLikeADevice(base_t *dev) {
	dev->SALVE_ID_REG = 1;
	dev->DEVICE_ID = 0x2001;
	dev->STATUS |= (uint16_t) (CAP_AUTO_SEND | CAP_DFU);
	dev->DEVICE_REG_READ_MAX = 0xD0FF;
	dev->DEVICE_REG_WRITE_MIN = 0xD080;
	dev->D000 = new uint8_t*[dev->DEVICE_REG_READ_MAX - 0xD000 + 1];
	for (unsigned i = 0; i < 256; ++i) {
		dev->D000[i] = &registers[i];
		registers[i] = (uint8_t) i;
	}
	dev->MSG_ACK_HANDLER[3] = onAck;
}

/* a request built the way a host builds one, with the index macros */
static uint16_t request(uint8_t *frame, uint8_t fn, uint16_t off, uint16_t cnt, const uint8_t *data, uint16_t dataLen) {
	frame[START] = 0x7B;
	frame[SLAVE_ID] = 1;
	frame[FN_CODE] = fn;
	frame[OFFSET0] = (uint8_t) off;
	frame[OFFSET1] = (uint8_t) (off >> 8);
	frame[REG_CNT0] = (uint8_t) cnt;
	frame[REG_CNT1] = (uint8_t) (cnt >> 8);
	for (uint16_t i = 0; i < dataLen; ++i) {
		frame[DATAx(i)] = data[i];
	}
	const uint16_t n = (uint16_t) (PACKET_BASE_SIZE + dataLen);
	const uint16_t crc = GetCrc16(frame, (int) n - 3);
	frame[CRC0(n)] = (uint8_t) crc;
	frame[CRC1(n)] = (uint8_t) (crc >> 8);
	frame[END(n)] = 0x7D;
	return n;
}

static void runTime() {
	check(protocolInit(&device) == NO_ERROR && device.A000 == nullptr, "protocolInit, A000 left nullptr");
	configureLikeADevice(&device);
	check((device.STATUS & (CAP_ERROR_FRAME | CAP_AUTO_SEND | CAP_DFU)) == (CAP_ERROR_FRAME | CAP_AUTO_SEND | CAP_DFU),
			"STATUS: the library's bits and the device's own");
	check(crctab16[1] == 0x1189 && GetCrc16((const uint8_t*) "123456789", 9) == 0x906E, "crctab16 and GetCrc16 (CRC-16/X-25)");

	uint8_t frame[64], resp[64];
	uint16_t len = 0;
	uint16_t n = request(frame, READ, 0xD000, 4, nullptr, 0);
	uint8_t err = decodePacketInto(&device, frame, n, resp, (uint16_t) sizeof(resp), &len);
	check(err == NO_ERROR && len == 14 && resp[FN_CODE] == READ_RESP && resp[SLAVE_ID] == device.SALVE_ID_REG
			&& resp[DATAx(0)] == 0 && resp[DATAx(3)] == 3 && resp[END(len)] == 0x7D
			&& dataLength(resp[FN_CODE], (uint16_t) (resp[REG_CNT0] | (resp[REG_CNT1] << 8))) == 4,
			"decodePacketInto: a READ answered, read back with the index macros");
	check(getBit(device.CONFIG, HEARTBEAT) == 1, "getBit: HEARTBEAT set by the accepted frame");

	len = 0;
	check(encodePacketInto(&device, device.SALVE_ID_REG, READ_RESP, 0xD000, 220, nullptr, resp, 64, &len) == BUFFER_TOO_SMALL
			&& len == 0, "encodePacketInto: AUTO_SEND's call, too small a buffer");
	uint8_t big[240];
	check(encodePacketInto(&device, device.SALVE_ID_REG, READ_RESP, 0xD000, 220, nullptr, big, (uint16_t) sizeof(big), &len) == NO_ERROR
			&& len == 230 && big[DATAx(219)] == 219, "encodePacketInto: AUTO_SEND's call, the bank from D000");

	uint8_t *packet = nullptr;
	uint16_t size = 0;
	check(encodePacket(&device, 1, READ, CONFIG_BASE_ADDR, 2, nullptr, &packet, &size) == NO_ERROR && size == PACKET_BASE_SIZE,
			"encodePacket: allocated");
	uint8_t *answer = nullptr;
	uint16_t answerSize = 0;
	check(decodePacket(&device, packet, size, &answer, &answerSize) == NO_ERROR && answerSize == 12, "decodePacket: allocated");
	free(packet);
	free(answer);

	check(addMsg(&device, (uint8_t) 3) == NO_ERROR && device.MSG_CNT == 1 && device.MSG_BUFFER[0] == 3, "addMsg");
	const uint8_t zero = 0;
	n = request(frame, WRITE_ACK, MSG_BUFFER_BASE_ADDR, 1, &zero, 1);
	check(decodePacketInto(&device, frame, n, resp, (uint16_t) sizeof(resp), &len) == NO_ERROR && acked == 1 && device.MSG_CNT == 0,
			"MSG_ACK_HANDLER[i] = f: acknowledged");

	const uint8_t reset[2] = { 1U << SYSTEM_RESET, 0 };
	n = request(frame, WRITE_ACK, CONFIG_BASE_ADDR, 2, reset, 2);
	decodePacketInto(&device, frame, n, resp, (uint16_t) sizeof(resp), &len);
	const uint16_t cfg = device.CONFIG;
	check(getBit(cfg, SYSTEM_RESET) && !getBit(cfg, DFU_MODE), "getBit on a CONFIG copy");
	clearBit(device.CONFIG, SYSTEM_RESET);
	setBit(device.CONFIG, MSG_ENABLE);
	toggleBit(device.CONFIG, AUTO_SEND);
	check(device.CONFIG == ((1U << HEARTBEAT) | (1U << MSG_ENABLE) | (1U << AUTO_SEND)), "setBit, clearBit, toggleBit on CONFIG");
	check(*getByteAddress(&device.DEVICE_ID, BYTE1) == 0x20, "getByteAddress: DEVICE_ID's high byte");

	n = request(frame, READ, 0xE000, 1, nullptr, 0);
	err = decodePacketInto(&device, frame, n, resp, (uint16_t) sizeof(resp), &len);
	check(err == PERMISSION_DENIED && resp[FN_CODE] == ERROR_RESP && std::strcmp(errName[resp[DATAx(0)]], "permission denied") == 0,
			"ERROR_RESP: the code indexes a host's table of names");

	/* the host program's mirror (1.1: ACCEPT_READ_RESP), and its clean-up */
	base_t mirror;
	check(initTagged(&mirror) == NO_ERROR, "a mirror: protocolInit, through a protocol_base * (the 1.0 tag, as a type name)");
	mirror.SALVE_ID_REG = 1;
	mirror.DEVICE_REG_READ_MAX = 0xD0FF;
	mirror.D000 = new (std::nothrow) uint8_t*[0x100];
	static uint8_t shadow[0x100];
	for (unsigned i = 0; i < 0x100; ++i) {
		mirror.D000[i] = &shadow[i];
	}
	mirror.ACCEPT_READ_RESP = 1;
	const uint8_t values[2] = { 0x5A, 0xA5 };
	check(encodePacketInto(&device, 1, READ_RESP, 0xD010, 2, (uint8_t*) values, frame, (uint16_t) sizeof(frame), &len) == NO_ERROR,
			"a READ_RESP built from pData");
	check(decodePacketInto(&mirror, frame, len, resp, (uint16_t) sizeof(resp), &n) == NO_ERROR && shadow[0x10] == 0x5A && shadow[0x11] == 0xA5,
			"the mirror stores the READ_RESP");
	delete[] mirror.A000;
	delete[] mirror.D000;
	delete[] device.A000; /* 1.0 allocated A000 in protocolInit and hosts free it: still harmless */
	delete[] device.D000;
	device.D000 = nullptr;
	mirror.D000 = nullptr;
	check(true, "delete[] on A000 and D000 after protocolInit");

	base_t copy(device);
	check(copy.DEVICE_ID == device.DEVICE_ID && copy.RESERVED_REG_READ_MAX == 0xA105, "a copy of a base_t");
}

/* ================================================================ 1.1 names */

static_assert(EVRE_LIB_VERSION >= 0x0101U && EVRE_HANDLED == 0xFFU, "EVRE_LIB_VERSION, EVRE_HANDLED");
#if EVRE_LIB_VERSION < 0x0101
#error "EVRE_LIB_VERSION must work in #if"
#endif
#ifndef EVRE_WEAK
#error "EVRE_WEAK"
#endif
#if !defined(EVRE_LOCK) || !defined(EVRE_UNLOCK)
#error "EVRE_LOCK, EVRE_UNLOCK"
#endif
static_assert(LOGIN_REQUIRED == 13 && RANGE_TABLE_INVALID == 14, "LOGIN_REQUIRED, RANGE_TABLE_INVALID");
static_assert(CAP_BROADCAST_D000 == 0x4000 && std::is_same<decltype(CAP_BROADCAST_D000), STATUS_ENUM>::value,
		"CAP_BROADCAST_D000: STATUS bit 14, among the STATUS bits");
static_assert(std::is_same<struct evre_range, evre_range_t>::value && std::is_same<struct evre_base, evre_base_t>::value,
		"the struct tags");
static_assert(std::is_same<decltype(evre_base_t::WRITE_HANDLER), evre_write_handler_t>::value
		&& std::is_same<decltype(evre_base_t::READ_HANDLER), evre_read_handler_t>::value, "the handler types");
static_assert(std::is_same<evre_write_handler_t, uint8_t (*)(evre_base_t *, uint16_t, const uint8_t *, uint16_t)>::value
		&& std::is_same<evre_read_handler_t, uint8_t (*)(evre_base_t *, uint16_t, uint16_t)>::value, "the handler signatures");
static_assert(offsetof(evre_base_t, SLAVE_ID_REG) == offsetof(evre_base_t, SALVE_ID_REG) && sizeof(evre_base_t::SLAVE_ID_REG) == 1,
		"SLAVE_ID_REG is SALVE_ID_REG");
static_assert(offsetof(evre_base_t, WRITE_HANDLER) > offsetof(evre_base_t, MSG_ACK_HANDLER)
		&& offsetof(evre_base_t, D_RANGES) > offsetof(evre_base_t, MSG_ACK_HANDLER)
		&& offsetof(evre_base_t, ACCEPT_BROADCAST_D000) > offsetof(evre_base_t, MSG_ACK_HANDLER)
		&& offsetof(evre_base_t, RX_SLAVE_ID) > offsetof(evre_base_t, MSG_ACK_HANDLER), "1.1 members after every 1.0 member");
static_assert(std::is_same<decltype(evre_base_t::ACCEPT_READ_RESP), uint8_t>::value
		&& std::is_same<decltype(evre_base_t::ACCEPT_BROADCAST_D000), uint8_t>::value
		&& std::is_same<decltype(evre_base_t::RX_SLAVE_ID), uint8_t>::value, "the 1.1 flags are uint8_t");
static_assert(atCompileTime.ACCEPT_READ_RESP == 0 && atCompileTime.ACCEPT_BROADCAST_D000 == 0 && atCompileTime.RX_SLAVE_ID == 0,
		"a device takes no response and no broadcast into its bank, by default");

/* The type names too, not only the values: a device declares a variable of an
 * enum type, and a table of ranges names the members or lists them in order. */
static_assert(std::is_same<decltype(READ_WRITE), PERMISSION_ENUM>::value && std::is_same<decltype(CONFIG_BASE_ADDR), RESERVED_MAP_ENUM>::value
		&& std::is_same<decltype(CAP_MSG), STATUS_ENUM>::value && std::is_same<decltype(BYTE1), BYTE_IND_ENUM>::value,
		"the 1.0 enum types, each value in its own");
static_assert(std::is_same<decltype(evre_range_t::start), uint16_t>::value && std::is_same<decltype(evre_range_t::len), uint16_t>::value
		&& std::is_same<decltype(evre_range_t::base), uint8_t *>::value && std::is_same<decltype(evre_range_t::writable), uint8_t>::value,
		"evre_range_t members by name");
static_assert(offsetof(evre_range_t, start) < offsetof(evre_range_t, len) && offsetof(evre_range_t, len) < offsetof(evre_range_t, base)
		&& offsetof(evre_range_t, base) < offsetof(evre_range_t, writable), "evre_range_t members in their order: tables are positional");

/* the macros are safe in expressions now; the bit macros keep their types */
static_assert(DATAx(0) + 1 == 8 && CRC0(10) + 1 == 8 && 2 * END(10) == 18 && DATAx(5 & 3) == 8, "parenthesised macros");
static_assert(std::is_same<decltype(getBit((uint16_t) 6, 1)), int>::value, "getBit on a uint16_t is an int, as in 1.0");

static uint8_t onWrite(evre_base_t *, uint16_t, const uint8_t *, uint16_t) { return EVRE_HANDLED; }
static uint8_t onRead(evre_base_t *, uint16_t, uint16_t) { return NO_ERROR; }

static void newNames() {
	static uint8_t ro[4] = { 1, 2, 3, 4 }, rw[4];
	static const evre_range_t table[] = { { 0xD000, 4, ro, READ_ONLY }, { 0xD004, 4, rw, READ_WRITE } };
	evre_base_t dev;
	dev.SLAVE_ID_REG = 7;
	check(dev.SALVE_ID_REG == 7, "SLAVE_ID_REG writes SALVE_ID_REG");
	dev.SALVE_ID_REG = 1;
	check(dev.SLAVE_ID_REG == 1, "and the other way round");
	dev.D_RANGES = table;
	dev.D_RANGE_CNT = 2;
	check(protocolInit(&dev) == NO_ERROR, "a range table set before protocolInit");
	evre_write_handler_t w = onWrite;
	evre_read_handler_t r = onRead;
	dev.WRITE_HANDLER = w;
	dev.READ_HANDLER = r;
	dev.ACCEPT_READ_RESP = 0;
	dev.ACCEPT_BROADCAST_D000 = 0;
	uint8_t frame[32], resp[32];
	uint16_t len = 0;
	const uint8_t data[2] = { 9, 9 };
	uint16_t n = request(frame, WRITE_ACK, 0xD004, 2, data, 2);
	check(decodePacketInto(&dev, frame, n, resp, (uint16_t) sizeof(resp), &len) == NO_ERROR && len == 10 && rw[0] == 0
			&& dev.RX_SLAVE_ID == 1, "the handler typedefs: EVRE_HANDLED, acknowledged, nothing stored; RX_SLAVE_ID the frame's");
	n = request(frame, READ, 0xD000, 8, nullptr, 0);
	check(decodePacketInto(&dev, frame, n, resp, (uint16_t) sizeof(resp), &len) == NO_ERROR && len == 18 && resp[DATAx(3)] == 4,
			"ranges: a read across both");
	const uint8_t a = 8, b = 0;
	check(getBit(a | b, 3) == 1 && getBit(0x80000000U, 31) == 1U, "getBit in an expression, and for bit 31");
	uint64_t wide = ~0ULL;
	clearBit(wide, 3);
	check(wide == 0xFFFFFFFFFFFFFFF7ULL, "clearBit on a 64-bit value keeps the upper bits");
	EVRE_LOCK();
	EVRE_UNLOCK();
	dev.MSG_ACK_HANDLER[0xFF] = onAck;
	check(protocolInit(&dev) == NO_ERROR && dev.MSG_ACK_HANDLER[0xFF] == nullptr, "MSG_ACK_HANDLER[0xFF]: a slot, cleared by protocolInit");
}

int main() {
	runTime();
	newNames();
	std::printf("%d failed\n", failed);
	return failed ? 1 : 0;
}

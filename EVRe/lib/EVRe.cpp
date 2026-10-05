/* SPDX-License-Identifier: Apache-2.0 */
/*
 * EVRe.cpp
 *
 *  Created on: Aug 5, 2024
 *      Author: Ahmed Ragab AbdulGhany
 *
 * The protocol moves bytes and never interprets them: where they are is the
 * device's table (D000 or D_RANGES), what they mean is the layer above's
 * (READ_HANDLER / WRITE_HANDLER, EVRe Guard).
 *
 * Reading order: the frame (header, CRC, building), the banks (checks, then
 * reading and writing), the messages, the function codes, the public API.
 *
 * Where the order of two steps decides what a frame gets, a comment says so,
 * and tests/lib_test.cpp holds a frame that tells the two orders apart.
 */

#include "EVRe.h"

#include <stddef.h>
#include <string.h>
#include <type_traits>

/* ================================================================== frame */

static const uint8_t FRAME_START_BYTE = 0x7B;
static const uint8_t FRAME_END_BYTE = 0x7D;
static const uint16_t FRAME_DATA = 0x07; /* DATAx(0) */
static const uint16_t FRAME_TRAILER = 0x03; /* CRC (2) and end byte (1): the CRC covers what comes before */

/* The four header fields, parsed once. */
typedef struct {
	uint8_t slave;
	uint8_t fn;
	uint16_t offset;
	uint16_t count;
} frame_header_t;

/* Every multi-byte field is little endian on the wire, whatever the CPU. The
 * high byte is shifted as unsigned: with a 16-bit int, 0xFF << 8 overflows an
 * int. */
static inline uint16_t getLE16(const uint8_t *bytes) {
	return (uint16_t) (bytes[0] | ((unsigned) bytes[1] << 8));
}

static inline void putLE16(uint8_t *bytes, uint16_t value) {
	bytes[0] = (uint8_t) value;
	bytes[1] = (uint8_t) (value >> 8);
}

static frame_header_t readHeader(const uint8_t *frame) {
	frame_header_t header;
	header.slave = frame[SLAVE_ID];
	header.fn = frame[FN_CODE];
	header.offset = getLE16(&frame[OFFSET0]);
	header.count = getLE16(&frame[REG_CNT0]);
	return header;
}

static bool crcMatches(const uint8_t *frame, uint16_t size) {
	return getLE16(&frame[size - FRAME_TRAILER]) == GetCrc16(frame, size - FRAME_TRAILER);
}

/* Whether the bytes can be believed at all. A frame that fails is line noise:
 * not even its slave id can be trusted, so it is never answered (several
 * slaves could answer at once). The delimiters before the CRC: they cost
 * nothing, and on a packet link no framer has looked at them. */
static bool wellFormed(const uint8_t *frame, uint16_t size) {
	if (frame == nullptr || size < PACKET_BASE_SIZE) {
		return false;
	}
	if (frame[START] != FRAME_START_BYTE || frame[size - 1] != FRAME_END_BYTE) {
		return false;
	}
	return crcMatches(frame, size);
}

static bool isRequest(uint8_t fn) {
	return fn == READ || fn == WRITE || fn == WRITE_ACK;
}

static bool isResponse(uint8_t fn) {
	return fn == READ_RESP || fn == WRITE_ACK_RESP || fn == ERROR_RESP;
}

/* WRITE, WRITE_ACK and READ_RESP carry count data bytes, READ and
 * WRITE_ACK_RESP none. In 32 bits, on a 16-bit int too: a count near 0xFFFF
 * cannot wrap to a small size. */
static uint32_t expectedSize(const frame_header_t *header) {
	const bool hasData = header->fn == WRITE || header->fn == WRITE_ACK || header->fn == READ_RESP;
	return (uint32_t) PACKET_BASE_SIZE + (hasData ? header->count : 0U);
}

/* Room in outBuf for a frame of dataLen data bytes. */
static bool fits(const uint8_t *outBuf, uint16_t outMax, uint16_t dataLen) {
	return outBuf != nullptr && outMax >= (uint32_t) PACKET_BASE_SIZE + dataLen;
}

/* The seven bytes before the data. The data goes to &frame[FRAME_DATA]. */
static void putHeader(uint8_t *frame, uint8_t slave, uint8_t fn, uint16_t offset, uint16_t count) {
	frame[START] = FRAME_START_BYTE;
	frame[SLAVE_ID] = slave;
	frame[FN_CODE] = fn;
	putLE16(&frame[OFFSET0], offset);
	putLE16(&frame[REG_CNT0], count);
}

/* CRC and end byte, once header and data are in place. Returns the length. */
static uint16_t sealFrame(uint8_t *frame, uint16_t dataLen) {
	const uint16_t size = (uint16_t) (PACKET_BASE_SIZE + dataLen);
	putLE16(&frame[size - FRAME_TRAILER], GetCrc16(frame, size - FRAME_TRAILER));
	frame[size - 1] = FRAME_END_BYTE;
	return size;
}

/* ================================================================== banks */

static const uint16_t BANK_MASK = 0xF000;
static const uint16_t RESERVED_BANK = 0xA000;
static const uint16_t DEVICE_BANK = 0xD000;
static const uint32_t DEVICE_BANK_END = 0xE000; /* one past 0xDFFF */
static const uint16_t BANK_SIZE = 0x1000;
static const uint16_t MSG_BUFFER_SIZE = 0x0FF;
static const uint16_t MSG_ACK_HANDLER_CNT = sizeof(evre_base_t::MSG_ACK_HANDLER) / sizeof(evre_base_t::MSG_ACK_HANDLER[0]);

/* 1.1: the reserved bank 0xA000..0xA105 is evre_base_t's DEVICE_ID, STATUS, CONFIG,
 * MSG_CNT and MSG_BUFFER, one after the other, read and written in place: no
 * pointer table (1 KB of RAM), no allocation. offsetof needs a plain struct. */
static_assert(std::is_standard_layout<evre_base_t>::value, "evre_base_t must stay a plain struct (see EVRe.h)");
/* Only a constexpr default constructor passes: a member set by a function or
 * a constructor would make a global device wait for the static constructors. */
static_assert(evre_base_t().RESERVED_REG_READ_MAX == 0xA105, "a global evre_base_t must be set up at compile time (see EVRe.h)");
static_assert(offsetof(evre_base_t, STATUS) == offsetof(evre_base_t, DEVICE_ID) + 2, "STATUS must be at 0xA002");
static_assert(offsetof(evre_base_t, CONFIG) == offsetof(evre_base_t, DEVICE_ID) + 4, "CONFIG must be at 0xA004");
static_assert(offsetof(evre_base_t, MSG_CNT) == offsetof(evre_base_t, DEVICE_ID) + 6, "MSG_CNT must be at 0xA006");
static_assert(offsetof(evre_base_t, MSG_BUFFER) == offsetof(evre_base_t, DEVICE_ID) + 7, "MSG_BUFFER must be at 0xA007");
static_assert(sizeof(evre_base_t::MSG_BUFFER) == MSG_BUFFER_SIZE, "MSG_BUFFER must end at 0xA105");

static inline uint16_t bankOf(uint16_t offset) {
	return (uint16_t) (offset & BANK_MASK);
}

/* From the start of the struct, not from &DEVICE_ID: the bytes past DEVICE_ID
 * are other members, and a pointer walked out of one member is undefined (GCC
 * -O2 warns about it). */
static inline uint8_t *reservedAt(evre_base_t *device, uint16_t addr) {
	return ((uint8_t*) device) + offsetof(evre_base_t, DEVICE_ID) + (addr - RESERVED_BANK);
}

/* In 32 bits, so that no count can wrap it past a limit. For a count of 0 it
 * is offset - 1 (offset is 0xA000 or more wherever it is used). */
static inline uint32_t lastByte(uint16_t offset, uint16_t count) {
	return (uint32_t) offset + count - 1U;
}

/* The reserved bank is writable up to the last queued message: 0xA006 + MSG_CNT. */
static inline uint32_t reservedWriteMax(const evre_base_t *device) {
	return (uint32_t) device->RESERVED_REG_READ_MAX - MSG_BUFFER_SIZE + device->MSG_CNT;
}

/* The pointer table's last register. The bank ends at 0xDFFF whatever
 * READ_MAX says: past it a request would run beyond a table of 0x1000
 * entries, and the index would wrap to its start. */
static inline uint32_t pointerReadMax(const evre_base_t *device) {
	return (device->DEVICE_REG_READ_MAX < DEVICE_BANK_END - 1U) ? device->DEVICE_REG_READ_MAX : DEVICE_BANK_END - 1U;
}

/* The 1.0 permission model, one boundary per bank: the start lies in
 * [lowest, highest], the last byte at lastMax at most. The start is judged
 * first, so a frame wrong in both gets badStart. */
static uint8_t checkBounds(uint16_t offset, uint16_t count, uint32_t lowest, uint32_t highest, uint32_t lastMax, uint8_t badStart) {
	if (offset < lowest || offset > highest) {
		return badStart;
	}
	if (lastByte(offset, count) > lastMax) {
		return REG_CNT_OUT_OF_RANGE;
	}
	return NO_ERROR;
}

/* ------------------------------------------------ the range table (1.1) */

static inline uint32_t rangeEnd(const evre_range_t *range) {
	return (uint32_t) range->start + range->len;
}

/* The range holding addr, or nullptr. The table is sorted, so the search
 * stops at the first range past addr. */
static const evre_range_t *findRange(const evre_base_t *device, uint32_t addr) {
	for (uint8_t ind = 0; ind < device->D_RANGE_CNT; ++ind) {
		const evre_range_t *range = &device->D_RANGES[ind];
		if (addr < range->start) {
			return nullptr;
		}
		if (addr < rangeEnd(range)) {
			return range;
		}
	}
	return nullptr;
}

/* The next range when it starts where this one ends, else nullptr (a gap or
 * the end of the table). In a sorted table without overlaps the range that
 * holds rangeEnd(range), if any, can only be the next one, so there is no need
 * to search the table again for it. */
static const evre_range_t *nextAdjacent(const evre_base_t *device, const evre_range_t *range) {
	const evre_range_t *next = range + 1;
	if (next == device->D_RANGES + device->D_RANGE_CNT || next->start != rangeEnd(range)) {
		return nullptr;
	}
	return next;
}

/* The pointer table's answers, on ranges: a start in no range is 4 for a read
 * and 3 for a write. A read-only range in a write is 3, judged before a gap
 * further on (5). With [0xD000, WRITE_MIN) read-only and [WRITE_MIN, READ_MAX]
 * read-write this answers exactly as the pointer table does. */
static uint8_t checkRanges(const evre_base_t *device, uint16_t offset, uint16_t count, bool forWrite) {
	const evre_range_t *range = findRange(device, offset);
	if (range == nullptr) {
		return forWrite ? PERMISSION_DENIED : REG_OFFSET_OUT_OF_RANGE;
	}
	const uint32_t end = (uint32_t) offset + count;
	while (true) {
		if (forWrite && !range->writable) {
			return PERMISSION_DENIED;
		}
		if (end <= rangeEnd(range)) {
			return NO_ERROR;
		}
		range = nextAdjacent(device, range);
		if (range == nullptr) {
			return REG_CNT_OUT_OF_RANGE;
		}
	}
}

/* One memcpy per range. checkRanges() has passed, so the ranges are adjacent. */
static void readRanges(const evre_base_t *device, uint16_t offset, uint16_t count, uint8_t *out) {
	uint32_t addr = offset;
	const uint32_t end = addr + count;
	for (const evre_range_t *range = findRange(device, offset); addr < end; ++range) {
		const uint32_t stop = (rangeEnd(range) < end) ? rangeEnd(range) : end;
		memcpy(out, range->base + (addr - range->start), stop - addr);
		out += stop - addr;
		addr = stop;
	}
}

static void writeRanges(const evre_base_t *device, uint16_t offset, uint16_t count, const uint8_t *data) {
	uint32_t addr = offset;
	const uint32_t end = addr + count;
	for (const evre_range_t *range = findRange(device, offset); addr < end; ++range) {
		const uint32_t stop = (rangeEnd(range) < end) ? rangeEnd(range) : end;
		memcpy(range->base + (addr - range->start), data, stop - addr);
		data += stop - addr;
		addr = stop;
	}
}

/* protocolInit(): a table the functions above can rely on */
static uint8_t validRanges(const evre_base_t *device) {
	uint32_t next = DEVICE_BANK;
	for (uint8_t ind = 0; ind < device->D_RANGE_CNT; ++ind) {
		const evre_range_t *range = &device->D_RANGES[ind];
		if (range->base == nullptr || range->len == 0 || range->start < next || rangeEnd(range) > DEVICE_BANK_END) {
			return RANGE_TABLE_INVALID;
		}
		next = rangeEnd(range);
	}
	return NO_ERROR;
}

/* ------------------------------------------------ checks, per direction */

/* A READ, or a READ_RESP, which reports registers and so has the read limits. */
static uint8_t checkRead(const evre_base_t *device, uint16_t offset, uint16_t count) {
	switch (bankOf(offset)) {
		case RESERVED_BANK:
			return checkBounds(offset, count, RESERVED_BANK, device->RESERVED_REG_READ_MAX,
					device->RESERVED_REG_READ_MAX, REG_OFFSET_OUT_OF_RANGE);
		case DEVICE_BANK:
			if (device->D_RANGES != nullptr) {
				return checkRanges(device, offset, count, false);
			}
			return checkBounds(offset, count, DEVICE_BANK, pointerReadMax(device), pointerReadMax(device),
					REG_OFFSET_OUT_OF_RANGE);
		default:
			return PERMISSION_DENIED;
	}
}

/* A WRITE, a WRITE_ACK, or the WRITE_ACK_RESP that answers one. */
static uint8_t checkWrite(const evre_base_t *device, uint16_t offset, uint16_t count) {
	switch (bankOf(offset)) {
		case RESERVED_BANK:
			/* DEVICE_ID and STATUS are read-only; past the last queued message
			 * there is nothing to acknowledge */
			return checkBounds(offset, count, device->RESERVED_REG_WRITE_MIN, device->RESERVED_REG_READ_MAX,
					reservedWriteMax(device), PERMISSION_DENIED);
		case DEVICE_BANK:
			if (device->D_RANGES != nullptr) {
				return checkRanges(device, offset, count, true);
			}
			return checkBounds(offset, count, device->DEVICE_REG_WRITE_MIN, pointerReadMax(device),
					pointerReadMax(device), PERMISSION_DENIED);
		default:
			return PERMISSION_DENIED; /* 1.1: an unknown bank, as for a read (1.0 acknowledged and dropped it) */
	}
}

/* ------------------------------------------------ reading and writing */

/* 1.1: a device's STATUS carries CAP_BROADCAST_D000 exactly when
 * ACCEPT_BROADCAST_D000 is set. The device may set that at any time (in
 * protocolConfigure, after protocolInit), so the bit is brought up to date
 * each time STATUS goes out, and only then: no call for the device to make, no
 * stale answer. Only for bytes that hold it. Written only when it is wrong, so
 * the decoder writes STATUS once after the setting changes, not on every read.
 * Set or cleared, never flipped: two refreshes that interleave (the main
 * loop's AUTO_SEND and the decoder's interrupt) still leave the right bit.
 *
 * A mirror (ACCEPT_READ_RESP set) is never refreshed. Its STATUS is its
 * device's, as the last READ_RESP stored it, bit 14 included, and a host that
 * serves it must pass on the device's bit, not the mirror's own setting. So a
 * READ the mirror answers (an echo of the host's own READ, say) and a
 * READ_RESP it builds carry the stored bit. */
static void refreshStatus(evre_base_t *device, uint16_t offset, uint16_t count) {
	if (device->ACCEPT_READ_RESP) {
		return;
	}
	/* bit 14 lies in STATUS's high byte, at 0xA003 on a little-endian CPU (the
	 * reserved bank goes out in the CPU's order: PROTOCOL.md, Build options,
	 * Porting) */
	const uint16_t capByte = STATUS_BASE_ADDR + 1;
	if (offset > capByte || lastByte(offset, count) < capByte) {
		return;
	}
	const uint16_t want = device->ACCEPT_BROADCAST_D000 ? (uint16_t) CAP_BROADCAST_D000 : (uint16_t) 0U;
	if ((device->STATUS & CAP_BROADCAST_D000) != want) {
		device->STATUS = (uint16_t) ((device->STATUS & ~CAP_BROADCAST_D000) | want);
	}
}

/* Either bank, whichever way the device bank is kept. checkRead() has passed. */
static void readBank(evre_base_t *device, uint16_t offset, uint16_t count, uint8_t *out) {
	if (bankOf(offset) == RESERVED_BANK) {
		refreshStatus(device, offset, count);
		memcpy(out, reservedAt(device, offset), count);
	} else if (device->D_RANGES != nullptr) {
		readRanges(device, offset, count, out);
	} else {
		for (uint32_t ind = 0; ind < count; ++ind) {
			out[ind] = *(device->D000[(offset + ind) & 0x0FFF]);
		}
	}
}

/* The device bank: no meaning attached, the bytes land as they came. */
static void writeDevice(evre_base_t *device, uint16_t offset, uint16_t count, const uint8_t *data) {
	if (device->D_RANGES != nullptr) {
		writeRanges(device, offset, count, data);
	} else {
		for (uint32_t ind = 0; ind < count; ++ind) {
			*(device->D000[(offset + ind) & 0x0FFF]) = data[ind];
		}
	}
}

/* ================================================================ messages */

/* Every code has a slot, 0xFF too, so no code reads past the array. */
static_assert(MSG_ACK_HANDLER_CNT == 0x100, "MSG_ACK_HANDLER needs a slot for every uint8_t code");
static void runAckHandler(evre_base_t *device, uint8_t code) {
	void (*const handler)(void) = device->MSG_ACK_HANDLER[code];
	if (handler != nullptr) {
		handler();
	}
}

/* Past MSG_CNT the buffer reads 0, so a host that reads all of it never sees
 * a code again that is already gone. */
static void zeroPastCount(evre_base_t *device) {
	memset(&device->MSG_BUFFER[device->MSG_CNT], 0, MSG_BUFFER_SIZE - device->MSG_CNT);
}

/* Any value written to MSG_CNT empties the queue. The queue is empty before
 * the handler runs, so the handler sees it empty, and it runs outside the
 * lock: it may queue a message itself, and that one is kept (the write that
 * cleared acknowledges nothing, see writeReserved). */
static void clearMessages(evre_base_t *device) {
	EVRE_LOCK();
	device->MSG_CNT = 0;
	zeroPastCount(device);
	EVRE_UNLOCK();
	runAckHandler(device, 0);
}

/* A write to MSG_BUFFER[ind] acknowledges that message: its handler runs, then
 * its slot is set to 0 for compactMessages() to close up. No lock: addMsg
 * writes at MSG_CNT, and ind is below it. checkWrite() lets a write reach the
 * last queued message at most, MSG_CNT only grows until the compaction, and a
 * write that clears acknowledges nothing. */
static void ackMessage(evre_base_t *device, uint8_t ind) {
	runAckHandler(device, device->MSG_BUFFER[ind]);
	device->MSG_BUFFER[ind] = 0;
}

/* In place: kept never passes ind, so no slot is overwritten before it is
 * read, and no stack copy is needed (this runs in the decoder's interrupt).
 * The messages below an acknowledged one keep their index, the ones above move
 * down: that is what lets a host acknowledge highest index first. MSG_CNT is
 * read after the acknowledgements, so a message an ack handler queued is
 * kept. */
static void compactMessages(evre_base_t *device) {
	EVRE_LOCK();
	const uint8_t queued = device->MSG_CNT;
	uint8_t kept = 0;
	for (uint8_t ind = 0; ind < queued; ++ind) {
		if (device->MSG_BUFFER[ind] != 0) {
			device->MSG_BUFFER[kept] = device->MSG_BUFFER[ind];
			++kept;
		}
	}
	device->MSG_CNT = kept;
	zeroPastCount(device);
	EVRE_UNLOCK();
}

/* A host's write to the reserved bank: CONFIG takes the bytes, MSG_CNT and
 * MSG_BUFFER take the act of writing (the bytes written there do not matter).
 * Byte by byte in address order, so the handlers run in that order. A run of
 * acknowledgements is marked first and compacted once.
 *
 * 1.1: the clear wins. The slots come after MSG_CNT, so once it is written the
 * rest of the write is slot bytes, and they do nothing: the write is the
 * clear alone. MSG_ACK_HANDLER[0] runs once, and a message it queues is kept;
 * an ack after the clear would have met that message in slot 0 and taken it
 * before any host saw it. CONFIG bytes before MSG_CNT are written as usual. */
static void writeReserved(evre_base_t *device, uint16_t offset, uint16_t count, const uint8_t *data) {
	bool acked = false;
	for (uint32_t ind = 0; ind < count; ++ind) {
		const uint16_t addr = (uint16_t) (offset + ind);
		if (addr == MSG_CNT_BASE_ADDR) {
			clearMessages(device);
			return; /* nothing acknowledged yet: the slots lie after MSG_CNT */
		} else if (addr >= MSG_BUFFER_BASE_ADDR && addr < MSG_BUFFER_BASE_ADDR + MSG_BUFFER_SIZE) {
			ackMessage(device, (uint8_t) (addr - MSG_BUFFER_BASE_ADDR));
			acked = true;
		} else {
			*(reservedAt(device, addr)) = data[ind];
		}
	}
	if (acked) {
		compactMessages(device);
	}
}

/* A host's write: the reserved bank's registers act on it, the device bank
 * takes the bytes. checkWrite() has passed. */
static void writeBank(evre_base_t *device, uint16_t offset, uint16_t count, const uint8_t *data) {
	if (bankOf(offset) == RESERVED_BANK) {
		writeReserved(device, offset, count, data);
	} else {
		writeDevice(device, offset, count, data);
	}
}

/* A mirror's copy of a device's registers: the bytes as they came, into either
 * bank, the message queue included. checkRead() has passed. */
static void copyToBank(evre_base_t *device, uint16_t offset, uint16_t count, const uint8_t *data) {
	if (bankOf(offset) == RESERVED_BANK) {
		memcpy(reservedAt(device, offset), data, count);
	} else {
		writeDevice(device, offset, count, data);
	}
}

/* ========================================================= function codes */

/* EVRE_HANDLED is a write handler's word, but a read handler that uses it
 * means the same: go on. RX_SLAVE_ID is set just before the call: whether
 * every slave heard the frame is framing, and only the frame knows it. */
static uint8_t askReadHandler(evre_base_t *device, const frame_header_t *rx) {
	if (device->READ_HANDLER == nullptr) {
		return NO_ERROR;
	}
	device->RX_SLAVE_ID = rx->slave;
	const uint8_t verdict = device->READ_HANDLER(device, rx->offset, rx->count);
	return (verdict == EVRE_HANDLED) ? (uint8_t) NO_ERROR : verdict;
}

/* NO_ERROR to go on, with *store cleared when the handler took the write
 * itself (EVRE_HANDLED); else the refusal. */
static uint8_t askWriteHandler(evre_base_t *device, const frame_header_t *rx, const uint8_t *data, bool *store) {
	(*store) = true;
	if (device->WRITE_HANDLER == nullptr) {
		return NO_ERROR;
	}
	device->RX_SLAVE_ID = rx->slave;
	const uint8_t verdict = device->WRITE_HANDLER(device, rx->offset, data, rx->count);
	if (verdict == EVRE_HANDLED) {
		(*store) = false;
		return NO_ERROR;
	}
	return verdict;
}

/* READ: checked, room checked, asked, answered with READ_RESP. The room before
 * the handler: a handler acts only on a read that can be answered (EVRe Guard
 * counts it as the session's activity). */
static uint8_t answerRead(evre_base_t *device, const frame_header_t *rx, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	uint8_t err = checkRead(device, rx->offset, rx->count);
	if (err != NO_ERROR) {
		return err;
	}
	if (!fits(outBuf, outMax, rx->count)) {
		return BUFFER_TOO_SMALL;
	}
	err = askReadHandler(device, rx);
	if (err != NO_ERROR) {
		return err;
	}
	putHeader(outBuf, device->SALVE_ID_REG, READ_RESP, rx->offset, rx->count);
	readBank(device, rx->offset, rx->count, &outBuf[FRAME_DATA]);
	(*outLen) = sealFrame(outBuf, rx->count);
	return NO_ERROR;
}

/* READ_RESP: a host's mirror stores it, a device refuses it (1.1). It stores
 * data, so the write handler decides. It is never answered. */
static uint8_t takeReadResp(evre_base_t *device, const frame_header_t *rx, const uint8_t *data) {
	if (!device->ACCEPT_READ_RESP) {
		return FUNCTION_CODE_ERR;
	}
	bool store = true;
	uint8_t err = checkRead(device, rx->offset, rx->count);
	if (err == NO_ERROR) {
		err = askWriteHandler(device, rx, data, &store);
	}
	if (err == NO_ERROR && store) {
		copyToBank(device, rx->offset, rx->count, data);
	}
	return err;
}

/* WRITE_ACK_RESP: a device refuses it, as it refuses a READ_RESP. A mirror
 * takes it as the news that its write landed. It stores nothing, so only the
 * bank is checked: the mirror's own permissions and queue say nothing about
 * the device's. */
static uint8_t takeWriteAckResp(const evre_base_t *device, const frame_header_t *rx) {
	if (!device->ACCEPT_READ_RESP) {
		return FUNCTION_CODE_ERR;
	}
	const uint16_t bank = bankOf(rx->offset);
	return (bank == RESERVED_BANK || bank == DEVICE_BANK) ? (uint8_t) NO_ERROR : (uint8_t) PERMISSION_DENIED;
}

/* 1.1: a broadcast reaches the reserved bank only, unless the device takes it
 * into its own bank too. Anyone on the link can send one, and every device
 * obeys it at once. */
static bool broadcastRefused(const evre_base_t *device, const frame_header_t *rx) {
	return rx->slave == BROADCAST_ID && !device->ACCEPT_BROADCAST_D000 && bankOf(rx->offset) == DEVICE_BANK;
}

/* WRITE and WRITE_ACK: checked, room checked (a WRITE_ACK), asked, stored,
 * then a WRITE_ACK is answered. The broadcast rule first, before the bank's
 * limits: a refused broadcast is refused whatever else is wrong with it. The
 * room before the handler: a handler acts only on a write that can be
 * acknowledged (a login counts only if the host hears it did), and without
 * room nothing is stored. The answer after the store, so outBuf may be
 * PACKET: the data are read by then. It carries the slave id from before the
 * store: a write to the id itself is acknowledged under the id the host is
 * listening for. */
static uint8_t applyWrite(evre_base_t *device, const frame_header_t *rx, const uint8_t *data, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	if (broadcastRefused(device, rx)) {
		return PERMISSION_DENIED;
	}
	uint8_t err = checkWrite(device, rx->offset, rx->count);
	if (err != NO_ERROR) {
		return err;
	}
	const bool acknowledged = (rx->fn == WRITE_ACK);
	if (acknowledged && !fits(outBuf, outMax, 0)) {
		return BUFFER_TOO_SMALL;
	}
	bool store = true;
	err = askWriteHandler(device, rx, data, &store);
	if (err != NO_ERROR) {
		return err;
	}
	const uint8_t slave = device->SALVE_ID_REG;
	if (store) {
		writeBank(device, rx->offset, rx->count, data);
	}
	if (acknowledged) {
		putHeader(outBuf, slave, WRITE_ACK_RESP, rx->offset, rx->count);
		(*outLen) = sealFrame(outBuf, 0);
	}
	return NO_ERROR;
}

/* The decoder proper. decodePacketInto() below wraps it and turns a refusal
 * into an ERROR_RESP frame when *answerRefusal says so. The order of the
 * checks decides which code a frame that is wrong in two ways gets: it is
 * part of the protocol. */
static uint8_t decodeCore(evre_base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t *outBuf, uint16_t outMax,
		uint16_t *outLen, bool *answerRefusal) {
	if (outLen != nullptr) {
		(*outLen) = 0;
	}
	(*answerRefusal) = false;
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}
	if (!wellFormed(PACKET, pSize)) {
		return INVALID_PACKET_ERR;
	}
	const frame_header_t rx = readHeader(PACKET);
	const bool broadcast = (rx.slave == BROADCAST_ID);
	if (!broadcast && rx.slave != device->SALVE_ID_REG) {
		return SLAVE_ID_MISMATCHED; /* never ours to answer */
	}
	/* The frame is ours. From here a refusal is answered, whatever its code,
	 * unless every slave heard the frame or it is itself an answer: two devices
	 * trading ERROR_RESP frames would never stop. */
	(*answerRefusal) = !broadcast && !isResponse(rx.fn);

	/* Every slave hears a broadcast, so nothing may answer one: only the
	 * function that produces no response is allowed. */
	if (broadcast && rx.fn != WRITE) {
		return FUNCTION_CODE_ERR;
	}
	/* Before the length, so a code the library does not decode is 2 whatever
	 * its size. ERROR_RESP is one of them: a host reads its code byte itself. */
	if (!isRequest(rx.fn) && rx.fn != READ_RESP && rx.fn != WRITE_ACK_RESP) {
		return FUNCTION_CODE_ERR;
	}
	/* Without this a short frame claiming a large count would make the copies
	 * below read past the end of PACKET into live registers. */
	if (pSize != expectedSize(&rx)) {
		return LENGTH_MISMATCH;
	}

	const uint8_t *data = &PACKET[FRAME_DATA];
	uint8_t err;
	switch (rx.fn) {
		case READ:
			err = answerRead(device, &rx, outBuf, outMax, outLen);
			break;
		case WRITE:
		case WRITE_ACK:
			err = applyWrite(device, &rx, data, outBuf, outMax, outLen);
			break;
		case READ_RESP:
			err = takeReadResp(device, &rx, data);
			break;
		default:
			err = takeWriteAckResp(device, &rx);
			break;
	}
	/* Only for a request: a mirror's CONFIG keeps the value the device
	 * reported. And last, after any answer is built: a READ of CONFIG reports
	 * it as it was before this frame, and a write to CONFIG cannot clear it. */
	if (err == NO_ERROR && isRequest(rx.fn)) {
		setBit(device->CONFIG, HEARTBEAT);
	}
	return err;
}

/* ERROR_RESP: echoes the offset and count of the request and carries one
 * ERR_CODE_ENUM byte, so the host can tie the failure to the exact request. */
static void buildErrorFrame(uint8_t slaveId, uint16_t regOffset, uint16_t regCount,
		uint8_t errCode, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	if (!fits(outBuf, outMax, 1)) {
		(*outLen) = 0;
		return;
	}
	putHeader(outBuf, slaveId, ERROR_RESP, regOffset, regCount);
	outBuf[FRAME_DATA] = errCode;
	(*outLen) = sealFrame(outBuf, 1);
}

/* ============================================================= public API */

#if EVRE_CRC_TABLE_RUNTIME
uint16_t crctab16[256];

/* Set once the table is complete. A plain flag: protocolInit() builds the
 * table at start-up, before any interrupt decodes, so only a program that
 * computes a CRC before protocolInit() builds it here; two first users would
 * write the same values. */
static bool crcTableBuilt = false;

/* CRC-16/X-25 table, reflected polynomial 0x8408. Idempotent. */
void EVRe_CrcTableInit(void) {
	for (unsigned i = 0; i < 256; ++i) {
		uint16_t v = (uint16_t) i;
		for (unsigned b = 0; b < 8; ++b) {
			v = (v & 1U) ? (uint16_t) ((v >> 1) ^ 0x8408U) : (uint16_t) (v >> 1);
		}
		crctab16[i] = v;
	}
	crcTableBuilt = true;
}
#else
/* Here once, not in the header: a static table there was a copy in every file
 * that includes it. */
const uint16_t crctab16[256] = { 0X0000, 0X1189, 0X2312, 0X329B, 0X4624, 0X57AD, 0X6536, 0X74BF, 0X8C48, 0X9DC1, 0XAF5A, 0XBED3, 0XCA6C, 0XDBE5, 0XE97E, 0XF8F7, 0X1081, 0X0108, 0X3393, 0X221A, 0X56A5, 0X472C, 0X75B7, 0X643E, 0X9CC9, 0X8D40, 0XBFDB, 0XAE52, 0XDAED, 0XCB64, 0XF9FF, 0XE876, 0X2102, 0X308B, 0X0210, 0X1399, 0X6726, 0X76AF, 0X4434, 0X55BD, 0XAD4A, 0XBCC3, 0X8E58, 0X9FD1, 0XEB6E, 0XFAE7, 0XC87C, 0XD9F5, 0X3183, 0X200A, 0X1291, 0X0318, 0X77A7, 0X662E, 0X54B5, 0X453C, 0XBDCB, 0XAC42, 0X9ED9,
		0X8F50, 0XFBEF, 0XEA66, 0XD8FD, 0XC974, 0X4204, 0X538D, 0X6116, 0X709F, 0X0420, 0X15A9, 0X2732, 0X36BB, 0XCE4C, 0XDFC5, 0XED5E, 0XFCD7, 0X8868, 0X99E1, 0XAB7A, 0XBAF3, 0X5285, 0X430C, 0X7197, 0X601E, 0X14A1, 0X0528, 0X37B3, 0X263A, 0XDECD, 0XCF44, 0XFDDF, 0XEC56, 0X98E9, 0X8960, 0XBBFB, 0XAA72, 0X6306, 0X728F, 0X4014, 0X519D, 0X2522, 0X34AB, 0X0630, 0X17B9, 0XEF4E, 0XFEC7, 0XCC5C, 0XDDD5, 0XA96A, 0XB8E3, 0X8A78, 0X9BF1, 0X7387, 0X620E, 0X5095, 0X411C, 0X35A3, 0X242A, 0X16B1, 0X0738, 0XFFCF, 0XEE46,
		0XDCDD, 0XCD54, 0XB9EB, 0XA862, 0X9AF9, 0X8B70, 0X8408, 0X9581, 0XA71A, 0XB693, 0XC22C, 0XD3A5, 0XE13E, 0XF0B7, 0X0840, 0X19C9, 0X2B52, 0X3ADB, 0X4E64, 0X5FED, 0X6D76, 0X7CFF, 0X9489, 0X8500, 0XB79B, 0XA612, 0XD2AD, 0XC324, 0XF1BF, 0XE036, 0X18C1, 0X0948, 0X3BD3, 0X2A5A, 0X5EE5, 0X4F6C, 0X7DF7, 0X6C7E, 0XA50A, 0XB483, 0X8618, 0X9791, 0XE32E, 0XF2A7, 0XC03C, 0XD1B5, 0X2942, 0X38CB, 0X0A50, 0X1BD9, 0X6F66, 0X7EEF, 0X4C74, 0X5DFD, 0XB58B, 0XA402, 0X9699, 0X8710, 0XF3AF, 0XE226, 0XD0BD, 0XC134, 0X39C3,
		0X284A, 0X1AD1, 0X0B58, 0X7FE7, 0X6E6E, 0X5CF5, 0X4D7C, 0XC60C, 0XD785, 0XE51E, 0XF497, 0X8028, 0X91A1, 0XA33A, 0XB2B3, 0X4A44, 0X5BCD, 0X6956, 0X78DF, 0X0C60, 0X1DE9, 0X2F72, 0X3EFB, 0XD68D, 0XC704, 0XF59F, 0XE416, 0X90A9, 0X8120, 0XB3BB, 0XA232, 0X5AC5, 0X4B4C, 0X79D7, 0X685E, 0X1CE1, 0X0D68, 0X3FF3, 0X2E7A, 0XE70E, 0XF687, 0XC41C, 0XD595, 0XA12A, 0XB0A3, 0X8238, 0X93B1, 0X6B46, 0X7ACF, 0X4854, 0X59DD, 0X2D62, 0X3CEB, 0X0E70, 0X1FF9, 0XF78F, 0XE606, 0XD49D, 0XC514, 0XB1AB, 0XA022, 0X92B9, 0X8330,
		0X7BC7, 0X6A4E, 0X58D5, 0X495C, 0X3DE3, 0X2C6A, 0X1EF1, 0X0F78 };
#endif

/* CRC-16/X-25 of the first nLength bytes: a frame's CRC covers all of it but
 * the last three bytes (the CRC itself and the end byte). */
uint16_t GetCrc16(const uint8_t *pData, int nLength) {
#if EVRE_CRC_TABLE_RUNTIME
	if (!crcTableBuilt) {
		EVRe_CrcTableInit(); /* a CRC before protocolInit() */
	}
#endif
	uint16_t fcs = 0xffff; // initialization
	while (nLength > 0) {
		fcs = (fcs >> 8) ^ crctab16[(fcs ^ *pData) & 0xff];
		nLength--;
		pData++;
	}
	return ~fcs; // negated
}

uint8_t decodePacketInto(evre_base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	if (outLen != nullptr) {
		(*outLen) = 0; /* before any refusal: a caller that sends when it is not 0 never repeats an old answer */
	}
	bool answerRefusal = false;
	const uint8_t err = decodeCore(device, PACKET, pSize, outBuf, outMax, outLen, &answerRefusal);
	/* PACKET still holds the request, even in place: a refused frame builds no
	 * answer and stores nothing. */
	if (err != NO_ERROR && answerRefusal) {
		buildErrorFrame(device->SALVE_ID_REG, getLE16(&PACKET[OFFSET0]), getLE16(&PACKET[REG_CNT0]), err,
				outBuf, outMax, outLen);
	}
	return err;
}

/* The device's own frames: a request, or a READ_RESP / WRITE of its own
 * registers (pData nullptr), which AUTO_SEND and streaming use. The handlers
 * are not asked: the device decides what it sends. */
uint8_t encodePacketInto(evre_base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	if (outLen != nullptr) {
		(*outLen) = 0;
	}
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}
	/* Every slave would answer at once, so every device refuses it: not built. */
	if (slaveId == BROADCAST_ID && fnCode != WRITE) {
		return FUNCTION_CODE_ERR;
	}

	uint8_t err;
	switch (fnCode) {
		case READ:
		case READ_RESP:
			err = checkRead(device, regOffset, regCount);
			break;
		case WRITE:
		case WRITE_ACK:
			err = checkWrite(device, regOffset, regCount);
			break;
		default:
			return FUNCTION_CODE_ERR; /* the answers are decodePacket's to build */
	}
	if (err != NO_ERROR) {
		return err;
	}
	const uint16_t dataLen = (fnCode == READ) ? 0 : regCount; /* a READ asks for count bytes, it carries none */
	if (!fits(outBuf, outMax, dataLen)) {
		return BUFFER_TOO_SMALL;
	}
	putHeader(outBuf, slaveId, fnCode, regOffset, regCount);
	if (pData != nullptr) {
		memmove(&outBuf[FRAME_DATA], pData, dataLen); /* pData may already lie in outBuf */
	} else {
		readBank(device, regOffset, dataLen, &outBuf[FRAME_DATA]);
	}
	(*outLen) = sealFrame(outBuf, dataLen);
	return NO_ERROR;
}

/* ---------------------------------------------------------------------- */
/* Allocating forms, kept for compatibility: work out how big the response
 * can be, allocate once, and hand the buffer to the *Into form above.      */
/* ---------------------------------------------------------------------- */

/* 11 bytes hold every answer but a READ_RESP. Only a READ that is ours gets
 * more, and never more than a bank: line noise, or a READ for another slave,
 * costs 11 bytes of heap, not 64 KB. The cheap tests before the CRC: a READ is
 * 10 bytes, so the CRC is never computed twice for a large frame. */
static uint32_t answerSize(const evre_base_t *device, const uint8_t *PACKET, uint16_t pSize) {
	const uint32_t errorResp = PACKET_BASE_SIZE + 1U;
	if (device == nullptr || PACKET == nullptr || pSize != PACKET_BASE_SIZE || PACKET[FN_CODE] != READ
			|| PACKET[SLAVE_ID] != device->SALVE_ID_REG || !wellFormed(PACKET, pSize)) {
		return errorResp;
	}
	const uint32_t count = getLE16(&PACKET[REG_CNT0]);
	const uint32_t want = PACKET_BASE_SIZE + ((count < BANK_SIZE) ? count : BANK_SIZE);
	return (want > errorResp) ? want : errorResp;
}

uint8_t decodePacket(evre_base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t **RESPONSE, uint16_t *rSize) {
	(*RESPONSE) = nullptr;
	(*rSize) = 0;

	uint32_t want = answerSize(device, PACKET, pSize);
	uint8_t *buf = (uint8_t*) calloc(want, sizeof(uint8_t));
	if (buf == nullptr && want > PACKET_BASE_SIZE + 1U) {
		/* No room for the READ_RESP: an ERROR_RESP still tells the host why. */
		want = PACKET_BASE_SIZE + 1U;
		buf = (uint8_t*) calloc(want, sizeof(uint8_t));
	}
	if (buf == nullptr) {
		return MEM_ALLOCATION_FAILED;
	}

	uint16_t len = 0;
	const uint8_t err = decodePacketInto(device, PACKET, pSize, buf, (uint16_t) want, &len);

	if (len == 0U) {
		free(buf); /* nothing to answer */
	} else {
		(*RESPONSE) = buf;
		(*rSize) = len;
	}
	return err;
}

uint8_t encodePacket(evre_base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t **PACKET, uint16_t *pSize) {
	(*PACKET) = nullptr;
	(*pSize) = 0;

	uint32_t want = (fnCode == READ) ? (uint32_t) PACKET_BASE_SIZE
			: (PACKET_BASE_SIZE + (uint32_t) regCount);
	if (want > 0xFFFFU) {
		want = 0xFFFFU;
	}

	uint8_t *buf = (uint8_t*) calloc(want, sizeof(uint8_t));
	if (buf == nullptr) {
		return MEM_ALLOCATION_FAILED;
	}

	uint16_t len = 0;
	const uint8_t err = encodePacketInto(device, slaveId, fnCode, regOffset, regCount, pData,
			buf, (uint16_t) want, &len);

	if (len == 0U) {
		free(buf);
	} else {
		(*PACKET) = buf;
		(*pSize) = len;
	}
	return err;
}

/* Two steps, the code then the count: an ack decoded between them would lose
 * or double a message, hence the lock (EVRE_LOCK in EVRe.h). */
uint8_t addMsg(evre_base_t *device, uint8_t MSG) {
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}
	if (MSG == 0) {
		return MSG_NULL; /* 0 marks an acknowledged slot */
	}
	uint8_t err = MSG_BUFFER_FULL;
	EVRE_LOCK();
	if (device->MSG_CNT < MSG_BUFFER_SIZE) {
		device->MSG_BUFFER[device->MSG_CNT] = MSG;
		++(device->MSG_CNT);
		err = NO_ERROR;
	}
	EVRE_UNLOCK();
	return err;
}

/* The order is a contract, see EVRe.h. */
uint8_t protocolInit(evre_base_t *device) {
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}

#if EVRE_CRC_TABLE_RUNTIME
	EVRe_CrcTableInit();
#endif

	/* 1.1: the reserved bank needs no table (see reservedAt) */
	device->A000 = nullptr;

	for (uint16_t ind = 0; ind < MSG_ACK_HANDLER_CNT; ++ind) {
		device->MSG_ACK_HANDLER[ind] = nullptr;
	}

	/* What this library provides. protocolConfigure() adds the bits that are
	 * the device's own (AUTO_SEND, DFU, ...). CAP_BROADCAST_D000 is not set
	 * here: it follows a setting protocolConfigure may still change, so it is
	 * set as a device's STATUS goes out (refreshStatus). */
	device->STATUS = (uint16_t) (EVRE_PROTOCOL_VERSION | CAP_ERROR_FRAME
			| CAP_BROADCAST | CAP_MSG | CAP_STATIC);

	const uint8_t configured = protocolConfigure(device);
	/* Whatever protocolConfigure returned: a device that ignores the code
	 * still decodes, and the library relies on a checked table. */
	uint8_t table = NO_ERROR;
	if (device->D_RANGES != nullptr) {
		table = validRanges(device);
		if (table != NO_ERROR) {
			device->D_RANGE_CNT = 0; /* fail closed: serves nothing */
		}
	}
	return (configured != NO_ERROR) ? configured : table;
}

/* Weak here and only here: see protocolConfigure() in EVRe.h. */
EVRE_WEAK uint8_t protocolConfigure(evre_base_t *device) {
	(void) device;
	return NO_ERROR;
}

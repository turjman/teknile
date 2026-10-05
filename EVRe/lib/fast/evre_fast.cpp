/* SPDX-License-Identifier: Apache-2.0 */
/*
 * evre_fast.cpp: Fast EVRe, the device's side (see evre_fast.h).
 * NOT PART OF THE PROTOCOL: it only builds READ_RESP frames with the
 * library's public names.
 */
#include "evre_fast.h"

#include "../EVRe.h"

/* The device bank: a window lies inside it, so a host's mirror and every
 * existing host take a block for an answer nobody asked for. */
#define EVRE_FAST_BANK_FIRST (0xD000UL)
#define EVRE_FAST_BANK_LAST  (0xDFFFUL)

/* The fields are written byte by byte: little endian on any CPU. */
static void put16(uint8_t *at, uint32_t value) {
	at[0] = (uint8_t)(value & 0xFFU);
	at[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void put32(uint8_t *at, uint32_t value) {
	put16(at, value & 0xFFFFUL);
	put16(at + 2, value >> 16);
}

uint8_t evre_fast_init(evre_fast_t *stream, const evre_fast_config_t *cfg) {
	if (stream == nullptr) {
		return INSTANCE_IS_NULL;
	}
	stream->cfg = nullptr;
	stream->next = 0;
	stream->pending = EVRE_FAST_FLAG_START;
	if (cfg == nullptr) {
		return INSTANCE_IS_NULL;
	}
	/* sums in 32 bits: a 16-bit int would overflow at the bank's end */
	const uint32_t first = cfg->addr;
	const uint32_t last = first + (uint32_t)cfg->size - 1UL;
	if (cfg->record == 0U || cfg->size < EVRE_FAST_HEADER + (uint32_t)cfg->record
			|| first < EVRE_FAST_BANK_FIRST || last > EVRE_FAST_BANK_LAST) {
		return PERMISSION_DENIED;
	}
	stream->cfg = cfg;
	return NO_ERROR;
}

void evre_fast_start(evre_fast_t *stream) {
	if (stream == nullptr) {
		return;
	}
	stream->next = 0;
	stream->pending = EVRE_FAST_FLAG_START;
}

/* The numbers go on past the dropped records: the host sees the gap in
 * `first` and counts it; LOST only tells a person who reads a dump. */
void evre_fast_lost(evre_fast_t *stream, uint32_t records) {
	if (stream == nullptr || records == 0UL) {
		return;
	}
	stream->next += records; /* wraps at 2^32, as the host expects */
	stream->pending = (uint8_t)(stream->pending | EVRE_FAST_FLAG_LOST);
}

uint16_t evre_fast_room(const evre_fast_t *stream) {
	if (stream == nullptr || stream->cfg == nullptr) {
		return 0U;
	}
	return (uint16_t)(((uint32_t)stream->cfg->size - EVRE_FAST_HEADER) / stream->cfg->record);
}

uint16_t evre_fast_frame(evre_fast_t *stream, uint8_t slave, uint8_t *buf, uint16_t n) {
	if (stream == nullptr || buf == nullptr || slave == BROADCAST_ID || stream->cfg == nullptr
			|| n > evre_fast_room(stream)) {
		return 0U;
	}
	const uint32_t data = EVRE_FAST_HEADER + (uint32_t)n * stream->cfg->record; /* the frame's count */
	const uint32_t body = EVRE_FAST_BEFORE + (uint32_t)n * stream->cfg->record; /* the bytes the CRC covers */
	const uint16_t length = (uint16_t)(body + EVRE_FAST_AFTER);

	buf[START] = 0x7B;
	buf[SLAVE_ID] = slave;
	buf[FN_CODE] = READ_RESP;
	buf[OFFSET0] = (uint8_t)(stream->cfg->addr & 0xFFU);
	buf[OFFSET1] = (uint8_t)(stream->cfg->addr >> 8);
	buf[REG_CNT0] = (uint8_t)(data & 0xFFU);
	buf[REG_CNT1] = (uint8_t)(data >> 8);

	uint8_t *block = buf + (EVRE_FAST_BEFORE - EVRE_FAST_HEADER);
	put32(block, stream->next);
	put16(block + 4, n);
	block[6] = stream->pending;
	block[7] = 0U;

	const uint16_t crc = GetCrc16(buf, (int)body);
	buf[CRC0(length)] = (uint8_t)(crc & 0xFFU);
	buf[CRC1(length)] = (uint8_t)(crc >> 8);
	buf[END(length)] = 0x7D;

	stream->next += n;
	stream->pending = 0U;
	return length;
}

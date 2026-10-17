/* SPDX-License-Identifier: Apache-2.0 */
/*
 * evre_fast.h: Fast EVRe, the device's side. NOT PART OF THE PROTOCOL.
 *
 * A device that takes samples on its own clock sends them in numbered blocks:
 * each block is a READ_RESP at the first address of a window of the device
 * bank, sent without being asked (PROTOCOL.md, "Fast EVRe"). The protocol
 * moves the bytes and never asks what they mean; this helper only puts the
 * frame around the records, where they lie, so nothing is copied:
 *
 *   | 7 B frame header | 8 B block header | n records ...       | CRC, end |
 *   ^                                     ^                     ^
 *   0                                     EVRE_FAST_BEFORE      EVRE_FAST_BEFORE + n x record
 *
 * The block header, little endian: first (u32), the number of the block's
 * first record, counted from the stream's start, dropped records included;
 * count (u16), the records in the block; flags (u8), START and LOST; spare
 * (u8), 0. These 8 bytes, the two flag bits and the members of
 * evre_fast_config_t keep their places for good: a later need goes into the
 * spare byte and the free flag bits.
 *
 * It uses the library's public names only (READ_RESP, the frame's index
 * macros, GetCrc16), so it builds with library 1.0 and 1.1 alike, and the
 * window needs no memory behind it and no entry in the device's map.
 *
 * Rules for the device:
 * - One context writes an evre_fast_t: the one that builds the blocks (the
 *   "buffer full" interrupt, say). A device that calls evre_fast_start() or
 *   evre_fast_lost() from another context masks the builder's interrupt around
 *   the call, or the numbers of a block can tear.
 * - A frame goes out whole. Blocks and the decoder's answers share the one
 *   transmitter, so they share one queue: no frame may start inside another,
 *   or both are lost.
 * - The device stops its streams when the host writes the enable register 0,
 *   when its host watchdog runs out, and at a reset. With a login (EVRe
 *   Guard) it sends no block while no session is open: the handlers are never
 *   asked about the device's own frames, so only the device can hold them.
 *
 * No heap, no state but the evre_fast_t, C++11.
 */
#ifndef EVRE_FAST_H
#define EVRE_FAST_H

#include <stdint.h>

#define EVRE_FAST_BEFORE (15U) /* the frame's 7 bytes and the block's 8, before the first record */
#define EVRE_FAST_AFTER  (3U)  /* the CRC and the end byte, after the last */
#define EVRE_FAST_HEADER (8U)  /* the block's header: first, count, flags, spare */

/* The block's flags. A host takes a block with any other bit set for a newer
 * kind and uses none of its records. */
#define EVRE_FAST_FLAG_START (0x01U) /* the first block since the stream started: its first record is 0 */
#define EVRE_FAST_FLAG_LOST  (0x02U) /* records were dropped just before this block */

/* The window's span in the device bank, and its records. */
typedef struct {
	uint16_t addr;   /* the window's first address: 0xD000 or above */
	uint16_t size;   /* the window's bytes: no block is larger, its header included */
	uint16_t record; /* bytes of one record: all channels of one instant, packed */
} evre_fast_config_t;

/* One stream's running state. */
typedef struct {
	const evre_fast_config_t *cfg; /* nullptr until a good init: no frame is built */
	uint32_t next;                 /* the number of the next record */
	uint8_t pending;               /* flags for the next block: START, LOST */
} evre_fast_t;

/* NO_ERROR, or PERMISSION_DENIED for a window outside 0xD000..0xDFFF or one
 * too small for the header and one record (INSTANCE_IS_NULL for a null
 * pointer). After a refusal the stream builds no frame. */
uint8_t evre_fast_init(evre_fast_t *stream, const evre_fast_config_t *cfg);

/* The stream starts: the next record is number 0, the next block says START. */
void evre_fast_start(evre_fast_t *stream);

/* The device had to drop records: they keep their numbers, the next block says LOST. */
void evre_fast_lost(evre_fast_t *stream, uint32_t records);

/* The most records one block takes; 0 before a good init. */
uint16_t evre_fast_room(const evre_fast_t *stream);

/* buf holds n records at EVRE_FAST_BEFORE, with EVRE_FAST_AFTER bytes free behind
 * them. Writes the headers and the CRC around them and returns the frame's
 * length; 0, and nothing written, when n does not fit the window, before a
 * good init, or for slave 0 (the broadcast address, which no device sends from). */
uint16_t evre_fast_frame(evre_fast_t *stream, uint8_t slave, uint8_t *buf, uint16_t n);

#endif

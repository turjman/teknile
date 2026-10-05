/* SPDX-License-Identifier: Apache-2.0 */
/* The registers the EVRe protocol itself defines, the same on every device:
 * the reserved bank at 0xA000 and where the device's own registers begin.
 * One place for them, so the master, the engine, the bus rule, the API, the
 * command line and the fakes cannot drift apart. The values are the EVRe
 * library's (EVRe.h: RESERVED_MAP_ENUM, STATUS_ENUM, CFG_ENUM); the
 * meaning of a device's own registers is its map's, never this file's.
 *
 *   0xA000  DEVICE_ID  u16  read-only
 *   0xA002  STATUS     u16  read-only: protocol revision (7..0), capabilities (15..8)
 *   0xA004  CONFIG     u16  HEARTBEAT, SYS_RESET, MSG_ENABLE, AUTO_SEND, DFU (bits 0..4),
 *                           the AUTO_SEND prescaler (bits 15..8)
 *   0xA006  MSG_CNT, then MSG_BUFFER, up to 0xA105
 *   0xD000  the device bank: its read-only block first (what AUTO_SEND sends)
 */
#pragma once

#include <cstdint>

namespace evre {

/* the reserved bank */
constexpr uint16_t DEVICE_ID = 0xA000;
constexpr uint16_t STATUS = 0xA002;
constexpr uint16_t CONFIG = 0xA004;

/* the reserved bank's extent: [RESERVED_FIRST, RESERVED_END), writable from RESERVED_WRITABLE (DEVICE_ID and STATUS
 * are read-only) */
constexpr uint16_t RESERVED_FIRST = 0xA000;
constexpr uint16_t RESERVED_WRITABLE = 0xA004;
constexpr uint16_t RESERVED_END = 0xA106;

/* STATUS: the protocol revision, and what the device implements */
constexpr uint16_t STATUS_REVISION_MASK = 0x00FF;
constexpr uint16_t CAP_ERROR_FRAME = 0x0100;    /* answers a refused request with ERROR_RESP */
constexpr uint16_t CAP_BROADCAST = 0x0200;      /* takes a WRITE to slave 0 */
constexpr uint16_t CAP_MSG = 0x0400;            /* MSG_CNT and MSG_BUFFER */
constexpr uint16_t CAP_AUTO_SEND = 0x0800;      /* CONFIG's AUTO_SEND bit */
constexpr uint16_t CAP_DFU = 0x1000;            /* CONFIG's DFU bit */
constexpr uint16_t CAP_STATIC = 0x2000;         /* the library built without the heap */
constexpr uint16_t CAP_BROADCAST_D000 = 0x4000; /* EVRe 1.1 (draft): takes a broadcast into the device bank too */

/* CONFIG's bits */
constexpr uint16_t CONFIG_HEARTBEAT = 0x0001;   /* the device sets it on every request it takes */
constexpr uint16_t CONFIG_SYS_RESET = 0x0002;
constexpr uint16_t CONFIG_MSG_ENABLE = 0x0004;
constexpr uint16_t CONFIG_AUTO_SEND = 0x0008;   /* the device sends its read-only block by itself */
constexpr uint16_t CONFIG_DFU = 0x0010;
constexpr int CONFIG_PRESCALER_SHIFT = 8;       /* the AUTO_SEND prescaler: bits 15..8 */
constexpr uint16_t CONFIG_PRESCALER_MASK = 0xFF00;

/* the device bank starts with its read-only block: AUTO_SEND sends it from here, at
 * AUTO_SEND_BASE_HZ / (prescaler + 1) frames a second */
constexpr uint16_t READ_ONLY_BLOCK = 0xD000;
constexpr int AUTO_SEND_BASE_HZ = 8000;
/* the prescaler is 1..255: it is the device timer's reload, and a reload of 0 stops the timer, so a device replaces
 * a 0 (by 1, 4000 Hz, or by its default 0x4F, 100 Hz); the rates the protocol names are 4000 Hz (1) down to 40 Hz
 * (199). A host never writes 0. */
constexpr int AUTO_SEND_PRESCALER_MIN = 1;
constexpr int AUTO_SEND_PRESCALER_DEFAULT = 0x4F; /* 100 Hz */
constexpr int AUTO_SEND_MIN_HZ = 40;

} // namespace evre

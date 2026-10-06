/* SPDX-License-Identifier: Apache-2.0 */
/*
 * EVRe.h
 *
 *  Created on: Aug 5, 2024
 *      Author: Ahmed Ragab AbdulGhany
 */

#ifndef EVRE_H
#define EVRE_H

#include <stdint.h>
#include <stdbool.h>
#include <cstdlib>

#define PACKET_BASE_SIZE  (0x0AU)

/* This implementation's protocol revision, reported in STATUS[7:0]. */
#define EVRE_PROTOCOL_VERSION (0x01U)

/* This library's own version, apart from the protocol revision. 1.1 adds the
 * range table (D_RANGES), the read and write handlers, ACCEPT_READ_RESP,
 * ACCEPT_BROADCAST_D000 and RX_SLAVE_ID; serves the reserved bank without a
 * pointer table; refuses writes to an unknown bank; takes a broadcast into the
 * reserved bank only, by default, and says in STATUS whether the device bank
 * takes one (CAP_BROADCAST_D000). */
#define EVRE_LIB_VERSION (0x0101U)

/* ================================================== MIGRATING FROM 1.0
 *
 * A HOST THAT KEEPS A MIRROR of a device (an evre_base_t it decodes the
 * device's answers into) MUST SET ACCEPT_READ_RESP = 1 on it. Without it every
 * READ_RESP is refused (FUNCTION_CODE_ERR) and nothing is stored: the mirror
 * never learns anything. 0 stays the default on purpose: a device must not let
 * a host write its read-only registers with a READ_RESP. Code that builds with
 * both versions:
 *
 *   #if defined(EVRE_LIB_VERSION) && EVRE_LIB_VERSION >= 0x0101
 *       mirror.ACCEPT_READ_RESP = 1;
 *   #endif
 *
 * A DEVICE THAT TAKES BROADCAST WRITES INTO ITS DEVICE BANK (a set point every
 * device takes at once, one stop command for all) MUST SET
 * ACCEPT_BROADCAST_D000 = 1. Without it a broadcast reaches the reserved bank
 * only: one to 0xD000..0xDFFF is refused, silently, and nothing is stored. 0
 * stays the default on purpose: whoever is on the link cannot write every
 * device at once. The same #if line as above builds with both versions. A
 * host tells the two apart by STATUS: CAP_BROADCAST_D000 is set exactly when
 * the device bank takes a broadcast.
 *
 * One 1.0 name is gone: the struct tag. `struct protocol_base` is
 * `struct evre_base` now, so code that writes `struct protocol_base` (or
 * declares it ahead) no longer compiles: write base_t. protocol_base alone,
 * without `struct`, still names the type (a typedef below).
 *
 * Also new in 1.1, for code that relied on the old behaviour:
 * - MSG_ACK_HANDLER has 256 slots (0xFF has one now): rebuild every object
 *   that uses evre_base_t.
 * - The frame index macros (DATAx, CRC0, CRC1, END) and the bit macros are
 *   parenthesised: inside a larger expression they mean what they say.
 *   DATAx(0) + 1 was 7 in 1.0 (the + 1 went into the ?:), it is 8 now.
 * - A device refuses an incoming WRITE_ACK_RESP as it refuses a READ_RESP.
 * - CONFIG.HEARTBEAT is set only for an accepted READ, WRITE or WRITE_ACK.
 * - A frame with a wrong start or end byte is dropped like a bad CRC.
 * - An unknown function code is FUNCTION_CODE_ERR whatever its length.
 * - A handler's refusal is answered with its code, 1 and 7 included.
 * - A write to an unknown bank is refused (PERMISSION_DENIED).
 * - A write that clears the queue (MSG_CNT) acknowledges nothing: its bytes
 *   past MSG_CNT do nothing, so it is the clear alone, and a message that
 *   MSG_ACK_HANDLER[0] queues is kept. 1.0 went on and acknowledged the slots.
 * - After an ack or a clear, MSG_BUFFER past MSG_CNT reads 0.
 * - STATUS bit 14 is the library's (CAP_BROADCAST_D000): it goes out as
 *   ACCEPT_BROADCAST_D000 says, whatever the device wrote there. Not on a
 *   mirror (ACCEPT_READ_RESP 1): it keeps and serves the bit its device
 *   reported.
 * - outLen is 0 after every call that builds nothing.
 * - The encoder builds no broadcast READ, WRITE_ACK or READ_RESP
 *   (FUNCTION_CODE_ERR): no device would take it.
 * - decodePacket allocates 11 bytes, more only for a READ that is ours (one
 *   bank at most), and falls back to 11 when the heap cannot give more.
 *
 * The full list: docs/PROTOCOL.md, Migrating from 1.0. */

/* A WRITE_HANDLER's answer for a write it has taken care of itself: the frame
 * is accepted (a WRITE_ACK is answered) and the library stores nothing. From a
 * READ_HANDLER it counts as NO_ERROR: the read is answered. */
#define EVRE_HANDLED (0xFFU)

/* Slave 0 addresses every slave on the link. No slave answers it, so only
 * WRITE is accepted; READ and WRITE_ACK would make every slave transmit at
 * once. 0 must never be used as a real device address. 1.1: a broadcast
 * reaches the reserved bank only, unless the device sets
 * ACCEPT_BROADCAST_D000. */
#define BROADCAST_ID      (0x00U)

/* Byte positions in a frame. A frame of pLEN bytes ends with CRC0, CRC1 and
 * END; the data, if any, starts at DATAx(0). The macros are safe in any
 * expression, but they evaluate their argument twice. */
#define START   		  (0x00U)

#define SLAVE_ID   		  (0x01U)

#define FN_CODE 		  (0x02U)

#define OFFSET0 		  (0x03U)
#define OFFSET1 		  (0x04U)

#define REG_CNT0 		  (0x05U)
#define REG_CNT1 		  (0x06U)

#define DATAx(ind)  	  ((((ind) + 0x07) > 0) ? ((ind) + 0x07) : 0) /* Index is zero based */

#define CRC0(pLEN)  	  ((((pLEN) - 0x03) > 0) ? ((pLEN) - 0x03) : 0) /* (LSB) */
#define CRC1(pLEN)   	  ((((pLEN) - 0x02) > 0) ? ((pLEN) - 0x02) : 0) /* (MSB) */
#define END(pLEN)  		  ((((pLEN) - 0x01) > 0) ? ((pLEN) - 0x01) : 0)

/* The int literal stays in the three that write: a wider VAL keeps its upper
 * bits. getBit shifts VAL itself, so it works for bit 31 and for 64-bit values,
 * and its result has the type it had. */
#define setBit(VAL, BIT) ((VAL) |= (1 << (BIT)))
#define clearBit(VAL, BIT) ((VAL) &= ~(1 << (BIT)))
#define toggleBit(VAL, BIT) ((VAL) ^= (1 << (BIT)))
#define getBit(VAL, BIT) (((VAL) >> (BIT)) & 1)

#define getByteAddress(VAL, BYTE) (((uint8_t *) (VAL)) + (BYTE))

/* How EVRe.cpp marks its default protocolConfigure() weak. This is the GCC and
 * Clang spelling; another compiler defines EVRE_WEAK before it includes this
 * file (IAR and Keil: __weak). */
#ifndef EVRE_WEAK
#define EVRE_WEAK __attribute__((weak))
#endif

/* Optional, empty by default. For a device that calls addMsg() from another
 * context than the decoder, one the decoder preempts, such as its main loop
 * while decodePacket runs in an interrupt: an ack could land between addMsg's
 * two steps, and a message would be lost or queued twice. The library takes
 * the lock around addMsg's body and around the decoder's clear and compaction
 * of the queue. It never nests the two and never calls a handler, or any other
 * code, in between, so a lock that saves the interrupt mask in a local
 * variable works. The longest hold is the compaction of a full queue: a loop
 * over up to 255 slots and a memset of up to 255 bytes, a few thousand cycles
 * (a few microseconds on a Cortex-M7 at 480 MHz, tens of microseconds on a
 * Cortex-M0 at 48 MHz). EVRe.cpp must see the two macros, so they are defined
 * for the whole build: on the command line, or in a header given to the
 * compiler with -include. With CMSIS on a Cortex-M, in that header (after the
 * device's CMSIS header):
 *
 *   #define EVRE_LOCK()   uint32_t evre_primask = __get_PRIMASK(); __disable_irq()
 *   #define EVRE_UNLOCK() __set_PRIMASK(evre_primask)
 *
 * The device's own writes to STATUS, CONFIG and MSG_* need the same care. A
 * lock that masks every interrupt, as the lines above do, also covers an
 * addMsg() from an interrupt that preempts the decoder: outside the lock such
 * a message lands above every slot the frame being decoded can acknowledge,
 * since a write that clears the queue acknowledges nothing. A lock that masks
 * less covers only the contexts it masks. */
#ifndef EVRE_LOCK
#define EVRE_LOCK()
#define EVRE_UNLOCK()
#endif

/* A run of the device bank served from one block of memory: the bytes
 * start .. start + len - 1 are base[0 .. len - 1]. The library only moves the
 * bytes; what they mean is the device's business (or EVRe Guard's). A table of
 * ranges is sorted by address, without overlaps, inside 0xD000..0xDFFF;
 * protocolInit() checks it. Tables are written positionally, so the member
 * order is fixed: a new member goes at the end. */
typedef struct evre_range {
	uint16_t start;
	uint16_t len;
	uint8_t *base;
	uint8_t writable; /* 0: read-only, 1: read-write */
} evre_range_t;

struct evre_base;

/* 1.1: the layer above's say on a frame, see WRITE_HANDLER and READ_HANDLER. */
typedef uint8_t (*evre_write_handler_t)(struct evre_base *device, uint16_t offset, const uint8_t *data, uint16_t count);
typedef uint8_t (*evre_read_handler_t)(struct evre_base *device, uint16_t offset, uint16_t count);

/* One device, or a host's mirror of one.
 *
 * A plain struct on purpose: no constructor, base class, virtual or private
 * member. So a global one is set up at compile time, before any code runs, and
 * offsetof works on it (EVRe.cpp checks both).
 *
 * Who owns what:
 * - D000 is the device's. The library reads and writes through it, and never
 *   allocates, frees or moves it. Entry k is register 0xD000 + k, so the table
 *   has DEVICE_REG_READ_MAX - 0xD000 + 1 entries. Not (READ_MAX + 1) & 0x0FFF:
 *   that is 0 for a bank that runs to 0xDFFF. A READ_MAX above 0xDFFF is taken
 *   as 0xDFFF: the bank ends there, and the table has 0x1000 entries at most.
 * - A000 stays so that 1.0 code compiles, and delete[] on it stays harmless.
 *   1.1 serves the reserved bank from DEVICE_ID .. MSG_BUFFER below and never
 *   uses A000: protocolInit() sets it to nullptr. A reserved register cannot be
 *   redirected through it any more.
 *
 * Calls on one device must not preempt each other: decodePacket, encodePacket,
 * addMsg, and the device's own writes to STATUS, CONFIG and MSG_* (1.1: the
 * library writes STATUS too, see CAP_BROADCAST_D000). A caller at another
 * priority masks the decoder's interrupt around its call; for addMsg the
 * library can do it (EVRE_LOCK above). */
typedef struct evre_base {
		/* One byte, two names: SALVE_ID_REG is the 1.0 spelling and stays, first,
		 * so that it keeps the initialiser. Reading the one that was not written
		 * last is not strict ISO C++, but GCC, Clang, IAR and Keil define it, and
		 * the library itself only uses SALVE_ID_REG. */
		union {
			uint8_t SALVE_ID_REG = 0;
			uint8_t SLAVE_ID_REG;
		};

		uint16_t DEVICE_REG_READ_MAX = 0;
		/* 0 by default, as in 1.0: a device that sets only READ_MAX has its whole
		 * bank writable. */
		uint16_t DEVICE_REG_WRITE_MIN = 0;

		const uint16_t RESERVED_REG_READ_MAX = 0xA105;
		const uint16_t RESERVED_REG_WRITE_MIN = 0xA004;

		uint8_t **A000 = nullptr; /* 1.1: always nullptr, see above */
		uint8_t **D000 = nullptr;

		uint16_t DEVICE_ID = 0;

		uint16_t STATUS = 0;

		uint16_t CONFIG = 0;

		uint8_t MSG_CNT = 0;

		uint8_t MSG_BUFFER[255] = { 0 };

		/* [0] runs once when a write reaches MSG_CNT (the queue cleared), [c] when
		 * message c is acknowledged. One slot for every code: 1.0 had 255, so
		 * acknowledging 0xFF called whatever lay past the array. */
		void (*MSG_ACK_HANDLER[256])(void) = {0};

		/* 1.1 and later: new members only here, after every 1.0 member, so that
		 * those keep their 1.0 offsets. */

		/* 1.1, optional: asked before the library stores a WRITE, a WRITE_ACK or
		 * (on a mirror) a READ_RESP, in either bank. It is asked once the
		 * library's own checks have passed, and for a WRITE_ACK once there is
		 * room for the answer. A broadcast into the device bank that
		 * ACCEPT_BROADCAST_D000 refuses never gets here. NO_ERROR: store it;
		 * EVRE_HANDLED: accepted, store nothing; any other code: refused with that
		 * code, nothing stored. The bytes are passed as they came: the handler
		 * decides what they mean. RX_SLAVE_ID says whether the frame was a
		 * broadcast. It runs where decodePacket runs (often an interrupt): keep
		 * it short. */
		evre_write_handler_t WRITE_HANDLER = nullptr;

		/* 1.1, optional: asked before a READ (of either bank) is answered, once the
		 * library's own checks have passed and there is room for the answer.
		 * NO_ERROR or EVRE_HANDLED: answer it; any other code: refused with that
		 * code. The device's own sending (encodePacket, AUTO_SEND) is not asked:
		 * the device decides what it sends. */
		evre_read_handler_t READ_HANDLER = nullptr;

		/* 1.1, in place of D000: the device bank as ranges (evre_range_t). When
		 * set, D000, DEVICE_REG_READ_MAX and DEVICE_REG_WRITE_MIN are not used.
		 * Set it before protocolInit() returns (in protocolConfigure, or before
		 * the call): only then is the table checked, and the library relies on a
		 * checked table. A table that fails the check serves nothing, whatever
		 * protocolConfigure returned. */
		const evre_range_t *D_RANGES = nullptr;
		uint8_t D_RANGE_CNT = 0;

		/* 1.1: whether this evre_base_t accepts responses. 1 for a host's mirror
		 * of a device: it stores the READ_RESP it receives (as 1.0 did) and takes
		 * a WRITE_ACK_RESP as the news that its write landed. 0, a device: both
		 * are refused (FUNCTION_CODE_ERR, never answered), so a host cannot write
		 * read-only registers with a READ_RESP. See MIGRATING FROM 1.0 above.
		 * With 1 the library never refreshes STATUS bit 14 (CAP_BROADCAST_D000):
		 * a mirror keeps the bit its device reported in a READ_RESP, and a READ
		 * it answers or a READ_RESP it builds carries that bit, whatever its own
		 * ACCEPT_BROADCAST_D000 says. */
		uint8_t ACCEPT_READ_RESP = 0;

		/* 1.1: whether a broadcast WRITE may reach the device bank. 0, the
		 * default: a broadcast reaches the reserved bank only (CONFIG, the queue's
		 * clear and acks); one whose offset lies in 0xD000..0xDFFF, of any count,
		 * is refused (PERMISSION_DENIED, never answered) before a handler is
		 * asked or a byte is stored. 1: the device bank takes it, as in 1.0: a set
		 * point every device takes at once, one stop command for all. A device's
		 * STATUS says which (CAP_BROADCAST_D000) from the next time it goes out; a
		 * mirror's says what its device reported (see ACCEPT_READ_RESP). See
		 * MIGRATING FROM 1.0 above. */
		uint8_t ACCEPT_BROADCAST_D000 = 0;

		/* 1.1, set by the decoder just before it asks a handler, read by the
		 * handler: the slave id of the frame the handler runs for, BROADCAST_ID
		 * (0) for a broadcast. Framing, not data: the layer above tells by it
		 * whether every slave heard the frame (EVRe Guard does). Outside a handler
		 * it means nothing: it holds the id of the last frame a handler was asked
		 * about. */
		uint8_t RX_SLAVE_ID = 0;
} evre_base_t;

/* The 1.0 name, kept so that every existing device and host still compiles.
 * New code uses evre_base_t. */
typedef evre_base_t base_t;

/* The 1.0 struct tag, as a type name, so that `protocol_base *p` still
 * compiles. `struct protocol_base` does not: C++ has no alias for a tag. */
typedef evre_base_t protocol_base;

/* EVRE_CRC_TABLE_RUNTIME: 0 = 512-byte constant table in flash (default).
 *                        1 = table built in RAM by EVRe_CrcTableInit(),
 *                            called from protocolInit(), or by the first
 *                            GetCrc16() before it (a host that only encodes):
 *                            moves the 512-byte table from flash to RAM
 *                            (about 430 B of flash freed, 513 B of RAM
 *                            spent, with the builder). Behaviour is
 *                            identical either way.
 * Either way the table is defined once, in EVRe.cpp. */
#ifndef EVRE_CRC_TABLE_RUNTIME
#define EVRE_CRC_TABLE_RUNTIME 0
#endif

#if EVRE_CRC_TABLE_RUNTIME
extern uint16_t crctab16[256];
void EVRe_CrcTableInit(void);
#else
extern const uint16_t crctab16[256];
#endif

uint16_t GetCrc16(const uint8_t *pData, int nLength);

/* Allocating forms: the response is calloc'ed and the caller must free it.
 * Thin wrappers around the *Into forms below. decodePacket allocates 11 bytes
 * (an ERROR_RESP), more only for a READ that is ours and has a good CRC:
 * sized by its count, one bank (0x1000) at most. If that much is not there it
 * takes 11 bytes after all, so the host still hears why: BUFFER_TOO_SMALL for
 * a READ that passes the library's checks, its own code for one that fails. */
uint8_t decodePacket(evre_base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t **RESPONSE, uint16_t *rSize);

uint8_t encodePacket(evre_base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t **PACKET, uint16_t *pSize);

/* No-heap forms: the caller supplies the buffer. outLen is the number of
 * bytes written: it is set to 0 first, on every call, and stays 0 when the
 * function produces no response. It must not be null. Returns
 * BUFFER_TOO_SMALL if outMax cannot hold the frame. Use these on small MCUs
 * and anywhere the call happens in an interrupt: they never allocate.
 *
 * decodePacketInto answers a refused frame with ERROR_RESP, whatever the code
 * and whoever returns it (a handler too). Silence is decided by the frame,
 * never by the code: a frame too short, with a wrong start (0x7B) or end
 * (0x7D) byte, a bad CRC or another slave's id is not ours to answer; a
 * broadcast is heard by every slave; a response (READ_RESP, WRITE_ACK_RESP,
 * ERROR_RESP) is never answered, or two devices could trade ERROR_RESP frames
 * for ever. The library never decodes an ERROR_RESP (FUNCTION_CODE_ERR): a
 * host reads its code byte, DATAx(0), itself.
 *
 * outBuf may be PACKET itself, for every function code, if it holds both the
 * frame and the answer: the header is read first, and a WRITE_ACK is answered
 * only after its data are stored.
 *
 * encodePacketInto takes the data from pData, or from the device's own
 * registers when pData is nullptr. pData may already lie in outBuf, at
 * DATAx(0). It builds no broadcast (slave 0) but a WRITE: every device refuses
 * the others. */
uint8_t decodePacketInto(evre_base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);

uint8_t encodePacketInto(evre_base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);

uint8_t addMsg(evre_base_t *device, uint8_t MSG);

/* protocolInit(device) does, in this order:
 *   1. with EVRE_CRC_TABLE_RUNTIME 1, builds the CRC table;
 *   2. A000 = nullptr;
 *   3. MSG_ACK_HANDLER[0..255] = nullptr;
 *   4. STATUS = EVRE_PROTOCOL_VERSION and the library's capability bits,
 *      except CAP_BROADCAST_D000, which is set as a device's STATUS goes out
 *      (STATUS_ENUM);
 *   5. protocolConfigure(device): once, with the same pointer, on the calling
 *      thread;
 *   6. if D_RANGES is set, checks the table, whatever step 5 returned. A table
 *      that fails sets D_RANGE_CNT to 0: every request to the device bank is
 *      then refused, so a caller that ignores the code serves nothing from a
 *      table the library cannot trust.
 * It returns step 5's code when that is not NO_ERROR, else RANGE_TABLE_INVALID
 * for a table that fails, else NO_ERROR. After step 5 only D_RANGE_CNT may be
 * written (step 6), and no other field is touched. So a field may be set
 * before protocolInit() (SALVE_ID_REG, DEVICE_ID), in protocolConfigure()
 * (MSG_ACK_HANDLER entries, STATUS |= the device's own bits) or after
 * protocolInit() returns (a range table set then is not checked). New steps go
 * before step 5. */
uint8_t protocolInit(evre_base_t *device);

/* The device's own setup, called by protocolInit(). EVRe.cpp has a weak
 * default that does nothing. A device overrides it with exactly this
 * signature (base_t * is the same type), at global scope, with C++ linkage,
 * and not weak. Anything else (a reference, a const pointer, a namespace) is
 * another function: the weak default then runs without a word, SALVE_ID_REG
 * stays 0 and the device answers nothing. -Wmissing-declarations makes the
 * compiler say so. extern "C" does not compile in a file that includes
 * EVRe.h. Never put EVRE_WEAK on this declaration: every override would be
 * weak too. */
uint8_t protocolConfigure(evre_base_t *device);

/* The values go on the wire (ERROR_RESP's data byte) and into other programs'
 * tables, so every one is written out. A new code is appended, below 0xFF.
 * winerror.h (windows.h) defines NO_ERROR as a macro: it is set aside here and
 * put back after, so the two headers may come in either order. */
#ifdef NO_ERROR
#pragma push_macro("NO_ERROR")
#undef NO_ERROR
#define EVRE_RESTORE_NO_ERROR
#endif

enum ERR_CODE_ENUM {
	NO_ERROR = 0U,
	INVALID_PACKET_ERR = 1U,
	FUNCTION_CODE_ERR = 2U,
	PERMISSION_DENIED = 3U,
	REG_OFFSET_OUT_OF_RANGE = 4U,
	REG_CNT_OUT_OF_RANGE = 5U,
	MEM_ALLOCATION_FAILED = 6U,
	SLAVE_ID_MISMATCHED = 7U,
	MSG_BUFFER_FULL = 8U,
	MSG_NULL = 9U,
	INSTANCE_IS_NULL = 10U,
	BUFFER_TOO_SMALL = 11U, /* caller's buffer too small for the response */
	LENGTH_MISMATCH = 12U, /* received length does not match the declared register count */
	/* On the wire: the device's access layer needs a login first; log in and
	 * retry. The library only reserves the code and never returns it: a
	 * handler does (EVRe Guard, when no session is open). */
	LOGIN_REQUIRED = 13U,
	RANGE_TABLE_INVALID = 14U, /* protocolInit(): D_RANGES has a range of 0 bytes or without memory, or is unsorted, overlapping or outside 0xD000..0xDFFF; never sent */
	/* On the wire: the device's layer above did not take a value; nothing was
	 * stored. The library only reserves the code and never returns it: a
	 * handler does (EVRe Guard). */
	VALUE_REFUSED = 15U,
};

#ifdef EVRE_RESTORE_NO_ERROR
#pragma pop_macro("NO_ERROR")
#undef EVRE_RESTORE_NO_ERROR
#endif

enum FN_CODE_ENUM {
	READ = 0xAA,
	READ_RESP = 0xAB,
	WRITE = 0xEA,
	WRITE_ACK = 0xEB,
	WRITE_ACK_RESP = 0xEC,
	ERROR_RESP = 0xEE /* data section: one ERR_CODE_ENUM byte */
};

enum PERMISSION_ENUM {
	READ_ONLY = 0U,
	READ_WRITE = 1U
};

enum RESERVED_MAP_ENUM {
	DEVICE_ID_BASE_ADDR = 0xA000,
	STATUS_BASE_ADDR = 0xA002,
	CONFIG_BASE_ADDR = 0xA004,
	MSG_CNT_BASE_ADDR = 0xA006,
	MSG_BUFFER_BASE_ADDR = 0xA007
};

/* STATUS (0xA002), read only: protocol revision and what this device
 * implements, so a host can detect features instead of assuming them. */
enum STATUS_ENUM {
	STATUS_VERSION_MASK = 0x00FFU, /* [7:0] EVRE_PROTOCOL_VERSION */
	CAP_ERROR_FRAME = 0x0100U, /* answers a rejected packet with ERROR_RESP */
	CAP_BROADCAST = 0x0200U, /* accepts slave 0 for WRITE (into the device bank only with ACCEPT_BROADCAST_D000) */
	CAP_MSG = 0x0400U, /* MSG_CNT / MSG_BUFFER implemented */
	CAP_AUTO_SEND = 0x0800U, /* CONFIG bit AUTO_SEND implemented */
	CAP_DFU = 0x1000U, /* CONFIG bit DFU_MODE implemented */
	CAP_STATIC = 0x2000U, /* built without the heap (the *Into functions) */
	/* 1.1: the device bank takes a broadcast WRITE (ACCEPT_BROADCAST_D000 is
	 * set), so a host knows whether its broadcast set point or stop reaches
	 * this device. The library's, not the device's: each time STATUS goes out
	 * (a READ answered, a READ_RESP built from the registers) the library sets
	 * or clears it as the setting is then, in the answer and in STATUS itself.
	 * Until STATUS first goes out after a change, the field may still show the
	 * old value. A mirror (ACCEPT_READ_RESP 1) is never refreshed: it keeps and
	 * serves the bit its device reported in a READ_RESP, and before one the
	 * bit protocolInit() left there. */
	CAP_BROADCAST_D000 = 0x4000U,
};

/* CONFIG (0xA004) bit numbers; hosts and devices use them by value. The
 * library sets HEARTBEAT for every accepted request (READ, WRITE, WRITE_ACK),
 * after its answer is built, and never for a response a mirror takes, so a
 * mirror's CONFIG is the device's. The device clears it; a host cannot. */
enum CFG_ENUM {
	HEARTBEAT = 0U,
	SYSTEM_RESET = 1U,
	MSG_ENABLE = 2U,
	AUTO_SEND = 3U,
	DFU_MODE = 4U
};

enum BYTE_IND_ENUM {
	BYTE0 = 0U,
	BYTE1,
	BYTE2,
	BYTE3,
	BYTE4,
	BYTE5,
	BYTE6,
	BYTE7,
};

#endif


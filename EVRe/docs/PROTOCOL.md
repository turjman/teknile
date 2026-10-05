# EVRe

**Embedded Volatile Register express** — pronounced **"ever"**, because the
registers are always live.

A small single-master register protocol for embedded devices. One request
returns a whole block of mixed-type telemetry, over any byte link: UART,
USART, I²C, SPI, RS-485, USB CDC or TCP/IP.

| Property | Value |
|---|---|
| Protocol&nbsp;revision | **1** (reported in `STATUS[7:0]`) |
| Library | **1.1** (`EVRE_LIB_VERSION` `0x0101`); what changed since 1.0: [Migrating from 1.0](#migrating-from-10) |
| Implementation | C++11, two files, no dependencies beyond the C and C++ standard headers |
| Footprint | ~3 kB of code; heap optional |
| Status | in production across 100+ devices since 2023 |

```
host                                   device
 |   { 01 AA 00 D0 CC 00 ... }          |   READ 204 registers at 0xD000
 +------------------------------------->|
 |   { 01 AB 00 D0 CC 00 <204 B> ... }  |   the device's entire state, one reply
 <-------------------------------------+
```

---

## Contents

1. [Why it exists](#why-it-exists)
2. [Layers: EVRe and EVRe Guard](#layers-evre-and-evre-guard)
3. [Quick start](#quick-start)
4. [Frame](#frame)
5. [Function codes](#function-codes)
6. [CRC](#crc)
7. [Register model](#register-model)
8. [Reserved bank — 0xA000](#reserved-bank--0xa000)
9. [Messages](#messages)
10. [Errors](#errors)
11. [Broadcast](#broadcast)
12. [Framing on a byte stream](#framing-on-a-byte-stream)
13. [Test vectors](#test-vectors)
14. [Device API](#device-api)
15. [Adding a device](#adding-a-device)
16. [Host implementation](#host-implementation)
17. [Patterns](#patterns)
18. [Pitfalls](#pitfalls)
19. [Conformance checklist](#conformance-checklist)
20. [Build options](#build-options)
21. [Migrating from 1.0](#migrating-from-10)
22. [Roadmap — v2](#roadmap--v2)

---

## Why it exists

Modbus is the obvious choice for this job and it has two properties that hurt
on a modern MCU:

- **Its register is 16 bits.** Every `float` becomes an awkward two-register
  pack with an endianness argument attached, and a `bool` wastes 15 bits.
- **It wants one request per contiguous block per type.** Reading a device's
  state means several round-trips.

EVRe changes one thing and most of the awkwardness goes away:

> **A register is one byte.**

A `float` is 4 registers, a `uint16_t` is 2, a `bool` is 1, and `REG_CNT` is
simply a byte length. Lay the read-only variables out contiguously and a
device's **entire state comes back in a single request** — mixed floats,
integers, booleans and bitfields, one transaction.

A second decision makes it portable: the frame carries its own delimiters and
CRC, so the *same bytes* work on every link. There is no RTU/ASCII/TCP split.

---

## Layers: EVRe and EVRe Guard

EVRe is a **transport**: it moves bytes between a host and a device's address
space and never asks what they mean. A float, a setting, a token: to EVRe they
are bytes at an address. Meaning lives in the layer above, which a device uses
or not; the protocol is the same either way.

```
+---------------------------------------------------------------+
| the map (evre-map/1): what each register is - names, types,   |
|   units, limits, value names, fields, login. For the tools.   |
+---------------------------------------------------------------+
| EVRe Guard (optional, in the device): who may read and write  |
|   - login with a token, lockout against brute force, idle     |
|   logout; checks a device adds itself                         |
+-------------------------------+-------------------------------+
                                |  bytes up, a verdict down
                                |  (READ_HANDLER, WRITE_HANDLER)
+-------------------------------v-------------------------------+
| EVRe: frames, CRC, slave, READ / WRITE / ACK / ERROR, the     |
|   reserved bank, messages, an address space of readable and   |
|   writable bytes (a pointer table or ranges)                  |
+---------------------------------------------------------------+
```

The rule for every change: **nothing that interprets data goes into EVRe.**
The library may learn *where* bytes are and *whether* they may be read or
written; *what they are* belongs to EVRe Guard, the map, or the device. The
library even reserves a code for the layer above, `LOGIN_REQUIRED`, without
knowing what a login is: only a handler returns it. It tells a handler which
frame it runs for (`RX_SLAVE_ID`, library 1.1: a broadcast or not), because
that is framing, not meaning.

---

## Quick start

**Device side** — three calls:

```c
#include "EVRe.h"

evre_base_t dev;

uint8_t protocolConfigure(evre_base_t *d) {      /* override the weak default: exactly this signature */
    d->SALVE_ID_REG = 1;
    d->DEVICE_ID    = 0x2001;
    d->DEVICE_REG_READ_MAX  = 0xD0EC;
    d->DEVICE_REG_WRITE_MIN = 0xD0CC;
    d->D000 = new uint8_t*[d->DEVICE_REG_READ_MAX - 0xD000 + 1];   /* entry k: 0xD000 + k */
    uint8_t *p = (uint8_t *) &my_registers;                  /* a packed struct */
    for (uint16_t i = 0; i < sizeof(my_registers); ++i) d->D000[i] = &p[i];
    return NO_ERROR;
}

protocolInit(&dev);                          /* once at start-up */

/* whenever a frame arrives */
uint16_t len = 0;
decodePacketInto(&dev, rx, rxLen, txBuf, sizeof(txBuf), &len);
if (len) transmit(txBuf, len);               /* reply, or an error frame */
```

**Host side** — see [Host implementation](#host-implementation) for a complete
Python client in ~80 lines.

---

## Frame

| Start | Slave&nbsp;ID | Fn.&nbsp;Code | Reg.&nbsp;Offset | No.&nbsp;Registers | Data | CRC | End |
|---|---|---|---|---|---|---|---|
| 1&nbsp;byte | 1&nbsp;byte | 1&nbsp;byte | 2&nbsp;bytes | 2&nbsp;bytes | *n*&nbsp;bytes | 2&nbsp;bytes | 1 byte |

| Offset | Field |
|---|---|
| `0` | **Start** — `0x7B` (`{`) |
| `1` | **Slave ID** — `0` is [broadcast](#broadcast) |
| `2` | **Function code** |
| `3..4` | **Register offset**, little endian |
| `5..6` | **Number of registers** (`REG_CNT`), little endian |
| `7 …` | **Data**, *n* bytes (below) |
| `len-3` | **CRC** low byte |
| `len-2` | **CRC** high byte |
| `len-1` | **End** — `0x7D` (`}`) |

`len = 10 + n`. The number of data bytes *n* comes from the function code, not
from the number of registers alone:

| Function | *n* | Frame&nbsp;length | The number of registers is |
|---|---|---|---|
| `READ` | 0 | 10 | the count asked for |
| `READ_RESP`,&nbsp;`WRITE`,&nbsp;`WRITE_ACK` | `REG_CNT` | 10&nbsp;+&nbsp;`REG_CNT` | the count of data bytes |
| `WRITE_ACK_RESP` | 0 | 10 | the write's count, echoed |
| `ERROR_RESP` | 1 | 11 | the request's count, echoed |

Smallest frame is **10 bytes**. A frame of a function code the library
decodes, at any other length, is refused with code 12; an unknown code or an
`ERROR_RESP` is code 2 whatever its length. One under 10 bytes is dropped as a
bad frame (code 1). A framer that expects `10 + REG_CNT` bytes for every frame
waits for bytes that never come after a `READ`, a `WRITE_ACK_RESP` or an
`ERROR_RESP`.

Every multi-byte field is **little endian**, on the wire and in the registers.
The library builds the header and the CRC byte by byte, so they are little
endian on any CPU; the registers it sends as they lie in memory (see
[Build options](#build-options), porting).

**A count of 0** is a valid request. `READ` x0 is answered with a 10-byte
`READ_RESP` and `WRITE_ACK` x0 with a `WRITE_ACK_RESP`. The offset is still
checked (`READ 0xA106` x0 is code 4), the handlers are asked, `HEARTBEAT` is
set, and nothing is stored: a write of no bytes at `MSG_CNT` does not clear
the queue. With `ACCEPT_BROADCAST_D000` 0, the default, a broadcast of no
bytes into the device bank is refused like any other
([Broadcast](#broadcast)); with 1 it is taken, as a unicast one is.

The delimiters are `{` and `}` deliberately: a frame is recognisable at a
glance in a terminal or a logic-analyser dump.

---

## Function codes

| Code | Name | Direction | Data | Answered with |
|---|---|---|---|---|
| `0xAA` | `READ` | →&nbsp;slave | none | `READ_RESP` |
| `0xAB` | `READ_RESP` | →&nbsp;master | *n*&nbsp;bytes | — |
| `0xEA` | `WRITE` | →&nbsp;slave | *n*&nbsp;bytes | *nothing* (`ERROR_RESP` if refused) |
| `0xEB` | `WRITE_ACK` | →&nbsp;slave | *n*&nbsp;bytes | `WRITE_ACK_RESP` |
| `0xEC` | `WRITE_ACK_RESP` | →&nbsp;master | none | — |
| `0xEE` | `ERROR_RESP` | →&nbsp;master | 1&nbsp;byte | — |

```
READ        >  +-- READ  ---------->|
               |<-- READ_RESP ------+

WRITE       >  +-- WRITE ---------->|      (nothing comes back)

WRITE_ACK   >  +-- WRITE_ACK ------>|
               |<-- WRITE_ACK_RESP -+

rejected    >  +-- anything ------->|
               |<-- ERROR_RESP -----+
```

`WRITE` is fire-and-forget: use it for high-rate set points where a lost
update is harmless, because the next one is already on its way.
`WRITE_ACK` is for anything that must be known to have landed.

**`WRITE_ACK_RESP` carries no status, by design.** A response at all means the
frame arrived with a valid CRC and was applied; a rejected write answers with
`ERROR_RESP` instead. There is nothing left for a status byte to say.

`READ_RESP` may also be *sent* by a device that was not asked — that is how
unsolicited streaming works.

**Responses are for the host** (library 1.1). A device refuses a `READ_RESP`
or a `WRITE_ACK_RESP` sent to it (code 2) and does not answer, so a host cannot
overwrite registers with a `READ_RESP`, the read-only ones included. A host
that decodes a device's answers into a copy of it sets `ACCEPT_READ_RESP` on
that copy: see [A host's mirror](#a-hosts-mirror-library-11). The library
decodes no `ERROR_RESP` (code 2, silent), a mirror's included: a host reads its
code byte, `DATAx(0)`, itself.

A broadcast (slave 0) is built for `WRITE` only: `encodePacket` refuses any
other function with code 2 (library 1.1), since every device would refuse it.
A broadcast `WRITE` into the device bank is built all the same: the encoder
cannot know which devices take one ([Broadcast](#broadcast)).

---

## CRC

**CRC-16/X-25**: polynomial `0x1021` reflected (`0x8408`), init `0xFFFF`,
reflected in and out, final XOR `0xFFFF`.

It covers every byte **before** the CRC field and excludes the CRC and the End
delimiter, so the length passed is `len - 3`.

```c
uint16_t GetCrc16(const uint8_t *pData, int nLength) {
    uint16_t fcs = 0xffff;
    while (nLength > 0) {
        fcs = (fcs >> 8) ^ crctab16[(fcs ^ *pData) & 0xff];
        nLength--;
        pData++;
    }
    return ~fcs;
}
```

Table generator, for a host implementation:

```python
TAB = []
for i in range(256):
    v = i
    for _ in range(8):
        v = (v >> 1) ^ 0x8408 if v & 1 else v >> 1
    TAB.append(v)
```

First eight entries, to check a port: `0x0000 0x1189 0x2312 0x329B 0x4624
0x57AD 0x6536 0x74BF`.

This is a standard CRC — most languages have it as **CRC-16/X-25** (also called
CRC-16/IBM-SDLC or CRC-B). If your library offers it by name, use that rather
than porting the table.

**What it catches.** In a frame of up to 4086 data bytes: every error of one,
two or three bits. The polynomial repeats after 32 767 bits, and such a frame
has at most 32 760 from its start byte to its CRC. In a longer frame, 4087 to
4096 data bytes (a `READ_RESP` or a write of nearly a whole bank), two flipped
bits exactly 32 767 bits apart can pass the CRC. At any length it catches
every error of an odd number of bits and every burst of up to 16 bits. Where
that matters, read a bank in two requests.

---

## Register model

Two banks, selected by the **top nibble** of the address:

| Bank | Range | Contents |
|---|---|---|
| `0xA000` | `0xA000`&nbsp;–&nbsp;`0xA105` | Reserved — identical on every EVRe device |
| `0xD000` | `0xD000`&nbsp;–&nbsp;device&nbsp;max&nbsp;(`0xDFFF`&nbsp;at&nbsp;most) | Device registers |

Any other address is rejected with code 3, for a read as well as a write
(library 1.1; before it, a write there was acknowledged and dropped). A request
may **not cross a bank boundary**: it belongs to the bank of its first
register, and one that runs past that bank's end gets code 5.

### Permissions

Each bank has **one address boundary**, not a per-register flag:

```
readable:  bank start        .. REG_READ_MAX
writable:  REG_WRITE_MIN     .. REG_READ_MAX
```

| Bank | `REG_READ_MAX` | `REG_WRITE_MIN` | A write ends at |
|---|---|---|---|
| `0xA000` | `0xA105` | `0xA004`&nbsp;(`CONFIG`) | the last queued message, `0xA006 + MSG_CNT` |
| `0xD000` | `DEVICE_REG_READ_MAX` (above `0xDFFF` taken as `0xDFFF`, library 1.1) | `DEVICE_REG_WRITE_MIN` (0 by default: the whole bank writable) | `DEVICE_REG_READ_MAX` |

So a device's map is laid out **read-only first, then read-write**. This is
not a limitation to work around — it is what lets a host read the entire
read-only block, whatever mixture of types it contains, in one request.

A request is judged by its start first, then by its end:

- a read that starts outside the readable area gets code 4;
- a write that starts outside the writable area gets code 3, and is refused
  **in its entirety**, even if part of the range would have been writable;
- either one that runs past the end gets code 5.

### Ranges (library 1.1)

A device can describe its bank as a few **ranges** instead of a pointer per
byte: each range is a run of addresses served from one block of memory, and is
read-only or read-write.

```c
typedef struct evre_range { uint16_t start, len; uint8_t *base; uint8_t writable; } evre_range_t;
```

The same rules apply range by range. A read may cross adjacent ranges, but not
a gap: code 4 if it starts in no range, 5 if it runs into a gap or past the
last range. A write gets code 3 if it starts in no range or touches a read-only
range, and 5 if it runs into a gap or past the last range (a read-only range
before the gap is judged first). Nothing is stored in either case. With the two
ranges `[0xD000, REG_WRITE_MIN)` read-only and `[REG_WRITE_MIN, REG_READ_MAX]`
read-write a device answers exactly as with the pointer table. Nothing changes
on the wire: a host cannot tell which one a device uses.

Read-only first, read-write after is still the layout to prefer: it keeps the
whole state readable, and every setting writable, in one request each.

---

## Reserved bank — 0xA000

| Address | Name | Perm | Size |
|---|---|---|---|
| `0xA000` | `DEVICE_ID` | R | 2 |
| `0xA002` | `STATUS` | R | 2 |
| `0xA004` | `CONFIG` | RW | 2 |
| `0xA006` | `MSG_CNT` | RW | 1 |
| `0xA007` | `MSG_BUFFER` | RW,&nbsp;up&nbsp;to&nbsp;`MSG_CNT` | 255 |

The five lie one after the other in `evre_base_t`, and the library reads and
writes them in place (library 1.1). A write to `MSG_CNT` or `MSG_BUFFER` is an
act, not data: see [Messages](#messages).

### STATUS — protocol revision and capabilities

Read it once and you know what the device supports, instead of assuming:

| Bits | Meaning |
|---|---|
| `[7:0]` | protocol revision (`1`) |
| `[8]` | `CAP_ERROR_FRAME` — answers rejects with `ERROR_RESP` |
| `[9]` | `CAP_BROADCAST` — accepts slave 0 (for `WRITE`; into the device bank only with bit 14) |
| `[10]` | `CAP_MSG` — message queue implemented |
| `[11]` | `CAP_AUTO_SEND` — periodic unsolicited frames |
| `[12]` | `CAP_DFU` — firmware update entry |
| `[13]` | `CAP_STATIC` — no-heap entry points available |
| `[14]` | `CAP_BROADCAST_D000` — the device bank takes a broadcast `WRITE` (library 1.1, see [Broadcast](#broadcast); a host's mirror serves the bit its device reported) |

`protocolInit()` sets `[7:0]` and bits 8, 9, 10 and 13 (`0x2701`). A device
adds bits 11 and 12 in `protocolConfigure()` for what it implements (the device
of the [test vectors](#test-vectors) reports `0x3F01`).

Bit 14 is the library's (library 1.1). Each time `STATUS`'s high byte goes
out, in a `READ` answered or in a `READ_RESP` the device builds from its
registers, the library sets the bit exactly when `ACCEPT_BROADCAST_D000` is
1, whatever the field held, and writes the field to match. So a device may
change the setting at any time. With it, a device reports `0x6701` from its
first read of `STATUS` (the test-vector device `0x7F01`). Until `STATUS` next
goes out, the field itself may still hold the old bit.

A host's mirror (`ACCEPT_READ_RESP` 1) is the exception: the library never
refreshes its bit 14. Its `STATUS` is its device's, bit 14 included, as the
last `READ_RESP` stored it, and a `READ` the mirror answers and a `READ_RESP`
it builds carry that bit, whatever the mirror's own `ACCEPT_BROADCAST_D000`
says. Before the first `READ_RESP` of `STATUS` it holds what `protocolInit()`
left there ([A host's mirror](#a-hosts-mirror-library-11)).

This is what lets one host library talk to a mixed fleet: a device without DFU
simply reports the bit clear. **Read `STATUS` before using any optional
feature.**

### CONFIG

| 15–8 | 7–5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|
| AUTO_SEND&nbsp;prescaler | — | `DFU` | `AUTO_SEND` | `MSG_ENABLE` | `SYS_RESET` | `HEARTBEAT` |

The library stores what a host writes to `CONFIG` and sets `HEARTBEAT`. It
writes no other bit and acts on none: the rest are the device's to read and
act on. `protocolInit()` does not touch `CONFIG`: it is 0 until the device or a
host writes it.

- **`HEARTBEAT`** is set by the library for every accepted request: a `READ`,
  a `WRITE` (a broadcast one too) or a `WRITE_ACK`, a write the handler took
  itself (`EVRE_HANDLED`) included. It is set last, after the answer is built:
  a `READ` of `CONFIG` shows it as it was before that `READ`, and a write to
  `CONFIG` cannot clear it, so a host cannot clear it at all. The device's
  application clears it. A refused frame does not set it (a `READ` refused
  only because its answer does not fit included), the encoder never does, and
  neither does a response a mirror takes (library 1.1), so a mirror's `CONFIG`
  is the device's, bit 0 too. Read it to confirm the link is alive.
- **`SYS_RESET`**, **`DFU`** — device actions, if advertised in `STATUS`.
- **`MSG_ENABLE`** — enables the message queue. The device checks it before it
  calls `addMsg()`; the library queues either way.
- **`AUTO_SEND`** — periodic unsolicited `READ_RESP` of the read-only block,
  which the device builds with `encodePacketInto()` from its own registers.

```
Freq = 8000 / (prescaler + 1)        prescaler = 8000 / Freq - 1
```

The prescaler is the reload of the device's timer, and a reload of 0 stops a
timer: **a host never writes 0**. The valid range is 1-255; the specified
rates are 4000 Hz (prescaler 1) down to 40 Hz (`0xC7`). A device that receives
0 replaces it (by 1, or by its default) and never stops the stream for it.

| Prescaler | CONFIG&nbsp;(AUTO_SEND&nbsp;on) | Rate |
|---|---|---|
| `0x01` | `0x0108` | 4000 Hz (the fastest) |
| `0x09` | `0x0908` | 800 Hz |
| `0x13` | `0x1308` | 400 Hz |
| `0x27` | `0x2708` | 200 Hz |
| `0x4F` | `0x4F08` | 100 Hz (a common default) |
| `0xC7` | `0xC708` | 40 Hz |

The default prescaler is the device's to set.

Measured on an STM32H750 at 100 / 200 / 40 Hz commanded: **100.16 / 199.87 /
39.98 Hz**.

---

## Messages

A device-to-host event queue: up to 255 one-byte codes, `1` to `0xFF`. The
device queues them with `addMsg()` while `CONFIG.MSG_ENABLE` is set.

- `MSG_CNT` — how many are queued.
- `MSG_BUFFER[0 .. MSG_CNT-1]` — the codes; `0` marks an empty slot. Past
  `MSG_CNT` the buffer reads `0` (library 1.1: an ack or a clear zeroes what it
  frees), so a host that reads the whole buffer never sees a code again that is
  already gone.
- Writing **any** value to `MSG_CNT` clears the queue. The handlers of the
  messages it discards do **not** run: `MSG_ACK_HANDLER[0]` runs instead,
  once, and finds the queue already empty.
- Writing to `MSG_BUFFER[i]` **acknowledges** message *i*: the device runs its
  handler (`MSG_ACK_HANDLER[code]`, one slot for every code, `0xFF` included,
  library 1.1), removes it and compacts the buffer. The value written does not
  matter. An index at or past `MSG_CNT` is refused (code 5).
- **A write that covers `MSG_CNT` is a clear, and nothing more** (library
  1.1). The slots lie after `MSG_CNT`, so the rest of such a write is slot
  bytes, and they do nothing: no ack handler runs and no slot changes. The
  write acts exactly as `MSG_CNT` alone: `MSG_ACK_HANDLER[0]` runs once, and a
  message it queues is kept. `CONFIG` bytes before `MSG_CNT` in the same write
  are stored as usual. The write is still checked first, up to the last queued
  message (code 5 past it), and a write that fails does nothing. (1.0 went on
  and acknowledged the slots. The clear left the old codes in the buffer, so
  the handlers of the messages it had just discarded ran after all, and a
  message that `MSG_ACK_HANDLER[0]` had just queued was acknowledged before
  any host saw it.)
- The handlers run where the decoder runs, often an interrupt: keep them short.
  One may queue a message itself; it is kept (if there is room: a full queue
  is compacted only after the acks). `protocolInit()` clears all 256, so a
  device sets them in `protocolConfigure()` or after `protocolInit()`.

The codes themselves are device-specific; the mechanism is not. The queue has
two writers, `addMsg()` and the decoder: [Message API](#message-api) says how
they share it.

**The invariant that matters:**

> Acknowledging index *i* leaves every index **below** *i* unchanged, and
> shifts every index **above** *i* down by one.

That is what allows selective acknowledgement — urgent messages now, the rest
later — which is the point of a queue rather than a flags register. It also
means index order matters:

| Intent | How | |
|---|---|---|
| Ack&nbsp;everything | one&nbsp;write&nbsp;to&nbsp;`MSG_CNT` | ✔ — `MSG_ACK_HANDLER[0]` runs, not the per-message handlers |
| Ack&nbsp;a&nbsp;contiguous&nbsp;run | **one**&nbsp;write&nbsp;covering&nbsp;the&nbsp;range | ✔ — the device marks all, then compacts once |
| Ack&nbsp;selectively | separate writes, **highest index first** | ✔ |
| Ack&nbsp;selectively | separate writes, lowest index first | ✘ — indices shift under you |

The failing case, starting from `[0x02, 0x04, 0x0A]`:

| Write | Effect |
|---|---|
| index&nbsp;0 | acks `0x02`, compacts → `[0x04, 0x0A]` |
| index&nbsp;1 | index 1 is now `0x0A` — **wrong message acked** |
| index&nbsp;2 | beyond `MSG_CNT` — refused (code 5) |

`0x04` is never acked and the host believes it was. Going high-to-low is
always correct.

---

## Errors

A rejected request is answered with `ERROR_RESP` (`0xEE`), echoing the offset
and count of the request plus one error byte. It is always 11 bytes:

```
7B  <slave>  EE  <off_lo> <off_hi>  <cnt_lo> <cnt_hi>  <err>  <crc_lo> <crc_hi>  7D
```

| Code | Name | Meaning | On the wire |
|---|---|---|---|
| 0 | `NO_ERROR` | — | — |
| 1 | `INVALID_PACKET_ERR` | a bad frame: none at all, under 10 bytes, a wrong start (`0x7B`) or end (`0x7D`) byte (library 1.1), a CRC mismatch | never from the library (it is silent); a handler's 1 is sent (1.1) |
| 2 | `FUNCTION_CODE_ERR` | a function code the library does not decode, whatever the frame's length (1.1), `ERROR_RESP` included; a `READ_RESP` or `WRITE_ACK_RESP` sent to a device (1.1); a broadcast of anything but `WRITE`. From the encoder: any function but `READ`, `READ_RESP`, `WRITE` and `WRITE_ACK`, or a broadcast of anything but `WRITE` (1.1) | yes, for an unknown code |
| 3 | `PERMISSION_DENIED` | a request to an unknown bank; a write that starts outside the writable area (below `REG_WRITE_MIN`, past `REG_READ_MAX`, in no range) or touches a read-only range; a broadcast into the device bank without `ACCEPT_BROADCAST_D000` (1.1); a handler's refusal (EVRe Guard: a failed login, a login by broadcast) | yes |
| 4 | `REG_OFFSET_OUT_OF_RANGE` | a read (or a mirror's `READ_RESP`) that starts outside the readable area: past `REG_READ_MAX`, or in no range | yes |
| 5 | `REG_CNT_OUT_OF_RANGE` | a request that starts well and runs past the end: `offset + count - 1` past `REG_READ_MAX`, into a gap or past the last range; a write to the reserved bank past the last queued message (`0xA006 + MSG_CNT`) | yes |
| 6 | `MEM_ALLOCATION_FAILED` | `decodePacket` or `encodePacket` could not allocate (`decodePacket`: not even 11 bytes) | never |
| 7 | `SLAVE_ID_MISMATCHED` | another&nbsp;slave's&nbsp;frame | never from the library (it is silent); a handler's 7 is sent (1.1) |
| 8 | `MSG_BUFFER_FULL` | `addMsg`:&nbsp;255&nbsp;messages&nbsp;queued | never |
| 9 | `MSG_NULL` | `addMsg`: the code 0, which marks an empty slot | never |
| 10 | `INSTANCE_IS_NULL` | no&nbsp;device&nbsp;(a&nbsp;null&nbsp;`evre_base_t *`) | never |
| 11 | `BUFFER_TOO_SMALL` | the caller's buffer cannot hold the answer: the `READ_RESP` or the `WRITE_ACK_RESP` (`decodePacketInto`), the frame (`encodePacketInto`) | yes, for a `READ` whose answer does not fit when 11 bytes do |
| 12 | `LENGTH_MISMATCH` | a frame of a function code the library decodes is not 10 + *n* bytes, *n* from its function code ([Frame](#frame)) | yes |
| 13 | `LOGIN_REQUIRED` | the device's access layer needs a login first: log in and retry (1.1). The library only reserves the code: a handler returns it (EVRe Guard, while no session is open) | from a handler |
| 14 | `RANGE_TABLE_INVALID` | `protocolInit()`: the range table is not valid, and `protocolConfigure()` did not fail (its own code comes first) (1.1) | never |

"Yes" means in an `ERROR_RESP`, under the rules of
[When a device is silent](#when-a-device-is-silent) below. A handler may return
any code, and it goes out as it is. Every value is fixed: the codes are in
other programs' tables. A new code is appended, below `0xFF`.

### The order of the checks

A frame that is wrong in two ways gets the code of the first check it fails.
The order is part of the protocol:

| # | Check | Code | Answered |
|---|---|---|---|
| 1 | a&nbsp;device&nbsp;(`evre_base_t *`&nbsp;not&nbsp;null) | 10 | no |
| 2 | a frame, 10 bytes or more, start `0x7B` and end `0x7D` (1.1), then the CRC | 1 | no |
| 3 | the slave id is 0 (broadcast) or ours | 7 | no |
| 4 | a&nbsp;broadcast&nbsp;is&nbsp;a&nbsp;`WRITE` | 2 | no |
| 5 | a function code the library decodes: `READ`, `WRITE`, `WRITE_ACK`, `READ_RESP`, `WRITE_ACK_RESP` (1.1: before the length) | 2 | yes; an `ERROR_RESP`, no |
| 6 | the&nbsp;length&nbsp;is&nbsp;10&nbsp;+&nbsp;*n* | 12 | yes; a response, no |
| 7 | the&nbsp;function's&nbsp;own&nbsp;checks,&nbsp;below | 2&nbsp;to&nbsp;5,&nbsp;11,&nbsp;a&nbsp;handler's | yes, unless a broadcast or a response |

The function's own checks, in order:

| Function | Checks, then what is done |
|---|---|
| `READ` | the bank (3), the start (4), the end (5); room for the answer (11); the read handler. Then the answer is built. |
| `WRITE`,&nbsp;`WRITE_ACK` | for a broadcast into the device bank, `ACCEPT_BROADCAST_D000` (3, 1.1); the bank (3), the start (3), the end (5); for a `WRITE_ACK`, room for the ack (11: silent, since no `ERROR_RESP` fits either); the write handler. Then the data are stored, and then a `WRITE_ACK` is answered. |
| `READ_RESP` | `ACCEPT_READ_RESP` (2); the bank (3), the start (4), the end (5); the write handler. Then the bytes are copied. |
| `WRITE_ACK_RESP` | `ACCEPT_READ_RESP` (2); the bank (3). Nothing is stored and no handler is asked. |

Last, for an accepted `READ`, `WRITE` or `WRITE_ACK`, `HEARTBEAT` is set.

So the CRC comes before the slave id: a damaged frame for another slave is
code 1, not 7. The function code comes before the length (library 1.1): an
unknown code is 2 whatever its size. The broadcast rule comes before the
bank's limits (library 1.1): a broadcast `WRITE` of the right length into the
device bank of a device with `ACCEPT_BROADCAST_D000` 0 is 3 whatever its
offset and count, 0 bytes too, and no handler hears of it. And the room comes
before the handler (library 1.1): a handler acts only on a frame that can be
answered.

### When a device is silent

Silence is decided by the frame, never by the error code (library 1.1). A
device does not answer:

| Case | Code | Why |
|---|---|---|
| A bad frame: none at all, under 10 bytes, a wrong start or end byte, a CRC mismatch | 1 | If the frame is bad, the slave-ID field cannot be trusted either. Answering could put several slaves on the bus at once. |
| Slave&nbsp;ID&nbsp;mismatch | 7 | It was never ours to answer. |
| A&nbsp;broadcast,&nbsp;accepted&nbsp;or&nbsp;refused | any | Every slave heard it. |
| A response (`READ_RESP`, `WRITE_ACK_RESP`, `ERROR_RESP`), taken or refused | any | Two devices trading `ERROR_RESP` would never stop. |
| An&nbsp;accepted&nbsp;`WRITE` | 0 | Fire-and-forget. |

Every other refusal is answered with its code, a handler's included, whatever
code the handler returns (1 and 7 too, library 1.1). Two more silences are the
device's own, not the frame's:

- its transmit buffer holds less than the 11 bytes of an `ERROR_RESP`;
- `decodePacket` finds not even 11 bytes of heap: it returns
  `MEM_ALLOCATION_FAILED` and decodes nothing, so a `WRITE` in that frame is
  lost.

So a host that times out is looking at a bad frame, a wrong address, a
broadcast, a device out of memory, or a dead link — and anything else names
itself.

---

## Broadcast

**Slave ID `0` addresses every slave.** No slave answers it, so only `WRITE`
is accepted; `READ` and `WRITE_ACK` would make every device transmit at once.
`0` must never be used as a real device address: a device left at
`SALVE_ID_REG` 0 hears only broadcasts, and answers nothing.

**By default a broadcast reaches the reserved bank only** (library 1.1):
`CONFIG`, the queue's clear and its acks. A broadcast `WRITE` into the device
bank (`0xD000`..`0xDFFF`) is refused with code 3, whatever its count, 0 bytes
too: before the bank's limits are checked, before a handler is asked, before
a byte is stored. It is silent, as every broadcast is. The reason: anyone on
the link can send one, and every device obeys it at once.

A device whose bank must take broadcasts sets **`ACCEPT_BROADCAST_D000 = 1`**,
at any time. Its device bank then takes a broadcast `WRITE` as 1.0 did, under
the same checks as a unicast one. That is what a broadcast is for: a
synchronised set point across several boards in one frame, or a single stop
command that reaches every device in the same millisecond instead of N
sequential round-trips. Code that builds against 1.0 and 1.1 alike:

```c
#if defined(EVRE_LIB_VERSION) && EVRE_LIB_VERSION >= 0x0101
    dev.ACCEPT_BROADCAST_D000 = 1;
#endif
```

A host tells the two kinds of device apart by `STATUS` bit 14,
`CAP_BROADCAST_D000`: the library sets it exactly when the device bank takes
a broadcast ([STATUS](#status--protocol-revision-and-capabilities)). A host's
mirror keeps the bit its device reported and passes it on as it is: the
mirror's own `ACCEPT_BROADCAST_D000` never changes it
([A host's mirror](#a-hosts-mirror-library-11)). The encoder builds a
broadcast `WRITE` into the device bank either way: it cannot know the setting
of every device that hears it.

While a handler runs, `RX_SLAVE_ID` holds the frame's slave id, `BROADCAST_ID`
(0) for a broadcast (library 1.1). EVRe Guard uses it: it never takes a login
by broadcast ([EVRe Guard](#evre-guard-library-11)).

A broadcast is never answered, accepted or refused: a host that must know it
landed reads it back from each device.

---

## Framing on a byte stream

On a packet link (USB, framed SPI) one frame is one transfer and there is
nothing to do: the library checks the start and end bytes itself (1.1), and
drops a frame with a wrong one as it drops a bad CRC. Two frames in one
transfer are both dropped: the CRC is checked at the end of the transfer.

On a raw byte stream, **frame by length, never by scanning for `0x7D`.** The
delimiters are not escaped, and `0x7B`/`0x7D` occur inside ordinary float data
all the time. The header gives you the length:

```
1. scan forward for 0x7B
2. read bytes 2 and 5..6 -> FN_CODE, REG_CNT
   expected length = 10 + n, where n = REG_CNT for READ_RESP, WRITE, WRITE_ACK
                                     n = 1       for ERROR_RESP
                                     n = 0       for READ, WRITE_ACK_RESP, any other code
3. check byte[len-1] == 0x7D and verify the CRC over len-3
4. pass -> frame accepted
   fail -> advance ONE byte past that 0x7B and go to 1
```

A device's framer only sees requests (`READ` 10 bytes, `WRITE` and
`WRITE_ACK` 10 + `REG_CNT`); a host's only answers (`READ_RESP` 10 +
`REG_CNT`, `WRITE_ACK_RESP` 10, `ERROR_RESP` 11).

This resynchronises after any corruption, and keeps the delimiters doing what
they are good at: making frames visible to a human.

---

## Test vectors

Real frames, slave 1, CRCs computed with the algorithm above. Use them to
validate a new implementation before touching hardware.

The answers are those of this device: `DEVICE_ID` `0x2001`; `STATUS`
`0x3F01` (the library's `0x2701` with `CAP_AUTO_SEND` and `CAP_DFU`;
`ACCEPT_BROADCAST_D000` left at 0);
`0xD000..0xD00F` read-only, with the float `12.5` at `0xD000`;
`0xD010..0xD01F` read-write; three messages queued before #13.

| # | Meaning | Bytes |
|---|---|---|
| 1 | `READ`&nbsp;DEVICE_ID,&nbsp;`0xA000`&nbsp;×2 | `7B 01 AA 00 A0 02 00 9E 75 7D` |
| 2 | `READ_RESP`&nbsp;→&nbsp;`0x2001` | `7B 01 AB 00 A0 02 00 01 20 2F 0F 7D` |
| 3 | `READ`&nbsp;STATUS,&nbsp;`0xA002`&nbsp;×2 | `7B 01 AA 02 A0 02 00 E8 4C 7D` |
| 4 | `READ_RESP`&nbsp;→&nbsp;`0x3F01` | `7B 01 AB 02 A0 02 00 01 3F 0F EF 7D` |
| 5 | `READ`&nbsp;`0xD000`&nbsp;×4 | `7B 01 AA 00 D0 04 00 96 A1 7D` |
| 6 | `READ_RESP`&nbsp;→&nbsp;float&nbsp;`12.5` | `7B 01 AB 00 D0 04 00 00 00 48 41 56 9A 7D` |
| 7 | `WRITE`&nbsp;`0xD010`&nbsp;×2&nbsp;=&nbsp;`1000` | `7B 01 EA 10 D0 02 00 E8 03 A7 2D 7D` |
| 8 | `WRITE_ACK`,&nbsp;same | `7B 01 EB 10 D0 02 00 E8 03 72 B2 7D` |
| 9 | `WRITE_ACK_RESP` | `7B 01 EC 10 D0 02 00 5D CC 7D` |
| 10 | `ERROR_RESP`, `PERMISSION_DENIED` at `0xD000`: the answer to a `WRITE_ACK` (or a `WRITE`) of `0xD000` ×1 | `7B 01 EE 00 D0 01 00 03 3D 18 7D` |
| 11 | `WRITE_ACK`&nbsp;CONFIG&nbsp;=&nbsp;`0x4F08`&nbsp;(AUTO_SEND&nbsp;100&nbsp;Hz) | `7B 01 EB 04 A0 02 00 08 4F 6C 94 7D` |
| 12 | **Broadcast**&nbsp;`WRITE`&nbsp;`0xD010`&nbsp;×2&nbsp;=&nbsp;0&nbsp;(slave&nbsp;0) | `7B 00 EA 10 D0 02 00 00 00 DA B9 7D` |
| 13 | `READ`&nbsp;MSG_CNT,&nbsp;`0xA006`&nbsp;×1 | `7B 01 AA 06 A0 01 00 6C 14 7D` |
| 14 | `WRITE_ACK`&nbsp;ack&nbsp;messages&nbsp;0–2&nbsp;in&nbsp;one&nbsp;write | `7B 01 EB 07 A0 03 00 00 00 00 C0 F9 7D` |
| 15 | `WRITE_ACK_RESP`&nbsp;to&nbsp;#11 | `7B 01 EC 04 A0 02 00 C8 FD 7D` |
| 16 | `READ_RESP`&nbsp;to&nbsp;#13&nbsp;→&nbsp;3&nbsp;queued | `7B 01 AB 06 A0 01 00 03 B6 6F 7D` |
| 17 | `WRITE_ACK_RESP`&nbsp;to&nbsp;#14 | `7B 01 EC 07 A0 03 00 DD C1 7D` |

Note #6: `00 00 48 41` is IEEE-754 `12.5` little endian. Note #14: one write
covering three indices is the correct way to ack a run. #7 and #12 are not
answered. #12 is applied only by a device with `ACCEPT_BROADCAST_D000` 1
(library 1.1), which reports `STATUS` `0x7F01`, not #4's `0x3F01`; the
device of these vectors refuses it, silently (code 3), and stores nothing.
The answers (#2, #4, #6, #9, #10, #15 to #17) are the device's decoder's to
build; the library's encoder builds the requests and a `READ_RESP` from the
device's own registers, and refuses `WRITE_ACK_RESP` and `ERROR_RESP` (code
2). Sent to a device, #9 and #10 are refused, silently (code 2).

---

## Device API

The device's state is an `evre_base_t` (`struct evre_base`). The 1.0 name
`base_t` is kept as an alias, and so is the 1.0 spelling `SALVE_ID_REG` (1.1
adds `SLAVE_ID_REG`, the same byte), so existing code compiles unchanged, but
for the one name of 1.0 that is gone: the struct tag. Code that writes
`struct protocol_base` writes `base_t` now (`protocol_base` without `struct`
still compiles; see [Migrating from 1.0](#migrating-from-10)).

```c
uint8_t protocolInit(evre_base_t *device);
uint8_t protocolConfigure(evre_base_t *device);   /* weak; override per device */

uint8_t decodePacket(evre_base_t *device, uint8_t *PACKET, uint16_t pSize,
                     uint8_t **RESPONSE, uint16_t *rSize);
uint8_t encodePacket(evre_base_t *device, uint8_t slaveId, uint8_t fnCode,
                     uint16_t regOffset, uint16_t regCount, uint8_t *pData,
                     uint8_t **PACKET, uint16_t *pSize);

uint8_t addMsg(evre_base_t *device, uint8_t MSG);
uint16_t GetCrc16(const uint8_t *pData, int nLength);
```

`decodePacket` handles a received frame, applies it, and builds the response
(including `ERROR_RESP`). `encodePacket` builds a request, or a `READ_RESP`
for unsolicited sending. Both allocate; the caller frees.

How much (library 1.1): `decodePacket` allocates 11 bytes, enough for every
answer but a `READ_RESP`. Only a `READ` that is ours (10 bytes, our slave id,
good delimiters and CRC) gets 10 + its count, one bank (`0x1000`) at most, so
line noise or another slave's frame never costs more. If the heap cannot hold
that, it takes 11 bytes after all, and the host still hears why:
`BUFFER_TOO_SMALL` for a `READ` that passes the library's checks, its own code
(5 for a `READ` past the bank) for one that fails them. With not even 11 bytes
it returns `MEM_ALLOCATION_FAILED` and decodes nothing. `encodePacket`
allocates the frame it builds (10 bytes for a `READ`, 10 + count for the
others) before it checks the request.

### The encoder

`encodePacket` and `encodePacketInto` build a `READ`, `WRITE` or `WRITE_ACK`
(a request), or a `READ_RESP` (the device's own frame). The answers are the
decoder's to build: `WRITE_ACK_RESP`, `ERROR_RESP` and any other code get
code 2.

- It checks the request against the caller's own `evre_base_t`, as the
  decoder would: a host gives its mirror the device's map, and for an ack the
  device's `MSG_CNT` (a mirror learns it from a `READ` of `0xA006`).
- With `pData` set it copies the data from there; `pData` may already lie in
  `outBuf`, at `DATAx(0)`. With `pData` `nullptr` it takes them from the
  caller's own registers: that is how a device streams (`AUTO_SEND`).
- It asks no handler and sets no `HEARTBEAT`: the device decides what it
  sends.
- A `READ_RESP` it builds from the registers carries `STATUS` bit 14 as
  `ACCEPT_BROADCAST_D000` says, as the decoder's answer does (library 1.1);
  a mirror's carries the bit its device reported.
- It builds no broadcast but a `WRITE` (library 1.1), and builds that one
  into either bank.

### No-heap use

Identical behaviour, caller-supplied buffer, **no allocation at all** — safe
to call from an interrupt:

```c
uint8_t decodePacketInto(evre_base_t *device, uint8_t *PACKET, uint16_t pSize,
                         uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);
uint8_t encodePacketInto(evre_base_t *device, uint8_t slaveId, uint8_t fnCode,
                         uint16_t regOffset, uint16_t regCount, uint8_t *pData,
                         uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);
```

`*outLen` is the number of bytes written. It is set to `0` first, on every
call (library 1.1), and stays `0` when the function produces no response, so
`if (len) transmit(...)` never sends an old answer again. `outLen` must not be
null. Returns `BUFFER_TOO_SMALL` if `outMax` is not enough; the room is
checked before a handler is asked (library 1.1), so a handler acts only on a
frame that can be answered. The allocating forms above are thin wrappers around
these. Build with `-ffunction-sections -fdata-sections` and link with
`-Wl,--gc-sections` (the STM32CubeIDE default): then the allocating forms, and
the heap with them, leave the image. The decoder needs about 120 bytes of
stack on a Cortex-M7 (`-Os`, with what it calls), plus what a handler uses.

Decoding straight into the transmit buffer removes both the allocation and the
copy:

```c
uint16_t len = 0;
decodePacketInto(&dev, rx, rxLen, txBuf, sizeof(txBuf), &len);
if (len) transmit(txBuf, len);
```

**In place.** The frame and the answer may even share one buffer
(`outBuf == PACKET`), for every function code (library 1.1), if it holds the
larger of the two, and 11 bytes at least for an `ERROR_RESP`. The header is
read first, a refused frame changes nothing, and a `WRITE_ACK` is answered only
after its data are stored. (1.0 built the ack first, and in place stored its
CRC and end byte as the first three data bytes.)

### Message API

```c
addMsg(&dev, MY_MSG_RAIL_FAULT);        /* queue an event */
dev.MSG_ACK_HANDLER[MY_MSG_RAIL_FAULT] = on_rail_fault_acked;   /* optional */
```

`addMsg` returns `NO_ERROR`, `MSG_NULL` for the code 0, or `MSG_BUFFER_FULL`
when 255 are queued. `MSG_ACK_HANDLER[0]` runs once when a host clears the
queue (any write that covers `MSG_CNT`), `MSG_ACK_HANDLER[code]` when it
acknowledges that code.

The queue has two writers: `addMsg`, and the decoder when a host acknowledges
or clears. They must not preempt each other: `addMsg` takes two steps (the
code, then the count), and an ack in between loses a message or queues one
twice. A device that calls `addMsg` from its main loop while it decodes in an
interrupt defines `EVRE_LOCK()` and `EVRE_UNLOCK()` for the whole build
(library 1.1, empty by default). The library then takes the lock around
`addMsg`'s body and around the decoder's clear and compaction of the queue.
It never nests the two, and never runs a handler, or any other code, while it
holds the lock, so a lock that saves the interrupt mask in a local variable
works. The longest hold is the compaction of a full queue: a loop over up to
255 slots and a memset of up to 255 bytes, a few thousand cycles (a few
microseconds on a Cortex-M7 at 480 MHz, tens of microseconds on a Cortex-M0
at 48 MHz). `EVRe.cpp` must see the two macros: define them on the command
line, or in a header given to the compiler with `-include`. With CMSIS on a
Cortex-M, in that header (after the device's CMSIS header):

```c
#define EVRE_LOCK()   uint32_t evre_primask = __get_PRIMASK(); __disable_irq()
#define EVRE_UNLOCK() __set_PRIMASK(evre_primask)
```

The device's own writes to `STATUS`, `CONFIG` and `MSG_*` need the same care:
mask the decoder's interrupt around them. `STATUS` too since library 1.1,
because the decoder writes its bit 14
([STATUS](#status--protocol-revision-and-capabilities)). The rule behind it:
calls on one device must not preempt each other (`decodePacket`,
`encodePacket`, `addMsg`, and the device's own writes).

A lock that masks every interrupt, as the lines above do, also covers an
`addMsg` from an interrupt that preempts the decoder. Outside the lock such a
message lands above every slot the frame being decoded can acknowledge, and a
write that clears the queue acknowledges nothing (library 1.1), so no frame
acknowledges it by mistake. An ack run leaves it queued. A clear keeps it if
it lands after the clear, and clears it with the rest if it lands before, like
any message queued before a clear. A lock that masks less covers only the
contexts it masks.

### Ranges and handlers (library 1.1)

Optional. A device that sets none of them still sees what 1.1 changes for
every device ([Migrating from 1.0](#migrating-from-10)): among them, a
`READ_RESP` sent to it is refused, a write to an unknown bank is refused
with code 3, and so is a broadcast into its device bank, unless it sets
`ACCEPT_BROADCAST_D000`.

```c
static const evre_range_t bank[] = {
    { 0xD000, sizeof(my_readonly_struct),  (uint8_t *) &my_readonly_struct,  0 },
    { 0xD000 + sizeof(my_readonly_struct), sizeof(my_readwrite_struct),
      (uint8_t *) &my_readwrite_struct, 1 },
};
dev->D_RANGES = bank;          /* in place of D000, READ_MAX and WRITE_MIN */
dev->D_RANGE_CNT = 2;

dev->READ_HANDLER  = on_read;  /* evre_read_handler_t:  uint8_t (evre_base_t *, uint16_t off, uint16_t cnt) */
dev->WRITE_HANDLER = on_write; /* evre_write_handler_t: uint8_t (evre_base_t *, uint16_t off, const uint8_t *data, uint16_t cnt) */
```

- **Ranges** are sorted by address, do not overlap, lie inside
  `0xD000..0xDFFF`, and each has memory (`base` not null) and at least one
  byte. `protocolInit()` checks the table, whatever `protocolConfigure()`
  returned. For one that fails it sets `D_RANGE_CNT` to 0, so a bad table
  serves nothing even to a caller that ignores the code: every request to the
  device bank is refused (4 for a read, 3 for a write). It returns
  `protocolConfigure()`'s own code when that failed, else
  `RANGE_TABLE_INVALID` (14) for a bad table, else `NO_ERROR`. A table of no
  ranges is valid, and serves nothing. Set the table before `protocolInit()`
  or in `protocolConfigure()`: a table set later is never checked, and the
  library relies on a checked table. `writable` is 0 for read-only, 1 for
  read-write. The members keep this order, because tables are written
  positionally; a new one would go at the end. A range costs 12 bytes on a
  32-bit MCU (24 on a 64-bit host), in flash as `const`, instead of 4 bytes
  of RAM per register byte, and a read is one `memcpy` per range.
- **The handlers.** The read handler is asked before a `READ` is answered. The
  write handler is asked before a `WRITE` or a `WRITE_ACK` is stored, and
  before a mirror stores a `READ_RESP` (it stores data too). Either bank. They
  are asked once the library's own checks have passed and there is room for
  the answer ([the order of the checks](#the-order-of-the-checks)), so a
  handler acts only on a frame that will be answered. They get the offset, the
  count and, for a write, the bytes as they came: they decide what the bytes
  mean. A refused frame, a `WRITE_ACK_RESP` and the device's own frames
  (`encodePacket`, AUTO_SEND) are never asked, nor is a broadcast into the
  device bank that `ACCEPT_BROADCAST_D000` refuses. While a handler runs,
  `RX_SLAVE_ID` holds the frame's slave id, `BROADCAST_ID` (0) for a
  broadcast: every slave heard that one. The decoder sets it just before it
  asks a handler; outside a handler it holds the id of the last frame a
  handler was asked about. Their answer:

  | The handler returns | From the read handler | From the write handler |
  |---|---|---|
  | `NO_ERROR` | answered | stored, then a `WRITE_ACK` is acknowledged |
  | `EVRE_HANDLED` (`0xFF`) | as `NO_ERROR` (1.1): answered from the bank | accepted and **not stored**: the handler took care of it (a login token, for example, is never stored); a `WRITE_ACK` is acknowledged |
  | any other code | refused with that code: nothing answered but an `ERROR_RESP`, nothing stored | the same |

  A refusal is answered like any other
  ([When a device is silent](#when-a-device-is-silent)): with its code, 1 and
  7 included (library 1.1), unless the frame is a broadcast or a response. A
  handler returns a code from `ERR_CODE_ENUM`: 3 for "no", 13
  (`LOGIN_REQUIRED`) for "log in first".
- The handlers run where `decodePacket` runs, often an interrupt: keep them
  short.
- A write to the slave id itself (a device that maps `SALVE_ID_REG` into its
  bank) is acknowledged under the old id, the one the host is listening for
  (library 1.1).
- **EVRe Guard** (`lib/guard/evre_guard.h`) is a ready pair of handlers: see
  [EVRe Guard](#evre-guard-library-11) below.
- **The reserved bank** is read and written in place: `DEVICE_ID`, `STATUS`,
  `CONFIG`, `MSG_CNT` and `MSG_BUFFER` lie one after the other in `evre_base_t`
  (the library checks it when it is compiled). No pointer table: 1 KB of RAM
  and the one allocation `protocolInit()` made are gone. `A000` is no longer
  used: it is always `nullptr`.
- **The pointer table** (`D000`): a `DEVICE_REG_READ_MAX` above `0xDFFF` is
  taken as `0xDFFF` (library 1.1): the bank ends there.

### EVRe Guard (library 1.1)

`lib/guard/evre_guard.h` is a ready pair of handlers, a layer above the
protocol: a login with a token, a lockout against brute force, an idle
logout. A device uses it or not; the protocol is the same either way.

- **The login.** Before it only `DEVICE_ID` and `STATUS` (`0xA000..0xA003`)
  and the spans in `open_reads` can be read, and only the login register
  written. A login is a write of exactly `login_size` bytes (32 at most) to
  `login_addr`. The token is compared in constant time and never stored (the
  handler answers `EVRE_HANDLED`). Log in with a `WRITE_ACK`: only its answer
  tells the host how the login went. Give each device a token of its own: a
  token seen on the link then opens one device, not all of them. Put the
  login register in a writable range of its own: in a session, a block write
  that runs over it is refused, and the session goes on.
- **The answers.** `LOGIN_REQUIRED` (13) while no session is open, to every
  other request the library itself would take (its own checks come first).
  `PERMISSION_DENIED` (3) for a login that failed, whatever the reason (a
  wrong token, a partial one, an attempt during a lockout), for a login by
  broadcast, and for every request while its config is bad
  (`evre_guard_init()` refuses the config, and the guard fails closed).
- **Brute force.** After `free_attempts` wrong tokens every attempt is
  refused, the right token too, for `lockout_ms`; each further failure doubles
  it, up to `lockout_max_ms`. A good login clears the count.
  `evre_guard_restore()` gives back a count of wrong tokens kept in
  non-volatile memory, so a reset hands out no free attempts.
- **A session** ends with `evre_guard_logout()`, a login attempt that fails,
  or `idle_logout_ms` without a read or a write.
- **No login by broadcast.** With `ACCEPT_BROADCAST_D000` 0, the default, the
  library refuses a broadcast into the device bank before the guard is asked,
  a login included ([Broadcast](#broadcast)). With 1 the guard skips a
  broadcast write that touches the login register, and it has no effect at
  all: it logs nobody in, the right token neither, counts no failure, starts
  no lockout, ends no session, counts as no activity, and its bytes are not
  stored (3, silent as every broadcast). A broadcast is never answered and can
  never log in, so it is no channel to guess a token by; and one frame cannot
  lock out, or log out, every device on the link. Every other broadcast write
  that reaches the guard needs an open session, as a unicast one does (13,
  silent), in either bank: a broadcast never goes past the guard. With 0, a
  broadcast into the device bank never gets that far (3, from the library),
  session or not.
- **The session belongs to the link**, not to a host: while a session is
  open, a broadcast from anyone on the link reaches that device, as a unicast
  write would. Behind a gateway that puts several clients on one link, the
  gateway authenticates its own clients.
- **The clock is the device's**, and has 64 bits: `now_ms` returns
  milliseconds that count up and never go back, so it never wraps. At every
  read, every write it takes up (not a skipped broadcast login) and
  `evre_guard_logged_in()` the guard looks at it: the time since the newest
  value it has seen comes off the lockout and goes onto the session's idle
  time. The looks may be any time apart, and any `uint32_t` limit works,
  `0xFFFFFFFF` too. A value not newer than the newest one seen counts as no
  time: a late read, or a clock set back, and then time stands still until
  the clock catches up. On a 32-bit MCU a timer tick adds to a 64-bit
  counter, and `now_ms` reads it without tearing: twice, until both reads
  agree, or with interrupts masked. The value must be whole from the main
  loop and from the decoder's context alike, so no reader may run in the
  middle of the tick's add. `now_ms` is also called under `EVRE_LOCK` (by
  `evre_guard_logged_in()`): if it masks interrupts, it saves and restores
  the mask, never a plain enable, and it takes no lock of its own.
- **Where to call it.** The handlers run where the decoder runs.
  `evre_guard_logged_in()`, `evre_guard_logout()` and `evre_guard_restore()`
  follow `addMsg`'s rule ([Message API](#message-api)): call them where the
  decoder runs, or from the main loop with `EVRE_LOCK()` and `EVRE_UNLOCK()`
  defined. They take the lock, because each update changes several fields,
  and a handler that ran in the middle of one would leave them out of step.

The clock, the config and the wiring, in the device:

```c
static volatile uint64_t ms64;
void tick_1ms(void) { ms64 = ms64 + 1; }   /* the 1 ms SysTick, above the decoder's priority */
static uint64_t now64(void) {
    uint64_t a, b;
    do { a = ms64; b = ms64; } while (a != b);   /* a tick between the two halves: read again */
    return a;
}

static uint8_t token[16];            /* the device's own token, filled from its own storage at start-up */
static const evre_guard_span_t open_reads[] = { { 0xD000, 4 } };   /* readable before a login */
static const evre_guard_config_t guard_config = {
    0xD010, 16, token,           /* login_addr, login_size, token */
    open_reads, 1,               /* open_reads, n_open_reads */
    3, 1000, 60000,              /* free_attempts, lockout_ms, lockout_max_ms */
    300000,                      /* idle_logout_ms */
    now64,                       /* now_ms */
};

static evre_guard_t guard;
static uint8_t onRead(evre_base_t *d, uint16_t off, uint16_t cnt) {
    (void) d;
    return evre_guard_read(&guard, off, cnt);
}
static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
    return evre_guard_write(&guard, d, off, data, cnt);  /* d as the handler got it */
}

/* at start-up, after protocolInit() */
if (evre_guard_init(&guard, &guard_config) != NO_ERROR) { /* a bad config: the guard refuses everything */ }
evre_guard_restore(&guard, saved_failures, now64());     /* optional */
dev.READ_HANDLER  = onRead;
dev.WRITE_HANDLER = onWrite;
```

`evre_guard_write` takes the device as the write handler got it: its
`RX_SLAVE_ID` tells a broadcast. A null device is refused (3). A device that
calls `evre_guard_write` from its own code, with a device whose `RX_SLAVE_ID`
is 0, has every login treated as a broadcast: that fails closed.

### A host's mirror (library 1.1)

A host that keeps a copy of a device's registers in an `evre_base_t` of its
own, and decodes the device's answers into it, sets that copy up as the device
is set up:

- `SALVE_ID_REG`: the device's id;
- the same map (`DEVICE_REG_READ_MAX` and `DEVICE_REG_WRITE_MIN`, or ranges),
  with memory behind it;
- **`ACCEPT_READ_RESP = 1`**. Without it every `READ_RESP` is refused (code
  2) and nothing is stored: the mirror never learns anything. It is 0 by
  default on purpose: a device must not let a host write its read-only
  registers with a `READ_RESP`.

Code that builds against 1.0 and 1.1 alike:

```c
#if defined(EVRE_LIB_VERSION) && EVRE_LIB_VERSION >= 0x0101
    mirror.ACCEPT_READ_RESP = 1;
#endif
```

What a mirror does with each answer:

| Frame | What the mirror does |
|---|---|
| `READ_RESP` | checks it against its own read limits (3, 4, 5), asks its write handler, then copies the bytes as they came, into either bank: `STATUS` and `CONFIG` as the device reported them (bit 14 and bit 0 too), and `MSG_CNT` and `MSG_BUFFER` raw (no ack, no handler). No `HEARTBEAT`. |
| `WRITE_ACK_RESP` | takes it as the news that the write landed: `NO_ERROR` for the reserved or the device bank, 3 for another. Only the bank is checked: the mirror's own permissions and queue say nothing about the device's. Nothing is stored, no handler is asked. |
| `ERROR_RESP` | does not decode it (code 2): the host reads the code byte, `DATAx(0)`, itself. |

None of them is ever answered. A mirror's decoder still answers a request (a
host's own frame echoed back on a half-duplex link, say): a host does not send
what its mirror builds.

**A mirror's `STATUS` is its device's** (library 1.1). The library never
refreshes a mirror's bit 14 (`CAP_BROADCAST_D000`): a `READ` the mirror
answers, an echo of the host's own `READ` say, and a `READ_RESP` it builds
from its registers carry the bit the device last reported, and the field
keeps it, whatever the mirror's own `ACCEPT_BROADCAST_D000` says. Before the
first `READ_RESP` of `STATUS` the mirror holds what `protocolInit()` left
there, bit 14 clear unless the program set it. A program that answers
clients of its own from a copy of a device's registers marks that copy a
mirror for the same reason (`ACCEPT_READ_RESP = 1`), or its clients hear bit
14 as the copy's own setting. The copy then also takes the `READ_RESP` and
`WRITE_ACK_RESP` frames a client sends it, as the table above says.

---

## Adding a device

Override the weak `protocolConfigure()`. `protocolInit()` has already set
`STATUS` and cleared `MSG_ACK_HANDLER`; the reserved bank needs no setup
(library 1.1).

```c
#include <new>

uint8_t protocolConfigure(evre_base_t *dev) {
    dev->SALVE_ID_REG = 1;
    dev->DEVICE_ID    = 0x2001;
    dev->STATUS      |= CAP_AUTO_SEND;      /* what this device adds */

    dev->DEVICE_REG_READ_MAX  = 0xD0EC;     /* last register */
    dev->DEVICE_REG_WRITE_MIN = 0xD0CC;     /* first writable one; 0, the default, makes all writable */

    /* one entry per register, 0xD000 + k in entry k. Not (READ_MAX + 1) & 0x0FFF:
     * that is 0 for a bank that runs to 0xDFFF. */
    dev->D000 = new (std::nothrow) uint8_t*[dev->DEVICE_REG_READ_MAX - 0xD000 + 1];
    if (!dev->D000) return MEM_ALLOCATION_FAILED;

    uint8_t *ro = (uint8_t *) &my_readonly_struct;   /* packed */
    for (uint16_t i = 0; i < sizeof(my_readonly_struct); ++i)
        dev->D000[i] = &ro[i];

    uint8_t *rw = (uint8_t *) &my_readwrite_struct;  /* packed */
    for (uint16_t i = 0; i < sizeof(my_readwrite_struct); ++i)
        dev->D000[sizeof(my_readonly_struct) + i] = &rw[i];

    return NO_ERROR;
}
```

**The override is a contract.** It is exactly
`uint8_t protocolConfigure(evre_base_t *device)` (`base_t *` is the same
type), at global scope, with C++ linkage, and not weak. Anything else (a
reference, a const pointer, a namespace) is another function: the weak
default then runs without a word, `SALVE_ID_REG` stays 0, and the device
answers nothing. `-Wmissing-declarations` makes the compiler say so.
`extern "C"` does not compile in a file that includes `EVRe.h`. `D000` is the
device's: the library reads and writes through it, and never allocates, frees
or moves it.

**`protocolInit()` does, in this order:** with `EVRE_CRC_TABLE_RUNTIME` 1,
builds the CRC table; sets `A000` to `nullptr`; clears
`MSG_ACK_HANDLER[0..255]`; sets `STATUS` to the protocol revision and the
library's capability bits (all but bit 14, which is set as a device's
`STATUS` goes out); calls `protocolConfigure()`, once, with the same pointer,
on the calling thread; then, if `D_RANGES` is set, checks the range table,
whatever `protocolConfigure()` returned, and sets `D_RANGE_CNT` to 0 if the
table fails. It writes nothing else. It returns `protocolConfigure()`'s code when
that is not `NO_ERROR`, else `RANGE_TABLE_INVALID` for a table that fails,
else `NO_ERROR`. So a field may be set before `protocolInit()`
(`SALVE_ID_REG`, `DEVICE_ID`, `ACCEPT_BROADCAST_D000`), in
`protocolConfigure()` (`MSG_ACK_HANDLER` entries, `STATUS |=` the device's
own bits, the range table), or after `protocolInit()` returns (anything but
the range table, which is then not checked).

Three things worth doing in a device's map:

- Back it with **packed structs** rather than scattered variables. The layout
  then *is* the wire format, and there is nothing to keep in sync.
- Order fields **widest first** (4-byte, then 2-byte, then 1-byte). Everything
  lands on its natural alignment with **no padding spent**, so a host can map a
  packed struct straight onto the response.
- **Bit-pack the booleans and small enumerations** into a few status words.
  Each register costs 4 bytes of pointer table, not one, so packing 24 flags
  into two words saves ~88 bytes of RAM as well as 22 bytes on every poll.
  (With ranges, library 1.1, the pointer table is gone; the poll saving stays.)

Writes land in the struct. Apply them from your main loop — compare against a
shadow copy and push changes through whatever validated command layer the
device already has, so the protocol cannot bypass limits that exist elsewhere.

---

## Host implementation

A complete client. No dependencies beyond `pyserial`.

```python
import struct, serial

TAB = []
for i in range(256):
    v = i
    for _ in range(8):
        v = (v >> 1) ^ 0x8408 if v & 1 else v >> 1
    TAB.append(v)

def crc16(d):
    f = 0xFFFF
    for b in d:
        f = (f >> 8) ^ TAB[(f ^ b) & 0xFF]
    return ~f & 0xFFFF

READ, READ_RESP, WRITE, WRITE_ACK, WRITE_ACK_RESP, ERROR_RESP = (
    0xAA, 0xAB, 0xEA, 0xEB, 0xEC, 0xEE)

def data_len(fn, count):
    """the data bytes a frame carries: from its function code, not its count alone"""
    if fn in (READ_RESP, WRITE, WRITE_ACK):
        return count
    return 1 if fn == ERROR_RESP else 0      # READ, WRITE_ACK_RESP: none

def build(slave, fn, offset, count, data=b''):
    body = bytes([0x7B, slave, fn,
                  offset & 0xFF, offset >> 8,
                  count & 0xFF, count >> 8]) + data
    c = crc16(body)
    return body + bytes([c & 0xFF, c >> 8, 0x7D])

class DeviceError(Exception):
    pass

class EVRe:
    def __init__(self, port, slave=1, timeout=0.05):
        self.s = serial.Serial(port, 115200, timeout=timeout)
        self.s.dtr = True
        self.slave = slave

    def _frame(self):
        head = self.s.read(7)                 # header tells us the length
        if len(head) < 7:
            raise TimeoutError("no response")
        n = data_len(head[2], head[5] | head[6] << 8)
        buf = head + self.s.read(n + 3)
        if len(buf) != n + 10 or buf[0] != 0x7B or buf[-1] != 0x7D \
                or crc16(buf[:-3]) != (buf[-3] | buf[-2] << 8):
            raise ValueError("framing")
        return buf[1], buf[2], buf[3] | buf[4] << 8, buf[5] | buf[6] << 8, buf[7:-3]

    def _txn(self, fn, offset, count, data=b'', expect=True):
        self.s.reset_input_buffer()
        self.s.write(build(self.slave, fn, offset, count, data))
        if not expect:
            return None                       # a refused WRITE's ERROR_RESP is dropped next time
        answer = READ_RESP if fn == READ else WRITE_ACK_RESP
        for _ in range(16):
            slave, rfn, roff, rcnt, rdata = self._frame()
            if (slave, roff, rcnt) != (self.slave, offset, count):
                continue                      # not ours: an AUTO_SEND frame, say
            if rfn == ERROR_RESP:
                raise DeviceError("device error %d at 0x%04X count %d" % (rdata[0], offset, count))
            if rfn == answer:
                return rdata
        raise TimeoutError("no answer")

    def read(self, offset, count):
        return self._txn(READ, offset, count)

    def write(self, offset, data, ack=True):
        return self._txn(WRITE_ACK if ack else WRITE,
                         offset, len(data), data, expect=ack)

# ---- use it
dev = EVRe("COM40")
print("DEVICE_ID 0x%04X" % struct.unpack("<H", dev.read(0xA000, 2))[0])
print("STATUS    0x%04X" % struct.unpack("<H", dev.read(0xA002, 2))[0])

block = dev.read(0xD000, 0xCC)               # the whole read-only block
value, = struct.unpack_from("<f", block, 0x0C)

dev.write(0xD0CC, struct.pack("<f", 12.5))   # a set point, acknowledged
```

Reading the 7-byte header first and then exactly its data bytes plus 3 (*n*
from the function code, see [Frame](#frame)) is the length-based framing from
above — it never scans for `0x7D`, so payload bytes that happen to equal a
delimiter cannot confuse it. Every answer is matched against its request: a
frame for another offset or count (an `AUTO_SEND` frame that came first) is
skipped, and an `ERROR_RESP` for this request raises with its code.

A host in C++ can use the library itself, with a mirror of the device:
[A host's mirror](#a-hosts-mirror-library-11).

---

## Patterns

Things that are not part of the protocol but come up in every device built on
it.

**Poll, or stream, or both.** Polling the read-only block at 50–100 Hz is
simple and costs one transaction. `AUTO_SEND` removes even that, at the price
of the host no longer controlling timing. They can be combined — a `READ`
response may arrive interleaved between two automatic frames, so **always
match the offset and count in a `READ_RESP` against what you asked for**
rather than assuming the next frame is your answer.

**Treat writes as requests, not pokes.** On the device, do not act inside the
decoder. Let writes land in the struct and apply them from the main loop by
comparing against a shadow copy, then push the change through whatever
validated command layer already exists. Limits stay in one place and the
protocol cannot bypass them.

**Host watchdog.** If a device can move something, decide what happens when
the host stops talking. A workable rule: once a host has been seen *and has
changed something*, treat every accepted request as a heartbeat
(`CONFIG.HEARTBEAT` is set on each one); after a timeout, stop the actuators
and restore every writable register to the value it held before that host
appeared. Restore through the same command layer a write uses. Be conservative
about what else you undo — a lost link is a reason to stop moving, not a reason
to power the system down. A response is not a request: since library 1.1 a
`WRITE_ACK_RESP` echoed back to a device is refused and feeds no watchdog.

**Read back what you wrote.** A value may be clamped rather than rejected. The
matching read-only register tells you what was actually applied.

---

## Pitfalls

| Pitfall | Consequence |
|---|---|
| Scanning for `0x7D` to find the frame end | payload bytes equal the delimiter; use the length |
| Expecting `10 + REG_CNT` bytes for every frame | a `READ`, a `WRITE_ACK_RESP` or an `ERROR_RESP` never completes; *n* comes from the function code |
| Two frames in one packet-link transfer | both are dropped, silently: the CRC is checked at the end of the transfer |
| One frame split across two transfers | never decoded |
| Crossing a bank boundary in one request | rejected |
| A&nbsp;write&nbsp;starting&nbsp;below&nbsp;`REG_WRITE_MIN` | rejected **in full**, even the writable part |
| `DEVICE_REG_WRITE_MIN`&nbsp;left&nbsp;at&nbsp;0 | the whole device bank is writable, the read-only block included |
| Acking messages lowest-index first | silently acks the wrong ones |
| Clearing the queue and acking slots in one write (1.1) | the slot bytes do nothing: it is the clear alone |
| A broadcast set point or stop to a 1.1 device without `ACCEPT_BROADCAST_D000` | refused, silently: nothing moves. `STATUS` bit 14 tells which devices take it |
| An EVRe Guard clock read in two halves without a retry | a torn value may jump 2^32 ms ahead: the session and the lockout end, and the guard's time then stands still until the clock catches up |
| `addMsg` from the main loop while an interrupt decodes, without `EVRE_LOCK` | a message lost or queued twice |
| `evre_guard_logged_in()`, `_logout()` or `_restore()` from the main loop while an interrupt decodes, without `EVRE_LOCK` | a lockout the interrupt just started can be cancelled, a session just opened can be closed |
| A&nbsp;host's&nbsp;mirror&nbsp;without&nbsp;`ACCEPT_READ_RESP`&nbsp;(1.1) | every `READ_RESP` refused: the mirror never learns anything |
| A program that answers its own clients' `READ`s from a copy of a device's registers, the copy not marked a mirror (1.1) | its clients hear `STATUS` bit 14 as the copy's own `ACCEPT_BROADCAST_D000`, not the device's |
| A&nbsp;range&nbsp;table&nbsp;set&nbsp;after&nbsp;`protocolInit()` | never checked |
| Assuming the low byte of an address is the register index | wrong once the map passes `0xnn FF` |
| Casting a pointer into the response buffer | misaligned unless the map is ordered widest-first |
| Assuming&nbsp;a&nbsp;write&nbsp;took&nbsp;effect | it may have been clamped — read it back |
| Using an optional feature without checking `STATUS` | works on one device, fails on the next |

---

## Conformance checklist

For a new host or device implementation:

- [ ] CRC table first eight entries match `0x0000 0x1189 0x2312 0x329B …`
- [ ] Every frame of the [test vectors](#test-vectors) is built byte-exactly: a request by the host's encoder, an answer by the device's decoder, on the device described there (#7 and #12 get none)
- [ ] CRC is computed over `len - 3`, not `len - 4`
- [ ] Multi-byte fields are little endian on the wire
- [ ] A frame is framed by `10 + n`, *n* from its function code: `READ` and `WRITE_ACK_RESP` 10 bytes, `ERROR_RESP` 11
- [ ] A frame with a corrupted CRC, or a wrong start or end byte, produces **no response**
- [ ] A frame for another slave produces **no response**
- [ ] A read starting outside the readable area of its bank produces `ERROR_RESP` code 4 (an unknown bank: 3)
- [ ] A request running past the bank's end produces `ERROR_RESP` code 5
- [ ] A write starting below `REG_WRITE_MIN`, or to an unknown bank, produces `ERROR_RESP` code 3
- [ ] A frame whose length is not `10 + n` produces code 12; an unknown function code or an `ERROR_RESP` produces code 2, whatever its length
- [ ] Slave 0 with `WRITE` into the reserved bank is applied and **not** answered
- [ ] Slave 0 with `WRITE` into the device bank is refused (code 3), stores nothing and is **not** answered; with `ACCEPT_BROADCAST_D000` 1 it is applied, and **not** answered (1.1)
- [ ] Slave 0 with `READ` is **not** answered
- [ ] A `READ_RESP` or `WRITE_ACK_RESP` sent to a device is not stored and **not** answered
- [ ] `CONFIG.HEARTBEAT` is set by the device on every accepted request, and on nothing else
- [ ] A host never writes an `AUTO_SEND` prescaler of 0; a device takes a 0 as 1 or its default
- [ ] `STATUS[7:0]` reports the protocol revision; bit 14 is set exactly when the device bank takes a broadcast (1.1)
- [ ] Acknowledging message *i* leaves every index below *i* unchanged; past `MSG_CNT` the buffer reads 0
- [ ] A write that covers `MSG_CNT` clears the queue and acknowledges nothing: `MSG_ACK_HANDLER[0]` runs once, and a message it queues is kept (1.1)

For a device on library 1.1 with ranges or handlers:

- [ ] Every frame of the checks above answers the same (ranges equivalent to the old boundary)
- [ ] A read into a gap between ranges produces code 5; one starting in a gap code 4
- [ ] A write starting in a gap or touching a read-only range produces code 3, one running into a gap code 5; nothing is stored
- [ ] A frame the handlers refuse stores nothing and is answered with the handler's code, whatever it is (1 and 7 too), unless it is a broadcast or a response
- [ ] A frame whose answer does not fit is refused (code 11) before a handler is asked
- [ ] `EVRE_HANDLED` from the write handler: accepted, acknowledged, nothing stored
- [ ] A handler sees `RX_SLAVE_ID` 0 for a broadcast and the device's id otherwise
- [ ] With EVRe Guard, no broadcast logs in, fails a login or ends a session; without a session every other write is refused (13), a broadcast one silently. With `ACCEPT_BROADCAST_D000` 0 the library refuses a broadcast into the device bank first (3), session or not

For a host:

- [ ] Every answer is matched against its request (function, offset, count); an `AUTO_SEND` frame in between is skipped
- [ ] An `ERROR_RESP` is read by its code byte, `DATAx(0)`
- [ ] A mirror kept with the library sets `ACCEPT_READ_RESP = 1` (1.1)
- [ ] `STATUS` is read before any optional feature is used; a broadcast into the device bank is relied on only where bit 14 is set (1.1)

---

## Build options

| Macro | Default | Effect |
|---|---|---|
| `EVRE_CRC_TABLE_RUNTIME` | `0` | `1` builds the CRC table in RAM at `protocolInit()`, or at the first CRC before it (library 1.1: a host that only encodes), instead of holding it in flash: **about 430 B of flash freed, 513 B of RAM spent** (the builder costs flash too). Behaviour is identical. Worth it when flash is the scarcer resource. |
| `EVRE_LOCK()`,&nbsp;`EVRE_UNLOCK()` | empty | a lock around `addMsg` and the decoder's work on the queue, for a device that calls `addMsg` from its main loop while it decodes in an interrupt (see [Message API](#message-api)), and around EVRe Guard's `evre_guard_logged_in()`, `_logout()` and `_restore()`, for a device that calls them from its main loop ([EVRe Guard](#evre-guard-library-11)). Defined for the whole build, since `EVRe.cpp` and `evre_guard.cpp` must see them. |
| `EVRE_WEAK` | `__attribute__((weak))` | how `EVRe.cpp` marks its default `protocolConfigure()` weak. Another compiler defines it before it includes `EVRe.h` (IAR and Keil: `__weak`). Never on the declaration: every override would be weak too. On MinGW (Windows) an override of a weak function is not reliable: a program there sets the fields after `protocolInit()` instead. |

`lib/ports/stm32h7/malloc_lock.c` is optional. It protects newlib's heap when
the allocating API is used from more than one priority level; with the
`*Into` functions it is unnecessary.

**Porting.** The library is C++11 and needs only the standard headers
(`stdint.h`, `stdbool.h`, `stddef.h`, `string.h`, `cstdlib`, `type_traits`).
It builds without a warning under `-Wall -Wextra` at every optimisation level.
It builds the frame's fields byte by byte, so the header and the CRC are right
on any CPU. The registers it moves as they lie in memory: `DEVICE_ID`,
`STATUS` and `CONFIG` are the CPU's own `uint16_t`, so the reserved bank is
little endian only on a little-endian CPU (every current target is one), and a
device on a big-endian CPU keeps its own registers little endian. `EVRe.h` may
come before or after `<windows.h>` (library 1.1): its `NO_ERROR` macro is set
aside around the enum.

---

## Migrating from 1.0

Library 1.1 keeps every name of 1.0 with its value but one, the struct tag,
and every 1.0 member of `evre_base_t` at its 1.0 offset. A 1.0 device or host
compiles unchanged unless it writes `struct protocol_base`. What it must know:

**The struct tag is `evre_base` now.** `struct protocol_base` is
`struct evre_base`; `base_t` and `evre_base_t` name it, and so does
`protocol_base` without `struct` (a typedef). Code that writes
`struct protocol_base`, or declares it ahead, no longer compiles: write
`base_t`. C++ has no alias for a tag, so this one cannot be kept.

**The frame index macros and the bit macros mean what they say inside an
expression.** `DATAx`, `CRC0`, `CRC1`, `END`, `getBit` and the others are
parenthesised. In 1.0 `DATAx(0) + 1` was 7 (the `+ 1` went into the `?:`); it
is 8 now. Code that uses them alone, as an index, sees no change.

**Rebuild everything that includes `EVRe.h`.** The struct tag changed,
`MSG_ACK_HANDLER` has 256 slots, and new members follow the 1.0 ones, so every
object built against 1.0 is stale. Most fail to link, which is what you want.
One that only defines `protocolConfigure()` links, and the weak default
silently takes its place.

**A host's mirror sets `ACCEPT_READ_RESP = 1`**, with the `#if` line of
[A host's mirror](#a-hosts-mirror-library-11). Without it the mirror stores
nothing.

**A device that relied on broadcasts into its device bank sets
`ACCEPT_BROADCAST_D000 = 1`** (a synchronised set point, one stop command for
all), with the `#if` line of [Broadcast](#broadcast). Without it a broadcast
reaches the reserved bank only, and one into `0xD000..0xDFFF` is refused,
silently: nothing is stored. A host tells the two apart by `STATUS` bit 14.

**`A000` is `nullptr`.** The reserved bank is served in place. A device that
wrote `A000[k]` in `protocolConfigure()`, to redirect a reserved register, now
writes through a null pointer. `delete[]` on `A000` stays harmless.

**These frames are answered differently:**

| Frame | 1.0 | 1.1 |
|---|---|---|
| a&nbsp;`READ_RESP`&nbsp;sent&nbsp;to&nbsp;a&nbsp;device | stored, the read-only registers and `DEVICE_ID` too; `HEARTBEAT` set | code 2, silent, nothing stored |
| a&nbsp;`WRITE_ACK_RESP`&nbsp;sent&nbsp;to&nbsp;a&nbsp;device | accepted,&nbsp;`HEARTBEAT`&nbsp;set | code 2, silent |
| a&nbsp;`WRITE`&nbsp;or&nbsp;`WRITE_ACK`&nbsp;to&nbsp;an&nbsp;unknown&nbsp;bank | accepted and dropped (a `WRITE_ACK` acknowledged) | `ERROR_RESP` 3 (a broadcast: code 3, silent) |
| a broadcast `WRITE` into the device bank | stored,&nbsp;`HEARTBEAT`&nbsp;set | code 3, silent: nothing stored, no handler asked, no `HEARTBEAT`. As 1.0 with `ACCEPT_BROADCAST_D000` 1 |
| one write that covers `MSG_CNT` and slots | the clear, then each slot acknowledged as the clear left it: the old codes were still there, so the handlers of the messages just discarded ran after all, and a message that `MSG_ACK_HANDLER[0]` had queued was acknowledged at once | the clear alone: `MSG_ACK_HANDLER[0]` once, a message it queues kept, the slot bytes do nothing |
| a `READ` of `STATUS` (and the device's own `READ_RESP` of it) | the&nbsp;field&nbsp;as&nbsp;it&nbsp;is | bit 14 as `ACCEPT_BROADCAST_D000` says, whatever the device wrote there; the field follows. A mirror's (`ACCEPT_READ_RESP` 1): the field as it is, as in 1.0 |
| a frame with a wrong start or end byte, its CRC good | answered | code 1, silent |
| an unknown function code with data | `ERROR_RESP`&nbsp;12 | `ERROR_RESP` 2 |
| an&nbsp;11-byte&nbsp;`ERROR_RESP` | code&nbsp;12 | code 2 (silent both times) |
| `MSG_BUFFER` past `MSG_CNT`, after an ack or a clear | the&nbsp;old&nbsp;codes | 0 |
| a&nbsp;`WRITE_ACK`&nbsp;decoded&nbsp;in&nbsp;place | the answer's CRC and end byte stored as the first three data bytes | the data stored |
| an&nbsp;ack&nbsp;of&nbsp;message&nbsp;`0xFF` | a call through the pointer past `MSG_ACK_HANDLER` | `MSG_ACK_HANDLER[0xFF]` |
| a&nbsp;mirror's&nbsp;`READ_RESP` | `HEARTBEAT`&nbsp;forced&nbsp;into&nbsp;the&nbsp;mirror's&nbsp;`CONFIG` | `CONFIG` as the device reported it |
| a&nbsp;mirror's&nbsp;`WRITE_ACK_RESP` | checked against the mirror's own write limits and queue (3 or 5) | `NO_ERROR` for either bank |
| a&nbsp;request&nbsp;past&nbsp;`0xDFFF`,&nbsp;`DEVICE_REG_READ_MAX`&nbsp;above&nbsp;it | wrapped to the table's start, or never ended | code 5: the bank ends at `0xDFFF` |
| `decodePacket`&nbsp;of&nbsp;a&nbsp;`READ`&nbsp;with&nbsp;a&nbsp;large&nbsp;count | 10 + count allocated before any check; on a small heap `MEM_ALLOCATION_FAILED`, silent | 11 bytes unless the `READ` is ours; one bank at most |

**And these calls:**

| Call | 1.0 | 1.1 |
|---|---|---|
| `decodePacketInto` or `encodePacketInto` with no device, no frame or a short one | `*outLen`&nbsp;left&nbsp;as&nbsp;it&nbsp;was | `*outLen` 0 |
| `encodePacket*`&nbsp;of&nbsp;a&nbsp;broadcast&nbsp;`READ`,&nbsp;`WRITE_ACK`&nbsp;or&nbsp;`READ_RESP` | built | code 2 |
| `encodePacket*`&nbsp;of&nbsp;a&nbsp;write&nbsp;to&nbsp;an&nbsp;unknown&nbsp;bank | built | code 3 |
| `GetCrc16`&nbsp;before&nbsp;`protocolInit()`,&nbsp;`EVRE_CRC_TABLE_RUNTIME`&nbsp;1 | wrong&nbsp;CRCs&nbsp;(an&nbsp;empty&nbsp;table) | the table built first |
| `protocolInit()` | allocated 262 pointers for `A000` (1 KB), cleared 255 ack handlers | allocates nothing, clears 256 |

**Also:** `crctab16` is one table, in `EVRe.cpp` (a device with a global
`crctab16` of its own now clashes at link time). A program that used the
header's `crctab16` alone now links `EVRe.cpp`. A loop over
`sizeof(MSG_ACK_HANDLER) / sizeof(MSG_ACK_HANDLER[0])` runs 256 times. Codes
13 (`LOGIN_REQUIRED`) and 14 (`RANGE_TABLE_INVALID`) are new; a device with
EVRe Guard sends 13. `EVRe.h` and `<windows.h>` may come in either order.
`STATUS` joins `CONFIG` and `MSG_*` among the device's own writes that must
not preempt the decoder ([Message API](#message-api)): the decoder writes its
bit 14. A 1.0 device that sets none of the new fields sees only the changes
above.

---

## Roadmap — v2

Known costs, deliberately not changed in v1 because they would break the
device-side API:

**Range-based register map.** *Done in library 1.1: for the device bank as an
option (see [Ranges](#ranges-library-11)), the pointer table staying; the
reserved bank is served in place, without a table.* Before it,
`D000[]`/`A000[]` held one pointer per register — **4 bytes of RAM for every
byte of register**. A device with 237 registers spends 948 B of pointers to
address 237 B of data. Describing the map as a few ranges instead:

```c
typedef struct { uint16_t start, len; uint8_t *base; uint8_t writable; } evre_range_t;
```

collapses that to ~56 B for a typical device, and makes a bulk read a single
`memcpy` instead of N indirect loads — so it is faster as well as smaller. It
also allows per-range permissions.

**Stable message indices.** Acknowledging currently compacts the buffer, so
indices shift (see [Messages](#messages)). Zeroing the slot in place and
compacting only when the device queues the next message would let a host ack in
any order, at the cost of redefining `MSG_CNT` as "slots in use".

---

## Credits

Protocol designed by **Ahmed Ragab AbdulGhany**. In production across 100+ devices since
2023.

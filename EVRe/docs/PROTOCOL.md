# EVRe

**Embedded Volatile Register express** — pronounced **"ever"**, because the
registers are always live.

A small single-master register protocol for embedded devices. One request
returns a whole block of mixed-type telemetry, over any byte link: UART,
USART, I²C, SPI, RS-485, USB CDC or TCP/IP.

| Property | Value |
|---|---|
| Protocol&nbsp;revision | **1** (reported in `STATUS[7:0]`) |
| Implementation | C++, two files, no dependencies beyond `stdint` / `stdlib` |
| Footprint | ~3 kB of code; heap optional |
| Status | in production across 100+ devices since 2023 |

```
host                                   device
 │   { 01 AA 00 D0 CC 00 … }            │   READ 204 registers at 0xD000
 ├─────────────────────────────────────►│
 │   { 01 AB 00 D0 CC 00 <204 B> … }    │   the device's entire state, one reply
 ◄─────────────────────────────────────┤
```

---

## Contents

1. [Why it exists](#why-it-exists)
2. [Quick start](#quick-start)
3. [Frame](#frame)
4. [Function codes](#function-codes)
5. [CRC](#crc)
6. [Register model](#register-model)
7. [Reserved bank — 0xA000](#reserved-bank--0xa000)
8. [Messages](#messages)
9. [Errors](#errors)
10. [Broadcast](#broadcast)
11. [Framing on a byte stream](#framing-on-a-byte-stream)
12. [Test vectors](#test-vectors)
13. [Device API](#device-api)
14. [Adding a device](#adding-a-device)
15. [Host implementation](#host-implementation)
16. [Patterns](#patterns)
17. [Pitfalls](#pitfalls)
18. [Conformance checklist](#conformance-checklist)
19. [Build options](#build-options)
20. [Roadmap — v2](#roadmap--v2)

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

## Quick start

**Device side** — three calls:

```c
#include "EVRe.h"

base_t dev;

uint8_t protocolConfigure(base_t *d) {      /* override the weak default */
    d->SALVE_ID_REG = 1;
    d->DEVICE_ID    = 0x2001;
    d->DEVICE_REG_READ_MAX  = 0xD0EC;
    d->DEVICE_REG_WRITE_MIN = 0xD0CC;
    d->D000 = new uint8_t*[(d->DEVICE_REG_READ_MAX + 1) & 0x0FFF];
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
Python client in ~40 lines.

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
| `5..6` | **Number of registers**, little endian |
| `7 …` | **Data**, *n* = number of registers |
| `len-3` | **CRC** low byte |
| `len-2` | **CRC** high byte |
| `len-1` | **End** — `0x7D` (`}`) |

`len = 10 + n`. Smallest frame is **10 bytes**. Every multi-byte field is
**little endian**, on the wire and in the registers.

The delimiters are `{` and `}` deliberately: a frame is recognisable at a
glance in a terminal or a logic-analyser dump.

---

## Function codes

| Code | Name | Direction | Data | Answered with |
|---|---|---|---|---|
| `0xAA` | `READ` | →&nbsp;slave | none | `READ_RESP` |
| `0xAB` | `READ_RESP` | →&nbsp;master | *n*&nbsp;bytes | — |
| `0xEA` | `WRITE` | →&nbsp;slave | *n*&nbsp;bytes | *nothing* |
| `0xEB` | `WRITE_ACK` | →&nbsp;slave | *n*&nbsp;bytes | `WRITE_ACK_RESP` |
| `0xEC` | `WRITE_ACK_RESP` | →&nbsp;master | none | — |
| `0xEE` | `ERROR_RESP` | →&nbsp;master | 1&nbsp;byte | — |

```
READ        →  ├── READ  ──────────►│
               │◄── READ_RESP ──────┤

WRITE       →  ├── WRITE ──────────►│      (nothing comes back)

WRITE_ACK   →  ├── WRITE_ACK ──────►│
               │◄── WRITE_ACK_RESP ─┤

rejected    →  ├── anything ───────►│
               │◄── ERROR_RESP ─────┤
```

`WRITE` is fire-and-forget: use it for high-rate set points where a lost
update is harmless, because the next one is already on its way.
`WRITE_ACK` is for anything that must be known to have landed.

**`WRITE_ACK_RESP` carries no status, by design.** A response at all means the
frame arrived with a valid CRC and was applied; a rejected write answers with
`ERROR_RESP` instead. There is nothing left for a status byte to say.

`READ_RESP` may also be *sent* by a device that was not asked — that is how
unsolicited streaming works.

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

---

## Register model

Two banks, selected by the **top nibble** of the address:

| Bank | Range | Contents |
|---|---|---|
| `0xA000` | `0xA000`&nbsp;–&nbsp;`0xA105` | Reserved — identical on every EVRe device |
| `0xD000` | `0xD000`&nbsp;–&nbsp;device&nbsp;max | Device registers |

Any other address is rejected. A request may **not cross a bank boundary**.

### Permissions

Each bank has **one address boundary**, not a per-register flag:

```
readable:  bank start        .. REG_READ_MAX
writable:  REG_WRITE_MIN     .. REG_READ_MAX
```

So a device's map is laid out **read-only first, then read-write**. This is
not a limitation to work around — it is what lets a host read the entire
read-only block, whatever mixture of types it contains, in one request.

A write starting below the boundary is refused **in its entirety**, even if
part of the range would have been writable.

---

## Reserved bank — 0xA000

| Address | Name | Perm | Size |
|---|---|---|---|
| `0xA000` | `DEVICE_ID` | R | 2 |
| `0xA002` | `STATUS` | R | 2 |
| `0xA004` | `CONFIG` | RW | 2 |
| `0xA006` | `MSG_CNT` | RW | 1 |
| `0xA007` | `MSG_BUFFER` | RW | 255 |

### STATUS — protocol revision and capabilities

Read it once and you know what the device supports, instead of assuming:

| Bits | Meaning |
|---|---|
| `[7:0]` | protocol revision (`1`) |
| `[8]` | `CAP_ERROR_FRAME` — answers rejects with `ERROR_RESP` |
| `[9]` | `CAP_BROADCAST` — accepts slave 0 |
| `[10]` | `CAP_MSG` — message queue implemented |
| `[11]` | `CAP_AUTO_SEND` — periodic unsolicited frames |
| `[12]` | `CAP_DFU` — firmware update entry |
| `[13]` | `CAP_STATIC` — no-heap entry points available |

This is what lets one host library talk to a mixed fleet: a device without DFU
simply reports the bit clear. **Read `STATUS` before using any optional
feature.**

### CONFIG

| 15–8 | 7–5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|
| AUTO_SEND&nbsp;prescaler | — | `DFU` | `AUTO_SEND` | `MSG_ENABLE` | `SYS_RESET` | `HEARTBEAT` |

- **`HEARTBEAT`** is set by the *device* on every accepted packet. Read it to
  confirm the link is alive.
- **`SYS_RESET`**, **`DFU`** — device actions, if advertised in `STATUS`.
- **`MSG_ENABLE`** — enables the message queue.
- **`AUTO_SEND`** — periodic unsolicited `READ_RESP` of the read-only block.

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
| `0x4F` | `0x4F08` | 100 Hz (default) |
| `0xC7` | `0xC708` | 40 Hz |

Measured on an STM32H750 at 100 / 200 / 40 Hz commanded: **100.16 / 199.87 /
39.98 Hz**.

---

## Messages

A device-to-host event queue: up to 255 one-byte codes. Enabled by
`CONFIG.MSG_ENABLE`.

- `MSG_CNT` — how many are queued.
- `MSG_BUFFER[0 .. MSG_CNT-1]` — the codes.
- Writing **any** value to `MSG_CNT` clears the queue.
- Writing to `MSG_BUFFER[i]` **acknowledges** message *i*: the device runs its
  handler, removes it and compacts the buffer.

The codes themselves are device-specific; the mechanism is not.

**The invariant that matters:**

> Acknowledging index *i* leaves every index **below** *i* unchanged, and
> shifts every index **above** *i* down by one.

That is what allows selective acknowledgement — urgent messages now, the rest
later — which is the point of a queue rather than a flags register. It also
means index order matters:

| Intent | How | |
|---|---|---|
| Ack&nbsp;everything | one&nbsp;write&nbsp;to&nbsp;`MSG_CNT` | ✔ |
| Ack&nbsp;a&nbsp;contiguous&nbsp;run | **one**&nbsp;write&nbsp;covering&nbsp;the&nbsp;range | ✔ — the device marks all, then compacts once |
| Ack&nbsp;selectively | separate writes, **highest index first** | ✔ |
| Ack&nbsp;selectively | separate writes, lowest index first | ✘ — indices shift under you |

The failing case, starting from `[0x02, 0x04, 0x0A]`:

| Write | Effect |
|---|---|
| index&nbsp;0 | acks `0x02`, compacts → `[0x04, 0x0A]` |
| index&nbsp;1 | index 1 is now `0x0A` — **wrong message acked** |
| index&nbsp;2 | beyond `MSG_CNT` — refused |

`0x04` is never acked and the host believes it was. Going high-to-low is
always correct.

---

## Errors

A rejected request is answered with `ERROR_RESP` (`0xEE`), echoing the offset
and count of the request plus one error byte:

```
7B  <slave>  EE  <off_lo> <off_hi>  <cnt_lo> <cnt_hi>  <err>  <crc_lo> <crc_hi>  7D
```

| Code | Name | Meaning |
|---|---|---|
| 0 | `NO_ERROR` | — |
| 1 | `INVALID_PACKET_ERR` | CRC mismatch |
| 2 | `FUNCTION_CODE_ERR` | unknown function code |
| 3 | `PERMISSION_DENIED` | write below `REG_WRITE_MIN`, or unknown bank |
| 4 | `REG_OFFSET_OUT_OF_RANGE` | start address outside the bank |
| 5 | `REG_CNT_OUT_OF_RANGE` | `offset + count - 1` past the bank maximum |
| 6 | `MEM_ALLOCATION_FAILED` | response buffer could not be allocated |
| 7 | `SLAVE_ID_MISMATCHED` | addressed to another slave |
| 8 | `MSG_BUFFER_FULL` | message queue full |
| 9 | `MSG_NULL` | attempt to queue message value 0 |
| 10 | `INSTANCE_IS_NULL` | internal |
| 11 | `BUFFER_TOO_SMALL` | caller's buffer too small for the response |
| 12 | `LENGTH_MISMATCH` | received length does not match the declared `REG_CNT` |

**Three cases stay silent, deliberately:**

| Case | Why |
|---|---|
| CRC&nbsp;mismatch | If the CRC is bad, the slave-ID field cannot be trusted either. Answering could put several slaves on the bus at once. |
| Slave&nbsp;ID&nbsp;mismatch | It was never ours to answer. |
| The request was itself a response | Two devices trading `ERROR_RESP` would never stop. |

So a host that times out is looking at a CRC error, a wrong address, or a dead
link — and anything else names itself.

---

## Broadcast

**Slave ID `0` addresses every slave.** No slave answers it, so only `WRITE`
is accepted; `READ` and `WRITE_ACK` would make every device transmit at once.
`0` must never be used as a real device address.

Useful for a synchronised set point across several boards in one frame, or a
single stop command that reaches every device in the same millisecond instead
of N sequential round-trips.

---

## Framing on a byte stream

On a packet link (USB, framed SPI) one frame is one transfer and there is
nothing to do.

On a raw byte stream, **frame by length, never by scanning for `0x7D`.** The
delimiters are not escaped, and `0x7B`/`0x7D` occur inside ordinary float data
all the time. The header gives you the length:

```
1. scan forward for 0x7B
2. read bytes 5..6 -> REG_CNT; expected length = 10 + REG_CNT
3. check byte[len-1] == 0x7D and verify the CRC over len-3
4. pass -> frame accepted
   fail -> advance ONE byte past that 0x7B and go to 1
```

This resynchronises after any corruption, and keeps the delimiters doing what
they are good at: making frames visible to a human.

---

## Test vectors

Real frames, slave 1, CRCs computed with the algorithm above. Use them to
validate a new implementation before touching hardware.

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
| 10 | `ERROR_RESP`, `PERMISSION_DENIED` at `0xD000` | `7B 01 EE 00 D0 01 00 03 3D 18 7D` |
| 11 | `WRITE_ACK` CONFIG = `0x4F08` (AUTO_SEND 100 Hz) | `7B 01 EB 04 A0 02 00 08 4F 6C 94 7D` |
| 12 | **Broadcast** `WRITE` `0xD010` ×2 = 0 (slave 0) | `7B 00 EA 10 D0 02 00 00 00 DA B9 7D` |
| 13 | `READ`&nbsp;MSG_CNT,&nbsp;`0xA006`&nbsp;×1 | `7B 01 AA 06 A0 01 00 6C 14 7D` |
| 14 | `WRITE_ACK` ack messages 0–2 in one write | `7B 01 EB 07 A0 03 00 00 00 00 C0 F9 7D` |

Note #6: `00 00 48 41` is IEEE-754 `12.5` little endian. Note #14: one write
covering three indices is the correct way to ack a run.

---

## Device API

```c
uint8_t protocolInit(base_t *device);
uint8_t protocolConfigure(base_t *device);   /* weak; override per device */

uint8_t decodePacket(base_t *device, uint8_t *PACKET, uint16_t pSize,
                     uint8_t **RESPONSE, uint16_t *rSize);
uint8_t encodePacket(base_t *device, uint8_t slaveId, uint8_t fnCode,
                     uint16_t regOffset, uint16_t regCount, uint8_t *pData,
                     uint8_t **PACKET, uint16_t *pSize);

uint8_t addMsg(base_t *device, uint8_t MSG);
uint16_t GetCrc16(const uint8_t *pData, int nLength);
```

`decodePacket` handles a received frame, applies it, and builds the response
(including `ERROR_RESP`). `encodePacket` builds a request, or a `READ_RESP`
for unsolicited sending. Both allocate; the caller frees.

### No-heap use

Identical behaviour, caller-supplied buffer, **no allocation at all** — safe
to call from an interrupt:

```c
uint8_t decodePacketInto(base_t *device, uint8_t *PACKET, uint16_t pSize,
                         uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);
uint8_t encodePacketInto(base_t *device, uint8_t slaveId, uint8_t fnCode,
                         uint16_t regOffset, uint16_t regCount, uint8_t *pData,
                         uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);
```

`*outLen` is the number of bytes written, `0` when the function produces no
response. Returns `BUFFER_TOO_SMALL` if `outMax` is not enough. The allocating
forms above are thin wrappers around these.

Decoding straight into the transmit buffer removes both the allocation and the
copy:

```c
uint16_t len = 0;
decodePacketInto(&dev, rx, rxLen, txBuf, sizeof(txBuf), &len);
if (len) transmit(txBuf, len);
```

### Message API

```c
addMsg(&dev, MY_MSG_RAIL_FAULT);        /* queue an event */
dev.MSG_ACK_HANDLER[MY_MSG_RAIL_FAULT] = on_rail_fault_acked;   /* optional */
```

---

## Adding a device

Override the weak `protocolConfigure()`. `protocolInit()` has already built
the reserved bank and set `STATUS`.

```c
uint8_t protocolConfigure(base_t *dev) {
    dev->SALVE_ID_REG = 1;
    dev->DEVICE_ID    = 0x2001;
    dev->STATUS      |= CAP_AUTO_SEND;      /* what this device adds */

    dev->DEVICE_REG_READ_MAX  = 0xD0EC;     /* last register */
    dev->DEVICE_REG_WRITE_MIN = 0xD0CC;     /* first writable one */

    dev->D000 = new uint8_t*[(dev->DEVICE_REG_READ_MAX + 1) & 0x0FFF];
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

Three things worth doing in a device's map:

- Back it with **packed structs** rather than scattered variables. The layout
  then *is* the wire format, and there is nothing to keep in sync.
- Order fields **widest first** (4-byte, then 2-byte, then 1-byte). Everything
  lands on its natural alignment with **no padding spent**, so a host can map a
  packed struct straight onto the response.
- **Bit-pack the booleans and small enumerations** into a few status words.
  Each register costs 4 bytes of pointer table, not one, so packing 24 flags
  into two words saves ~88 bytes of RAM as well as 22 bytes on every poll.

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

def build(slave, fn, offset, count, data=b''):
    body = bytes([0x7B, slave, fn,
                  offset & 0xFF, offset >> 8,
                  count & 0xFF, count >> 8]) + data
    c = crc16(body)
    return body + bytes([c & 0xFF, c >> 8, 0x7D])

def parse(buf):
    if len(buf) < 10 or buf[0] != 0x7B or buf[-1] != 0x7D:
        raise ValueError("framing")
    if crc16(buf[:-3]) != (buf[-3] | buf[-2] << 8):
        raise ValueError("crc")
    fn = buf[2]
    off = buf[3] | buf[4] << 8
    cnt = buf[5] | buf[6] << 8
    if fn == ERROR_RESP:
        raise RuntimeError("device error %d at 0x%04X count %d" % (buf[7], off, cnt))
    return fn, off, cnt, buf[7:-3]

class EVRe:
    def __init__(self, port, slave=1, timeout=0.05):
        self.s = serial.Serial(port, 115200, timeout=timeout)
        self.s.dtr = True
        self.slave = slave

    def _txn(self, fn, offset, count, data=b'', expect=True):
        self.s.reset_input_buffer()
        self.s.write(build(self.slave, fn, offset, count, data))
        if not expect:
            return None
        head = self.s.read(7)                 # header tells us the length
        if len(head) < 7:
            raise TimeoutError("no response")
        n = head[5] | head[6] << 8
        if head[2] == ERROR_RESP:
            n = 1
        return parse(head + self.s.read(n + 3))[3]

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

Reading the 7-byte header first and then exactly `REG_CNT + 3` more bytes is
the length-based framing from above — it never scans for `0x7D`, so payload
bytes that happen to equal a delimiter cannot confuse it.

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
changed something*, treat every accepted packet as a heartbeat
(`CONFIG.HEARTBEAT` is set on each one); after a timeout, stop the actuators
and restore every writable register to the value it held before that host
appeared. Restore through the same command layer a write uses. Be conservative
about what else you undo — a lost link is a reason to stop moving, not a reason
to power the system down.

**Read back what you wrote.** A value may be clamped rather than rejected. The
matching read-only register tells you what was actually applied.

---

## Pitfalls

| Pitfall | Consequence |
|---|---|
| Scanning for `0x7D` to find the frame end | payload bytes equal the delimiter; use the length |
| Two frames in one packet-link transfer | the second is ignored |
| One frame split across two transfers | never decoded |
| Crossing a bank boundary in one request | rejected |
| A write starting below `REG_WRITE_MIN` | rejected **in full**, even the writable part |
| Acking messages lowest-index first | silently acks the wrong ones |
| Assuming the low byte of an address is the register index | wrong once the map passes `0xnn FF` |
| Casting a pointer into the response buffer | misaligned unless the map is ordered widest-first |
| Assuming&nbsp;a&nbsp;write&nbsp;took&nbsp;effect | it may have been clamped — read it back |
| Using an optional feature without checking `STATUS` | works on one device, fails on the next |

---

## Conformance checklist

For a new host or device implementation:

- [ ] CRC table first eight entries match `0x0000 0x1189 0x2312 0x329B …`
- [ ] All 14 [test vectors](#test-vectors) encode and decode byte-exactly
- [ ] CRC is computed over `len - 3`, not `len - 4`
- [ ] Multi-byte fields are little endian on the wire
- [ ] A frame with a corrupted CRC produces **no response**
- [ ] A frame for another slave produces **no response**
- [ ] An out-of-range offset produces `ERROR_RESP` code 4
- [ ] An out-of-range count produces `ERROR_RESP` code 5
- [ ] A write below `REG_WRITE_MIN` produces `ERROR_RESP` code 3
- [ ] A frame whose length disagrees with `REG_CNT` produces code 12
- [ ] Slave 0 with `WRITE` is applied and **not** answered
- [ ] Slave 0 with `READ` is **not** answered
- [ ] `CONFIG.HEARTBEAT` is set by the device on every accepted frame
- [ ] A host never writes an `AUTO_SEND` prescaler of 0; a device takes a 0 as 1 or its default
- [ ] `STATUS[7:0]` reports the protocol revision

---

## Build options

| Macro | Default | Effect |
|---|---|---|
| `EVRE_CRC_TABLE_RUNTIME` | `0` | `1` builds the CRC table in RAM at `protocolInit()` instead of holding it in flash: **512 B of flash freed, 512 B of RAM spent**. Behaviour is identical. Worth it when flash is the scarcer resource. |

`malloc_lock.c` is optional. It protects newlib's heap when the allocating API
is used from more than one priority level; with the `*Into` functions it is
unnecessary.

---

## Roadmap — v2

Known costs, deliberately not changed in v1 because they would break the
device-side API:

**Range-based register map.** Today `D000[]`/`A000[]` hold one pointer per
register — **4 bytes of RAM for every byte of register**. A device with 237
registers spends 948 B of pointers to address 237 B of data. Describing the map
as a few ranges instead:

```c
typedef struct { uint16_t start, len; uint8_t *base; uint8_t writable; } evre_range_t;
```

collapses that to ~56 B for a typical device, and makes a bulk read a single
`memcpy` instead of N indirect loads — so it is faster as well as smaller. It
also allows per-range permissions. It changes `protocolConfigure()` for every
device, which is why it waits for v2.

**Stable message indices.** Acknowledging currently compacts the buffer, so
indices shift (see [Messages](#messages)). Zeroing the slot in place and
compacting only when the device queues the next message would let a host ack in
any order, at the cost of redefining `MSG_CNT` as "slots in use".

---

## Credits

Protocol designed by **Ahmed Ragab**. In production across 100+ devices since
2023.

# EVRe Guard part 2: register checks, the plan

Revision 2, 2026-10-05. Revision 1 was 2026-09-27. Section 0 says what revision 2 changed.

The owner confirmed every decision, G-1 to G-25 (section 11), as recommended on 2026-10-05: where a decision
lists options, the recommended one is the decision. They are D-26 to D-50 in REVIEW.md. Phase G of the work plan
(CLAUDE.md) builds part 2 on the line of the branch `evre-1.1`. Section 15 fixes what the build needs beyond the
decisions.

Numbers marked (measured) were measured with arm-none-eabi-g++ 14.3, -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16
-mfloat-abi=hard -Os -std=c++11. Numbers marked (estimate) are judgement, and the build measures them.

## 0. Revision 2: what changed, and what to decide first

The design is the one of revision 1. It was read again against the library, the Studio and the repo as they
are on 2026-10-05, and against the plan for Fast EVRe (FAST_PLAN.md). Five things changed.

1. **One recommendation is turned round: G-13.** A bad table now refuses the writes it governs, those of the
   device bank, and lets the reserved bank through. Revision 1 recommended refusing both banks. The reason: a
   table comes out of a build, so a bad one is in every device of that release. With both banks refused, none
   of them takes CONFIG any more: no DFU and no reset over EVRe, and the fix needs the debug probe on every
   unit. Refusing the device bank alone still lets no unchecked value through, which is what R6 is for.
   Sections 1, 3, 4, 5, 9, 10, 11 and 12 are changed to match.
2. **One new decision: G-25, the device's own frames and the login.** Found while planning Fast EVRe. The
   handlers are never asked about frames the device sends by itself (auto send today, fast blocks later). So a
   device with a login sends its read-only block to a host that never logged in, and goes on after an idle
   logout. Nothing in part 1's docs says that the device must stop them. It is part 1's, it costs a paragraph
   and a line of the example, and it is listed here so that it is not lost.
3. **Where the work happens.** Revision 1 was written before the repo and the cloud sessions.
   - Library 1.0 is in the repo as it is. Library 1.1 and part 1 are in the dev copy, waiting for your code
     review (changes.diff), and are not merged. Part 2 needs 1.1's write handler, so it is built in the dev
     copy and comes into the repo with 1.1 or after it.
   - The Studio's and the tools' parts (section 6) go by a branch and a pull request, as every change of the
     repo does now: a GUI check for each behaviour, STUDIO.md and the Help pages, every new text with its
     Arabic, the editor's new controls shown in both themes and both languages.
   - Section 13 is cut again along that line. It says what can start before the 1.1 merge and what cannot.
4. **Added, with no new decision:**
   - The names of the codes. The tools learn 13 with library 1.1 and 15 with part 2. Today the Studio's
     `errorName` knows 0 to 5 and 12 and shows the rest as "error 13". Decided for 1.1 and still to build: a
     name for 13, a register refused with 13 is not marked "not available" for good, and the Studio does not
     log in again by itself.
   - A gap in a writable run (G-6). A block write across it is refused, because no entry covers the gap. That
     breaks "every setting in one write" for a map with gaps. The checker now warns about such a gap and names
     the way out: declare it as a `bytes` register.
   - The Studio's check before a broadcast on a bus. It asks each device's map what the write would mean to
     that device (`broadcastRefusal`). The three new keys join that check.
   - The proof of the generator needs the Guard beside the library. Until library 1.1 is in the repo, the
     repo's CI cannot run `guard_table_test.py`. It runs locally against the dev copy and says SKIP in CI
     (sections 9 and 13).
   - The dev copy's MAP_FORMAT.md is older than the repo's. The new keys are written into the repo's, and the
     Guard paragraphs the dev copy added are carried over at the 1.1 merge.
5. **Fast EVRe beside it.** A stream's window is read-only: it gets no entry, and a write into it is refused
   like any byte no entry covers. A read of a window, if that is ever built, passes the login's read handler
   like any read. Fast EVRe does not need library 1.1, so it can be built while part 2 waits for the merge.

Seven decisions change what a device or a host sees. The other eighteen are engineering choices. All are
confirmed as recommended.

| # | Question | Recommended | Why it is yours |
|---|---|---|---|
| G-1 | refuse&nbsp;or&nbsp;clamp | refuse | the contract of every device that uses part 2 |
| G-2 | the&nbsp;codes | a&nbsp;new&nbsp;code&nbsp;15,&nbsp;VALUE_REFUSED | one line in `lib/EVRe.h`, and a number that other programs keep |
| G-5 | part&nbsp;of&nbsp;a&nbsp;number | refused&nbsp;(3) | frames that land today are refused |
| G-6 | bytes&nbsp;no&nbsp;entry&nbsp;covers | refused&nbsp;(3) | the same, and the gaps in a writable run |
| G-11 | a&nbsp;register&nbsp;the&nbsp;device&nbsp;clamps | a&nbsp;map&nbsp;key,&nbsp;`past_limits` | the map format grows |
| G-13 | a&nbsp;bad&nbsp;table | the device bank refused, the reserved bank let through (changed) | recovery over EVRe, against "refuse everything" |
| G-22 | how&nbsp;the&nbsp;firmware&nbsp;adopts&nbsp;it | observe, then clamp, then refuse register by register | the firmware's contract with its host |

The whole list, each with its options: section 11.

## 1. What part 2 is, and what it is not

Part 2 checks the values a host writes. It runs in the device, in the write handler, before the library stores
a byte. It reads a const table made from the map. For each register a host may write, the table says where it
is, its size and type, its raw limits, the values that always pass, and the bits that must be 0. The check walks
every register the frame touches and gives one verdict for the whole frame. One bad register refuses the frame,
and nothing is stored.

It is:

- refuse-only. It never clamps, never stores and never returns EVRE_HANDLED. The handler gets the bytes as
  const, and the library stores all of them or none.
- for the device bank, 0xD000..0xDFFF. The reserved bank belongs to the library and to the login.
- after the login. A host without a session gets 13 and learns nothing about the limits.
- a const table in flash and a check with no state on its path, apart from a refusal counter.
- optional. A device without it works as today.

It is not:

- part of the protocol. lib/EVRe.* learns no type, limit or name. The one line it may gain is a reserved code,
  VALUE_REFUSED 15 (G-2). The library never returns it itself, as with LOGIN_REQUIRED 13.
- a clamp. Clamping stays the device's main-loop job, as PROTOCOL.md's pattern "Treat writes as requests" says.
- a read check. The read handler cannot hide bytes: the answer is read from memory after it returns.
- state rules, rules across registers, read-only bits inside a writable register, persistence. Those stay the
  device's.
- a gate on what the device sends by itself. The handlers are never asked about the device's own frames (auto
  send, fast blocks). A device with a login stops them itself (G-25).

The layering rule: EVRe is the protocol. EVRe Guard is the device-side layer: write checks, descriptors,
limits, persistence. The map is the description. The protocol knows nothing about the data it moves.

```
+-------------------------------------------------------------+
| the map (evre-map/1): the description. Names, types, units, |
|   limits, value names, fields, login. For the tools, and    |
|   the source of the generated table.                        |
+-------------------------------------------------------------+
        |
        |  evre export MAP --to guard (EVRe Studio, the evre CLI)
        v
+-------------------------------------------------------------+
| EVRe Guard, in the device (optional)                        |
|   part 1: the login (token, lockout, idle logout)           |
|   part 2: the register checks (a const table: size, type,   |
|           raw limits, listed values, bits that must be 0)   |
+------------------------------+------------------------------+
                               |  bytes up, a verdict down
                               |  (WRITE_HANDLER, READ_HANDLER)
+------------------------------v------------------------------+
| EVRe, the protocol: frames, CRC, slave, READ / WRITE / ACK, |
|   ERROR, the reserved bank, messages, ranges of readable    |
|   and writable bytes. It never asks what the bytes mean.    |
+-------------------------------------------------------------+
```

The check flow, for a write:

```
 a frame
    |
    v
+------------------------------------------------------------+
| the library: frame, CRC, slave, function, length;          |
|   bank and ranges (3, 4, 5), room for a WRITE_ACK (11)     |
+------------------------------------------------------------+
    |  a refusal here keeps its code, and no handler is asked
    v
 WRITE_HANDLER: the device's one-line wrapper
    |
    v
 evre_guard_write_checked
    |
    +--> part 1, the login (unchanged)
    |      EVRE_HANDLED, 13 or 3 ----------------------> returned as they are
    |      NO_ERROR: a session is open
    v
 evre_guard_check_write (part 2)
    |--> no check, no device, a mirror -----------------> 3
    |--> count 0 ---------------------------------------> NO_ERROR
    |--> inside the reserved bank ----------------------> NO_ERROR (not the table's)
    |--> no good table ---------------------------------> 3
    |--> find the first register the span touches
    |--> walk each register the span touches, in address order:
    |      a byte no entry covers ----------------------> 3
    |      only part of a number register --------------> 3
    |      a bit that must be 0, NaN or Inf, a value
    |        not in a closed list, outside min..max ----> 15
    v
 NO_ERROR --> the library stores every byte, answers, sets HEARTBEAT
```

A device without a login wires evre_guard_check_write alone. The flow is the same without the login box.

## 2. The descriptor format

One entry for each register a host may write: access rw or wo, or a register with an rw or w1c field (the
Studio's hostWrites() rule). No entry means not writable. Read-only registers, gaps and the login register get
no entry.

evre_guard_desc_t, 24 B on every target, with no padding:

| Offset | Member | Type | Meaning |
|---|---|---|---|
| 0 | addr | uint16_t | the first byte, 0xD000..0xDFFF |
| 2 | size | uint16_t | bytes: 1, 2 or 4 by the type; 1..0x1000 for BYTES |
| 4 | type | uint8_t | EVRE_GUARD_U8 1, I8 2, U16 3, I16 4, U32 5, I32 6, F32 7, BYTES 8. 0 is no type, so a zeroed entry fails init |
| 5 | flags | uint8_t | EVRE_GUARD_CLOSED 0x01: only listed values pass. Any other bit: a bad table |
| 6 | n_values | uint8_t | how many values this register lists, 0..255 |
| 7 | spare1 | uint8_t | 0. Kept for a later part. Not 0: a bad table |
| 8 | first_value | uint16_t | where this register's values start in the value list |
| 10 | spare2 | uint16_t | 0. Kept for a later part. Not 0: a bad table |
| 12 | min | uint32_t | the raw low limit, as bits |
| 16 | max | uint32_t | the raw high limit, as bits |
| 20 | zero_bits | uint32_t | bits a write must leave 0; integers only; 0 means no rule |

The type member is a uint8_t, not the enum, so the entry's size never depends on the compiler's enum size.

How a limit is stored:

- unsigned: the value itself.
- signed: sign-extended to 32 bits. -1000 is 0xFFFFFC18.
- f32: its IEEE 754 bits. 24.0 is 0x41C00000.
- bytes: min = max = 0. Only the span is checked.
- A limit the map leaves out is the type's end: u8 0..0xFF, i16 0xFFFF8000..0x00007FFF, f32 -FLT_MAX..FLT_MAX
  (0xFF7FFFFF..0x7F7FFFFF). So every number takes the same compare, and no "has a limit" flag is needed.

Why bits in a uint32_t: C++11 has no designated initialisers, and a union can only be set up through its first
member. One fixed encoding, decoded by the type, works in every standard.

The value list is one const uint32_t array for the whole table. Each register's part of it is sorted strictly
ascending as uint32_t. Signed values are sign-extended, f32 values are bits, and a list never holds NaN, an
infinity or -0.0 (0x80000000). It holds the specials, which always pass, and, with CLOSED, the only values that
pass.

evre_guard_table_t: regs, values (pointers), n_regs, n_values (uint16_t). 12 B on a 32-bit MCU, 8 B on AVR.
n_regs is uint16_t: a bank can hold 4096 one-byte registers, so D_RANGE_CNT's uint8_t would be too small.

evre_guard_check_t, the device's state in RAM: table (a pointer, nullptr until a good init), refused
(uint32_t, saturates), last_addr (uint16_t), last_why (uint8_t). 12 B on a 32-bit MCU, 9 B on AVR.

The entry and the table are frozen. The generator writes them positionally, and a new member would make every
existing table warn under -Wmissing-field-initializers. So a later need goes into the spare members and the free
flag bits. An older Guard refuses a table that uses them, so a newer table on an older Guard fails closed. A
need that does not fit gets a new table beside this one, with its own call. EVRE_GUARD_TABLE_FORMAT (1) names
this layout, and a generated file checks it with #error. The state struct is set up only by init, never by an
initialiser, so it may grow at its end. Its header marks the place with a line, "later parts: new members only
below this line", as evre_base_t marks its "1.1 and later" section (R2).

What the table leaves out: names and units (the host has the map), scale and offset (limits are raw), default
(a device starts from its own state), persist (no data without a behaviour; a later part adds it as a flag bit),
the effects of action and w1c (the device's main loop).

Flash: 24 B per writable register, 4 B per listed value, 12 B per table.

## 3. The check flow

Where it runs: in WRITE_HANDLER, where the decoder runs. That is often an interrupt; in the firmware it is a
low-priority timer interrupt. It runs after the library's own checks, after the room check for a WRITE_ACK,
after the login, and before the store.

evre_guard_check_write(check, device, offset, data, count), in this order:

1. The check or the device is null: 3, in both banks. There is nothing to ask.
2. The device is a mirror (ACCEPT_READ_RESP not 0): 3 (G-14). It is checked here too, because the flag can
   change after init.
3. count 0: NO_ERROR. A write of no bytes touches no register. data then points at the CRC and is not read. This
   is part 1's rule.
4. The span lies inside the reserved bank (0xA000..0xA105): NO_ERROR, untouched. CONFIG, the queue's clear and
   the acks are the library's. A map may describe them for the tools (CONFIG's bits, say), but the generator
   skips the reserved bank, so the table never covers them.
5. There is no good table (never set up, or init refused it): 3 (G-13). Only a write to the device bank gets
   this far. So CONFIG, the queue's clear and the acks still work on a device whose table is bad: it can be
   reset, and sent into DFU, over EVRe.
6. The span is not inside 0xD000..0xDFFF (end = offset + count, in uint32_t), or data is null: 3. The library
   never hands these in, so it refuses.
7. The walk.

This is the order for G-13 (b), the recommendation since revision 2. With G-13 (a), "no good table" moves up
into step 1 and refuses both banks.

The check never reads RX_SLAVE_ID: a broadcast gets the same checks as a unicast write. Part 1 has already
handled a login by broadcast.

The walk:

```
end = offset + count                                (uint32_t)
i   = the first entry with addr + size > offset     (a halving search: 13 steps at most for 0x1000 entries)
pos = offset                                        (uint32_t)
while pos < end:
    no entry i, or regs[i].addr > pos               -> 3   (a byte no entry covers)
    a number, and regs[i].addr < pos
      or regs[i].addr + regs[i].size > end          -> 3   (only part of a number)
    a number whose value does not pass              -> 15
    pos = regs[i].addr + regs[i].size
    i = i + 1
-> NO_ERROR
```

The loop ends: i grows by one each pass, and it is a uint16_t below n_regs (at most 0x1000). The first refusal
ends the walk, so the verdict is the first bad register in address order. It goes to the diagnostics.

What the walk relies on. It ends whatever the table holds, because i grows on every pass. But its reads stay
inside the entries and the value list only for a table that passed init. So the table is const, and a device
that builds one in RAM must never change it after init. The walk reads data[0] to data[count - 1] and nothing
past them.

A number passes, one whole register, in this order:

1. Read its bytes little endian into a uint32_t, one byte at a time, by shifts on uint32_t. Never cast the data
   pointer: the bytes lie at frame byte 7, at any alignment, and an unaligned VLDR faults on a Cortex-M7.
2. (raw & zero_bits) not 0: 15. The test is on the raw bytes as read, before any sign extension, so a signed
   register's zero_bits stay inside its own width.
3. f32 with the exponent bits all ones (NaN or an infinity): 15. It is a bit test, so -ffast-math cannot remove
   it. From here on, -0.0 counts as +0.0: its bits 0x80000000 are read as 0x00000000, so a listed 0 takes it.
4. Signed types are sign-extended to 32 bits. Then the value is looked up in the register's list, as a
   uint32_t: it passes when it is there. That is a special, or a member of a closed set.
5. CLOSED, and not in the list: 15.
6. Outside min..max, compared in the type's own order: unsigned, signed, or for f32 as G-24 decides. 15.
   - G-24 (a), on the FPU: the float test is written !(v >= min && v <= max), the form that NaN fails too.
   - G-24 (b), in integers: a finite float's bits are turned into a key that sorts like the float (a negative
     one: all bits inverted; a positive one: the top bit set), and the keys are compared as uint32_t. No float
     type is used anywhere in the check.

Signed values are sign-extended from their size without an implementation-defined conversion: a value with the
top bit set is -(int32_t) (~u) - 1.

The cases:

- Several registers in one frame (a pair of set points, a block of settings): each is checked. One bad value
  refuses the whole frame, and nothing is stored. The ERROR_RESP echoes the request's offset and count, not the
  bad register. last_addr holds the bad register for the device's log.
- Part of a register: refused for a number (3). A torn float is not a value anybody wrote, and its whole value
  would need the stored bytes, which the handler does not get. A bytes register may be written in any part: a
  name or a blob is written in pieces.
- A read-only register: in a read-only range the library refuses it first (3). Inside a writable range it has
  no entry, so it gets 3 here. The library cannot see that case, and part 2 closes it.
- A value outside the limits: 15, never clamped. A register the map marks past_limits "clamp" (G-11) has the
  type's full range in its entry, so only NaN, infinities and bits that must be 0 are refused, and the device
  clamps the rest.
- A special: passes, even outside min..max, as MAP_FORMAT.md section 6 says.
- A broadcast (only with ACCEPT_BROADCAST_D000 1): the same checks. A refusal is silent, and one bad value drops
  the whole broadcast.
- Session activity: part 1 resets the idle time before the check runs, so a refused value still counts as the
  logged-in host being active (G-4).
- The main loop: the check reads only the const frame and the const table, never the device's memory. The store
  follows at once, in the same context. So a main loop that reads the block under a lock that masks the decoder
  sees a multi-register write whole, or not at all.
- The device's own writes to its memory (a restore of its settings, say) never pass the check: it guards the
  wire only.

The time bound: about log2(n_regs) + 1 steps to find the start, then one pass per register the frame touches
(at most count, and at most n_regs). Each pass does at most 8 steps in a value list.

## 4. The answers on the wire

All of this is the library's behaviour today. Part 2 only chooses the code.

| Case | What&nbsp;the&nbsp;Guard&nbsp;does | Code | Answered&nbsp;or&nbsp;silent | Stored |
|---|---|---|---|---|
| unicast WRITE_ACK, every register good | passes | 0 | WRITE_ACK_RESP | all, and HEARTBEAT is set |
| unicast WRITE, every register good | passes | 0 | silent (an accepted WRITE is never answered) | all, and HEARTBEAT is set |
| unicast WRITE or WRITE_ACK, one value bad (a limit, a closed set, a bit that must be 0, NaN or Inf) | refuses | 15 | ERROR_RESP(15), echoing the request's offset and count. A plain WRITE's refusal is answered too | none, and no HEARTBEAT |
| only&nbsp;part&nbsp;of&nbsp;a&nbsp;number&nbsp;register | refuses | 3 | ERROR_RESP(3) | none |
| a byte no entry covers (a gap, a read-only register in a writable range, past the last entry) | refuses | 3 | ERROR_RESP(3) | none |
| part of a bytes register, the rest good | passes | 0 | as&nbsp;the&nbsp;function | all |
| count&nbsp;0 | passes | 0 | as&nbsp;the&nbsp;function | nothing |
| the reserved bank (CONFIG, the queue's clear, an ack) | not&nbsp;checked | as&nbsp;today | as&nbsp;today | as today |
| with&nbsp;a&nbsp;login,&nbsp;no&nbsp;session | part 1 answers; no value is looked at | 13 | ERROR_RESP(13) | none |
| a login, or a write over the login register | part&nbsp;1&nbsp;answers | EVRE_HANDLED&nbsp;or&nbsp;3 | as&nbsp;part&nbsp;1 | as part 1 |
| broadcast (ACCEPT_BROADCAST_D000 1), every register good | passes | 0 | silent | all |
| broadcast,&nbsp;one&nbsp;register&nbsp;bad | refuses | 15&nbsp;or&nbsp;3 | silent | none, for the whole broadcast |
| a&nbsp;bad&nbsp;or&nbsp;missing&nbsp;table | refuses every write to the device bank; the reserved bank works as today (G-13 (b); with (a), both banks are refused) | 3 | ERROR_RESP(3); silent for a broadcast | none |
| a&nbsp;mirror | init refuses; every write refused | 3 | silent&nbsp;for&nbsp;a&nbsp;READ_RESP | none |
| a refused WRITE_ACK with an output buffer of exactly 10 B | refuses | 15&nbsp;or&nbsp;3 | silent (an ERROR_RESP needs 11 B; the library's room rule) | none |
| a&nbsp;READ | part&nbsp;2&nbsp;is&nbsp;not&nbsp;asked | - | as&nbsp;part&nbsp;1,&nbsp;or&nbsp;as&nbsp;today | - |
| a READ_RESP or WRITE_ACK_RESP sent to a device (ACCEPT_READ_RESP 0) | never&nbsp;asked | 2 | silent&nbsp;(a&nbsp;response) | none |
| a frame the library refuses (3, 4, 5, 11, 12) | never&nbsp;asked | as&nbsp;today | as&nbsp;today | none |

Without a login (evre_guard_check_write wired alone) the rows are the same, less the two login rows.

For a refused frame the library sets no HEARTBEAT and returns the refusal code. So a device that counts only
accepted frames does not count it either. The firmware counts the decodes that return NO_ERROR, and only those
refresh its drive hold.

## 5. The API and the wiring

New files: lib/guard/evre_guard_desc.h and lib/guard/evre_guard_desc.cpp. Part 1's files and
evre_guard_config_t do not change, apart from one pointer line in evre_guard.h's comment. The header includes
stdint.h, EVRe.h and evre_guard.h. C++11, no heap, no virtual, no templates, plain aggregates.

evre_guard_write_checked calls part 1's evre_guard_write. So a device without a login, which wires
evre_guard_check_write alone, still builds and links evre_guard.cpp, or it gets an undefined reference. With
-ffunction-sections and --gc-sections the linker drops the part 1 code it never calls. G-20 names the other
choice: evre_guard_write_checked in a small file of its own.

```cpp
#define EVRE_GUARD_TABLE_FORMAT (1U)

enum EVRE_GUARD_TYPE_ENUM {
	EVRE_GUARD_U8 = 1U, EVRE_GUARD_I8 = 2U, EVRE_GUARD_U16 = 3U, EVRE_GUARD_I16 = 4U,
	EVRE_GUARD_U32 = 5U, EVRE_GUARD_I32 = 6U, EVRE_GUARD_F32 = 7U, EVRE_GUARD_BYTES = 8U
};

enum EVRE_GUARD_FLAG_ENUM {
	EVRE_GUARD_CLOSED = 0x01U
};

/* Why the last write was refused, for the device's log. */
enum EVRE_GUARD_WHY_ENUM {
	EVRE_GUARD_WHY_NONE = 0U, EVRE_GUARD_WHY_SETUP = 1U, EVRE_GUARD_WHY_NOT_WRITABLE = 2U,
	EVRE_GUARD_WHY_PART = 3U, EVRE_GUARD_WHY_BITS = 4U, EVRE_GUARD_WHY_NOT_FINITE = 5U,
	EVRE_GUARD_WHY_NOT_LISTED = 6U, EVRE_GUARD_WHY_LIMIT = 7U
};

typedef struct {
	uint16_t addr;
	uint16_t size;
	uint8_t type;
	uint8_t flags;
	uint8_t n_values;
	uint8_t spare1;
	uint16_t first_value;
	uint16_t spare2;
	uint32_t min;
	uint32_t max;
	uint32_t zero_bits;
} evre_guard_desc_t;              /* 24 B; the member order is fixed for good */

typedef struct {
	const evre_guard_desc_t *regs;
	const uint32_t *values;
	uint16_t n_regs;
	uint16_t n_values;
} evre_guard_table_t;             /* the member order is fixed for good */

typedef struct {
	const evre_guard_table_t *table; /* nullptr until a good init: no write to the device bank passes */
	uint32_t refused;                /* writes the check refused; stops at 0xFFFFFFFF */
	uint16_t last_addr;              /* the register of the last refusal */
	uint8_t last_why;                /* EVRE_GUARD_WHY_... */
} evre_guard_check_t;

/* After protocolInit() (the ranges are checked by then). NO_ERROR, or
 * PERMISSION_DENIED for a bad table, a null pointer, a mirror, a range table
 * out of order, or an entry outside one writable range of the device. Then
 * every write to the device bank is refused. The table must stay in place,
 * unchanged, while the check is used. */
uint8_t evre_guard_check_init(evre_guard_check_t *check, const evre_guard_table_t *table, const evre_base_t *device);

/* The write handler's body for a device without a login: NO_ERROR,
 * VALUE_REFUSED or PERMISSION_DENIED, never EVRE_HANDLED. It takes no lock.
 * Call it where the decoder runs. */
uint8_t evre_guard_check_write(evre_guard_check_t *check, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count);

/* The login first (evre_guard_write), then the values, only on the login's
 * NO_ERROR. Every other answer of the login is passed on as it is. */
uint8_t evre_guard_write_checked(evre_guard_t *guard, evre_guard_check_t *check, const evre_base_t *device, uint16_t offset, const uint8_t *data, uint16_t count);

/* From the main loop: the count of refusals, and the last one's register and
 * reason, read together under EVRE_LOCK. addr and why may be nullptr. */
uint32_t evre_guard_check_last(evre_guard_check_t *check, uint16_t *addr, uint8_t *why);
```

In EVRe.h, only if G-2 says yes, one line after LOGIN_REQUIRED:

```cpp
	/* On the wire: the device's layer above did not take a value; nothing was
	 * stored. The library only reserves the code and never returns it: a
	 * handler does (EVRe Guard). */
	VALUE_REFUSED = 15U,
```

Wiring, with a login:

```cpp
static evre_guard_t guard;         /* part 1, the login */
static evre_guard_check_t check;   /* part 2, the values */

static uint8_t onRead(evre_base_t *d, uint16_t off, uint16_t cnt) {
	(void) d;
	return evre_guard_read(&guard, off, cnt);
}

/* The login comes first: without a session no value is looked at. */
static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	return evre_guard_write_checked(&guard, &check, d, off, data, cnt);
}

...
protocolInit(&dev);
if (evre_guard_init(&guard, &guard_config) != NO_ERROR) { /* a bad config: every request refused */ }
if (evre_guard_check_init(&check, &example_table, &dev) != NO_ERROR) { /* a bad table: the device bank takes no write */ }
dev.READ_HANDLER = onRead;
dev.WRITE_HANDLER = onWrite;   /* set even when a check failed: without it every write would land */
```

Without a login (the firmware's case):

```cpp
static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	return evre_guard_check_write(&check, d, off, data, cnt);
}
```

In the main loop:

```cpp
uint16_t addr;
uint8_t why;
const uint32_t refused = evre_guard_check_last(&check, &addr, &why);
```

Who writes each shared field, and under which lock (R5):

| Field | Written&nbsp;by | When, and under which lock |
|---|---|---|
| the table, the entries, the value list | nobody | const, in flash |
| check.table | evre_guard_check_init | in the main loop, before WRITE_HANDLER is set or with the decoder masked; it checks the table without the lock, then stores it and zeroes the diagnostics in one EVRE_LOCK block. On a 16-bit MCU a pointer store is two bytes, so a handler must never see half of one |
| check.refused, last_addr, last_why | evre_guard_check_write | in the decoder's context, no lock: the decoder never preempts itself |
| (a&nbsp;read&nbsp;of&nbsp;the&nbsp;three) | evre_guard_check_last | in the main loop, one EVRE_LOCK block, never nested, nothing called inside |
| device->ACCEPT_READ_RESP (read by the check on every write) | the&nbsp;device | set before the handlers are wired; one byte, so a read of it never tears |
| device->D_RANGES, D_RANGE_CNT (read by init only) | the device, and protocolInit() for D_RANGE_CNT | before init, in the main loop |
| the&nbsp;device&nbsp;bank's&nbsp;memory | the library, right after the check returns NO_ERROR | in the decoder's context; the main loop reads it under its own lock, as today |
| part&nbsp;1's&nbsp;fields | as&nbsp;today | unchanged |

A device that calls evre_guard_check_write from its own main loop (not through the decoder) holds EVRE_LOCK
around the call.

One check per decoder. The table may be shared, as it is const. But a device with two links, whose decoders
run at different priorities, gives each its own evre_guard_check_t: one decoder could preempt the other in the
middle of a diagnostics update.

What init checks. Any failure leaves check.table nullptr, and every write to the device bank is refused (G-13):

- check, table and device are not null; regs is not null; values is not null when n_values is above 0.
- The device is no mirror (ACCEPT_READ_RESP 0).
- n_regs is 1..0x1000.
- Each entry: a known type; the size matches the type; inside 0xD000..0xDFFF, with addr + size computed in
  uint32_t; sorted, with no overlap; only known flag bits; spare1 and spare2 are 0.
- Each list: first_value + n_values inside the value list (in uint32_t); strictly ascending; every value inside
  the type; no NaN, infinity or -0.0 for f32; CLOSED with an empty list is refused, as it would refuse every
  write.
- min <= max in the type's order; both inside the type; f32 limits finite.
- zero_bits inside the register's width, and 0 on f32 and bytes.
- A bytes entry has min = max = 0, no list, no flags and no zero_bits.
- D_RANGES itself is in order and without overlap. protocolInit() checks that, but a range table set after it
  returns is never checked, and the merge walk below needs a sorted list. One pass, D_RANGE_CNT steps.
- Each entry lies inside one writable range of D_RANGES (a merge walk of two sorted lists), or inside
  DEVICE_REG_WRITE_MIN..DEVICE_REG_READ_MAX (0xDFFF at most) for a device with a pointer table. An entry across
  two adjacent ranges is refused: a register lies in one block of memory. A range table protocolInit() refused
  has D_RANGE_CNT 0, so no range is writable, and init fails.

## 6. Generation from the map

What EVRe Studio and the evre CLI emit:

- A new target: `evre export MAP --to guard [--prefix P] [-o FILE]`. It writes FILE.h and FILE.cpp. It is built
  in src/model/map_export.cpp next to exportDeviceTable and reuses its helpers.
- FILE.h: the banner, the format check, the extern table, and the raw limits as typed constants for the device's
  own clamps and static_asserts. It is safe to include in any file.
- FILE.cpp: the value list, the entries and the table. A const table in a header would be copied into every file
  that includes it, so it goes in the source.
- `--check`: exports again and compares with the files on disk; exit 1 if they differ. For a device's build or
  CI, so a stale table fails the build. It works for `--to header` too.
- A device with its own memory layout (the firmware) keeps its structs by hand. It ties each writable member to
  the map with static_asserts against the `--to header` export's _ADDR and _SIZE: the address of the member
  equals the register's, and its size too. The guard file and the C header come from the same map, and
  `--check` keeps both fresh. So the device's layout, the table and the map cannot drift apart without a build
  error. Init alone catches only an entry outside a writable range, not an entry on the wrong register. The
  C header's LOGIN_ADDR and LOGIN_SIZE set part 1's config the same way.
- `--to table` keeps today's output, byte for byte. A later addition, A3 (section 13), adds `--to table
  --lib 1.1`: D_RANGES in place of the pointer table, the same entries from the same register list, and a
  static_assert per entry tying it to its image member (offsetof and sizeof). With ranges, the 1.0 rule "no
  read-only register above the first writable one" goes.

From the map to one entry:

- Which registers: every register in 0xD000..0xDFFF that hostWrites() says a host writes, in address order. The
  reserved bank is skipped, as today, though a map may describe CONFIG, MSG_CNT and MSG_BUFFER for the tools.
  The map's login register gets no entry: the check never sees a write over it. Outside the bank and overlaps
  stay export errors.
- Limits: raw = (shown - offset) / scale, with min and max swapped for a negative scale. This is keep_limits'
  rawOf(), shared, so the two cannot drift.
  - Integers: ceil(min) and floor(max), with the existing 1e-9 slack. A limit beyond the type becomes the type's
    end.
  - f32: the nearest float to the raw limit, the same float a host makes of the map's number (G-12). A limit
    beyond FLT_MAX becomes FLT_MAX, the type's end: its nearest float would be an infinity, and init refuses
    an infinite limit.
  - A limit the map leaves out: the type's end.
  - Written as hex bits, with the shown value in a comment.
- past_limits "clamp" (G-11): the type's full range in the entry. The typed constants still carry the map's
  limits.
- Specials: for an integer register each must be a whole raw value inside the type, and for f32 a finite
  value (its nearest float), or the export stops and names the register. An f32 special of -0 is listed as +0,
  because a list never holds -0.0. They go into the value list.
- closed (G-9, addition A1): the enum's raw keys and the specials, sorted, without duplicates, and CLOSED set.
  For a `write: "action"` register the idle value (its default, else 0) is always listed, so a block write that
  writes back what it read passes.
- reserved_zero (G-10, addition A2): zero_bits = the register's bits minus every field's bits.
- w1c: no limits; zero_bits only with reserved_zero.
- bytes: the size only.
- Export errors: a special that is not a whole raw value; no raw value left between min and max after rounding;
  closed without an enum; closed on f32 or bytes; reserved_zero without fields; a list over 255 values ("use min
  and max").

The map format grows by optional keys only (MAP_FORMAT.md section 2, rule 2). Old readers ignore them:

| Key | On | Values | Meaning for a device with part 2 |
|---|---|---|---|
| past_limits | a&nbsp;register | "refuse"&nbsp;(default),&nbsp;"clamp" | "clamp": the device takes any value of the type and clamps it; the Guard checks only NaN, infinities and bits that must be 0 |
| closed | a&nbsp;register | true | only the enum's values and the specials may be written (addition A1) |
| reserved_zero | a&nbsp;register&nbsp;with&nbsp;fields | true | bits no field covers must be written 0 (addition A2) |

These change together: MAP_FORMAT.md (section 4, and a new part of section 6, "What EVRe Guard checks"), the
JSON Schema, the parser and RegDef, the Studio's register editor (one choice, two check boxes), the checker, and
the C header, Markdown and Python exports that state them. CONTRIBUTING.md's rule "one device, many tools" adds
the rest: a map key is implemented the same way in the Studio, `evre`, `evre-sim` and the Python package. So
`evre-sim` answers as a device with part 2 would (15 or 3, from the same rules), the Studio and the Python
package refuse before they send what the Guard would refuse, and STUDIO.md, the help pages and CHANGELOG.md say
so.

The checker gains:

- errors: the export errors above;
- warnings: a limit outside the type (taken as the type's end); a limit that is not on a raw step (narrowed by
  ceil or floor); a special outside min..max on a register without a value list (none, once G-8 is built);
  past_limits "clamp" without min or max; enum names outside the limits of a closed register; an action register
  without a default (0 is taken as idle); past_limits, closed or reserved_zero on a register of the reserved
  bank (the table skips that bank, so the key does nothing on the device); a gap between two registers a host
  writes, with no read-only register in it (added in revision 2): a device with part 2 refuses a block write
  across it (G-6), and the message says to declare the gap as a `bytes` register if such a write must pass.

Three more places follow the same keys and codes (added in revision 2):

- The Studio's check before a broadcast on a bus. It asks each device's map what the write would mean to that
  device (`broadcastRefusal`), and now also whether that device's part 2 would refuse it.
- The names of the codes. The tools learn 13 (`LOGIN_REQUIRED`) with library 1.1 and 15 with part 2: the
  Studio's `errorName` knows 0 to 5 and 12 today and shows the rest as "error 13". A register refused with 13
  is not marked "not available" for good (only 3, 4 and 5 are), and the Studio does not log in again by
  itself. Both were decided for 1.1 and are still to build.
- The repo's own rules: a branch and a pull request, a GUI check for each behaviour, the Help pages, every new
  text with its Arabic, and the editor's new controls shown in both themes and both languages.

An example, from a neutral map with three registers:

```json
{ "addr": "0xD040", "name": "OUTPUT_V", "type": "f32", "unit": "V", "access": "rw", "min": 0, "max": 24 },
{ "addr": "0xD044", "name": "SPEED", "type": "i16", "unit": "%", "access": "rw", "scale": 0.1, "min": -100, "max": 100 },
{ "addr": "0xD046", "name": "WATCHDOG_S", "type": "u8", "unit": "s", "access": "rw", "min": 5, "max": 255, "special": { "0": "off" } }
```

example_guard.h:

```cpp
/* Generated by evre export example.json --to guard. Change the map, not this file.
 * EVRe Guard checks: size, type, limits, listed values, bits that must be 0.
 * Still the device's: state rules, rules across registers, read-only bits
 * inside a writable register, the effects of action and w1c, persistence. */
#ifndef EXAMPLE_GUARD_H
#define EXAMPLE_GUARD_H

#include "evre_guard_desc.h"

#if !defined(EVRE_GUARD_TABLE_FORMAT) || EVRE_GUARD_TABLE_FORMAT != 1
#error "this file is for EVRe Guard table format 1"
#endif

extern const evre_guard_table_t example_table;

/* The raw limits, typed, for the device's own clamps. */
constexpr float EXAMPLE_OUTPUT_V_RAW_MIN = 0.0f;
constexpr float EXAMPLE_OUTPUT_V_RAW_MAX = 24.0f;
constexpr int16_t EXAMPLE_SPEED_RAW_MIN = -1000;
constexpr int16_t EXAMPLE_SPEED_RAW_MAX = 1000;
constexpr uint8_t EXAMPLE_WATCHDOG_S_RAW_MIN = 5;
constexpr uint8_t EXAMPLE_WATCHDOG_S_RAW_MAX = 255;

#endif
```

example_guard.cpp:

```cpp
/* Generated by evre export example.json --to guard. Change the map, not this file. */
#include "example_guard.h"

static const uint32_t example_values[] = {
	0x00000000UL, /* WATCHDOG_S: 0 "off" */
};

static const evre_guard_desc_t example_regs[] = {
	/* addr, size, type, flags, n_values, spare1, first_value, spare2, min, max, zero_bits */
	{ 0xD040u, 4u, EVRE_GUARD_F32, 0u, 0u, 0u, 0u, 0u, 0x00000000UL, 0x41C00000UL, 0x00000000UL }, /* OUTPUT_V: 0 .. 24 V */
	{ 0xD044u, 2u, EVRE_GUARD_I16, 0u, 0u, 0u, 0u, 0u, 0xFFFFFC18UL, 0x000003E8UL, 0x00000000UL }, /* SPEED: -100.0 .. 100.0 %, raw -1000 .. 1000 */
	{ 0xD046u, 1u, EVRE_GUARD_U8, 0u, 1u, 0u, 0u, 0u, 0x00000005UL, 0x000000FFUL, 0x00000000UL }, /* WATCHDOG_S: 5 .. 255 s, or 0 "off" */
};

const evre_guard_table_t example_table = { example_regs, example_values, 3u, 1u };
```

A frame of the four bytes 0xD044..0xD047 would be refused with 3: 0xD047 has no entry.

## 7. The firmware's path

Each step stands on its own, is safe on its own, and ends with the host tests, the firmware's docs brought up to
date, and a bench session. Nothing is flashed without the owner's OK, and every flash follows the firmware's own
flashing rules. Drive tests only with the wheels lifted.

Where the firmware is today: it builds its own copy of the library, older than 1.1 (no handlers, no ranges, the
pointer table allocated with new). The decoder runs in a low-priority timer interrupt. The main loop applies
every setting after the store, through checked setters that clamp. Its documented contract: a value outside the
range is clamped and applied, not refused, and the result goes into its action status byte. Its map has no
machine-readable limits. Its login lives on the host program's gateway, not on the board.

- F0. Nothing in the firmware changes until part 2 is built, reviewed and merged on the library side.
- F1. Move to library 1.1, with no handlers (REVIEW.md section 9). EVRE_LOCK and EVRE_UNLOCK in the firmware's
  own lock form. Masking around its own CONFIG and STATUS writes. Two ranges (the read-only block and the
  read-write block) replace the pointer table, which gives about 1 KB of RAM back. ACCEPT_READ_RESP and
  ACCEPT_BROADCAST_D000 stay 0. The host program builds the library from the firmware's tree, so it moves in
  the same step: its mirror of the board and its per-connection copies set ACCEPT_READ_RESP 1 (REVIEW.md
  section 9), or it hears nothing. Proof: a transcript of the host program's frames before and after differs
  only in the 1.1 classes. This is the largest step. Combine it with nothing.
- F2. Numbers into the map, with no code change. min and max from the firmware's C constants: six f32 set
  points, the signed drive pair (-1000..1000), and the u8 settings (the cell count, the LED mode, the grace
  time, the command register 0..15). past_limits "clamp" on every one, except the LED mode and the command
  register, which the firmware refuses today. closed on the command register (A1): 0 named idle, and the retired
  value 6 unnamed (its note moves to notes). reserved_zero on the two bit registers (A2). Their upper bits read
  back 0 today, so a read-modify-write still passes. The table is made from a map of the board's own registers:
  the host program's gateway bank and its login go into an overlay (MAP_FORMAT.md section 7), so the board's
  table carries neither. The firmware's C limits are tied to the generated typed constants (G-23). Its 15
  read-write members are tied to the map's addresses and sizes by static_asserts against the `--to header`
  export (section 6); today it asserts 2 of the 15 by hand. Nothing on the wire changes.
- F3. Observe. The firmware's own write wrapper calls evre_guard_check_write and returns NO_ERROR anyway. That is
  the device's choice for a bench period. The Guard has no such mode, so R6 holds in the library. The wrapper
  runs in the decoder's interrupt and prints nothing. The main loop calls evre_guard_check_last and logs a line
  on the console (the register and the reason) when the count grows. Measure the handler's worst frame with the
  cycle counter against the decoder's receive window. Pass: 0 refusals over a bench run of the host program's
  normal traffic, driving included.
- F4. Enforce the shape rules. The wrapper returns the verdict. With the limits on clamp, the Guard refuses only
  what the firmware cannot mean: part of a number (3), NaN or an infinity (15), a command value it does not have
  (15), an LED mode above its last (15), a reserved bit (15). The host program already writes with WRITE_ACK and
  hands back the board's code. It learns the name of 15: no retry, a log line, and its gateway passes the code
  on to its clients as it is. A refused write never reaches the firmware's main loop, so its action status byte
  says nothing about it: the code on the wire is the answer. The firmware's protocol note gets these rows.
- F5. Register by register to refuse (G-22). The set points move from clamp to refuse after a bench session. The
  drive pair stays on clamp: a refused speed leaves the last accepted one running for as long as the host keeps
  polling, because every accepted request refreshes the drive hold. The protocol note lists which registers
  refuse, and the action status reports "clamped" only for the clamp ones.
- F6. One source of limits. The firmware takes its limit constants from the generated header, and the compare
  test goes.

The firmware's state rules stay in the firmware: a duty only in open loop, the cell count not while charging,
the drive only with the system and the motor controller on and the charger away, one rail taking another down.
Its restore of the read-write block after a host timeout writes memory directly and never meets the check.

Cost on the firmware (estimate): about 1.0 to 1.3 KB of code, about 370 to 430 B of table, 12 B of RAM, about
160 B of the decoder's stack without a login, and about 2 us for a write of the whole settings block.

Fast EVRe (FAST_PLAN.md) has a firmware part of its own. It does not need F1: its helper works with the library
the firmware has today. The two can go in either order (added in revision 2).

## 8. Cost

Flash:

- The check, evre_guard_desc.o: about 1.0 to 1.3 KB (estimate). A prototype of a similar engine, with a value
  list and bits that must be 0, compiled but never run: 516 B for the write check and 460 B for init (measured).
  About half is init's table check. Part 1 is 614 B (measured).
- The library: +0 B. VALUE_REFUSED is an enum value only.
- The table: 24 B per writable register, 4 B per listed value, 12 B per table (8 B on AVR).
- The firmware: 15 writable registers, 360 B, plus 15 values for a closed command register (60 B), plus 12 B:
  about 430 B. Without A1, about 370 B.
- On an MCU without an FPU (Cortex-M0, AVR), with G-24 (a): about 150 to 300 B more of soft-float compare from
  the runtime (estimate). With G-24 (b): none, as the check uses no float.

RAM:

- evre_guard_check_t: 12 B on a 32-bit MCU, 9 B on AVR. No counters per register, no buffers, no heap.
- Part 1 is unchanged.

Stack. The handler runs inside decodePacketInto's frame, and the store runs after it returns:

- decodePacketInto: 64 B (measured). The device's wrapper: 16 B (measured; it passes a 5th argument).
- The login path: evre_guard_write 32 B + look 12 B = 44 B (measured).
- evre_guard_check_write: about 72 to 80 B (the prototype: 72 B, plus 8 B for a helper call, measured; 56 B at
  -O2).
- evre_guard_write_checked: about 16 to 24 B (estimate).
- The whole guarded write path: without a login about 64 + 16 + 80 = 160 B; with the login about 64 + 24 + 24 +
  max(44, 80) = about 190 B (estimate). Today's guarded path is about 124 B.
- On avr-g++ -Os (16-bit int), the prototype's check: 41 B (measured).
- The suite fails if evre_guard_check_write goes above 96 B on the Cortex-M7 at -Os.
- The firmware builds its release with -Oz and -flto. Link-time inlining can merge the wrapper, the login and
  the check into fewer, larger frames, so the numbers above are measured again on that build, and at F3 on the
  board.

Time per write, Cortex-M7 at 480 MHz, code in cached flash (estimate, measured with the cycle counter at F3):

- Finding the start: about 4 steps for 15 entries.
- Each register: about 40 to 60 instructions; up to 8 more steps in a value list.
- One f32 set point: about 0.2 to 0.3 us.
- The firmware's whole 35-byte settings block (15 registers): about 1.5 to 2.5 us.
- The worst frame is one one-byte register per data byte. For the firmware, whose receive buffer is 512 B,
  that is 502 data bytes: about 40 to 60 us, and up to about 100 us with a full value list on each. On a 48 MHz
  Cortex-M0, more than 1 ms. The library takes up to 0x1000 data bytes (a whole bank), so a device with a
  receive buffer that large can meet about 8 times that. The docs state the bound per data byte, and a device
  measures its own worst frame.
- A refused frame stops at the first bad register.
- A cold instruction cache can double or triple these numbers.
- With G-24 (a) the float compare uses the FPU. On a Cortex-M4F or M7 the core adds a lazy FP state save when
  the interrupted code had FP context. With G-24 (b) the check uses no FPU. The finite test is integer only
  either way.
- Init: linear in the table's size, in the main loop only.

## 9. Tests

Everything runs on Windows and Linux through run_lib_tests.py and the Studio's tests. Nothing needs a board.

1. Feature tests, in lib_test.cpp -DFEATURES (or a new guard_desc_test.cpp the runner builds), one block per
   decision, named "D-26:" onward. Each is written first and fails on the tree before part 2, or on the
   decision's mutant. The runner counts how many fail before, as it did for D-25. They cover:
   - every integer type at min - 1, min, max, max + 1; the type's ends (INT32_MIN, UINT32_MAX, -1 as i16 against
     0xFFFF); u32 above 0x7FFFFFFF;
   - f32: quiet, signalling and negative NaN, both infinities, -0.0 against min 0, a subnormal, FLT_MAX, the
     map's max as its nearest float, and the next float above it; with G-24 (a), the subnormals again with the
     FPU's flush-to-zero on (MXCSR on a PC);
   - a special outside the limits; a closed list with named and unnamed values; a bit that must be 0;
   - past_limits "clamp": any value passes but NaN, infinities and bits that must be 0;
   - several registers: all good; one bad first, in the middle, last. The memory is compared byte for byte:
     none of it stored;
   - part of a number: the first byte, the last three bytes, a span that starts inside a register, one that ends
     inside the next; any part of a bytes register;
   - a gap; a read-only register inside a writable range; a span past the last entry; count 0; a null data
     pointer;
   - the frame at buffer offsets +1, +2 and +3, and decoding in place, so no unaligned access can hide. They run
     on a PC, where an unaligned load does not fault. So on Linux they also run under -fsanitize=undefined,
     whose alignment check catches a cast of the data pointer on a PC too. The ARM builds of the matrix only
     compile: no ARM simulator is installed;
   - the frame at the very end of a buffer of its exact size, under -fsanitize=address on Linux: the check
     reads no byte past data[count - 1];
   - zero_bits on a signed register (tested on the raw bytes, before sign extension); -0.0 against a list that
     holds 0;
   - the check alone, without a login (the firmware's wiring): every row of the wire table that has no login;
   - init and the device: a range table set after protocolInit() and out of order; an entry across two
     adjacent writable ranges; a range table protocolInit() refused (D_RANGE_CNT 0); each must fail init;
   - the reserved bank with a table: CONFIG, the queue's clear, an ack;
   - a broadcast with ACCEPT_BROADCAST_D000 1: good (stored, silent) and bad (nothing stored, silent);
   - a WRITE_ACK answered ERROR_RESP(15) and ERROR_RESP(3); a plain WRITE answered the same; no HEARTBEAT on a
     refusal; outMax 10 silent;
   - the order with the login: no session and a bad value gives 13, never 15, and the refusal count stays 0; a
     login gives EVRE_HANDLED and its register is never value-checked; a write over the login register in a
     session gives 3;
   - a refused value in a session resets the idle time;
   - a mirror: init refused; a READ_RESP silent and not stored;
   - every init rule on its own: a table that breaks one rule fails init, and then every write to the device
     bank is refused with 3, while a write of CONFIG, the queue's clear and an ack still land (G-13 (b)); a
     zeroed check; a failed re-init leaves the check refusing;
   - evre_guard_check_last: the count, the register, the reason.
   Each row of the wire table in section 4 is one named test.
2. The lock build (lock_hooks.h): evre_guard_check_write takes no lock; evre_guard_check_init and
   evre_guard_check_last take exactly one each.
3. Classifier classes. Every change of an answer, a stored byte or a code gets a name: G-VALUE (15, nothing
   stored), G-PART (3), G-UNCOVERED (3), G-SETUP (3, the device bank), G-BCAST (silent, nothing stored), G-MIRROR
   (3, silent). Each class check proves the exact line: the code, answered or silent, the echoed offset and
   count, the memory unchanged, HEARTBEAT untouched. Once confirmed, each class takes its D-number and goes into
   run_lib_tests.py's TRANSCRIPT_CLASSES and FUZZ_CLASSES, with a tag in fuzz_test.cpp, as CONTRIBUTING.md
   asks. The 1.0 transcript (12 096 frames) has no login, so a build with the login would refuse its writes
   with 13. It goes through a build with the check alone and a neutral table, against the 1.1 build without
   handlers. Every line that differs must fall in a class.
4. A differential fuzz, new: today no fuzz runs the Guard.
   - Build A (the login only) and build B (the login and the check, with a random valid table from the seed) run
     the same frames, logged in or not. Every line that differs has a class, worked out from the inputs alone.
   - Build C (the check alone, the firmware's wiring) runs the same frames against the library without
     handlers, under the same rule.
   - Every verdict of B equals an independent oracle's: a 4096-byte map of which register owns each byte, a
     linear search, and plain int64 and double compares. It is not the engine's code.
   - A property: after every accepted write, each whole register it touched holds a value its entry allows.
   - The values lean to limit - 1, limit, limit + 1, NaN patterns, 0x80000000 and 0x7F800000, and to spans that
     start or end inside a register.
   - Random bad tables (one field of a good table changed) must fail init.
   - 2 x 300 000 cases, at -O1 and -O2. The existing library fuzz stays as it is, and checks on every line that
     the library never returns 15.
5. Mutants, one per decision, run by a script. Each must fail at least one feature check and the fuzz:
   - the NaN test removed, or written as (v < min || v > max);
   - <= turned into < on max;
   - a signed register compared unsigned; no sign extension;
   - values read big endian;
   - the part-of-a-number rule removed; the uncovered-byte rule removed;
   - the list ignored (specials refused); CLOSED ignored; zero_bits ignored;
   - the reserved bank checked, so CONFIG is refused;
   - a bad table let through; init skipping each of its rules (sorting, size, flags, spare members, limits);
   - a bad table that refuses the reserved bank too (CONFIG no longer lands);
   - the check before the login (15 leaks without a session);
   - EVRE_HANDLED returned in place of NO_ERROR;
   - only the first register checked; the search off by one;
   - a mirror accepted; the mirror flag read at init only;
   - -0.0 not read as +0.0 (a listed 0 refuses it); zero_bits tested after sign extension;
   - init trusting a range table that is out of order.
6. api_compat.cpp pins every public name of part 1 (it pins none today) and part 2: every function by a
   function-pointer type, the enum values, sizeof(evre_guard_desc_t) == 24, offsetof of every member of the
   entry, the table's member order (offsets relative to a pointer's size, which differs by target),
   EVRE_GUARD_TABLE_FORMAT == 1, EVRE_GUARD_TOKEN_MAX, VALUE_REFUSED == 15. And evre_guard_config_t's member
   order, by a positional initialiser, the way devices write it.
7. The build matrix, in the suite, not in a reviewer's scratch program:
   - host g++; arm-none-eabi-g++ for Cortex-M7 (hard float) and Cortex-M0 (soft float); g++ -m32; avr-g++
     (16-bit int, 32-bit double);
   - avr-g++ has no C++ library, and EVRe.h includes <cstdlib>, EVRe.cpp <type_traits>. The review's AVR builds
     used two 3-line stand-in headers. They move into tests/, and only the AVR build sees them;
   - C++11, 14, 17, 20; -O0, -O1, -O2, -O3, -Os; and for the Cortex-M7 also -Oz with -flto, the firmware's own
     release flags, where inlining can move the stack numbers;
   - -Wall -Wextra -Wpedantic -Werror, for the library, part 1, part 2 and a generated table;
   - -fstack-usage for the new functions, against the limit in section 8;
   - on Linux, the feature tests and the fuzz also under -fsanitize=address,undefined;
   - a missing compiler fails the run, unless it is named on a skip flag, and the run prints what it skipped.
8. The generator: tests/guard_table_test.py beside device_table_test.py.
   - A neutral test map with every type, a negative scale, an offset, limits beyond the type, a special outside
     the limits, an f32 limit that is no exact float (3.65), a bytes register, a w1c register, a read-only
     register inside a writable run, a gap, past_limits "clamp", closed, reserved_zero, and an action register
     with a default.
   - It is exported, compiled with the library and the Guard on the same matrix, and driven through
     decodePacketInto: every limit at its edges (ACK, 3 or 15), the memory unchanged after a refusal.
   - The typed constants equal the entries' limits. The export's raw limits equal rawOf() for every register.
     keep_limits and the Guard agree on 10 000 random values per register, where both apply.
   - An f32 host value at exactly the shown limit passes; the next float is refused. An f32 limit beyond
     FLT_MAX gives FLT_MAX. An f32 special of -0 is listed as +0.
   - A device struct tied by static_asserts to the `--to header` export builds; one member moved by a byte
     fails the build.
   - Each export error fires on its bad map. `--check` gives its exit codes. `--to table` equals a golden file
     byte for byte.
   - map_test for each new checker message, schema_test for the new keys, cli_test for the new flags,
     sim_test for evre-sim's answers of 15 and 3, the Python package's tests for its refusals.
   - Where it runs (added in revision 2): the test needs the Guard beside the library. Until library 1.1 is in
     the repo it takes the dev copy's (`EVRE_LIB`, the way device_table_test.py finds its library) and says
     SKIP where there is none, CI included. So it runs locally, on Windows and Linux, before every merge that
     touches the generator.
9. Docs checks: PROTOCOL.md's wiring example is extracted and compiled; the error table is compared with EVRe.h;
   the new map keys are compared with the schema; the sizes and stack numbers in the docs equal the ones the
   script measured.
10. The Studio (added in revision 2), in gui_test.cpp, a check for each behaviour: the editor's choice and its
    two boxes; the refusals before a send; the broadcast check with the new keys; the names of 13 and 15. The
    new texts in Arabic, and pictures of the new controls in both themes.

## 10. Risks

- The contract changes for a device that clamps today (the firmware). A value it clamped and applied is refused.
  Mitigation: clamp first (F4), then refuse register by register (F5). The protocol note and the host program
  change in the same step.
- Drive: a refused motion frame leaves the last accepted command running while the host keeps polling, because
  accepted polls refresh the drive hold. Keep the drive pair on clamp. The host program treats 15 on a drive write
  as stop.
- Frames that land today may be refused: one byte of a float, or a block write over bytes the map does not
  describe. Search the host program and the tools for such writes before F4. The firmware's read-write block is
  contiguous, so a whole-block write stays legal.
- The map becomes safety-relevant. A wrong limit refuses real writes, or lets a bad one through. Review the map
  like code. The typed constants tie the firmware's own limits to it.
- f32 at the edge: a host that converts shown to raw another way (in float, or truncating) can be refused by one
  ulp at the exact limit. Every tool uses the nearest float, MAP_FORMAT.md states the rule, and the edges are
  tested.
- Integer limits that are not on a raw step are narrowed by ceil and floor. A host that rounds to nearest can be
  refused at the edge. The checker warns.
- Stack: the decoder's path grows from about 124 B to about 160 to 190 B. Fine on a Cortex-M7. A small MCU must
  budget it. The suite holds a limit.
- Time in the interrupt: bounded, but it grows with the frame and the lists. About 40 to 100 us for the worst
  frame on a Cortex-M7, more than 1 ms on a 48 MHz Cortex-M0. The firmware's frames take about 2 us. The docs
  state the bound, and a device measures its own.
- With G-24 (a), the FPU in the decoder's interrupt: a lazy FP save on a Cortex-M4F or M7; soft float on an M0
  or AVR, with 150 to 300 B more code. And a device that runs its FPU with flush-to-zero (the FZ bit on a
  Cortex-M; -ffast-math turns it on for a program on a PC) reads a subnormal as 0: a negative subnormal then
  passes a min of 0, and the check no longer equals the oracle. G-24 (b) has none of these.
- A stale table: the map changed and nobody exported again. `--check` in the device's build catches it. Init
  against the ranges catches a table that does not fit, not a wrong limit.
- A device whose own structs drift from the map: the table then checks the wrong bytes, and init cannot see it.
  The static_asserts against the `--to header` export catch it (section 6). The firmware asserts 2 of its 15
  read-write members today; F2 adds the rest.
- Two sources of limits in the firmware until F6. The compare test (G-23) is required, not optional.
- A bad table. With G-13 (b) the device refuses every write to its bank and still takes CONFIG, so it can be
  reset and updated over EVRe. With (a) it would refuse the reserved bank too: no DFU and no reset over EVRe
  until the debug probe is on the unit, for every device of that release. Either way it is found at the first
  start on the bench, if the device reports init's answer loudly.
- Part 2 inherits library 1.1's schedule (added in revision 2). Until 1.1 is merged it lives in the dev copy
  only, and the generator's proof runs locally, not in CI (section 9).
- The device's own frames (added in revision 2). A device with a login that sends by itself (auto send, fast
  blocks) sends to a host that never logged in, unless the device stops them (G-25).
- A gap in a writable run (added in revision 2). A block write across it is refused (G-6), which breaks "every
  setting in one write" for a map with gaps. The checker warns, and a `bytes` register over the gap is the way
  out.
- Tools that do not know 15 show "unknown error": EVRe Studio, evre, evre-sim, the Python package, the fake
  devices and the host program learn it in the same step.
- The order with the login: a device that chains by hand in the wrong order lets a host without a session probe
  the limits (15 against 13). evre_guard_write_checked, the header's example and a mutant cover the library's
  side only.
- The check sees only the frame. Read-only bits in a writable register, state rules and rules across registers
  are not checked, by design. The banner and the docs say so, or a device author will assume the Guard covers
  them.
- A refused broadcast is silent. One bad value drops a set point for every device, and no host sees it.
- On a 16-bit MCU a pointer store is not atomic. Init under EVRE_LOCK prevents a torn table pointer, but only if
  the device follows the rule.
- avr-g++ has no C++ standard library, so the Guard's own includes are C headers only (stdint.h, string.h,
  and float.h with G-24 (a)). The library it includes already needs two stand-in headers there (<cstdlib>,
  <type_traits>), and the suite carries them. A careless include breaks the AVR build. The matrix catches it.
- A gateway that keeps a mirror of a device (the host program does) cannot use part 2 on its clients' writes:
  with G-14 (a) init refuses a mirror. Such a gateway passes the device's 15 on, and the tools check the map's
  limits before they send.
- The proof (the oracle fuzz, the matrix in the suite, the mutants) is the part most likely to slip past the
  estimate.
- F1, the firmware's move to 1.1, is the largest single step on the firmware's side. It needs its own plan
  (REVIEW.md section 9).

## 11. Owner decisions

Each has options and a recommendation. Once confirmed, each becomes a D-number, from D-26 on.

All confirmed as recommended (2026-10-05). The short form, then each in full, with the options that were
weighed. Section 0 names the seven that change what a device or a host sees.

| # | Question | Recommended |
|---|---|---|
| G-1 | a&nbsp;value&nbsp;outside&nbsp;the&nbsp;limits | refused, never clamped |
| G-2 | the&nbsp;codes | a new 15, VALUE_REFUSED, for a value; 3 for a place |
| G-3 | where&nbsp;the&nbsp;check&nbsp;sits | a unit of its own, and one function that puts the login first |
| G-4 | a&nbsp;refused&nbsp;value&nbsp;and&nbsp;the&nbsp;session | it counts as activity |
| G-5 | part&nbsp;of&nbsp;a&nbsp;register | refused (3) for a number; any part of a bytes register may be written |
| G-6 | bytes&nbsp;no&nbsp;entry&nbsp;covers | refused (3) |
| G-7 | NaN&nbsp;and&nbsp;the&nbsp;infinities | always refused (15) |
| G-8 | a&nbsp;special&nbsp;outside&nbsp;the&nbsp;limits | it passes: a value list for each register |
| G-9 | a&nbsp;closed&nbsp;set&nbsp;of&nbsp;values&nbsp;(A1) | a map key, `closed` |
| G-10 | bits&nbsp;no&nbsp;field&nbsp;covers&nbsp;(A2) | a map key, `reserved_zero` |
| G-11 | a&nbsp;register&nbsp;the&nbsp;device&nbsp;clamps | a map key, `past_limits` |
| G-12 | limits&nbsp;into&nbsp;raw&nbsp;values | ceil and floor for integers, the nearest float for f32 |
| G-13 | a&nbsp;bad&nbsp;or&nbsp;missing&nbsp;table | the device bank refused, the reserved bank let through (changed in revision 2) |
| G-14 | mirrors | refused |
| G-15 | broadcasts | the same check; one bad value drops the whole broadcast |
| G-16 | init&nbsp;and&nbsp;the&nbsp;device | init checks the table against the device's ranges |
| G-17 | the&nbsp;entry's&nbsp;format | 24 bytes, frozen, with spare members |
| G-18 | diagnostics | a count, the last register and the reason |
| G-19 | what&nbsp;stays&nbsp;out | all of its list |
| G-20 | names&nbsp;and&nbsp;files | `lib/guard/evre_guard_desc.*` |
| G-21 | the&nbsp;generator | a new `--to guard`, with `--check` |
| G-22 | the&nbsp;firmware's&nbsp;adoption | observe, then clamp, then refuse register by register |
| G-23 | the&nbsp;firmware's&nbsp;limits | tied to the generated constants |
| G-24 | comparing&nbsp;f32 | in integers |
| G-25 | the device's own frames and the login (part 1, new in revision 2) | the device stops them without a session |

1. **G-1 Refuse or clamp a value outside the limits?**
   Options: (a) the Guard refuses. It never stores and never returns EVRE_HANDLED, and the device's own clamps
   and state checks stay behind it as the second line. (b) the Guard clamps. It would have to store the frame
   itself and return EVRE_HANDLED: a storage layer, and a reserved-bank write would lose its CONFIG store, its
   clear or its acks.
   Recommendation: (a). A register that must keep a clamp contract uses G-11.
2. **G-2 Which codes?**
   Options: (a) a new VALUE_REFUSED = 15, reserved by the library and never returned by it, as 13 is. 15 for a
   whole register holding a value the device does not take (a limit, a closed set, a bit that must be 0, NaN or
   Inf). 3 for a byte that cannot be written this way (no entry, part of a number) and for a bad setup. (b) 3
   for everything, with no library change. (c) as (a), but 15 for part of a number too. Other names:
   BAD_VALUE, VALUE_OUT_OF_RANGE (too narrow: it covers closed sets and NaN too). Never 4, 5 or 12: they are
   address and length codes.
   Recommendation: (a). One enum line, one error-table row, one pin. A host can tell "not writable here" (3)
   from "not this value" (15). 3 is about where, 15 about what.
3. **G-3 Where does the check sit, and who fixes the order with the login?**
   Options: (a) its own unit, evre_guard_desc, plus evre_guard_write_checked(): the login first, the values only
   on its NO_ERROR, the order fixed in the library. evre_guard_check_write alone for a device without a login.
   (b) each device chains the two in its own wrapper. (c) inside evre_guard_write, through a setter: part 1
   changes.
   Recommendation: (a). Part 1 and its tests stay as reviewed, and the order cannot be got wrong by a device
   that uses the library's function.
4. **G-4 Does a write the values refuse count as session activity?**
   Options: (a) yes: part 1 resets the idle time first, as it already does for a refused write over the login
   register in a session. (b) no: that needs a part 1 change, or a value check before the session check, which
   leaks limits.
   Recommendation: (a).
5. **G-5 A write that covers only part of a register?**
   Options: (a) refused (3) for numbers; a bytes register may be written in any part. (b) refused for every
   type. (c) accepted without a check, as the firmware does today.
   Recommendation: (a). A torn number cannot be checked without the stored bytes. A name or a blob is written in
   pieces.
6. **G-6 Bytes of the device bank no entry covers (a gap, a read-only register inside a writable range, past the
   last entry)?**
   Options: (a) refused (3). (b) allowed: only described registers are checked.
   Recommendation: (a). It fails closed, and it closes the one read-only hole the library cannot see.
7. **G-7 NaN and the infinities in an f32 register?**
   Options: (a) always refused (15), with or without limits, by a bit test. (b) refused only when the map gives
   a limit. (c) a map key decides.
   Recommendation: (a). No host means NaN as a set point, JSON cannot name NaN as a special, and the bit test
   survives -ffast-math.
8. **G-8 A special value that lies outside min..max?**
   Options: (a) a sorted value list per register: a special always passes, as MAP_FORMAT.md section 6 says. 4 B
   per value. (b) the export refuses such a map, and there is no list.
   Recommendation: (a). "0 = off" below a minimum is a common pattern, and the list also carries G-9.
9. **G-9 Can a register's values be a closed set? (addition A1)**
   Options: (a) a new optional key, "closed": true, on a register: only its enum values and specials may be
   written; an action register's idle value is always listed. (b) enums stay open, and only limits are checked.
   (c) every enum is closed: that changes what old maps mean, which the growth rule forbids.
   Recommendation: (a), on registers only. A closed field can come later in the spare members.
10. **G-10 Bits no field covers? (addition A2)**
    Options: (a) a new optional key, "reserved_zero": true: they must be written 0 (15). (b) not checked. (c)
    always 0 when a register has fields: that changes old maps.
    Recommendation: (a). It is one AND, and the slot is in the entry anyway.
11. **G-11 A register the device clamps?**
    Options: (a) a new optional key, "past_limits": "refuse" (the default) or "clamp". A clamp register's entry
    gets the type's full range, so only NaN, infinities and bits that must be 0 are refused; the device clamps
    with the generated typed constants. (b) no key: a device that clamps leaves min and max out of its map.
    Recommendation: (a). The map keeps its limits for the tools, says what the device really does, and lets a
    device move one register at a time. It costs the Guard no code.
12. **G-12 How are limits turned into raw values?**
    Options: (a) integers: ceil(min) and floor(max) with the existing 1e-9 slack, a limit beyond the type becomes
    the type's end; f32: the nearest float, the one a host makes of the map's number, and a limit beyond FLT_MAX
    becomes FLT_MAX. (b) as (a), but f32 rounded outward: float(limit) always passes, and a value one ulp beyond
    may too.
    Recommendation: (a), with the rule written into MAP_FORMAT.md and tested at the edges.
13. **G-13 Scope, and a bad or missing table?** (the recommendation changed in revision 2)
    The table covers 0xD000..0xDFFF only, and a good table lets the reserved bank through untouched. Options for
    a bad table: (a) every write refused (3), both banks, reads untouched, as part 1's bad config does. (b) only
    device-bank writes refused, so CONFIG (DFU, reset) and the acks keep working.
    Recommendation: (b). A table is const and comes out of a build, so a bad one is in every device of that
    release. With (a) none of them takes CONFIG any more: no DFU and no reset over EVRe, and the fix needs the
    debug probe on each unit. With (b) the device still refuses every value it cannot check, which is what R6
    asks ("every write the table governs"), and it can still be updated. Part 1's bad config is another case:
    without a good config nobody can log in, so refusing everything is the only safe answer there. Take (a) if
    you want the two parts to fail the same way. Revision 1 recommended (a) for that reason.
14. **G-14 Mirrors (ACCEPT_READ_RESP 1)?**
    Options: (a) init refuses a mirror, and a write is refused (3) whenever the flag is set. (b) skip the check
    on a mirror. (c) the library passes the function code to the handlers, a library change.
    Recommendation: (a). The checks are for devices, and a mirror that wires them by mistake fails at start.
    It also means a gateway that keeps a mirror (the host program does) cannot check its clients' writes with
    part 2. It passes the device's answer on instead.
15. **G-15 Broadcasts with ACCEPT_BROADCAST_D000 1?**
    Options: (a) the same check: one bad value drops the whole broadcast, silently. (b) with a table attached,
    refuse every broadcast into the device bank.
    Recommendation: (a), with a note in PROTOCOL.md.
16. **G-16 Does init check the table against the device?**
    Options: (a) evre_guard_check_init takes the device: it refuses a mirror, a range table out of order (one set
    after protocolInit() was never checked), and any entry that is not inside one writable range (D_RANGES, or
    WRITE_MIN..READ_MAX for a pointer table); an entry across two adjacent ranges is refused too. (b) the table
    alone.
    Recommendation: (a). A table that does not fit fails at start, not on the first write. It does not catch an
    entry on the wrong register inside a writable range: the device's static_asserts do (G-21).
17. **G-17 The descriptor format?**
    Options: (a) the 24 B entry of section 2, the same on every target, with spare members and unknown flag bits
    that must be 0; limits as uint32_t bits, a missing limit taken as the type's end; the entry and the table
    frozen; EVRE_GUARD_TABLE_FORMAT 1 checked by #error. (b) as (a), with HAS_MIN and HAS_MAX flags in place of
    the type's ends. (c) a 16 B entry with no list: a later list needs a second table.
    Recommendation: (a). Room for the next parts without a new layout, and an older Guard refuses a newer table.
18. **G-18 Diagnostics?**
    Options: (a) a refusal count, the last refused register and its reason: 12 B of RAM, read with
    evre_guard_check_last under EVRE_LOCK. Every refusal counts, one for a missing table too (reason SETUP, the
    frame's offset as the register), unless the check pointer itself is null. (b) a count only. (c) none: the
    check keeps no state.
    Recommendation: (a). The wire says only 15 or 3 for the whole frame, and the device needs the register and
    the reason for its log. The firmware's observe step (F3) depends on it.
19. **G-19 What stays out of part 2?**
    The list: reads (wo is a hint for hosts, and the read handler cannot hide bytes), read-only fields inside a
    writable register, the meaning of w1c, the effects of an action, closed fields, persist and default, limits
    changed at run time, state rules, a map fingerprint in a register. Options: (a) all of these out. (b) as (a),
    but persist carried as a flag now.
    Recommendation: (a). No data goes in without a behaviour and a test. Each can come later in the spare room.
20. **G-20 Names and files?**
    Options: (a) lib/guard/evre_guard_desc.h/.cpp; evre_guard_desc_t, evre_guard_table_t, evre_guard_check_t,
    evre_guard_check_init, evre_guard_check_write, evre_guard_check_last, evre_guard_write_checked,
    EVRE_GUARD_U8 and the rest. (b) a family of its own, evre_desc_*. And where evre_guard_write_checked lives:
    (i) in evre_guard_desc.cpp, so a device without a login still builds evre_guard.cpp (--gc-sections drops it);
    (ii) in a small file of its own, lib/guard/evre_guard_checked.cpp, which only a device with a login builds.
    Recommendation: (a) with (i). It is EVRe Guard, and one file less; the firmware's build already drops
    unused sections.
21. **G-21 What does the generator emit?**
    Options: (a) a new `evre export MAP --to guard`: FILE.h and FILE.cpp, with the typed constants, and
    `--check`, for `--to header` too. `--to table` stays byte for byte. Later, addition A3: `--to table --lib 1.1`
    with ranges and the same entries tied to the images by static_asserts. A device with its own memory layout
    ties its structs to the map by static_asserts against the `--to header` export's _ADDR and _SIZE. (b) only
    inside `--to table`. (c) one header, all inline: the table would be copied into every file that includes
    it.
    Recommendation: (a). A device with its own memory layout (the firmware) needs only the table, plus the C
    header it may already use.
22. **G-22 (the firmware) How does it adopt part 2, and which registers refuse?**
    Options: (a) observe (F3), then enforce with every limit on clamp (F4), then refuse register by register
    (F5); the drive pair stays on clamp. (b) refuse on every register at once. (c) no limits for the Guard at all,
    only the shape rules.
    Recommendation: (a).
23. **G-23 (the firmware) Where do its limits live?**
    Options: (a) a host test compares its C constants with the generated typed constants, until F6. (b)
    static_asserts in the firmware against the generated header, where its constants are constexpr. (c) take
    them from the generated header at once.
    Recommendation: (b) where it can, (a) for the rest, then (c) at F6.
24. **G-24 How does the check compare f32 values with min and max?**
    NaN and the infinities are already refused by a bit test (G-7), and -0.0 is read as +0.0. Options: (a) on the
    FPU: the bits are copied into a float (memcpy), and the test is !(v >= min && v <= max). (b) in integers:
    the bits of a finite float become a key that sorts like the float (a negative one: all bits inverted; a
    positive one: the top bit set), and the keys are compared as uint32_t. The limits and init's min <= max test
    use the same key.
    Recommendation: (b). The answer is exact on every target, whatever the FPU's flush-to-zero mode. No FPU
    state is saved in the decoder's interrupt, and no soft-float code comes in on a Cortex-M0 or AVR. It is one
    small function with a comment that says why. It is a known trick, so the fuzz's oracle, which compares in
    double, proves it equals the float compare. Take (a) if you want the plain float test in the code.
25. **G-25 (part 1, new in revision 2) The device's own frames and the login?**
    The handlers are asked about requests. They are never asked about frames the device sends by itself: auto
    send today, fast blocks later (FAST_PLAN.md). So a device with a login sends its read-only block to a host
    that never logged in, and goes on after an idle logout. Options: (a) a rule, in PROTOCOL.md's EVRe Guard
    section and in the header: a device with a login sends by itself only while `evre_guard_logged_in()` says
    a session is open, and stops its streams when the session ends; one line in the wiring example; a test
    that the example holds it. (b) as (a), with a function named for it, `evre_guard_may_send()`. (c) nothing:
    what a device sends by itself is its own business.
    Recommendation: (a). It costs no code in the Guard, and (b) would be a second name for the same answer.
    `evre_guard_logged_in()` is called under part 1's rule (where the decoder runs, or with EVRE_LOCK). A host
    that only listens does not keep a session open: the idle time runs on, so the host goes on asking, as the
    Studio does with its read of CONFIG every 100 ms. It is part 1's, and it can go in before part 2 is built.

## 12. Build rules

The build and every reviewer check these item by item.

- **R1 The layering rule.** Types, limits and names live only in lib/guard/evre_guard_desc.* and the generated
  files. lib/EVRe.* gains at most the VALUE_REFUSED line (G-2). The check reads only frame facts (the offset,
  the count, the bytes) and public device fields (ACCEPT_READ_RESP; D_RANGES and D_RANGE_CNT at init). Checked
  by: the diff of lib/EVRe.* is that line and its comment; a grep of lib/EVRe.* finds no evre_guard_ name; the
  library fuzz proves the library never returns 15.
- **R2 The public API only grows.** New names only; part 1 and evre_guard_config_t unchanged; enums with written
  values; the entry and the table frozen, growth into the spare members and flag bits; evre_guard_check_t grows
  only below its marked "later parts" line. Checked by: api_compat.cpp pins every Guard name of parts 1 and 2
  (signatures, values, sizeof, offsetof, evre_guard_config_t's member order); the 9 positional configs and the
  PROTOCOL.md example still build with -Werror.
- **R3 C++11, no heap, no recursion, no goto, a small stack.** Loops over a uint16_t index with i < n; every
  address sum, offset + count included, in uint32_t; every shift on a uint32_t with UL literals, so a 16-bit int
  never overflows; values built byte by byte, so the check is the same on a little- and a big-endian CPU; with
  G-24 (a), floats by memcpy from a uint32_t with static_assert(sizeof(float) == 4); C headers only; no buffer
  copies, so no memmove. Checked by: the matrix; a grep for casts of the data pointer and for goto; nm on
  evre_guard_desc.o finds no malloc, calloc, free, new or delete; -fcallgraph-info shows no function that
  reaches itself; -fstack-usage shows every new function "static" and under the limit; the mutant "values read
  big endian" (no big-endian CPU is in the matrix).
- **R4 Readable code.** One small function per job (readLE, toSigned, isFinite, listed, inLimits, valueOk,
  firstAt, walk, one per init rule). Readable lower-case locals, comments that say why, the owner's voice, LF,
  UTF-8, tabs as in the files, ASCII only in comments, diagrams only with + - | < > ^ v, never the section sign.
  Checked by: review, and a scan for non-ASCII bytes and CR.
- **R5 Interrupt-safe.** The table is const. The handler writes only the diagnostics, in the decoder's context,
  and reads ACCEPT_READ_RESP, one byte the device sets before it wires the handlers. Init and
  evre_guard_check_last take EVRE_LOCK once each, never nested, nothing called inside. One check per decoder.
  The table in section 5 says who writes what. Checked by: the lock build (lock_hooks.h) and review against that
  table.
- **R6 Fail closed.** A null pointer, a missing or bad table, a mirror, a range table out of order, an entry
  outside one writable range, an impossible offset: every write the table governs is refused (the device bank
  with G-13 (b), both banks with (a)). The check never returns EVRE_HANDLED, and NaN is refused by a bit test
  before any compare. Checked by: a feature test per init rule, the bad-table fuzz, and the mutants "a bad
  table let through", "init trusting a range table out of order" and "EVRE_HANDLED returned".
- **R7 Tests first-class.** A feature test per behaviour that fails before and passes after; a named class per
  wire change, in TRANSCRIPT_CLASSES and FUZZ_CLASSES; the Guard in a differential fuzz, with the login (build
  B) and without it (build C); a mutant per decision; Windows and Linux; 0 warnings with -Wall -Wextra
  -Wpedantic on the matrix of section 9 (-O0..-O3, -Os, C++11..C++20, g++, arm-none-eabi-g++, -m32, avr-g++);
  on Linux also clean under the address and undefined-behaviour sanitizers. Checked by: run_lib_tests.py and the
  Studio's tests, with the counts in REVIEW.md.
- **R8 Docs with the code.** PROTOCOL.md (the Layers diagram, the error table, EVRe Guard's section with a
  "Register checks" part and the wire table, Patterns, Pitfalls: "it may have been clamped" gains "or refused
  with 15"), the headers' comments, MAP_FORMAT.md, the schema, STUDIO.md and the help pages, CHANGELOG.md and
  REVIEW.md change in the same step as the code. Every statement is checked by a program. No behaviour is built
  before its D-number is confirmed. Checked by: the docs checks of section 9, and the D-numbers in REVIEW.md.
- **R9 Open-source hygiene.** No product, company or person's name (the author line excepted); "the firmware"
  and "the host program". The examples and tests use a neutral map, and the generated banner takes the map's
  device string, so no product map enters the open-source tree. This plan and REVIEW.md are working files of
  the dev copy: section 7 speaks of the firmware in general words, and the scan runs over every file before it
  is published. Checked by: a word scan before every publish, with its word list kept outside the tree.

In the repo, the Studio's and the tools' parts also follow the repo's own rules (CLAUDE.md, added in revision
2): a branch and a pull request for each step; a GUI check for each behaviour, with the check count updated;
the Help pages; every new text through `tr()`, with its Arabic; the rules for what a person sees, in both
themes and both languages; no new library.

## 13. Phases and effort

A session is one working block, like the earlier review rounds. All of it is an estimate. Revision 2 cut the
phases again by where the work happens: the dev copy (library 1.1 and the Guard, local) or the repo (the
Studio and the tools, a branch and a pull request each).

| Phase | Where | Content | Effort |
|---|---|---|---|
| P0&nbsp;Decisions | the&nbsp;owner | done on 2026-10-05: G-1 to G-25 as recommended; they are D-26 to D-50 in REVIEW.md | done |
| P1&nbsp;The&nbsp;check | the&nbsp;dev&nbsp;copy | VALUE_REFUSED in EVRe.h with its error-table row and pin; evre_guard_desc.h/.cpp (about 300 to 400 lines of .cpp and 200 to 250 of header, mostly comments); the feature tests first (about 500 to 700 lines); the api pins for parts 1 and 2; the lock build; PROTOCOL.md and the headers in the same step | 2 sessions |
| P2&nbsp;The&nbsp;proof | the&nbsp;dev&nbsp;copy | the Guard fuzz with its oracle, builds B and C (about 400 to 500 lines), the classes and the transcript build, the mutant script, the build matrix with the AVR stand-in headers, the -Oz -flto row, the sanitizers on Linux and the stack limit in run_lib_tests.py (about 200 to 300 lines) | 2 to 2.5 sessions |
| P3a&nbsp;The&nbsp;keys&nbsp;and&nbsp;the&nbsp;hosts | the&nbsp;repo | past_limits in MAP_FORMAT.md, the schema, the model, the checker (with the warning for a gap in a writable run) and the editor; the hosts refuse before they send: the Studio, its broadcast check, `evre`, the Python package; `evre-sim --strict` answers 15 and 3; the names of 13 and 15; STUDIO.md, the help pages, the Arabic, CHANGELOG.md. It needs nothing of library 1.1 | 1 to 1.5 sessions |
| P3b&nbsp;The&nbsp;generator | the&nbsp;repo | `--to guard`, `--check` (for `--to header` too) and the typed constants (about 250 to 350 lines); guard_table_test.py on the matrix (about 250 to 350 lines), proven against the dev copy until library 1.1 is in the repo | 1 session |
| P4&nbsp;Additions&nbsp;A1&nbsp;and&nbsp;A2 | both | closed and reserved_zero: the keys, the checker, the editor, the check's two rules, their tests and mutants. Cut them, and nothing in the format changes | 1 session |
| P5&nbsp;Addition&nbsp;A3 | the&nbsp;repo | `--to table --lib 1.1`: ranges, the entries tied to the images, the setup; the 1.0 golden file | 1 to 1.5 sessions, or later |
| P6&nbsp;Review | both | review round 1 and its fixes, then round 2 | 1.5 to 2 sessions |
| G-25&nbsp;The&nbsp;device's&nbsp;own&nbsp;frames | the&nbsp;dev&nbsp;copy | the rule in PROTOCOL.md and in the header, the line in the example, its test. It is part 1's and can go in at any time | 0.25 session |
| F1..F6&nbsp;The&nbsp;firmware | its&nbsp;own&nbsp;session | 1.1 first (1 to 2), then the map and the wiring (0.5 to 1), observe and enforce (1), refuse per register and one source of limits (0.5) | 3 to 4.5 sessions, plus 3 to 4 bench sessions |

In total:

- The library and the tools, the core (P0, P1, P2, P3a, P3b, P6): about 8 to 10 sessions.
- With the additions A1 to A3 (P4, P5): about 10 to 12 sessions.
- The firmware: about 3 to 4.5 sessions, plus the bench sessions.
- All of it: about 13 to 17 sessions, plus 3 to 4 bench sessions. The core with the firmware: about 11 to 14.

The completeness pass (section 14) added about one session to the core: evre-sim and the Python package, build
C of the fuzz, the sanitizers, the AVR stand-ins and the -Oz -flto row.

What can start when (added in revision 2):

- With your answers: P1 and P2 in the dev copy, and P3a in the repo. P3a does not touch the device's side.
- P3b can be written at any time. Its proof runs locally until library 1.1 is in the repo, and in CI from then.
- The merge of library 1.1 is the gate for the rest: for part 2 to reach the repo, for CI to run the library's
  suite, and for the firmware's F1. It waits for your review of changes.diff.
- Fast EVRe (FAST_PLAN.md) waits for none of this: it is built on the line of `main`.
- Since then library 1.1 is on a branch of the repo, and every part is built there: section 15.

## 14. How this plan was made

Three designs were drawn up and scored against the layering rule (all three pass it), simplicity, interrupt
cost, fit with the firmware and the generator, testability and the owner's style.

- The lean design is the base: a refuse-only check in its own unit, part 1 untouched, the device bank only,
  never EVRE_HANDLED, and its build-rules section.
- From the generated design: the 24 B entry with a value list and bits that must be 0, past_limits "clamp", the
  typed constants, `--check`, the format #error, the staged firmware path, `--to table` kept byte for byte, the
  split of 3 (where) and 15 (what), and the only measured numbers.
- From the robust design: evre_guard_write_checked, init against the device, the diagnostics under EVRE_LOCK,
  the wire table as named tests, the oracle fuzz, the firmware's observe step, the -0.0 rule, and the action
  idle value in a closed list.
- New here: explicit spare members, so the entry is 24 B on every target and can grow without a new layout.

Then a completeness pass read the plan against the library, part 1, PROTOCOL.md, MAP_FORMAT.md,
CONTRIBUTING.md, the tests, the generator and the firmware, and filled what was missing:

- Wrong or loose: the map may describe the reserved bank (the firmware's does), so section 3 no longer says it
  does not; the worst frame is per data byte, 502 for the firmware and up to 0x1000 elsewhere, not "any
  device"; the 1.0 transcript has no login, so its Guard build is the check alone against no handlers; the
  firmware counts decodes that return NO_ERROR, not HEARTBEAT; the host program already writes with WRITE_ACK;
  the F3 log line comes from the main loop, not the interrupt; the Guard's AVR build needs the library's two
  stand-in headers.
- Added: init checks the range table's order itself and wants each entry in one range; zero_bits on the raw
  bytes; -0.0 against a listed 0; f32 limits past FLT_MAX and a -0 special; the device's structs tied to the
  map by static_asserts; the host program moving with F1; one check per decoder; the shared fields the check
  reads; evre-sim and the Python package; build C of the fuzz; the sanitizers; the -Oz -flto row; the new
  rows, tests, mutants and risks for all of these; R2's marker line, R3's checks for heap, recursion and
  endianness, R4's diagram rule, R8's Pitfalls, STUDIO.md and CHANGELOG.md.
- A new decision, G-24: how f32 values are compared. The integer key it recommends matched the float compare
  on 19.9 million random finite pairs, subnormals and both zeros included, with 0 mismatches (a scratch
  program, g++ on a PC).

Revision 2 (2026-10-05) read the plan again against the code as it is now: the library and the Guard in the
dev copy; the Studio's master, engine and `errorName`, its bus and its broadcast check; MAP_FORMAT.md in both
copies; the repo's rules; and the plan for Fast EVRe. What it changed is in section 0. What it checked and
left as it was: the entry's 24 bytes and their offsets; the walk and its bounds; the order of the login and
the check; the other rows of the wire table; the integer key for f32; the generator's rounding rules.

## 15. Fixed for the build

What phase G of the work plan (CLAUDE.md) needs beyond the decisions. Where this section and an earlier one
differ, this one holds.

**The decisions are taken.** G-1 to G-25, as recommended: G-13 is (b). In REVIEW.md they are D-26 to D-50, in
the order G-1 to G-25. G.1 writes them into REVIEW.md, in a section for part 2, the way the earlier decisions
are written there. From then on a test, a class or a mutant is named by its D-number.

**Where things are.** "The dev copy" of the sections above is the line of the branch `evre-1.1` of the repo:

| What | Where |
|---|---|
| library&nbsp;1.1 | `EVRe/lib/EVRe.h`, `EVRe.cpp` |
| the&nbsp;Guard,&nbsp;part&nbsp;1 | `EVRe/lib/guard/evre_guard.h`, `.cpp` |
| the&nbsp;Guard,&nbsp;part&nbsp;2&nbsp;(new) | `EVRe/lib/guard/evre_guard_desc.h`, `.cpp` |
| the&nbsp;library's&nbsp;suite | `EVRe/tests`: `run_lib_tests.py`, `lib_test.cpp`, `fuzz_test.cpp`, `api_compat.cpp`, `configure_test.cpp`, `lock_hooks.h` |
| library&nbsp;1.0,&nbsp;frozen | `EVRe/tests/lib_1.0`: the transcript's other side. Never edited |
| PROTOCOL.md,&nbsp;the&nbsp;1.1&nbsp;text | `EVRe/docs/PROTOCOL.md` |
| the&nbsp;review's&nbsp;record | `EVRe/REVIEW.md` |
| the&nbsp;Studio&nbsp;and&nbsp;the&nbsp;tools | `EVRe/studio`, as on `main` |

The suite: `python tests/run_lib_tests.py`, run in `EVRe`. On 2026-10-05 it ended with 676 passed, 0 failed on
Windows (MinGW g++ 13.1), and `studio/tests/device_table_test.py` with 24 passed against library 1.1.

"The firmware" and "the host program" are not in the repo. Section 7 and the rows F1 to F6 are not part of
phase G.

**Where the work happens now.** Section 13's "Where" column and its list of what can start when describe the
time before library 1.1 was on a branch of the repo. Now every part, P1 to P6, is built on the line of
`evre-1.1`, by the parts G.0 to G.6 of the work plan, each with a pull request into `evre-1.1`. The Guard is
beside the library there, so `guard_table_test.py` runs in CI like every other test: the SKIP of section 9 no
longer applies.

**G.0, the dev copy made at home in the repo.** No behaviour of the library changes in it.

- `ci.yml` runs the library's suite on Linux and Windows, and the README's list of tests names it
  (CONTRIBUTING.md's already does).
- The tools learn the code 13. `errorName` in the Studio, the Python package's `ERRORS` and `evre` say "login
  required". A register refused with 13 is still asked again at the next poll: only 3, 4 and 5 mark it "not
  available". The Studio does not log in again by itself. A check for each, STUDIO.md 18's table of codes, the
  Arabic.
- PROTOCOL.md has the repo's rule for tables applied already (a header row, short cells on one line): it stays
  so.
- CHANGELOG.md gets a section for what is not released yet: library 1.1 and EVRe Guard, in a few lines taken
  from PROTOCOL.md's "Migrating from 1.0". The version in `studio/CMakeLists.txt` stays.
- Every test of CLAUDE.md's "Build and test on Linux" still passes with library 1.1 in `EVRe/lib`.

**The compilers of the matrix** (section 9, item 7). `ci.yml` installs them on Linux: the ARM and AVR
cross-compilers and the 32-bit libraries. Where the machine of a session lacks one, the suite's skip flag names
it, the run prints what it skipped, and the pull request says so. No ARM or AVR program is run: they are only
compiled.

**G.6, the review.** One pass over the diff of G.1 to G.5: every rule of section 12 with its "checked by", item
by item; every test, class and mutant that section 9 names exists, and each mutant fails where it should; the
numbers in the docs equal the ones the scripts measure. What fails is fixed in G.6. REVIEW.md's section for part
2 is brought up to date: the decisions, what was built, the counts.

**The Studio's parts** (G.3, and the editor's side of G.4) follow the repo's rules, as section 12 says. Their
pull requests list the new user texts, English beside Arabic, for the owner's review.

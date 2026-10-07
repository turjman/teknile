# Fast EVRe: sample streams, the plan

The design of Fast EVRe and the plan to build it. The owner confirmed every decision, F-1 to F-19 (section 12),
as recommended on 2026-10-05: where a decision lists options, the recommended one is the decision. Phase 5 of
the work plan (CLAUDE.md) builds it part by part (section 14). Section 16 fixes what the build needs beyond the
decisions.

Numbers marked (measured) were measured on 2026-10-05 on the Windows laptop, with a Release build (MinGW g++
13.1, Qt 6.8.3) of a scratch program around the Studio's own `src/evre/frame.cpp`, or are taken from
PERFORMANCE.md. Numbers marked (estimate) are judgement, and the build measures them.

## 1. What Fast EVRe is, and what it is not

Fast EVRe carries samples that a device takes on its own clock: many in one frame, every block numbered. It is
for what polling and auto send cannot give: a current or a voltage at 10 000, 100 000 or a million samples a
second, drawn down to the single sample, with every lost sample counted.

A host gets values from an EVRe device in two ways today. Fast EVRe is the third:

| Way | Who&nbsp;times&nbsp;a&nbsp;sample | Rate | One&nbsp;frame&nbsp;holds | A lost frame |
|---|---|---|---|---|
| Poll | the&nbsp;host | up to about 4000 a second on a fast link (STUDIO.md 13.7) | the&nbsp;registers&nbsp;asked&nbsp;for,&nbsp;once | is seen: the request times out |
| Auto&nbsp;send | the&nbsp;device's&nbsp;timer | 40 to 4000 a second (CONFIG's prescaler) | the&nbsp;read-only&nbsp;block,&nbsp;once | is not seen: the frames carry no number, and the host stamps each with the time it arrived |
| Fast | the&nbsp;device's&nbsp;sample&nbsp;clock | what the link carries (section 9) | hundreds of records of a few channels | is counted: every block says where it starts |

It is:

- a layer above the protocol, beside EVRe Guard. With F-2 (a) the wire gains no function code, no bank and no
  STATUS bit, and `lib/EVRe.*` does not change by one line.
- optional. A device without it works as today. A host that does not know it skips its frames: they are answers
  nobody asked for.
- timed by the device. A record's time comes from its number and the stream's rate, not from the moment its
  frame arrived.
- counted. The device numbers every record it takes, also those it has to drop. A host sees a gap as a gap,
  with its size.
- described by the map: where the blocks come from, how fast, and what a record holds.

It is not:

- a faster poll or a faster auto send. Both stay as they are.
- lossless. When the link is slower than the samples come, the device drops records, and the numbers say how
  many. Nothing is sent again.
- a new transport. The same frames, the same CRC, the same links.
- for a bus, in its first build. A device that sends by itself collides with the others on a shared line, as
  with auto send.
- part of the protocol. EVRe moves the bytes of a block and never asks what they mean.

```
+----------------------------------------------------------------+
| the map (evre-map/1): "streams" - a window, a rate, and the    |
|   channels of a record. For the tools.                         |
+----------------------------------------------------------------+
| Fast EVRe (optional): blocks of records, each block numbered   |
|   in the device: lib/fast puts a frame around the samples      |
|   in the host:   the tools take the blocks apart               |
+-------------------------------+--------------------------------+
                                |  bytes: a READ_RESP of the
                                |  stream's window
+-------------------------------v--------------------------------+
| EVRe, the protocol: frames, CRC, slave, READ_RESP. It never    |
|   asks what the bytes mean. Unchanged.                         |
+----------------------------------------------------------------+
```

## 2. The wire: a block is a READ_RESP of a window

A stream owns a **window**: a span of addresses in the device bank that the map gives to it, 0xDC00..0xDFFF
say. The device sends a block as a READ_RESP at the window's first address, without being asked:

```
 7B  slave  AB  off_lo off_hi  cnt_lo cnt_hi  the block: 8 B header, the records  crc_lo crc_hi  7D
            ^   ^              ^
            |   |              the block's bytes: 8 + records x record size
            |   the window's first address
            READ_RESP
```

PROTOCOL.md already allows it: a `READ_RESP` "may also be *sent* by a device that was not asked", and "that is
how unsolicited streaming works". Auto send is the first use of that sentence. This is the second.

What programs do with such a frame today, checked in the code:

| Program | What it does with a block it does not know |
|---|---|
| EVRe&nbsp;Studio | `Master::findPending` finds no request for it, so it goes to `IoEngine::onUnsolicited`, which takes only auto send's frame at 0xD000. It is dropped. The Monitor lists it as "not a pending request" |
| `evre` | the same master: dropped |
| the&nbsp;Python&nbsp;package | `Master._request` skips an answer at another address |
| a&nbsp;host's&nbsp;mirror | the mirror checks the frame against its own read limits: a window outside its map is refused (4 or 5), and nothing is stored |
| an&nbsp;EVRe&nbsp;device | never gets one on a link of its own. Library 1.1 would refuse a READ_RESP sent to it (2), silently |

So a device may send blocks today, and no existing host breaks.

The limits:

- A block is at most as large as its window, and the window lies in the device bank's 0x1000 addresses. A map
  that keeps its registers below 0xD100 can give a stream up to 3840 bytes.
- CRC-16/X-25 catches every error of up to three bits in a frame of up to 4086 data bytes (its polynomial
  repeats after 32 767 bits). A window of 3840 bytes stays below that.
- A READ of the window is the device's choice: it answers with its newest block or refuses. The first build
  does not use it (F-1).

## 3. The block

The data bytes of the frame, all little endian:

| Offset | Member | Type | Meaning |
|---|---|---|---|
| 0 | first | u32 | the number of the block's first record. The device counts every record it takes from the stream's start, also those it has to drop. It wraps at 2^32 |
| 4 | count | u16 | the records in this block |
| 6 | flags | u8 | bit 0, START: the first block since the stream started. Bit 1, LOST: records were dropped just before this block. The other bits are 0 |
| 7 | spare | u8 | 0. Kept for a later part (a range or gain number, say) |
| 8 | records | | `count` records, each as long as the map says (section 7) |

The rules:

- The frame's count is exactly 8 + count x record size. Any other count is a bad block: its records are not
  used, and it is counted.
- A stream starts at record 0: its first block has `first` 0 and START set.
- `first` is the truth about losses. LOST only tells a person who reads a dump. The host compares `first` with
  the end of the block before: equal means nothing lost, ahead by n means n records lost.
- The host keeps the number in 64 bits. With d = (first - expected) mod 2^32: d below 2^31 is d records lost,
  0 for none; anything else, or START, is a new start (a reset of the device, a stop and a start).
- A host that joins a running stream begins at the first block it sees.
- A flag bit the host does not know, or a spare that is not 0: the block is of a newer kind. The host uses none
  of its records and says so once. It fails closed (R6).
- A block of 0 records is allowed and carries only its header.

An example. A record is two i16 values, 4 bytes. The window has 1024 bytes, so a block holds up to
(1024 - 8) / 4 = 254 records and its frame is 1034 bytes. At 100 000 records a second that is 394 frames and
407 kB a second on the wire. At 1 000 000 records a second: 3937 frames, 4.07 MB a second.

The overhead is 18 bytes a block: 10 of the frame and 8 of the header. That is 1.8 % at this size.

## 4. Time

A record's place in time is its number: record k of a stream was taken k / rate seconds after record 0.

The host lays a stream on its own clock with two numbers: when record 0 was, and how long a record takes.

- It starts from the first block's arrival, less the block's own length, and the map's rate.
- Then it corrects the rate, slowly, from the blocks' arrival times. A block can arrive late, never early, so
  the earliest arrivals are taken as the truth. The stream then neither runs ahead of the Studio's clock nor
  falls behind it over hours: a crystal that is off by 50 parts in a million would drift 180 ms an hour.
- The correction is shown: "100 003 records/s (+32 ppm)".
- A new start gets a new pair of numbers.

Polled values keep their arrival times, as today. Both kinds are drawn on one time axis. They agree to within
the link's delay, a millisecond or so on USB, which is what a polled value's time is good for anyway.

Inside a stream, a time difference is exact to the device's sample clock, as fitted: 1 000 000 records at
1 000 000 a second are 1 s between cursors A and B, times the correction.

## 5. Starting and stopping

The protocol has no part in it. A stream is switched by ordinary registers of the device bank, which the map
names (section 7):

- **`enable`**: a writable register. The host writes 1 to start the stream and 0 to stop it, with WRITE_ACK. A
  stream without one is the device's own business: the host only listens.
- **`rate_reg`**: a register whose shown value is the rate in records a second, read at the start and after a
  write to it. Without one the map's `rate` holds.

What the Studio does around it, as it does for auto send today:

- While a stream is on it reads CONFIG every 100 ms, whatever the polling is (the engine's heartbeat). So a
  device's host watchdog sees a live host, and so does a login's idle logout.
- Disconnect and closing the window switch every stream off first, the way they clear AUTO_SEND.
- Not on a bus: the switch is offered with one device on the link only.

What a device does:

- It stops a stream when `enable` is written 0, when its host watchdog runs out (PROTOCOL.md, Patterns), and
  at a reset.
- With a login (EVRe Guard) it sends no block while no session is open (F-10). The handlers are never asked
  about the device's own frames, so the Guard cannot stop them: the device must.

## 6. The device's side: lib/fast

A small helper beside `lib/guard`, marked "not part of the protocol". It puts the frame around the samples
where they lie, so nothing is copied:

```
 the buffer the device gives its ADC:

 | 7 B frame header | 8 B block header | n records ...                     | 3 B CRC, end |
 ^                                     ^                                   ^
 0                                     15: the ADC's DMA writes here       15 + n x record size

 evre_fast_frame() writes the two headers and the CRC, and returns the frame's length.
 The device then sends the buffer whole.
```

The API, as a sketch:

```cpp
#define EVRE_FAST_BEFORE (15U) /* the frame's 7 bytes and the block's 8, before the first record */
#define EVRE_FAST_AFTER  (3U)  /* the CRC and the end byte, after the last */

typedef struct {
	uint16_t addr;   /* the window's first address */
	uint16_t size;   /* the window's bytes: no block is larger */
	uint16_t record; /* bytes of one record */
} evre_fast_config_t;

typedef struct {
	const evre_fast_config_t *cfg; /* nullptr until a good init: no frame is built */
	uint32_t next;                 /* the number of the next record */
	uint8_t pending;               /* flags for the next block: START, LOST */
} evre_fast_t;

/* NO_ERROR, or PERMISSION_DENIED for a window outside 0xD000..0xDFFF or one
 * too small for the header and one record. */
uint8_t evre_fast_init(evre_fast_t *stream, const evre_fast_config_t *cfg);

/* The stream starts: the next record is number 0, the next block says START. */
void evre_fast_start(evre_fast_t *stream);

/* The device had to drop records: they keep their numbers, the next block says LOST. */
void evre_fast_lost(evre_fast_t *stream, uint32_t records);

/* The most records one block takes. */
uint16_t evre_fast_room(const evre_fast_t *stream);

/* buf holds n records at EVRE_FAST_BEFORE, with EVRE_FAST_AFTER bytes free behind
 * them. Writes the headers and the CRC around them and returns the frame's
 * length, 0 when n does not fit the window. */
uint16_t evre_fast_frame(evre_fast_t *stream, uint8_t slave, uint8_t *buf, uint16_t n);
```

How a device uses it, in words: a timer starts the ADC, DMA fills one of two buffers from byte 15 on, and at
"buffer full" the device swaps the buffers, calls `evre_fast_frame()` on the full one and hands it to its
transmitter. When the transmitter is still busy with the buffer before, the device drops this one and calls
`evre_fast_lost()`.

- It builds the frame with the library's public names only (`READ_RESP`, the frame index macros, `GetCrc16`),
  so it works with library 1.0 and 1.1 alike, and the window needs no memory behind it and no entry in the
  device's map (F-9).
- A frame goes out whole. Blocks and the decoder's answers share the device's one transmitter, so they share
  one queue: no frame may start inside another.
- `evre_fast_t` is written in one context only, the one that builds blocks. A device that calls
  `evre_fast_start()` from elsewhere masks that context around the call (R5).
- No heap, no state but the 12 bytes of `evre_fast_t`, C++11.

Cost (estimate): about 200 to 300 B of code; 12 B of RAM a stream, plus the device's buffers. The time is the
CRC's: about 10 instructions a byte. PERFORMANCE.md measured 2977 instructions for a 220 B READ_RESP built from
ranges, copy included. So a 1 KB block costs about 10 000 instructions, and 4 MB a second (a million records
of two i16) takes under 10 % of a 480 MHz Cortex-M7. An MCU with a CRC unit can do it in hardware, later, if a
bench says it matters.

## 7. The map

One new optional key at the top level, `streams`. `evre-map/1` grows by optional keys only, and an old reader
ignores a key it does not know and keeps it when it saves the map (MAP_FORMAT.md section 2).

```json
"streams": [
  { "name": "ADC", "addr": "0xDC00", "size": 1024, "rate": 100000, "enable": "ADC_STREAM",
    "group": "Power", "desc": "load current and bus voltage, sampled together",
    "channels": [
      { "name": "I_LOAD", "type": "i16", "unit": "A", "scale": 0.0005 },
      { "name": "V_BUS",  "type": "i16", "unit": "V", "scale": 0.001 }
    ] }
]
```

A stream:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | **required** | unique among the map's streams and registers |
| `addr` | 16-bit&nbsp;number | **required** | the window's first address |
| `size` | integer | **required** | the window's bytes: the largest block, header included |
| `rate` | number | **required** | records a second, as the device is built |
| `rate_reg` | string | none | a register whose shown value is the rate now |
| `enable` | string | none | a writable register: 1 starts the stream, 0 stops it |
| `group`,&nbsp;`desc`,&nbsp;`notes` | strings | as&nbsp;a&nbsp;register's | for people |
| `channels` | array | **required** | what one record holds, in order, packed, little endian |

A channel:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | **required** | unique in its stream. Tools name the line `STREAM.CHANNEL`, as they name a register's field |
| `type` | string | `"i16"` | a number type of section 5.1: `u8` to `f32`. Never `bytes` |
| `unit`,&nbsp;`scale`,&nbsp;`offset`,&nbsp;`decimals`,&nbsp;`desc` | as&nbsp;a&nbsp;register's | | shown = raw x scale + offset |

The record's size is the sum of its channels' sizes. All channels of a record belong to one instant.

What the checker refuses: a window outside 0xD000..0xDFFF; a window that shares a byte with a register or
another window; a window smaller than the header and one record; no channel; a `bytes` channel; a rate that is
not above 0; `enable` or `rate_reg` naming no register, or `enable` naming one a host cannot write; a name used
twice.

No register is declared over a window. A tool that polled it would read a kilobyte at every poll.

## 8. The tools

### 8.1 EVRe Studio, as a user sees it

- **The Registers tab** gets a group for each stream and a row for each channel: `ADC.I_LOAD`, its type and
  unit, its newest value at the Show values pace, and a Plot tick. No Log tick: a CSV row per record would be
  megabytes a second (F-14).
- **The sidebar** gets a card, Fast streams: for each stream a start and stop button, the rate as measured with
  its correction, and the records lost. Greyed with the reason where it cannot be used (not connected, on a
  bus), as auto send's box is. It fits the sidebar's 312 px in both languages.
- **The chart** draws a fast channel as a line like any other: its legend chip, its lane, the cursors, the A-B
  bar, the crosshair, the right-click menu. The view zooms in to 10 us (0.001 s today), and the time labels
  gain milliseconds and microseconds. Where the view holds few records, each one is drawn at its own time.
- **Lost records** break the line: nothing is drawn across the gap, and the tooltip there says how many are
  missing.
- **The memory**: a fast channel keeps its records as they came, 2 bytes for an i16, not the 23 bytes a sample
  of a polled line takes. The RAM field covers both kinds.
- **Recording**: section 8.3.

All of it under the repo's rules for what a person sees: buttons that look like buttons, tooltips, both
themes, English and Arabic.

### 8.2 EVRe Studio, inside

- **The engine** (`src/io`): a block is recognised by its slave and its window. The I/O thread checks it
  (section 3), counts it, writes it to a running recording, and queues its bytes for the window. As with
  polled samples, the window takes what is queued once a display frame. If the window stalls, the queue is
  capped and what the chart missed is counted; the recording misses nothing.
- **The store** (`src/model`, a unit of its own with its own test): the records as bytes in large pieces; the
  starts and gaps as a short list; and for each channel summaries at two levels, made as the records come:
  the min and the max of every few hundred records, and the min, the max, the sum and the sum of squares of
  every few thousand. A view of an hour and a view of 100 us then cost the same: the work follows the pixels,
  not the records. The statistics between the cursors come from the same summaries, so they do not slow down
  with the span. The build chooses the two sizes by measuring; the summaries take a few percent of the
  records' memory (estimate).
- **The chart**: both drawing paths, the CPU's and the graphics card's, draw from bins, one per pixel column,
  and stay as they are. A fast line makes its bins from the store, and has a key of its own range, as math
  lines have. The work is where the chart reads a line's `times` and `values` arrays directly: 66 lines of
  `chart_widget.cpp` today. Each gets its fast branch or is shown not to need one. Nothing around them is
  reshaped (the rule: no refactoring).
- **Math lines** do not take a fast channel in the first build (F-16, A2). The math editor says so when one is
  named.
- **The frame parser** needs no rework. As it is, it takes 71 to 87 MB a second of blocks (measured: 75 000
  frames a second of 1 KB). A million records a second of two i16 are 4 MB a second: about 5 % of one core.
  Most of that is the CRC, which the Studio computes bit by bit (80 to 92 MB/s, measured). The library's table
  does 300 MB/s (measured). The table is an easy gain if it is ever wanted. Min, max and sum over i16 samples
  run at 2 GB a second (measured): the summaries cost nothing next to the CRC.

### 8.3 Recordings

A CSV row per record is not possible: a row is about 60 bytes, so a million records a second are about 60 MB
of text a second. A fast stream is recorded as it came (F-14):

```
 a file of pieces, each with a name of 4 bytes and a length of 4 bytes before it,
 so a reader skips a piece it does not know:

   the head      JSON: the format's name ("evre-fast-rec/1"), the device, the stream's entry
                 from the map, the wall-clock time of the start
   a time mark   at every start, and then once a second: a record's number and the Studio's
                 time_s for it, as fitted at that moment
   a block       as it came: its 8 B header and its records
```

- Writing is appending bytes in the I/O thread. Nothing is converted and nothing is lost: gaps stay gaps.
- The time marks carry the fit of section 4 into the file, start by start, so a reader lays the records on the
  recording's clock without the Studio.
- A file that was cut off (a crash, a full disk) opens up to its last whole piece.
- A later piece can hold the summaries, so that a large file opens at once (not in the first build).
- One file a stream, beside the CSV of the same recording: `run.csv`, `run.ADC.evrs`. The registers stay in
  the CSV.
- The recording window opens a `.evrs` alone, or together with its CSV. The file is mapped into memory, not
  read into it: only the summaries are built, on a thread with a progress bar, so a recording larger than the
  RAM opens (F-15).
- Python reads it in a few lines (section 8.5).

### 8.4 `evre`, `evre-sim` and the fake devices

- `evre record LINK --map MAP --stream NAME -o FILE [--seconds S]` writes a `.evrs`. `evre info` lists a map's
  streams, `evre validate` checks them, and the exports (Markdown, C header, Python) describe them.
- `evre-sim` and `evre_fake_fast` serve a map's streams: each channel a wave, at the map's rate, in blocks,
  while its `enable` register holds 1. For the tests they can also lose records, run fast or slow by some ppm,
  and start the counter just below 2^32.
- `evre check` (conformance) gains: a stream starts, its first block says START, the numbers follow, the rate
  is within a tolerance of the map's, and it stops.

### 8.5 The Python package

- `dev.stream("ADC")` gives the blocks as they come: the first record's number and each channel's values in
  shown units. Standard library only, as the package is.
- `evre.read_recording(path)` reads a `.evrs`.

### 8.6 A gateway

A host program that serves a device to its own clients passes blocks on as they are. It need not understand
them. If it keeps a mirror of the device, the mirror refuses a block (4), so the gateway forwards blocks
before its mirror sees them. Starting and stopping for each of its clients is the gateway's own business.

## 9. What a link carries, and what it costs

| Link | Bytes&nbsp;a&nbsp;second | Records&nbsp;of&nbsp;two&nbsp;i16&nbsp;a&nbsp;second | Note |
|---|---|---|---|
| UART,&nbsp;115200&nbsp;baud | 11&nbsp;500 | about&nbsp;2&nbsp;000 | at 70 % of the link, the share auto send may take today |
| UART,&nbsp;921600&nbsp;baud | 92&nbsp;000 | about&nbsp;16&nbsp;000 | the same rule |
| UART,&nbsp;3&nbsp;Mbaud | 300&nbsp;000 | about&nbsp;50&nbsp;000 | the same rule |
| USB&nbsp;full&nbsp;speed,&nbsp;CDC | about&nbsp;1&nbsp;000&nbsp;000&nbsp;at&nbsp;the&nbsp;very&nbsp;most | 100&nbsp;000&nbsp;to&nbsp;200&nbsp;000&nbsp;(estimate) | set by the device's USB code; measure it first (section 14, the firmware) |
| USB&nbsp;high&nbsp;speed,&nbsp;CDC | tens&nbsp;of&nbsp;millions | several&nbsp;million&nbsp;(estimate) | the same |
| TCP,&nbsp;100&nbsp;Mbit | about&nbsp;10&nbsp;000&nbsp;000 | about&nbsp;2&nbsp;500&nbsp;000 | through a gateway |

On the host (measured, section 8.2): the Studio's parser takes about 75 MB a second as it is, so the link and
the device set the limit, not the Studio.

In the Studio's memory: two i16 channels at a million records a second are 4 MB a second, plus a few percent
for the summaries (estimate). The default 2 GB then hold about 8 minutes; at 100 000 records a second, 80
minutes. As two polled lines the same data would take 46 MB a second and fill 2 GB in 44 seconds.

On disk: the same 4 MB a second, 14 GB an hour.

## 10. Tests

Everything runs on Linux and Windows against the fake devices, without a board.

1. **The block's rules**: every rule of section 3 as a named check: a count that does not fit, the wrap at
   2^32, START, LOST, a restart without START, a flag nobody knows, a spare that is not 0, a block of no
   records, a host that joins late. And a fuzz of random bytes: no crash, and never a record out of a bad block.
2. **The helper** (`lib/fast`): built with the library the way `device_table_test.py` builds a generated table;
   its frames go through the Studio's parser and block check; the numbers after `evre_fast_lost()`; the largest
   block; a block that does not fit. C++11 to C++20, -O0 to -O3 and -Os, `-Wall -Wextra -Wpedantic -Werror`, no
   heap (`nm`), its stack (`-fstack-usage`).
3. **The store**, without a window: its summaries equal a plain loop over random data, at every level and for
   every span; gaps; trimming by the RAM; what it says it holds equals what it holds.
4. **The Studio**, GUI checks in `gui_test.cpp`, one for each behaviour: the card and its states; start and
   stop; the heartbeat while on; off at disconnect; not on a bus; the rows and the Plot tick; a spike of one
   record in a long run shows at every zoom; single records at their own times; a gap is not bridged; the
   statistics between the cursors equal a plain loop; the time labels below a millisecond; the RAM shared; the
   lanes, the legend and the crosshair with fast lines; a recording made, opened and equal; a file cut off; the
   texts in Arabic.
5. **Both drawing paths**: on Windows the card's picture of fast lines equals the CPU's, at the level of the
   existing picture checks.
6. **Speed**, with `EVRE_PERF_LOG`: the targets of F-17.
7. **The tools**: `cli_test.py` (`evre record`, `info`, `validate`), `sim_test.py` (a stream from a map), the
   Python package's tests (live blocks, a recording read back), `map_test` and `schema_test.py` (the new keys
   and every refusal of the checker).
8. **Docs checks**: PROTOCOL.md's example block is built by a program and compared; the map keys are compared
   with the schema.

## 11. Risks

- **The chart is the Studio's largest piece** (3893 lines), and a fast line must come in beside what works
  without reshaping it. Mitigation: the store is its own unit with its own test; the drawing paths stay as
  they are, because they draw bins; phase 5.2 is planned as the largest step and gets the Windows picture
  checks and an on-screen speed run.
- **What a real device's USB carries is not known** until it is measured, and neither is what the Studio's
  serial path takes at megabytes a second. Mitigation: the firmware's first step sends dummy blocks and
  measures (section 14); over TCP everything is proven with the fake device first.
- **Two clocks.** The device's sample clock and the Studio's differ by parts in a million. The fit of
  section 4 hides it; a fit that goes wrong would shift fast lines against polled ones. It is tested with a
  fake device that runs fast and slow.
- **The window takes addresses** of the device bank. A device with a full bank has no room for a stream.
- **The name.** "Stream" is already the JSON API's command for values at a period, and the engine's own word
  for auto send's frames. The docs must keep the three apart (F-5).
- **A device with a login** must stop its streams when the session ends. Nothing in the library does it for
  the device (F-10).
- **No bus** in the first build. A stream needs a link of its own.
- **Blocks and answers share the device's transmitter.** A frame that starts inside another is lost, with the
  one it cut. The helper's docs say it, and the firmware's transmit queue must hold it.
- **RAM and disk** fill quickly at a million records a second (section 9). The memory strip and the recording
  card say how much is left.
- **Old tools and new maps.** An old Studio keeps `streams` when it saves a map, but shows nothing of them.
- **Two copies of PROTOCOL.md** exist, 1.0 in the repo and 1.1 in the dev copy. The Fast section is written
  once, for the repo, and carried over at the 1.1 merge.
- **Every new text needs its Arabic**, and every new control its look in both themes.

## 12. Owner decisions

All confirmed as recommended (2026-10-05). The short form, then each in full, with the options that were
weighed.

| # | Question | Recommended |
|---|---|---|
| F-1 | the&nbsp;first&nbsp;build's&nbsp;scope | the device pushes blocks, one device a link; reading the window waits |
| F-2 | the&nbsp;wire | a READ_RESP of a window in the device bank: no change to the protocol |
| F-3 | the&nbsp;block's&nbsp;header | 8 bytes: first, count, flags, spare |
| F-4 | a&nbsp;record | all channels of one instant, packed in the map's order |
| F-5 | the&nbsp;names | Fast EVRe; `streams` and `channels` in the map; `lib/fast` |
| F-6 | start,&nbsp;stop,&nbsp;rate | ordinary registers that the map names |
| F-7 | time | the record's number and the rate, fitted to the Studio's clock |
| F-8 | lost&nbsp;records | a gap, counted, never filled in |
| F-9 | the&nbsp;device&nbsp;helper | `lib/fast`, with public names only: works with 1.0 and 1.1 |
| F-10 | with&nbsp;a&nbsp;login | the device sends no block without a session |
| F-11 | the&nbsp;map | a top-level `streams` list |
| F-12 | in&nbsp;the&nbsp;Studio | rows in the Registers tab, a card in the sidebar, lines on the chart |
| F-13 | the&nbsp;store | records as they came, with summaries; the RAM shared with the chart |
| F-14 | recording | a format of our own, `.evrs`: a head, the blocks as they came, time marks |
| F-15 | opening&nbsp;one | mapped into memory, alone or beside its CSV |
| F-16 | what&nbsp;waits | reading the window, a bus, math lines, and the rest of its list |
| F-17 | the&nbsp;targets | a million records a second of two i16, ten minutes, none lost |
| F-18 | who&nbsp;builds&nbsp;what | the cloud by phases; the helper under `EVRe/lib` with your OK |
| F-19 | the&nbsp;firmware | its own session; first measure the link with dummy blocks |

1. **F-1 What does the first build carry?**
   Options: (a) the device pushes blocks by itself, on a link with one device. A READ of the window is not
   used. (b) as (a), and a host can also read the window to pull the next block, so a bus device or a slow
   link is drained at the host's pace. That needs a read handler in the device (library 1.1), and a lost answer
   loses its block. A stream is then pushed or pulled, never both: an answer and a pushed block look alike.
   Recommendation: (a). It is auto send's sibling and needs nothing new in the library. The header is already
   fit for (b): `count` says how many records of a window that was read whole are real. (b) is addition A1.
2. **F-2 How does a block travel?**
   Options: (a) as a READ_RESP of a window in the device bank, unasked. The protocol, the library and every
   existing host stay as they are (section 2). (b) in a third bank, 0xF000 say: the device bank stays free and
   a block may be 4096 bytes, but the register model gains a bank, the library must learn it, and a mirror
   refuses it until then. (c) with a new function code: clean to read in a dump, but every framer in the field
   takes an unknown code for line noise and searches the block's bytes for a frame, the library changes, and
   the protocol gets a new revision.
   Recommendation: (a). Take (b) only if a device's bank is too full for a window.
3. **F-3 What does the block's header hold?**
   Options: (a) 8 bytes: `first` (u32, counts records, the dropped ones too), `count` (u16), `flags` (START,
   LOST), `spare`. (b) 4 bytes: `first` alone. The frame's count gives the records, and a restart shows as a
   number that goes back. No room to grow, and no way to read a window. (c) 16 bytes, with the device's own
   time in microseconds in every block.
   Recommendation: (a). The record number is the time (F-7), so (c) sends the same fact twice.
4. **F-4 What is a record?**
   Options: (a) all channels of a stream at one instant, packed, little endian, in the map's order. A device
   with channels at different rates has a stream for each rate. (b) one stream a channel, always: simpler to
   describe, but two channels that belong together (a current and its voltage) lose their common instant and
   cost two frames.
   Recommendation: (a).
5. **F-5 The names?**
   Options: (a) the layer is Fast EVRe; in the map `streams` and `channels`; on screen "fast streams" and
   "fast lines"; the helper `lib/fast/evre_fast.*`; the recording `.evrs`. The JSON API's `stream` command and
   auto send keep their names, and the docs say which is which. (b) another word in the map, `fast` or
   `scopes`, to stay clear of the API's `stream`.
   Recommendation: (a). "Stream" is the word an engineer looks for.
6. **F-6 How is a stream started, stopped and set?**
   Options: (a) by ordinary registers of the device bank that the map names: `enable`, and `rate_reg` where the
   rate can change. (b) by a bit of CONFIG, as auto send: a change to the reserved bank, and one bit for every
   stream. (c) not described at all: each device has its own way, and the tools cannot start one.
   Recommendation: (a). The protocol stays out of it, and the tools can still press the button.
7. **F-7 Where does a record's time come from?**
   Options: (a) from its number and the rate, laid on the Studio's clock and corrected slowly from the
   arrival times (section 4). (b) from each block's arrival, as auto send's frames: the records of a block
   would be spread over a guess, and the link's jitter would be in the data. (c) from a time the device puts in
   every block: it needs a device clock that means something to the host.
   Recommendation: (a).
8. **F-8 What does a host do with lost records?**
   Options: (a) a gap: nothing drawn, nothing measured there, the number lost shown and counted. (b) hold the
   last value across the gap. (c) a straight line across it.
   Recommendation: (a). The other two draw data nobody measured.
9. **F-9 What does the library give a device?**
   Options: (a) `lib/fast/evre_fast.*`: it builds the frame itself from the library's public names
   (`GetCrc16`, `READ_RESP`, the frame index macros). It works with library 1.0 and 1.1, the window needs no
   memory and no entry in the device's map, and Fast EVRe does not wait for the 1.1 merge. (b) through
   `encodePacketInto`: the frame's layout stays in one file, but the window must be readable in the device's
   own map. That is 4 bytes of pointer for every byte of window on a 1.0 pointer table, or a range with real
   memory behind it on 1.1. (c) no helper: a page of documentation and an example.
   Recommendation: (a). It adds a folder beside `lib/guard` and changes no line of `lib/EVRe.*`. Under the
   repo's rule the folder needs your OK: this decision is it.
10. **F-10 A device with a login (EVRe Guard)?**
    Options: (a) the rule, in PROTOCOL.md and in the wiring example: the device asks `evre_guard_logged_in()`
    before it sends a block, and stops its streams when the session ends. The helper knows nothing of the
    Guard. (b) the helper takes a function it asks before every block. (c) nothing is said.
    Recommendation: (a). The same gap exists today for auto send: the Guard's docs do not say that a device
    must stop its own frames when nobody is logged in. It is G-25 in the Guard plan.
11. **F-11 How does the map describe a stream?**
    Options: (a) a new top-level list, `streams` (section 7). Old readers ignore it and keep it. (b) a register
    of a new type, `"type": "stream"`: an old reader would take the window for a register of a type it does
    not know, or would poll it.
    Recommendation: (a).
12. **F-12 Where does a stream show in the Studio?**
    Options: (a) where registers show: a group in the Registers tab with a row and a Plot tick for each
    channel, a card in the sidebar to start and stop, and lines on the one chart, beside polled lines, with
    the same cursors and measurements. (b) a tab of its own, a scope: its own plot, its own controls. (c) as
    (a), but the channels and their Plot ticks sit in the sidebar's card, not in the Registers tab: the
    table's code stays untouched, and the channels are away from the search and the groups.
    Recommendation: (a). A fast current next to a polled temperature on one time axis is the point. A second
    plot would need everything the chart has, a second time. Take (c) if the rows turn out to cost the table
    too much.
13. **F-13 How does the Studio keep the records?**
    Options: (a) as they came, with summaries at two levels (section 8.2), in the chart's RAM budget. A fast
    line counts as one of the chart's 64 lines, and not in the 64 000 samples a second that polled lines
    share. (b) through the polled lines' path, thinned to what that path takes: no new store, but the single
    records are gone, and they are what Fast EVRe is for.
    Recommendation: (a).
14. **F-14 How is a stream recorded?**
    Options: (a) a format of our own, `.evrs`: named pieces, a head in JSON, the blocks as they came and a
    time mark a second (section 8.3). No library, nothing converted, gaps kept, a cut-off file still opens, and
    a reader skips a piece it does not know. (b) WAV: every tool opens it, but it
    has one sample type for all channels, no gaps, and 4 GB at most. (c) CSV, thinned to a row a millisecond:
    readable anywhere, but the single records are gone.
    Recommendation: (a). An export of a short span to CSV exists already (the chart's menu). WAV can come
    later as an export.
15. **F-15 How is a recording opened?**
    Options: (a) in the recording window, as CSV recordings are, the file mapped into memory and only the
    summaries built: a file larger than the RAM opens. Alone, or with the CSV of the same name beside it.
    (b) read into memory, like a CSV: the RAM budget then cuts long recordings.
    Recommendation: (a). Qt maps a file itself; no new library.
16. **F-16 What waits for later?**
    The additions, each with its own decision when its time comes: A1 reading the window, and with it a bus;
    A2 math lines over the channels of one stream (a power from a current and a voltage); A3 a fast channel's
    mean in each CSV row; A4 channels of single bits; A5 an export to WAV; A6 the API passes blocks on to its
    clients; A7 the device table export gives a 1.1 device its window as a range and the helper's config.
    Options: (a) all of them wait. (b) A2 in the first build.
    Recommendation: (a). Until A2, a device that wants power on the chart sends it as a channel, and exact
    charge and energy come from the device's own totals in ordinary registers.
17. **F-17 The targets?**
    Options: (a) against the fake device over TCP, on the laptop: a million records a second of two i16 (4 MB
    a second) for ten minutes with no record lost; the chart at 50 frames a second or more, live and held, at
    any zoom from the whole memory to 100 us; the statistics between the cursors within one frame's time,
    whatever the span; a 2 GB recording open within 5 seconds. (b) a tenth of that.
    Recommendation: (a). The measured parser has room for ten times as much.
18. **F-18 Who builds what, and where?**
    Options: (a) the Studio, the tools and the docs on the cloud, by the phases of section 14, each on its own
    branch with one pull request; the Windows runs, the card's pictures, the speed runs on screen, the merges
    on your word and the deploys stay local. The helper is small and goes with phase 5.1, under `EVRe/lib`,
    with your OK (F-9). (b) all of it local.
    Recommendation: (a), as phases 9, 0, 7 and 10 went.
19. **F-19 The firmware's part?**
    It is not in this tree. Options: (a) in the firmware's own session, on a bench board, in three steps:
    first dummy blocks, to measure what the board's USB really carries; then the ADC on a timer with DMA into
    two buffers; then the totals (charge, energy) summed on the board and shown as ordinary registers. (b)
    not now: Fast EVRe is built and proven with the fake device, and the firmware follows when a board is on
    the bench.
    Recommendation: (b) to begin with. Nothing in phases 5.1 to 5.5 needs the board. Its first step is worth
    doing early all the same: it tells how fast "fast" is on the real device.

## 13. Build rules

The helper follows the rules of the 1.1 refactor, so that it needs no second pass:

- **R1 The layering rule.** `lib/EVRe.*` does not change. The helper sits in `lib/fast`, marked "not part of
  the protocol", and uses the library's public names only. Checked by: the diff of `lib/EVRe.*` is empty.
- **R2 The public API only grows.** The header's 8 bytes, the two flag bits and the config's member order are
  fixed for good; a later need goes into `spare` and the free flag bits.
- **R3 C++11, no heap, no recursion, no goto, a small stack.** Fields built byte by byte, so the header is
  little endian on any CPU; sums in uint32_t; safe with a 16-bit int.
- **R4 Readable code.** Small functions, comments that say why, the owner's voice, LF, UTF-8, tabs, ASCII
  comments and diagrams, never the section sign.
- **R5 Interrupt-safe.** One context writes `evre_fast_t`. The header says so, and says what a device does
  that calls from another.
- **R6 Fail closed.** A bad config builds no frame. A host uses no record of a block it does not fully
  understand.
- **R7 Tests first-class.** A check for each behaviour, the fuzz, Windows and Linux, 0 warnings.
- **R8 Docs with the code.** PROTOCOL.md, MAP_FORMAT.md, the schema, STUDIO.md, the Help pages and
  CHANGELOG.md change in the same step as the code. Statements are checked by programs where they can be.
- **R9 Open-source hygiene.** No product, company or person's name; examples use the example map.

The Studio's part follows the repo's rules (CLAUDE.md): a branch and a pull request for each phase; a GUI
check for each behaviour, with the check count updated; STUDIO.md and the Help pages; user text through
`tr()`, each with its Arabic; the rules for what a person sees, in both themes and both languages; only what
the task needs changed; no new library.

## 14. Phases and effort

A session is one working block, as in the Guard plan. A cloud phase is one branch and one pull request. All
of it is an estimate.

| Phase | Where | Content | Effort |
|---|---|---|---|
| 5.0&nbsp;Decisions | the&nbsp;owner | done on 2026-10-05: F-1 to F-19 as recommended | done |
| 5.1&nbsp;The&nbsp;wire&nbsp;and&nbsp;the&nbsp;map | cloud | the block's section and its example in PROTOCOL.md; `streams` in MAP_FORMAT.md, the schema, the parser and the checker; streams in `evre-sim` and `evre_fake_fast`; the engine takes blocks, checks and counts them, starts and stops a stream, keeps the heartbeat; the sidebar's card; `evre record` and `evre info`; `lib/fast` with its test | 2 to 3 sessions |
| 5.2&nbsp;The&nbsp;store&nbsp;and&nbsp;the&nbsp;chart | cloud,&nbsp;then&nbsp;Windows | the store with its own test; fast lines on the chart, from the whole memory down to single records; the rows in the Registers tab; the time axis below a millisecond; gaps; the RAM shared; both drawing paths; the speed targets | 3 to 4 sessions |
| 5.3&nbsp;Measuring | cloud | cursors and the Measure table from the summaries; totals; the crosshair; the histogram and the spectrum (no resampling: the records are evenly spaced already); the trigger on a fast line; the chart's exports | 1.5 to 2 sessions |
| 5.4&nbsp;Recording | cloud | `.evrs` from the Studio and from `evre record`; the recording window opens it mapped, alone or with its CSV; `evre.read_recording` in Python | 1.5 to 2 sessions |
| 5.5&nbsp;The&nbsp;rest | cloud | a Streams page in the Map editor; streams in the exports (Markdown, C header, Python); `dev.stream()` in Python; `evre check`; STUDIO.md, the Help pages and the Arabic complete; a review round | 1.5 to 2 sessions |
| Windows&nbsp;runs | local | for each pull request: the Windows suites, the card's pictures, a speed run on screen, screenshots in both themes and both languages, the merge on your word, the deploy | 0.5 session each |
| The&nbsp;firmware | its&nbsp;own&nbsp;session | dummy blocks to measure the link; the ADC blocks; the totals in registers | 2 to 4 sessions, plus the bench |

In total, for the Studio, the tools and the helper: about 10 to 13 cloud sessions and 2.5 local ones. The
firmware comes on top.

What you can see after each phase:

- after 5.1: the card counts a fake device's blocks, and `evre record` writes them to a file;
- after 5.2: a million records a second on the chart, zoomed down to the single record;
- after 5.3: cursors, statistics, a spectrum and a trigger on it;
- after 5.4: record an hour, open it again;
- after 5.5: a map with streams made in the Map editor, and the docs complete.

Fast EVRe does not need library 1.1 (F-9). It is built on the line of `main`, beside EVRe Guard part 2, which is
built on the line of the branch `evre-1.1` (GUARD_PLAN.md there).

## 15. How this plan was made

Read, in full or the parts that matter: PROTOCOL.md (the 1.1 copy) and both versions of the library's
encoder; the Guard's header; the Studio's frame parser, its master and its engine (the auto send path); the
chart's data structures (STUDIO.md 23 and `chart_widget.h`); the chapters on polling, auto send and
recordings; MAP_FORMAT.md; `evre-sim`, the fake devices and the Python client; PERFORMANCE.md.

Checked in the code, not assumed:

- no existing host takes a READ_RESP at an unknown address for anything (section 2);
- both library versions build a READ_RESP for a device, and `GetCrc16` and the frame macros are public;
- the handlers are not asked about the device's own frames, so a login does not stop them;
- the chart keeps a sample in 23 bytes with its time, caps a line at 16 million samples, and both drawing
  paths draw from bins;
- the map format grows by keys that old readers ignore and keep.

Measured, for section 8.2: the Studio's parser and CRC on back-to-back blocks of 256, 1024 and 4000 bytes,
400 MB each, fed in 64 KiB pieces.

Found on the way, not part of Fast EVRe:

- The Python client matched an answer by its address only, not by its count as PROTOCOL.md asks. An auto send
  frame at 0xD000 could be taken for the answer to a read of part of that block. Fixed in pull request #12.
- A frame with more than 4086 data bytes is longer than the span over which CRC-16/X-25 catches every error of
  two bits: two flipped bits exactly 32 767 bits apart pass. Only a READ of nearly a whole bank is that long
  (4087 to 4096 bytes). PROTOCOL.md's CRC section says so since pull request #12.
- The Studio has no name for code 13 (`LOGIN_REQUIRED`) yet. That belongs to phase G (GUARD_PLAN.md).

## 16. Fixed for the build

What the parts of phase 5 need beyond the decisions. Where this section and an earlier one differ, this one
holds.

**Names and places.**

- The helper: `EVRe/lib/fast/evre_fast.h` and `.cpp`. Nothing else under `EVRe/lib` changes.
- The Studio: the block's rules and a stream's running state in `src/io/fast_stream.*`; the store in
  `src/model/fast_store.*`; the recording in `src/model/fast_recording.*`. The Python package:
  `python/evre/fast.py`.
- A fast line's key on the chart: `FIRST_FAST_KEY = 2 << 24`, plus 256 x the stream's index, plus the channel's
  index.
- Tests: `tests/fast_lib_test.py` (the helper), `evre_fast_test` (the block's rules and the store: QtTest, no
  window, like `evre_map_test`), and checks in `gui_test.cpp`, `cli_test.py`, `sim_test.py`, `map_test.cpp`,
  `schema_test.py` and the Python package's tests. `ci.yml` runs the new ones on Linux and Windows.

**The example.** `maps/example_device.json` stays as it is: its 16 registers are counted in many checks and
docs. A second example map, `maps/example_fast.json` ("Example fast device"), holds what the fast tests and docs
need: a few polled registers (an uptime in ms, two read-only f32 values, and the writable `ADC_STREAM`: u8, 0
"off", 1 "on") and one stream, `ADC`: window 0xDC00, size 1024, rate 10000, `enable` `ADC_STREAM`, channels
`I_LOAD` (i16, A, scale 0.0005) and `V_BUS` (i16, V, scale 0.001). The GUI test starts `evre_fake_fast` with it
on a fixed port of its own, listed with the others in STUDIO.md 26.

**On screen the Studio says "samples"**, its own word, where this plan says records: "10.0 k samples/s", "lost
1 024 samples". "Record" stays the word of PROTOCOL.md and MAP_FORMAT.md, where a record is one instant of all
channels. A stream's window is called a window only in those two documents: the Map editor's page labels its
two numbers Address and Size.

**The fake devices' test aids** (`evre_fake_fast` and `evre-sim`):

- `--fast-lose N`: every N-th block is not sent. Its records keep their numbers, and the next block says LOST.
- `--fast-ppm P`: the sample clock runs P parts in a million fast; negative, slow.
- `--fast-first K`: the first block starts at record K, with START. K just below 2^32 walks a host across the
  wrap.
- `--fast-rate R`: R records a second in place of the map's rate, so the example map also serves the speed
  check.
- A block is sent when it is full or 10 ms old, whichever comes first.
- `tests/fake_device.py` serves no stream.

**The engine.**

- A block is recognised before the auto send test: an unasked READ_RESP from the device's slave at a stream's
  window.
- While a stream is on, CONFIG is read every `HEARTBEAT_MS`, as for auto send.
- A stream is switched off before Disconnect and at close with the care AUTO_SEND's off takes: sent straight,
  flushed, the link drained.
- A lost link keeps the stream wanted: it is switched on again after the reconnect.
- When no block comes `FIRST_FRAME_MS` after the device took the enable, the stream is switched off again and
  the Log says so.
- What is queued for the window is capped at 64 MB. Past it the oldest blocks are dropped and counted as not
  shown.
- The Monitor names a block: "READ_RESP (fast stream ADC)".
- In 5.1 the window draws nothing yet: the card counts. The queue for the window comes with 5.2.

**The clock's fit** (section 4). The requirement, not the method: against a fake device that runs 200 ppm fast
or slow, a fast line stays within 2 ms of the Studio's clock after one minute and after ten; the correction
changes the rate by at most a few ppm a second and never steps the time; and the correction shown is within
20 ppm of the truth.

**The store.** Start from summaries of every 256 and every 4096 records, and measure. Trimming drops whole
pieces from the front, by the RAM budget the chart already has.

**The Registers tab's rows** (F-12). If they cost the table's code too much, take option (c): the channels and
their Plot ticks in the sidebar's card. Say so in the pull request, without a stop to ask.

**The `.evrs` pieces** (section 8.3): a name of 4 ASCII bytes, a length (u32, little endian: the body's bytes),
then the body.

- `EVRS`, always first: JSON, UTF-8, with `"format": "evre-fast-rec/1"`, `"device"` (the map's), `"stream"` (the
  stream's object from the map) and `"start"` (local time, ISO 8601 with milliseconds).
- `TIME`, 16 bytes: a record number (u64) and the writer's clock for it in seconds (f64): the Studio's `time_s`,
  or the seconds since `evre record` started. One before the first block of every start, then one a second.
- `BLK ` (the fourth byte a space): one block as it came.
- A reader skips a piece whose name it does not know, and stops at a piece that is cut off.

**The docs.** No chapter of STUDIO.md gets a new number. Fast streams get new sections at the ends of the
chapters they belong to: 7 (the chart), 12 (recordings), 13 (after Auto send), 16, 18 and the internals. The Help
gets a part for them. MAP_FORMAT.md gets a section for `streams`, and PROTOCOL.md a section "Fast EVRe" before
"Patterns", with an example block whose bytes a test builds and compares.

**What is checked where.** On Linux, by the session that builds a part: every check of section 10, and the
speed check as 20 s at 1 000 000 samples a second of two i16 from `evre_fake_fast` (no sample lost, no bad
block), with `EVRE_PERF_LOG`'s average paint at most 8 ms for two fast lines over a 10 s window. Left for the
Windows run, and said in each pull request: the card's picture of fast lines against the CPU's, ten minutes at
that rate with the frames a second live and held, and the new rows and the card at 225 %.

**New texts.** Each pull request lists its new user texts, English beside Arabic, for the owner's review. The
new terms go into the glossary of `translations/README.md`.

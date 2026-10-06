# EVRe 1.1 dev, rounds 3 and 4: the library made readable, your decisions, the documents

*This record was written in the owner's dev copy, before library 1.1 came into the repo on the branch
`evre-1.1`. "This folder" is that dev copy; `changes.diff` and the timing files stayed there. On the branch the
library is `EVRe/lib`, its suite `EVRe/tests`, and the 1.0 library the transcript compares with is frozen in
`EVRe/tests/lib_1.0`. Phase G of the work plan adds a section here for EVRe Guard part 2 (decisions D-26 to
D-50).*

For your review before anything reaches `teknile/EVRe`. Nothing outside this folder was changed: not
`teknile/EVRe`, not EVRe Studio, not the firmware, not the host program.

**The rule every change here follows:** EVRe moves bytes and never interprets them. The library learns *where* the
bytes are (a pointer table or ranges) and *asks* the layer above before it answers or stores (the handlers). What
the bytes mean, a token included, is EVRe Guard's business, the map's or the device's. This round added no meaning
to the library: `LOGIN_REQUIRED` is only a reserved code, and only a handler returns it. Round 4 tells a handler
which frame it runs for (`RX_SLAVE_ID`, a broadcast or not): that is framing, not meaning.

**Your two decisions after review round 2 come first** (D-25 and N11), then review round 2, then round 4, your
answers to Q1 to Q7. Sections 1 to 11 are round 3 as you reviewed it. Where round 4, review round 2 or D-25 and
N11 changed something there, a note in brackets says so.

## EVRe Guard part 2: your decisions D-26 to D-50

Part 2 checks the values a host writes (`docs/GUARD_PLAN.md`, revision 2). You confirmed every decision, G-1 to
G-25, as recommended on 2026-10-05. They are D-26 to D-50 here, in that order, and from now on a test, a class or a
mutant is named by its D-number. Phase G of the work plan builds them on the line of `evre-1.1`, in the parts G.0
to G.6, each with a pull request into `evre-1.1`.

| # | Plan | The&nbsp;question | Your decision |
|---|---|---|---|
| D-26 | G-1 | a&nbsp;value&nbsp;outside&nbsp;the&nbsp;limits | DECIDED: refused, never clamped. The check never stores and never returns `EVRE_HANDLED` |
| D-27 | G-2 | the&nbsp;codes | DECIDED: a new code 15, `VALUE_REFUSED`, for a value; 3 for a place. One line in `lib/EVRe.h`; the library never returns 15 |
| D-28 | G-3 | where&nbsp;the&nbsp;check&nbsp;sits | DECIDED: a unit of its own, `lib/guard/evre_guard_desc.*`, and `evre_guard_write_checked`, which puts the login first |
| D-29 | G-4 | a&nbsp;refused&nbsp;value&nbsp;and&nbsp;the&nbsp;session | DECIDED: it counts as activity (part 1 resets the idle time first) |
| D-30 | G-5 | part&nbsp;of&nbsp;a&nbsp;register | DECIDED: refused (3) for a number; any part of a bytes register may be written |
| D-31 | G-6 | bytes&nbsp;no&nbsp;entry&nbsp;covers | DECIDED: refused (3): a gap, a read-only register inside a writable range, past the last entry |
| D-32 | G-7 | NaN&nbsp;and&nbsp;the&nbsp;infinities | DECIDED: always refused (15), by a bit test |
| D-33 | G-8 | a&nbsp;special&nbsp;outside&nbsp;the&nbsp;limits | DECIDED: it passes: a sorted value list for each register |
| D-34 | G-9 | a&nbsp;closed&nbsp;set&nbsp;of&nbsp;values&nbsp;(A1) | DECIDED: a map key, `closed` |
| D-35 | G-10 | bits&nbsp;no&nbsp;field&nbsp;covers&nbsp;(A2) | DECIDED: a map key, `reserved_zero` |
| D-36 | G-11 | a&nbsp;register&nbsp;the&nbsp;device&nbsp;clamps | DECIDED: a map key, `past_limits` (`"refuse"` by default, `"clamp"`): the type's full range in the entry |
| D-37 | G-12 | limits&nbsp;into&nbsp;raw&nbsp;values | DECIDED: ceil and floor for integers, the nearest float for f32; past `FLT_MAX`, `FLT_MAX` |
| D-38 | G-13 | a&nbsp;bad&nbsp;or&nbsp;missing&nbsp;table | DECIDED (b): the device bank refused, the reserved bank let through |
| D-39 | G-14 | mirrors | DECIDED: refused (init fails; a write is refused whenever `ACCEPT_READ_RESP` is set) |
| D-40 | G-15 | broadcasts | DECIDED: the same check; one bad value drops the whole broadcast |
| D-41 | G-16 | init&nbsp;and&nbsp;the&nbsp;device | DECIDED: init checks the table against the device's ranges, and their order |
| D-42 | G-17 | the&nbsp;entry's&nbsp;format | DECIDED: 24 B, frozen, with spare members; `EVRE_GUARD_TABLE_FORMAT` 1 |
| D-43 | G-18 | diagnostics | DECIDED: a count, the last register and the reason |
| D-44 | G-19 | what&nbsp;stays&nbsp;out | DECIDED: all of its list (reads, read-only fields, w1c, actions, persist, run-time limits, state rules) |
| D-45 | G-20 | names&nbsp;and&nbsp;files | DECIDED: `lib/guard/evre_guard_desc.*`, `evre_guard_write_checked` in the same file |
| D-46 | G-21 | the&nbsp;generator | DECIDED: a new `--to guard`, with `--check` |
| D-47 | G-22 | the&nbsp;firmware's&nbsp;adoption | DECIDED: observe, then clamp, then refuse register by register (not part of phase G) |
| D-48 | G-23 | the&nbsp;firmware's&nbsp;limits | DECIDED: tied to the generated constants (not part of phase G) |
| D-49 | G-24 | comparing&nbsp;f32 | DECIDED (b): in integers, a key that sorts as the float does |
| D-50 | G-25 | the device's own frames and the login | DECIDED: the device stops them without a session (a rule in the docs and the header) |

### What is built, part by part

- **G.0** (the dev copy at home in the repo): CI runs this suite on Linux and Windows; the tools name code 13.
- **G.1** (the check, plan phase P1): `VALUE_REFUSED` (15) in `lib/EVRe.h`, its row in PROTOCOL.md's error table
  and its pin; `lib/guard/evre_guard_desc.h` and `.cpp`; `tests/guard_desc_test.cpp`, a check or more for each
  decision that the check carries (D-26 to D-33, D-36, D-38 to D-43, D-49) and one for each row of the wire table;
  the pins of every public name of parts 1 and 2 in `api_compat.cpp`; the lock build; PROTOCOL.md (the Layers
  diagram, the error table, "Register checks" with its wire table, Patterns, Pitfalls). D-34 and D-35 (the closed
  set, the bits that must be 0) come with G.4: until then init refuses a flag bit and `zero_bits`, as an older
  Guard refuses a newer table. Measured with arm-none-eabi-g++ 13.2 for a Cortex-M7 at `-Os`: 504 B for
  `evre_guard_check_write`, 610 B for `evre_guard_check_init`, 1447 B for the whole unit; 56 B of stack for the
  check.

## After review round 2: your decisions D-25 and N11

| # | The&nbsp;question | Your decision |
|---|---|---|
| NB9 | A mirror that answers a READ of STATUS (an echo of the host's own READ, say) stamps its own bit 14 over its device's | DECIDED, D-25 (option b): a mirror's STATUS is never refreshed |
| N11 | `changes.diff`'s paths were doubled (`a/EVRe/EVRe/...`) and needed `git apply -p3` | DECIDED: clean paths, `git apply -p1` |

**D-25, a mirror keeps its device's bit 14.** `refreshStatus()` returns at once when `ACCEPT_READ_RESP` is not 0.
So a mirror's STATUS is what its device's last READ_RESP stored, bit 14 included, and every path where STATUS goes
out carries that bit: a READ the mirror answers, a READ_RESP it builds from its registers. The mirror's own
`ACCEPT_BROADCAST_D000` does not touch it. A mirror that has stored no STATUS keeps what `protocolInit` wrote. A
device (`ACCEPT_READ_RESP` 0) refreshes exactly as D-24 says. It costs 6 B of flash; the decoder's stack is
unchanged. Where a reader finds it:

- `EVRe.cpp`: the comment of `refreshStatus`, and `protocolInit`'s ("as a device's STATUS goes out").
- `EVRe.h`: the comments of `CAP_BROADCAST_D000`, `ACCEPT_READ_RESP` and `ACCEPT_BROADCAST_D000`, the STATUS line
  of MIGRATING FROM 1.0, and step 4 of `protocolInit`.
- PROTOCOL.md: the STATUS table (row 14) and a paragraph under it; Broadcast; the encoder; A host's mirror (a new
  paragraph: the mirror's STATUS is its device's, and a program that serves its own clients from a copy marks the
  copy a mirror); Adding a device (`protocolInit`); a Pitfalls row; the STATUS row of the Migrating table.
- This file: NB9 decided (What stays open, round 4), and the follow-up in section 9, in your words.

**The tests of D-25.**

- `lib_test.cpp -DFEATURES`, 9 checks named "D-25:" in each program. A mirror with its own setting 0 that stored a
  READ_RESP of STATUS with bit 14 set: a READ it answers (of STATUS, of its high byte alone, of the whole reserved
  bank) and a READ_RESP it builds carry bit 14, and STATUS keeps it. The same with the setting 1 and a stored 0. A
  mirror that has stored no STATUS, setting 1: it serves what `protocolInit` wrote. The same `evre_base_t` set back
  to a device refreshes as D-24 says, both ways. 6 of the 9 fail on the library before D-25; the other 3 (the
  setup, and the device both ways) pass on both, as they must.
- The fuzz: an operation that may send STATUS's high byte is D24 on a device and D25 on a mirror. D25 is no class:
  the frozen library never refreshed STATUS, so a mirror's line must equal the old one. And it is exact on every line
  of the new run, the same as before or not: its k field is `k=` when the call left STATUS as it was and any
  READ_RESP it built carries STATUS as it was, `k!` when not.
- The transcript: an eighth build, a mirror that takes broadcasts (`MIRROR` and `BROADCAST_D000`). The classifier
  gives D-24 to a device only, so every STATUS frame of that build must be 1.0's.

**N11, clean paths.** The recipe now: a scratch tree with `a/` (the 1.0 files) and `b/` (every file of this
folder but `REVIEW.md` and `changes.diff`), and inside it

    git -c core.autocrlf=false diff --no-index --no-color --ignore-cr-at-eol --no-prefix a b

with every CR stripped from the output, and a new file's `diff --git b/X b/X` line written `diff --git a/X b/X`, as
git writes it inside a repository. So a changed file reads `--- a/lib/EVRe.h` and `+++ b/lib/EVRe.h`, a new one
`--- /dev/null` and `+++ b/tests/lib_test.cpp`. 13 files: 5 changed (`CONTRIBUTING.md`, `docs/PROTOCOL.md`,
`lib/EVRe.cpp`, `lib/EVRe.h`, `studio/docs/MAP_FORMAT.md`) and 8 new (`lib/guard/` and `tests/`).

To use it: `git apply changes.diff` (`-p1`, git's default) in a git work tree of 1.0: a clone of its repository,
or a copy of the folder after `git init`. In a work tree git reads 1.0's own `.gitattributes`
(`* text=auto eol=lf`), so it reads each file as LF and writes LF, whatever the file's line ends and whatever
`core.autocrlf` says. The 13 files then equal this folder's byte for byte, and no other file is touched.

Outside a work tree git reads no `.gitattributes`, and `core.autocrlf` alone decides. With `false` it refuses a
file with CRLF line ends; with `true` (Git for Windows' default) it writes the 13 files with CRLF. That is why a
work tree is the condition: the 1.0 folder here is not one, and three of its files have CRLF (`lib/EVRe.h`,
`lib/EVRe.cpp`, `lib/ports/stm32h7/malloc_lock.c`), though git stores all three with LF. `-p3` no longer applies.

The proof, each case once with `core.autocrlf=false` and once with `true`:

| Where&nbsp;`git apply`&nbsp;runs | Result |
|---|---|
| a fresh clone of a repository of 1.0, committed with its `.gitattributes` (no file with CR) | exit 0; the 13 files equal this folder's byte for byte; the other 108 files as they were, byte for byte |
| a plain copy of the 1.0 folder (3 files with CRLF), after `git init` | the same, with both settings |
| a copy of the folder with LF line ends, after `git init` | the same, with both settings |
| no work tree: `git archive` of that repository, unpacked; or the LF copy | `false`: the same. `true`: exit 0, but the 13 files have CRLF (equal to this folder's once CR is stripped) |
| no work tree: a plain copy of the 1.0 folder | `false`: exit 1, `EVRe.h` and `EVRe.cpp` refused, nothing written. `true`: exit 0, the 13 files with CRLF |
| `git apply -p3`,&nbsp;on&nbsp;a&nbsp;clone | exit 128 |

**The check of D-25 and N11.** A reviewer went over both and wrote a program of its own: 145 checks, devices and
mirrors, each setting, every path where STATUS goes out. It passes on this library and fails 55 checks on the
library before D-25. The code of D-25 is the early return and comments, nothing else. The check found two things,
both in this file:

- Section 9's follow-up was not in your words. It is now, word for word. A note after it says what the library
  does with each frame: it stores a READ_RESP's bytes; a WRITE_ACK_RESP carries none, so it is taken and nothing
  is stored. The older line there, "its server-side `evre_base_t` stays 0", now points to your note.
- `git apply -p1` on a plain copy of the 1.0 folder failed on its CRLF files, and two notes in brackets (sections
  2 and 7) said "a copy of 1.0" without the condition. The condition is a git work tree, as above; those notes,
  and the N11 line of What stays open, now say so. Neither 1.0 nor `changes.diff` changed.

**The numbers.** Measured again after the check, all the same: the two suites, FEATURES, the transcript, the fuzz
lines, size and stack, `changes.diff` and its proof, the scans. The other rows are from D-25's own run, and the
check measured each of them again, with the same result.

| Check | Result |
|---|---|
| `run_lib_tests.py --fuzz <the frozen library before the refactor>`,&nbsp;Windows&nbsp;(MinGW&nbsp;13) | **689 passed, 0 failed** (668 before: 9 FEATURES checks more in each program, and 3 for the new transcript build) |
| the&nbsp;same,&nbsp;Linux&nbsp;(WSL) | **695 passed, 0 failed** (674) |
| FEATURES,&nbsp;the&nbsp;two&nbsp;programs | 315 and 322 checks, all pass (306 and 313) |
| The&nbsp;1.0&nbsp;transcript | 12 096 frames, sha256 `7e64822467bc2a5c`, as before; now through 8 builds. The new one: F2 425, D-9 456, D-10 98 of 408, D-13 22 of 367, and every STATUS frame as 1.0's |
| Fuzz lines that differ, seed 1 / seed 2 x 300 000 cases | 81 047 / 81 327 (81 298 / 81 583), each in its class and checked; **0 outside the classes**; the same on Windows and Linux, at -O1 and -O2. D24 164 / 167 (267 / 274): the mirror lines left it. 747 / 786 D25 lines, each `k=`. The other classes moved only because lines that followed a mirror's refresh, counted "then" before, now stand in their own class: "then" 13 263 -> 13 103 and 13 335 -> 13 181; D3, D8, D9m, D11, D12, D12s, D17 and D21 gain 1 to 5 lines each, in one seed or both |
| The library before D-25 and after it, through this round's fuzz program (2 x 300 000 cases) | 277 / 272 lines differ, in 109 / 112 cases: every one a mirror case, and in each the first line that differs is a D25 line that shows `k!` before and `k=` after |
| The same, through this round's transcript builds | the six 1.1 builds of before: identical. The new build: 44 frames differ, every one a frame that sends STATUS's high byte, and each is 1.0's now |
| Warnings | the suite's check: 0 in 11 builds. And 0 in 288 builds of `EVRe.cpp` and EVRe Guard with `-Wall -Wextra -Wpedantic`: g++ 13 and arm-none-eabi-g++ 14.3, C++11 to C++20, -O0 to -O3, -Os, -Og, the CRC table in flash and in RAM, a lock |
| `EVRe.o`,&nbsp;Cortex-M7,&nbsp;`-Os` | 2974 B (2968 B); 2546 B with the CRC table in RAM (2540 B); 2994 B with the CMSIS lock (2988 B) |
| `decodePacketInto`'s&nbsp;stack,&nbsp;Cortex-M7,&nbsp;`-Os` | 64 B, 120 B with what it calls; 72 B with the CMSIS lock: unchanged |
| EVRe&nbsp;Guard,&nbsp;Cortex-M7,&nbsp;`-Os` | 614 B, unchanged |
| The check's own D-25 program, 145 checks (g++ 13, -O1, `-Wall -Wextra -Wpedantic`: 0 warnings) | 145 passed, 0 failed; on the library before D-25, 90 passed, 55 failed |
| The mutant m25: D-25 taken out, this round's tests, the fuzz at 100 000 cases | 19 of 689 suite checks fail: 6 FEATURES checks in each program and both programs' results, the new transcript build, and all 4 fuzz runs (72 and 99 lines outside the classes, at -O1 and -O2 alike) |
| `changes.diff` | 13 files, 8 161 lines, 389 929 bytes, no CR; made again by the recipe, byte-identical; `git apply -p1` in a git work tree of 1.0 gives this folder byte for byte, as above |
| The scans, every file of this folder | 0 of the names that must not appear, 0 section signs, 0 non-ASCII characters in code (the sources, and the code blocks of the Markdown files), 0 CR bytes |

## Review round 2: round 4 reviewed, and the fixes

Three reviewers went over the code and the documents of round 4: one for correctness, one for embedded use and
portability, one for the API and the documents. A second reviewer tried to refute each blocking finding. None of
them changed anything here; they wrote their own programs.

**What they found.** All 8 blocking items of review round 1 are fixed, B1 and B2 under the 64-bit clock too. D-19
to D-24 and Q6 are in exactly as you decided them. There is no bug in the library or in EVRe Guard, no frame that
answers outside a decided class, and no warning. They found 5 blocking problems, in the documents, the comments
and one test. The second reviewers confirmed all 5; after the fix they checked again and found each one fixed.

| # | What&nbsp;they&nbsp;found | The&nbsp;fix | The proof |
|---|---|---|---|
| MSG-IRQ-KEPT | PROTOCOL.md said an `addMsg` from an interrupt, during a write that clears the queue, is kept. One that lands before the clear (in the write handler, say) is cleared with the rest. REVIEW.md said a program had fired at every point; it had fired only after the clear. | The text says what happens: no frame acknowledges such a message by mistake; an ack run keeps it; a clear keeps it if it lands after the clear, and clears it if it lands before. | A program fires at 22 points (in the write handler, before each lock, after each unlock, in each ack handler): every one as the text says. |
| DOC-BC1,&nbsp;GUARD-BCAST-13 | Four sentences of PROTOCOL.md, and one of `evre_guard.h`, gave the answer of one `ACCEPT_BROADCAST_D000` setting as if it held for both: a broadcast of 0 bytes, the order of the checks, EVRe Guard's 13 for other broadcasts, and a checklist line that contradicted another one. | Each names its setting. With 0 a broadcast into the device bank gets 3 from the library, session or not. With 1 it reaches the Guard, which asks for a session (13). | Programs for both settings, 19 and 13 checks: all pass. |
| API-COMPAT-NAMES | `api_compat.cpp` never named `PERMISSION_ENUM`, `RESERVED_MAP_ENUM` or `BYTE_IND_ENUM`, nor the members of `evre_range_t`: 7 renames went through, though CONTRIBUTING.md says a rename stops it compiling. | `static_assert`s name each enum type through one of its values, and the members of `evre_range_t` by name and in their order. | 18 renames of `EVRe.h`, one at a time: each one stops the build. |
| EMB2-B1-int16-expectedSize | On a 16-bit `int` (AVR), `expectedSize` added in 16 bits: 10 + 0xFFFF gave 9, against its comment and this file. The answers were right all along: a frame under 10 bytes is refused first. | `(uint32_t) PACKET_BASE_SIZE + ...`.&nbsp;`getLE16`&nbsp;shifts&nbsp;its&nbsp;high&nbsp;byte&nbsp;as&nbsp;`unsigned`. | avr-g++ 5.4: 0x10009 now, 9 before. The ARM code of the sum is the same. |

**What changed.** The reviewers asked for 34 notes to be applied, three of them by two reviewers. All are applied:

- **The library.** `refreshStatus` sets or clears bit 14, it no longer flips it. Two refreshes that interleave (an
  AUTO_SEND from the main loop, and the decoder's interrupt between its check and its write) now leave the right
  bit. The rule of EVRe.h forbids that interleaving, but a 1.0 device that sends AUTO_SEND from its main loop did
  not have this failure before. A reviewer's race program: 2 of 3 checks failed with the flip, 3 of 3 pass now.
  +6 B. The 16-bit sum above. Comments.
- **The tests.** The fuzz prints two more fields. `e`: an 11-byte answer is the request's ERROR_RESP, with the
  return code in its code byte and the request's offset and count; D8, D2room and D12 require it. `h`: which
  handlers ran, in which order and on which `MSG_CNT`, without the queue bytes that D-13 changes; D13 requires it
  equal to the old line's. D9, D9m and D9u are checked exactly on every line, as D21 is. `api_compat.cpp` names
  the enum types and the range members, and uses a neutral map. The fuzz and the transcript keep their map (READ_MAX
  0xD0FE, WRITE_MIN 0xD0DC) until the next baseline: changing it moves every count in this file.
- **The documents.** The lock's longest hold (the compaction of a full queue: a few microseconds on a Cortex-M7).
  `--gc-sections`, for an image without the heap. What the CRC table in RAM costs: about 430 B of flash freed, 513
  B of RAM spent. `extern "C"` does not compile. Two lines in "Migrating from 1.0", and a pointer to it from
  EVRe.h. The checklist's unknown bank. The encoder's code 2 for any other code. The write handler's comment. The
  weak function on MinGW. `new (std::nothrow)` in "Adding a device". When the Guard looks at the clock (not for a
  skipped broadcast login). Which requests get 13 (MAP_FORMAT.md). An ack handler's message when the queue is full.
  The cases of `RANGE_TABLE_INVALID`. `now_ms` runs under the lock: it restores the interrupt mask, never enables.
  `ms64 = ms64 + 1` (C++20 deprecates `++` on a volatile). A Pitfalls row, and the Build options row, for the
  Guard's lock. Ragged lines rewrapped, no word changed.
- **This file.** The check counts, one way in every row. The transcript's sha since D-24. The stack of the
  device's wrapper. The host program and bit 14 (section 9). **`changes.diff`** is made again with the recipe
  exactly: the 8 new files' `diff --git` lines now read `a/EVRe/EVRe_dev/...`, as the recipe makes them. [N11:
  the recipe is new, and the paths are clean (`a/lib/EVRe.h`, `b/lib/EVRe.h`): `git apply -p1`. See the first
  section.]

**No answer changed.** The library before this round and after it, through the same test programs: the fuzz (2
seeds x 300 000 cases, 1.2 million lines each, at -O1 and -O2) and the six 1.1 transcript builds give identical
output, byte for byte.

**The mutants of this round.** Each is one change in a copy of the tree, through the whole suite (the fuzz at 100 000
cases), once with this round's tests and once with the tests from before it:

| Mutant | This round's tests: checks that fail, of them in the fuzz | The tests before this round |
|---|---|---|
| mK: a handler's 1 or 7 answered, but its ERROR_RESP carries 3 (N1) | 16,&nbsp;the&nbsp;fuzz&nbsp;4&nbsp;of&nbsp;4 | 12, the fuzz 0 of 4 |
| mK2: an ERROR_RESP that echoes a count of 0 | 10,&nbsp;the&nbsp;fuzz&nbsp;4&nbsp;of&nbsp;4 | 10, the fuzz 4 of 4 |
| mD3: the clear no longer runs `MSG_ACK_HANDLER[0]` (NB1) | 24,&nbsp;the&nbsp;fuzz&nbsp;4&nbsp;of&nbsp;4 | 20, the fuzz 0 of 4 |
| mD5: the ack of slot 0 runs `MSG_ACK_HANDLER[0]`, not its code's | 17,&nbsp;the&nbsp;fuzz&nbsp;4&nbsp;of&nbsp;4 | 15, the fuzz 2 of 4 |
| mE: a mirror answers 3 to a WRITE_ACK_RESP for 0xA000..0xA003, the old answer (NB3) | 6,&nbsp;the&nbsp;fuzz&nbsp;4&nbsp;of&nbsp;4 | 2, the fuzz 0 of 4 |
| mE2: a device takes a WRITE_ACK_RESP, the old answer | 17,&nbsp;the&nbsp;fuzz&nbsp;4&nbsp;of&nbsp;4 | 17, the fuzz 4 of 4 |
| mZ: the 16-bit fix taken out (it must change nothing on a 32-bit `int`) | 0:&nbsp;the&nbsp;suite&nbsp;passes,&nbsp;as&nbsp;it&nbsp;must | - |

The fuzz now catches the three it missed before (mK, mD3, mE); before, only the feature checks or the transcript
caught them.

**The numbers.**

| Check | Result |
|---|---|
| `run_lib_tests.py --fuzz <the frozen library before the refactor>`,&nbsp;Windows&nbsp;(MinGW&nbsp;13) | **668 passed, 0 failed** |
| the&nbsp;same,&nbsp;Linux&nbsp;(WSL) | **674 passed, 0 failed** (`configure_test.cpp` runs there, 7 of 7) |
| Fuzz lines that differ, seed 1 / seed 2 x 300 000 cases | 81 298 / 81 583, each in its class and checked; **0 outside the classes**; the same on Windows and Linux, at -O1 and -O2 |
| The&nbsp;1.0&nbsp;transcript | 12 096 frames, sha256 `7e64822467bc2a5c` |
| Warnings,&nbsp;`EVRe.cpp`&nbsp;and&nbsp;EVRe&nbsp;Guard | 0 in 828 builds with `-Wall -Wextra`, and 0 with `-Wpedantic` added: g++ 13 (300 builds), arm-none-eabi-g++ 14.3 (300), an old GCC 6.3 for i386 (120), avr-g++ 5.4 with a 16-bit `int` (108); C++11 to C++20, -O0 to -O3, -Os, -Og; the CRC table in flash and in RAM, the CMSIS lock, the test lock hooks. The suite's own check: 0 in 11 builds |
| `EVRe.o`,&nbsp;Cortex-M7,&nbsp;`-Os` | 2968 B (2962 B before this round); 2540 B with the CRC table in RAM (+513 B of RAM); 2988 B with the CMSIS lock (2996 B before) |
| EVRe&nbsp;Guard,&nbsp;Cortex-M7,&nbsp;`-Os` | 614 B (644 B with the CMSIS lock), as before |
| `decodePacketInto`'s&nbsp;stack,&nbsp;Cortex-M7,&nbsp;`-Os` | 64 B, 120 B with what it calls, as before; 72 B with the CMSIS lock (80 B before) |

**For you to decide.** Nothing new blocks. Two questions the reviewers raised are already under "What stays open"
below: a mirror that decodes an echoed READ of STATUS stamps its own bit 14 (NB9), and `changes.diff`'s doubled
paths (N11). [Both decided: D-25 and N11, the first section.] One more, an option: `evre_guard_logged_in` could
read the clock before it takes the lock (a late value counts as no time, so that is safe under D-22). The lock
would then never hold a call into the device's code. The text now tells a device how to write `now_ms` for the
lock as it is.

## Round 4: your answers, Q1 to Q7

### The decisions

All seven questions are DECIDED.

| # | The&nbsp;question | Your decision |
|---|---|---|
| Q1 | One write that clears the queue and acknowledges slots (review note N5) | DECIDED, D-19: the clear wins. Once `MSG_CNT` is written, the slot bytes of the same write do nothing. |
| Q2 | The range table checked only when `protocolConfigure` succeeds (N6) | DECIDED, D-20: checked whatever `protocolConfigure` returned; a bad table sets `D_RANGE_CNT` to 0. |
| Q3 | D-12 on a mirror: an ERROR_RESP handed to a mirror is 2, silent (N9) | DECIDED: kept as it is. No change. |
| Q4 | EVRe Guard's clock: 32 bits, a one-hour window, looks less than 49 days apart | DECIDED, D-22: a 64-bit clock the device supplies; the window and the 49-day rule are gone. |
| Q5 | The&nbsp;values&nbsp;of&nbsp;`LOGIN_REQUIRED`&nbsp;and&nbsp;`RANGE_TABLE_INVALID` | DECIDED: 13 and 14. No change. |
| Q6 | The&nbsp;author's&nbsp;name | DECIDED: "Ahmed Ragab AbdulGhany", in both "Author:" lines and PROTOCOL.md's Credits line; teknile stays the owner. |
| Q7 | Broadcast, and a login by broadcast | DECIDED, D-21 and D-23: a broadcast reaches the reserved bank only unless `ACCEPT_BROADCAST_D000` is 1, and EVRe Guard skips a login by broadcast. D-24 adds the STATUS bit. |

What each decision does:

- **D-19, the clear wins.** `writeReserved` returns right after the clear. So a write that covers `MSG_CNT` acts
  exactly as `MSG_CNT` alone: `MSG_ACK_HANDLER[0]` runs once, a message it queues is kept, no slot handler runs and
  no slot changes. `CONFIG` bytes before `MSG_CNT` in the same write are still stored. Before: 1.0 acknowledged
  the slots, and since its clear left the old codes in the buffer, the handlers of the messages just discarded ran
  after all; round 3 ran `MSG_ACK_HANDLER[0]` again for each slot. In both, a message that `MSG_ACK_HANDLER[0]`
  queued was acknowledged at once.
- **D-20, the table always checked.** `protocolInit` checks `D_RANGES` (when set) whatever `protocolConfigure`
  returned. A table that fails sets `D_RANGE_CNT` to 0 (fail closed). It returns `protocolConfigure`'s own code when
  that failed, else `RANGE_TABLE_INVALID` for a bad table, else `NO_ERROR`.
- **D-21, broadcast into the reserved bank only, by default.** New member `ACCEPT_BROADCAST_D000`, 0 by default,
  next to `ACCEPT_READ_RESP`. With 0 a broadcast WRITE into 0xD000..0xDFFF is refused with 3, silently, before any
  handler is asked and before anything is stored. With 1 the device bank takes it as in 1.0: a synchronised set
  point, one stop command for every device. Broadcasts into the reserved bank (CONFIG, the clear, the acks) are
  unchanged, and so is the encoder. This closes the broadcast login by default: the Guard's login register lies in
  the device bank.
- **D-22, the Guard's 64-bit clock.** Below.
- **D-23, no login by broadcast.** New member `RX_SLAVE_ID`: the frame's slave id while a handler runs, 0 for a
  broadcast. `evre_guard_write(guard, device, offset, data, count)` takes the device the handler got. A broadcast
  write that touches the login register has no effect at all: no login (the right token neither), no failure, no
  lockout, no logout, no activity, nothing stored (PERMISSION_DENIED, silent). With `ACCEPT_BROADCAST_D000` 0 the
  library refuses such a broadcast before the Guard is asked; with 1 the Guard skips it. Every other broadcast write
  that reaches the Guard still needs a session (LOGIN_REQUIRED, silent); with 0 the library refuses one into the
  device bank first (3), session or not.
- **D-24, the STATUS bit** (your answer to the open point on D-21). `CAP_BROADCAST_D000` = 0x4000, bit 14, was
  free. It goes out in STATUS exactly when `ACCEPT_BROADCAST_D000` is 1. The decoder refreshes that one bit each
  time STATUS's high byte goes out (a READ answered, the device's own READ_RESP from its registers), and writes the
  field only when the bit is wrong. So the bit is right whenever the device sets the flag, in `protocolConfigure` or
  after `protocolInit`. The decoder's stack is unchanged. [D-25: a mirror's STATUS is never refreshed; it serves
  the bit its device reported.]

### What changes, counted

**Transcript:** the 12 096 frames of the 1.0 comparison, now through 7 builds. The two new builds set
`ACCEPT_BROADCAST_D000` to 1, and must equal 1.0 on every broadcast. **Fuzz:** lines that differ from the frozen
library before the refactor, seeds 1 and 2 x 300 000 cases; every one carries its class, and its class checks the
new line. **Checks:** in `lib_test.cpp -DFEATURES` (in each of its two programs) and in `configure_test.cpp`,
counted without the setup lines (`protocolInit`, `evre_guard_init`). [Review round 2: counted one way in every
row; D-21 shares one check with D-10 and one with D-23.]

| # | What&nbsp;a&nbsp;host&nbsp;sees&nbsp;change | Transcript | Fuzz&nbsp;lines,&nbsp;seed&nbsp;1&nbsp;/&nbsp;seed&nbsp;2 | Checks |
|---|---|---|---|---|
| D-19 | a write of `MSG_CNT` and slots: `MSG_ACK_HANDLER[0]` once, its message kept, no slot acked | - (the transcript has no ack handlers: those frames stay in D-13, 367 frames, 22 changed, as before) | 169 / 170 (D19); each one is also decoded cut after `MSG_CNT`, on the same memory, and must give the same outcome | 7 in each program, and 1 lock count in the lock build |
| D-20 | nothing&nbsp;on&nbsp;the&nbsp;wire | - | - | 5 in `configure_test.cpp` (Linux) |
| D-21 | a broadcast WRITE into the device bank: 3, silent, nothing stored (1.0 stored it) | 187 frames, 68 changed, in each of the 4 default builds; the 2 broadcast builds have no D-21 frame | 494 / 534 (D21) [D-25: 494 / 535]; checked exactly on every line | 26 in each program, 1 shared with D-10 and 1 with D-23 |
| D-22 | nothing&nbsp;on&nbsp;the&nbsp;wire | - | - (the fuzz does not run EVRe Guard) | 9 |
| D-23 | with `ACCEPT_BROADCAST_D000` 1, a broadcast of the token changes nothing | - | the fuzz's handlers check `RX_SLAVE_ID` on every call | 12 in each program, and 1 shared with D-21 |
| D-24 | STATUS&nbsp;bit&nbsp;14&nbsp;as&nbsp;`ACCEPT_BROADCAST_D000`&nbsp;says | 60 frames, 44 changed, in each broadcast build; none in the 4 default builds, whose STATUS reads equal 1.0 | 267 / 274 (D24) [D-25: 164 / 167; a mirror's lines are D25 now] | 13 in each program, 2 in `configure_test.cpp`, 1 `static_assert` in `api_compat.cpp` |

The rest of the transcript is as in round 3: F1 408, F2 425, D-9 456, D-10 98 of 408 (mirror builds), D-13 22 of
367. **0 fuzz lines outside the classes**, the same on Windows and Linux, at -O1 and -O2.

**The mutants.** Each one reverts one decision in a copy of the tree; the tests are unchanged. All are caught.

| Mutant | What fails |
|---|---|
| m19: the write goes on past the clear to the slots (the `return` removed) | 8 FEATURES checks (19 suite checks in all); the fuzz, 14 and 12 lines outside the classes |
| m20: the table checked only after a good `protocolConfigure` | `configure_test.cpp` on Linux |
| m21: the device bank takes every broadcast | 22 FEATURES checks; the fuzz, 65 and 61 lines |
| m22: the 32-bit clock with the one-hour window | 7 FEATURES checks |
| m23g: the Guard takes a login by broadcast | 8 FEATURES checks |
| m23rx:&nbsp;`RX_SLAVE_ID`&nbsp;never&nbsp;set | 92 suite checks, `api_compat` among them; every fuzz run exits with code 3 |
| m24none, m24every, m24inv, m24low, m24early, m24sticky: bit 14 never refreshed, refreshed on every reserved read, inverted, judged on 0xA002, refreshed before the checks, never cleared | 27, 19, 32, 12, 8 and 10 checks; each also fails the fuzz |

### EVRe Guard's new clock (D-22)

- `now_ms` is `uint64_t (*)(void)`: milliseconds that count up and never go back. 64 bits, so it never wraps.
- `seen_ms` is `uint64_t` and keeps the newest value seen. A value not newer counts as no time: a late read, or a
  clock set back. Time then stands still until the clock catches up.
- At each look the step, in 64 bits, comes off `lock_left` and goes onto `idle_ms`, both saturating. They and the
  config limits stay `uint32_t`.
- Gone from the code and the docs: `LATE_READ_MS`, the one-hour window, and the rule that looks be less than 49
  days apart. The looks may be any time apart, and any `uint32_t` limit works, `0xFFFFFFFF` too.
- `evre_guard_restore(guard, failures, uint64_t now)`.
- The lock stays in `evre_guard_logged_in`, `_logout` and `_restore`: each update changes several fields, and a
  handler that ran in the middle of one would leave them out of step.
- The header shows how a device builds the clock on a 32-bit MCU: a 64-bit counter that a 1 ms SysTick adds to,
  read twice until both reads agree (or with interrupts masked). The value must be whole from the main loop and
  from the decoder's context.

### Confirmed as built (decided, no change)

- A broadcast into 0xD000..0xDFFF of any count, 0 bytes too, is refused with 3 before the bank's limits. It is
  silent either way; only the device's own caller of `decodePacketInto` sees 3 where 1.0 said 5.
- `RX_SLAVE_ID` is set just before a handler is asked (setting it right after the slave check cost 82 bytes of
  flash and 8 of stack). Outside a handler it holds the id of the last frame a handler was asked about.
- `evre_guard_read` keeps its signature, without the device: a broadcast READ never reaches a handler.
- `evre_guard_write` takes the device. A device that calls it from its own code, with `RX_SLAVE_ID` 0, has every
  login treated as a broadcast: that fails closed, and PROTOCOL.md says so.
- A skipped broadcast login leaves `guard.refused` and the clock untouched.
- `evre_guard_restore` with a `now` older than the newest value seen counts the lockout from that value: longer,
  never shorter.
- The guarded write path takes 44 bytes of stack on ARM (32 before). [Review round 2: plus 16 B in the device's
  wrapper, which pushes the 5th argument and cannot tail-call: a guarded write needs about 124 B of the decoder's
  stack at `-Os` (96 B in round 3).]

### The documents of round 4

- **PROTOCOL.md**, where a reader looks: Messages (a write that covers `MSG_CNT` is a clear; the old text on acks
  after a clear is gone); STATUS (row 14 and how bit 14 goes out); the error table (codes 3 and 14); the order of
  the checks (the broadcast rule first); Broadcast (rewritten: the default, `ACCEPT_BROADCAST_D000`, bit 14,
  `RX_SLAVE_ID`); test vector #12 (applied only with the setting 1); the encoder; Message API (STATUS among the
  device's own writes, and the lock text D-19 made true); Ranges and handlers (D-20, `RX_SLAVE_ID`); a new
  subsection **EVRe Guard** (the answers, no login by broadcast, the session per link, the 64-bit clock, the lock
  rule, the wiring with the new `evre_guard_write` and a clock); the mirror (STATUS as reported); Adding a device
  (what `protocolInit` returns); 3 pitfalls; 6 checklist lines; Migrating from 1.0 (the broadcast paragraph, 3 rows,
  STATUS); the Credits line (Q6).
- **CONTRIBUTING.md**: the layering rule names `RX_SLAVE_ID` as framing; the tests table has `configure_test.cpp`.
- **studio/docs/MAP_FORMAT.md**: a tool logs in to the map's `slave`, never by broadcast; which requests get 13.
- **changes.diff**: made again, the same way; `tests/configure_test.cpp` is new, against `/dev/null`. [N11: made
  now with clean paths, for `git apply -p1`.]

How it was checked: six scratch programs, outside this folder, ran every new statement against the library. One
(66 checks, Windows and Linux) covers the D-19 frames, the broadcasts, STATUS, and the Guard's broadcast rules and
clock. One (5 checks, Linux) runs `protocolInit` with a real `protocolConfigure` for D-20 and D-24. One fires an
interrupt's `addMsg` after the clear's lock of a clear-and-slots write, and in an ack run: the message is kept
every time. One that lands before the clear is cleared with the rest. [Review round 2: the text said "at every
point outside the lock"; the points before the clear were never fired at. A review check then fired at 22 points,
in the write handler, before each lock, after each unlock and in each ack handler: every one as PROTOCOL.md now
says.] One shows the torn-clock pitfall. One takes the two new C blocks out of PROTOCOL.md as they are, builds
them with `-Werror`, and runs them. One runs the clear-and-slots write on 1.0, on the library before the refactor
and on this one, for the "1.0" column of the migration table.

### The proof at the end of round 4

| Check | Result |
|---|---|
| `run_lib_tests.py --fuzz <the frozen library before the refactor>`,&nbsp;Windows&nbsp;(MinGW&nbsp;13) | **668 passed, 0 failed** (516 at the end of round 3) |
| the&nbsp;same,&nbsp;Linux&nbsp;(WSL) | **674 passed, 0 failed**: 6 more, because `configure_test.cpp` runs there (7 of 7); on MinGW it is only compiled |
| FEATURES,&nbsp;the&nbsp;two&nbsp;programs | 306 and 313 checks, all pass |
| Warnings,&nbsp;`-Wall -Wextra` | 0: `EVRe.cpp` in 11 builds, EVRe Guard and `configure_test.cpp` in 10 each |
| `api_compat` | passes in C++11, C++17, with the CRC table in RAM, and with `<windows.h>` first |

Size and stack, arm-none-eabi-g++ 14.3, Cortex-M7, `-Os`:

| | end&nbsp;of&nbsp;round&nbsp;3 | D-19&nbsp;to&nbsp;D-23 | with&nbsp;D-24 | after review round 2 (now) |
|---|---|---|---|---|
| `EVRe.o` | 2842&nbsp;B | 2916&nbsp;B | 2962&nbsp;B | 2968 B |
| `decodePacketInto`&nbsp;stack&nbsp;frame | 64&nbsp;B | 64&nbsp;B | 64&nbsp;B | 64 B (120 B with what it calls) |
| EVRe&nbsp;Guard | 572&nbsp;B | 614&nbsp;B | 614&nbsp;B | 614 B |
| the&nbsp;guarded&nbsp;write&nbsp;path's&nbsp;stack | 32&nbsp;B | 44&nbsp;B&nbsp;(`evre_guard_write`&nbsp;32,&nbsp;its&nbsp;clock&nbsp;look&nbsp;12) | 44&nbsp;B | 44 B; plus 16 B in the device's wrapper, which pushes the 5th argument and cannot tail-call: about 124 B of the decoder's stack (96 B in round 3) |

### What stays open

Only what is new this round:

- **The STATUS field lags.** Bit 14 is right on the wire every time, but `dev->STATUS` gets it only when STATUS
  next goes out. `protocolInit` does not set it even when the flag is set before the call: that keeps its contract
  that after `protocolConfigure` nothing but `D_RANGE_CNT` is written. Confirm.
- **STATUS joins the device's own writes that must not preempt the decoder** (with CONFIG and `MSG_*`), because
  the decoder now writes it, once after each change of the setting. Fine for a 1.0 device that changes STATUS at
  run time from another context?
- **A mirror that answers a READ of STATUS** (NB9): [decided, D-25: a mirror's STATUS is never refreshed; it
  serves the bit 14 its device reported.]
- **The transcript's "L" lines** never show their answer bytes (the length is taken before the decode runs), in any
  build or class. An old test weakness. The fix is one line, but it changes what every class sees, so it belongs
  in a round of its own.
- **`changes.diff`'s paths were doubled** (N11): [decided: clean paths, `a/lib/EVRe.h` and `b/lib/EVRe.h`; it
  applies with `git apply -p1`, git's default, in a git work tree of 1.0. The recipe is in the first section.]
- **The fuzz's and the transcript's map** still has the bounds of a real device (READ_MAX 0xD0FE, WRITE_MIN
  0xD0DC). A neutral map moves every fuzz count and the transcript's sha, so it waits for the next baseline
  (review round 2, N16).
- **Before any merge:** a second review round, over the code of round 4 and these documents. [Done: Review
  round 2, above.]

## 1. This round in short

| Stage | What | How it is proved |
|---|---|---|
| 1 | `lib/EVRe.cpp` rewritten as small functions, `lib/EVRe.h` made safer to include. Not one answer changed. | The differential fuzz: 1.8 million operations, identical line for line. 30 plausible reorderings, all caught. |
| 2 | The audit's corner cases, fixed as you decided them: D-1 to D-18. | Each decision has checks that fail on the stage-1 library and pass now. Every frame that answers differently falls in a named class, and the class checks the new answer. |
| 3 | The documents: PROTOCOL.md checked against the code, line by line; CONTRIBUTING.md; a note in MAP_FORMAT.md; this file; `changes.diff`. | Small programs ran every statement of the spec against the library; the Python host example ran against the real library. |
| Review | Three reviewers tried to break it: 7 blocking findings, all fixed (section 8). Two were real bugs in EVRe Guard; the library's answers did not change. | The reviewers' own programs pass now; 11 mutants, all caught. |
| Round&nbsp;4 | Your answers to Q1 to Q7: D-19 to D-24 in the code, the tests and the documents (the section above). | Every changed frame in a checked class; 12 mutants, all caught. |
| Review&nbsp;2 | Three reviewers on round 4: 5 blocking problems, in the text and one test, all fixed; 34 notes applied (the first section). | No answer changed: the fuzz and the transcripts identical before and after; 6 new mutants, each caught by the fuzz. |

## 2. What to review, in this order

| File | What changed |
|---|---|
| `lib/EVRe.h` | "MIGRATING FROM 1.0" at the top; `MSG_ACK_HANDLER[256]`; codes 13 `LOGIN_REQUIRED` and 14 `RANGE_TABLE_INVALID`; `EVRE_LOCK` / `EVRE_UNLOCK`; `EVRE_WEAK`; `SLAVE_ID_REG` beside `SALVE_ID_REG`; the contracts written down (the weak `protocolConfigure`, the order of `protocolInit`, who owns `D000` and `A000`, in-place decoding, the silence rules, `HEARTBEAT`) |
| `lib/EVRe.cpp` | stage 1's small functions; stage 2's decisions (section 4) |
| `lib/guard/evre_guard.*` | EVRe Guard, with the stage-2 fixes (section 5) and the review's (section 8: time counted, the lock) |
| `docs/PROTOCOL.md` | section 6 |
| `tests/` | `lib_test.cpp` (the 1.0 transcript and the features), `run_lib_tests.py`, `fuzz_test.cpp`, `api_compat.cpp`, `lock_hooks.h`, and since round 4 `configure_test.cpp` (a device's own `protocolConfigure`) |
| `CONTRIBUTING.md`,&nbsp;`studio/docs/MAP_FORMAT.md` | section 6 |
| `changes.diff` | everything above as one diff against `teknile/EVRe` 1.0 (made with `--ignore-cr-at-eol`) [N11: with clean paths, `a/lib/EVRe.h` and `b/lib/EVRe.h`: `git apply -p1` in a git work tree of 1.0 (a clone, or a copy after `git init`) gives this folder byte for byte; the first section says why a work tree] |

## 3. Stage 1: the library made readable, and nothing else

**The rule of this stage:** not one answer, return code or stored byte changes, for any frame or call. Three 1.0
behaviours that are bugs were kept, each pinned by a test, for stage 2 to turn round (it did: D-1, D-2, D-3).

| File | Change |
|---|---|
| `lib/EVRe.cpp` | One small function per job: the header parsed once; checks per bank (`checkRead`, `checkWrite`, `checkBounds`, `checkRanges`); `readBank`, `writeBank`, `copyToBank`, which hide the pointer table, the ranges and the reserved bank; the messages (`clearMessages`, `ackMessage`, `compactMessages`, in place); the frame builders (`putHeader`, `sealFrame`). No goto. No byte-pointer views of a `uint16_t` (`getLE16`, `putLE16`). Offset + count in 32 bits. `decodeCore` is 57 lines, was 419. |
| `lib/EVRe.h` | Compile-only: every enum value written out; the index and bit macros parenthesised; `crctab16` defined once, in `EVRe.cpp`; `NO_ERROR` set aside when `windows.h` came first; the handler typedefs; `struct evre_range`; `SLAVE_ID_REG` beside `SALVE_ID_REG` (one byte); `EVRE_WEAK`; the 1.1 members moved behind `MSG_ACK_HANDLER`, so every 1.0 member is back at its 1.0 offset. |
| `tests/` | `fuzz_test.cpp` (new: the differential fuzz), `api_compat.cpp` (new: every public name), three counts past 0x2000 in the transcript, 35 checks that tell apart two orders of the same steps, the warning check at -O0 to -O3. |

| Check | Result |
|---|---|
| The&nbsp;suite,&nbsp;Windows&nbsp;and&nbsp;Linux | 156 and 154 passed, 0 failed |
| Differential fuzz, before against after | 3 seeds x 300 000 cases, 1.8 million operations: **identical line for line**, at -O0, -O1, -O2, -Os, -O3, as C++11 and with the CRC table in RAM |
| Mutation study: 30 plausible reorderings of the new code | the suite catches all 30, the fuzz all 30; the suite from before this stage missed 15 |
| Warnings,&nbsp;`-Wall -Wextra` | 0 at every level, C++11 to C++20 (before: `-Wstringop-overflow` at -O2 and -O3) |
| Flash,&nbsp;`-Os`,&nbsp;Cortex-M7 | 3416 B -> 2568 B (CRC table in RAM: 2960 B -> 2112 B) |
| `decodePacketInto`&nbsp;stack,&nbsp;`-Os` | 320 B -> 56 B (the 255-byte copy of the queue is gone) |

Where "the same" needs a word: it differs only where the old code was undefined or hung. A `DEVICE_REG_READ_MAX`
above 0xDFFF (the old 16-bit loops never ended). A range table `protocolInit()` would refuse, set after it.
Memory that overlaps in ways that make no sense (`pData` inside the header being built).

## 4. Stage 2: your decisions

**The rule of this stage:** exactly the decisions, nothing else. Each has checks in `lib_test.cpp -DFEATURES`,
named `D-n:`. The last column counts them: how many fail on the stage-1 library, out of how many.

Two measures of what changes. **Transcript:** the 12 096 frames of the 1.0 comparison, per 1.1 build. **Fuzz:**
the lines that differ from the frozen library before this round, over 3 seeds x 300 000 cases; every one carries
its class, and its class checks the new line. (Since the review, section 8, the classes check more of each line;
the lines are the same, but 10 in-place writes to the queue now count under D-13.)

| # | Decision | What&nbsp;a&nbsp;host&nbsp;sees&nbsp;change | Transcript | Fuzz&nbsp;lines | Checks |
|---|---|---|---|---|---|
| D-1 | `MSG_ACK_HANDLER[256]`;&nbsp;`protocolInit`&nbsp;clears&nbsp;all&nbsp;256 | acknowledging message 0xFF runs its own handler, not the write handler | - | -&nbsp;(undefined&nbsp;before) | 2 / 3 |
| D-2 | READ and WRITE_ACK in this order: checks, room, handler, store, answer. The slave id taken before the store. `outBuf` may be `PACKET`. | a READ too big for the buffer is 11 before a handler is asked; a WRITE_ACK decoded in place stores the data | - | 2&nbsp;165&nbsp;(room)&nbsp;+&nbsp;218&nbsp;(in&nbsp;place) | 6 / 19 |
| D-3 | `*outLen = 0`&nbsp;first,&nbsp;in&nbsp;every&nbsp;call | no frame; the caller's length is 0 | - | 46&nbsp;697 | 4 / 4 |
| D-4 | `decodePacket`: 11 bytes; a READ that is ours sized by its count, a bank at most; 11 again if that fails | on a small heap, an ERROR_RESP instead of silence | -&nbsp;(sizes,&nbsp;seen&nbsp;through&nbsp;`--wrap=calloc`) | - | 5 / 8 |
| D-5 | the RAM CRC table built by the first `GetCrc16` | a host that only encodes builds right CRCs | - | - | 1 / 2 |
| D-6 | `DEVICE_REG_READ_MAX`&nbsp;above&nbsp;0xDFFF&nbsp;taken&nbsp;as&nbsp;0xDFFF | code 5 past 0xDFFF (it wrapped, or hung) | - | - | 2 / 4 |
| D-7 | a&nbsp;range&nbsp;table&nbsp;`protocolInit`&nbsp;refuses:&nbsp;`D_RANGE_CNT = 0` | the device bank serves nothing (it served the bad table) | - | with&nbsp;D-17 | 2 / 4 |
| D-8 | silence decided by the frame; a handler's 1 or 7 answered; EVRE_HANDLED from the read handler is NO_ERROR | ERROR_RESP 1 or 7 where there was silence; a READ_RESP where there was ERROR_RESP 0xFF | - | 3&nbsp;921 | 6 / 8 |
| D-9 | a device refuses WRITE_ACK_RESP (2, silent); a mirror checks only the bank | nothing on the wire; the return code, and no HEARTBEAT | 456&nbsp;of&nbsp;456 | 30&nbsp;722 | 3 / 5 |
| D-10 | HEARTBEAT only for an accepted READ, WRITE or WRITE_ACK | a mirror's CONFIG is the device's | 98&nbsp;of&nbsp;408&nbsp;(mirror&nbsp;builds) | 2&nbsp;172 | 2 / 8 |
| D-11 | the start and end bytes checked: 1, silent | silence for a frame with a wrong delimiter | - | 66&nbsp;211 | 3 / 4 |
| D-12 | the function code before the length | ERROR_RESP 2, not 12, for an unknown code with data | - | 20&nbsp;587 | 2 / 3 |
| D-13 | past MSG_CNT the queue reads 0 after an ack or a clear | a READ of the whole queue shows no old codes | 22&nbsp;of&nbsp;367 | 1&nbsp;832 | 2 / 2 |
| D-14 | the encoder builds no broadcast but a WRITE | a host is told 2 before it sends a frame no device answers | - | 758 | 4 / 5 |
| D-15 | `EVRE_LOCK()` / `EVRE_UNLOCK()`, empty by default, around `addMsg` and the decoder's clear and compaction | nothing, unless a device defines them | - | - | 4 / 7 (the lock build) |
| D-16 | `ACCEPT_READ_RESP` stays 0; "MIGRATING FROM 1.0" in `EVRe.h` | nothing | - | - | 0 / 1 (documentation) |
| D-17 | 13&nbsp;`LOGIN_REQUIRED`,&nbsp;14&nbsp;`RANGE_TABLE_INVALID` | a&nbsp;guarded&nbsp;device&nbsp;sends&nbsp;13 | - | 27&nbsp;492&nbsp;(with&nbsp;D-7) | 1 / 2 |
| D-18 | EVRe&nbsp;Guard&nbsp;(section&nbsp;5) | 13&nbsp;without&nbsp;a&nbsp;session | -&nbsp;(not&nbsp;in&nbsp;the&nbsp;fuzz) | - | 19 / 24 |

Also in the transcript, from round 1: **F1**, a READ_RESP sent to a device, 408 frames on the device builds, all
refused now; **F2**, a write to an unknown bank, 425 frames, all refused with code 3. And in the fuzz, 38 919 lines
later in a case whose memory a classified line had changed. **0 lines outside the classes.**

Where a decision needed a word:

- **D-2** keeps one detail of 1.0: the ack of a write to the slave id itself carries the old id, the one the host
  is listening for.
- **D-9:** the bank is the bank of the start offset (0xA000 or 0xD000, else 3). Nothing is stored and no handler is
  asked.
- **D-12:** an ERROR_RESP sent to a mirror is also 2, silent: the library decodes none. A host reads its code byte
  itself.
- **D-13** and a clear followed by acks in one write: the acks find empty slots and run `MSG_ACK_HANDLER[0]` again.
  That is what a byte-by-byte write does. [Round 4, D-19: such a write is the clear alone now; its slot bytes do
  nothing.]
- **D-15:** the lock is for `addMsg` from a context the decoder preempts, a main loop. The ack itself takes none:
  `addMsg` writes only at MSG_CNT, above every slot an ack can name.
- **D-17** changes the value of `RANGE_TABLE_INVALID`, a name 1.1 added and never released. `api_compat` pins 13
  and 14.

## 5. EVRe Guard

The login layer above the protocol, in `lib/guard/`. It plugs into the two handlers and is not part of the
protocol: a device uses it or not.

**What it does** (from round 1, unchanged):

- Before a login only DEVICE_ID and STATUS (0xA000..0xA003) and the configured `open_reads` can be read, and only
  the login register written.
- A login is a write of exactly `login_size` bytes (32 at most) to `login_addr`. The token is compared in constant
  time and never stored (the handler answers `EVRE_HANDLED`).
- A session ends with `evre_guard_logout()`, a login attempt that fails, or `idle_logout_ms` without a read or a
  write.
- Brute force: after `free_attempts` wrong tokens every attempt is refused for `lockout_ms`, the right token too,
  which is not even compared. Each further failure doubles the lockout, up to `lockout_max_ms`.

With `free_attempts` 3, `lockout_ms` 1000 and `lockout_max_ms` 60000, at most 1 attempt a minute after a short
ramp. Trying every token:

| Token | Tokens | With&nbsp;the&nbsp;lockout | Without (1000 frames/s) |
|---|---|---|---|
| 4&nbsp;digits | 10^4 | 7&nbsp;days | 10 s |
| 6&nbsp;lower-case&nbsp;letters | 3.1&nbsp;x&nbsp;10^8 | 590&nbsp;years | 3.6 days |
| 8&nbsp;random&nbsp;printable&nbsp;characters | 6.6&nbsp;x&nbsp;10^15 | 10^10&nbsp;years | 200 000 years |
| 16&nbsp;random&nbsp;bytes | 3.4&nbsp;x&nbsp;10^38 | never | never |

So: with the lockout, 8 random characters or more is safe; a PIN is not.

**What stage 2 changed (D-18):**

| Change | Why |
|---|---|
| Time is counted: at each look at the clock the step since the last look comes off the lockout and goes onto the idle time (review round 1, B2; stage 2 compared `now - since` with the limit) | a signed difference turns negative after 2^31 ms (25 days): an idle session, or an old lockout, came back. And a stamp compared with a limit near 2^32 ends only for a look in a 1 ms window |
| The doubling saturates, and a lockout ends at the first look after it ran out | the lockout ends at every access, not only at the next attempt; `lockout_max_ms` 0xFFFFFFFF no longer wraps to no wait at all |
| `evre_guard_init` returns NO_ERROR or PERMISSION_DENIED, and fails closed | a null token or clock, `login_size` 0 or above 32, or `open_reads` missing crashed in the interrupt, or let a write with no token log in |
| In a session, a write over the login register that is not a login is refused, and the session goes on | a block write of the settings next to it logged the host out and counted a failure |
| `evre_guard_restore(guard, failures, now)` | a device that keeps the count in non-volatile memory gives it back, so a reset hands out no free attempts |
| No session: `LOGIN_REQUIRED` (13). A failed login, whatever the reason: `PERMISSION_DENIED` (3) | a host can tell "log in again" from "no such register", and still learns nothing about a token |
| `evre_guard_logged_in()`, `_logout()` and `_restore()` take `EVRE_LOCK()`; a clock value up to an hour older than the last look counts as no time (review round 1, B1) [round 4, D-22: the window is gone; the clock has 64 bits, and any value not newer counts as no time] | a main loop's call could undo a lockout, or a login, that the decoder's interrupt had just made |
| A write of 0 bytes is no login attempt (review round 1, N3) | at `login_addr + 1` it counted as a failed login, at `login_addr` it did not |

Its size, `-Os`, Cortex-M7: 422 B -> 538 B (stage 2) -> 572 B (602 B with the CMSIS lock). [Round 4: 614 B.]

## 6. The documents (stage 3)

The spec-against-code audit has 30 findings (SV-1 to SV-30), 19 of them for the spec; the other three audits asked
for documentation in some 20 more. Each one is now in PROTOCOL.md where a reader looks for it, with every stage-2
behaviour:

| Topic | Where&nbsp;in&nbsp;PROTOCOL.md | Audit |
|---|---|---|
| The frame length per function code (READ and WRITE_ACK_RESP 10 bytes, ERROR_RESP 11) | Frame (a table), Framing on a byte stream, code 12, the checklist, the host example | SV-2 |
| The host example: it waited a full timeout after every acknowledged write, and broke on the next frame | Host implementation: frames by function code, matches each answer to its request, skips AUTO_SEND frames | SV-3 |
| Count&nbsp;0&nbsp;is&nbsp;a&nbsp;valid&nbsp;request | Frame | SV-27 |
| The&nbsp;order&nbsp;of&nbsp;the&nbsp;checks | Errors, a new table: what is checked first decides the code | RF-4, RF-8, D-2, D-12 |
| When a device is silent, all the cases | Errors, a new table; decided by the frame | SV-7, D-8 |
| An "on the wire" column; codes 13 and 14 | Errors | SV-26, D-17 |
| Codes 3, 4 and 5, and where a write ends in the reserved bank | Register model, Permissions (a table), the error table | SV-10 |
| A write into a gap between ranges is 5, not 3 | Ranges | SV-11 |
| HEARTBEAT: which frames, when, who clears it | CONFIG | SV-14, D-10 |
| The 100 Hz "default" and MSG_ENABLE are the device's | CONFIG,&nbsp;Messages | SV-15 |
| Messages: ack all runs `MSG_ACK_HANDLER[0]` only; 0xFF has a slot; the tail reads 0; the value written does not matter | Messages | SV-12, D-1, D-13 |
| `addMsg` against the decoder: the lock hooks | Message API, Build options, Pitfalls | SV-13, D-15 |
| The handlers: what they are asked, their answers, EVRE_HANDLED, 1 and 7 answered | Ranges&nbsp;and&nbsp;handlers&nbsp;(a&nbsp;table) | SV-8, D-2, D-8 |
| Range tables: every invalid case, set before `protocolInit`, 0 ranges | Ranges&nbsp;and&nbsp;handlers | SV-21, D-7 |
| A host's mirror: its setup, ACCEPT_READ_RESP for READ_RESP and WRITE_ACK_RESP | a&nbsp;new&nbsp;section,&nbsp;A&nbsp;host's&nbsp;mirror | SV-27, D-9, D-16 |
| The encoder: its own map, `pData` null, no broadcast but WRITE, no answers | Device API, a new section, The encoder | SV-24, D-14 |
| In-place decoding, the allocating form's sizing, `outLen` | Device&nbsp;API | D-2, D-3, D-4 |
| The test vectors: the device they assume, and three answers that were missing (#15 to #17) | Test&nbsp;vectors | SV-19 |
| Two frames in one transfer are both dropped | Framing,&nbsp;Pitfalls | SV-16 |
| Little endian: fields on any CPU, the reserved bank only on a little-endian one | Frame,&nbsp;Build&nbsp;options&nbsp;(porting) | SV-23, RF-11 |
| The runtime CRC table, `EVRE_WEAK`, the `malloc_lock.c` path, the headers used | Build&nbsp;options | SV-28, D-5 |
| `protocolConfigure` is a contract; what `protocolInit` does, in order; who owns `D000`; WRITE_MIN 0 makes all writable | Adding&nbsp;a&nbsp;device | API-2, API-3, API-10, CC-29 |
| Migrating from 1.0: rebuild everything, a mirror sets ACCEPT_READ_RESP, A000 is nullptr, every frame and call that answers differently | a new section, Migrating from 1.0 | API-1, API-11, API-14, SV-20 |
| EVRe Guard: the login register in its own range, the session per link, 49 days, restore | Ranges and handlers [round 4: a subsection of its own, EVRe Guard; the 49 days are gone] | CC-3, CC-17, CC-27, D-18 |

**How it was checked.** Three scratch programs, outside this folder:

- one runs every statement of the spec against the library: the 17 test vectors on the device the spec describes,
  both directions; the codes 3, 4 and 5; count 0; HEARTBEAT; every silent case; the handlers' answers; in-place
  decoding; the messages; the range table's invalid cases; the pointer path's limits. All as written.
- one is built against 1.0 and against 1.1, with the 1.0 API only: every row of the migration tables is what the
  two libraries do.
- one runs the spec's Python host, as it is written, against the real library (built as a DLL) through a fake
  serial port, with AUTO_SEND frames before and after each answer. The old example waited a full timeout after every
  acknowledged write; the new one never waits, skips the AUTO_SEND frames, and reports codes 3, 4 and 5.

**The other documents:**

- `CONTRIBUTING.md`: the layering rule stays; a section for the library ("the public API only grows"); the test
  commands (`run_lib_tests.py`, `--fuzz`, `fuzz_test.cpp` by hand, `api_compat.cpp`). It says that CI runs the
  tools' tests, and the library's are run by hand.
- `studio/docs/MAP_FORMAT.md`: under the top level, how a device with EVRe Guard takes the `login` token, and its
  codes 13 and 3.
- `changes.diff`: made again, the same way as before. [N11: the recipe is new, the paths clean, `git apply -p1`.]

## 7. The proof, at the end of the round

| Check | Result |
|---|---|
| `run_lib_tests.py`,&nbsp;Windows&nbsp;(MinGW&nbsp;13) | **503 passed, 0 failed** after the review's fixes (472 before them) |
| the&nbsp;same&nbsp;with&nbsp;`--fuzz <the frozen library before this round>` | **516 passed, 0 failed** (485 before) |
| `run_lib_tests.py`,&nbsp;Linux&nbsp;(GCC&nbsp;15) | **501 passed, 0 failed**, and **514** with `--fuzz` (no `windows.h` build there) |
| The&nbsp;1.0&nbsp;transcript | 12 096 frames, sha256 `4d03ac44044f02f7`, the same on both [Round 4: `7e64822467bc2a5c`; each line also shows STATUS since D-24] |
| Differential fuzz matrix, 3 seeds x 300 000 cases | the new library at -O0, -O1, -O2, -Os, -O3, as C++11 and with the CRC table in RAM: identical to each other; against the old one, 0 lines outside the classes; every one of the 64 672 frames decoded in place has the outcome of two buffers |
| The library before and after the review's fixes | the fuzz identical, line for line (2 seeds x 300 000 cases): only a `static_assert` was added to `EVRe.cpp` |
| Warnings | 0: `-Wall -Wextra` at -O0 to -O3, C++11 and C++17; ARM at -O0, -O2, -Os, -O3, the CRC table in flash and in RAM, with and without the CMSIS lock; EVRe Guard with `-Werror` in 20 builds |
| `changes.diff` | made again; applied to a copy of 1.0 it gives this tree, file for file [N11: with `git apply -p1` in a git work tree of 1.0 (a clone, or a copy after `git init`), byte for byte] |

Size and stack, arm-none-eabi-g++ 14.3, Cortex-M7, `-Os`:

| | 1.0 | 1.1&nbsp;before&nbsp;this&nbsp;round | stage&nbsp;1 | stage 2 (now) |
|---|---|---|---|---|
| Flash,&nbsp;CRC&nbsp;table&nbsp;in&nbsp;flash | 2802 B, + 1048 B of heap for the reserved bank | 3416&nbsp;B | 2568&nbsp;B | 2842 B |
| Flash,&nbsp;CRC&nbsp;table&nbsp;in&nbsp;RAM | - | 2960&nbsp;B | 2112&nbsp;B | 2436 B (+ 1 B of RAM) |
| `decodePacketInto`&nbsp;stack&nbsp;frame | 304&nbsp;B | 320&nbsp;B | 56&nbsp;B | 64 B (120 B with what it calls) |
| `sizeof(evre_base_t)` | 1304&nbsp;B | 1320&nbsp;B | 1320&nbsp;B | 1324 B (every 1.0 offset the same) |
| EVRe&nbsp;Guard | - | 422&nbsp;B | 422&nbsp;B | 538 B; 572 B after the review (602 B with the CMSIS lock), 24 B of stack for a handler |

## 8. The review, round 1, and the fixes

Three reviewers tried to break the round: one for correctness, one for embedded use and portability, one for the
API and the documents. They wrote their own programs and changed nothing here. They found 7 blocking problems. All
are fixed. The library's answers did not change: the fuzz of the library before and after the fixes is identical,
line for line (2 seeds x 300 000 cases). Two of the problems were real bugs in EVRe Guard; the rest were in the
tests and the documents.

| # | What&nbsp;they&nbsp;found | The&nbsp;fix | The proof |
|---|---|---|---|
| B1 | **EVRe Guard, a bug of stage 2.** A main loop that calls `evre_guard_logged_in()`, as the header advised, could undo a lockout that the decoder's interrupt had just started: its clock value was older than the interrupt's, the unsigned difference wrapped, and the lockout ended. The right token then logged in at once. The same call could close a session the interrupt had just opened. The guard before stage 2 was immune. | `evre_guard_logged_in()`, `_logout()` and `_restore()` take `EVRE_LOCK()` around the clock read and the update; the header says they follow `addMsg`'s rule. And a clock value up to an hour older than the last one the guard saw counts as no time. [Round 4, D-22: the lock stays; the hour is gone, since with 64 bits any value not newer counts as no time.] | Two new checks run the "interrupt" inside the clock read: the lockout runs on, the session stays. The lock build checks that the clock is read under the lock, and that the handlers take none. Both reviewers' race programs pass now. |
| B2 | **EVRe Guard, a false promise.** D-18 said any `uint32_t` limit works. It did not: a lockout of `0xFFFFFFFF` ms ended only if a look landed in a 1 ms window, so with a look once a day it never ended; an idle limit of `0xFFFF0000` never idled out. | Time is counted, not compared. At each look the step since the last look comes off the lockout and goes onto the idle time (`lock_left`, `idle_ms`, `seen_ms` replace `locked_until`, `last_ms`, `lock_ms`). Any limit ends, if the looks are less than 49 days apart. [Round 4, D-22: the looks may be any time apart.] | Once-a-day checks: `0xFFFFFFFF` is locked on day 49 and over on day 50; the idle limits `0xFFFF0000` and `0xFFFFFFFF` end on day 50; looks 40 days apart count. The exact-jump check is gone. The reviewer's adversarial program: 60 of 60 (it was 57). |
| B3 | **The suite let changes through.** A mirror whose READ_RESP kept its own MSG_CNT passed the whole suite (a real regression: a host's encoder then refuses to build acks). A mirror that refused the ack of a write to 0xA000 passed the fuzz. | The transcript's D-10 check compares MSG_CNT. A new check stores the whole reserved bank in a mirror, raw. The fuzz prints a loose hash (the memory but for HEARTBEAT and the queue past MSG_CNT): D-10 and D-13 must leave it unchanged. D-9's tag carries the code it expects. D-8, D-2 (room) and D-12 check the length of the ERROR_RESP. A frame decoded in place is also decoded with a buffer of its own, on the same memory: the two outcomes must be the same on every line. | 11 mutants, below. |
| B4,&nbsp;EMB-B1,&nbsp;and&nbsp;the&nbsp;API&nbsp;reviewer | **The documents said a 1.0 device compiles unchanged.** The struct tag `protocol_base` became `evre_base` in round 2 (your decision), and `struct protocol_base` no longer compiles. Also: the index macros are parenthesised since stage 1, so `DATAx(0) + 1` is 8, it was 7; "a frame of any other length is code 12" is wrong for an unknown code; the Roadmap still said ranges wait for v2. | Written down in PROTOCOL.md (Migrating from 1.0, Device API), in EVRe.h's MIGRATING block and in CONTRIBUTING.md's rule. `typedef evre_base_t protocol_base;` added, so `protocol_base *p` compiles again; `struct protocol_base` cannot, C++ has no alias for a tag. The length sentence and the Roadmap fixed. | `api_compat.cpp` checks the typedef and uses it. |
| EMB-B2 | EVRe.h said EVRe.cpp checks that a global device is set up at compile time. It checked only the plain layout. | `static_assert(evre_base_t().RESERVED_REG_READ_MAX == 0xA105, ...)` in EVRe.cpp: plain C++11, no code, no data. | A copy of EVRe.h with one member set by a function: the new assert fails, `is_standard_layout` passes. |

**The mutants.** Each is one change in a copy of the tree. The first six ran through the whole suite with `--fuzz`
against the frozen library before this round; the last five through the feature program alone (the fuzz does not
run EVRe Guard, and the write-handler mutant is the old behaviour, which a comparison with the old library cannot
see):

| Mutant | Checks&nbsp;that&nbsp;fail | Of them in the fuzz |
|---|---|---|
| a start in a gap between ranges answers 4, not 5 (the reviewers') | 20 | 4 of 4 |
| HEARTBEAT set before the READ answer is built (the reviewers') | 8 | 4 of 4 |
| an ERROR_RESP carries the frame's slave id (the reviewers') | 8 | 4 of 4 |
| a mirror's READ_RESP keeps its own MSG_CNT (the reviewers'; passed everything before) | 10 | 4 of 4 (0 before) |
| a mirror refuses the ack of a write to 0xA000 (the reviewers'; passed the fuzz before) | 10 | 4 of 4 (0 before) |
| a WRITE_ACK in place built before the store (the 1.0 order) | 12 | 4 of 4 (0 without the in-place check: the old library does the same) |
| a write handler's 1 silent again (review note N2) | 2 | - (it is the old behaviour) |
| the guard without the late-read rule | 3 | - |
| the guard moves its stamp back on a late read | 1 | - |
| the guard's idle limit compared with `>` (0xFFFFFFFF never ends) | 1 | - |
| a write of 0 bytes in the login register is a login attempt | 2 | - |

**From the reviewers' notes, applied because they were clearly right and cheap:**

- N2: checks that a write handler's 1 is answered on a WRITE_ACK and on a WRITE.
- N3: a write of 0 bytes inside the login register is no login attempt. It was a failed attempt at
  `login_addr + 1` and none at `login_addr`.
- N4: log in with a WRITE_ACK, never by broadcast; a token per device (evre_guard.h, PROTOCOL.md).
- N7, N8: EVRe.h's MIGRATING block names the tag, the macros, D-14 and D-4; the 11-byte fallback says what the host
  hears; `protocolInit` may write `D_RANGE_CNT`.
- N10: a note on the union behind `SLAVE_ID_REG`.
- EMB-N1: the guard's main-loop rule (B1).
- EMB-N2: the lock covers `addMsg` from a context the decoder preempts, not from an interrupt that preempts the
  decoder: that one can land inside a write that clears and then acknowledges, and have its message acknowledged at
  once. Written down (EVRe.h, PROTOCOL.md, Message API). [Round 4, D-19: a write that clears acknowledges nothing
  now, so a lock that masks every interrupt covers that case too; the text says so.]
- The Messages section said the acks after a clear "find empty slots". If `MSG_ACK_HANDLER[0]` queues a message
  there, the next ack acknowledges it (a reviewer's program showed it). Rewritten; to clear, write `MSG_CNT` alone.
  [Round 4, D-19: rewritten again: the slot bytes of such a write do nothing.]

**Two things moved by this round, on purpose:** EVRe Guard's state members (the guard was never released), and one
boundary by a millisecond: a session now ends when it has been idle for exactly `idle_logout_ms` (it was one ms
later). Without that a saturated idle time could never end a limit of `0xFFFFFFFF`.

**For you to decide** [all decided in round 4]:

- N5: in one write that clears and acknowledges, `MSG_ACK_HANDLER[0]` runs once more for each empty slot. Skip the
  handler for a slot that holds 0? [Q1, decided: D-19, the clear wins.]
- N6: `protocolInit` checks the range table only when `protocolConfigure` returns `NO_ERROR`. A device whose
  configure fails, and that ignores the code, serves an unchecked table. Check it always? Not changed: no test here
  can override the weak `protocolConfigure`. [Q2, decided: D-20, checked always; `configure_test.cpp` overrides it
  on Linux.]
- N9: D-12 is applied to a mirror too: an ERROR_RESP decoded by a mirror is 2, not 12, silent both ways. The host
  program reads ERROR_RESP itself, so nothing breaks. Please confirm. [Q3, decided: kept.]
- N1, the rest: after a classified change of memory, the rest of that fuzz case is counted as "then", not checked.
  [Still so.]

## 9. What the firmware and the host program must do, when you move them to 1.1 (the follow-ups)

Nothing changes there until you decide. When you do:

- **The host program:** set `ACCEPT_READ_RESP = 1` on its mirror of the board (the `#if` line in "MIGRATING FROM
  1.0" builds against 1.0 and 1.1). Without it every READ_RESP is refused and it hears nothing. Its TCP server
  answers its clients' READs from a per-connection copy of the board's registers: mark that copy a mirror too
  (`ACCEPT_READ_RESP = 1`, D-25), or its clients hear bit 14 as the copy's own setting. A mirror also takes the
  frames its clients send: a READ_RESP within the copy's read limits is stored, its write handler asked (outside
  them it gets 4 or 5 and nothing is stored); a WRITE_ACK_RESP stores nothing (0 for the reserved or the device
  bank, 3 for another). None of them is answered. Check that when it moves to 1.1. Rebuild everything.
- **The firmware:** rebuild everything. It queues messages from its main loop while it decodes in an interrupt: give
  the build `EVRE_LOCK()` / `EVRE_UNLOCK()` (the CMSIS lines are in `EVRe.h`), and mask the decoder's interrupt
  around its own writes to CONFIG. If it takes EVRe Guard, the same lock covers the guard's main-loop calls.
  [Round 4: and its own writes to STATUS. With EVRe Guard it supplies the 64-bit clock, a counter its 1 ms
  SysTick adds to, read without tearing.]
- The audit found no `A000[...]` in either, and no `struct protocol_base`.
- [Round 4] A search found no broadcast in either, so D-21 changes nothing for them. A device that takes broadcast
  set points or stops into its bank sets `ACCEPT_BROADCAST_D000 = 1`.

## 10. What stays open

| Item | Note |
|---|---|
| Publishing | nothing is in `teknile/EVRe` yet; its `CHANGELOG.md` and `README.md` need a 1.1 entry then |
| CI | `.github/workflows/ci.yml` does not run `tests/run_lib_tests.py` yet |
| The&nbsp;tools | EVRe Studio, `evre`, `evre-sim`, the Python package and the fake devices speak only over the wire and were not touched. The simulated devices were not checked against the frames that changed (F1, F2, D-9, D-11, D-12, D-13; round 4: D-19, D-21, D-24); a tool that logs in could log in again when it sees 13 |
| `encodePacket` | allocates 10 + count before it checks the request (the encoder half of audit RF-13) |
| Big-endian&nbsp;CPUs | the reserved bank goes out in the CPU's order: documented, not changed |
| 16-bit&nbsp;`int`&nbsp;targets | the size sums are 32-bit (`expectedSize`'s since review round 2; stage 1 did it in `unsigned int`, 16 bits there, and wrapped), and `getLE16` shifts its high byte as `unsigned` (review round 2). Built on one since review round 2: avr-g++ 5.4 (AVR), 108 builds of the library and the Guard, 0 warnings with `-Wall -Wextra -Wpedantic`. [Round 3 said the sums were 32-bit since stage 1: `expectedSize`'s was not.] |
| `D_RANGES`&nbsp;with&nbsp;`D_RANGE_CNT`&nbsp;0 | accepted by `protocolInit`, and serves nothing |
| `DEVICE_REG_WRITE_MIN`&nbsp;0&nbsp;by&nbsp;default | the whole bank writable, as in 1.0: documented, kept |
| EVRe&nbsp;Guard | `free_attempts` 0 makes the first lockout 2 x `lockout_ms`, as its words say; a refused attempt during a lockout is faster than a compared one, so the timing shows the lock; anyone who sees the login frame has the token (on a network: TLS at the gateway) |
| The&nbsp;review's&nbsp;notes&nbsp;N5,&nbsp;N6,&nbsp;N9 | [decided in round 4: Q1, Q2, Q3] |
| EVRe&nbsp;Guard's&nbsp;clock | [decided in round 4, D-22: 64 bits, supplied by the device; the looks may be any time apart] |
| From&nbsp;round&nbsp;1 | the descriptors (limits, persistence, names) as EVRe Guard's second part, generated from the map; the device table export writing ranges and the Guard setup |
| Roadmap&nbsp;v2 | stable message indices |
| The&nbsp;Credits&nbsp;line&nbsp;of&nbsp;PROTOCOL.md | names the protocol's designer, as 1.0 does [decided in round 4, Q6: the author's full name] |
| Round&nbsp;4 | its own open points: the section "Round 4", "What stays open" |

## 11. Earlier rounds

**Round 1** added to the library, with no meaning in it:

- **B1, ranges:** the device bank as a few `evre_range_t` instead of a pointer per byte; the answers the same as the
  pointer table's. For scale: the firmware's bank would be 2 ranges, 24 bytes of flash, in place of 1020 bytes of
  RAM of pointers.
- **B2, the handlers:** `READ_HANDLER` and `WRITE_HANDLER`, one pair per device, asked for both banks.
- **EVRe Guard**, the login (section 5).
- **F1:** a READ_RESP sent to a device wrote into its registers, DEVICE_ID too; a device refuses it now, and a
  host's mirror sets `ACCEPT_READ_RESP`. **F2:** a write to an unknown bank was acknowledged and dropped; refused
  with code 3 now. **F3:** ASCII diagrams. **F4:** LF files.

**Round 2, your answers:** the reserved bank served in place, without a table (1048 bytes of RAM and the one
allocation of `protocolInit()` gone; `A000` always `nullptr`); `protocol_base` renamed `evre_base`, `base_t`
renamed `evre_base_t`, with `base_t` kept as an alias.

The decisions of round 1, all confirmed by you:

| # | Decision | Chosen |
|---|---|---|
| D1 | a request across a gap between ranges | refused (4 or 5) |
| D2 | the&nbsp;reserved&nbsp;bank | served in place, no table |
| D3 | handlers | one pair per device |
| D4 | the&nbsp;refusal&nbsp;code | the handler's own |
| D5 | `EVRE_HANDLED` | 0xFF |
| D6 | handlers&nbsp;asked&nbsp;for&nbsp;both&nbsp;banks | yes |
| D7 | READ_RESP | refused by a device; stored by a mirror, the write handler asked |
| D8 | versions | library 1.1 (`EVRE_LIB_VERSION` 0x0101), protocol revision 1, no STATUS bit |
| D9 | `RANGE_TABLE_INVALID` | `protocolInit()` only, never sent (14 since D-17) |
| D10 | names | EVRe Guard, `lib/guard/evre_guard.*`; `evre_base_t` with `base_t` as an alias |
| D11 | Guard&nbsp;defaults | the caller sets all of them |

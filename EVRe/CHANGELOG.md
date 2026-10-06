# Changelog

## Not released yet: library 1.1 and EVRe Guard

On the branch `evre-1.1`; `docs/PROTOCOL.md` describes it, its "Migrating from 1.0" says what a device or a host
must know, and `REVIEW.md` holds the review and its decisions.

### Protocol and device library

- Library 1.1 (`EVRE_LIB_VERSION` `0x0101`): every 1.0 name with its value but the struct tag (`struct evre_base`
  now; `base_t` and `protocol_base` still name it), and every 1.0 member of `evre_base_t` at its 1.0 offset.
  Rebuild everything that includes `EVRe.h`.
- Ranges of readable and writable bytes in place of the pointer table (`D_RANGES`), checked by `protocolInit()`;
  the reserved bank served in place (`A000` is `nullptr`).
- A read handler and a write handler, asked before a request is answered or stored, with the frame's slave.
- A host's mirror sets `ACCEPT_READ_RESP = 1`; a device that takes broadcasts into its bank sets
  `ACCEPT_BROADCAST_D000 = 1`, and says so in `STATUS` bit 14.
- Frames answered differently on purpose: a `READ_RESP` or `WRITE_ACK_RESP` sent to a device (code 2, silent), a
  write to an unknown bank (code 3), a broadcast into the device bank of a device that takes none (code 3, silent),
  a wrong start or end byte (code 1), an unknown function code (2), the queue zeroed past `MSG_CNT`, a `WRITE_ACK`
  decoded in place, a request past `0xDFFF` (5).
- A new code, 13 `LOGIN_REQUIRED`, which the library reserves and never returns itself.

### EVRe Guard, part 1: the login

- `lib/guard/evre_guard.*`, beside the library and optional: a token written to a login register opens a session;
  without one every request is refused with 13; a lockout after wrong tokens, and a logout after an idle time.

### EVRe Guard, part 2: the register checks

- `lib/guard/evre_guard_desc.*`: a const table made from the map says, for each register a host may write, its
  place, size, type, raw limits and the values that always pass. The check walks every register a frame touches,
  after the login and before the library stores a byte: a value outside the limits, NaN or an infinity is refused
  with the new code 15 `VALUE_REFUSED`; a byte no entry covers, or part of a number, with 3. Nothing is clamped,
  nothing stored. A bad table refuses every write to the device bank and lets the reserved bank through.
- `lib/EVRe.h` reserves 15 `VALUE_REFUSED`; the library never returns it.

### The map format

- A new optional register key, `past_limits`: `"refuse"` (the default) or `"clamp"`, what the device does with a
  value past `min` or `max` (MAP_FORMAT.md 6.1, the schema, the Studio's model, editor, CSV and exports).

### The tools

- `evre export MAP --to guard` and the Map editor's **Export > EVRe Guard table**: EVRe Guard's table (`.h` with the
  typed raw limits, `.cpp` with the value list and the entries). `--check` compares with the files there and exits
  1 when one is older than the map; it works for every export with `-o`.
- The map check names what the Guard's table takes in place of the map's numbers (a limit past the type or between
  raw steps), its export errors, `clamp` without limits, and a gap between two registers a host writes.
- EVRe Studio, `evre`, the API and the Python package send a value past the limits of a register that clamps without
  asking, never send NaN or an infinity, refuse a broadcast of part of a number, and name code 15 *value refused*.
- `evre-sim --strict` answers as a device with the Guard: 15 for a value, 3 for part of a number, and clamps a
  register that clamps.
- The C header and the device table write an `f32` limit past the largest float as the largest (it was `inf`).
- EVRe Studio, `evre` and the Python package name code 13 *login required*. A register refused with 13 is asked again
  at the next poll (only 3, 4 and 5 mark it *not available*), and the Studio does not log in again by itself: the Log
  says so once for the device.
- `evre_fake_fast --login-required` plays a device whose login is required.
- CI runs the library's suite (`tests/run_lib_tests.py`) on Linux and Windows.

## 1.0.0 (not released yet)

The first public version of EVRe and the tools around it, under the Apache License 2.0.

### Protocol and device library

- EVRe protocol revision 1 (`docs/PROTOCOL.md`): frames, function codes, CRC-16/X-25, the reserved bank at
  `0xA000`, messages, errors, test vectors, the device API and a conformance checklist.
- The device library `lib/EVRe.h`, `lib/EVRe.cpp`; `lib/ports/stm32h7/malloc_lock.c` for newlib on Cortex-M7.

### The map format `evre-map/1`

- The contract for every tool (`studio/docs/MAP_FORMAT.md`) and its JSON Schema (`studio/docs/evre-map-1.schema.json`).
- Registers with types, scale and offset, value names, special values, bit fields (with their own access), limits,
  defaults, write behaviour (action, write-1-to-clear), write-only access, persistence, notes; the device's ID, USB
  IDs, login register, protocol and group notes; overlays (`extends`).
- The device's slave address (`slave`, 1 to 255; 0 is the broadcast address), and `plot`: `false` for a value that
  does not change with time (an ID, a version, a setting), which a host does not offer as a line on a chart.

### EVRe Studio

- Live registers over TCP or serial, decoded fields, quick write with a datasheet-style bit view, safe writes
  (Allow writes, danger confirmation, limits, changed-while-editing), an oscilloscope-style chart with cursors,
  measurements and math lines, CSV recording, a frame monitor, an event log, and an API for Python, MATLAB and
  LabVIEW (JSON lines and EVRe pass-through).
- The Map editor: a map made from nothing or changed without touching JSON (bulk edits, copy and paste, undo and
  redo, value names and bit fields on a bit strip, live checks, a live preview); a save changes a file only where
  it was edited.
- Several devices on one link (RS-485, a gateway): a bus file (`evre-bus/1`) puts each at its slave address; their
  registers are named after them in the table, chart, CSV and API; a device that stops answering goes offline
  without slowing the others. Broadcast writes, where the map allows them, read back from each device.
- Auto send: a device that can sends its read-only block by itself at a set rate (40 to 4000 a second); each frame is
  one chart point and one CSV row; switched off on the device at Disconnect.
- The chart for many fast lines: min/max summaries of 8 to 4096 samples, binning and drawing on several threads, a
  RAM budget the samples keep to (with a note of what the Memory set needs), at most 64 lines at 1000 samples a
  second (fewer at faster rates), the plot drawn by a dedicated graphics card when there is one, or a card picked by
  name (Windows: Direct3D 11, shown as a layer of the window; 64 lines of 1000 Hz at 60 frames a second on a 4K
  screen), the values of every line beside the mouse, and a Display menu: Normalise, Smooth, Hover values, and who
  draws (a card by name, or the CPU).
- Formula completion in the math line dialog: register names and functions as you type.
- Exports: a Markdown specification, a C header, a Python module, CSV; CSV import.
- The device table: the map as the device side for `lib/EVRe.h` (packed read-only and read-write images, their
  offsets checked at compile time, the defaults as start values, a bind function, a limits check); a map the
  library cannot serve (a read-only register above a writable one, a register outside `0xD000..0xDFFF`) is refused
  with the registers in the way.
- Installers: a Windows installer (per user, no administrator) and a portable zip, a Linux AppImage, and the
  command-line tools for Linux, made by the release workflow; the teknile mark as the program's icon.

### Tools

- `evre`: validate, export, info, read, dump, watch, write, broadcast and check a device against its map, from a
  terminal or CI; a bus of devices by its bus file.
- `evre-sim`: a map served as a device over TCP, behaving as the map says.
- `evre` for Python (`studio/python`): a device by register name with its map, standard library only. An answer is
  matched by its slave, offset and count, so a frame the device sends by itself is never taken for one.

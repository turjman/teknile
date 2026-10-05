# Changelog

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

### Tools

- `evre`: validate, export, info, read, dump, watch, write, broadcast and check a device against its map, from a
  terminal or CI; a bus of devices by its bus file.
- `evre-sim`: a map served as a device over TCP, behaving as the map says.
- `evre` for Python (`studio/python`): a device by register name with its map, standard library only.

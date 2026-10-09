# Changelog

## 1.0.0 (2026-10-09)

The first public version of EVRe and the tools around it, under the Apache License 2.0.

### Protocol and device library

- EVRe protocol revision 1 (`docs/PROTOCOL.md`): frames, function codes, CRC-16/X-25, the reserved bank at
  `0xA000`, messages, errors, test vectors, the device API and a conformance checklist.
- The device library `lib/EVRe.h`, `lib/EVRe.cpp`; `lib/ports/stm32h7/malloc_lock.c` for newlib on Cortex-M7.
- Fast EVRe (`docs/PROTOCOL.md`, "Fast EVRe"), a layer above the protocol, not part of it: a device sends samples
  taken on its own clock in numbered blocks, as READ_RESP frames of a window of the device bank that nobody asked
  for; every lost record is counted. `lib/fast/evre_fast.*` builds a block's frame around the records where they
  lie (no copy, no heap), with the library's public names only.

### The map format `evre-map/1`

- The contract for every tool (`studio/docs/MAP_FORMAT.md`) and its JSON Schema (`studio/docs/evre-map-1.schema.json`).
- Registers with types, scale and offset, value names, special values, bit fields (with their own access), limits,
  defaults, write behaviour (action, write-1-to-clear), write-only access, persistence, notes; the device's ID, USB
  IDs, login register, protocol and group notes; overlays (`extends`).
- `streams` (optional): a device's fast streams, each its window, rate, enable and rate registers and the channels
  of one record; the checks refuse a window outside the device bank, over a register or another window, or too
  small for one record.
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
- Fast streams (Fast EVRe): the sidebar's card starts and stops a map's streams (their enable registers), counts the
  blocks, shows the samples a second as fitted to the Studio's clock with the correction in ppm, and the samples
  lost; CONFIG read every 100 ms meanwhile; off on the device at Disconnect, on again after a lost link.
- Fast lines: a stream's channels on the chart (a Plot tick in the card), every record at its own time, from an hour
  down to 10 us; a view costs its pixel columns, not its records (summaries of 256 and 4096 records), so a spike of
  one record in millions shows at every zoom; the line broken where records were lost, the gap's tooltip counting
  them; the records kept as they came, a few bytes each, within the chart's RAM; time labels below a millisecond.
- Fast lines measured as any line: the Measure table from the summaries (an hour at a million records a second within
  a frame), nothing across a gap; totals since Clear; the histogram; the spectrum of the records as they are (even
  steps, over the longest part without a gap); the trigger (never across a gap); Export to CSV, a row per record.
- Fast streams recorded: while a CSV records, each stream's blocks as they came beside it (`run.ADC.evrs`); the
  recording window opens a CSV with its streams' recordings, or a `.evrs` alone, the file mapped (larger than the RAM
  too), a file cut off up to its last whole piece. `evre.read_recording` in the Python package.
- Fast streams in the Map editor (Map settings, a Streams page: the window, rate, enable and rate registers and the
  channels, the map's checks live), in the exports (Markdown, C header, Python module), in `evre check --writes` (a
  stream starts with START, its numbers follow, its rate within 2 %, it stops) and live in Python (`dev.stream`).
- The Fast streams card: a stream's name as the row's title, "Start stream" / "Stop stream", the card before Polling
  and recording; the Map settings' channel table with columns that fit their words.
- Fast lines at 60 frames a second: a view moved by a few columns keeps the columns it had and bins only the new
  ones, the memory strip binned at most once a second, and the timing aid (`EVRE_PERF_LOG`) counting the fast
  columns binned and the grid's time.
- The window's thread held (a dialog closed with its title bar's X, a title-bar button pressed): the Chart tab's
  dialogs open and close without the desktop's animations, and the fast blocks queued meanwhile are taken over
  the next frames (at most about 8 ms a frame, the chart painting first), so no frame pays for the whole pile.
- The chart for many fast lines: min/max summaries of 8 to 4096 samples, binning and drawing on several threads, a
  RAM budget the samples keep to (with a note of what the Memory set needs), at most 64 lines at 1000 samples a
  second (fewer at faster rates), the plot drawn by a dedicated graphics card when there is one, or a card picked by
  name (Windows: Direct3D 11, shown as a layer of the window; 64 lines of 1000 Hz at 60 frames a second on a 4K
  screen), the values of every line beside the mouse, and a Display menu: Normalise, Smooth, Hover values, and who
  draws (a card by name, or the CPU).
- The recording's window: it opens with its lines, their values and its Y range at once (with Measure on too), a
  change is painted when no more frames come, made bigger it is painted whole (on a card, a frame the system let go
  is drawn again: no black bar over the new part), its Window and Y range row packed, and the theme switch reaches it.
- A recording with fast streams: the window says when a stream's samples end before the CSV's last row (each file is
  written as it comes), Normalise ranges a line with its values at the view's edges too (one sample in a short view
  no longer jumps off the plot), and the info line says *idle* when a held view paints nothing. Its **Lines** is a
  checklist of every line, grouped (registers, each stream's channels, math), with All, None, a search and the
  count on the button; the lines unticked are kept for the next recording. A stream's **Log** tick chooses whether
  a CSV recording writes it beside it; each stream's row is headed by its name, channels and rate on one line. A
  recent recording whose file is gone is greyed, *(not found)*, and a click takes it off; **Clear the list**.
- The chart's readouts: standard deviation and peak-to-peak beside RMS, a column chooser on the Measure table, each
  line's total since Clear (value x time, in hour units: Ah, Wh), and a Log Y range with decade lines.
- Lanes: one plot per unit, stacked, each with its own Y range; lanes that do not fit scroll, fold to a strip with
  their values, are resized by dragging the border between them, and are listed in a lane menu on the toolbar.
- Pictures and files: copy or save the chart as a picture, export the view or A -> B to CSV, notes at a time on the
  chart (saved beside a recording), and a recording opened in a window of its own (also by dropping the file on the
  window, or from the recent list).
- Histogram and spectrum of a line over A -> B or the view, each in its own window with a cursor readout.
- The trigger: each line its own level and edge, set in its lane by dragging the level; Auto, Normal and Single
  with Run and Stop, a hold-off, a trigger position you drag, Force and Arm; the crossing found among every record
  for fast lines. Its state is said in steady words, and below 100 ms the chart locks on the busiest line by itself
  (Auto, short window), marked in its own colour.
- Short windows: below 1 s the time grid is in divisions, labelled from the right edge or from the trigger point,
  with a "1 ms/div" readout; the wheel zooms in 1-2-5 steps; the cursors stay where they were put on the grid and
  read the wave moving under them (A at -3.20 ms, B at T +1.75 ms; Shift snaps to a tenth of a division).
- Long memory: older fast records kept as summaries of 256 records (the Older samples setting), so a stream of a
  million records a second keeps far more time in the same RAM; the newest part keeps every record.
- The RAM budget against the free memory: the chart keeps to less when the PC has less free, and says so, so it
  trims before Windows pages; a budget cut is released in slices, so the chart never stops for it. At most 64
  lines, polled, fast and math together.
- Fast math lines: a formula over one stream's channels is computed for every record, with registers in it held at
  their last polled value; it is drawn, measured, triggered, exported and recorded as a fast line.
- A stopped stream is said on the chart ("ADC stopped · last record 14:03:12.345", its values greyed), the live
  view goes on with the clock, and the stream's header gives its measured rate (the rate set while it is off).
- The window in Arabic (right-to-left) or English, the Help included; numbers, units and register names stay left
  to right, in tables too.
- Formula completion in the math line dialog: register names and functions as you type.
- Fast streams in the API, reading only (no write switch): `list` names the map's streams and their channels
  (`ADC.I_LOAD`: unit, rate, on or off); `get` of a channel its newest record and that record's time; `stream` of
  channels a line of each period's min, max and mean (`period_ms`, 10 ms at least) beside the registers' samples; on
  port 1219 a client that writes a stream's enable register gets its blocks as the device sent them, and while the
  Studio streams it nothing reaches the device. `--fast NAME` starts streams once connected.
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
- `evre-sim`: a map served as a device over TCP, behaving as the map says; its fast streams too.
- `evre record`: a fast stream's blocks into a `.evrs` file, as they came, with time marks.
- `evre` for Python (`studio/python`): a device by register name with its map, standard library only. An answer is
  matched by its slave, offset and count, so a frame the device sends by itself is never taken for one.
  `evre.connect_studio()`: EVRe Studio's JSON API, a fast stream's channels as each period's min, max and mean.

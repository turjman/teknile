# Changelog

## 1.5.1 (not released yet)

### EVRe Studio

- **The phone gets an interface of its own** (Android, the skeleton): a window in Qt Quick, drawn by the phone's
  graphics card, with a header (the menu, the screen's name, the link's state), the Connect drawer (the dark theme and
  the language at its bottom), a tab bar for Registers, Chart, Monitor and Log, and right to left in Arabic. The
  screens say what will come; the desktop's widget window is no longer built for Android, and the desktop does not
  change. A proof screen (`am start ... --ez p9 true`) measures a live list's frames.
- **Fine detail** in the Display menu (on by default, saved): on a scaled screen a line is binned per device pixel,
  not per logical pixel. At 225 % a noisy line over a 1 min view was drawn as steps 2.25 pixels wide and looked
  smoothed out on the screen, and *Copy picture* showed it as separate bars with faint joins; now both show every
  swing as one line. It costs more drawing time: turn it off when a busy chart runs slow. Nothing changes at 100 %.
- **No dashes in a copied picture.** A line with bars in about half its pixel columns (a slow sine at two or three
  samples a column) was drawn by the CPU as wide bars joined by hairlines: dashes in *Copy picture* and *Save
  picture*, where the screen showed one line. The thin joins are kept for lines of bars in nearly every column.
- **The Window and Memory boxes no longer put an old length back.** A box only clicked in, or left when a menu took
  the focus, applied its text again: a window changed since (a span shown, a recording zoomed) jumped back to the
  box's length, clipped to the memory. Now a box applies a length only when one is typed or picked.
- **A refused token stops the reconnecting.** A device that refused the token and closed the link was tried again
  twice a second for as long as *Reconnect by itself* was ticked, the pill showing only that it was connecting.
  Now the link stays closed and the pill says *token refused: type the right token, then Connect* (the reason in
  the Log). With no token, a device that refuses the device ID read while its map declares a login register is said
  to want a token (the Log, and the pill when it closes the link), and is not tried again by itself either.
- **Touch scrolling.** On a touch screen (a Windows one, and the phone) a finger dragged over any list, table or
  panel scrolls it, with momentum, wherever it comes down, and presses or selects nothing; a tap is a click. A drag
  that starts on a selected row selects the rows it passes; a fast flick glides on, and at an end of a list its frame
  lights. On the phone the lists no longer tear or keep old strips while they scroll (Qt's own touch scroller did,
  QTBUG-135011). Before, only the scroll bars moved them, and a finger's
  drag over a list selected its rows. The mouse is unchanged.

## 1.5.0 (2026-11-07)

Room to focus: the sidebar and the quick-write panel fold away.

### EVRe Studio

- **The sidebar folds.** The chevron at the end of the *EVRe Studio* title row, or Ctrl+B, folds the sidebar to a thin
  bar: the connection dot (its tooltip gives the state and the poll rate), one icon per card, Help and Language. An
  icon opens the sidebar at its card. The sidebar slides (not when Windows' animation effects are off), the fold is
  remembered, and folding never disconnects or stops the polls, a recording or the API server. Mirrored in Arabic.
- **The quick-write panel folds.** A chevron on its title folds the Registers tab's quick-write panel to that one line,
  remembered; the table gets the height back and keeps the selected row in view.
- **JSON errors in our own words.** A bus file or a recording's notes file that is not valid JSON names the place and
  the reason as the map's errors do (*line 3, column 19: unexpected character ']'*), in English and Arabic; the API
  server says *not a JSON object* for a request that is valid JSON but not an object.
- **Arabic.** Latin pieces stay whole in the chart's memory list, the lane list beside Y range, the recent
  recordings, the bulk write's notes and the Dump's fast and bus lines.

### Releases

- `evre.exe`, `evre-sim.exe` and the Windows installer carry a version block (product, version, description), as
  `EVReStudio.exe` already did: code signing needs it.

## 1.4.1 (2026-11-05)

Fixes found while the website's tutorials were written: the Studio now does what its guide says, and the guide says
what the Studio does.

### Changed: the Python export's DEVICE_ID

- **A map exported as a Python module** (Map editor, Export, or `evre export --to py`): `DEVICE_ID` is now the map's
  device ID, and the protocol's register of that name is `REG_DEVICE_ID = 0xa000` (a register named as one of the
  module's own names gets `REG_` before it, with a comment). Before, the register's address 0xA000 overwrote the device
  ID. **A script that used `DEVICE_ID` from an old export as the register's address must use `REG_DEVICE_ID`.**
- `evre export` refuses a map that `evre validate` refuses (a min above its max, a name used twice): it lists the
  errors, writes no file and ends with exit code 1. Warnings do not stop it.

### EVRe Studio

- **Registers tab.** A changed value glows again; a selected row keeps its colours, a stale value's grey too; the
  Decoded column is as wide as its text and a width dragged wider stays; F2 on any cell of a row edits its value; not
  connected, no editor opens; a value name picked from the list is written and logged as its number (`= 2`); the list
  follows the device after a pick; a bit in no field says so; a bulk write's refusal is named in the status bar.
  *1 register*, *1 device* and their plurals; the saved path no longer breaks after *C:*; tooltips for Record CSV,
  Clear cursors, Mean, RMS and Latency.
- **Glow.** Changed values glow only after a rest, with an arrow for the direction; Registers → Glow turns it off.
- **The selected row stays in view.** Selecting a read-write register near the bottom of a long map no longer hides
  it under the quick-write panel; every list and table keeps its current row in view when it gets shorter.
- **Map editor.** New and Open ask first when the map has changes not saved; Ctrl+Y and Ctrl+Shift+Z redo on Windows;
  a key typed into a group cell starts the group anew; the value names' Hex box and the Bit fields page follow at once;
  saving after changing one fast stream writes the others as they were; the Markdown export's table headers are in the
  export's language; a map that extends itself names its base once.
- **Chart.** The Y boxes show what was made of the typing (a min above the max swapped, 12 and 12 as 12 and 13, a
  refused Log min); the info line keeps its count at the narrowest window; a drag with nothing to pan to says why; the
  Drawing list names each graphics card once; a change to one math line keeps the others' samples; the measurements'
  time over a held view follows the clock; the totals since Clear bridge two poll intervals when the polls are slower
  than one a second; the trigger's level box holds six digits; the crosshair box shows whole numbers without decimals;
  *1 bin*, *1 sample*, *takes 1 value*. The math dialog's Tab takes a suggested name, as Enter does, and its help lists
  `bits(x, lsb, width)`.
- **Fast streams.** After Start the card says *waiting for the first block* until one comes; the rate's tooltip says
  ppm once; the `.evrs` line in the Log gives its path in the system's separators.
- **Dump, Log, recording window.** A byte's tooltip follows the byte under it; a fast channel's tooltip breaks at its
  parts, never mid-phrase; Show info off hides the info lines already in the Log, on brings them back; the Lines list
  stays open with 13 lines or more; a math line naming a column the recording lacks says so; a CSV of titles only reads
  *no samples*.
- **Start.** `--map` no longer empties the map chosen last in the settings; a map that does not load says so over the
  window, once it is shown. `EVReStudio --help` and `--version` go to the console they were started from, or to a
  window in a fixed-pitch font when there is none; a bad option ends with exit code 1.
- **Arabic.** Every new text translated; *offline* (a device that does not answer) has its own word, apart from *not
  connected*; one name for Copy decoded; *10 kS/s مضبوط*; the Log opens at its lines' start, left to right; the pop-up's
  count is whole; the write line in the Log, raw bytes, a lane's manual range and the recent recordings keep their
  numbers and paths in order.
- **Downloads.** The map format's JSON Schema ships beside the README in the zip, the installer and the Linux tools'
  archive, and its `$id` is where it can be fetched.
- **The guide** (STUDIO.md and the Help) corrected where the tutorials found it wrong: Slave applies at once, an offline
  device's values, *(no group)*, a held view's frame rate, the legend's number format, the Monitor's notes for auto send
  and fast blocks, the Dump's menu, `evre-sim`'s STATUS, `evre record`'s ppm and more.

## 1.4.0 (2026-11-02)

### EVRe Studio

- **Listen: hear what another program says to a USB device.** A third choice in the Connection card, beside TCP and
  Serial / USB, for a port another program holds: the Studio reads the USB traffic under it with the system's own
  capture and **sends nothing**. USB CDC boards and USB-UART bridges, on Windows and on Linux. Windows: USBPcap, a
  capture driver installed once, which the Studio starts for that one device; it lets only an administrator capture, so
  Windows asks for your permission each time **Listen** is pressed, and Stop, closing the Studio or a crash end the
  capture with nothing left behind. Run as administrator nothing asks. Linux: usbmon, in the kernel; **Copy the commands**
  gives what makes it readable, and the Help gives what lasts.
- The port box is the one of Serial / USB, and a line under it says what can be heard (*listens to USB CDC · bus 2,
  device 5*) or why not, with what to do about it: **Get USBPcap…**, *USBPcap is not on this device yet* and unplugging it,
  **Copy the commands**. **Connect** reads **Listen**, then **Stop**; the pill reads
  *Listening · COM13 · USB CDC* on a blue tint of its own, and its tooltip says what is heard. A port that cannot be
  heard is refused with the reason, in red.
- The Monitor marks who said what: `->DEV` the other program's request, `DEV->` the device's answer, `??` bytes that are
  no EVRe frame, *(no request heard)* on an answer nobody was heard asking for.
- The table, the chart, the CSV and the Dump fill from the answers to the other program's reads, one chart point and
  one CSV row per round of its reads, for the registers that round read; heard AUTO_SEND frames, while they come, and
  fast blocks are taken as ever. A register it never reads
  stays `—`, with the tooltip *not read by the other program yet*. The status bar and the Polling card read *heard 120
  req/s*, the Auto send row *heard 100/s*.
- Everything that would send is disabled while listening, each control saying why: Poll, Allow writes, Quick write, the
  Monitor's request row, Auto send, a stream's Start, Read now, the Dump's Write…, a bus's presets and the API card. A running API is
  stopped and said in the Log. The engine and the link refuse whatever got past them.
- The signs of a capture that lost something (requests without an answer, a stream's numbers that skip) go to the pill's
  tooltip and to the Log, at most once a minute.
- A disabled primary button (Send, Write) now looks disabled in both themes: it kept its blue. Every line of the Monitor
  runs left to right in Arabic, the time and the mark first. A long reason in the pill wraps whole in a short sidebar too.

### Python package 1.0.1

- `evre.connect_serial` answers through an ST-LINK's virtual COM port: it set the port's timeout for each read, which on
  Windows reconfigures the port, and an answer arriving meanwhile was lost (1 read in 20 answered there).

## 1.3.0 (2026-10-29)

### EVRe Studio

- **One value to several registers.** Select several rw registers of one type on the Registers tab (Ctrl-click,
  Shift-click), then right-click one of them, **Write to 3 selected registers...**, or press **F2**, type and
  **Enter**, or use the quick-write panel under the table. A dialog lists every register with its value now and
  what it gets (encoded for each: its scale, its value names): a register that does not take the value is greyed with
  the reason, one past its map limits is noted and the button reads *Write 3 anyway*, danger registers are marked
  with one sentence for all. **Cancel** is the default. Each register is written and read back on its own, a refusal
  does not stop the others, and the Log keeps the values replaced in one summary line.
- The quick-write panel on several registers: its title names how many, the box starts with the value they all hold
  (*(several values)* while they differ), and a list under it shows each one's value as the polls bring it.
- On a bus with All devices shown, registers of several devices can be one bulk write: each to its own device (an
  acknowledged write each, no broadcast), with a Device column in the dialog.
- The Registers tab's right-click menu acts on every selected row, with the count: *Plot 3 registers*, *Read 3
  registers now* (each from its own device), *Copy 3 values*, *Remove 3 registers...* (it asks first; one undo
  step). A right-click on a row outside the selection selects that row alone first. Read now now asks the
  register's own device (with All devices shown it asked the selected one).
- A click on empty room, or Esc, clears a selection in every view: the Registers table (the detail line, the panel
  and the Dump follow), the Dump (a register lit by *Show in dump* stays no longer), the Map editor's table and
  checks, the bit fields, the value names, the chart's notes, the measurement table, the Monitor and the Log, the map
  settings' lists. In a dialog Esc still closes it.
- On the bit fields' strip the first click on a field after a register was shown picked nothing; it picks now.
- A disabled menu entry is muted and never highlighted under the mouse (every disabled entry in the Studio looked
  enabled, in both themes).
- The sidebar's link pill keeps its whole height when the device line under it takes three lines (*last seen: ...*
  and *the map is for 0x1002*); the Connection card grows for the line instead.
- In Arabic the Registers tab's detail line is right to left as a whole: its value and the several-selected hint
  read in order.

## 1.2.1 (2026-10-26)

### EVRe Studio

- On USB CDC the line under the Baud box now reads *USB CDC: the baud rate is not used* (it said *ignored on USB
  CDC*, which did not say what was ignored). It sits right under the box, as part of the Baud row, and in a narrow
  sidebar goes on two lines, never cut. The Baud box's tooltip says *Not used on USB CDC (see Link)*.
- The chart's CPU path (no graphics card, or on Linux) paints a noisy fast line's bars more than twice as fast: bars
  that share rows are drawn as one, the stroke the bars hide is left out, and what is left is drawn on the chart's
  threads. The picture is the same. At 225 % the longest frame with a full-height fast line went from 13 to 15 ms to
  about 5.5 ms.
- The GUI test's RAM checks (*no paint over 20 ms*) no longer fail now and then on a quiet Windows PC.

## 1.2.0 (2026-10-26)

### The map format `evre-map/1`

- `protocol.auto_send` (optional): the Auto send rates the device makes, `{ "max_hz": 1000, "tick_hz": 1000 }`.
  `max_hz` is the highest frame rate it delivers, `tick_hz` its timing resolution (a frame period is a whole number
  of ticks); both 40 to 8000. A device on a 1 ms tick declares 1000 / 1000; a device on an 8 kHz timer 8000 / 8000 or
  nothing. Without it every rate is offered, as before. The checker refuses a value out of range, a `max_hz` over
  `tick_hz` and limits that leave no rate, and warns when `max_hz` is not a rate the Studio offers.

### EVRe Studio

- Auto send offers only the rates the device makes, from its map: a device on a 1 ms tick gets 1000, 500, 250, 200,
  100, 50 and 40 Hz. A saved rate the map no longer allows falls to the next one below, never to a faster one, and the
  note under the rate says why. With a UART's baud limit too, the lower of the two wins and the note names it.
- What is asked against what comes: when the frames received stay more than 10 % under the rate asked for 2 s, the
  note says *only 700/s of 1000 Hz: the device, the link or the PC's load* in the warn colour, with the likely causes (the device's limit,
  the link, the PC's load); it waits 3 s after a start or a rate change. It works for maps that declare nothing.
- The Map editor's Protocol page: **Auto send top rate** and **Auto send tick**, and a live **Rates offered** row
  (*none: a value is out of range (40 to 8000)* when a value is too small). A change while Auto send is on asks the
  device again at a rate the new limits allow.
- Over USB CDC the connected pill reads *COM13 · USB CDC* (the baud rate means nothing there); a UART keeps
  *COM13 @ 115200*. While the link is USB CDC the Baud row is muted, with *ignored on USB CDC* under it; it stays
  editable for a switch to UART.
- In Arabic the Baud box's number, its placeholder and the text cursor sit at the left, and so do the rates in its
  list opened; the selected rate in that list is readable in the light theme. Rates per second (*700/s*) read the
  right way round in Arabic.
- The GUI test's Dump tick checks count the ticks Qt draws together in one paint (two ticks in one frame show the
  newer data) and fail on a tick never drawn, instead of failing now and then on a busy PC.

## 1.1.0 (2026-10-18)

### EVRe Studio

- The **Dump tab**: each block the Studio reads or writes, as the frames carry it. A card per block: the request and
  the reply as strips of named fields (START, SLAVE, FUNCTION, OFFSET, COUNT, DATA, CRC, END) with CRC OK or not, then
  the data as a hex grid where each register is a tinted chip, and the decoded values beside it; the mouse on a byte
  lights its register. Follow and Hold; polls, writes, Auto send and a manual READ each get their card.
- The Dump's grid has column labels `00` to `0F` over its bytes, so a row's address and a column give a byte's
  address without counting; the byte under the mouse lights its row and its column.
- A fast stream's newest block in the Dump tab: the reply nobody asked for, its header named (FIRST, COUNT, FLAGS with
  LOST, SPARE), the first and last records with their values to one raw step, Show all, and Copy as hex, CSV or a C
  array; the records lost since the last block shown are counted, never missed.
- The trigger: a dashed line down from the T flag while the view is held on a crossing, in the watched line's colour
  (a setting); it stays after a pan.
- The trigger's line box shows only lines it holds: when the watched line leaves the chart, the box reads *choose a
  line* and its tooltip says which line it waits for; plotted again, it comes back with its own level and edge.
- The recording's window reads right: no fps on its info line (it is drawn only on a change); the totals' column
  reads *Whole file* and the line over the table *totals over the file (59.9 s)* (no Clear there); the legend shows
  each line's latest sample in the view, so a part zoomed into shows its own values; the focus on the chart at open;
  no Smooth in its Display menu.
- A **File ▾** button (beside Lines in a recording's window, right of Display on the Chart tab): Copy picture, Save
  picture…, Export to CSV…, Open recording… and the recent recordings, until now on the chart's right-click alone.
- The splitter between the chart and the measure table has a handle that shows (a grip, lit under the mouse, the
  resize cursor, a tooltip); its place is kept, the Chart tab's and a recording's window's apart.
- A big RAM cut no longer holds a frame: the memory let go is given back to the system piece by piece, a big array
  in slices across frames, and a closed chart's memory on a thread of its own (closing a chart that held 3.6 GB took
  the window 2.2 s, now about 1 ms).
- The spectrum's rate just under 10000 Hz is written whole, never *1e+04*.
- Serial over USB CDC (a virtual COM port, where the baud rate is not the link's speed): Auto send is no longer slowed
  to fit the baud rate. A **Link** row under Baud says which kind the port is (*Auto: USB CDC* or *Auto: UART*, from
  its USB IDs; USB-to-UART cables and debug probes count as UART) and can be set by hand.
- The serial card reads right: a port's long name is cut with "…" and shown whole in its tooltip and its list; a
  failed or lost port says why in words (*COM10 is gone: unplugged or reset?*, *not found*, *busy or not allowed*),
  whole on two lines when needed, with the system's text in the tooltip and the Log; the device's ID after a lost link
  reads *last seen*; the Baud box takes any rate typed, 300 to 20000000, lists rates up to 12000000, and refuses what
  is not a rate.

### Docs

- Pictures of the Studio, light and dark, in the README, the Studio's README and STUDIO.md.

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

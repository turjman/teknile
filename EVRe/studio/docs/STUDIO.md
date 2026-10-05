# EVRe Studio — Software Documentation

EVRe Studio is a desktop register tool for any device that speaks **EVRe**, over TCP or a serial / USB CDC port. A
JSON device map names the device's registers; the Studio polls them live, shows and decodes them in a table, charts
them like an oscilloscope, measures and records them, writes them with safety checks, and shares the device with
other programs through a local API.

It is for engineers who bring up and test devices, for test and automation scripts (Python, MATLAB, LabVIEW, …), and
for developers of EVRe firmware. It runs on **Windows** and **Linux** (C++17, Qt 6). EVRe is a register protocol of
**teknile**.

This document has three parts:

- **Part I, Using the Studio**: what it does and how to use it, from the first session to troubleshooting.
- **Part II, Reference**: the device map format, the API and the protocol as the Studio uses it.
- **Part III, Internals**: the architecture, threads, data flows, source layout, building and tests, for people who
  maintain or extend the Studio.
- **Part IV, Making maps**: the Map editor, value names and bit fields, exporting (Markdown, C, Python, CSV),
  overlays, the `evre` command-line tool and the `evre-sim` simulator.

The built-in help (F1) is a short form of Part I. Every example uses the registers of `maps/example_device.json`.

## Contents

- **[Part I. Using the Studio](#part-i-using-the-studio)**
  - [1. Overview](#1-overview)
    - [1.1 What EVRe Studio is](#11-what-evre-studio-is)
    - [1.2 Who it is for](#12-who-it-is-for)
    - [1.3 Features](#13-features)
    - [1.4 The design rule: the device is the truth](#14-the-design-rule-the-device-is-the-truth)
  - [2. Getting started](#2-getting-started)
    - [2.1 Build](#21-build)
    - [2.2 Start the fake device](#22-start-the-fake-device)
    - [2.3 A first session, step by step](#23-a-first-session-step-by-step)
    - [2.4 From the command line](#24-from-the-command-line)
  - [3. Connecting](#3-connecting)
    - [3.1 TCP](#31-tcp)
    - [3.2 Serial / USB](#32-serial--usb)
    - [3.3 Slave and timeout](#33-slave-and-timeout)
    - [3.4 In flight](#34-in-flight)
    - [3.5 Connect, Disconnect and Reconnect by itself](#35-connect-disconnect-and-reconnect-by-itself)
    - [3.6 The token and the map's login register](#36-the-token-and-the-maps-login-register)
    - [3.7 The device ID check](#37-the-device-id-check)
    - [3.8 What the state pill shows](#38-what-the-state-pill-shows)
    - [3.9 Several devices on one link (a bus)](#39-several-devices-on-one-link-a-bus)
    - [3.10 Broadcast](#310-broadcast)
  - [4. The Registers tab](#4-the-registers-tab)
    - [4.1 The toolbar](#41-the-toolbar)
    - [4.2 The columns](#42-the-columns)
    - [4.3 Glow and stale grey](#43-glow-and-stale-grey)
    - [4.4 Tooltips and the ⓘ](#44-tooltips-and-the-)
    - [4.5 Errors and *not available*](#45-errors-and-not-available)
    - [4.6 Search](#46-search)
    - [4.7 The groups menu](#47-the-groups-menu)
    - [4.8 Plot and Log ticks, Plot shown / Unplot shown](#48-plot-and-log-ticks-plot-shown--unplot-shown)
    - [4.9 The right-click menu](#49-the-right-click-menu)
    - [4.10 The Decoded column](#410-the-decoded-column)
    - [4.11 The detail line, adding and editing registers](#411-the-detail-line-adding-and-editing-registers)
    - [4.12 Editing a value in the table](#412-editing-a-value-in-the-table)
  - [5. Quick write and the bit view](#5-quick-write-and-the-bit-view)
    - [5.1 The controls](#51-the-controls)
    - [5.2 What a value may be](#52-what-a-value-may-be)
    - [5.3 Read-modify-write](#53-read-modify-write)
    - [5.4 The bit view](#54-the-bit-view)
  - [6. Writes and safety](#6-writes-and-safety)
    - [6.1 Allow writes](#61-allow-writes)
    - [6.2 Changed while editing](#62-changed-while-editing)
    - [6.3 Danger confirmation](#63-danger-confirmation)
    - [6.4 The order of the checks](#64-the-order-of-the-checks)
    - [6.5 What is logged](#65-what-is-logged)
  - [7. The Chart](#7-the-chart)
    - [7.1 The axes row](#71-the-axes-row)
    - [7.2 The actions row](#72-the-actions-row)
    - [7.3 Window and memory, Hold and Live](#73-window-and-memory-hold-and-live)
    - [7.4 The memory strip](#74-the-memory-strip)
    - [7.5 Y range: Auto and Manual](#75-y-range-auto-and-manual)
    - [7.6 Normalise and Smooth](#76-normalise-and-smooth)
    - [7.7 Cursors A and B](#77-cursors-a-and-b)
    - [7.8 Mouse and keyboard](#78-mouse-and-keyboard)
    - [7.9 Legend, labels and the info line](#79-legend-labels-and-the-info-line)
  - [8. Measurements](#8-measurements)
    - [8.1 The range](#81-the-range)
    - [8.2 The values](#82-the-values)
    - [8.3 Units of the area](#83-units-of-the-area)
    - [8.4 Number format](#84-number-format)
  - [9. Math lines](#9-math-lines)
    - [9.1 Making and managing them](#91-making-and-managing-them)
    - [9.2 The grammar](#92-the-grammar)
    - [9.3 Operators and precedence](#93-operators-and-precedence)
    - [9.4 Functions and constants](#94-functions-and-constants)
    - [9.5 Errors](#95-errors)
    - [9.6 How inputs are matched](#96-how-inputs-are-matched)
    - [9.7 The chart key](#97-the-chart-key)
    - [9.8 Examples](#98-examples)
  - [10. The Monitor tab](#10-the-monitor-tab)
    - [10.1 The request row](#101-the-request-row)
    - [10.2 The frame lines](#102-the-frame-lines)
    - [10.3 Limits](#103-limits)
  - [11. Log and pop-ups](#11-log-and-pop-ups)
    - [11.1 What is logged](#111-what-is-logged)
    - [11.2 The Log tab and the file](#112-the-log-tab-and-the-file)
    - [11.3 The anti-spam rules](#113-the-anti-spam-rules)
    - [11.4 The Log tab's counter](#114-the-log-tabs-counter)
  - [12. CSV recording](#12-csv-recording)
    - [12.1 Starting and stopping](#121-starting-and-stopping)
    - [12.2 Which columns](#122-which-columns)
    - [12.3 The format](#123-the-format)
    - [12.4 Timing](#124-timing)
  - [13. Polling performance and tuning](#13-polling-performance-and-tuning)
    - [13.1 What a poll is](#131-what-a-poll-is)
    - [13.2 The interval](#132-the-interval)
    - [13.3 The block merge rules](#133-the-block-merge-rules)
    - [13.4 When the device refuses a block](#134-when-the-device-refuses-a-block)
    - [13.5 Why N reads per poll, and In flight](#135-why-n-reads-per-poll-and-in-flight)
    - [13.6 The "slower than asked" hint](#136-the-slower-than-asked-hint)
    - [13.7 What to expect](#137-what-to-expect)
    - [13.8 Auto send](#138-auto-send)
  - [14. Command line, environment variables, settings](#14-command-line-environment-variables-settings)
    - [14.1 Command-line options](#141-command-line-options)
    - [14.2 Environment variables](#142-environment-variables)
    - [14.3 Settings](#143-settings)
    - [14.4 Help and the theme](#144-help-and-the-theme)
  - [15. Troubleshooting and FAQ](#15-troubleshooting-and-faq)
    - [15.1 The pill turns red: *Connection refused*](#151-the-pill-turns-red-connection-refused)
    - [15.2 It connects, then drops after a moment](#152-it-connects-then-drops-after-a-moment)
    - [15.3 Every value shows *error* or stays grey, and the Log reports timeouts](#153-every-value-shows-error-or-stays-grey-and-the-log-reports-timeouts)
    - [15.4 A register shows *not available*](#154-a-register-shows-not-available)
    - [15.5 The serial port cannot be opened (*Access denied*, *Permission denied*, busy)](#155-the-serial-port-cannot-be-opened-access-denied-permission-denied-busy)
    - [15.6 The device resets when the Studio connects over serial, or it answers only after connecting twice](#156-the-device-resets-when-the-studio-connects-over-serial-or-it-answers-only-after-connecting-twice)
    - [15.7 The poll rate is lower than the interval asks for](#157-the-poll-rate-is-lower-than-the-interval-asks-for)
    - [15.8 The chart is slow or stutters](#158-the-chart-is-slow-or-stutters)
    - [15.9 A write is refused](#159-a-write-is-refused)
    - [15.10 The table shows another value than the one I wrote](#1510-the-table-shows-another-value-than-the-one-i-wrote)
    - [15.11 *Value changed while editing* appears when I write](#1511-value-changed-while-editing-appears-when-i-write)
    - [15.12 The map does not load](#1512-the-map-does-not-load)
    - [15.13 *the map is for device 0x1001, this is 0x2002*](#1513-the-map-is-for-device-0x1001-this-is-0x2002)
    - [15.14 An API client gets *API writes are off* or cannot connect](#1514-an-api-client-gets-api-writes-are-off-or-cannot-connect)
    - [15.15 A math line draws nothing](#1515-a-math-line-draws-nothing)
    - [15.16 The Monitor shows frames marked *(not a pending request)*](#1516-the-monitor-shows-frames-marked-not-a-pending-request)
- **[Part II. Reference](#part-ii-reference)**
  - [16. The device map format "evre-map/1"](#16-the-device-map-format-evre-map1)
    - [16.1 A minimal map](#161-a-minimal-map)
    - [16.2 Top-level keys](#162-top-level-keys)
    - [16.3 Register keys](#163-register-keys)
    - [16.4 Types](#164-types)
    - [16.5 Scale and offset](#165-scale-and-offset)
    - [16.6 Enum](#166-enum)
    - [16.7 Fields and the bits syntax](#167-fields-and-the-bits-syntax)
    - [16.8 Hex format, danger, access, groups](#168-hex-format-danger-access-groups)
    - [16.9 Load and save rules](#169-load-and-save-rules)
    - [16.10 Validation errors](#1610-validation-errors)
    - [16.11 The example map, annotated](#1611-the-example-map-annotated)
    - [16.12 Tips for writing a map for a new device](#1612-tips-for-writing-a-map-for-a-new-device)
  - [17. The API](#17-the-api)
    - [17.1 Enabling it](#171-enabling-it)
    - [17.2 The write switches, and why they are never saved](#172-the-write-switches-and-why-they-are-never-saved)
    - [17.3 Port 1220: JSON lines](#173-port-1220-json-lines)
    - [17.4 Port 1219: EVRe pass-through](#174-port-1219-evre-pass-through)
    - [17.5 Examples](#175-examples)
  - [18. The EVRe protocol as the Studio uses it](#18-the-evre-protocol-as-the-studio-uses-it)
    - [18.1 Frame layout](#181-frame-layout)
    - [18.2 Function codes](#182-function-codes)
    - [18.3 Error codes](#183-error-codes)
    - [18.4 CRC](#184-crc)
    - [18.5 Receiving: the parser](#185-receiving-the-parser)
    - [18.6 Pipelining and answer matching](#186-pipelining-and-answer-matching)
    - [18.7 Timeouts and the keep-alive](#187-timeouts-and-the-keep-alive)
    - [18.8 The connect sequence and the device ID read](#188-the-connect-sequence-and-the-device-id-read)
- **[Part III. Internals](#part-iii-internals)**
  - [19. Architecture](#19-architecture)
    - [19.1 Layers](#191-layers)
    - [19.2 The GUI thread and the I/O thread](#192-the-gui-thread-and-the-io-thread)
    - [19.3 Who owns what](#193-who-owns-what)
    - [19.4 The one-writer rule of `RegTable`](#194-the-one-writer-rule-of-regtable)
  - [20. Threads and timing](#20-threads-and-timing)
    - [20.1 The threads](#201-the-threads)
    - [20.2 Why Qt timers do not pace the polls](#202-why-qt-timers-do-not-pace-the-polls)
    - [20.3 The high-resolution ticker (Windows)](#203-the-high-resolution-ticker-windows)
    - [20.4 The condition-variable ticker (Linux, macOS)](#204-the-condition-variable-ticker-linux-macos)
    - [20.5 Due ticks and catch-up rules](#205-due-ticks-and-catch-up-rules)
    - [20.6 The frame clock](#206-the-frame-clock)
    - [20.7 Every QObject on the I/O thread is a child of a moved object](#207-every-qobject-on-the-io-thread-is-a-child-of-a-moved-object)
    - [20.8 Time bases](#208-time-bases)
  - [21. Data flows step by step](#21-data-flows-step-by-step)
    - [21.1 The connect sequence](#211-the-connect-sequence)
    - [21.2 One poll cycle](#212-one-poll-cycle)
    - [21.3 A write from the table](#213-a-write-from-the-table)
    - [21.4 A map load](#214-a-map-load)
    - [21.5 An API request](#215-an-api-request)
  - [22. Source layout](#22-source-layout)
    - [22.1 Every file](#221-every-file)
    - [22.2 Module `evre`](#222-module-evre)
    - [22.3 Module `io`](#223-module-io)
    - [22.4 Module `model`](#224-module-model)
    - [22.5 Module `api`](#225-module-api)
    - [22.6 Module `ui`](#226-module-ui)
  - [23. The chart renderer](#23-the-chart-renderer)
    - [23.1 Data structures](#231-data-structures)
    - [23.2 Binning on absolute time](#232-binning-on-absolute-time)
    - [23.3 Chunks and memory trimming](#233-chunks-and-memory-trimming)
    - [23.4 The smooth delay](#234-the-smooth-delay)
    - [23.5 Y range](#235-y-range)
    - [23.6 The drawing fast path](#236-the-drawing-fast-path)
    - [23.7 The plot on a graphics card](#237-the-plot-on-a-graphics-card)
    - [23.8 Measurements and math lines](#238-measurements-and-math-lines)
    - [23.9 Frame budget](#239-frame-budget)
  - [24. Extending the Studio](#24-extending-the-studio)
    - [24.1 An API command](#241-an-api-command)
    - [24.2 A register type](#242-a-register-type)
    - [24.3 A link type](#243-a-link-type)
    - [24.4 A tab](#244-a-tab)
    - [24.5 A setting](#245-a-setting)
    - [24.6 General rules for a change](#246-general-rules-for-a-change)
  - [25. Building](#25-building)
    - [25.1 Requirements](#251-requirements)
    - [25.2 Windows (Qt + MinGW + CMake + Ninja)](#252-windows-qt--mingw--cmake--ninja)
    - [25.3 Linux](#253-linux)
    - [25.4 CMake targets and options](#254-cmake-targets-and-options)
  - [26. Tests](#26-tests)
    - [26.1 Overview](#261-overview)
    - [26.2 The GUI test](#262-the-gui-test)
    - [26.3 The API test](#263-the-api-test)
    - [26.4 The fake devices](#264-the-fake-devices)
    - [26.5 The probe](#265-the-probe)
    - [26.6 `EVRE_SHOT`](#266-evre_shot)
  - [27. Design decisions and pitfalls](#27-design-decisions-and-pitfalls)
  - [28. Glossary](#28-glossary)
  - [29. Open items](#29-open-items)
- **[Part IV. Making maps](#part-iv-making-maps)**
  - [30. The Map editor](#30-the-map-editor)
    - [30.1 The table](#301-the-table)
    - [30.2 Bulk edits](#302-bulk-edits)
    - [30.3 Adding, duplicating, copying, deleting](#303-adding-duplicating-copying-deleting)
    - [30.4 Undo and redo](#304-undo-and-redo)
    - [30.5 The form](#305-the-form)
    - [30.6 The checks](#306-the-checks)
    - [30.7 Map settings](#307-map-settings)
    - [30.8 Saving](#308-saving)
  - [31. Value names and bit fields](#31-value-names-and-bit-fields)
    - [31.1 Value names (enum)](#311-value-names-enum)
    - [31.2 Special values](#312-special-values)
    - [31.3 Bit fields](#313-bit-fields)
    - [31.4 A field on the chart](#314-a-field-on-the-chart)
  - [32. Exporting and importing](#32-exporting-and-importing)
    - [32.1 Markdown specification](#321-markdown-specification)
    - [32.2 C header](#322-c-header)
    - [32.3 Python module](#323-python-module)
    - [32.4 CSV and Import CSV](#324-csv-and-import-csv)
    - [32.5 The JSON Schema](#325-the-json-schema)
    - [32.6 Device table for the EVRe library](#326-device-table-for-the-evre-library)
  - [33. Overlays](#33-overlays)
  - [34. The `evre` command-line tool](#34-the-evre-command-line-tool)
    - [34.1 `evre check`: does the device answer as its map says?](#341-evre-check-does-the-device-answer-as-its-map-says)
  - [35. `evre-sim`: a device made from a map](#35-evre-sim-a-device-made-from-a-map)
  - [36. The `evre` Python package](#36-the-evre-python-package)

---

# Part I. Using the Studio

## 1. Overview

### 1.1 What EVRe Studio is

EVRe Studio is a desktop tool for devices that speak the EVRe register protocol. It connects to one device over TCP or a serial port (including USB CDC). It reads the device's registers many times a second and shows them in a table. It charts them like an oscilloscope, records them to CSV and writes them. It also serves them to other programs through a local API.

The Studio has no built-in knowledge of any device. It learns what a device holds from a **device map**: a JSON file that lists the registers, with their addresses, types, units, bit fields, value names and which ones may be written. One map describes one kind of device. The repository ships one example, `maps/example_device.json`, and every example in this document uses its registers.

The Studio runs on Windows and Linux. It is written in C++17 with Qt 6.

### 1.2 Who it is for

- **Engineers who bring up or test a device.** They watch live values, plot signals, measure energy or averages over a time span, flip configuration bits and record long runs to CSV.
- **Test and automation scripts.** Python, MATLAB, LabVIEW or any program that can open a socket can read and write registers by name through the Studio while it runs. The Studio stays the only program on the port.
- **Developers of EVRe firmware.** They see every frame on the link, send raw requests and check how a device answers errors, pipelined requests and block reads.

### 1.3 Features

| Area | What it does |
|---|---|
| Links | TCP (any EVRe-over-TCP server or gateway) or a serial / USB CDC port. Slave address, answer timeout, pipelining (*In flight*), reconnect by itself. |
| Login | An optional token, written after connecting to a login register that the map declares. |
| Device check | Reads DEVICE_ID and STATUS at every connect. Warns when the map was made for another device ID. |
| Register table | Live values with units. Decoded bit fields and enum names. Stale values turn grey and changed values glow. Search, group filter, tooltips, a detail line for the selected register. |
| Writes | Only when *Allow writes* is on. Registers marked danger ask first. Every write is read back, and a write asks first when the value changed while you were editing. |
| Quick write | A panel for the selected register: a value box with the map's range, its list of named and special values, a Default button, and its bits drawn as in a datasheet (click a bit to flip it). |
| Map editor | Make a map from nothing or change one: a table edited in place, bulk edits, copy and paste, undo and redo, value names and bit fields on a bit strip, limits, defaults, notes, live checks, a live preview of the value. Saving changes only what was edited (Part IV). |
| Export | The map as a Markdown specification, a C header, a Python module, CSV, or the device table for firmware on the EVRe library; CSV back in; a JSON Schema of the format (chapter 32). |
| Chart | Oscilloscope style: the memory depth is set apart from the view, with Hold / Live, a memory strip, cursors A and B, Auto or Manual Y, Normalise and Smooth. |
| Measurements | Per line: the value at A and B, B − A, and min, max, mean, RMS and area over A..B or over the view. Area units follow the line's unit (W → J and Wh, A → A·s and Ah). |
| Math lines | Formulas over registers (`SUPPLY_V * SUPPLY_I`), drawn and measured like registers. |
| Several devices on one link | A bus file (`evre-bus/1`) puts devices at their slave addresses on one link (RS-485, a gateway). Their registers are named after them (`D1_SPEED`) in the table, chart, CSV and API; a device that stops answering goes offline without slowing the others (3.9). |
| Broadcast | One write to every device at once (slave 0), only where it means the same to each, then each read back (3.10). |
| Auto send | A device that can sends its read-only block by itself at a set rate: one chart point and one CSV row per frame (13.8). |
| Monitor | Every frame sent and received. Raw reads and writes, to any slave address or as a broadcast. |
| Log | Every event in a tab and in a daily file. Pop-ups for warnings and errors; the same message pops up at most every 30 s (11.3). |
| CSV | One row per poll of the registers you choose (per frame with auto send). |
| API | Port 1220: JSON lines by register name. Port 1219: EVRe pass-through for existing EVRe clients. |
| Speed | All device I/O runs on its own thread with a high-resolution poll clock. Registers are merged into block reads and polls can overlap. Auto send: one device sends its read-only block by itself, up to 4000 frames/s (13.8). |

### 1.4 The design rule: the device is the truth

The Studio never shows what it *thinks* a register holds. It only shows what the device last reported. Many details in this document follow from this rule:

- **A write does not change the table.** The value typed goes to the device. The Studio then reads the register back, and the table shows the answer. A device that clamps 150 % to 100 % shows 100 %.
- **Values carry their age.** A value that has not been refreshed for a while turns grey, and its tooltip says how long ago it was read (see 4.3).
- **A write checks for changes first.** If the device, or another client, changed a register while you were typing a new value, the Studio shows you both values and asks before it writes yours (see 6.2).
- **The device's refusals shape the poll.** If the device refuses a read of a merged block, the block is split. If it refuses one register for good, that register is marked *not available* and is no longer polled. The map is not edited: the next connect tries again.
- **The API reads the device.** A `get` on the JSON API reads the registers fresh from the device, and a `set` answers with the value read back after the write.
- **The device's ID is checked.** Every connect reads DEVICE_ID. The Studio warns when it does not match the ID in the map.

## 2. Getting started

This chapter builds the Studio, starts the fake device from `tests/` and walks through a first session: connect, read, plot, measure, write and record.

### 2.1 Build

You need:

- Qt 6.5 or newer, with the modules Widgets, Network, SerialPort and Test. Test is needed for every build, not only for the GUI test: CMake asks for all four modules at once, so configuring fails without it.
- a C++17 compiler
- CMake 3.21 or newer

From the source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<your Qt 6 folder>
cmake --build build
```

- **Linux:** the distribution's Qt 6 development packages are enough. On Debian and Ubuntu these are `qt6-base-dev` and `qt6-serialport-dev`. You can usually leave out `CMAKE_PREFIX_PATH`.
- **Windows:** use a Qt installation for your compiler, with the Qt Serial Port module. To run the program outside the build environment, copy the Qt libraries next to it with `windeployqt --release build/EVReStudio.exe`.

Chapter 25 has the details: toolchains, short build paths on Windows, deployment and the CMake targets.

The build creates four programs:

| Program | Purpose |
|---|---|
| `EVReStudio` | The Studio. |
| `evre_gui_test` | Drives the real window with QtTest against the fake device. |
| `evre_probe` | The protocol core without the window, for checking a device. It reads the registers and writes nothing, except the login token when `EVRE_TOKEN` is set (26.5). |
| `evre_fake_fast` | A fast fake device, for measuring the Studio. |

After each build, the contents of `maps/` are copied next to the executable, into `build/maps/`.

### 2.2 Start the fake device

`tests/fake_device.py` is a small EVRe device written in Python. It needs only the standard library. It listens on TCP `127.0.0.1:1210` and serves the registers of `maps/example_device.json`:

```sh
python3 tests/fake_device.py                 # Windows: python tests\fake_device.py
# fake EVRe device on 127.0.0.1:1210 (16 registers, login at 0xF000)
```

Options: `--port <n>` (default 1210), `--map <file.json>` (default `maps/example_device.json`) and `--token <text>`
(the login token it accepts, default `example-token`) and `--slave <n>` (the slave address it answers, else the
map's). Its STATUS has `CAP_AUTO_SEND` (bit 11) set, but it does not send frames by itself (`evre_fake_fast` does,
26.4).

What the fake device does:

- Answers DEVICE_ID with the map's `device_id` (0x1001) and STATUS with 0x3F01 (protocol revision 1, plus capability bits).
- Moves the read-only `f32` registers (SUPPLY_V, SUPPLY_I, TEMPERATURE) as slow sine waves between 5 and 15.
- Moves the read-only `i16`/`i32` registers without bit fields (PRESSURE) as slower sine waves.
- Counts milliseconds in the read-only `u32` registers whose unit is `ms` (UPTIME).
- Keeps whatever is written to read-write registers.
- Refuses any address that lies in no register of the map, with `ERROR_RESP` code 4 (offset out of range).
- Checks the login: the map's login register (0xF000, 16 bytes) accepts the token `example-token` and refuses any
  other with `ERROR_RESP` code 3 (permission denied). It never requires a login (3.6, 26.4).

To measure the Studio at thousands of polls a second, use `evre_fake_fast [port] [map.json] [token]` instead. It is the same device in C++. The Python device takes about 100 µs per request and becomes the limit. Both are described in more detail in 26.4.

### 2.3 A first session, step by step

1. **Start the Studio.** Run `build/EVReStudio` (on Windows `build\EVReStudio.exe`). The window has a sidebar on the left and four tabs: *Registers*, *Chart*, *Monitor* and *Log*.
   - On a fresh install the link settings are already TCP, host `127.0.0.1`, port `1210`.
   - The example map is loaded by itself: with no saved choice, the Studio opens the first `*.json` in the `maps/` folder next to the program.
   - The sidebar's *Device map* card reads **Example device**, *16 registers · example_device.json*.
2. **Connect.** Click **Connect** in the *Connection* card.
   - The pill under the button turns green: *Connected · 127.0.0.1:1210*.
   - The line under the pill reads *Device ID 0x1001 · protocol rev 1*.
   - The Log tab records `connecting to 127.0.0.1:1210`, `connected: 127.0.0.1:1210` and `device ID 0x1001, protocol revision 1`.
3. **Read.** The Registers tab now shows live values at the default interval of 100 ms. The *Polling & recording* card reads about *10.0 polls/s, 15 registers in 3 reads*. Of the 16 registers, 15 are polled. MSG_BUFFER is a 255-byte array, which is read only on request (see 13.3).
   - SUPPLY_V, SUPPLY_I and TEMPERATURE change. Each changed value glows for a moment.
   - UPTIME counts up.
   - STATE shows `0x0000`, with an ⓘ beside it. Hover the ⓘ to see the decoded fields: `MODE=idle`.
4. **Look at one register in full.** Click the SUPPLY_V row. The detail line under the table shows its address, type, access, group, value, raw bytes and description.
5. **Plot.** Tick **Plot** on SUPPLY_V and SUPPLY_I, then open the **Chart** tab. Both lines scroll in from the right, and the legend shows their latest values. Move the mouse over the chart to read both lines at one moment.
6. **Add a math line.** Click **ƒ Math → New math line…**. The dialog offers the name `P` and the unit `W`. Type the formula `SUPPLY_V * SUPPLY_I` and click OK. A third line, *ƒ P*, shows the power.
7. **Measure.** Click **Measure**. A table under the chart shows min, max, mean, RMS and area for every line over the view. For *ƒ P* the area is in J and in Wh. Click **Cursors**, then click the chart twice to place A and B. The measurements now cover A → B.
8. **Write.** Go back to the Registers tab and tick **Allow writes** (it is off at every start).
   - Select LED_MODE. The quick-write panel under the table shows *Write LED_MODE* with a list of its values.
   - Pick `blink  (2)`. The Studio writes it, reads it back, and the table shows `2`, decoded `blink`.
   - The Log records `written: LED_MODE = 2 (02 at 0xD085)`.
9. **Try a danger register.** Double-click the value of MOTOR_SPEED (its Access column shows `rw ⚠`), type `300` and press Enter. A dialog asks *Write 300 to MOTOR_SPEED (0xD086)?*. Only **Write** sends it.
10. **Record.** Click **● Record CSV** in the sidebar and choose a file. Every poll adds a row. Click **■ Stop recording** to close the file. The card then says *Saved N rows to <file>*.
11. **Close.** The Studio saves the link, the polling interval, the chart settings, the window's geometry and the map you used last. The next start opens the same way.

### 2.4 From the command line

The same session, connected at start, with two registers already plotted and the chart shown:

```sh
EVReStudio --tcp 127.0.0.1:1210 --map maps/example_device.json --plot SUPPLY_V,SUPPLY_I --tab chart --connect
```

All options are in chapter 14.

## 3. Connecting

Everything about the link is in the sidebar's **Connection** card. The card has, from top to bottom:

- the choice **TCP** / **Serial / USB**
- the fields for the chosen link
- **Slave**, **Timeout** and **In flight**
- **Reconnect by itself**
- **Connect**, with the state pill and the device line under it

### 3.1 TCP

| Field | Range / default | Meaning |
|---|---|---|
| host | text, default `127.0.0.1` | Name or IP address of the EVRe-over-TCP server. Spaces around it are ignored. |
| port | 1 – 65535, default `1210` | Its TCP port. |
| token | text, empty by default | See 3.6. Shown as dots and never saved. |

The server can be a device with a network port, a TCP gateway in front of a serial device, or another EVRe Studio's pass-through port 1219 (see 17.4). The Studio turns Nagle's algorithm off on the socket, so every request leaves at once.

The Studio sets no connect timeout of its own. A host that does not answer takes as long as the operating system's connect timeout before the pill shows the error.

### 3.2 Serial / USB

| Field | Range / default | Meaning |
|---|---|---|
| port | the ports found | Every serial port the operating system lists. The refresh button beside it (a circular arrow) looks for ports again. |
| Baud | editable list, default `115200` | 9600, 57600, 115200, 230400, 460800, 921600, 2000000, or any rate typed. |

- **Port settings.** The port is opened at 8 data bits, no parity, one stop bit and no flow control. After opening, the Studio sets **DTR** on (many USB CDC devices send nothing until DTR is set) and clears the port's buffers. RTS is left as the driver sets it.
- **The port list.** A USB port is listed as `COM7  · <description>  1234:abcd`: the port name, the driver's description, and the USB vendor and product IDs in hex. A port without USB IDs shows its description, if it has one. With no port at all, the list shows *no ports found*.
- **The map's USB IDs.** If the loaded map declares `"usb": { "vid", "pid" }` (see 16.2), a port with exactly those IDs is listed with the map's device name in place of the driver's description. The Studio picks that port by itself only when no port was selected before. This happens, for example, when the list said *no ports found* until the device was plugged in and the refresh button was clicked.
- **Baud rate.** For USB CDC the baud rate usually does not matter, but for a real UART it must match the device.
- **One program per port.** A serial port can be open in one program only. See 15.5.
- **No token.** The token is sent only over TCP.

### 3.3 Slave and timeout

| Option | Range / default | Meaning |
|---|---|---|
| **Slave** | 1 – 255, default 1 | The EVRe slave address put in every request. It is set from the map's `"slave"` when a map loads, and written back to the map when you save it. It is not a setting of its own. 0 is the broadcast address, which no device answers: a map with `"slave": 0` is an error. On a bus each device has its own (3.9) and the box only shows the selected one's. |
| **Timeout** | 20 – 10000 ms, default 500 | How long the Studio waits for an answer before it fails the request with `timeout (<n> ms)`. Radio links and slow gateways need more. |

The Studio sends Slave, Timeout and In flight to its I/O engine each time it connects. It also sends them each time In flight changes. After changing Slave or Timeout while connected, reconnect for the change to apply. The Timeout also sets how soon a value turns grey (see 4.3), and that part applies at once.

### 3.4 In flight

**In flight** (1 – 1024) is the number of requests the Studio sends before their answers come. Up to 1024 because a
poll of several devices on one link (3.9) is many reads: twelve devices of three reads make 36, and two polls at once
need 72. The range shows beside the box (*1 – 1024*). A number typed past it stays as typed, the box's edge turns
amber, and Enter or leaving the box makes it the nearest end of the range (2000 becomes 1024); Slave and Timeout do
the same, their range in their tooltips.

| Value | Behaviour |
|---|---|
| 1 | Strictly one request at a time. Use it for a device on a UART, which takes one request at a time. |
| 2 or more | Requests are pipelined. The next ones go out while the answers to the first are still on their way. This is much faster over TCP to a server that accepts several requests at once. |

The value is kept separately for TCP and for serial. The defaults are **4 for TCP** and **1 for serial**, and switching between TCP and Serial / USB loads the value saved for that link. The value is saved each time it changes.

Only reads overlap. A write waits until everything sent before it has been answered, and nothing is sent while a write is unanswered. So a read that follows a write always sees the new value (see 18.6). How In flight sets the poll rate is explained in chapter 13.

### 3.5 Connect, Disconnect and Reconnect by itself

The button under the options changes with the link's state:

| Button text | When | Clicking it |
|---|---|---|
| **Connect** | disconnected | opens the link |
| **Cancel** | connecting | gives up |
| **Disconnect** (red) | connected | closes the link |
| **Stop reconnecting** | waiting to try again (see below) | stops the retries |

**Reconnect by itself** is on by default. After you clicked Connect, the Studio keeps trying until you click Disconnect or Stop reconnecting:

- A link that was up and is lost is tried again after **0.5 s**.
- A connect attempt that fails is tried again after **2 s**.

The retries do not log *connecting to …* again. Each failure logs *not connected: <why>* as a warning. The log counts repeated identical lines instead of writing them again (see 11.3).

With **Reconnect by itself** off, a lost or failed link stays down until you click Connect.

A serial link with no port selected fails at once. The pill shows *No serial port*.

### 3.6 The token and the map's login register

Some devices, or the TCP gateway in front of them, answer only after the client has logged in with a token. The Studio supports this in a generic way:

- **The map says where the token goes.** An optional top-level key declares the login register and its size in bytes (see 16.2):

  ```json
  "login": { "addr": "0xF000", "size": 16 }
  ```

- **The token box holds the token.** It is in the TCP fields, with the placeholder *Token (not stored)* and the tooltip *Sent to the map's login register after connecting; never saved*. The environment variable `EVRE_TOKEN`, if set, fills the box at start. There is no command-line option for the token, because a command line is visible to other users of the computer.
- **When it is sent.** The token is written only when a token is set **and** the map declares a login register. It is the very first request after the TCP link opens, because a gateway may drop a client that does not log in soon.
- **What is written.** The token is encoded as UTF-8, then cut or zero-padded to exactly `size` bytes. It is written with an acknowledged write (`WRITE_ACK`). For example, the token `abc` with size 16 becomes `61 62 63 00 00 00 00 00 00 00 00 00 00 00 00 00`.
- **A token but no login register.** The Log shows the warning *the map declares no login register: the token was not sent*.
- **A refused login.** When the write fails (an error answer, or a timeout), the Log shows the error *token refused: <reason>*, and it pops up. For example: *token refused: permission denied*. The line under the pill shows it too, until the answer to the device ID read replaces that line.
- **No token.** Nothing is written, whatever the map says.

Only a TCP link sends the token. A serial link never does.

The example map declares `"login": { "addr": "0xF000", "size": 16 }`. With the token box empty nothing is written
there. The fake device accepts the token `example-token` and refuses any other (26.4): start the Studio with
`EVRE_TOKEN=example-token` to see a login accepted, with another token to see *token refused: permission denied*.

### 3.7 The device ID check

Right after the login (or right after the link opens, when there is none), the Studio reads 4 bytes at `0xA000`. These are DEVICE_ID (`0xA000`) and STATUS (`0xA002`), which every EVRe device has.

- **The answer comes.** The line under the pill shows *Device ID 0x1001 · protocol rev 1*. The protocol revision is the low byte of STATUS. The Log records *device ID 0x1001, protocol revision 1*.
- **The map is for another device.** If the map has a non-zero `device_id` that differs from the device's, the line adds *the map is for 0x1001* in amber. The Log adds the warning *the map is for device 0x1001, this is 0x2002*. The Studio still polls: the warning only tells you that the map may not fit.
- **The read fails.** The line shows *DEVICE_ID not read: <reason>*, and the same error goes to the Log. Polling goes on.

A map without `device_id` (or with 0) is not checked.

### 3.8 What the state pill shows

The pill is a dot and the state, on a tint with round ends and no edge: a state, not a second button. *Disconnected*
has no tint at all. The pill is one line and its tooltip has the whole text. An address is never cut: when
*Connected · 192.168.0.254:1209* is longer than the pill, it shows *192.168.0.254:1209* alone (green, with the dot).
Only what still does not fit, a long reason or a long host name, is cut in the middle. The device line under it is always there, two lines tall, so
the card never changes height.

| Pill | Colour | Meaning |
|---|---|---|
| *Disconnected* | grey, no tint | No link. |
| *Connecting…* | amber | The link is opening. |
| *Connected · 127.0.0.1:1210* | green | The link is open. The text names the link: `host:port`, or `COM7 @ 115200` for serial. |
| the reason, for example *Connection refused* | red | The last attempt failed, or the link was lost. The text is the reason, cut in the middle when it is longer than the pill; the tooltip and the Log have it in full. |

The device line under the pill keeps its last text after a disconnect, until the next device ID answer or login error replaces it.

### 3.9 Several devices on one link (a bus)

EVRe puts a slave address in every frame, so several devices can share one link: an RS-485 line, or a TCP gateway
with several devices behind it. The Studio holds them as a **bus**: a small file, `evre-bus/1`, that says which
devices share the link, at which slave address, and with which map.

```json
{ "format": "evre-bus/1", "name": "Test bench",
  "devices": [
    { "name": "D1", "slave": 1, "map": "motor.json" },
    { "name": "D2", "slave": 2, "map": "motor.json" },
    { "name": "D3", "slave": 7, "map": "supply.json", "poll": false } ],
  "broadcasts": [
    { "name": "Stop all", "register": "MOTOR_SPEED", "value": "0" } ] }
```

| Key | Meaning |
|---|---|
| `name` | The device's name: letters, digits and `_`, starting with a letter. It is the prefix of its registers' names. Unique on the bus, and no name followed by `_` may start another (`D1` and `D1_A` would make `D1_A_X` read two ways). |
| `slave` | Its address on the link, 1 to 255, unique. It replaces the map's `"slave"`. 0 is the broadcast address (3.10). |
| `map` | Its map, relative to the bus file (or absolute). Several devices may share one map. |
| `poll` | Optional, default `true`. `false`: the device is on the bus but not polled; it is still read and written on request. |
| `broadcasts` | Optional, at the top level (not per device): broadcasts kept by name (3.10). `register` is a register by its name in the map (`MOTOR_SPEED`, not `D1_MOTOR_SPEED`), `value` as typed in quick write. |

A map describes a device; the bus file describes an installation. The map format does not change. Keys the Studio
does not know are kept when the bus file is saved again. Tokens are never in it.

**The Devices on the link card** (in the sidebar, under Connection):

| One device | A bus |
|---|---|
| A line that says so, **New bus** (a bus of the map's device, at the sidebar's slave address, to add the others to) and **Open bus…**. | The list of the devices: a dot (green: answers, red: offline, grey: not connected), then *D1 · slave 1 · motor.json*. **+ Device**, **− Device**, **Edit…** (or a double-click), and the **Bus file** menu: **Open…**, **Save**, **Save as…** and **Close bus** (back to one device: the selected one's map), and the **Broadcast** menu (3.10). |

**Names.** On a bus every register is named after its device: `D1_SUPPLY_V`, `D2_STATUS`. The table, the chart, the
math lines, the CSV columns and the API all use those names, so any register of any device is reached by its name
alone, without a slave address.

**The selected device.** The Registers tab shows the device selected in the list, and the Map editor edits its map.
The Registers tab has a picker of its own over the table: a device, or **All devices**, every device's registers in
one table (with twelve devices of one map, a search for `SPEED` lists `D1_SPEED` to `D12_SPEED`); editing the
definition of another device's register there selects that device first, and Remove takes only the selected
device's registers. The Map editor says over its toolbar whose map it is (*Map of D1, D2 · motor.json · a change
applies to all 2 devices*; more than four devices are named in short, *D1, D2, D3 and 9 more*, all of them in its
tooltip), and **Live values from** picks, among the devices of that map, the one whose values its live line shows.
Every device picker (the Registers tab's, the Map editor's, the Monitor's) shows a device as the Devices card does:
its state's dot (green: answers, red: offline, grey: not connected), then *D1 · slave 1*, at a fixed width (a long
name is cut), the whole name and the state in words in the row's tooltip. A device chosen in any of them is selected everywhere.
Selecting a device with another map asks first when the map edited has unsaved changes. The Slave box of the
Connection card shows the selected device's address and is read-only: each device has its own, in the bus file; the
line under the pill names the device (*D1: Device ID 0x1001 · protocol rev 1*, or *D2: Device ID not read yet*
until it is read); it is always two lines tall, so the card does not move. On a bus the Map settings' Slave box is
disabled too. On a bus the Monitor names the devices
in place of its Slave number, with *Broadcast · slave 0* after them, and starts at the selected device. The chart, the
math lines, the CSV and the API see every device: plot `D1_SPEED` and `D2_SPEED` together, or write a math line
`D1_SPEED - D2_SPEED`.

**Polling.** A poll reads every polled device, one after the other through the one queue, and never merges a read
across two devices. With In flight 1 (a UART bus) the poll rate is one over the sum of the devices' answer times. On a serial link
with a bus, In flight is 1 whatever is set: a UART answers one request at a time. The value set is kept, and used
again with one device or over TCP.

**A device that stops answering.** After 3 timeouts in a row it is *offline*: its dot turns red, the Log says so once,
it is left out of the polls (its values go grey, as any stale value) and it is asked for its DEVICE_ID again: every
2 s one offline device, in turn, with never more than one such request waiting.
The others keep their poll rate instead of each poll waiting a timeout for it. Its first answer brings it back. On a
new connection every device starts online.

**After connecting**, every device is logged in (its map's login register, with its own token, typed in its
**Edit…** dialog and never saved, or else the token of the Connection card)
and asked for its DEVICE_ID; each answer is logged with its name (`D2: device ID 0x1001, protocol revision 1`), and a
map for another device ID is warned of per device. The line under the pill shows the selected device's.

The bus file chosen last opens at the next start, as a map does (setting `map/bus`). Opening or making a map closes
the bus. Opening another bus asks first when the open one has unsaved changes. A start never opens a bus file as a
map. `--bus FILE` opens one from the command line (14.1). **+ Device** takes the next free slave address; when all
255 are taken it adds nothing and the Log says why.

### 3.10 Broadcast

Slave address **0** is the broadcast address: every device on the link hears the frame, and none answers it. So only
a WRITE without acknowledge can go there, and nothing says that a device took it.

A broadcast must mean the same to every device. The Studio sends one only when:

| Where | Allowed |
|---|---|
| The reserved bank's writable registers, `0xA004` .. `0xA105` (CONFIG, MSG_CNT, MSG_BUFFER) | always: every EVRe device has them, whatever its map. For example a CONFIG write to every device at once. |
| `0xA000` .. `0xA003` (DEVICE_ID, STATUS) | never: read-only on every device. |
| Anywhere else | only when every device on the link has the same register map (the same registers and device ID), and every byte written lies in a writable register of it. |
| CONFIG with AUTO_SEND (bit 3) set | never, wherever it falls: every device would start sending by itself at once, over the others (13.8). A CONFIG broadcast with the bit clear goes as above. |

Three ways to send one, all behind **Allow writes**:

- **To all devices**, in the quick-write panel (5.1), on a bus, offered once a value is typed: the value typed, after a confirmation, then each device
  is read back. The Log says whether every device took it, or which one holds something else.
- **The Monitor tab** (10.1): Slave 0 locks the function to WRITE (no ack); the bytes typed go out as they are. A
  broadcast the rule refuses is not sent: `!! no broadcast: ...`.
- **The Broadcast menu** of the Devices card, on a bus: the broadcasts kept in the bus file, each sent in one click
  (after the same confirmation as To all devices, with the same read-back), **New broadcast…** (a name, a register
  the rule allows, a value it takes) and **Remove**. Bus file > Save keeps them in the file (`"broadcasts"`). The
  kept broadcasts are disabled while not connected or with Allow writes off; the tooltip says why.

A broadcast names its register by the map's own name (`FAN_SPEED`) or by a bus name (`D1_FAN_SPEED`), in the
command line (`evre broadcast --bus`, 35), the API's `broadcast` command (16) and the Python package alike. The
API's `broadcast` with one device goes to slave 0 too. `evre broadcast` refuses a read-only register.

A device built on EVRe 1.1 takes a broadcast into its own registers (0xD000 and up) only when it allows it
(`ACCEPT_BROADCAST_D000`, STATUS bit 14); a 1.0 device takes it as a unicast WRITE. Read the register back to know.

The API's pass-through (17) sends a client's broadcast WRITE on a bus under the same rule and the same write switches.

## 4. The Registers tab

The Registers tab has four parts, from top to bottom:

- the toolbar
- the register table
- the detail line
- the quick-write panel (see chapter 5)

### 4.1 The toolbar

| Control | What it does |
|---|---|
| Search box | Filters the table as you type (see 4.6). |
| Groups button | A menu of the map's groups, to show one, several or all (see 4.7). |
| **Plot shown** / **Unplot shown** | Puts every numeric register the table shows now on the chart, or takes them off (see 4.8). |
| **Allow writes** | Off at every start. On: values can be edited and written. Shown in amber bold while on (see 6.1); it keeps the bold width when off, so the buttons beside it do not move. |
| **+ Register** | Adds a register to the map (see 4.11). |

### 4.2 The columns

Each column's header is aligned as its cells: Value's at the right, over its numbers; the others at the left.

| Column | Content |
|---|---|
| **Plot** | A tick puts the register on the chart. It exists only for numeric registers: a byte array shows a dash, and its tooltip says why (*Not plotted: a bytes register is not a number*). |
| **Log** | A tick includes the register in the CSV recording. All are ticked when a map loads. |
| **Address** | `0xD004`: four upper-case hex digits. |
| **Name** | The name from the map. |
| **Value** | The value as the device holds it, in bold monospace (formats below). |
| **Unit** | The unit from the map. |
| **Decoded** | Bit fields or the enum name (see 4.10). Hidden by default. |
| **Type** | `u8` … `f32`, or `bytes[N]` for a byte array. |
| **Access** | `ro`, `rw` (accent colour), `wo`, or `rw ⚠` (amber: a danger register), as the map and the Map editor write it. |
| **Group** | The group from the map. It takes the rest of the width. |

The Value column shows:

| Register | Shown as | Example |
|---|---|---|
| Integer | the number | `12500` |
| Integer with `"format": "hex"`, with bit fields, or with the unit `bitmask` | hex, two digits per byte | `0x1001`, `0x0005` |
| An enum register | the number (the name is in Decoded) | `2` |
| `f32`, or any register with a scale or an offset | a number whose decimals shrink as it grows: 1 from 1000 up, 2 from 100, 3 from 1, 4 below 1 | `1234.5`, `123.45`, `12.345`, `0.4200` |
| A float that is not a number | the text NaN | `NaN` |
| Byte array | the first 24 bytes in hex, ` …` when there are more | `48 45 4C 4C 4F …` |
| Not read yet | a dash | `—` |
| Read failed and no good value yet | *error* in red | |
| Refused by the device for good | *not available* in red (see 4.5) | |

The columns fit their content when a map loads and after each edit of the map. The Value column is at least 110 pixels wide, so values have room to grow. Twice a second while the tab is shown, a column widens when its values have grown; when the tab is shown again (after the Chart, say), it widens at once, before the table is drawn, so it does not stretch in front of you. The columns do not shrink by themselves, so they do not jump. Right-click → **Fit columns** shrinks them to fit again. Decoded is as wide as its longest text. If the window is narrower than the columns, a scroll bar appears: nothing is cut off.

### 4.3 Glow and stale grey

- **Glow.** When a register's value changes, its cell glows in the accent colour. The glow fades out over 0.7 s. A value read again unchanged does not glow.
- **Stale grey.** A value that has not been read successfully for longer than **the larger of two poll intervals and one poll interval plus the timeout, plus the time between two value updates on screen** (*Show values*, 13.2) turns grey. This applies to the Value and Decoded columns. At the defaults (100 ms interval, 500 ms timeout, 10 values per second) that is 700 ms. With the interval *max* (0) it is the timeout plus 100 ms. With *Show values* at *every frame* nothing is added.

  Grey means *not refreshed*. Polling may be off, the link may be lost, or reads of this register fail while an older good value is kept. The value stays visible so you can still read it, but it is marked old.

### 4.4 Tooltips and the ⓘ

**The row's tooltip.** Hover any cell to see:

- the name and the address in lower case (`0xd004`)
- the description
- the raw bytes (`raw: 00 00 41 41`)
- the decoded fields, one per line
- the age: *read 0.1 s ago*, or in amber *not refreshed: last read 3.2 s ago*
- the last read error in red
- *⚠ writes are confirmed* for a danger register

**The ⓘ.** A value with bit fields or an enum name has a small circled *i* at the right of its cell. Hover the ⓘ to see the name, the value, the unit and every decoded field on its own line. The rest of the cell keeps the row's tooltip. The ⓘ lets you read decoded fields without turning on the Decoded column.

### 4.5 Errors and *not available*

A failed read is handled by its kind:

- **A timeout or a lost link.** The register keeps its last good value, which turns grey when it is old enough. The error goes to the tooltip and the detail line. It is read again at the next poll.
- **An error answer to a merged block.** The block is split into one read per register (see 13.4).
- **An error answer to one register.** Codes 3 (permission denied), 4 (offset out of range) and 5 (count out of range) mean the device does not have this register. It shows *not available* and is no longer polled. Other codes show *error* when there is no good value, and the register is polled again.

The Log records a register's read error when it starts, and *NAME: read again* when it works again (see 11.1). *Not available* comes back as a normal register at the next connect, when the Studio tries every register again. It is not an error: it is how the Studio learns which registers of a map this device, or this link, really has.

### 4.6 Search

The search box filters rows by a plain, case-insensitive substring. There is no other syntax: no wildcards, no regular expressions and no operators. A row is shown when the text is found in any of these:

- its **name**
- its **description**
- its **group**
- its **unit**
- its **address**, written as `0xd004` (lower-case, four digits)

Examples with the example map:

| Typed | Shows |
|---|---|
| `supply` | SUPPLY_V, SUPPLY_I |
| `°C` | TEMPERATURE, SETPOINT |
| `d08` | the four registers from 0xD080 to 0xD086 |
| `0xa0` | the protocol bank, 0xA000 – 0xA007 |
| `motor` | MOTOR_SPEED (by its name, and by its description *moves the motor…*) |

The search and the groups work together: a row must pass both.

### 4.7 The groups menu

The groups button lists **All groups**, then a check box for every group of the map, in the order the groups first appear.

- Tick one or several groups to show only those. Ticking a box does not close the menu.
- **All groups** unticks them all. No group ticked means every group is shown.
- A register whose group is empty is listed under *(no group)*.

The button shows the names of the groups ticked. When they do not fit, it shortens them and adds the count, for example `Power & supply, Sen… (3)`. The full list is in its tooltip. The groups ticked stay ticked when the map is edited, as long as the map still has them. They are not saved.

### 4.8 Plot and Log ticks, Plot shown / Unplot shown

- **Plot** ticks put registers on the chart. A line gets the next colour of an eight-colour palette. The palette starts again with a new map.
- **Log** ticks choose the CSV columns (see chapter 12).
- **Plot shown** ticks Plot on every numeric register the table shows now, after the search and the groups. When all of them are plotted already, or the chart is full (more shown than it may hold), the button reads **Unplot shown** and takes the shown ones off; with the chart full of others, Plot shown says to untick some first.
- **How many on the chart** depends on the rate the samples come at: **64,000 samples a second** for all the lines together, at most 64 lines (more cannot be followed by eye). Up to 1000 Hz: 64; 1500 Hz: 42; 2000 Hz: 32; 4000 Hz: 16. The rate is Auto send's while it is on, otherwise the polling interval's (2 ms: 500 Hz), or the polls measured when the interval is 0 (rounded up to 100 Hz). So the chart keeps its frame rate whatever the speed (23.6).
- Plot shown charts as many as there is room for, in the table's order, without a question; the status bar says how many were left off: *64 registers added to the chart, 34 left off: 64 at most at this rate*. A Plot ticked past the limit (or picked from the right-click menu) stays unticked, and the status bar says *At most 32 registers on the chart at 2000 samples a second: untick one first*. When the rate goes up (a shorter interval, Auto send on or faster), the newest lines come off the chart, and the Log names them. `--plot` past the limit says so in the Log. Math lines do not count (they have their own 64).
- A register the map marks `"plot": false` (a fixed value: an ID, a version, a setting; the Map editor's *plot* box) has no Plot box, only a dash whose tooltip says why; Plot shown passes it by, and `--plot` names it in the Log. A math line may still use it.
- The status bar confirms: *N registers added to the chart*.

Neither tick is saved. A map loads with every Plot off and every Log on. To plot registers at start, use `--plot` (see chapter 14).

### 4.9 The right-click menu

On a register:

- **Plot** or **Remove from chart** (numeric registers only)
- **Read now**: one read of this register at once, into the table. This is how a byte array too long to be polled is read (see 13.3).
- **Copy value**: the value text, as shown when the menu opened.
- **Edit definition…** (see 4.11)
- **Remove**: removes the **selected** rows from the map. It does not ask.

Anywhere on the table:

- **Plot all shown (N)**, **Remove shown from the chart**, **Remove all from the chart**
- **Fit columns**
- **Decoded column** (on / off)
- **Log all**, **Log none**
- **Add register…**

The column header's own right-click menu has **Decoded column** and **Fit columns**.

### 4.10 The Decoded column

The Decoded column is off by default, because most registers have nothing to decode. Turn it on from either right-click menu. The choice is saved (`ui/decodedColumn`).

What it shows:

- **An enum register:** the name of the value, or `? (7)` for a value that has no name.
- **A register with bit fields:** each field in turn, separated by two spaces:
  - a one-bit field with no value names is shown by its name, and only when it is set
  - any other field is shown as `NAME=value`, using the value's name if it has one and the number otherwise
- **Fields, none of them showing:** `—`.
- **Both an enum and fields:** the enum wins.

Examples from the example map:

| Register | Raw | Decoded |
|---|---|---|
| STATE | `0x0005` | `MODE=run  READY` |
| STATE | `0x0000` | `MODE=idle` |
| LED_MODE | `2` | `blink` |
| STATUS | `0x3F01` | `protocol revision=1  CAP_ERROR_FRAME  CAP_BROADCAST  CAP_MSG  CAP_AUTO_SEND  CAP_DFU` |

### 4.11 The detail line, adding and editing registers

**The detail line.** Under the table, the selected register is shown in full. The line wraps, so nothing is cut off, and its text can be selected. It shows:

- the name, address, type, access and group
- the value with its unit and raw bytes
- every decoded field, separated by ` · `
- the last error in red
- the description

With nothing selected, it says *Select a register to see it here in full: value, decoded, raw bytes, description.*

**Definitions are made on the Map editor tab** (chapter 30). **+ Register** and *Edit definition…* open it, with the
new register (after the selected one) or the chosen one selected; *Remove* takes the selected registers out as an
undoable step. What follows describes the old *Add register* form, kept for reference: the Map editor's table and
form have every field it had, and all the others.

*The old form started from a proposal:*

- the next address after the selected register (or after the last one)
- that register's group and type (a byte array becomes `u16`)
- the name `REG_XXXX`
- in an empty map: address `0xD000` and group `Registers`

The form has these fields:

| Field | Notes |
|---|---|
| Address | Hex (`0x…`) or decimal, 0x0000 – 0xFFFF. |
| Name | Required. |
| Type | `u8` … `f32`, `bytes`. |
| Size (bytes) | For `bytes` only, 1 – 255. Other types have a fixed size. |
| Unit | Free text. |
| Access | read-only / read-write. |
| Group | Pick one of the map's groups or type a new one. Empty becomes `Registers`. |
| Description | Free text. |
| Scale, Offset | Six decimals, ±1e9. |
| confirm every write (moves or powers something) | Makes it a danger register. |

**Add** stays disabled while the form has an error: *address: 0x0000 … 0xFFFF* or *a name is needed*. If the address is already in the map, the Studio asks *0xD004 is already in the map. Add another register there anyway?*. The new row is inserted in address order.

A plotted register's chart line starts again when its address, name, unit, scale or offset change; its live value is
kept while the register is read the same way.

Each of these edits marks the map *modified* in the sidebar. **Save** writes it, and closing the Studio with unsaved changes asks *Save them?* with Save, Discard and Cancel.

### 4.12 Editing a value in the table

With **Allow writes** on, double-click the value of a read-write register, or press the platform's edit key (F2 on Windows and most Linux desktops). Type the new value and press **Enter** to write it, or **Esc** to cancel.

- **The editor starts from the value as shown.** For example `12.000`, `0x0005` for a register with fields, or `2` for an enum register.
- **Polls do not overwrite your typing.** New values keep arriving while you type, but the editor's text stays yours.
- **What you can type** is set out in 5.2: a number, `0x…`, `0b…`, or an enum name.
- **The table does not take the typed value.** The write goes through the checks of chapter 6, and the table shows the device's answer after the read-back.

## 5. Quick write and the bit view

When you select a **read-write** register, the quick-write panel appears under the detail line. For a read-only register it hides.

### 5.1 The controls

The panel has one row of controls:

| Control | What it does |
|---|---|
| *Write NAME* (with ⚠ for a danger register) | The register the panel writes. |
| Value box | Type a value and press **Enter**, or click **Write**. Placeholder: *value, 0x1F, 0b101, or a name*, or with the map's limits *value (0 … 100 %), or a name*. |
| Named values list | Shown for a register with value names or special values. The special values come first, in shown units, then the value names: `name  (value)`, for example `blink  (2)`. Picking one writes it at once (a value name as its raw number, a special value as its shown value). The list follows the device's value, except while you are choosing in it. |
| **Write** | Writes the value box. |
| **Default** | Shown when the map gives the register a default: writes it (its tooltip says which). |
| **To all devices** | On a bus only (3.9): the value typed, to every device on the link in one broadcast frame, after a confirmation; then each device is read back and the Log says whether every one took it. Enabled only where the broadcast rule allows it (3.10); the tooltip says why not. |
| **Bits** | Shown for an integer register that has no bit fields and no scale or offset. Ticked, it draws the bit view (5.4) for that register too. Saved (`ui/quickBits`). |
| Hint, at the right | *tick Allow writes to write*, or *not connected*. |

The controls are enabled only while **Allow writes** is on **and** the link is up. The hint says which of the two is missing.

Every quick write follows exactly the same path as an edit in the table: the danger confirmation and the other checks of chapter 6. After each attempt, written or not, the named-values list and the bit view show the device's value again. The value box keeps the text you typed, so a refused value can be corrected. It is emptied only when the panel is set up anew: another register is selected, a map is loaded, or **Bits** is switched.

### 5.2 What a value may be

| Typed | Meaning | Example on the example map |
|---|---|---|
| A plain number | The value **as shown**: scale and offset are undone before writing. Use a dot for decimals, in any locale. | `22.5` on SETPOINT; `1.5` on a register with scale 0.01 writes the raw integer 150 |
| `0x…` | Hex: the raw integer, with no scale or offset undone | `0x02` on LED_MODE |
| `0b…` | Binary: the raw integer, with no scale or offset undone | `0b101` |
| A value name | Its number (names match in any case) | `blink` on LED_MODE |
| A special value's name | Its shown value | `off` on a register with `"special": { "0": "off" }` |
| Hex bytes, for a `bytes` register | Exactly `size` bytes. Spaces and other non-hex characters are ignored. | `48 45 4C 4C 4F` on a 5-byte register. The example map's only `bytes` register, MSG_BUFFER, has size 255 and needs all 255 bytes: these 5 are refused with *255 hex bytes needed, got 5*. |

Integers are rounded to the nearest whole number and checked against the type's range. The refusal names the range, for example *300 is out of range for u8 (0 … 255)*.

Two points to know:

- **Hex and binary are unsigned.** On a signed register, a negative value must be typed as a negative number: `0xFFFF` on an `i16` is out of range, while `-1` is accepted.
- **Hex on an `f32` register is a number, not a bit pattern.** `0x10` on an `f32` writes 16.0.

A value that cannot be encoded is not written. The Log shows the error *NAME: text not written: reason*, for example *FAN_SPEED: 300 not written: 300 is out of range for u8 (0 … 255)*, or *…: not a number: "abc"*.

### 5.3 Read-modify-write

The bit view writes one bit or one field at a time, but the device only knows whole registers. The Studio therefore takes the value the device holds now, changes only that field's bits, and writes the whole register. The other bits stay as the device had them.

If the register has not been read yet, a bit click writes nothing, because there is no value to change safely. For a signed register the result is written as a signed number, so the range check passes.

Between the last read and the write, the device may have changed the other bits itself. The write carries the value the Studio last saw. If the device changes a register's other bits often, poll fast, or write the whole value in the value box.

### 5.4 The bit view

The bit view draws the register as a datasheet does:

- the bit numbers, with the highest bit first
- a box for each field across the bits it takes
- a cell per bit showing 0 or 1, filled with the accent colour when set

A register wider than 16 bits takes one line per 16 bits: bits 31 – 16, then 15 – 0.

The bit view appears for every integer register with bit fields that has no scale or offset. For other integer registers it appears when **Bits** is ticked. Float and scaled registers have none.

What each part shows and does:

| Part | Shows | Click |
|---|---|---|
| Bit cell | `0` / `1`, or `·` while the value is unknown | Flips that bit only. |
| One-bit field without value names (a flag) | its name | Flips it. |
| Wider field, or one with value names | `NAME = value`, using the value's name when it has one | Opens a menu of its named values (the current one checked) and **Value…**, which asks for a number from 0 to the field's maximum. |
| A bit in no field | a box with `—` | |

The tooltip over a bit or field shows its bits (`bits 1:0`), its value names and what a click does.

Example with STATE on the example map. MODE (bits 1:0) shows `MODE = idle`. A click on it offers `idle  (0)`, `run  (1)` and `fault  (2)`. Picking *run* writes the whole register with bits 1:0 set to 1. READY and ALARM are flags: a click flips them. STATE is read-only in the example map, so this is only an illustration. A read-write register with fields, such as CONFIG, behaves the same way.

## 6. Writes and safety

### 6.1 Allow writes

**Allow writes**, on the Registers tab, is the master switch for every write made in the window:

- edits in the table
- the quick-write panel
- raw writes on the Monitor tab

It is off at every start and it is never saved. While it is on, its text is amber and bold. The login token (see 3.6) is written whatever the switch says, because it is not a register write that you ask for.

The API has switches of its own, which Allow writes does not affect (see 17.2).

### 6.2 Changed while editing

When an edit starts in the table, the editor remembers the raw value it started from. When you press Enter, the Studio compares that value with what the device holds now. If they differ, something else wrote the register meanwhile: the device itself, or another client. The Studio then asks:

> **Value changed while editing**
> SETPOINT changed while you were editing:
> when you started: 20.000
> now on the device: 25.000
> you typed: 22.5
> Something else writes this register (the device itself, or another client). Write yours anyway?

The dialog has **Cancel** (the default) and **Write anyway**. A cancel logs *SETPOINT: write of 22.5 cancelled (the value changed meanwhile)*.

A quick write always starts from the value that is current at the click, so it never raises this dialog.

### 6.3 Danger confirmation

A register marked `"danger": true` in the map (shown `rw ⚠`) asks before every write, from the table and from the quick-write panel:

> **Confirm write**
> Write **300** to **MOTOR_SPEED** (0xD086)?
> = *(the value's name, for an enum register)*
> *(the register's description, muted)*
> This register moves, powers or changes something on the device.

The dialog has **Cancel** (the default) and **Write**. A cancel logs *MOTOR_SPEED: write of 300 cancelled at the confirmation*.

Two write paths do not show this dialog:

- **The Monitor's raw write** is a raw tool. It needs only Allow writes.
- **API clients** need the separate switch *including ⚠ registers* instead (see 17.2).

### 6.4 The order of the checks

A write from the window goes through these steps in order:

1. Changed while editing? Ask (6.2).
2. Encode the text for the register's type (5.2). A value that cannot be encoded is logged as an error and not written.
3. Past the map's `min` or `max`? Ask *Outside the map's limits*, with **Write anyway** and **Cancel** (Cancel is
   the default). A special value is never outside them. The API refuses such a write instead (17.3).
4. A danger register? Ask (6.3).
5. Write with acknowledgement (`WRITE_ACK`).
6. If the device acknowledged the write, read the register back. The table shows the answer, and the next polls keep reading it.

Section 21.3 follows the same path through the code.

### 6.5 What is logged

| Event | Level | Text (example) |
|---|---|---|
| Written | Info | `written: LED_MODE = 2 (02 at 0xD085)`, and *LED_MODE written* in the status bar for 3 s |
| Refused by the device, or timed out | Error | `write refused: LED_MODE = 9 (09 at 0xD085): permission denied` |
| Not a valid value | Error | `FAN_SPEED: 300 not written: 300 is out of range for u8 (0 … 255)` |
| Cancelled | Info | `…: write of … cancelled (the value changed meanwhile)` or `… cancelled at the confirmation` |
| Monitor raw write | Info / Error | `raw write 00 64 at 0xD086: OK` or `…: refused: <reason>` |

Errors also pop up (see 11.3).

## 7. The Chart

The Chart tab has two rows of controls, the chart, and the measurements under a movable splitter. The first row is what is shown and kept (Window, Memory, RAM and what the lines need, the Y range); the second what to do (Hold, Measure, Cursors, ƒ Math), how the lines are drawn (the Display menu: Normalise, Smooth, Drawing), the info line, and Clear, Remove all.

### 7.1 The axes row

| Control | What it does |
|---|---|
| **Window** | The time the view shows. Pick a preset or type a length (see below). Default 30 s. |
| **Memory** | How much is kept, like an oscilloscope's memory depth. Pick a preset or type a length. Default 60 s. |
| **RAM** | The most memory the chart's samples take, all the lines together. 2 GB by default; presets 512 MB to 16 GB (those within three quarters of the computer's memory), or any size typed: `3000`, `3000 MB`, `3 GB`. Saved. With many fast lines the Memory holds less than asked (7.4). |
| Memory note | Beside RAM, muted: what the lines need to keep the Memory set, at the rates their samples come now: *needs 1.4 GB*. More than the RAM, in amber, with what fits: *needs 2.8 GB, keeps 22 min*. Updated twice a second while the Chart tab is shown; empty until a line has two samples. |
| **Y range** Auto / Manual | Auto follows the lines. Manual uses the **min** and **max** fields (7.5). |
| **min**, **max** | The Y range. In Auto they are grey and show what the chart does, to four digits (*4.2*, not *4.20007*), the whole part always (*17420*, not *1.742e+04*). Typing either one switches to Manual, which keeps six digits. |

- **Window presets:** 1 s, 5 s, 10 s, 30 s, 1 min, 2 min, 5 min, 10 min, 30 min, 1 h.
- **Memory presets:** 10 s, 30 s, 1 min, 2 min, 5 min, 10 min, 30 min, 1 h, 2 h.
- **Typed lengths:** a number with an optional unit: `ms`, `s` or `sec`, `m` or `min` (minutes), `h`. A bare number is seconds. A comma works as the decimal point. Examples: `45`, `2.5 s`, `500 ms`, `3 min`, `1 h`. Text that is not a length puts the field back to what is shown.

The limits:

| Setting | Shortest | Longest |
|---|---|---|
| Window, typed | 10 ms | 24 h |
| Window, with the mouse wheel | 1 ms | the memory |
| Memory | 1 s | 24 h |

A Window longer than the Memory grows the Memory to hold it. A Memory shorter than the Window shrinks the Window.

Window, Memory, Smooth and the Y mode with its range are saved at each change.

### 7.2 The actions row

| Button | What it does |
|---|---|
| **❚❚ Hold** / **▶ Live** | Hold stops the view where it is while the memory keeps filling. Live follows *now* again. It is one button of fixed size, and it is filled in the accent colour while held. |
| **Measure** | Shows the measurement table (chapter 8). Off by default, and saved. |
| **Cursors** | Cursor mode: clicks place cursors A and B (7.7). Turning it on turns Measure on, and turning Measure off turns Cursors off. Turning it off takes A and B off the chart. |
| **Clear cursors** | Removes A and B. |
| **ƒ Math** | The math lines menu (chapter 9). The button shows the count of active lines: *ƒ Math (2)*. |
| **Display** | A menu of how the lines are drawn. The button keeps its text; its tooltip says what is on now: *Normalise off · Smooth on · Hover values on · drawn by the GPU: NVIDIA Quadro T1000*. In the menu: |
| - **Normalise** | Each line scaled to its own range (7.6). |
| - **Smooth** | A small display delay so that the lines scroll without steps (7.6). On by default. |
| - **Hover values** | The box of every line's value beside the mouse over the chart. On by default. Off: only the crosshair's line and its dots (the box can cover the cursors' tags). Saved. |
| - **Drawing** | Who draws the lines: **Auto (a dedicated GPU if there is one, else the CPU)**, the default; each graphics adapter found by name (*Dedicated GPU: NVIDIA Quadro T1000*, *Internal GPU: Intel(R) UHD Graphics 630*); or **CPU**. A card draws many fast lines at the display's rate (23.7). The processor's graphics is offered but draws slower than the CPU on a large screen. Saved; the Log says which draws, and when a card fails the CPU takes over and the Log says why. A card picked (or at start) takes a moment to open, up to about a second while it wakes: the CPU draws meanwhile and the window answers; the tooltip then says *CPU, opening the GPU: …*. The info line ends with *GPU* or *CPU*. On a system without Direct3D 11 (Linux): Auto and CPU. |
| Info line | *32/64 plotted · 2 math · 60 fps · 3.2 ms · delay 12 ms · GPU*: the registers on the chart of as many as it may hold at the rate now (4.8), the math lines when there are any, frames drawn per second, the average time to draw one, and the Smooth delay (7.9). Narrow, the end is cut first: the count stays. The tooltip says what each number is. |
| **Clear** | Empties every line and the memory. The lines go on from now. |
| **Remove all** | Takes every register off the chart (every Plot is unticked). Math lines stay. |

### 7.3 Window and memory, Hold and Live

The chart keeps the last *Memory* seconds of every line, and shows *Window* seconds of them.

- **Live**, the view ends at *now* and scrolls.
- **Held**, the view stays at a fixed time while new samples keep filling the memory. The top right of the chart says *held: -12.5 s · Live to follow*: how far the view's end lies behind now.

Ways to look back:

- **Drag** the chart left or right to pan through the memory. Dragging holds the view by itself. Dragging back until the view's end is within 0.2 % of the window from now goes Live again.
- **Hold**, then drag or use the memory strip.
- **Live** (the same button) jumps back to now.

Data older than the memory is dropped in whole blocks, a little behind the memory depth, so the chart need not move its arrays at every poll. All the lines together share a budget of memory, RAM (below), and each line keeps at most 16 million samples (at 1000 polls a second about 4.4 hours).

### 7.4 The memory strip

Under the time labels, a thin strip shows the **whole memory depth**. Every line is drawn there thin and faded, each in its own range. The part the view shows is marked on it.

- While the memory fills up, the data grows in from the right. Where there is room, the strip says how much is kept so far, for example *filling: 20 s of 60 s kept* with the default memory. Lengths below 120 s are written in seconds, from 120 s in minutes with one decimal (*2.5 min*), and from 7200 s in hours (*2.0 h*).
- The samples kept have a budget for all the lines together: **RAM** on the Chart tab's first row, beside Memory, 2 GB by default (a sample takes about 23 bytes: its time and value, and its share of the min/max summaries). Pick 512 MB to 16 GB, or type any size (*3000*, *3000 MB*, *3 GB*); it is kept within 256 MB and three quarters of the computer's memory, and saved. Many fast lines can need more than the memory asked for: with 2 GB, 98 lines at 500 Hz fill it in about 32 minutes, at 4000 Hz in about 4. The oldest samples then go, an eighth of each line at a time, and the strip says so: *memory full: 4.0 min of 30.0 min kept (98 lines)*; so the time kept goes down by an eighth and fills up again (with 12 min that fit: 12, then 10.5, then 12). A line keeps at most 16 million samples whatever the RAM. RAM is what the samples take: the Studio itself needs about 150 MB more (the window, its pictures, the graphics card's buffers), so with RAM 1 GB Task Manager shows it at about 1.14 GB once the memory is full.
- Click or drag on the strip: the view centres at that time and holds.

### 7.5 Y range: Auto and Manual

**Auto** fits the lines that are in view, with an 8 % margin above and below.

The value axis labels all have the decimals their step needs: with a step of 2 they read *6, 8 … 14*, with 0.2
*0.2, 0.4 … 1.0* (not each label its own: *14.0* over *8.00*).

- A flat line gets a range of at least ±0.5, or ±5 % of its value when that is more.
- Live, the range **grows at once**, so nothing is cut off, and **shrinks gently**, with a time constant of 0.4 s, so it does not jump.
- Held, the range jumps to fit.

**Manual** keeps the min and max:

- Typed min and max are swapped if min is greater than max. Equal values become min and min + 1.
- **Ctrl + mouse wheel** zooms Y around the value under the mouse, by 1.25 per wheel notch. This switches to Manual.
- A **double-click** on the chart goes back to Auto.
- The top right shows *Y manual*.

### 7.6 Normalise and Smooth

**Normalise** scales each line into the chart by its own range in the view, so lines of different units can be compared by their shapes. The value labels become percentages (0 % – 100 %). The Y controls are disabled while Normalise is on. Normalise is not saved.

**Smooth** fixes a problem of live charts. Samples arrive in bursts: per poll, per display frame and per TCP packet. Without Smooth, the right end of each line would jump back and forth. Smooth delays the whole picture by a little more than the gap last measured between now and the newest sample, so the lines always reach the right edge and scroll without steps.

- The delay grows quickly (in 0.15 s) and shrinks slowly (in 2 s), so the scroll speed does not wobble.
- It is at most 0.5 s. With no data for more than a second, it stops adapting.
- The info line shows the delay. Turn Smooth off to see samples the moment they arrive.

### 7.7 Cursors A and B

With **Cursors** on:

1. The first click on the chart places **A**.
2. The second click places **B**.
3. Each further click moves the nearer of the two to the click.

Drag a cursor to move it. Each cursor is a dashed line with a lettered tag at the top, and the span between them is shaded. The top right shows *cursors: click / drag*.

Cursors are fixed **times**, not screen positions. In a live view they move left with the data. Once both are placed, the measurements cover A → B instead of the view.

### 7.8 Mouse and keyboard

| Where | Action | Effect |
|---|---|---|
| Chart | Drag, left button | Pan through the memory, and hold. |
| Chart | Wheel | Zoom the time by 1.25 per notch. Live, the right edge stays at now. Held, the zoom is around the time under the mouse. |
| Chart | Ctrl + wheel | Zoom Y around the mouse (switches to Manual). |
| Chart | Double-click | Y back to Auto. |
| Legend (chips overflow) | Wheel | Scroll the chips, 60 px per notch. The time zoom is left alone. |
| Legend scroll bar | Click / drag | Bring the thumb under the mouse, then drag it. |
| Legend arrows | Click | Scroll half a row that way. |
| Chart, cursor mode | Click / drag | Place or move cursor A, then B (7.7). |
| Chart | Hover | Crosshair: a dashed line at the mouse, a dot on each line that has a sample within 1/20 of the window, and a box with the clock time (`14:03:12.345`), how long ago (`-2.40 s`) and every line's value, in the short number format of 7.9. With many lines the values stand in as many columns as the plot's height needs (64 lines: two in a 700 px plot); the box stays inside the plot. The values change at the **Show values** pace, as the legend's, and at once when the mouse moves; the dots follow the lines at every frame. Each column has room for the longest name, the widest number (right-aligned) and the longest unit, so the box keeps its size and place while the digits change. |
| Memory strip | Click / drag | Centre the view there, and hold. |
| Anywhere | F1 | Help. |
| Register table | Enter / Esc / F2 | Write the edited value / cancel the edit / start an edit. |

The chart itself takes no keyboard input.

### 7.9 Legend, labels and the info line

- **Legend.** Across the top, a chip per line shows its name and latest value: `SUPPLY_V  12.1 V` for a supply of 12.05 V. With no line yet, the plot says *Tick "Plot" on any register to chart it*.
- **How often the values change.** At the pace chosen in *Show values* (13.2): 10 times a second by default, like the Registers table. A number that changes at every frame cannot be read. The lines still move at every frame.
- **Fixed places.** A chip's width comes from the line's name, its unit and room for the widest number the legend writes (`-0.000e+00`). It never depends on the value, so a changing value cannot move the chips after it. The value is right-aligned in its room, with the unit after it: only the digits change.
- **More lines than the row holds.** The chips use the row left of the state text. When they need more, a thin scroll bar appears under them, and an arrow at each end of the row marks where more chips lie. Scroll with the mouse wheel over the row (a sideways wheel too), drag the bar's thumb, click the bar to bring the thumb there, or click an arrow to move half a row. The scroll stays where you leave it while the values change; after a resize or a line removed it is kept within what the chips need. The bar sits inside the legend's row, so the plot does not move when it appears. When all chips fit, there is no bar and the wheel over the row zooms the time, as over the plot.
- **Number format of the legend and the crosshair.** Both show about four significant digits, whatever the register's own format: no decimal from 100 up, one from 10 up, two from 1 up and three below 1. Values from 100 000 up or below 0.001 are written in exponent form (`1.235e+05`). Zero is `0`.
- **Value labels.** They use 1-2-5 steps, about five of them.
- **Time labels.** They show the **clock time**, as precise as the grid step needs (`14:03:12`, `14:03:12.5`, `14:03:12.35`, `14:03:12.345`). The grid lines are fixed to clock times, so they move with the data.
- **Info line.** It starts with the registers on the chart of the limit at the rate now (*32/64 plotted*) and the math lines, then shows *fps*, the frames drawn in the last second, and *ms*, the average time to draw one frame. With Smooth on it also shows *delay*. It ends with who draws, *GPU* or *CPU* (Drawing, 7.2). It is updated twice a second, only while the Chart tab is shown. Frames that take more than about 60 % of a refresh skip one now and then, as many as needed, so the chart never takes more than about 60 % of the window's time: with very many lines the fps drops, but the rest of the window keeps answering (23.6).

Frames follow the display's refresh. On Windows the Studio waits for each refresh of the compositor. Without a compositor (a remote session, a screen that is off), and on other systems, a 16 ms timer paces the frames instead (see 20.6).

## 8. Measurements

**Measure** shows a table under the chart with one row per line: every plotted register and every active math line. The table is computed every 250 ms while the Chart tab is shown, and at once when the cursors move; while a cursor is dragged, at most every 100 ms, A, B and B − A alone (the rest at the 250 ms pace), and all of it at the place it is left. With Measure off, nothing is computed.

A line above the table says what is measured:

- *Measured between the cursors: A → B = 2.500 s*, or
- *Measured over the view: 30.000 s (Cursors: measure between two points)*, which says *(place cursor A on the chart)* or *…B…* while cursor mode is on.

### 8.1 The range

- **Both cursors placed, at different times:** the range is from the earlier to the later cursor.
- **Otherwise:** the range is the view, as drawn in the last frame, live or held.

### 8.2 The values

Let *t₀ … t₁* be the range. The samples inside it are *(tᵢ, vᵢ)* for *i = 0 … n−1*, with times rising.

| Column | Formula |
|---|---|
| **at A**, **at B** | The line's value at the cursor's time, **interpolated linearly** between the two samples around it. `—` when the cursor is not placed, or lies before the line's first or after its last sample. |
| **B − A** | at B − at A, when both exist. |
| **Min**, **Max** | The smallest and largest sample inside the range. The edges are not interpolated. |
| **Area ∫ dt** | Trapezoids between neighbouring samples: Σ ½ (vᵢ + vᵢ₋₁)(tᵢ − tᵢ₋₁), in *unit × seconds*. |
| **Area / 3600** | The same area in *unit × hours*. |
| **Mean** | Time-weighted: Area ÷ (t_last − t_first), where t_first and t_last are the first and last **samples** inside the range. |
| **RMS** | √( Σ ½ (vᵢ² + vᵢ₋₁²)(tᵢ − tᵢ₋₁) ÷ (t_last − t_first) ). |

- **One sample only:** Mean and RMS are that sample, and the area is 0.
- **No samples:** the row shows `—`.

**Time-weighted** means that a sample counts for the time it lasts. If polls come unevenly, a long gap weighs more than a short one. The mean is therefore the true average of the signal as drawn, not the average of the samples.

The area and mean cover only the time between the first and the last sample inside the range. They are not stretched to the exact cursor times.

### 8.3 Units of the area

The area's unit follows the line's unit:

| Line unit | Area ∫ dt | Area / 3600 |
|---|---|---|
| `W` | J | Wh |
| `A` | A·s | Ah |
| `mA` | mA·s | mAh |
| none | ·s | ·h |
| any other, e.g. `bar` | bar·s | bar·h |

Examples:

- **Energy.** Add the math line `P = SUPPLY_V * SUPPLY_I` with unit `W` (chapter 9). Put A and B around a test run. The P row's area shows the energy in J and in Wh.
- **Charge.** The SUPPLY_I row (unit `A`) gives the charge in A·s and in Ah.

### 8.4 Number format

Values show five significant digits from 1e6 up and below 1e-3. Between those, they show 1 to 4 decimals depending on size: 1 from 1000, 2 from 100, 3 from 1, 4 below 1. Each value carries the line's unit. The table's cells can be selected and copied.

## 9. Math lines

A math line is a formula over registers, drawn and measured like a register's own line. For example, a power: `P [W] = SUPPLY_V * SUPPLY_I`.

### 9.1 Making and managing them

**ƒ Math → New math line…** opens a dialog with three fields:

- **Name**: offered as `P` for a new line
- **Unit**: offered as `W`; it sets the measurements' area units (8.3)
- **Formula**

**Completion.** While a name is typed in the formula, a list under the box offers what it may become: the map's
numeric registers (the ones a formula can read), each with its unit and description, and the functions and
constants, each function with its parameters (`atan2(y, x)`). The best first: names that start with what is typed,
then names with a part after `_` or `.` that does (`I` finds SUPPLY_**I**), then names that contain it, in any
case. Up and Down pick one, **Enter** or **Tab** takes it, **Esc** closes the list. A register goes in as its name, a
function as `name()` with the cursor inside the brackets. Only the word at the cursor is replaced: the rest of the
formula stays. The functions and constants come from the parser's own table (`Expr::builtins`), so the list never
offers one the formula would refuse.

The formula is checked against the map as you type. The dialog shows either *OK: reads SUPPLY_V, SUPPLY_I* in green, or what is wrong in red. **OK** needs a valid formula and a name. The Log records *math line P = SUPPLY_V * SUPPLY_I*.

The **ƒ Math** menu lists every line as `P = SUPPLY_V * SUPPLY_I`. A line whose formula does not compile against the current map shows its error: `X = FOO * 2  (no register "FOO" in the map)`. Each line has a submenu:

- **Shown**: on or off. It is disabled while the formula has an error.
- **Edit…**
- **Remove**

Math lines are kept in the settings (`chart/math`), not in the map. They are saved at every change and come back at the next start. They are compiled again whenever the map changes. A line that names registers the current map does not have shows its error and is not drawn. It works again with a map that has them.

On the chart, a math line is named `ƒ P`. Math lines take colours from the end of the palette, while registers take them from its start. Up to 64 math lines are drawn. More are kept, but not drawn.

### 9.2 The grammar

```
sum     = product { ("+" | "-") product }
product = unary   { ("*" | "/") unary }
unary   = "-" unary | "+" unary | power
power   = atom [ "^" unary ]
atom    = number | name | name "(" sum { "," sum } ")" | "(" sum ")"
```

**Numbers:** `12`, `2.5`, `.5`, `1e-3`, `1E+3`. There are no hex numbers in formulas.

**Names:** start with a letter or `_` and go on with letters, digits, `_` or `.`. A name is looked up in this order:

1. followed by `(`: a function
2. `pi` in any case, or `e` in lower case only: a constant
3. otherwise: a register, matched by name in any case

Only **numeric** registers can be used, and the first register of that name is taken. A register named `e` or `pi` cannot be used in a formula. Spaces are allowed anywhere between tokens.

### 9.3 Operators and precedence

From the tightest binding to the loosest:

| Level | Operators | Notes |
|---|---|---|
| 1 | `( … )`, function calls | |
| 2 | `^` | power, right-associative: `2^3^2` = 2⁹ = 512. The right side may carry a sign: `2^-1` = 0.5. |
| 3 | unary `-`, `+` | applies to the whole power: `-2^2` = −4 |
| 4 | `*`, `/` | left to right |
| 5 | `+`, `-` | left to right |

### 9.4 Functions and constants

Function names match in any case.

| Function | Arguments | Result |
|---|---|---|
| `abs(x)` | 1 | \|x\| |
| `sqrt(x)` | 1 | √x |
| `exp(x)` | 1 | eˣ |
| `log(x)`, `ln(x)` | 1 | natural logarithm |
| `log10(x)` | 1 | base-10 logarithm |
| `sin`, `cos`, `tan` | 1 | radians |
| `asin`, `acos`, `atan` | 1 | radians |
| `atan2(y, x)` | 2 | the angle of (x, y), in radians |
| `min(a, b)`, `max(a, b)` | 2 | |
| `pow(a, b)` | 2 | aᵇ |
| `floor`, `ceil`, `round` | 1 | `round` rounds halves away from zero |
| `sign(x)` | 1 | −1, 0 or 1 |
| `clamp(x, lo, hi)` | 3 | x limited to [lo, hi]; the limits may be in either order |

| Constant | Value |
|---|---|
| `pi` (any case) | 3.14159… |
| `e` (lower case only) | 2.71828… |

### 9.5 Errors

| Message | Cause |
|---|---|
| `empty` | No formula. |
| `no register "X" in the map` | An unknown name, or a register that is not numeric (a byte array). |
| `no function "X"` | An unknown name before `(`. |
| `X takes N values` | The wrong number of arguments. |
| `a ) is missing` | An unclosed parenthesis. |
| `the expression ends too early` | For example `SUPPLY_V *`. |
| `unexpected "…"` | A character that does not fit, for example `0x10` gives `unexpected "x10"`. |
| `"1.2.3" is not a number` | A malformed number. |

At run time, a result that is not a finite number is not drawn. This covers `sqrt` of a negative value, `log(0)` and a division by zero: the line simply has no point for that poll. A formula nested deeper than 61 levels gives no points.

### 9.6 How inputs are matched

Every poll stamps all the values it read with **one** time. A math line is evaluated per poll: for each sample of the first register it names, the Studio looks for the other registers' samples with **exactly the same poll time**. Only when all are there does it compute a point, at that time. A poll that failed for one of the registers gives no point.

This keeps inputs from different moments from being mixed. Otherwise, for example, a voltage from one poll could be multiplied by a current from the next.

The registers an active math line reads are sampled for the chart even when their own Plot is not ticked. A formula that names no register at all (a constant such as `2 * pi`) compiles, but it has no poll to follow and draws nothing.

### 9.7 The chart key

Inside the chart, each line has a numeric key:

- a register's line uses `regKey(slave, address)`: the slave in bits 16 to 23, the address in bits 0 to 15
- math line *i* uses `FIRST_CHART_KEY + i` (`FIRST_CHART_KEY` = 1 << 24), clear of every register key

This keeps the two kinds apart even in a map that uses every address.

### 9.8 Examples

| Name | Unit | Formula | What it shows |
|---|---|---|---|
| P | W | `SUPPLY_V * SUPPLY_I` | Power. Its area is the energy (J, Wh). |
| R | Ω | `SUPPLY_V / SUPPLY_I` | Load resistance. No point where SUPPLY_I is 0. |
| T_F | °F | `TEMPERATURE * 9/5 + 32` | Temperature in Fahrenheit. |
| dT | °C | `TEMPERATURE - SETPOINT` | Distance from the setpoint. |
| p_psi | psi | `PRESSURE * 14.5038` | PRESSURE (already scaled to bar by the map) in psi. |
| fan | % | `clamp(FAN_SPEED, 0, 100)` | Limited to 0 – 100. |
| speed | rpm | `abs(MOTOR_SPEED)` | Speed without direction. |

## 10. The Monitor tab

The Monitor shows the frames on the link and sends single requests by hand.

### 10.1 The request row

| Control | Meaning |
|---|---|
| **Slave** | The device the request goes to: the map's slave, or on a bus the selected device's (on a bus a list of the devices by name, as in 3.9, and *Broadcast · slave 0* last). With one device it is a number that follows the sidebar's Slave, and **0 (broadcast)** is the broadcast address (3.10); leaving slave 0 restores the function chosen before. Slave 0: the function locks to WRITE (no ack), the rule of 3.10 is checked first (*!! no broadcast: ...*), every device takes it and none answers. |
| Function | **READ**, **WRITE + ack** (`WRITE_ACK`) or **WRITE (no ack)** (`WRITE`). |
| **Address** | `0xA000` (the default), or a decimal address. 0 – 0xFFFF. Named *Address* beside it. |
| **Count** / **Bytes** | Named after the function: *Count* for READ, *Bytes* for a WRITE. For READ: the byte count, 1 – 65535, in decimal or `0x…` hex (default `2`). For WRITE: the value as hex bytes, **the low byte first** (the protocol is little endian): `2C 01` writes 300 (0x012C). `2C 01`, `0x2C 0x01`, `2C,01` and `2c01` all work; anything else is refused (*!! not hex bytes*): a decimal `300` is not taken as `03 00`. The box's hint says which it wants, and switching to a WRITE empties READ's count. |
| **Send** (or Enter in either box) | Sends the request. |
| **Log frames** | Shows every frame sent and received. Off at every start. |
| **Clear** | Empties the view. |

While the view is empty it says what will show there: *Nothing yet. Send a request above, or tick Log frames to see
every frame of the polling.*

How each kind of request behaves:

- **A raw write** needs **Allow writes** on the Registers tab: *!! writes are off: tick "Allow writes" on the Registers tab*. It does not ask for danger registers. It is a raw tool.
- **A WRITE + ack** at the address of a map register is read back into the table, like any write (with one device: only when it goes to the map's slave).
- **A READ to slave 0** is refused before it is sent: nobody answers a broadcast.
- **A raw READ** does not go into the table.
- **A WRITE (no ack)** gets no answer, so nothing says it arrived: the view shows *-> 0xD085 sent: 06 (no acknowledge …)* and the Log *…: sent (no acknowledge)*, not *OK*. READ the address to see what the device holds.
- **Each result is logged** as *raw write 00 64 at 0xD086: OK*, *…: sent (no acknowledge)* or *…: refused: <reason>*.

Answers to requests sent from here are printed in the view:

```
== 0xA000 OK: 01 10  (0.4 ms)       a read: the bytes, and the time the answer took
== 0xD085 OK                        a WRITE + ack (no data; a write is not timed)
-> 0xD085 sent: 06  (no ack…)       a WRITE (no ack): sent, nothing more is known
!! 0xD200 offset out of range       refused, or timed out
!! bad address "0xG000"             input errors: also "READ needs a count", "WRITE needs hex bytes"
```

A write sent without acknowledgement is reported `OK` as soon as it is sent.

### 10.2 The frame lines

With **Log frames** on, each frame is one line:

```
14:03:12.345  TX  7B 01 AA 00 A0 04 00 4E 21 7D
14:03:12.346  RX  7B 01 AB 00 A0 04 00 01 10 01 3F B3 19 7D   READ_RESP
```

| Part | Meaning |
|---|---|
| time | Local clock time, in milliseconds, when the I/O thread handled the frame. |
| `TX` / `RX` | Sent by the Studio / received from the link. |
| bytes | The whole frame in hex, from the start byte `7B` to the end byte `7D` (layout in 18.1). |
| note (RX only) | The function name: `READ_RESP`, `WRITE_ACK_RESP` or `ERROR_RESP`. For an error answer the code is the data byte before the CRC (codes in 18.3). |
| `(not a pending request)` | A valid frame that answers nothing the Studio is waiting for (see below). |

The Monitor shows **all** traffic: polls, the keep-alive reads of `0xA000` (18.7), reads and writes from the table, the login, and every request from API clients.

A frame marked `(not a pending request)` is usually one of these:

- a late answer that came after its request timed out
- the echo of the Studio's own request on a half-duplex line (RS-485 converters often echo)
- a frame the device sends by itself

Frames with a wrong CRC or end byte are not shown. They only increase the *Bad frames* counter in the status bar.

### 10.3 Limits

At thousands of frames per second, the Monitor would slow the window down, so it keeps two limits:

- **Between two display frames:** at most 1500 lines are kept. When more come, the oldest are left out, and the view says *… N frames not shown (too many to show)*.
- **In the view:** the newest 5000 lines.

Logging frames costs some time in the I/O thread too, so leave it off for speed measurements.

## 11. Log and pop-ups

### 11.1 What is logged

Every line has a time and a level: Info, Warning (amber) or Error (red). Its colours follow a switch of the look
(Theme switch). One event is one line: a long line (a map's
path) scrolls sideways, it does not wrap in the middle of a path. A map given on the command line is loaded once at
start (one *map loaded*), not after the one chosen last.

| Event | Level |
|---|---|
| `connecting to 127.0.0.1:1210` (` (with a token)`), `opening COM7 at 115200` | Info |
| `connected: 127.0.0.1:1210` | Info |
| `connection lost: <why>` (a link that was up) | Error |
| `not connected: <why>` (an attempt that failed) | Warning |
| `device ID 0x1001, protocol revision 1` | Info |
| `the map is for device 0x1001, this is 0x2002` | Warning |
| `DEVICE_ID not read: <why>` | Error |
| `token refused: <why>` | Error |
| `the map declares no login register: the token was not sent` | Warning |
| `map loaded: <path> (16 registers)` | Info |
| `map not loaded: <path>: <why>` (also a message box) | Error |
| `NAME (0xD004): read error: <why>` | Error |
| `NAME (0xD200): not available on this device: <why>` | Warning |
| `NAME: read again` | Info |
| `3 request(s) timed out (17 in all)` | Warning |
| `1 error answer(s) from the device (4 in all)` | Warning |
| `2 bad frame(s) received (2 in all)` | Warning |
| Writes, refusals, cancels, raw writes (6.5) | Info / Error |
| `CSV recording started`, `CSV recording stopped: 1200 rows in <path>` | Info |
| `CSV recording not started: <why>` (also a message box) | Error |
| `API server started` | Info |
| `API server not started: <why>` | Error |
| `math line P = SUPPLY_V * SUPPLY_I` | Info |

### 11.2 The Log tab and the file

The tab's bar shows where the file is (*Also saved to …*) and has four controls:

- **Pop-ups**: on by default, and saved (`ui/popups`)
- **Show info**: on by default, not saved. Off, the tab shows only warnings and errors, while the file still gets everything. Info lines hidden this way do not come back when you turn it on again.
- **Open folder**: opens the folder of the log file
- **Clear**: empties the tab, not the file

The tab keeps the newest 5000 lines, and the file keeps everything.

**The file** is `logs/studio_<yyyyMMdd>.log` in the program's folder. A new file starts each day. When that folder is not writable, the file goes to the user's application data folder instead: on Windows `%LOCALAPPDATA%\teknile\EVReStudio\logs`, on Linux `~/.local/share/teknile/EVReStudio/logs`. When neither can be written, the bar says *Not saved to a file (no writable folder)*.

Each line is UTF-8:

```
2026-09-25 14:03:12.345  INFO   connected: 127.0.0.1:1210
2026-09-25 14:03:14.101  WARN   not connected: Connection refused
        … the line above 7 more time(s)
2026-09-25 14:03:30.020  ERROR  write refused: LED_MODE = 9 (09 at 0xD085): permission denied
```

### 11.3 The anti-spam rules

A link in trouble can produce the same message many times a second. These rules keep the log readable and the pop-ups rare:

1. **Repeated lines are counted.** A line with the same level and text as the line just before it is not written again. When a different line comes, one line first says `… the line above N more time(s)`. This applies to the tab and the file alike. A repeated line does not pop up again either.
2. **Read errors: one line per register per 5 s.** A register's change of state (failing, not available) is logged at most once every 5 s for that register. Coming back (*read again*) is logged when it happens.
3. **Link counters: summarised twice a second.** Timeouts, error answers and bad frames are not logged one by one. Twice a second, a counter that went up is logged as one warning with the number of new events and the total.
4. **Pop-ups are for warnings and errors only,** and only while **Pop-ups** is on. They have their own rules:
   - **The same message at most every 30 s.** Messages are compared with every digit removed, so *3 request(s) timed out (17 in all)* and *5 request(s) timed out (22 in all)* count as the same message.
   - **One pop-up at a time.** The newest replaces the shown one, and *+N more* counts the others that came while it was up. Once an error is shown, the pop-up stays red for the warnings that follow.
   - **Shown for 5 s** after the latest message, or **8 s** when it is an error.
   - **It covers nothing.** It sits in the free space right of the tab bar, as one line, shortened to fit. Hover it for the full text. **Show in Log** opens the Log tab and closes the pop-up, and a later resize does not bring it back. When that space is narrower than 260 pixels, the status bar shows the message instead, for the same time.
   - **It never blocks.** No pop-up waits for a click.

### 11.4 The Log tab's counter

While another tab is shown, the Log tab's title counts the warnings and errors logged since you last looked: **Log (3)**. Opening the tab sets the count back to zero. Info lines and repeated lines are not counted.

## 12. CSV recording

### 12.1 Starting and stopping

**● Record CSV**, in the *Polling & recording* card, asks for a file. The suggested name is `evre_<yyyyMMdd_HHmmss>.csv` in your home folder. An existing file is overwritten.

- While recording, the button reads **■ Stop recording**. The card shows *Recording 15 columns · 1234 rows* and the file's name.
- **■ Stop recording** closes the file. The card then says *Saved 1234 rows to <path>*.
- `--record file.csv` starts a recording at start-up (chapter 14).
- A file that cannot be opened shows *Cannot record* with the reason, and logs an error.

### 12.2 Which columns

The columns are fixed when the recording starts:

1. `time_s`
2. `datetime`
3. every register ticked **Log** that is polled, in the table's order (by address)

"Polled" leaves out two kinds of register: those shown *not available*, and byte arrays longer than 32 bytes. All registers are ticked Log when a map loads. **Log all** and **Log none** in the right-click menu tick or untick all of them.

A register's column title is `NAME [unit]`, or `NAME` when it has no unit. A comma in a title becomes a semicolon, so the columns stay intact.

### 12.3 The format

- **Separator:** comma. The decimal point is always a dot.
- **Encoding:** UTF-8, without a byte-order mark. Units such as `°C` are written as they are in the map.
- **Line endings:** the platform's (CRLF on Windows, LF on Linux).
- **`time_s`:** seconds on the Studio's own clock, with 6 decimals. It is the same time base as the chart, and it starts when the program starts, not when the recording starts.
- **`datetime`:** local clock time in ISO 8601 with milliseconds, for example `2026-09-25T14:03:12.345`.
- **A numeric register:** the value **as shown**, with scale and offset applied, to 9 significant digits. Integers are written in decimal, even when the table shows them in hex.
- **A byte array (up to 32 bytes):** hex digits without spaces.
- **A register with no good value yet:** an empty cell.

Example, with the example map and the fake device:

```
time_s,datetime,DEVICE_ID,STATUS,CONFIG,MSG_CNT,UPTIME [ms],SUPPLY_V [V],SUPPLY_I [A],TEMPERATURE [°C],STATE,COUNTER,PRESSURE [bar],SETPOINT [°C],FAN_SPEED [%],LED_MODE,MOTOR_SPEED [rpm]
12.503214,2026-09-25T14:03:12.345,4097,16129,0,0,12483,13.2207594,7.46382809,11.0371876,0,0,0.42,0,0,2,0
12.603190,2026-09-25T14:03:12.445,4097,16129,0,0,12583,13.3011427,7.39512062,11.0955114,0,0,0.43,0,0,2,0
```

### 12.4 Timing

- **One row per completed poll.** A poll is complete when every block read of it has been answered, or has failed.
  With auto send (13.8) a row is written per frame instead. On a bus, an offline device (3.9) leaves its cells empty,
  and its lines get no chart points.
- **Rows are written in the I/O thread.** The window's drawing never delays a row, and CSV gets every poll.
- **The file is flushed every 250 ms** and when the recording stops.
- **A failed read keeps the last good value.** When a read times out, that poll's row still carries the register's last good value. A row therefore shows what the Studio knew at the end of the poll, not proof of a fresh read. To find gaps, compare `time_s` with the interval, and check the Log for timeouts.

## 13. Polling performance and tuning

### 13.1 What a poll is

A **poll** reads every polled register of the map once. Registers are not asked one by one: they are merged into a few **block reads**, one READ request each. A poll is done when all its blocks have answered.

The sidebar shows the result, for example *10.0 polls/s, 15 registers in 3 reads*, beside the interval asked for. The status bar shows the link's numbers only (latency, traffic, errors), not the rate a second time.

### 13.2 The interval

**Poll** (ticked at every start; the tick is never saved) and **every** *interval*, in the *Polling & recording* card, set the polling:

| Setting | Behaviour |
|---|---|
| Interval 0.05 – 60000 ms (default 100) | Polls start on a clock at that interval. The field has two decimals; anything below 0.05 ms is run at 0.05 ms. |
| **max** (0) | Polls run back to back. The next one starts as soon as a slot is free. |
| **Poll** unticked | No polling. The keep-alive (18.7) still runs, and values turn grey. |

You can type the interval, or use the arrows or the mouse wheel: they step by a tenth (10 → 9, 1 → 0.9 → … 0.1 → 0.09). A change applies at once. 0.25 ms is 4000 polls/s.

**Show values**, in the same card, sets how often the numbers on screen change: the Value and Decoded columns of the Registers table (and what the quick-write panel and the bit view show from them), and the values in the chart's legend. The choices are 2, 5, 10 (default) and 30 per second, and *every frame*. It does not change the polling, the samples, the CSV file or the API: the chart's lines still get every sample and move at every frame. The choice is saved (`ui/valueRate`) and applies at once.

Two design choices make these rates possible:

- **A separate I/O thread.** Polls run on their own thread with their own clock. Qt's timers tick only every 15.6 ms on Windows outside the window's thread. The Studio uses its own high-resolution waiting instead (a high-resolution waitable timer on Windows 10 1803 and later, precise timed waits on Linux), so a 1 ms interval holds on both, and so does 0.25 ms (4000 polls/s when the device and In flight keep up):
  a wake-up late by several periods counts all of them (20.5).
- **Limited catch-up.** When the Studio falls behind (a stall), it catches up at most 20 ms of polls (8 polls at least). After that it starts again from now, instead of firing a burst.

Chapter 20 explains both in detail.

### 13.3 The block merge rules

The pollable registers are sorted by address. Each one joins the block before it when both conditions hold:

1. It is in the **same 256-address bank**: the same high byte of the address, for example `0xD0xx`.
2. At most **8 bytes** lie between the end of the block and the register.

Otherwise it starts a new block.

**Byte arrays longer than 32 bytes are not polled** (a message buffer, for example). Read them with right-click → **Read now**. They are also left out of CSV.

The example map makes three blocks:

| Block | Registers | Bytes |
|---|---|---|
| `0xA000` | DEVICE_ID, STATUS, CONFIG, MSG_CNT (MSG_BUFFER, 255 bytes, is not polled) | 7 |
| `0xD000` | UPTIME, SUPPLY_V, SUPPLY_I, TEMPERATURE, STATE, COUNTER, PRESSURE | 22 |
| `0xD080` | SETPOINT, FAN_SPEED, LED_MODE, MOTOR_SPEED | 8 |

The second and third blocks share a bank, but 106 bytes lie between them, so they stay two reads.

**Why a block never crosses a bank.** Devices often keep separate banks in separate tables, and some refuse a read across a bank boundary.

**Why the gap is limited to 8 bytes.** A small gap costs a few extra bytes on the wire but saves a whole request. A large one would read a lot of unused memory, and is more likely to cover addresses the device refuses.

The same rules merge the reads of the API's `get` and `stream` (chapter 17) and of `evre_probe`.

### 13.4 When the device refuses a block

When a merged block gets an error answer, the device probably has a hole in it. The block is replaced by one read per register. Then:

- a single register refused with code 3, 4 or 5 is marked *not available* and no longer polled
- other errors and timeouts are tried again at the next poll

A new block layout ignores the answers still due to the old one. At the next connect, every register is tried again.

### 13.5 Why N reads per poll, and In flight

The number of reads comes from the map's layout, not from a setting. Each read costs at least one **latency**, the time for an answer to come back (see the status bar, *Latency*):

- well under 1 ms over USB or to a device on the same computer
- often 1 – 5 ms over Wi-Fi

**In flight** sets how many requests may wait for their answers at once:

- **In flight 1:** the reads of a poll go one after the other. A poll of 3 reads costs 3 latencies.
- **In flight ≥ reads per poll:** all the reads of one poll go at once. A poll costs about one latency.
- **In flight ≥ 2 × reads per poll:** polls **overlap**. The next poll's reads leave while the answers of the last one are still coming.

The number of polls under way at once is In flight ÷ reads, rounded down, between 1 and 8. The upper bound on the rate is therefore:

  **polls/s ≈ (polls under way) ÷ latency**

With 3 reads and In flight 4, one poll runs at a time: at 5 ms latency that is about 200 polls/s. In flight 12 lets 4 polls run at once: up to 800 polls/s at 5 ms. A UART takes one request at a time, so keep In flight at 1 for serial devices.

Answers come in the order they were sent, so overlapping polls also complete in order. Each poll's samples and CSV row stay in sequence.

### 13.6 The "slower than asked" hint

When the rate is below 90 % of the interval's rate (or the interval is *max*), and the rate is close to the limit that latency and In flight set, the status bar says why, in amber, at the right of the link's numbers. The hint needs the rate to be above 60 % of that limit, because only then is the limit what holds it back. The limit: with In flight at least the reads of a poll, a poll's reads go together, so a poll costs one answer time and In flight / reads polls overlap (at most 8): overlap / latency. With In flight below the reads of a poll, they go In flight at a time, so a poll costs reads / In flight (rounded up) answer times: one over that.

It comes and goes with each refresh, and moves nothing: its place in the status bar is always there (empty when the rate is fine), in plain text as tall as the others, and the numbers before it sit in labels as wide as their widest text. When the window is too narrow for the whole hint it ends in "…" and its tooltip has it all. The polling card shows the rate alone (with the hint in it, the card and the sidebar's scroll bar jumped), and the sidebar's cards keep one width whether its scroll bar shows or not. It takes one of these forms:

- *Slower than asked: a poll is 36 reads, sent 1 at a time; 0.6 ms per answer. Set In flight to 64 for more.* In flight is below the reads of a poll (twelve devices of three reads each, In flight 1: 36 answer times a poll, about 45 polls/s at 0.6 ms).
- *Slower than asked: a poll is 3 reads; In flight 4 runs 1 poll(s) at once; 5.0 ms per answer. Set In flight to 15 for more.* The time is per answer (a poll's reads go out together), not per poll. The suggestion is the number of polls needed at once (polls/s asked × latency, rounded up, at least one more than now; 8 for *max*) times the reads per poll, never past the engine's 8 polls at once (`IoEngine::MAX_POLLS_UNDER_WAY`: 24 for 3 reads; beyond it more In flight runs no more polls, and the hint then adds *(8 polls at once: the most)*), and at most 1024.
- *Slower than asked: 5.0 ms per answer is the link's limit.* In flight is already high enough, or 8 polls already overlap. The link or the device is the limit: use a longer interval, or fewer registers.
- *Slower than asked: a poll is 36 reads, 1 at a time on the serial link, 0.6 ms per answer: 21.6 ms per poll.* A serial link answers one request at a time: nothing to raise; a longer interval, fewer registers or devices, or a faster baud rate.

When there is no hint and the rate is still low, something other than latency is the limit, for example the device's processing time.

### 13.7 What to expect

Against the fast test device (`evre_fake_fast`) on the same computer, the Studio reaches about **4000 polls/s** back to back. Over a real link the rule of 13.5 sets the limit.

Your numbers depend on the device, the link and the map's layout. The sidebar shows what you get. Other things that cost time:

- the Monitor's *Log frames*
- a large number of plotted lines at a long window on a high-resolution screen (the chart's info line shows the time per frame)
- API clients, whose requests share the same queue

Keeping all the device I/O on its own thread means the window never slows the polls. The window only copies what the I/O thread has: the samples once per display frame, the table's values at the *Show values* pace.

### 13.8 Auto send

A device whose STATUS has `CAP_AUTO_SEND` (bit 11) can send its read-only block by itself: with CONFIG's `AUTO_SEND` (bit 3) set, it sends a READ_RESP of the block (offset `0xD000`, count the size of its read-only registers there) at 8000 / (prescaler + 1) Hz, the prescaler in CONFIG's bits 15..8, without being asked. No request, no answer time: the values come as fast as the device sends them, even over a link with a long latency.

**Auto send** and its rate, in the *Polling & recording* card under the poll rate:

| Part | Behaviour |
|---|---|
| The box | Offered only with one device on the link (not on a bus: devices sending by themselves would collide on a shared link), once connected and STATUS (read at connect) has `CAP_AUTO_SEND`. Otherwise disabled; its tooltip says why. Never saved: it changes the device, so it is off at every start. |
| The rate | The 16 rates the device makes exactly, 8000 Hz divided by 2, 4, 5, 8, 10, 16, 20, 25, 32, 40, 50, 80, 100, 125, 160, 200 (prescaler + 1): 4000, 2000, 1600, 1000, 800, 500, 400, 320, 250, 200, 160, 100, 80, 64, 50, 40 Hz. The prescaler is the device timer's reload and a reload of 0 stops a timer, so it is never 0 (a device replaces a 0: by 1, or by its default 0x4F); the protocol's rates are 4000 Hz to 40 Hz. Default 100 Hz, saved (`poll/autoSendHz`). A change while on is written to the device at once. |

What the Studio does:

- **On:** CONFIG is read, then written with WRITE_ACK as (CONFIG & `0x0004`) | `0x0008` | (prescaler << 8): MSG_ENABLE kept as read, AUTO_SEND set, SYS_RESET and DFU always 0 (HEARTBEAT is the device's). At 100 Hz the prescaler is 79: `0x4F08`. The Log says *auto send on: the device sends its read-only block 100 times a second*.
- **The frames:** each READ_RESP from the device's slave at `0xD000` that answers no request fills the registers wholly inside it, and is one sample tick: a point on the chart per plotted register, one CSV row. The answer to a read is matched by slave, function, offset **and** count (18.6), so a frame is never taken for the answer to a read of part of the block (the Monitor's READ of `0xD000`, 4 bytes, still gets its 4 bytes).
- **The polls** read everything else (CONFIG, the writable registers, the messages): the registers the last frame covered leave the blocks (13.3); until the first frame, everything is polled, and the polls give the chart points and the CSV rows, so a device that
never sends a frame still gives samples. From the first frame on, a poll gives no chart point and no CSV row: the frames do. Poll and its interval work as before, for the rest.
- **The device's host watchdog:** while on, CONFIG is read every 100 ms whatever the polling is (Poll off, or nothing left to poll), so a device that stops its actuators after 2 s without a request never does. A read that finds AUTO_SEND cleared (the device reset, or another client wrote CONFIG) is logged once, *the device stopped auto send (reset?)*; the box unticks and the Studio does not switch it on again by itself.
- **A serial link:** the frames may take at most 70% of the link (rate x (frame data + 10 bytes) <= 0.7 x baud / 10), so the polls and the heartbeat still get through. A faster rate asked for is cut to the fastest rate of the list that fits, and the Log says so: *auto send at 250 Hz, not 4000 Hz: 4000 frames of 32 bytes a second would take more than 70% of the serial link at 115200 baud*. TCP takes the rate asked for.
- **Off:** unticking it, **Disconnect** and closing the window clear AUTO_SEND: (CONFIG & `0x0004`) | (prescaler << 8). On Disconnect and close the WRITE goes straight on the link and is flushed before the link closes; on TCP, what the device still sends is then read until it is quiet (at most 100 ms, `Link::drain`): closed with bytes unread, the connection was reset rather than ended, and the device dropped the off it had not read yet and went on sending (a GUI check failed now and then, 2026-10-05). It goes whenever the device may still be sending: from the WRITE that switched it on until the device acknowledged an off, so a Disconnect right after unticking (the off still under way) clears it too. A lost link cannot send it; the device's own watchdog is then the safety. Disconnect also unticks the box.
- **No frames:** when not one frame has come 2 s after the device took AUTO_SEND, something between the Studio and the device does not pass them on (a gateway that answers requests from its own copy of the device and streams nothing). The Studio switches it off again (on the device too), unticks the box, says so in the Log (*no frame came in 2 s ...*), and the registers are polled as before.
- **Not offered:** the box is greyed and its rate list says why in a word or two in place of a rate: *not connected*, *not offered* (the device's STATUS has no CAP_AUTO_SEND) or *not on a bus*. The box's tooltip has the whole reason; the rate chosen is kept and comes back with the offer.
- **A lost link** keeps it: after the reconnect (Reconnect by itself), once STATUS says the device can, it is switched on again.
- **A bus:** loading one switches it off first; a broadcast that sets AUTO_SEND is refused (3.10).

The sidebar's rate line while on: *Auto send 99.8/s · 10.0 polls/s* (the frames a second, then the polls of the rest; past 100 a second without decimals, so it stays one line at 8000 and 4000 in Linux's wider fonts too). The "slower than asked" hint (13.6) stays empty meanwhile: the polls then read only what the frames leave.

## 14. Command line, environment variables, settings

### 14.1 Command-line options

```
EVReStudio [--tcp host:port | --serial COMx[:baud]] [--map file.json | --bus bus.json]
           [--plot NAME,NAME] [--tab registers|chart|monitor|map] [--connect]
           [--interval ms] [--inflight n] [--record file.csv]
           [--api] [--api-writes] [--api-writes-danger]
```

| Option | Effect |
|---|---|
| `--tcp host:port` | Chooses TCP and fills host and port. The port follows the last `:`. With no `:` only the host is set. |
| `--serial COMx[:baud]` | Chooses Serial / USB and that port (on Linux for example `/dev/ttyACM0`), added to the list if it is not there. Optionally sets the baud rate. |
| `--map file.json` | Loads this map **for this run only**. The next plain start opens the map chosen last. |
| `--bus bus.json` | Loads this bus (3.9) **for this run only**, in place of a map. |
| `--plot NAME,NAME` | Ticks Plot on these registers (names in any case). One the map marks fixed (`"plot": false`), or past what the chart holds at the poll rate (4.8), is left off, and the Log says which. |
| `--tab registers\|chart\|monitor\|map` | The tab shown at start (`map`: the Map editor). Any other value is ignored. |
| `--connect` | Connects at start, as if you clicked Connect. |
| `--interval ms` | The poll interval. `0` is *max*, `0.25` is 4000 polls/s. |
| `--inflight n` | In flight, for the link type chosen, as if you typed it: it is saved like a typed value. |
| `--record file.csv` | Starts a CSV recording at once. Rows come as polls complete. |
| `--api` | Ticks Serve API. |
| `--api-writes` | Ticks Allow API writes. Implies `--api`. |
| `--api-writes-danger` | Also ticks including ⚠ registers. Implies both of the above. |
| `--help`, `--version` | Prints the help or the version, and ends. |

A bad option ends the program with a message. Options are applied in this order, after the window opens:

1. map
2. link
3. token (from the environment)
4. plot
5. tab
6. interval
7. In flight
8. recording
9. API switches
10. connect

The link fields, the interval and the other sidebar values set by options are saved on close like typed values. An option therefore also changes what the next plain start uses.

### 14.2 Environment variables

| Variable | Effect |
|---|---|
| `EVRE_TOKEN` | Fills the token box at start (see 3.6). This is the only way to pass a token without typing it: a command line is visible to other users of the computer. |
| `EVRE_SHOT` | A test aid that saves a picture of the window and quits (see 26.6). While it is set, the Studio uses separate settings (`EVReStudio-test`), so yours are not touched. |

### 14.3 Settings

The Studio saves its settings with Qt's `QSettings`, under the organisation `teknile` and the application `EVReStudio`:

- **Windows:** the registry, `HKEY_CURRENT_USER\Software\teknile\EVReStudio`
- **Linux:** `~/.config/teknile/EVReStudio.conf`

| Key | Default | Saved | Meaning |
|---|---|---|---|
| `link/tcp` | `true` | on close | TCP (true) or Serial / USB. |
| `link/host` | `127.0.0.1` | on close | TCP host. |
| `link/port` | `1210` | on close | TCP port. |
| `link/serial` | empty | on close | Serial port name. |
| `link/baud` | `115200` | on close | Baud rate. |
| `link/timeout` | `500` | on close | Answer timeout, ms. |
| `link/reconnect` | `true` | on close | Reconnect by itself. |
| `link/inflightTcp` | `4` | on change | In flight for TCP. |
| `link/inflightSerial` | `1` | on change | In flight for serial. |
| `poll/interval` | `100` | on close | Poll interval, ms (0 = max). |
| `poll/autoSendHz` | `100` | on change | Auto send's rate, Hz (13.8): one of 8000 / (prescaler + 1) for the 16 prescalers offered (4000 Hz to 40 Hz); another number, an 8000 saved by an older version too, takes the nearest. |
| `api/on` | `false` | on close | Serve API. |
| `api/network` | `false` | on close | Network (not only this computer). |
| `api/evrePort` | `1219` | never (read only) | The EVRe pass-through port. Change it by editing the settings. |
| `api/jsonPort` | `1220` | never (read only) | The JSON port. Change it by editing the settings. |
| `map/bus` | empty | on close | The bus file to open at start (3.9), in place of `map/file`: the bus last opened or saved. Empty: none (a map opened, made, or the bus closed). |
| `map/file` | empty | on close | The map to open at start (an absolute path): the map last loaded or saved. A load or a save only notes it; the key is written when the window closes normally, so a program that ends otherwise keeps the old value. |
| `ui/dark` | `true` | on close | Dark theme. |
| `ui/geometry` | none | on close | The window's size and place. |
| `ui/decodedColumn` | `false` | on change | Decoded column shown. |
| `ui/quickBits` | `false` | on change | Quick write: Bits ticked. |
| `ui/popups` | `true` | on change | Log: Pop-ups. |
| `ui/valueRate` | `10` | on change | Show values: values per second on screen (2, 5, 10, 30; 0 = every frame). Another number reads as 10. |
| `chart/window` | `30` | on change | Window, seconds. |
| `chart/memory` | `60` | on change | Memory, seconds. |
| `chart/drawing` | `0` | on change | Drawing: 0 Auto, 1 a dedicated card, 2 the processor's graphics, 3 the CPU (`ChartView::Drawing`). |
| `chart/ramMB` | `2048` | on change | RAM: the most memory the chart's samples take, all the lines together, MB (7.4); kept within 256 and three quarters of the computer's memory. |
| `chart/smooth` | `true` | on change | Smooth. |
| `chart/hoverValues` | `true` | on change | Hover values: the crosshair's box. |
| `chart/yAuto` | `true` | on change | Y range Auto. |
| `chart/yMin`, `chart/yMax` | `0`, `1` | on change (Manual) | The Manual Y range. |
| `chart/measure` | `false` | on change | Measure shown. |
| `chart/math` | empty | on change | Math lines: one text per line, `name⇥unit⇥formula⇥1\|0`. The last field means shown, and a line without it counts as shown. |

**Never saved, by design:**

- the token
- Allow writes
- the API write switches (Allow API writes, including ⚠ registers)
- the Plot and Log ticks
- the **Poll** tick: polling is on at every start (only `poll/interval` is saved)
- the **Auto send** tick: it changes the device, so it is off at every start (only `poll/autoSendHz` is saved)
- the groups ticked
- Normalise
- the Monitor's Log frames
- Show info

The slave address is part of the map, not of the settings.

The map opened at start is the one saved in `map/file` if that file still exists. Otherwise it is the first `*.json` (in name order) in the `maps/` folder next to the program. When there is none, the Studio starts a new map: *New device*, with DEVICE_ID, STATUS and CONFIG.

### 14.4 Help and the theme

The foot of the sidebar holds two buttons and the version line (*v1.0.0 · teknile*):

- **Help** (tooltip *Help (F1)*) opens the built-in help, a short form of Part I, in a window of its own that does not block the main window. **F1** anywhere does the same. Its pages: Getting started, Connecting, Polling & speed, Registers & writes, Chart & recording, Device maps, Map editor, API (MATLAB, LabVIEW, Python), Monitor, Log & pop-ups, Command line, Keys & mouse.
- The **theme button** switches between the dark and the light theme at once: the window, the table's glow colour and the chart follow without a restart. Its text names the other theme: *☀  Light theme* while the dark one is shown, *☾  Dark theme* while the light one is. The Studio starts dark; the theme in use is saved on close (`ui/dark`).

## 15. Troubleshooting and FAQ

### 15.1 The pill turns red: *Connection refused*

Nothing listens at that host and port. Check the address and port, and that the server or device is running. For the fake device: `python3 tests/fake_device.py`, which listens on `127.0.0.1:1210`.

A host that does not answer at all takes the operating system's connect timeout before the pill turns red. Possible causes: a wrong IP address, a computer on another network, or a firewall.

### 15.2 It connects, then drops after a moment

- **A token is needed.** A gateway that expects a token may drop a client that does not send one. Check the Log:
  - *the map declares no login register: the token was not sent*: add `"login"` to the map (16.2)
  - *token refused: …*: the token or the login register is wrong
  - no token line at all: the token box is empty (or `EVRE_TOKEN` is not set)
- **The server drops silent clients.** Some servers drop a client that is silent for about a second. The Studio's keep-alive reads DEVICE_ID every 400 ms while nothing else is sent, which prevents this, provided the device answers reads of `0xA000`.

### 15.3 Every value shows *error* or stays grey, and the Log reports timeouts

The device does not answer. The usual causes:

- the wrong **Slave** address (it comes from the map's `"slave"`)
- the wrong **baud rate** on a real UART
- a device that is not running EVRe on that port
- a **Timeout** too short for the link (radio links, slow gateways)

Raise the timeout and reconnect: Slave and Timeout apply at the next connect. The Monitor, with *Log frames* on, shows whether anything comes back at all.

### 15.4 A register shows *not available*

The device refused its address for good: code 3 (permission denied), 4 (offset out of range) or 5 (count out of range). The map lists a register that this device, or this firmware version, or this link, does not have. It is not an error of the Studio, and polling goes on without that register. The Log records it once as a warning. The Studio tries again at the next connect.

### 15.5 The serial port cannot be opened (*Access denied*, *Permission denied*, busy)

A serial port can be open in only one program at a time. Close any terminal program, other tool or second Studio that holds the port.

On Linux, your user also needs permission for the device file. On many distributions this means being a member of the `dialout` group. Log out and in again after adding the group.

A port that disappears while connected (a USB device that is unplugged or resets) closes the link. With *Reconnect by itself* on, the Studio tries again every 2 s until the port is back.

### 15.6 The device resets when the Studio connects over serial, or it answers only after connecting twice

The Studio sets **DTR** after opening a serial port, because many USB CDC devices send nothing until DTR is set. Some boards reset on a DTR change. Such a board restarts at every connect and may miss the first requests: the device ID read may then time out. Wait for the device to boot and reconnect, or change the board's DTR wiring or firmware behaviour.

### 15.7 The poll rate is lower than the interval asks for

Read the amber hint under the poll rate (13.6):

- It suggests an In flight value: set it, if the link allows pipelining (TCP usually does, a UART does not).
- It says the link takes *n* ms per answer: the link or the device is the limit. Use a longer interval, or fewer registers in the map.

With no hint, look at the status bar's latency and the number of blocks. Reorganising the map so that registers sit close together in one bank makes fewer reads per poll.

### 15.8 The chart is slow or stutters

Look at the info line: fps, and the time per frame.

- Fewer lines, a shorter window or a lower screen scaling reduce the drawing time.
- Long windows are drawn from min/max summaries, so their cost follows the pixels, not the number of samples.
- In a remote desktop session there may be no compositor to pace frames. The Studio then uses a 16 ms timer, which can show small hitches.
- Stepped scrolling usually means **Smooth** is off.
- Values that change only every few hundred milliseconds are usually a long poll interval. Check the poll rate.

### 15.9 A write is refused

| Log or panel says | Meaning |
|---|---|
| The value cannot be edited at all | *Allow writes* is off, the register is `ro` in the map, or the link is down (quick write: *not connected*). |
| `write refused: …: permission denied` | The device refused the write (code 3). The register may be read-only on the device, locked in its current state, or protected until a login. |
| `write refused: …: offset out of range` / `count out of range` | The device does not have that address or size. The map may not fit the device. |
| `write refused: …: timeout (500 ms)` | No acknowledgement came. The write may or may not have happened. The read-back did not run, but the next poll shows the value. |
| `… not written: 300 is out of range for u8 (0 … 255)` | The value does not fit the register's type. Nothing was sent. |
| `… not written: not a number: "…"` | The text is not a number, a `0x`/`0b` value or a value name. |
| `… not written: 4 hex bytes needed, got 2` | A byte array must be written with exactly its size. |

### 15.10 The table shows another value than the one I wrote

The table shows what the device reports after the write, never the typed text (1.4). A device may clamp, round or reject part of a value: a setpoint limited to a range, a mode that is not allowed in the current state, a float stored with less precision. The read-back shows what the device actually holds.

### 15.11 *Value changed while editing* appears when I write

The register changed on the device between the start of your edit and Enter. The device may update it itself (a counter, a state machine), or another client wrote it (an API client, another tool). The dialog shows both values. *Write anyway* sends yours, and *Cancel* keeps the device's. Registers the device changes by itself are better written through the quick-write panel.

### 15.12 The map does not load

A message box and the Log give the reason:

- `JSON error at <offset>: <message>`: the file is not valid JSON (a trailing comma, a comment, a missing quote)
- `register "NAME": bad address`
- `register NAME: unknown type "x"`
- `login: bad address` / `login: bad size`

The map that was loaded before stays. See 16.10.

### 15.13 *the map is for device 0x1001, this is 0x2002*

The map's `device_id` does not match the device's DEVICE_ID. You may be connected to another device than you think, or using the wrong map. The Studio keeps polling. Remove `device_id` from the map (or set it to 0) to turn off the check.

### 15.14 An API client gets *API writes are off* or cannot connect

- *API writes are off in EVRe Studio (tick "Allow API writes")*: tick it in the sidebar, or start with `--api-writes`. Danger registers also need *including ⚠ registers*. Both switches are off at every start.
- A client on another computer cannot connect: tick **Network (not only this PC)**, and allow ports 1219 / 1220 through the firewall.
- *EVRe Studio is not connected to a device*: the API is running, but the Studio has no link.

### 15.15 A math line draws nothing

- The ƒ Math menu shows the formula's error.
- A line whose registers are fine still draws nothing when every poll of those registers fails, or when the result is not a finite number (a division by zero, the log of 0).
- A formula without any register (a constant) never draws.

### 15.16 The Monitor shows frames marked *(not a pending request)*

These are late answers after a timeout, echoes of the Studio's own requests on a half-duplex RS-485 line, or frames the device sends by itself (with **Auto send** on, its read-only block at `0xD000`, which the Studio takes into the table: 13.8). They do no harm. Many late answers mean the Timeout is too short for the link.

# Part II. Reference

## 16. The device map format "evre-map/1"

A device map is a JSON file (UTF-8) that describes one kind of device: how to reach it, and its registers. The Studio opens maps with **Device map → Open…** and saves them with **Save** / **Save as…**. **New** starts an empty one. Maps are made and changed on the Map editor tab (Part IV). The format's contract for every tool, not only the Studio, is [MAP_FORMAT.md](MAP_FORMAT.md); `docs/evre-map-1.schema.json` says the same for tools that check JSON (32.5).

### 16.1 A minimal map

```json
{
  "format": "evre-map/1",
  "device": "Minimal device",
  "registers": [
    { "addr": "0xD000", "name": "UPTIME", "type": "u32", "unit": "ms" }
  ]
}
```

Every top-level key has a default. Even `format` and `device` may be left out, and a map without `registers` loads as a map with no registers, with no error. Inside a register only `addr` is required (16.3).

### 16.2 Top-level keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `format` | string | `"evre-map/1"` | The format name. It is not checked, and it is written back as read. |
| `device` | string | `""` | The device's name: shown in the Device map card and the port list, and reported by the API (`info`). *Save as* sets it to the file's name when it is empty. |
| `device_id` | number or string | `0` (not checked) | The expected DEVICE_ID (`0xA000`). A mismatch at connect logs a warning (3.7). Written as `"0x1001"` or `4097`. |
| `desc` | string | `""` | A line on the map, in the export. |
| `notes` | string | `""` | Longer text on the map (Markdown), in the export. |
| `slave` | number | `1` | The slave address, loaded into the Slave box. Saving the map writes the box's value back. |
| `usb` | object | none | `{ "vid": "0x1234", "pid": "0xABCD" }`: the device's USB vendor and product IDs, in hex or decimal. The serial port list names the port with these IDs after the map's device, and picks it when no port was selected (3.2). |
| `login` | object | none | `{ "addr": "0xF000", "size": 16 }`: the register the token is written to after a TCP connect (3.6). `addr` must be 1 – 0xFFFF (a JSON number is not range-checked, see 16.10). `size` is 1 – 65535 bytes, 16 when left out. Without `login`, no token is sent. |
| `protocol` | object | none | `{ "transport": "serial", "baud": 115200, "tcp_port": 1210, "timeout_ms": 200, "notes": "…" }`: how the device is reached, for the reader of the map and its export. Every key is optional. The Studio connects as the sidebar says. |
| `groups` | object | none | `{ "Power": { "notes": "…" } }`: notes on a group, by its name. |
| `extends` | string | none | An overlay: the map this one changes, relative to this file (chapter 33). |
| `registers` | array | empty | The registers (16.3). |

Unknown keys are kept: a save writes them back as they were (16.9).

### 16.3 Register keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `addr` | string or number | required | The address, 0 – 0xFFFF: `"0xD004"` (hex, any case), `"53252"` or `53252`. Only a text address is range-checked; a JSON number is cut to 16 bits without a message (16.10). |
| `name` | string | the address, e.g. `"0xD004"` | The name used in the table, the chart, math lines, CSV and the API. Keep it unique. The API and math lines find registers by name, in any case, and take the first match. |
| `type` | string | `"u16"` | 16.4. Also accepts the C names `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `float`, in any case. |
| `size` | number | `1` | Byte count, for `bytes` only. Other types have a fixed size. |
| `unit` | string | `""` | Shown beside the value, and used in CSV titles and chart legends. `W`, `A` and `mA` set special area units in the measurements (8.3). The unit `bitmask` shows an integer in hex. |
| `access` | string | `"ro"` | `"ro"`, `"rw"`, or `"wo"`: write-only, **never read, so never polled** (a key, a command). Any other text containing `w` means read-write. |
| `write` | string | normal | `"action"`: a write does something, then the register reads back idle; `"w1c"`: a 1 written to a bit clears it, a 0 leaves it. For the reader and the export; the Studio writes such registers as any other. |
| `persist` | boolean | `false` | Kept across a reset (non-volatile); `default` is then the factory value. |
| `plot` | boolean | `true` (numbers) | `false`: a fixed value, an ID or a command, not worth a line: no Plot box in the table, left out by *Plot shown* and `--plot`. Math lines may still read it. |
| `notes` | string | `""` | Longer text (Markdown), shown in the Map editor and in the export. |
| `group` | string | `"Registers"` | For the groups filter and the table's Group column. |
| `desc` | string | `""` | Description: shown in tooltips, the detail line and the danger dialog. Searched by the search box. |
| `scale` | number | `1` | shown = raw × scale + offset (16.5). |
| `offset` | number | `0` | See `scale`. |
| `format` | string | none | `"hex"`: show an integer in hex. No other value has an effect. |
| `danger` | boolean | `false` | Every write from the window asks first (6.3), and API clients also need *including ⚠ registers* (17.2). |
| `decimals` | number | automatic | The shown value with this many decimals (0 – 15). |
| `min`, `max` | number | none | The shown value's limits for writes, in shown units: the window asks before writing past them, the API refuses (6.4). |
| `default` | number or string | none | The value after a reset, in shown units, or one of the register's value names. The quick-write panel's **Default** writes it. |
| `special` | object | none | Names of single values of a number, in shown units: `{ "-1": "not measured" }`. Decoded shows the name, a write may always set them (31.2). |
| `enum` | object | none | Names of values (16.6). |
| `fields` | array | none | Bit fields (16.7). |

### 16.4 Types

All multi-byte values are **little endian** (the low byte first).

| Type | Size | Range | Notes |
|---|---|---|---|
| `u8` | 1 | 0 … 255 | |
| `i8` | 1 | −128 … 127 | |
| `u16` | 2 | 0 … 65535 | |
| `i16` | 2 | −32768 … 32767 | |
| `u32` | 4 | 0 … 4294967295 | |
| `i32` | 4 | −2147483648 … 2147483647 | |
| `f32` | 4 | IEEE 754 single | Shown with decimals by size (4.2). NaN shows `NaN`. |
| `bytes` | `size` | any bytes | Shown as hex (the first 24 bytes). Written as exactly `size` hex bytes. Not plotted. Polled only up to 32 bytes: longer ones are read with *Read now*. |

### 16.5 Scale and offset

A register with `scale` ≠ 1 or `offset` ≠ 0 is treated as a decimal value:

- **Reading:** shown = raw × scale + offset. This applies to the table, the chart, CSV and the API.
- **Writing a plain number:** raw = (typed − offset) ÷ scale, rounded to an integer and checked against the type's range. A scale of 0 is treated as 1 when writing.
- **Writing `0x…` or `0b…`:** the raw integer, with no conversion.

Example: PRESSURE is an `i16` with `"scale": 0.01`. The raw value 150 shows `1.500` bar, and writing `2.5` sends the raw value 250 (if the register were writable).

A scaled integer register has no bit view.

### 16.6 Enum

```json
"enum": { "0": "off", "1": "on", "2": "blink" }
```

- **Keys** are the numbers, in decimal (`"2"`) or hex (`"0x10"`). Keys that are not numbers are ignored.
- **Values** are the names.
- The name appears in the Decoded column and the ⓘ, or `? (7)` for a value that has no name.
- The quick-write panel offers the names as a list.
- A typed name is accepted in any case (`Blink` writes 2).
- The danger dialog shows the name of the value about to be written.

### 16.7 Fields and the bits syntax

```json
"fields": [
  { "name": "MODE",  "bits": "1:0", "values": { "0": "idle", "1": "run", "2": "fault" } },
  { "name": "READY", "bits": "2" },
  { "name": "ALARM", "bits": "3" },
  { "name": "AUTO_SEND prescaler", "bits": "15:8" }
]
```

| Key | Meaning |
|---|---|
| `name` | The field's name. |
| `bits` | `"high:low"` (either order: `"8:6"` equals `"6:8"`) for bits *low* to *high* inclusive, or one bit number, e.g. `"3"`. Bit 0 is the least significant bit of the (little-endian) value. |
| `access` | Optional: `"ro"`, `"rw"` or `"w1c"`, where the field differs from its register. |
| `desc` | Optional: a description of the field, in the export. |
| `values` | Optional names of the field's values, with keys in decimal or `0x` hex, as for `enum`. |

- **How fields are shown:** a register with fields shows its value in hex. Decoded lists `NAME=value` for wider fields, and the names of one-bit fields that are set (4.10).
- **Writing:** the bit view writes one field at a time, keeping the other bits (5.3).
- **Overlaps and gaps:** fields may leave gaps, which show `—` in the bit view. Fields should not overlap. With overlaps, the bit view shows the first field that contains a bit.
- **Enum and fields together:** Decoded shows the enum name.

### 16.8 Hex format, danger, access, groups

- **`"format": "hex"`:** the Value column shows `0x` and two digits per byte, e.g. DEVICE_ID `0x1001`. Registers with fields, and registers with the unit `bitmask`, are shown in hex too. CSV and the API always use numbers.
- **`"danger": true`:** for registers that move, power, switch or reset something. The table shows `rw ⚠` in amber, every write from the window asks (6.3), and the API needs its extra switch (17.2).
- **Access:** a read-only register cannot be edited in the table, has no quick-write panel, and is refused by the API's `set` (*NAME is read-only*). The Monitor's raw write and the API's raw `write` do not check access: the device decides.
- **Groups:** any text. Registers with the same group are shown or hidden together (4.7). Groups also help the reader of the map.

### 16.9 Load and save rules

**On load:**

- The registers are sorted by address. Registers at the same address keep their order in the file.
- A file that fails to load leaves the current map as it was.
- The map's slave address goes into the Slave box.
- The chart's lines are cleared, and math lines are compiled again.
- The port list is refreshed with the map's USB IDs.

**On save** the file changes only where the map was edited:

- Everything unchanged is copied from the file as it was: its layout (one register per line, or one key per line),
  blank lines, key order, keys the Studio does not know, numbers as typed (`2.0` stays `2.0`), strings with their
  escapes, a UTF-8 BOM and Windows line ends.
- A changed register is written again in its own layout (one line wrapped as the file's lines are, or one key per
  line), keeping its key order; a new key goes where the Studio's order puts it among the keys there. Keys set back
  to their defaults go.
- A new register is written in the layout of the others, in address order, with the separator most of them have
  (the blank lines between groups stay where they were).
- A copied register (Duplicate) keeps the unknown keys of the one it came from.
- Value names keyed `0x…` stay hex, also when names are added.
- An overlay keeps only what differs from its base (chapter 33).
- A new map, never saved, is written as indented JSON, two spaces, one key per line.

A save made by the Studio of a file it did not change is the same file, byte for byte; the tests check it on every map
they have (26.7).

**The first save of a new map** opens *Save as*, which offers `maps/device.json` next to the program.

### 16.10 Validation errors

| Message | Cause |
|---|---|
| The operating system's message, e.g. *No such file or directory* | The file cannot be read. |
| `line L, column C: <what>` | Not valid JSON: *',' or '}' is expected*, *the string is not closed*, … JSON allows no comments and no trailing commas. |
| `not a map: the file is not a JSON object` | A top-level array, number or string. |
| `"registers" is not a list`, `"registers": an item is not an object` | |
| `device_id: not a 16-bit number` | |
| `register NAME: "default" names no value of it: "x"` | `default` is a text that is none of the register's value or special names. |
| `"extends" …`, `base map <file>: …` | The base cannot be read or is not a map, or maps extend each other more than 8 deep. |
| `register "NAME": bad address` | `addr` is missing, a text that is not a number, or a number outside 0 – 0xFFFF. |
| `register NAME: unknown type "x"` | `type` is not a known type name. |
| `login: bad address` | `login` has no valid address (as for `addr`), or its address is 0. |
| `login: bad size` | `login.size` is outside 1 – 65535. |

These do not stop a load:

- an `enum` key that is not a number (ignored)
- a `bits` text that is not a number (read as 0)
- two registers with the same name, registers that share bytes, fields past the register's bits: the Map editor's
  checks list them (30.6), and the JSON Schema catches the malformed ones (32.5)

### 16.11 The example map, annotated

`maps/example_device.json` shows every kind of register. Its main parts:

```json
{
  "format": "evre-map/1",
  "device": "Example device",
  "device_id": "0x1001",
  "slave": 1,
  "login": { "addr": "0xF000", "size": 16 },
  "registers": [
    { "addr": "0xA000", "name": "DEVICE_ID", "type": "u16", "access": "ro", "group": "Protocol", "format": "hex",
      "desc": "which device this is" },
    { "addr": "0xA002", "name": "STATUS", "type": "u16", "access": "ro", "group": "Protocol",
      "desc": "protocol revision and capabilities",
      "fields": [ { "name": "protocol revision", "bits": "7:0" }, { "name": "CAP_ERROR_FRAME", "bits": "8" },
                  { "name": "CAP_BROADCAST", "bits": "9" }, { "name": "CAP_MSG", "bits": "10" },
                  { "name": "CAP_AUTO_SEND", "bits": "11" }, { "name": "CAP_DFU", "bits": "12" } ] },
    { "addr": "0xA004", "name": "CONFIG", "type": "u16", "access": "rw", "group": "Protocol", "danger": true,
      "fields": [ { "name": "HEARTBEAT", "bits": "0" }, { "name": "SYS_RESET", "bits": "1" },
                  { "name": "MSG_ENABLE", "bits": "2" }, { "name": "AUTO_SEND", "bits": "3" },
                  { "name": "DFU", "bits": "4" }, { "name": "AUTO_SEND prescaler", "bits": "15:8" } ] },
    { "addr": "0xA006", "name": "MSG_CNT", "type": "u8", "access": "rw", "group": "Protocol" },
    { "addr": "0xA007", "name": "MSG_BUFFER", "type": "bytes", "size": 255, "access": "rw", "group": "Protocol" },

    { "addr": "0xD000", "name": "UPTIME", "type": "u32", "unit": "ms", "access": "ro", "group": "System",
      "desc": "time since the device started" },
    { "addr": "0xD004", "name": "SUPPLY_V", "type": "f32", "unit": "V", "access": "ro", "group": "Power & supply" },
    { "addr": "0xD008", "name": "SUPPLY_I", "type": "f32", "unit": "A", "access": "ro", "group": "Power & supply" },
    { "addr": "0xD00C", "name": "TEMPERATURE", "type": "f32", "unit": "°C", "access": "ro", "group": "Sensors" },
    { "addr": "0xD010", "name": "STATE", "type": "u16", "access": "ro", "group": "System",
      "desc": "packed state word",
      "fields": [ { "name": "MODE", "bits": "1:0", "values": { "0": "idle", "1": "run", "2": "fault" } },
                  { "name": "READY", "bits": "2" }, { "name": "ALARM", "bits": "3" } ] },
    { "addr": "0xD012", "name": "COUNTER", "type": "u16", "access": "ro", "group": "System" },
    { "addr": "0xD014", "name": "PRESSURE", "type": "i16", "unit": "bar", "access": "ro", "group": "Sensors",
      "scale": 0.01, "desc": "sent in hundredths of a bar" },

    { "addr": "0xD080", "name": "SETPOINT", "type": "f32", "unit": "°C", "access": "rw", "group": "Settings" },
    { "addr": "0xD084", "name": "FAN_SPEED", "type": "u8", "unit": "%", "access": "rw", "group": "Settings" },
    { "addr": "0xD085", "name": "LED_MODE", "type": "u8", "access": "rw", "group": "Settings",
      "enum": { "0": "off", "1": "on", "2": "blink" } },
    { "addr": "0xD086", "name": "MOTOR_SPEED", "type": "i16", "unit": "rpm", "access": "rw", "group": "Settings",
      "danger": true, "desc": "moves the motor: confirmed on every write" }
  ]
}
```

| Part | Shows |
|---|---|
| `login` | A login register outside the register list: a token is written to 0xF000, 16 bytes, after a TCP connect (3.6). It is not polled or shown. |
| DEVICE_ID | `"format": "hex"`: shown as `0x1001`. It is checked against `device_id` at connect. |
| STATUS | Fields over one word: the protocol revision in bits 7:0 and capability flags. Decoded lists the flags that are set. |
| CONFIG | A writable register with flags and a multi-bit field, marked danger because it can reset the device: every write asks. |
| MSG_CNT, MSG_BUFFER | A counter, and a 255-byte buffer that is too long to poll (read it with *Read now*). |
| UPTIME | A `u32` in ms. |
| SUPPLY_V, SUPPLY_I, TEMPERATURE | `f32` with units. The fake device moves them as sine waves. The group "Power & supply" has an `&`, which the menus show as it is (4.7). |
| STATE | A packed state word: a two-bit field with value names and two flags. |
| PRESSURE | A scaled integer: `i16` in hundredths of a bar, shown in bar. |
| SETPOINT, FAN_SPEED | Writable settings. |
| LED_MODE | An enum, written from the named values list. |
| MOTOR_SPEED | A signed danger setpoint with a description the confirmation shows. |

The layout makes three block reads per poll (13.3).

### 16.12 Tips for writing a map for a new device

- **Start from the device's register list,** or from **New** plus **+ Register** in the Studio, then refine the JSON by hand for enums and fields. **New** already contains DEVICE_ID, STATUS and CONFIG.
- **Always include DEVICE_ID** (`0xA000`) and set `device_id`, so the Studio warns about the wrong device or map.
- **Group registers by what they are for** (Power, Sensors, Settings), not by address.
- **Keep registers polled together close in the address space.** Within one 256-address bank and with gaps of at most 8 bytes, they cost one read (13.3). Fewer reads mean faster polls over high-latency links.
- **Give units** wherever there are any. The chart, CSV and measurements use them, and `W` and `A` get energy and charge units.
- **Use `scale` and `offset`** for fixed-point values, so users see and type real units.
- **Name the values of state words** with `enum` or field `values`, so the table reads `MODE=run` instead of a number.
- **Mark everything that moves, powers, switches or resets something** with `"danger": true`, and write in `desc` what a write does. The confirmation dialog shows it.
- **Keep names unique and without spaces** if they are to be used in math lines and scripts: `SUPPLY_V`, not `Supply voltage`.
- **Declare `usb`** for a USB device, so its port is easy to find, and **`login`** for a device that needs a token.
- **Check the result in the Studio:** the block count in the sidebar, the decoded fields, and that no register shows *not available*.

## 17. The API

When **Serve API** is on, the Studio shares the device it is connected to with other programs. Every request of every client goes through the Studio's own request queue, the same queue as its polls. Clients and the Studio therefore never collide on the port, and a device that takes one client at a time still serves them all.

| Port | Protocol | For |
|---|---|---|
| **1220** | JSON lines, by register name | Scripts: Python, MATLAB, LabVIEW, anything with a TCP socket. |
| **1219** | EVRe pass-through: the same frames as the device | Existing EVRe clients, and another EVRe Studio. |

The ports can be changed only in the settings (`api/evrePort`, `api/jsonPort`, see 14.3).

### 17.1 Enabling it

- Tick **Serve API** in the sidebar's *API server* card, or start with `--api`.
- **Localhost:** by default the server listens on `127.0.0.1` only, so only programs on the same computer can connect.
- **Network (not only this PC):** the server listens on all addresses. Ticking or unticking it while serving restarts the server on the new addresses. Both Serve API and Network are saved.
- **Running:** the card shows *EVRe :1219 · JSON :1220 on this PC*, or *on the network*, with the number of clients and requests.
- **Off:** the card says *Off. Scripts: see Help → API.* (MATLAB, LabVIEW, Python: the Help page names them)
- **A failed start:** for example when a port is in use. Serve API is unticked again, the card shows *not started: EVRe port 1219: <reason>*, and the Log has the error.

The server keeps running while the Studio has no link. In that state `info`, `list` and `stop` work, and every other JSON command answers *EVRe Studio is not connected to a device*.

### 17.2 The write switches, and why they are never saved

| Switch | Effect |
|---|---|
| **Allow API writes** | Off: clients can only read and stream. On: `set`, `write` and pass-through writes are allowed, except to danger registers. Shown in amber while on. |
| **including ⚠ registers** | Enabled only while Allow API writes is on. Also allows writes that touch a register marked `danger`. Shown in red while on. Unticking Allow API writes unticks it. |

Both are off at every start, and neither is ever saved. They can be set at start with `--api-writes` and `--api-writes-danger`.

The reason: a Studio started later, perhaps unattended or by a script, must not let a client move or switch something unless a person decided so for this session. API writes do not show the window's confirmation dialogs: the danger switch takes the dialog's place. The window's own *Allow writes* has no effect on the API.

A write is checked against the danger registers by its byte span. A raw write that **overlaps** any byte of a danger register needs the danger switch.

The refusal texts:

- `API writes are off in EVRe Studio (tick "Allow API writes")`
- `MOTOR_SPEED is a ⚠ register: tick "including ⚠ registers" in EVRe Studio`

### 17.3 Port 1220: JSON lines

**Framing.** Each request is one JSON object on one line, ended by a line feed (`\n`), and each answer is one line too. The rules:

- Empty lines are ignored.
- A client that sends more than 64 KiB without a line feed is disconnected.
- `cmd` is matched in any case.

**Answers.** A success has `"ok": true`. A failure is `{"ok":false,"error":"<why>"}`. The connection stays open after a failure.

**`id`.** Any request may carry `"id"`, which can be any JSON value except null. It comes back unchanged in the answer. Use it to match answers when a client sends several requests without waiting. Answers usually come in request order, since the Studio's queue answers reads in order, but `id` is the reliable way.

**Names.** Wherever a request names registers, a name is the map's name in any case, or an address as text (`"0xD00C"`) that is exactly the start of a map register. Answers use the map's names. On a bus (3.9) the names carry their device's: `"D2_SUPPLY_V"` reads that register of D2, whatever its slave address; `list` gives each register's `"slave"`; an address as text, and `read` / `write` by address, are the selected device's. JSON objects in answers list their keys in alphabetical order.

**Values.**

| Register | Value in answers |
|---|---|
| Integer | a JSON integer |
| `f32`, or scaled / offset | a JSON number |
| `bytes` | a string of hex digits without spaces (`"48454c4c4f"`) |
| No valid value, or not finite | `null` |

**A request that is not a JSON object** gets `{"ok":false,"error":"not a JSON object: <parser message>"}`, without an `id`.

**Command order.** The commands `info`, `list` and `stop` work without a device. Every other command, including an unknown one, first needs the Studio to be connected. Otherwise it gets `EVRe Studio is not connected to a device`.

#### `info`

The Studio, the device, the link and the write switches.

```
{"cmd":"info","id":1}
{"app":"EVRe Studio","connected":true,"danger_writes":false,"device":"Example device","evre_port":1219,
 "id":1,"json_port":1220,"link":"127.0.0.1:1210","ok":true,"registers":16,"writes":false}
```

| Field | Meaning |
|---|---|
| `device` | The map's device name. |
| `connected` | The Studio has a link. |
| `link` | `host:port` or `COM7 @ 115200`, empty without a link. |
| `registers` | The number of registers in the map. |
| `writes`, `danger_writes` | The two switches. `danger_writes` is true only when both are on. |
| `evre_port`, `json_port` | The ports served. |

#### `list`

Every register of the map, including those that are not polled.

```
{"cmd":"list"}
{"ok":true,"registers":[{"access":"ro","addr":"0xA000","desc":"which device this is","group":"Protocol",
 "name":"DEVICE_ID","plot":false,"size":2,"type":"u16"}, ...,
 {"access":"rw","addr":"0xD086","danger":true,"desc":"moves the motor: confirmed on every write","group":"Settings",
 "name":"MOTOR_SPEED","size":2,"type":"i16","unit":"rpm"}]}
```

Each entry has `name`, `addr`, `type`, `size`, `access` (`ro` / `rw`) and `group`. It also has `unit`, `desc` and `danger: true` when the map sets them, and `plot: false` for a register the map marks not plottable (16.3).

#### `get`

Fresh values, read from the device now. They are not the table's cached values.

```
{"cmd":"get","names":["SUPPLY_V","STATE"]}
{"decoded":{"STATE":"MODE=run  READY"},"ok":true,"values":{"STATE":5,"SUPPLY_V":12.05}}
```

- **Naming the registers:** `names` (an array), `name` (one string), or both.
- **Reading:** the registers are merged into block reads by the same rules as the poller (13.3).
- **`decoded`:** present when at least one of the registers has bit fields or an enum. It holds their Decoded text.
- **The table:** the values read go into the Studio's table too.
- **Errors:**
  - `no register "X" in the map`
  - `"names" is empty`
  - the first block that failed, for example `0xD000: timeout (500 ms)`

#### `set`

Writes values by name, then answers with what the device holds after the writes.

```
{"cmd":"set","values":{"LED_MODE":2,"SETPOINT":22.5}}
{"ok":true,"values":{"LED_MODE":2,"SETPOINT":22.5}}
```

- **Value forms:**
  - JSON numbers, used at full precision
  - `true` / `false`, sent as 1 / 0
  - strings, taken as typed text (5.2): an enum name (`"blink"`), `"0x1F"`, `"0b101"`, or hex bytes for a `bytes` register
- **All or nothing:** every value is checked and encoded before the first write. One bad value refuses the whole request, and nothing is written.
- **Limits:** a value past the register's `min` or `max` is refused: *NAME: 150 is above the maximum 100 % (the
  map's limit)*. There is no one to ask *write anyway* (6.4). A special value is always allowed.
- **Order:** each write is acknowledged (`WRITE_ACK`). They go out in the key order of the parsed object, which is alphabetical. When the order of writes matters, send separate `set` requests.
- **Read-back:** after the writes, all the registers are read back, and the answer carries those values. A device that clamps a value answers with the clamped one.
- **Errors:**
  - `no register "X" in the map`
  - `NAME is read-only`
  - the write refusals of 17.2
  - `NAME: <encoding error>`, e.g. `FAN_SPEED: 300 is out of range for u8 (0 … 255)`
  - `"values" is empty: {"cmd":"set","values":{"NAME":value}}`
  - a device error: `0xd085: permission denied` (the address in lower-case hex)
  - `written, but the read-back failed: <why>`

#### `stream`

A sample line at a fixed period, until `stop`, a new `stream`, or the end of the connection.

```
{"cmd":"stream","names":["SUPPLY_V","SUPPLY_I"],"ms":50,"id":"s1"}
{"id":"s1","ms":50,"ok":true,"streaming":2}
{"stream":"s1","t":1790170000.125,"values":{"SUPPLY_I":0.8,"SUPPLY_V":12.0}}
{"stream":"s1","t":1790170000.175,"values":{"SUPPLY_I":0.81,"SUPPLY_V":12.01}}
```

- **Names:** as for `get`. An empty list is the same as `stop`.
- **`ms`:** the period in whole milliseconds. It defaults to 100 when left out or not a whole number, and the minimum is **5**: smaller values are raised to 5. The confirmation reports the period used.
- **Each sample** has:
  - `t`: the time in seconds since 1970, with millisecond resolution
  - `values`: as in `get`, without `decoded`
  - `stream`: the stream request's `id`, when it was a string or a number (a numeric id comes back as text, `"7"`)

  A failed read gives `{"t":…,"ok":false,"error":"<why>"}`, with `stream` as well.
- **One read at a time.** The next sample is skipped while the previous read is still under way, so a slow device lowers the rate instead of piling up requests. No samples are sent while the Studio has no link.
- **One stream per client.** A new `stream` replaces the old one. The client can send other commands meanwhile, and their answers are mixed with the samples.

#### `stop`

Ends the stream, if there is one. It always answers `{"ok":true}`.

#### `read`

Raw bytes from any address, not merged and not stored in the table.

```
{"cmd":"read","addr":"0xD000","count":16}
{"hex":"e8030000000040410000c03f...","ok":true}
```

- `addr` is a JSON number (0 – 65535) or a text (`"0xD000"`, `"53248"`).
- `count` is a whole number from 1 to 65535.
- The answer is lower-case hex without spaces.
- Errors: `read needs "addr" and "count"`, or the device's refusal (`offset out of range`, a timeout).

#### `write`

Raw bytes to any address, with acknowledgement. There is no read-back and no access check, but the write switches apply to the byte span (17.2).

```
{"cmd":"write","addr":"0xD085","hex":"02"}
{"ok":true}
```

- Characters that are not hex digits are ignored in `hex`, so `"00 64"` works.
- Errors: `write needs "addr" and "hex"`, the write refusals, or the device's refusal.

#### `broadcast`

A value to every device on the link at once (slave 0: a `WRITE` nobody answers), then that register read back from
each device that has it. The rule of 3.10 holds (into the reserved bank always, elsewhere only when every device has
the same map), and the write switches of 17.2 for each device's register there.

```
{"cmd":"broadcast","name":"D1_SPEED","value":0}
{"ok":true,"values":{"D1_SPEED":0,"D2_SPEED":0}}
```

- `name` is any device's register at that address; the value is encoded, and checked against the limits, as for
  `set`.
- Errors: the rule's refusal (3.10), the write refusals, or `sent, but the read-back failed: ...`.

#### An unknown command

It gets `unknown cmd "x": info, list, get, set, stream, stop, read, write, broadcast`.

#### Several clients

Any number of clients can be connected at once. Each has its own connection, its own stream and its own `id`s. All their requests share the Studio's queue with its polling, so heavy API traffic lowers the poll rate and polls lower the API's rate. The status line of the API card counts the clients and the requests (JSON lines and EVRe frames).

### 17.4 Port 1219: EVRe pass-through

The pass-through accepts the device's own frames (layout in 18.1) and forwards them through the Studio's queue. An EVRe client that works with the device or with a TCP gateway works here unchanged. It can even be another EVRe Studio connected over TCP to port 1219: its polls, keep-alive and writes pass through this Studio's queue.

| Request from the client | What the Studio does | Answer to the client |
|---|---|---|
| `READ` | Forwards it as a READ of the same offset and count. | `READ_RESP` with the device's bytes, or `ERROR_RESP` with the device's code. |
| `WRITE_ACK`, writes allowed | Forwards it as a `WRITE_ACK`. | `WRITE_ACK_RESP`, or `ERROR_RESP` with the device's code. |
| `WRITE_ACK`, writes refused (17.2) | Nothing is sent to the device. The check comes before the link check, so this holds with no link too. | `ERROR_RESP` code **3** (permission denied), at once. |
| `WRITE`, writes allowed | Forwards it as a `WRITE_ACK`: the device always acknowledges, so the queue can order it. | none, as for a `WRITE` on the wire |
| `WRITE`, writes refused | Nothing is sent. | none |
| A `READ`, or an allowed write, while the Studio has no link | Nothing is sent. | none: the client times out, as with a silent device |
| A timeout or a lost link | | none |
| Any other function code (an answer sent to the server) | Ignored. | none |
| A frame with a bad CRC or end byte | Dropped. | none |

How frames are handled:

- **Answers mirror the request.** They carry the request's slave address, offset and count, as the device itself would answer.
- **The Studio's own slave address is used, with one device.** The Studio forwards to its device with the slave address in its Slave box, not the client's.
- **On a bus, the frame's slave address is used** (3.9): a frame goes to the device it names, and the answer comes as from that device. A broadcast `WRITE` (slave 0) goes out as one when the rule of 3.10 and the write switches allow it for every device, and is never answered; a broadcast `READ` or `WRITE_ACK` is not sent.
- **Frames are not merged.** Each request is one request to the device.
- **Pipelining works.** A client may pipeline requests. The Studio's In flight then limits how many are on the link at once.

### 17.5 Examples

The `examples/` folder has a client for Python, a demo for MATLAB and step-by-step notes for LabVIEW. They use the registers of `maps/example_device.json`: put your own map's names in their place.

**Python** (`examples/python/evre_studio_client.py`, standard library only). The file holds a small client class, and run as a script it prints info, reads values and streams for 2 s:

```python
from evre_studio_client import EvreStudio

s = EvreStudio()                       # 127.0.0.1:1220
print(s.info()["device"])
print(s.get("SUPPLY_V", "SUPPLY_I"))   # {'SUPPLY_I': 0.8, 'SUPPLY_V': 12.05}
print(s.set(LED_MODE=2))               # read back: {'LED_MODE': 2}, needs Allow API writes
for t, values in s.stream(["SUPPLY_V"], ms=20):
    print(t, values["SUPPLY_V"])
    break                              # leaving the loop sends {"cmd":"stop"}
s.close()
```

Run it directly: `python3 examples/python/evre_studio_client.py [NAME ...]`.

The same without the class:

```python
import json, socket
sock = socket.create_connection(("127.0.0.1", 1220))
f = sock.makefile("rw", encoding="utf-8", newline="\n")
f.write(json.dumps({"cmd": "get", "names": ["SUPPLY_V"]}) + "\n"); f.flush()
print(json.loads(f.readline())["values"]["SUPPLY_V"])
```

**MATLAB** (`examples/matlab/evre_studio_demo.m`, R2020b or newer). The demo reads info and values, makes one write, then streams for 5 s and plots the result:

```matlab
c = tcpclient("127.0.0.1", 1220, "Timeout", 3);
configureTerminator(c, "LF");
writeline(c, jsonencode(struct("cmd", "get", "names", {{"SUPPLY_V", "SUPPLY_I"}})));
r = jsondecode(char(readline(c)));
fprintf("SUPPLY_V = %.3f V\n", r.values.SUPPLY_V);
```

**LabVIEW** (no toolkit needed):

1. **TCP Open Connection** to `127.0.0.1`, port `1220`.
2. **TCP Write** the request text followed by a line feed, e.g. `{"cmd":"get","names":["SUPPLY_V"]}` + `\n`.
3. **TCP Read** in **CRLF** mode (bytes to read 65536, timeout 3000 ms): one answer per line.
4. **Unflatten From JSON** (LabVIEW 2013 or newer) into a cluster that matches the answer, for example `ok` (Boolean) and `values` (a cluster with `SUPPLY_V`, a DBL).
5. For a stream: send `{"cmd":"stream","names":["SUPPLY_V"],"ms":20}` once, then keep reading lines in a loop. Send `{"cmd":"stop"}`, or close the connection, to end it.

**Existing EVRe code:** point it at port 1219 instead of the device. A refused `WRITE_ACK` gets `ERROR_RESP` code 3.

## 18. The EVRe protocol as the Studio uses it

This chapter describes how the Studio uses EVRe. The protocol itself is specified by teknile in the EVRe protocol documentation.

### 18.1 Frame layout

Every frame, request or answer, has the same layout. All multi-byte numbers are little endian.

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | START | `0x7B` |
| 1 | 1 | SLAVE | The slave address. |
| 2 | 1 | FN | The function code (18.2). |
| 3 | 2 | OFF | The register offset (address), low byte first. |
| 5 | 2 | CNT | The byte count, low byte first. |
| 7 | n | DATA | The length follows from FN (18.2). |
| 7 + n | 2 | CRC | CRC-16/X-25 over bytes 0 … 6 + n, low byte first (18.4). |
| 9 + n | 1 | END | `0x7D` |

A frame is 10 + n bytes long. Examples, with slave 1:

```
READ 4 bytes at 0xA000 (DEVICE_ID and STATUS):
7B 01 AA 00 A0 04 00 4E 21 7D

its answer, DEVICE_ID 0x1001, STATUS 0x3F01:
7B 01 AB 00 A0 04 00 01 10 01 3F B3 19 7D

WRITE_ACK 1 byte (2) at 0xD085 (LED_MODE = blink), and its acknowledgement:
7B 01 EB 85 D0 01 00 02 32 B1 7D
7B 01 EC 85 D0 01 00 AD 66 7D

ERROR_RESP code 4 (offset out of range) to a read of 2 bytes at 0xD200:
7B 01 EE 00 D2 02 00 04 90 BA 7D
```

### 18.2 Function codes

| Code | Name | DATA | Meaning |
|---|---|---|---|
| `0xAA` | READ | none | Read CNT bytes at OFF. |
| `0xAB` | READ_RESP | CNT bytes | The answer to READ. |
| `0xEA` | WRITE | CNT bytes | Write CNT bytes at OFF, with no answer. |
| `0xEB` | WRITE_ACK | CNT bytes | Write CNT bytes at OFF, answered. |
| `0xEC` | WRITE_ACK_RESP | none | The answer to WRITE_ACK: written. |
| `0xEE` | ERROR_RESP | 1 byte, the error code | The answer to any request that failed. |

How the Studio uses them:

- **READ** for polls, reads on request, the device ID and the keep-alive.
- **WRITE_ACK** for every write made in the window, by the API or for the login.
- **WRITE** only for the Monitor's *WRITE (no ack)*, which counts as done when sent.

### 18.3 Error codes

| Code | Name, as the Studio shows it | How the Studio reacts |
|---|---|---|
| 0 | no error | |
| 1 | invalid packet | the register shows *error*, and is tried again |
| 2 | unknown function code | the register shows *error*, and is tried again |
| 3 | permission denied | **refused for good**: *not available* for a single register |
| 4 | offset out of range | **refused for good** |
| 5 | count out of range | **refused for good** |
| 12 | length mismatch | the register shows *error*, and is tried again |
| other | `error <n>` | the register shows *error*, and is tried again |

An error answer to a merged block always splits the block first (13.4). Only a single-register read refused with code 3, 4 or 5 marks the register *not available*. The status bar's *Errors* counts every error answer.

### 18.4 CRC

- **Variant:** CRC-16/X-25.
- **Polynomial:** 0x1021, reflected (0x8408 as the shift register sees it).
- **Initial value:** 0xFFFF.
- **Final:** XOR with 0xFFFF.
- **Covered bytes:** everything from the start byte up to the last DATA byte.
- **Check value:** CRC("123456789") = 0x906E.

```c
uint16_t crc = 0xFFFF;
for (int i = 0; i < length; i++) {
	crc ^= bytes[i];
	for (int bit = 0; bit < 8; bit++) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
}
crc ^= 0xFFFF;
```

### 18.5 Receiving: the parser

The Studio reads frames out of the byte stream as follows:

1. Skip everything before a start byte `0x7B`.
2. Once the 7-byte header is in, take the DATA length from FN.
3. If FN is not a function code, that `0x7B` was data. Drop it and search again.
4. Once the whole frame is in, check the CRC and the end byte.
5. If either is wrong, count a **bad frame** (the status bar's *Bad frames*), drop one byte, and search again.

Noise, half frames and frames of other talkers cost only their own bytes: the parser finds the next real frame.

The parser also recognises request frames (READ, WRITE, WRITE_ACK). On a shared half-duplex line, an echo of the Studio's own request is parsed as a frame. Nothing is waiting for it, so it is shown in the Monitor as *(not a pending request)* and otherwise ignored.

### 18.6 Pipelining and answer matching

**The queue.** The Studio's master keeps one queue for everything: polls, reads and writes from the window, the login, the keep-alive and API clients. It sends from the head of the queue while fewer than **In flight** requests are waiting for their answers. Two rules keep writes safe:

- **Only reads overlap.** A write is sent only when nothing is waiting.
- **Nothing is sent while a write is unanswered.** A read queued after a write therefore always sees the new value. This is why a read-back after a write is reliable even with pipelining.

**Matching.** An answer is matched to the **oldest** waiting request **to the slave it comes from**, with the **same offset and the same count** (every answer echoes both), whose expected answer it is (several devices may share the link, 3.9):

- READ_RESP for a READ
- WRITE_ACK_RESP for a WRITE_ACK
- ERROR_RESP for either

The count matters because a device may send frames by itself: with AUTO_SEND (13.8) a READ_RESP of its whole read-only block can arrive while a read of part of it (the same offset, fewer bytes) waits, and must not be taken for its answer.

An answer that matches nothing is reported as unsolicited (the Monitor's *(not a pending request)*); with auto send on, the engine takes the device's frames among them.

**Statistics.** The status bar shows these counters:

| Counter | Meaning |
|---|---|
| TX / RX | Frames sent and received. |
| Timeouts | Requests that got no answer in time. |
| Errors | Error answers from the device. |
| Bad frames | Frames with a wrong CRC or end byte (18.5). |
| Latency | An exponential average over roughly the last 20 successful answers, measured from sending a request to receiving its answer. |

### 18.7 Timeouts and the keep-alive

**Timeouts.** One timer runs for the oldest waiting request only, set to the time it has left.

- When it fires, that request fails with `timeout (<n> ms)` and the timer moves on to the next one.
- A timed-out read of a poll leaves its registers' last good values (4.5), and the next poll tries again.
- A late answer that comes after its timeout is matched to a newer request with the same offset if there is one, or reported as unsolicited. Either way no request is left hanging.

**The keep-alive.** Every **400 ms**, when the link is open and nothing is queued or waiting, the Studio reads 2 bytes at `0xA000` (DEVICE_ID). Some servers drop a client that stays silent for about a second, and a device may watch its host the same way. With polling on, the queue is rarely empty, so the keep-alive is rarely sent. With polling off, it keeps the link alive.

### 18.8 The connect sequence and the device ID read

When a link opens, the Studio goes through these steps (21.1 follows them through the code):

1. It clears every value, so the table starts afresh, and resets *not available*.
2. **Login:** if a token is set and the map declares `login`, it sends a WRITE_ACK of the token to the login register (3.6). A refusal is logged. Because a write blocks the queue until it is answered, the login's answer comes before anything else.
3. **Device ID:** it reads 4 bytes at `0xA000`, which are DEVICE_ID (u16) and STATUS (u16). It reports the ID, and the protocol revision (the low byte of STATUS), and compares the ID with the map's `device_id` (3.7).
4. **Polls** start at the chosen interval.

**Link details:**

- **TCP:** Nagle's algorithm is off (low-delay option), so each request leaves at once.
- **Serial:** 8N1 with no flow control. DTR is set on after opening, and the port's buffers are cleared.
- **Link loss:** a lost link fails every waiting request with *cancelled* or *not connected*. The window then reconnects if *Reconnect by itself* is on (3.5).

# Part III. Internals

This part is for people who maintain or extend the Studio. It describes how the code is built and why. Part I
describes the tool as a user sees it. Part II covers the map format, the API and the protocol.

## 19. Architecture

### 19.1 Layers

The Studio has five modules. `evre` and `model` form the base: each uses only what is above it in the table. `io`
and `api` depend on each other: the engine creates and owns the `ApiServer`, and the `ApiServer` works on the
engine's `RegTable` (`io/reg_table.h`) and its master. `ui` uses all of them.

| Module | Directory | Depends on | Holds |
|---|---|---|---|
| `evre` | `src/evre/` | Qt Core, Network, SerialPort | frames and the CRC, the links (TCP, serial), the master (queue, pipelining, timeouts) |
| `model` | `src/model/` | `evre` (byte order helpers) | the device map and value coding, formulas, math lines, the register table model |
| `io` | `src/io/` | `evre`, `model`, `api` | the I/O engine: the connection, the poller, CSV, the table the I/O thread writes |
| `api` | `src/api/` | `evre`, `model`, `io/reg_table.h` | the API server (EVRe pass-through, JSON lines) |
| `ui` | `src/ui/` | all of the above | the window, the tabs, the chart, the theme, help |

`src/main.cpp` reads the command line, applies the theme and opens the window. The four executables are built from
the same sources (25.4). `evre_probe` and `evre_fake_fast` link only `evre_protocol`: the `evre` files and
`device_map`. They do not link the window.

```
 GUI thread
 +--------------------------------------------------------------------------------+
 | MainWindow                                                                     |
 |  +- Sidebar            link, map, polling, API choices and states              |
 |  +- RegistersTab       QTableView <- RegisterFilter <- RegisterModel           |
 |  |                     ValueDelegate, detail line, QuickWritePanel (BitView)   |
 |  +- ChartTab           ChartWidget / ChartView, MathLines, measurements        |
 |  +- MonitorTab         frames on the link, hand-typed requests                 |
 |  +- EventLog + Notice  Log tab, logs/studio_<date>.log, the pop-up             |
 |  +- FrameClock --tick--> MainWindow::sync()   (once per display refresh)       |
 |                                                                                |
 |     | IoEngine::post(lambda)            ^ queued signals                       |
 |     | (queued, in order)                | + copies taken under a mutex:        |
 |     |                                   |   snapshot(), takeSamples(),         |
 |     |                                   |   takeFrames(), stats()              |
 +-----|-----------------------------------|--------------------------------------+
 I/O thread "evre-io" (QThread, HighPriority)
 +-----v-----------------------------------+--------------------------------------+
 | IoEngine                                                                       |
 |  +- RegTable          the definitions and the last values (one writer)         |
 |  +- poller            blocks, polls under way     <-- ticker thread            |
 |  +- CSV writer        one row per poll                                         |
 |  +- ApiServer         :1219 EVRe, :1220 JSON      <-- other programs           |
 |  +- evre::Master -- evre::Link (TcpLink | SerialLink) -- the device            |
 +--------------------------------------------------------------------------------+
```

### 19.2 The GUI thread and the I/O thread

All device traffic runs on the I/O thread. This covers the link, the master's queue, the poll clock, CSV rows and
the API server. The reason: a slow paint, a modal dialog or a large table repaint must never delay a poll or a CSV
row. The chart and the CSV file get a sample for every poll, not one per display frame.

The window does not call the engine's functions directly. It uses three paths:

| Direction | How | Used for |
|---|---|---|
| window → engine | `IoEngine::post(std::function<void()>)`, which calls `QMetaObject::invokeMethod(engine, f, Qt::QueuedConnection)` | every request: map, link, polling, plotted set, CSV, writes, reads, API switches |
| engine → window | signals, queued because the sender lives in the other thread | `opened`, `closed`, `deviceInfo`, `loginRefused`, `loginSkipped`, `readDone`, `writeDone`, `recordStarted`, `recordStopped`, `apiStarted` |
| window reads engine state | copies under a mutex | `table().snapshot()`, `takeSamples()`, `takeFrames()`, `stats()` |

The posted lambdas capture their arguments by value. They run in the order they were posted, so a `setMap` posted
before `connectTcp` is applied first.

A request that is answered carries an id. `MainWindow::engineRead` and `engineWrite` store the callback in
`pendingReads_` or `pendingWrites_`, keyed by `nextRequestId_`, and post the request. The engine answers with
`readDone(id, ...)` or `writeDone(id, ...)`. The lambdas that `startEngine()` connects to these two signals take the
callback out of the hash and run it in the GUI thread.

### 19.3 Who owns what

| Object | Owner / parent | Thread | Notes |
|---|---|---|---|
| `QThread ioThread_` | `MainWindow` (child) | GUI | object name `evre-io`, started with `QThread::HighPriority` |
| `IoEngine` | none; moved with `moveToThread(ioThread_)` | I/O | deleted by `deleteLater` when the thread finishes |
| `RegTable` | `IoEngine` member (not a `QObject`) | written in I/O | see 19.4 |
| `evre::Master` | `IoEngine` (child) | I/O | moved along with the engine |
| `ApiServer` | `IoEngine` (child) | I/O | its two `QTcpServer`s are its children |
| `QTimer statsTimer_`, `offlineTimer_` (the offline retry, 3.9), `heartbeatTimer_` (auto send, 13.8), `QFile csvFile_` | `IoEngine` (children) | I/O | |
| `evre::Link` | `IoEngine::link_` (`std::unique_ptr`) | I/O | created with `new` in `IoEngine::connectTcp` / `connectSerial`, which run in the I/O thread; `attach()` takes it over; its socket or port is its child |
| API client sockets | the `QTcpServer` that accepted them | I/O | created by `nextPendingConnection()` in the I/O thread |
| API stream timers | `ApiServer` (child) | I/O | created in the I/O thread by `startStream()` |
| ticker `std::thread` | `IoEngine` | its own | only while polling at an interval > 0 |
| `RegisterModel` | `MainWindow` (child) | GUI | the window's copy of the values; its text colours come from the window (`setColors`), the model knows no theme |
| `FrameClock` | `MainWindow` (child) | GUI | on Windows its waiting `std::thread` |
| chart data (`ChartView::series_`) | `ChartView` | GUI | read by the chart's own threads (`pool_`) during a paint, while the GUI thread waits for them (23.6); written only by the GUI thread |
| `DeviceMap map_` | `MainWindow` member | GUI | the engine gets copies of the definitions |

When the window closes, `~MainWindow` stops the frame clock. It then runs `IoEngine::shutdown()` in the I/O thread
with `Qt::BlockingQueuedConnection`. This stops the recording and the API server, drops the link and stops the
ticker. The destructor then asks the thread to quit and waits up to 3 s.

### 19.4 The one-writer rule of `RegTable`

`RegTable` (`src/io/reg_table.h`) holds one `RegValue` per register:

| Field | Meaning |
|---|---|
| `def` | the definition |
| `raw` | the bytes last read |
| `valid` | there is a good value |
| `error` | the last read error |
| `unavailable` | the device refused the register for good |
| `updatedMs` | the time of the last good read, on the table's own clock |
| `version` | +1 on every change |

The rules:

1. **Only the I/O thread writes it.** The writers are the poller (`storeBlock`, `onBlockRefused`), reads on
   request (`IoEngine::read`), the read-back after a write (`readBack`) and the API server (`readRegisters`).
2. **Every write takes the mutex.** `setDefs`, `setRaw`, `setError` and `clearValues` lock `mutex_`.
3. **The I/O thread reads without a lock** (`rows()`, `size()`). No other thread writes, so the I/O thread cannot
   read a half-written row.
4. **Other threads only read, and only through `snapshot()`.** It copies the rows, the map generation they belong
   to and the table's clock, all under the mutex.

`setRaw` increments `version` even when the same bytes are read again. The age of the value changes, and the
window must learn that the value is fresh. The window keeps the last version it copied for each row
(`MainWindow::copiedVersions_`) and applies only the rows that moved.

The copy is a `QVector` assignment, so it is implicitly shared and cheap to take. The next write in the I/O thread
detaches it while holding the mutex.

The generation number tells the window which map a snapshot belongs to. `MainWindow::pushMap()` increments
`mapGeneration_` and posts it with the definitions. `sync()` ignores snapshots of another generation, or with a
different number of rows. Values of an old map therefore never land in the rows of a new one.

`setDefs` (a new map, a map edited, another device picked on a bus) keeps the value of a register read the same
way as before: the same device and address (`regKey`), type and size. The table and the API are not blanked until
the next poll. On a bus every device has a register at the same address, so the device is part of the match: a
device's register never takes another's value.

The window has its own copy of the values, in `RegisterModel`. It never writes back into `RegTable`. An edit in the
table becomes a write request (21.3). The table shows the new value only when the engine has read it back.

## 20. Threads and timing

### 20.1 The threads

| Thread | Runs | Created in |
|---|---|---|
| GUI | the window, the table, the chart, the log, the frame clock's fallback timer | `main()` |
| `evre-io` | `IoEngine`, `Master`, the link, CSV, `ApiServer` | `MainWindow::startEngine()` |
| ticker | waits for each poll deadline and wakes the engine | `IoEngine::startTicker()` (plain `std::thread`) |
| frame waiter (Windows) | waits for each display refresh (`DwmFlush`) and posts a tick to the window | `FrameClock::start()` (plain `std::thread`) |

The ticker runs only while the link is up and polling is on at an interval above 0. `stopTicker()` ends it and
joins it. It runs on disconnect, when Poll is unticked, when the interval becomes 0, and at shutdown.

### 20.2 Why Qt timers do not pace the polls

On Windows, Qt's timers in a thread other than the GUI thread tick at the system tick of about 15.6 ms. The C++
library's timed waits round to the same tick (comments in `engine.h` and `IoEngine::runTicker`). A 1 ms or 5 ms
poll interval would come out as 15.6 ms. The engine therefore paces polls with its own ticker thread.

`IoEngine`'s constructor also calls `timeBeginPeriod(1)` on Windows, and the destructor calls `timeEndPeriod(1)`.
This keeps the system timer resolution at 1 ms while the Studio runs, which the remaining Qt timers need (answer
timeouts, API streams).

Qt timers still serve jobs that need no fine pacing:

| Timer | Interval | Owner |
|---|---|---|
| answer timeout | the time left for the oldest pending request, `Qt::PreciseTimer` | `Master` |
| keep-alive | 400 ms | `Master` |
| statistics | 250 ms | `IoEngine` |
| API stream | the client's `ms`, at least 5, default 100, `Qt::PreciseTimer` | `ApiServer` |
| table repaint | 50 ms, single shot | `RegisterModel` |
| status bar and sidebar | 500 ms | `MainWindow` |
| measurements | 250 ms; at most every 100 ms while the cursors move | `ChartTab` |
| reconnect | 500 ms after a lost link, 2000 ms after a failed connect | `MainWindow` |
| frame clock fallback | 16 ms, `Qt::PreciseTimer` | `FrameClock` |

The API stream timers use `Qt::PreciseTimer` because coarse timers drift by about 20 % on Windows (comment in
`ApiServer::startStream`).

### 20.3 The high-resolution ticker (Windows)

`IoEngine::runTicker()` on Windows:

1. Creates a waitable timer with `CreateWaitableTimerExW(..., CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, ...)`. This
   flag needs Windows 10 version 1803 or later. If the call fails, it falls back to `CreateWaitableTimerW`, which
   ticks at the system tick.
2. For each period, moves the deadline on by one period (20.5) and computes the wait in microseconds. It sets the
   timer as a relative due time in 100 ns units. The timer wakes some 0.3 ms late: below about 0.5 ms one wake-up
   passes several deadlines, and all of them count (step 5).
3. Waits with `WaitForMultipleObjects` on two handles: a manual-reset stop event and the timer. The stop event ends
   the loop. `stopTicker()` sets it with `SetEvent` before it joins the thread.
4. If the deadline has already passed, it checks the stop event without waiting and ticks at once.
5. Calls `onTick(tickPeriodsDue(...))`: the periods due since the deadline it waited for, that one included.

### 20.4 The condition-variable ticker (Linux, macOS)

Elsewhere, the C++ library's timed wait is precise. The loop holds `tickerMutex_` and calls
`tickerWake_.wait_until(lock, deadline, [] { return !tickerRunning_; })`. `stopTicker()` clears `tickerRunning_`
under the mutex and calls `notify_all()`. The wait then ends at once.

The code was built and tested on Windows and Linux. The macOS path is the same as the Linux one, but it has not
been tried.

### 20.5 Due ticks and catch-up rules

The ticker never starts a poll itself. It counts polls that are due. The engine starts them when it can. This keeps
the ticker simple and lets the engine decide what to do when the device is slower than the interval.

1. **Deadlines.** Each deadline is one period after the previous one. At a wake-up `tickPeriodsDue()` counts the
   deadline reached and every later one already past, and moves the deadline to the last of them: a timer coarser
   than the period passes several at each wake-up, and they are polls due, not lost. (Until 2026-10-01 a wake-up
   more than one period late started again from now: at 0.25 ms Windows' timer made that 1750 polls/s, not 4000.)
   Only far behind (2^16 periods, a long stall) does it start again from now. The backlog cap (step 3) keeps a stall
   from becoming a burst. The period is read from the atomic `tickIntervalUs_` every time, so a new interval needs
   no restart. The interval is at least 50 µs (`setPolling`: `max(50, interval × 1000)`).
2. **One wake-up at a time.** `onTick(n)` adds the periods due to the atomic `ticksDue_`. If no wake-up is queued yet
   (`tickQueued_`), it queues one call of `pollTick()` into the engine's thread. The engine's event queue therefore
   never fills with ticks, however short the interval is.
3. **Due polls become a backlog.** `pollTick()` moves `ticksDue_` into `pollsBehind_`. The backlog is capped at
   `max(8, 20 000 µs / interval µs)`, so about 20 ms of polls and never fewer than 8. At a 1 ms interval the cap is
   20 polls, at 0.25 ms it is 80, and at 5 ms or more it is 8.
4. **Slots.** A poll is one read per block. Polls may overlap, so the next one can start while the answers to the
   last are still coming. The number of polls under way at once is `clamp(In flight / blocks, 1, 8)`
   (`maxPollsUnderWay()`). `pollTick()` starts polls while there is a backlog and a free slot.
5. **Polls that end start the rest.** When a poll's last block is answered, `pollDone()` queues another
   `pollTick()` if the backlog is not empty.

`startTicker()` resets both counters. A reconnect therefore does not start with old due polls.

**Interval 0 ("max").** There is no ticker (`continuous_`). `pollTick()` fills every free slot, and every
`pollDone()` queues the next `pollTick()`. The device and the link set the rate.

**The rate shown.** `updateStats()` runs every 250 ms. It computes the poll rate over at least one second (polls
done ÷ time elapsed). Until a second has passed, the last rate stands. The sidebar compares that rate with the
interval asked for. When the rate is slower than asked and close to the limit that In flight and the latency set,
it explains why (`Sidebar::slowPollReason`, see 13.6).

### 20.6 The frame clock

`FrameClock` emits `tick()` once per display refresh. `MainWindow::sync()` runs on it: it copies samples and
monitor lines, and the chart repaints. The table's values are copied only when `ValuePacer::due()` says so
(`src/ui/value_pace.*`, *Show values*), and the chart's legend takes its values the same way in
`ChartView::frame()`, each with its own pacer: a number that changes at every refresh cannot be read. `due()` counts
a period as met up to 4 ms early, so 10 per second is every sixth refresh at 60 Hz, not every seventh.

- **Windows.** A thread loops on `DwmFlush()`, which returns at the next composition of the desktop. After each
  return it posts one tick to the GUI thread, never a second one while the first is still queued (`tickQueued_`).
  The chart then moves by the same step at every refresh, whatever the display rate. A 16 ms timer against a 60 Hz
  display drops a frame every 0.4 s, which is a visible hitch.
- **Fallback.** A `DwmFlush` that fails, or returns in under 2 ms, waited for no refresh. This happens in a remote
  session or with the screen off. After more than 20 of those in a row, the thread starts the 16 ms timer and then
  checks again every 250 ms (8 ms before that). When `DwmFlush` waits again, it stops the timer, and the refreshes
  pace the ticks again.
- **Elsewhere.** The 16 ms `Qt::PreciseTimer`.

### 20.7 Every QObject on the I/O thread is a child of a moved object

`moveToThread()` moves an object and its **children**. It does not move members that are `QObject`s without a
parent. Such a member keeps the thread affinity it was created with. The engine is created in the GUI thread and
then moved, so any member left behind still belongs to the GUI thread.

If such an object is a `QTimer` and it is started from the I/O thread, Qt refuses. It prints
`QObject::startTimer: Timers cannot be started from another thread` and the timer never fires. Qt only warns, so
nothing crashes and the feature silently does nothing.

**A bug of this kind, now fixed.** Earlier versions of `evre::Master` kept the answer-timeout timer and the
keep-alive timer as plain `QTimer` members. The master is a child of the engine, so the master itself moved to the
I/O thread, but its two timers stayed in the GUI thread. Every start from the I/O thread failed: in one GUI test run
on Windows the warning came 283 times. It went unnoticed because on Windows Qt sends such warnings to the debugger
output. On Linux the GUI test hung. The effects:

- Request timeouts never fired. A request without an answer stayed pending, and with In flight 1 it blocked the
  queue.
- The keep-alive never ran.

The fix creates both timers with `new QTimer(this)` in `Master`'s constructor (`timeoutTimer_`, `keepAliveTimer_`),
so they move with the master. The GUI test now counts Qt's thread warnings and fails if it sees any (26.2).

The rule for all code on the I/O thread: every `QObject` must be one of these:

- a child of an object that is moved (as `Master`, `ApiServer`, the engine's timers and `csvFile_` are children of the
  engine, and the two `QTcpServer`s are children of `ApiServer`), or
- created in the I/O thread itself (the link in `IoEngine::connectTcp()` / `connectSerial()`, the client sockets from
  `nextPendingConnection()`, the stream timers in `ApiServer::startStream()`).

An audit of `src/evre`, `src/io` and `src/api` found no other case. The GUI thread's own plain `QTimer` members
(`RegisterModel::repaintTimer_`, `MainWindow::statusTimer_`, `FrameClock::timer_`, ...) are correct. Their owners
never move.

### 20.8 Time bases

| Clock | Where | Used for |
|---|---|---|
| `IoEngine::clock_` (`QElapsedTimer`), read by `IoEngine::now()` in seconds | engine | the time of every sample and the `time_s` column of the CSV |
| the same clock, handed to `ChartTab` as a `std::function` | chart | the chart's right edge at every frame; the wall-clock time of its zero is computed once for the axis labels |
| `RegTable::clock_` | table | `updatedMs`; the window turns it into an age (`nowMs - updatedMs`) |
| `RegisterModel::clock_` | window | glow and staleness of values |
| `QDateTime::currentDateTime()` | CSV `datetime`, log lines, monitor lines, API stream `t` | wall-clock time |

A sample's time is the time its poll ended (`pollDone()`), not the time a request was sent. All registers of one
poll get the same time. Math lines rely on this to pair their inputs (9.6, 23.8).

## 21. Data flows step by step

### 21.1 The connect sequence

1. **Click.** `Sidebar::connectClicked` calls `MainWindow::toggleConnect()`, which sets `wantConnected_` and calls
   `connectLink()`.
2. **Window side.** `connectLink()` first posts `disconnectLink()`, so any old link goes. It then posts the link
   options (slave, timeout, In flight) and the polling state (on, interval).
   - TCP: it logs `connecting to <host>:<port>`, followed by ` (with a token)` when the token box is not empty. It
     then posts `connectTcp(host, port, token)`.
   - Serial: an empty port list stops here, and the pill shows "No serial port". Otherwise it logs
     `opening <port> at <baud>` and posts `connectSerial(port, baud)`.

   The pill shows "Connecting…" and the button shows "Cancel". While the window reconnects by itself
   (`retrying_`), the "connecting to" line is not logged again.
3. **Engine side.** `IoEngine::connectTcp()` or `connectSerial()` creates the new link in the I/O thread and hands
   it to `attach()`. `attach()` drops any old link, takes the new one, stores the token, connects `opened` and `closed`, and hands the link to the master (`Master::setLink`). `setLink` clears the
   queue and the parser and starts the keep-alive timer. Then it calls `open()`.
   - `TcpLink` calls `connectToHost`. On `connected` it turns Nagle off (`LowDelayOption`) and emits `opened()`.
   - `SerialLink` opens the port at 8N1 without flow control, sets DTR (USB CDC devices often wait for it), clears
     the buffers and emits `opened()`. A port that cannot be opened emits `closed(errorString)` at once.
4. **Opened.** `IoEngine::onOpened()`:
   1. sets `connected_` and clears every value in the table (`clearValues()`), so nothing old is shown as fresh;
   2. rebuilds the blocks (the "unavailable" marks were cleared as well);
   3. emits `opened(link)`. The window logs `connected: <link>`, enables the quick-write panel, and the pill turns
      green. This signal comes first so that the connection is logged before any login warning;
   4. **login** (`sendLogin()`), described below;
   5. **device ID** (`readDeviceInfo()`), described below;
   6. starts polling (`resumePolling()`): the ticker, or the first poll at once when the interval is 0.
5. **Login.** The login happens only when a token was given and the map declares a login register:
   - No token: nothing is sent.
   - A token but no `"login"` in the map: the engine emits `loginSkipped()`. The window logs the warning
     `the map declares no login register: the token was not sent`.
   - Otherwise `encodeLoginToken(token, loginSize)` (in `device_map.cpp`, shared with `evre_probe`) converts the
     token to UTF-8, cut or zero-padded to `loginSize` bytes, and it is written with
     acknowledge (`WRITE_ACK`) to `loginAddr`. If the device refuses, the engine emits `loginRefused(reason)`. The
     window shows `token refused: <reason>` under the pill and logs the same text as an error.

   Only the TCP path carries a token (`connectSerial` passes none). The login is queued first because a gateway may
   drop a client that does not log in soon. The master sends nothing else while a write is unanswered (18.6), so
   the device-ID read and the first poll go out only after the login is answered.
6. **Device ID.** A `READ` of 4 bytes at `0xA000` covers DEVICE_ID and STATUS, which every EVRe device has. The
   engine emits `deviceInfo(ok, id, status, err)`. The window shows `Device ID 0x1001 · protocol rev 1` under the
   pill (the revision is STATUS bits 7:0) and logs it. If the map names another device (`"device_id"` set and
   different), it adds `the map is for 0x....` in the warning colour and logs a warning. A failed read shows and
   logs `DEVICE_ID not read: <reason>`.
7. **Lost link.** The link emits `closed(why)`, with an empty `why` when the Studio asked for the close.
   `IoEngine::onClosed` drops the link and passes `closed(why)` on. The window logs `connection lost: <why>` if the
   link was up, or the warning `not connected: <why>` if it never came up. It shows the reason in the pill. If
   Connect is still wanted and "Reconnect by itself" is ticked, it tries again after 500 ms (the link was up) or
   2000 ms (it never came up). While it waits, the button shows "Stop reconnecting".

Dropping a link (`IoEngine::disconnectLink()`) stops the ticker and increments `layoutGeneration_`. Answers still
due for the old link are then ignored. It also clears the polls under way. `Master::setLink(nullptr)` fails every
queued request with "cancelled". The link is disconnected from the engine, closed, and deleted with
`deleteLater()`. It is deleted later because this code may run inside the link's own `closed` signal.

### 21.2 One poll cycle

1. **Tick.** The ticker's deadline passes. `onTick(n)` counts the polls due and wakes the engine (20.5).
2. **Start.** `pollTick()` starts as many polls as the backlog and the free slots allow. `startPoll()` gives the
   poll an id, records how many blocks it still waits for (`pollsUnderWay_`), and calls `Master::read(addr, size)`
   once per block. Each callback carries the layout generation, the poll id and the block.
3. **Send.** The master queues the requests. It sends from the head of the queue while fewer than In flight are
   pending and the next one need not wait (only reads overlap). Each frame is `7B SLAVE AA OFF CNT CRC 7D`.
4. **Answer.** `Master::onReceived` feeds the bytes to the parser. For each whole frame it finds the oldest pending
   request to the frame's slave, with the same offset and count and the expected answer (`READ_RESP` for a read, `WRITE_ACK_RESP` for a write, or
   `ERROR_RESP`). It computes the latency, counts the result and runs the callback. It then sends what may go now.
   An answer with another count matches no request: its request times out, and the answer is logged as unsolicited.
5. **Store.** `onBlockResult()` first ignores answers of an older block layout. Then:
   - OK: `storeBlock()` cuts each register's bytes out of the block and calls `setRaw`.
   - An error code: `onBlockRefused()`. A merged block is split into one read per register (13.4). A single
     register refused with code 3, 4 or 5 is marked unavailable and is no longer polled. The code means the same
     request will fail again.
   - A timeout or a link failure: `setError(row, message)` for its rows. They are tried again at the next poll.
6. **Poll done.** When the last block is answered, `pollDone()` counts the poll for the rate and starts the next one
   if polls are waiting. It takes `t = now()`, appends one sample per plotted register (`appendSamples`), and
   writes one CSV row (`writeCsvRow`). Registers without a valid value are skipped in the samples and left empty in
   the CSV.
7. **The window's next frame.** When the *Show values* pace says it is time (`ValuePacer::due`), `sync()` takes a
   snapshot, applies the rows whose version moved (`RegisterModel::applyValue`), and logs read errors that start or
   stop, at most one line per register every 5 s. At every frame it hands the samples to the chart
   (`ChartTab::frame`) and the monitor lines to the Monitor tab.
8. **Repaint.** `RegisterModel` marks the changed rows. It tells the view about them at most every 50 ms, in one
   `dataChanged` over the rows that changed, from the Address to the Decoded column. The chart repaints every frame
   while it is visible.

The samples of plotted registers are what the math lines read too. `MainWindow::pushPlotted()` sends the engine the
union of the plotted registers and the registers the active math lines read.

### 21.3 A write from the table

1. **Edit.** A double-click on a value opens an editor. The cell is editable only when Allow writes is ticked and
   the register is RW (`RegisterModel::flags`). `ValueDelegate::createEditor` stores the raw value the edit starts
   from as `base`. `setEditorData` fills the text only once, so polls while the editor is open do not overwrite
   what is being typed.
2. **Request.** On Enter, `ValueDelegate::setModelData` sends `{text, base}` as `RegisterModel::WriteRole`.
   `RegisterModel::setData` emits `writeRequested(row, text, base)` and changes nothing. The Registers tab passes it
   on to `MainWindow::onWriteRequested`. The quick-write panel sends the same request.
3. **Changed meanwhile?** If `base` is set and the value now differs from it, the window asks
   "Value changed while editing" with the old, the current and the typed value. Cancel logs
   `NAME: write of X cancelled (the value changed meanwhile)`.
4. **Encode.** `encodeValue(def, text)` turns the text into bytes: a number as shown (scale and offset are undone),
   `0x..` or `0b..` as raw bits, or an enum name. A value out of range or not a number logs the error
   `NAME: X not written: <reason>`, with a pop-up. Nothing is sent.
5. **Danger?** A register marked `"danger"` asks "Confirm write" every time, and shows the enum name and the
   description.
6. **Send.** `engineWrite(addr, bytes, ack = true)` posts `IoEngine::write(id, ...)`. When not connected, the answer
   is at once "not connected". Otherwise `Master::write` queues a `WRITE_ACK`. It waits until everything sent
   before it is answered, and nothing is sent after it until it is answered.
7. **Read back.** When the write is acknowledged, the engine emits `writeDone(id, true)` and reads the register
   back (`readBack`) into the table. The table shows what the device holds, which may be a clamped value, never
   what was typed. The window shows `NAME written` in the status bar and logs
   `written: NAME = X (hex at 0x....)`. A refusal logs the error `write refused: ...`.

Example: with `FAN_SPEED` (u8, %, RW), typing `300` stops at step 4 with "300 is out of range for u8 (0 … 255)".
Typing `2` into `MOTOR_SPEED` (i16, danger) asks at step 5 before anything is sent.

### 21.4 A map load

1. `MainWindow::loadMap(file)` calls `DeviceMap::load`. The file is parsed into a temporary map, so a file that
   fails leaves the current map as it was. A failure logs `map not loaded: <file>: <reason>` and shows a message
   box.
2. On success it logs `map loaded: <path> (N registers)`. It remembers the file for the next start, unless the map
   came from `--map`. It sets the slave address from the map, removes every line from the chart, and calls
   `RegisterModel::setMap`, which resets the model.
3. `setMap` emits `structureChanged`, and the window reacts:
   - `pushMap()` increments the map generation, marks every row as "not copied yet", and posts
     `IoEngine::setMap(defs, generation, name, devices)`: the devices are the map's one (slave 0: the link's
     slave), or a bus's, each with its slave, login register, `poll` and map (3.9).
   - It posts the set of registers to sample (`pushPlotted()`).
   - It updates the map card in the sidebar.
   - `ChartTab::setRegisters()` compiles the math lines against the new registers, draws the active ones, rebuilds
     the ƒ Math menu, and emits `mathRegistersChanged`, which calls `pushPlotted()` again.

   The Registers tab rebuilds its group menu and fits its columns.
4. **Engine.** `setDefs` replaces the table's rows (all values empty) with the new generation. The engine rebuilds
   the address → row index and the blocks. A new block layout increments `layoutGeneration_`, so answers of the old
   layout are ignored. It starts the statistics timer the first time.
5. The serial port list is refreshed. A port with the map's USB vendor and product ID is labelled with the map's
   device name.

Adding, editing or removing a register on the Registers tab goes through step 3 in the same way
(`RegisterModel::addDef`, `replaceDef`, `removeRow`, each emitting `structureChanged`). The map is then marked as
modified.

### 21.5 An API request

**A JSON `get`**, for example `{"cmd":"get","names":["SUPPLY_V","STATE"],"id":7}`:

1. `readJsonLines()` collects bytes until a newline. A client whose unfinished line grows past 64 KiB is dropped.
2. `onJsonLine()` counts the request and parses the object. It dispatches on `cmd`, in any case. `info`, `list` and
   `stop` work without a device. Every other command fails with
   `EVRe Studio is not connected to a device` while there is no link.
3. `cmdGet()` resolves the names. A name is a register name in any case, or an address such as `"0xD004"`. It calls
   `readRegisters()`, which merges the registers into block reads with the poller's rule (13.3) and queues them on
   **the same master** as the polls. The Studio and its API clients therefore never collide on the link.
4. Each answer is cut into registers. The raw values go into the answer and into `RegTable` (`setRaw`), so the table
   sees them as well.
5. When the last block is answered, the reply is written:
   `{"decoded":{"STATE":"MODE=run  READY"},"id":7,"ok":true,"values":{"STATE":5,"SUPPLY_V":12.1}}`. The keys are
   in alphabetical order, as `QJsonDocument` writes them (17.3).
   If the client has left in the meantime, the reply is dropped. Clients are held by `shared_ptr` and the answer
   keeps a `weak_ptr`.

**A pass-through `READ`** on port 1219:

1. Each client has its own `evre::Parser`. `onEvreRequest()` handles `READ`, `WRITE` and `WRITE_ACK` and ignores
   any other function code.
2. `passRead()` sends nothing back while there is no device, just as a silent device would behave. Otherwise it
   queues `Master::read(addr, cnt)`.
3. The answer goes back with the client's own slave, offset and count: `READ_RESP` with the data, or `ERROR_RESP`
   with the device's code. On a timeout nothing is sent, and the client times out as it would on the wire. The
   device is always addressed with the Studio's slave setting. The client's slave byte is only echoed.

**A pass-through write** is checked first (`writeRefusal`). API writes must be allowed, and a range that overlaps a
danger register also needs "including ⚠ registers". A refused `WRITE_ACK` is answered with `ERROR_RESP` code 3
(permission denied). A refused `WRITE` gets no answer. An allowed write always goes to the device as `WRITE_ACK`,
so the queue waits for it. The client hears of the result only if it asked for an acknowledge.

## 22. Source layout

### 22.1 Every file

| File | Role |
|---|---|
| `CMakeLists.txt` | the build: the protocol library, the core library, the program, the GUI test, the probe, the fast fake device, the maps copied beside them |
| `src/main.cpp` | command line → `MainWindow::Startup`, organisation and application names, theme, the `EVRE_SHOT` test aid |
| `src/evre/frame.h`, `.cpp` | function codes, CRC-16/X-25, `build()`, `Parser`, byte order helpers, names for codes and errors |
| `src/evre/link.h`, `.cpp` | `Link` interface, `TcpLink`, `SerialLink` |
| `src/evre/master.h`, `.cpp` | `Master`: queue, pipelining, answer matching, timeouts, keep-alive, statistics |
| `src/evre/registers.h` | namespace `evre`: the reserved bank's addresses (DEVICE_ID, STATUS, CONFIG and its bits), the STATUS capability bits, the read-only block at 0xD000, the AUTO_SEND base rate (8000 Hz) and its prescaler (1 to 255, default 0x4F; 40 Hz the least the protocol names) |
| `src/io/reg_table.h` | `RegValue`, `RegTable`: the I/O thread's table (19.4) |
| `src/io/engine.h`, `.cpp` | `IoEngine`: connect sequence, login, device ID, ticker, blocks and polls, samples, CSV, monitor lines, reads and writes asked for, API control |
| `src/model/device_map.h`, `.cpp` | `RegType`, `BitField`, `RegDef`, `DeviceMap`, `MapIssue`; decode, format and encode of values, limits, special values; addresses; block-merge rule; pollable rule; chart keys `regKey(slave, addr)`, `regKeySlave`, `regKeyAddr`; `requestSlave` |
| `src/model/map_file.cpp` | `DeviceMap::load`, `save`, `toJson`: reading with `extends`, and writing back only what changed; `registersToJson` / `registersFromJson` (the clipboard) |
| `src/model/map_check.cpp` | `checkMap`: the Map editor's checks |
| `src/model/json_doc.h`, `.cpp` | `jsondoc`: JSON as an ordered tree that remembers where each value was in its text; rendering (pretty, compact) and `patchSequence` |
| `src/model/map_export.h`, `.cpp` | Markdown, C header, Python, CSV and device table export, CSV import, `identifier` |
| `src/model/map_document.h`, `.cpp` | `MapDocument`: the map being edited, its undo history, uids, the checks' cache |
| `src/model/bus_file.h`, `.cpp` | `BusFile`, `BusDevice`: several devices on one link (`evre-bus/1`); `checkBus`, `nextBusDevice`, `busRegisterName`, `broadcastNames` (a register by the map's or the bus name), `broadcastRefusal` (the broadcast rule); `nextBusDevice` gives slave 0 when all 255 are taken |
| `src/model/expr.h`, `.cpp` | `Expr`: the formula parser (recursive descent to postfix) and its stack machine |
| `src/model/math_lines.h`, `.cpp` | `MathLine`, `MathLines`: formulas over registers, kept in the settings, evaluated per frame |
| `src/model/register_model.h`, `.cpp` | `RegisterModel` (the table's model), `RegisterFilter` (search and groups) |
| `src/api/api_server.h`, `.cpp` | `ApiServer`: EVRe pass-through on 1219, JSON lines on 1220, streams, write permissions, `broadcastWriteRefusal` (a pass-through broadcast under the rule) |
| `src/ui/main_window.h`, `.cpp` | `MainWindow`: builds the window, wires the parts to the engine, sync per frame, writes, CSV, API, log |
| `src/ui/sidebar.h`, `.cpp` | `Sidebar`: the connection, devices, map, polling and recording, and API cards; link settings |
| `src/ui/main_window_bus.cpp` | `MainWindow`'s bus part (the same class): the bus file, its devices, the pickers, broadcasts |
| `src/ui/limit_spin_box.h`, `.cpp` | `LimitSpinBox`: a number box held to the map's limits |
| `src/ui/elided_label.h`, `.cpp` | `ElidedLabel`: one line, cut with an ellipsis, the whole text in its tooltip (`setFullText`, `fullText`, `isCut`); the Map editor's banner, the Devices card's info line, the status bar's hint, the link state pill (`setElideMode`: cut in the middle; `setFullText`'s `shorter`: a shorter text shown whole first, the address alone) |
| `src/ui/bus_panel.h`, `.cpp` | `BusPanel`: the Devices on the link card: one device, or the devices of a bus and their state |
| `src/ui/bus_device_dialog.h`, `.cpp` | `BusDeviceDialog`: one device of a bus: name, slave, map, polled, its own token; checked while typing |
| `src/ui/bus_preset_dialog.h`, `.cpp` | `BusPresetDialog`: a broadcast kept in the bus file: name, register, value; checked while typing |
| `src/ui/registers_tab.h`, `.cpp` | `RegistersTab`: toolbar, table, menus, groups, Plot shown, map editing, detail line |
| `src/ui/value_delegate.h`, `.cpp` | `ValueDelegate`: the Value column's drawing (the ⓘ mark) and editor (`base`) |
| `src/ui/quick_write_panel.h`, `.cpp` | `QuickWritePanel`: value box, named values, bit view for the selected RW register |
| `src/ui/bit_view.h`, `.cpp` | `BitView`: a register drawn bit by bit; click to flip or pick a field value |
| `src/ui/map_editor_tab.h`, `.cpp` | `MapEditorTab`: toolbar, the register table, checks list, clipboard, export and import; on a bus the banner (whose map, `showDevices`) and the Live values from picker |
| `src/ui/map_table_model.h`, `.cpp` | `MapTableModel`: the Map editor's table, edited in place, bulk edits |
| `src/ui/register_editor.h`, `.cpp` | `RegisterEditor`: the form (General, Values, Bit fields, Notes) and the live line |
| `src/ui/name_table.h`, `.cpp` | `NameTable`: value names or special values, Paste lines, Hex |
| `src/ui/field_editor.h`, `.cpp` | `FieldEditor`: bit fields on the bit strip, their list and value names |
| `src/ui/map_settings_dialog.h`, `.cpp` | `MapSettingsDialog(doc, onBus, parent)`: device, protocol and notes of the map, one undo step; on a bus its Slave box is disabled |
| `src/ui/chart_tab.h`, `.cpp` | `ChartTab`: chart controls, measurements table, math-line menu, chart settings |
| `src/ui/chart_widget.h`, `.cpp` | `ChartView` (the chart) and `ChartWidget` (its wrapper) |
| `src/ui/gpu_lines.h`, `.cpp` | `GpuLines`: the chart's plot drawn by a graphics card and shown as a layer of the window (Direct3D 11, a swap chain, DirectComposition) |
| `src/ui/math_line_dialog.h`, `.cpp` | `MathLineDialog`: name, unit and formula, checked while typing |
| `src/ui/formula_completer.h`, `.cpp` | `FormulaCompleter`: the list of registers and functions while a formula is typed |
| `src/ui/monitor_tab.h`, `.cpp` | `MonitorTab`: frame log, hand-typed READ or WRITE |
| `src/ui/event_log.h`, `.cpp` | `EventLog` (Log tab and daily file), `Notice` (the pop-up) |
| `src/ui/frame_clock.h`, `.cpp` | `FrameClock`: one tick per display refresh |
| `src/ui/value_pace.h`, `.cpp` | `ValuePace` (the *Show values* choices and setting), `ValuePacer`: how often the numbers on screen change |
| `src/ui/help_dialog.h`, `.cpp` | `HelpDialog`: the help pages, kept as HTML in the source |
| `src/ui/theme.h`, `.cpp` | `ThemeColors`, `Theme::apply`: Fusion style, palettes, style sheet, the combo boxes' arrow image |
| `src/ui/ui_helpers.h`, `.cpp` | time lengths as text and back, `noMnemonic`, `coloredSpan`, card, muted label, segment button, `repolish`, `setHighlighted`, `monospaceFont`, `mediaIcon`, `warningIcon` (a tab's warning sign), `refreshIcon`, `confirmed` (a yes/no question), `mapsFolder`, `stateDot` and `fillDevicePicker` (one look for every device picker) |
| `tests/gui_test.cpp` | `evre_gui_test`: the real window driven by QtTest against the fake device |
| `tests/map_test.cpp` | `evre_map_test`: the map files (save byte for byte, edits, overlays, keys, checks, exports) without a window |
| `cli/evre.cpp` | `evre`: the command-line tool (chapter 34) |
| `cli/evre_sim.cpp` | `evre-sim`: a device made from a map (chapter 35) |
| `tests/sim_test.py` | `evre-sim` driven with `evre`: every behaviour of chapter 35 |
| `python/evre/` | the `evre` Python package: frames, link and master, the map, a device by register name (`python/README.md`) |
| `python/tests/test_evre.py` | the Python package (unittest), with a session against `evre-sim` and a bus on `evre_fake_fast` |
| `tests/cli_test.py` | `evre` end to end, against its own fake device on port 1212 |
| `tests/schema_test.py` | the maps against `docs/evre-map-1.schema.json` (needs the `jsonschema` package) |
| `docs/evre-map-1.schema.json` | the JSON Schema of `evre-map/1` |
| `tests/api_test.py` | the API end to end, in three modes |
| `tests/fake_device.py` | a fake EVRe device over TCP (Python, standard library only) |
| `tests/fake_device_fast.cpp` | `evre_fake_fast`: the same in C++, for speed measurements |
| `tests/fake_login_test.py` | the login of both fake devices and of `evre_probe`, checked the same way (26.4) |
| `tests/evre_probe.cpp` | `evre_probe`: the protocol core without a window, for real devices; it writes only the login token (26.5) |

### 22.2 Module `evre`

**`frame.h` / `frame.cpp`.** The frame layout, the function codes with their DATA lengths and the CRC are in 18.1,
18.2 and 18.4. `build()` makes a frame. `Parser::next()` reads frames out of a byte stream by the rules of 18.5: a
byte that is no function code, or a wrong CRC or end byte, costs exactly one byte, so noise and half frames are
skipped, never fatal. `badFrames()` counts the CRC and end-byte failures. `errorName()` gives the words for the
error codes 1, 2, 3, 4, 5 and 12 (18.3). `hex()` formats bytes for the monitor.

**`link.h` / `link.cpp`.** `Link` is the interface: `open()`, `close()`, `isOpen()`, `send()`, `flush(ms)` (what was
sent goes out before a close), `drain(ms)` (what still comes is read and dropped before a close) and `describe()`, and
the signals `opened()`, `closed(why)` and `received(bytes)`. `why` is empty for a close that was asked for.

- `TcpLink` turns Nagle off at connect. It does not report a remote close twice. A failed connect is reported
  through `errorOccurred`, and a close while connected through `disconnected`. Its `drain` reads until the device
  is quiet for 20 ms, at most the time given: closed with bytes unread, the connection is reset rather than ended
  (13.8).
- `SerialLink` reports the errors after which the port is gone (`ResourceError`, `PermissionError`,
  `DeviceNotFoundError`, `OpenError`) as `closed(why)`.

The socket or port is the link's child, so it lives in the link's thread.

**`master.h` / `master.cpp`.** `Master` implements the queue and the matching of 18.6 and the timeouts and
keep-alive of 18.7:

- `read()`, `write()` (with acknowledge) and `writeNoAck()`, each with an optional callback taking a `Result`
  (`ok`, `error` code, `message`, `data`, `latencyMs`); `readFrom()`, `writeTo()` and `writeNoAckTo()` name the
  slave. Slave 0 takes only `writeNoAckTo()`: a read or an acknowledged write to it is refused at once. An answer is
  matched by slave, function code, offset and count (18.6).
- `Link::flush(ms)` waits until the bytes sent have left, for a last frame before a close.
- The rule of `nextMustWait()`: **only reads overlap**. A write waits for everything sent before it, and nothing is
  sent while a write is unanswered.
- One timeout timer runs for the oldest pending request, for the time it has left (`armTimeout`). A late answer
  that no longer matches a pending request is reported as `unsolicited`.
- The keep-alive timer: every 400 ms, when nothing is queued or pending, a 2-byte read of `0xA000`.
- Both timers are children of the master (20.7).
- `clear()` empties both lists **before** it runs the "cancelled" callbacks, because a callback may queue anew.
- `Stats`: tx, rx, ok, errors (error answers), timeouts, bad frames, and the last and average latency. The average is
  exponential: 0.95 × old + 0.05 × new, about the last 20 answers.
- Signals: `frameSent(raw)`, `frameReceived(raw, what)` and `unsolicited(frame)`.

*Tested by:* every GUI and API test goes through it. `evre_probe` runs it alone against a device. The API test's
pass-through checks compare frames byte for byte (chapter 26).

### 22.3 Module `io`

**`IoEngine`** (see chapters 19 – 21 for the flows).

- Called through `post()`: `setMap`, `connectTcp`, `connectSerial`, `disconnectLink`, `setLinkOptions`,
  `setPolling`, `setAutoSend(on, prescaler)`, `setPlotted`, `startRecord`, `stopRecord`, `write`, `read`,
  `setMonitor`, `apiStart`, `apiStop`, `apiSetWrites`.
- Called with `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)`: `shutdown`, from `~MainWindow`
  (19.3), so the window waits until the engine has stopped.
- Called from any thread: `table()`, `now()`, `stats()`, `takeSamples()`, `takeFrames(dropped)`, and the static
  `autoSendDividers()` (the 18 prescalers + 1 of 13.8) and `autoSendPrescalerFor(prescaler, baud, streamBytes)` (the
  serial link's cap, public for the tests).
- AUTO_SEND's signals: `autoSendSet(on, hz, err)` (switched, or not and why), `autoSendSlowed(why)` (a serial link
  cut the rate), `autoSendStopped()` (CONFIG shows it cleared). `Stats::autoSend` and `Stats::autoSendHz` give the
  sidebar its rate line.

**Auto send (13.8).** `setAutoSend` keeps the wish; `applyAutoSend()` reads CONFIG and writes it (only after
STATUS, read at connect, shows `CAP_AUTO_SEND`). `onUnsolicited()` takes the frames from `evre::Master::unsolicited`:
a READ_RESP at `0xD000` from the device's slave fills the rows wholly inside it (`streamRows_`), counts for the
frames/s and is a sample tick (`appendSamples`, `writeCsvRow`). When its coverage changes, `rebuildBlocks()` leaves
those rows out of the polls, and `pollDone()` no longer makes samples or CSV rows. `heartbeat()` (the
`heartbeatTimer_`, a child, `Qt::PreciseTimer`, every 100 ms) reads CONFIG, one read at a time. `disconnectLink()`
sends the clearing WRITE straight on the link and flushes it (`Link::flush`) before closing. An answer to an older
switch (`autoSendRequest_`) is ignored.

**Blocks.** `rebuildBlocks()` applies the merge rule of 13.3 to the pollable registers: `sameBank` (the same
256-address bank) and at most `MAX_BLOCK_GAP` = 8 bytes between the end of the block and the next register. Byte
arrays longer than `MAX_POLLED_BYTES` = 32 are not polled or recorded, only read on request, and registers marked
unavailable are left out. The API server and the probe merge with the same rule. The example map gives three
blocks.

A refused merged block is split in place into one read per register (13.4). This is how a device with holes in its
address space is found out, and `tests/fake_device.py` refuses holes on purpose to exercise it. A single register
refused with code 3, 4 or 5 is removed from the blocks and marked unavailable. The table shows "not available".

**Samples.** Between two takes by the window, at most `MAX_SAMPLES` = 100 000 points are kept per register. When
the limit is reached, the oldest tenth goes. This bounds memory when the window does not take them, for example
while it is minimised.

**Monitor lines.** The text of a line is built in the I/O thread. At most `MAX_MONITOR_LINES` = 1500 are kept
between two frames. The oldest are dropped and counted, and the Monitor tab shows "… N frames not shown".

**CSV.** The format is in 12.3. `writeCsvRow()` writes one row per poll (with auto send on, per frame) in the I/O
thread. The stream is flushed
every 250 ms (`updateStats`) and when recording stops.

*Tested by:* the GUI test (polling, stale values, reconnect, writes and read-back, the login sent first, refused
and skipped, auto send, thread warnings) and the API test.

**`RegTable`**: 19.4.

### 22.4 Module `model`

**`device_map`.**

- `DeviceMap::load` (`map_file.cpp`) reads the file with `jsondoc::parse`, which keeps every value's place in the
  text; an overlay reads its base first and merges them. The map it makes, the file's tree and what was loaded
  go into a shared `MapSource` (`DeviceMap::source`); each `RegDef::source` says which loaded register it is.
- `save` / `toJson` build each register's JSON the Studio's way now and when loaded, and compare them key by key
  (`keepAsWritten`): a key that did not change keeps its value from the file. `jsondoc::patchSequence` then writes
  the file again with the unchanged items copied from the text (with the text between them), and only the others
  rendered. An overlay writes `overlayDiff`s against its base. A register copied keeps its `source`: the first one
  claims the file's item (the same address first), the copy is written as a new item from the same keys.
- `checkMap` (`map_check.cpp`) gives the `MapIssue`s of 30.6.
- `sort()` orders registers by address, stable for equal addresses.
- The value functions: `decodeRaw`, `decodeNumber` (applies scale and offset), `formatValue` (the Value column),
  `formatDecoded` (fields `MODE=run  READY`, or the enum name) and `encodeValue` (typed text → bytes, range
  checked). `encodeLoginToken` gives the bytes of a login token; the engine and `evre_probe` both use it.
- The address helpers are `addrText` and `parseAddress`. The block rule is `MAX_BLOCK_GAP`, `sameBank` and
  `isPollable`.

**`Expr`.** `parse(text, resolve, err)` compiles the text once into a postfix program. It uses one recursive-descent
function per precedence level of 9.2: sum, product, unary, power (right-associative) and atom. `eval(inputs)` runs
the program on a fixed stack of 61 values without allocating. An expression nested deeper gives NaN. Names resolve
through a callback, so `Expr` knows nothing of registers. `builtins()` lists its functions (with their parameter
names, kept in the same table as the functions) and constants, for the formula box's completion (9.1, `FormulaCompleter`).

**`MathLines`.**

- `load` and `save` use the setting `chart/math`: one text per line, `name \t unit \t formula \t 1|0`. A missing
  fourth field means shown, for entries written by older versions.
- `compile(registers)` compiles every line. A formula that does not parse, or that names a register the map does
  not have or one that is not numeric, keeps its reason in `error` and is not drawn. A formula that names no
  register at all (`2 * pi`) compiles with an empty `error`, but it has no inputs, so `evaluate` never computes a
  point for it (9.6).
- `registersRead()` lists the registers the active lines read.
- `evaluate(samples, sink)` computes the points for one frame.
- Line i is keyed `FIRST_CHART_KEY + i` (1 << 24) on the chart, clear of every register key (`regKey`). At most 64 lines are drawn.

**`RegisterModel` / `RegisterFilter`.**

- `applyValue` stores a value at once. The view is told of changes at most every 50 ms.
- A value that changed glows for 700 ms. A value older than the stale limit is grey. The limit is
  `max(2 × interval, interval + timeout)`, set by `MainWindow::updateStaleAfter`.
- `setDefinitions(defs)` follows the `MapDocument` (on a bus: every device's registers): rows are matched by
  `RegDef::uid` and `RegDef::slave`; a register read the same way (device, address, type, size) keeps its value,
  plot and log tick; a plotted one whose line changes is drawn again. `rowOfUid` looks among the selected device's
  rows (`setSelectedDevice`), the ones the Map editor's uids belong to.
- `setPlot(row, on)` is the one place a Plot is ticked (the box, Plot shown, the right-click menu, `--plot`): never
  a register that cannot be plotted (`RegDef::canPlot`: a number the map does not mark `"plot": false`), at most
  `plotLimit()`; past it, `false` and `plotLimitReached`. `plottedCount()` counts them. `plotLimitFor(hz)` is the
  limit at a rate: `PLOT_SAMPLES_PER_SECOND` (64 000) / hz, at most `MAX_PLOTTED` (64); `setPlotLimit` sets it
  (`MainWindow::updatePlotLimit`, from `sampleRateHz()`: Auto send's rate, the interval's, or the measured polls)
  and takes the newest (`Row::plotOrder`) off past it, with `plotsTakenOff(names)`.
- Signals: `writeRequested`, `plotChanged`, `plotLimitReached`, `plotsTakenOff` and `structureChanged`.
- `RegisterFilter` matches the search text in the name, description, group, unit or address (`0x00ab` form). It
  also filters by the groups ticked. On Qt 6.10 and later it uses `beginFilterChange()` /
  `endFilterChange(Direction::Rows)`, and before that `invalidateFilter()`.

*Tested by:* the GUI test covers encoding and refusals ("300" into a u8, "abc"), decoded fields and tooltips, groups,
`"login"` through a save and a load, math lines (including one naming a missing register and one saved without its fourth field). The API test covers
`list`, `get` with `decoded`, and the range check of `set`. There are no separate unit tests (chapter 29).

### 22.5 Module `api`

**`ApiServer`** lives in the I/O thread as the engine's child. Chapter 17 describes its protocol.

- `start(evrePort, jsonPort, network, err)` listens on `QHostAddress::LocalHost`, or on `Any` when Network is
  ticked. The ports come from the settings `api/evrePort` (default 1219) and `api/jsonPort` (default 1220).
- `stop()`, `running()`, `setAllowWrites()`, `setAllowDanger()`, `clientCount()` and `requests()`.
- The engine gives it three functions: `isConnected`, `linkName` and `deviceName`.
- JSON commands: `info`, `list`, `get`, `set`, `stream`, `stop`, `read` and `write`, one handler function each.
- Reads go through `readRegisters()`, which merges blocks and writes the table. Streams use one `QTimer` per client
  and skip a tick while a sample is still being read, so a slow device lowers the rate instead of piling up
  requests. `set` checks and encodes every value before the first write (all or nothing), writes in order, and
  replies with the values read back.

*Tested by:* `tests/api_test.py` in three modes (26.3).

### 22.6 Module `ui`

| Class | Responsibility | Main functions and signals | Tested by |
|---|---|---|---|
| `MainWindow` | puts the parts together; the only object that talks to the engine | `applyStartup`, `sync`, `onWriteRequested`, `loadMap`, `pushMap`, `pushPlotted`, `engineRead`, `engineWrite`, `logEvent`, `refreshStatus` | GUI test |
| `Sidebar` | holds the choices and shows the states; the window does the work | getters (`host`, `port`, `token`, `inFlight`, ...), `show...` functions; signals `connectClicked`, `slaveChanged`, `pollingChanged`, `timingChanged`, `inFlightChanged`, `apiServeChanged`, `apiWritesChanged`, ..., `suggestedInFlight` (the poll hint's In flight, never past `IoEngine::MAX_POLLS_UNDER_WAY` x blocks); Auto send (13.8): `autoSendOn`, `autoSendHz`, `setAutoSendHz`, `setAutoSendOn` (without the signal), `setAutoSendOffered(offered, why, shortWhy)` (shortWhy: the greyed rate list's reason), signal `autoSendChanged` | GUI test (Connect, Disconnect, Poll, Auto send) |
| `RegistersTab` | the table and its tools; edits the map on the model | `setConnected`, `setShown`, `refreshStatus`; signals `writeRequested`, `readRequested`, `writesAllowedChanged`, `unplotAllRequested`, `mapEdited`, `statusMessage` | GUI test |
| `ValueDelegate` | draws the value and the ⓘ mark; the editor keeps `base` | `createEditor`, `setEditorData` (once), `setModelData` (`WriteRole`) | GUI test (typed text kept, tooltips) |
| `QuickWritePanel` | writes the selected RW register: typed, a named value, a field; only asks | `showRegister`, `setConnected`, `setBroadcastRule`; signals `writeRequested`, `broadcastRequested` | GUI test (value, bits, danger flag, link state) |
| `BusPanel` | the Devices on the link card; shows, the window does the work | `showBus`, `pickerDevices` (the devices with their state, for the pickers), `namesText`, `setBroadcastBlocked` (the kept broadcasts off, with the reason); signals `newBusClicked`, `openBusClicked`, `saveBusClicked`, `closeBusClicked`, `addDeviceClicked`, `editDeviceClicked`, `removeDeviceClicked`, `deviceSelected`, `presetChosen`, `newPresetClicked`, `removePresetClicked` | GUI test (bus step) |
| `BusPresetDialog` | one broadcast preset; OK only when the value is one the register takes | `result` | GUI test (bus step) |
| `BusDeviceDialog` | one device of a bus; OK only when `checkBus` finds nothing | `result` | map test (`checkBus`) |
| `BitView` | the register drawn bit by bit, 16 bits a line (a number register only, 64 bits at most) | `setRegister`, `setValue`, `bitCell`, `fieldCell`; signal `writeField(lsb, width, value)` | GUI test |
| `RegisterDialog` | one definition by hand | `result()` | screenshot extra `regdlg` |
| `ChartTab` | chart controls, measurements, math lines, chart settings | `setRegisters`, `plotRegister`, `clearLines`, `frame`, `setShown`, `refreshStatus`, `ramNeedText` (static: the memory note's text), `setRegisterLimit`, `infoText` (the info line), `displayState` (the Display menu in words), `measureUpdates` / `measureFullUpdates` (tests: the measurements made, all of the table); signals `mathRegistersChanged`, `unplotAllRequested`, `logged` | GUI test |
| `ChartView` / `ChartWidget` | the chart (chapter 23) | `append`, `frame`, `setWindow`, `setMemory`, `setLive`, `stats`, `range`, `pointsPerLine`, `pointsKept`, `bytesNeeded`, `memoryFull`, `bytesHeld` (tests: the arrays' memory), `setRamBudget` / `ramBudget` (MB; `DEFAULT_RAM_MB`, `MIN_RAM_MB`), `setDrawThreads` (tests: 1 = the GUI thread alone), `setDrawing` / `drawing` / `drawingName` / `drawsOnGpu` / `openingGpu` (who draws the plot; a card opened on a thread), `setHoverValues` / `hoverValues` (the crosshair's box), `refresh` (an update, not of the plot while the card shows it), `plotOnCard` / `gpuPicture` (tests: the card's layer shown, its last frame), `paints` (tests: the frames painted), `legendMeasures` (tests: the legend's chips measured), `FrameBudget` (the frame budget, tests), `stats(keys, cursorsOnly)` (several lines on threads; A and B alone while a cursor is dragged), `draggingCursor` (a cursor held by the mouse), `readoutRowsPerColumn` (static: the crosshair box's rows a column), `readoutBuilds` / `readoutSize` (tests: the crosshair's box made, its size); signals `drawingFailed`, `drawingChanged`, `windowChangedByUser`, `yChangedByUser`, `liveChanged`, `memoryChanged`, `cursorsChanged`; `chartAxisLabel` (a value axis label, its step's decimals) | GUI test (math line value and area, hold and live, memory grows; many lines: threads draw the same picture, a spike in an hour shows, the samples' budget, their arrays' memory within it, the memory needed and its note, the RAM box; bins kept from frame to frame, the GPU's frame the CPU's picture, a picture of the chart drawn by the CPU, the layer away after the window painted and back after two frames, the card opened on a thread, the frame budget's rate, the legend's chips measured once, the mouse painted by the next frame, the crosshair's box at most every 50 ms while the mouse moves, a dragged cursor measured at most every 100 ms, Cursors off clearing A and B, the lines measured on threads, the RAM lowered trimming in one go, the memory full on many lines trimming over a few frames, lines filling together growing at different moments, a dragged cursor's A and B alone until it is let go, the last line off (the layer away once the window has the CPU's whole frame), the mouse over the plot on a card, the crosshair's box made at the values' pace and its size steady, Hover values, the Display menu: Drawing, Normalise, Smooth, Hover values, its marks) |
| `GpuLines` | the chart's plot on a graphics card (23.7) | `adapters` (static), `open`, `name`, `present` (a `Frame`: background, `Layer`s of segments, `Sprite` pictures; into the window's layer at its pixels), `setShown` / `shown` (the layer over the window or not), `lastPicture` (read back: under the layer when it is shown; tests) | GUI test (the frame against the CPU's picture, the layer shown and taken away; skipped without an adapter) |
| `MathLineDialog` | name, unit, formula; OK only when valid | `result()` | GUI test (with its completion) |
| `FormulaCompleter` | the formula box's completion: the word at the cursor, ranked candidates | `rank`, `wordStart`, `shown` | GUI test |
| `MonitorTab` | frame log and single requests | `addFrames`, `showAnswer`, `showSent` (a WRITE without ack), `parseHexBytes` (what a WRITE takes), `setSlave`, `setDevices` (a bus: the devices by name); signals `logFramesToggled`, `readRequested`, `writeRequested` | GUI test (READ, the checks of what is typed, WRITE + ack, WRITE without ack, Enter, Clear) |
| `EventLog` / `Notice` | log tab and daily file; one-line pop-up in the tab bar's row | `add`, `setShown`; signals `unseenChanged`, `popUp`; `Notice::post`, `place`; signals `showLogClicked`, `noRoom` | GUI test (pop-up covers nothing, Show in Log) |
| `FrameClock` | ticks per display refresh | `start`, `stop`; signal `tick` | runs in every test |
| `HelpDialog` | the help pages | `showTopic` | GUI test (every page with its text, the command line page's options); screenshot extra `help` |
| `Theme`, `ui_helpers` | look and shared helpers | `Theme::apply`, `colors`, `isDark`, `switched` (a widget's colours after a switch of the look) | GUI test (contrast of both looks, focus ring, hover edges, check marks, colours after a switch) |

Widgets that tests or the theme find carry fixed object names. Examples: `registers`, `measures`, `quickWrite`,
`qwValue`, `qwEnum`, `bitView`, `groups`, `plotShown`, `hold`, `measure`, `math`, `cursors`, `eventLog`, `notice`,
`detail`, `sideScroll`, `sidebar`, `pill`, `primary`, `danger`, `valuePace`, `formScroll`, `helpTopics`,
`mapIssues`, `autoSend`, `deviceInfo`, `mapDevicesText`, `problem` (the bus dialogs' problem line), `mapSlave`,
`monitorDevice`, `monitorFunction`, `registersDevice`, `liveDevice`, `chartDisplay`, `chartNormalise`, `chartSmooth`,
`chartHoverValues` (the Display menu's actions), `chartRam`, `ramNeed`, `menuTitle` (a menu's title label).
Renaming one breaks the tests or the style sheet.

**The combo boxes' arrow.** The style sheet styles their drop-down (no border, 20 px), and Qt then draws no arrow
of its own; a style sheet shows an image only from a file. `Theme::apply` therefore draws a small chevron in the
theme's muted colour into the user's cache folder (`combo-arrow-<colour>.png`, 40 x 24 px shown at 10 x 6, so it
stays sharp at any scaling; the temp folder when the cache folder cannot be written) and uses it as
`QComboBox::down-arrow`. If the file cannot be written, the combo boxes have no arrow, as before 2026-10-01.

**Accessibility (WCAG 2.1 AA), both looks.** Checked by the GUI test (`uiAudit`), so a colour change that breaks
it fails the build's tests:

- **Contrast.** Every text colour (text, muted, accent, good, warn, bad) is 4.5:1 or more on the window, the panels
  and the cards. White text sits on `accentFill` / `badFill` (the primary and danger buttons, a checked segment,
  the chart's cursor tags), not on `accent` / `bad` (3.2 and 3.0 in the dark look). The edge of an input box or a
  check box (`control`) is 3:1 or more; card and table edges stay subtle (they only group). The light look's tints
  behind coloured text are lighter (alpha 24, not 40) so that text keeps 4.5:1 on them.
- **Scroll bars.** The handle is drawn in `control` too (3:1 on every background; the border colour it had before
  was barely there), 12 px wide, and in `muted` while hovered or dragged.
- **Lists of states** (the Devices card): the text stays in the text colour, readable on a selected row in either
  look; the state is a coloured dot beside it, and *offline* is written too.
- **Focus.** `* { outline: none; }` hides Qt's dotted rectangle; the theme draws its own ring instead, only for the
  focus given from the keyboard (Tab, Shift+Tab, a shortcut), as a browser's `:focus-visible`. An application event
  filter sets `[keyFocus="true"]` on the widget then (and clears it on the next focus change or a click); the style
  sheet's last rules draw the ring by it: a button's edge in the text colour, a check box's row tinted, the selected
  tab tinted, a table's edge. Text boxes keep their accent edge on any focus.
- **Hover.** A combo box, text box or spin box shows the accent edge while the mouse is over it, as a button does
  (not when disabled, read-only or out of range).
- **Check boxes.** Ticked: a white tick on the fill (`check-tick.png`); partly ticked (a bulk edit of registers
  that differ): a dash (`check-dash-<colour>.png`). The state is never told by colour alone. Both are drawn as the
  combo arrow is (`drawnImageFile`).
- **Menus' marks.** A check in a menu (*Normalise*, *Smooth*) is the check box's: a box, filled with the white tick
  when on. The choice of several (*Drawing*) is a tick in the accent colour (`menu-tick-<colour>.png`). Fusion's own
  marks were small and blurred at 225 %. A menu section's title is not drawn by the style (only its line), so a
  titled part of a menu has a label of its own (`QLabel#menuTitle`, muted, in a `QWidgetAction`).
- **A switch of the look.** What writes a theme colour into what it shows (a log line, a value name's bad key, the
  checks' list, a switch's highlight, the chart's Auto boxes) shows it again in the new look's colours
  (`Theme::switched` in its `changeEvent`, or the window's `themeChanged`): a line logged in the dark look was
  near-white on the light one. Text in the plain text colour carries no colour of its own, so it follows by itself.
- **Heights.** Text boxes, spin boxes and combo boxes are one height (20 px of content); their editors inside a
  table, and a combo box's own text field, are not limited.

**Every drop-down has that arrow.** A button that opens a menu (*ƒ Math*, *Export*, the Registers tab's groups)
uses the same chevron as `QPushButton::menu-indicator`, at the right and centred, in place of Fusion's triangle.
Such a button is made with `setButtonMenu(button, menu)` (`ui_helpers`), never with `setMenu` alone: it also sets
the `menuButton` property, which gives the button room for the chevron (`padding-right: 30px`). A GUI check fails
for any button with a menu that was not made so.

## 23. The chart renderer

`ChartView` (`src/ui/chart_widget.*`) is a raster widget. It paints every pixel at every display frame, so it sets
`Qt::WA_OpaquePaintEvent`. `ChartWidget` wraps it for the Chart tab. Chapter 7 describes the chart as a user sees
it.

### 23.1 Data structures

| Structure | Holds |
|---|---|
| `Series` | name, unit, colour; `times` and `values` (rising times); `chunks[4]`: one `Chunk` per full 8, 64, 512 and 4096 samples (`CHUNK_SIZE`); the last value for the legend |
| `Chunk` | `t0`, `t1`, `min`, `max`, `first`, `last` of 8, 64, 512 or 4096 consecutive samples |
| `series_` | `QMap<int, Series>`: key = `regKey(slave, address)` (slave << 16 | address) or `FIRST_CHART_KEY + i` (1 << 24) for math line i |
| `Bin` | one pixel column: its column number, sample count, first and last time, first, last, min and max value |
| `BinnedLine` | one line's bins for a span, and the min and max of what lies inside the span |
| `Axes` | a time span and a value range mapped onto a rectangle: the plot, or the memory strip |

A chunk's index in its level is simply `sample index / its size`. This holds only if the memory always drops whole
chunks of the largest level, 4096 samples (23.3). `addChunks` makes them as the samples come: the smallest from 8
samples, each larger one from the 8 chunks below it, so a sample costs about one comparison per level.

### 23.2 Binning on absolute time

For each frame, each line is reduced to one bin per pixel column (`binSeries`). A sample's column is
`floor(t / columnSeconds)`, where `columnSeconds = window / plot width in logical pixels`. Columns are counted from
time zero, not from the left edge of the view. A sample therefore stays in the same column while the view scrolls,
and a column's min and max do not change from frame to frame. Binning relative to the edge would make the line
shimmer.

The bins start one sample before the view and end one sample after it, so the line enters and leaves at the edges.

`toPolyline` turns bins into points:

- A column with one or two samples keeps them at their exact times. The line is smooth at slow polls.
- A fuller column becomes a vertical stroke at the column's centre: the first value, then the min and the max (the
  max first when the line falls), then the last value.
- On the plot (not the memory strip) three things keep the points few, the same pixels drawn:
  - a full column whose swing is under one device pixel is one point (its last value): a slow line costs one point
    a column, not four;
  - a column whose swing is taller than the line is wide becomes a **bar**, a plain fill the line's width from the
    min to the max (`fillBands`, antialiased: a half-pixel edge comes out the same on a stripe), and the polyline goes through its first and last value only. A
    line that is mostly bars is joined by a single stroke (the bars give it its width): a fast, noisy line costs a
    fill per column instead of five antialiased strokes over its whole swing;
  - a level run is one segment: while the line stays at the same height, its end moves on instead of a point being
    added. A register that does not change costs two points.

### 23.3 Chunks and memory trimming

`binSeries` bins chunks instead of samples where a column holds many samples. It picks the largest level whose
chunk is at most half a column (more than two chunks per column), so a chunk falls in one column and the min and
max land where they belong; on the memory strip (`overview`) a chunk may fill a whole column. From each sample it
takes the largest chunk that starts there, ends inside the span and lies in one column, down to single samples at
the column edges: a column's bin is exactly its own samples. The cost
then follows the number of pixels, not the number of samples: a 60 s view of 500 Hz samples bins about 3 750
chunks of 8 per line instead of 30 000 samples, an hour about 3 500 chunks of 512 instead of 1.8 million. A
one-sample spike in an hour of samples is kept by every level above it, and shows (GUI check).

The view's bins are also kept from frame to frame (`Series::viewBins`, `binViewSeries`). Columns are counted on
absolute time, so a column that is complete never changes: each frame bins only the samples after the last complete
column (the open last column again), and a live view costs the new samples, not the window. The kept bins are let go
when they cannot hold: another column width (the window, the plot's size), the lines changed (`seriesGeneration_`),
or the view starts before them (held, dragged back), and the view is binned from its start once. `Series::dropped`
counts the samples the memory let go, so a bin's first sample (`Bin::firstSample`) is a number that does not move.
64 lines of 1000 Hz with a 60 s view: binning went from 6.5 ms a frame to about 2.

Memory is trimmed in two places:

- **Engine** (22.3): at most 100 000 points per register between two frames.
- **Chart** (`dropExpired`, run on every `append`): trimming starts when the oldest sample is more than
  `memory × 1.05 + 0.5 s` old, or when the line holds its share of the budget: `pointsPerLine()` = the RAM
  (`setRamBudget`, MB, 2048 by default) divided by `BYTES_PER_SAMPLE` (23: time and value, 16 bytes, and the
  chunks' share) and by the lines, at least 65 536 and at most `MAX_POINTS` = 16 000 000 (trimming moves a line's
  arrays: 256 MB at most). It then drops everything older than `memory`, and at least an eighth when the line is full,
  **rounded down to whole chunks of 4096**: about a twentieth at a time by age (the 5 % it is let grow past the
  memory), and by the budget from a sixteenth short of the share down to seven eighths of it, not a sample at a
  time, which would move the whole vector at every poll. With the budget full, the time kept therefore saws between
  seven eighths of what fits and all of it (12 min, then 10.5, then 12 again). The lines fill together, and trimmed
  together they moved 0.9 GB in one frame (65 lines at RAM 1 GB: 85 ms, five frames lost, every 86 s): a frame
  moves at most 4 million samples by trims (`TRIM_PER_FRAME`, about 5 ms; `movedThisFrame_`, reset by `frame()`),
  the other lines waiting their turn between the sixteenth short and the share, which they never pass. A line further past its share (the RAM lowered, lines added) drops all of the excess and
  the eighth at once, and lets its room go with one copy of what stays (`dropFront`): an eighth at each sample,
  each moving the whole line, held the window 3 s when 1 GB full of 65 lines was set to 512 MB. A line trimmed by the budget sets `memoryFull()`, and the memory
  strip says *memory full* (7.4) until the samples are cleared or the lines change.
- **The arrays keep to the budget.** What is dropped moves the rest of a line's arrays to their front
  (`dropFront`), and they grow by about doubling only up to the line's share (`roomForOne`), each line by its own
  step, 2 to 2.44 times (`spread`, by the order the lines came), so lines that fill together grow at different
  moments (all at once, 64 lines at RAM 1 GB moved 0.5 GB in one frame at their last growth); a share that shrank (more
  lines, less RAM) lets the room go at the next trim. Qt's vectors keep the room freed at their front, and when the
  end comes with two thirds of them in use they double rather than move: the samples took up to twice the RAM set
  (2 GB for 1 GB, 64 lines at 1000 Hz). `bytesHeld()` (tests) adds up what the arrays hold. Measured 2026-10-04,
  RAM 1 GB, 64 lines of 1000 Hz (a fake device), Memory 1 h: full after about 12 minutes, then flat at 1.14 GB of
  private working set (Task Manager's Memory) for the 13 minutes after.

The memory is 1 s to 86 400 s (`MIN_MEMORY`, `MAX_SPAN`). The view is 0.001 s (with the wheel; the shortest length
that can be typed is 0.01 s) to 86 400 s. A view longer than the memory makes the memory grow to it.

### 23.4 The smooth delay

Samples arrive in bursts: per poll, per display frame, per TCP packet. If the right edge were exactly "now", the
newest part of the line would jitter in and out of view. With Smooth on, the edge is `now − delay`.
`updateDelay()` computes the delay:

| Constant | Value | Meaning |
|---|---|---|
| gap | now − the newest sample of any line | measured each frame; gaps < 0 or > 1 s are ignored (no data is coming) |
| `PEAK_DECAY` | 0.05 s per s | the peak gap is forgotten this fast |
| target | `min(0.5 s, peak gap × 1.1 + 3 ms)` | the delay aimed for |
| `RISE_TIME` | 0.15 s | time constant when the delay must grow |
| `FALL_TIME` | 2.0 s | time constant when it may shrink |

The delay follows slowly, so the scroll speed does not wobble. It is at most 0.5 s. A slow poll simply shows its gap.
Smooth off sets the delay to 0. The chart info line shows the delay (`delay N ms`).

### 23.5 Y range

- **Auto** (`followData`). The range of what the view shows, with 8 % margin above and below. A flat line
  (range < 1e-9) is padded by `max(5 % of |value|, 0.5)`. At the first frame and while held, the range jumps
  there. While live, it grows at once, so nothing is cut off, and shrinks with a 0.4 s time constant, so it does not
  jump.
- **Manual.** The limits typed, or set with Ctrl + wheel, which zooms around the mouse and switches to Manual. A
  double-click goes back to Auto.
- **Normalise.** Each line is scaled into 0..1 by its own range in the view, and the axis shows −8 % … 108 %. A
  line flatter than 1e-12 gets a range of one unit around it (`widenFlatRange`), and the crosshair dot uses the same
  scaling.

### 23.6 The drawing fast path

`paintEvent` (`paintFrame`) draws in this order:

1. card
2. grid (on a card: its labels only; the card draws the plot, 23.7)
3. cursor span, then the lines (not on a card)
4. cursors (not on a card)
5. memory strip
6. legend (the chips clipped to their part of the row, then the scroll bar and arrows when they overflow)
7. crosshair (not on a card)
8. state text

The following choices keep a frame cheap on a high-DPI screen:

- **Lines as cosmetic polylines.** Qt's raster engine has a fast path for 1-device-pixel antialiased cosmetic
  lines, and none for a wide antialiased stroke. `strokePolyline` draws the polyline with a cosmetic pen of width 0,
  `copies = max(2, round(1.5 × devicePixelRatio))` times. It shifts the copies by one device pixel side by side, and
  also above and below: a plus shape about 1.5 logical pixels thick. One wide antialiased stroke cost more than
  20 ms a frame for four lines at 225 % scaling on a 4K screen (comment in `chart_widget.h`). The memory strip uses
  a single thin copy.
- **The card.** A plain fill, then four small corner shapes in the window's colour. One antialiased rounded
  rectangle the size of the chart costs milliseconds at 4K.
- **The grid** is drawn without antialiasing: crisp 1 px lines, cheaper. Time grid lines sit at multiples of the
  time step on the time base, so they are fixed to wall-clock times and move with the data. Labels are `HH:mm:ss`,
  with `.z`, `.zz` or `.zzz` as the step needs.
- **Only when visible.** `ChartView::frame()` asks for a frame only while the widget is visible. A hidden Chart tab
  costs nothing but the `append` calls.
- **Paced by the frames.** While frames come (`frame()` called in the last 250 ms, `FRAMES_STOPPED_MS`), a change
  (`refresh()`: a mouse move, a drag, the wheel, a setting) is painted by the next frame, not at once. The mouse
  moves up to 1000 times a second: a frame for each painted the chart past the display's rate and past the frame
  budget; with 64 lines on a card it held 35 frames a second where 60 were drawn, and the Smooth delay went up as
  frames came between the samples (2026-10-05). Without frames (a chart alone, the tests) a change is painted at
  once.
- **Many lines on threads.** The chart has its own thread pool (`pool_`: the CPU's threads less three, 1 to 8).
  `inParallel(count, job, jobsPerThread)` runs the jobs on it and on the GUI thread, and returns when all are
  done, so nothing else touches `series_` meanwhile. The GUI thread waits only for the jobs other threads took,
  never for a thread still waking up (on Windows that can take milliseconds; waiting for the pool cost 2 lines
  8 ms a frame); a thread that starts late finds no job left and ends. Fewer than 4 jobs a thread are not worth
  waking one: with a few lines the GUI thread does it all, as before. The lines are binned and turned into
  polylines one line per job; a stripe (below) is a job of its own on each thread. With more than
  20 000 points (`POINTS_PER_STRIPE`) the plot is cut into vertical stripes, one per thread, on whole device
  pixels. Each stripe is drawn into an image of its own, every line's part in it, 8 device pixels past its edges
  (an image's own edge pixels are antialiased a little differently), and the stripes go side by side.
  `prepareTile` maps the widget's coordinates into the image by the painter's own device transform and a shift by
  whole pixels only, so a line takes exactly the pixels it would drawn directly: a widget need not start on a
  whole device pixel (at 225 % a widget at x = 13 starts at 29.25). A GUI check compares 60 lines drawn so with
  the same drawn on one thread (`setDrawThreads(1)`); where several lines cross, blending them on a stripe first
  rounds a few pixels apart, never a column of them. 98 lines of 500 Hz samples with a 60 s view take about 5 ms
  a frame at 225 % on a 6-core laptop CPU (they took 57 ms, growing to 290 ms as the view filled, drawn on one
  thread without the smaller chunks).
- **The memory strip as an image.** Its lines are drawn into an image (`stripImage_`, on the same device grid),
  again only when the data has moved a pixel on the strip (`strip.columnSeconds()`), the lines or the memory
  changed, or the size; between those, the image is drawn. The view's mark on it is drawn at every frame.
- **The legend** draws only the chips in its part of the row, not every chip under a clip, into a picture
  (`legendImage_`) made again only when its key changes: the lines, the values' tick, the scroll, the size,
  the scaling or the theme. Between those the picture is drawn (1.3 ms a frame at 4K drawn each time). The chips'
  widths (`legendLayout`, also used at every mouse move for the pointer's shape) are measured once while the lines
  and the font stay (`chipWidths_`): 64 names measured at every frame and every mouse move held the chart near 52
  frames a second with the mouse moving.
- **The crosshair's box while the mouse moves** is made again at most every 50 ms (`READOUT_FOLLOW_MS`), the box
  and the dots following the mouse at every frame: made at every frame, its 4.5 ms at 4K held the chart near 46
  frames a second.
- **The crosshair's box as a picture.** Its frame, dots, names and units are a base picture
  (`readoutBase`), made again only when the lines read, the plot's height, the scaling or the theme change; at the
  values' pace (`valuesTick_`, the legend's) or when the mouse moves, the time and the numbers are written on a
  copy of it (`readoutPicture`, into the last box's memory when it is the same size: a fresh 6 MB picture at 4K
  cost more than the copy); every other frame draws the same picture beside the mouse, its opaque middle
  copied and only its corner rows and side edges blended (`drawBoxPicture`). The dots on the lines
  are stamped from one small picture per colour (`dotPicture`). 64 values laid out and drawn at every frame took
  7 ms at 4K and 225 % (the frame budget then held the chart near 30 fps while the mouse was over it); now about
  2 ms.
- **The frame budget.** A credit of the GUI thread's time (`ChartView::FrameBudget`): each `frame()` earns 60 % of
  the time since the last (`FRAME_SHARE`), a paint spends what it took (at most 30 ms, `BUDGET_SAMPLE_MS`), and a
  frame is painted while the credit is not short; at most one frame's share is kept. So the chart takes at most
  about 60 % of the GUI thread and the window answers at any number of lines, and the rate falls off as the frames
  cost more: 11 ms frames at 60 Hz skip about one in twelve (55 a second), 20 ms ones every other refresh. Before
  (2026-10-05), every frame over 10 ms on average made the next wait as long as it took, and the refresh rounded that
  up to a whole refresh: 11.5 ms frames ran at 35 a second. One slow frame among quicker ones (a resize, a theme
  switch, the memory strip drawn again) holds back a frame or two, not seconds: it counts 30 ms or twice the frames'
  average at most (`averageMs`); slow frames all along count in full, so they too stay within the share (counted as
  30 ms, the CPU's 41 ms frames took 80 % of the thread).

### 23.7 The plot on a graphics card

An OpenGL widget in the window drew the lines faster, but Qt then composed the whole window on the GPU, and every
label update elsewhere cost a frame or two (2026-09-24). So the window stays a raster window, and the plot is a
layer of the window over the chart (DirectComposition), which a card draws and shows (`GpuLines`,
`src/ui/gpu_lines.*`):

- **The adapter, by name.** `GpuLines::adapters()` lists the system's adapters (DXGI): a dedicated card has memory
  of its own (512 MB at least) and is not the processor's; Microsoft's software one is left out. The Direct3D 11
  device is made on the one chosen (Drawing, 7.2) on a thread of its own (`opener_`): making it wakes the card,
  0.8 s on the Optimus laptop the first time and 0.13 s awake (the shaders: 10 ms), and on the window's thread it
  held the chart and the window that long. The CPU draws meanwhile (`openingGpu`), the card takes over when it is
  ready (`gpuOpened`, `drawingChanged`), and a choice made again before lets the card being opened go. On an
  Optimus laptop the screen is the processor's graphics: the system copies each frame across to it.
- **The layer.** A DirectComposition visual over all of the window's own pixels (its target on the top-level
  window, topmost), its content a swap chain (flip model, two buffers) the card presents into. It covers the plot
  and 2 px around it (the lines' antialiasing: `layerRect()`), placed on whole pixels of the window's client area
  (`layerPixels_`; the chart's coordinates times the scaling may not be whole). `Present` never waits: a frame the
  system is still busy with is dropped, the next comes a few ms later. The chart itself paints only what is around
  it, and asks Qt to paint again only that part (`refresh()`, for every change of the chart), so the plot is
  neither copied by Qt nor sent to the screen by it (at 4K that was half of a frame).
- **Shown and taken away with the window.** A layer, not a native child window: Windows took a child window away
  at once when its tab was hidden, a tenth of a second before Qt had painted the new tab, and the window's old
  pixels showed there meanwhile; shown again, it showed its frame of when the chart was left. The layer is shown
  and taken away when the chart says (`GpuLines::setShown`), at the screen's next refresh with what the window
  painted just before: when the chart is hidden it stays until the window has painted what is there now
  (`hideEvent`: the tabs' area repainted at once, then the layer away). The system shows a layer's change at its
  next refresh but what GDI drew (Qt's window) a refresh later, so the layer goes after that refresh (`GdiFlush`,
  `DwmFlush`): taken away at once, the screen showed the new tab where the layer was and the old one around it for
  a frame (seen in a 60 frames-a-second recording of the screen); when the chart is shown again, or a card
  opened, its frames are drawn by the card and read back (`lastPicture`) into the window under the layer, and the
  layer is shown once the second of them is on the window (`paintFrame`, `LAYER_AFTER_FRAMES`): by then the first
  is surely done on the card (a frame not done yet when the screen shows the layer would leave its old frame
  there, from when the chart was left; on an Optimus laptop each frame is also copied to the processor's
  graphics). With no line left, the chart's own hint is drawn by the CPU and the layer goes the same way, the whole
  chart painted again first, the plot's part too (`repaint()`: a frame painted around the plot alone, as while the
  layer is there with `refresh()`, left the old lines in the window, which showed for a frame where the layer had
  been); a Drawing change paints the CPU's frame first too, then the layer goes after that refresh.
- **All of the plot.** `plotOnGpu` lists, in the layer's pixels, the background, the grid lines (crisp: on whole
  pixels, as the CPU's 1 px pen), the cursors' span (a bar as tall as the plot), the lines, the cursors' and the
  crosshair's dashed lines (`GpuLines::Layer`: segments of one width), then the pictures over them
  (`GpuLines::Sprite`): the cursors' tags (`tagPicture`), the crosshair's dots (`dotPicture`) and its box
  (`readout_`). A picture stays on the card while it is the same `QImage` (`cacheKey`): the dots and tags once, the
  box when it is made again (23.6).
- **A segment is an instance.** Each line's polyline (23.2, without the bars: a card draws the strokes) becomes
  segments of 20 bytes (`GpuLines::Segment`: two points in the layer's pixels, the colour); the vertex shader makes
  each a quad the line's width (as the CPU's copies: `max(2, round(1.5 × devicePixelRatio))` pixels) and half the
  width longer at each end (`caps`), so the segments of a line overlap at its points. A picture is a quad too, its
  pixels copied one to one. 4x antialiasing, resolved into the layer's buffer. The shaders are HLSL compiled at run
  time (`D3DCompile`): no shader files.
- **The mouse** is the chart's, as without a card: a layer is no window, nothing is between the mouse and the chart.
- **A picture of the chart** (`grab()`, `EVRE_SHOT`, the tests) is drawn by the CPU, the plot too: `paintEvent`
  draws on the card only when it paints into the window's backing store. The tests read the card's last frame back
  with `gpuPicture()` (`GpuLines::lastPicture`).
- **Popups without animations.** Qt's popup animations are off (`Theme::apply`): a drop-down's slide went away
  before the drop-down was drawn, so it flashed, and the fades copy the screen the old way (GDI), which does not
  hold the layer.
- **A failure** (a lost device, a driver reset, no swap chain or layer) closes the card and its layer: the CPU
  draws from that frame on, and the Log says why (`drawingFailed`).

Measured 2026-10-04, 64 lines of 1000 Hz, a 60 s view, a maximized window on a 4K screen at 225 %, Measure on,
NVIDIA Quadro T1000: about 3.3 ms a frame and a steady 60 fps (the binning 0.3, the card's list and the present
0.5, what is around the plot 2.5); with the mouse moving over the chart and cursors set, about 5 ms (the box
made again 20 to 30 times a second: at most about 3 ms each, then sent to the card) and 59 to 60 fps. The CPU
drew the same in about 15 ms, 30 fps under the frame budget. Before the card showed the plot itself, its picture
came back to the chart (a frame late) and was drawn into the window: about 6.5 ms a frame, 8.5 with the mouse, and
about 45 fps, the window's own copy of the plot to the screen being the rest. The processor's graphics (Intel UHD
630) drew the lines in about 26 ms at 4K, slower than the CPU: Auto does not choose it.

### 23.8 Measurements and math lines

`ChartView::stats(key)` computes the values of chapter 8 over cursors A → B, or over the view without cursors:

- the value at A and B, by linear interpolation;
- min and max;
- the area ∫ v dt, by trapezoids between samples;
- mean = area ÷ time;
- RMS = √(∫ v² dt ÷ time).

The table shows the area in unit·s and unit·h (8.3). It is updated every 250 ms while the Chart tab is shown and
Measure is on, when the tab is shown, and when the cursors move: at once, then at most every 100 ms while they move
(`ChartTab::measureSoon`, `MEASURE_FOLLOW_MS`), the last place always measured. While a cursor is dragged, only
A, B and B − A follow it (`stats(keys, cursorsOnly)`, the columns not fitted again); the rest over A → B comes once it
is let go (`cursorsChanged` at the release) and at the 250 ms pace: all of it at every step held the chart near 51
frames a second with 64 lines. A dragged cursor moves at every
mouse move, and 64 lines over 5 minutes of 1000 Hz samples measured at each held the chart near 17 frames a second.
The lines are measured on the chart's threads (`stats(keys)`, as `inParallel`), each as `stats(key)` alone.

Math lines are evaluated in the GUI thread from the frame's samples (`MathLines::evaluate`). A point is made for
each sample of the line's first input register. The other inputs are matched by **identical time**, which works
because a poll gives all its registers the same time (20.8). A poll that lacks one of the inputs gives no point.
Example: the line `P [W] = SUPPLY_V * SUPPLY_I` needs both registers plotted or sampled. `pushPlotted()` makes sure
they are sampled even when they are not plotted.

### 23.9 Frame budget

At 60 Hz, a frame lasts 16.7 ms. The work in the GUI thread per frame is:

- `sync()`: a snapshot, the rows that moved, the samples, the monitor lines;
- the chart's paint;
- the table's repaint, at most 20 times a second.

The chart measures itself. The info line on the Chart tab shows frames per second, the average paint time (a
running average, 0.9 old + 0.1 new) and the smooth delay. A paint time close to the frame time shows up as fps below
the display rate. Samples are not lost when frames drop: the engine keeps them until the next take.

## 24. Extending the Studio

Each change touches code, tests, the help pages (`src/ui/help_dialog.cpp`) and this document. The lists below say
where.

### 24.1 An API command

1. Declare `cmdName(...)` in `ApiServer` (`api_server.h`, the JSON section) and implement it in `api_server.cpp`.
   Use `reply`, `replyOk` or `fail`. For an answer that comes later, capture `WeakClient(client)` and check
   `stillThere()` first.
2. Dispatch it in `onJsonLine()`. It goes before the `isConnected()` check only if it works without a device. Add
   it to the list in the "unknown cmd" error text.
3. Device access goes through `master_` (the one queue). Register values go through `readRegisters()`, so the table
   sees them. Writes must pass `writeRefusal()`.
4. Tests: a check in `tests/api_test.py`, in the modes it concerns.
5. Documentation: the API help page, `examples/README.md`, chapter 17, and Part III if the flow changes.

### 24.2 A register type

1. Add the `RegType` value. Update `typeSize`, `typeName`, `parseType` (map name and C name), `integerRange`,
   `decodeRaw`, `decodeNumber`, `formatValue` and `encodeValue` in `device_map.cpp`.
2. Check the functions that look at the type: `RegDef::isNumeric` and `isFloat`, `valueJson` in `api_server.cpp`,
   `isSigned` and `bitsWritable` in `quick_write_panel.cpp`, and `ALL_TYPES` in `register_dialog.cpp`.
3. The fake devices: `PACKING` in `tests/fake_device.py` and `animate()` in `tests/fake_device_fast.cpp`.
4. Documentation: chapter 16, the "Device maps" help page, and the type list in the header comment of
   `device_map.h`.

### 24.3 A link type

1. Derive from `evre::Link`. Implement `open`, `close`, `isOpen`, `send` and `describe`, and emit `opened`,
   `closed(why)` (empty `why` for a close that was asked for) and `received`. Make the transport object the link's
   child.
2. Add `IoEngine::connectX(...)`, which calls `attach(new XLink(...), token)`. The window posts `connectX`, so it
   runs in the I/O thread and the link is created there.
3. Add the fields to the Sidebar's connection card, with settings keys under `link/`. Keep In flight per link type
   (`savedInFlight`). Handle the new link in `MainWindow::connectLink`.
4. Optional: a command-line option (`main.cpp`, `MainWindow::Startup`, `applyStartup`) and support in `evre_probe`
   (`makeLink`).
5. Implement `flush(ms)` when the transport buffers what is sent: the engine flushes the last frame (auto send off)
   before it closes the link.
6. Tests: at least a GUI test path if a fake device can serve it.
7. Documentation: the "Connecting" help page, chapter 3, and Part III.

### 24.4 A tab

1. Write the tab as its own widget class in `src/ui/`. It talks to the window through signals, like `MonitorTab`.
   Add its files to `evre_studio_core` in `CMakeLists.txt`.
2. Build it in a `buildXTab()` of `MainWindow` and add it with `addTab`. The `MainWindow::Tab` enum must stay in the
   order of the `addTab` calls. The Log tab is added last by `buildEventLog()`. The GUI test selects the Chart tab by
   index 1 and the Log tab by `TabLog`, so a tab added before them changes those indexes.
3. If the tab only needs to work while shown, connect `QTabWidget::currentChanged` to a `setShown(bool)`, as the
   other tabs do.
4. Optional: accept its name in `--tab` (`applyStartup`) and add an `EVRE_SHOT` extra.
5. Documentation: help, Part I, and Part III.

### 24.5 A setting

1. Choose a key `group/name` next to the existing ones (`link/`, `poll/`, `api/`, `ui/`, `chart/`, `map/`) and a
   default. The default is what a fresh install gets. **Never rename or re-purpose an existing key:** saved settings
   must keep loading.
2. Read it where the value is restored (`Sidebar::restoreLinkSettings`, `restoreApiSettings`,
   `ChartTab::restoreSettings`, or at the widget's creation). Save it either at close (`Sidebar::saveSettings`,
   `MainWindow::closeEvent`) or on every change, as the `chart/` and `ui/` switches are.
3. Anything that can cause harm (a write switch, a token) is not stored.
4. Tests run under their own application names (26.1), so a new setting does not touch the user's.
5. Documentation: the settings table in 14.3.

### 24.6 General rules for a change

- Behaviour and visible text follow the conventions in the code: `tr()` for user text, `QStringLiteral` for any
  non-ASCII text, and `noMnemonic()` for map text on buttons, check boxes and menus.
- Device access only in the I/O thread, through `post()`. No `QObject` on the I/O thread without a moved parent
  (20.7).
- Build warning-free with GCC on Windows and Linux.
- Run the GUI test and the API test in all three modes (chapter 26).

## 25. Building

### 25.1 Requirements

| Item | Version |
|---|---|
| CMake | 3.21 or later |
| C++ compiler | C++17. Tested with GCC (MinGW on Windows, GCC on Linux). Clang and MSVC are not tested; MSVC in particular defines `M_PI` and `M_E`, which `src/model/expr.cpp` uses, only with `_USE_MATH_DEFINES`, so it may need that define. MSVC keeps its own warning defaults. |
| Qt 6 | 6.5 or later, components Widgets, Network, SerialPort, Test |
| Generator | Ninja (recommended) or any CMake generator |
| Windows only | the system libraries `winmm` (1 ms timer resolution) and `dwmapi` (`DwmFlush`), linked by the build |

GCC and Clang build with `-Wall -Wextra`. The code builds without warnings with the Qt 6.8 MinGW toolchain on
Windows and with Qt 6.10 and GCC on Linux.

### 25.2 Windows (Qt + MinGW + CMake + Ninja)

Install Qt 6 for MinGW 64-bit with the Qt Serial Port module. The Qt online installer also offers MinGW, CMake and
Ninja under "Developer and Designer Tools". Put them on `PATH` and configure. `<Qt>` is the Qt install folder and
`<ver>` the Qt version.

```sh
export PATH="<Qt>/Tools/mingw1310_64/bin:<Qt>/Tools/CMake_64/bin:<Qt>/Tools/Ninja:<Qt>/<ver>/mingw_64/bin:$PATH"
cmake -S . -B C:/b/evre -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>/<ver>/mingw_64
cmake --build C:/b/evre
```

The folder name `mingw1310_64` depends on the MinGW version installed.

**Use a short build path.** CMake's object file paths plus a deep source or build folder can exceed Windows' limit
of 260 characters, and the build then fails with odd "file not found" errors. A build folder such as `C:/b/evre`
avoids it. Keep the build folder out of folders that a sync tool mirrors, because the build writes many small files.

**Deployment.** The executable needs the Qt runtime beside it:

```sh
windeployqt --release --no-translations C:/b/evre/EVReStudio.exe
```

Ship the executable, what `windeployqt` copied, and the `maps/` folder beside it. The build copies `maps/` next to
the executable after every build of `EVReStudio`. The program writes its log to `logs/` beside itself, or to the
user's application data folder when that folder is not writable (11.2). `EVReStudio` is a GUI executable (`WIN32`)
with no console. The test programs are console programs.

### 25.3 Linux

Use the distribution's Qt 6 packages. On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-serialport-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/EVReStudio
```

`CMakeLists.txt` asks only for Widgets, Network, SerialPort and Test, which `qt6-base-dev` and `qt6-serialport-dev`
provide. The GUI test needs a display; on a machine without one, run it under a virtual X server such as `xvfb-run`. Use `xvfb-run` also when the test's window would share a desktop someone works on (WSLg on Windows): a click elsewhere takes the keyboard focus, a cell editor then closes and commits, and a step can stop at a question nobody answers.

A serial port needs read and write access for the user. On most distributions that means membership in the group
that owns `/dev/ttyACM*` and `/dev/ttyUSB*` (often `dialout`).

**Deployment.** There is no install target. Run the program from the build folder, or copy `EVReStudio` together
with `maps/` beside it. The target machine needs the same Qt 6 runtime packages, or a bundle made with a third-party
tool.

### 25.4 CMake targets and options

| Target | Kind | Built from | Notes |
|---|---|---|---|
| `evre_protocol` | static library | `src/evre/*` (with `registers.h`), `src/model/device_map.*` and `src/model/bus_file.*` | Qt Core, Network, SerialPort only; the core, the probe and the fast fake device link it |
| `evre_studio_core` | static library | every other file under `src/` except `main.cpp` | links `evre_protocol`; the program and the GUI test link it; a new source file is one more line here (or in `evre_protocol`) |
| `evre_maps` | custom target | `maps/` | copies `maps/` beside the programs at every build, so an edited map is there even when nothing was linked again |
| `EVReStudio` | executable | `src/main.cpp` + core | depends on `evre_maps` |
| `evre_gui_test` | executable | `tests/gui_test.cpp` + core + Qt Test | depends on `evre_maps`: it opens the maps beside it |
| `evre_probe` | executable | `tests/evre_probe.cpp` + `evre_protocol` | Qt Core, Network, SerialPort only |
| `evre` | executable | `cli/evre.cpp` + `evre_protocol` | the command-line tool (chapter 34) |
| `evre-sim` | executable | `cli/evre_sim.cpp` + `evre_protocol` | the simulator (chapter 35) |
| `evre_map_test` | executable | `tests/map_test.cpp` + `evre_protocol` + Qt Test | depends on `evre_maps` |
| `evre_fake_fast` | executable | `tests/fake_device_fast.cpp` + `evre_protocol` | depends on `evre_maps`: without a map argument it opens the example map beside it |

The project defines no options of its own. The usual CMake variables apply: `CMAKE_BUILD_TYPE`, `CMAKE_PREFIX_PATH`
(where Qt is) and the generator. The version comes from `project(EVReStudio VERSION 1.0.0)`. It reaches the code as
`EVRE_STUDIO_VERSION`, is shown in the sidebar's footer, and is printed by `--version`. AUTOMOC is on.

## 26. Tests

### 26.1 Overview

| Program | Covers | Needs | Writes? |
|---|---|---|---|
| `evre_gui_test` | the real window, end to end | `tests/fake_device.py` on 127.0.0.1:1210 | yes, a danger register included |
| `tests/api_test.py` | the API server, three modes | the Studio connected to the fake device, API on | yes, in the modes `writes` and `danger` |
| `evre_probe` | the protocol core against a real device | a device or gateway | no register values; only the login, if asked |
| `tests/fake_device.py` | a fake device for the tests and for trying the Studio | Python 3, standard library | serves writes |
| `evre_fake_fast` | a fast fake device, to measure the Studio itself | built with the project | serves writes |
| `tests/fake_login_test.py` | the login of both fake devices, and the probe's | the build folder (`evre_fake_fast`, `evre_probe`) | only to the fake devices' login register |
| `evre_map_test` | the map files, the exports (26.7) | nothing; `gcc` and `python` on PATH compile and import the exports | only its own temporary folder |
| `tests/cli_test.py` | `evre`: validate, export, info, read, dump, watch, write, the token, the refusals; `--bus` and `broadcast` on two devices | the build folder; it starts `fake_device.py` on 1212, and `evre_fake_fast` as two devices on 1232, itself | yes, to its own fake devices |
| `tests/sim_test.py` | `evre-sim`: defaults, moving values, wo, ro, action, w1c, ro fields, strict, login, persist | the build folder (`evre-sim`, `evre`); it starts the simulator on 1213 itself | yes, to its own simulator |
| `tests/schema_test.py` | the maps against the JSON Schema (26.7) | the `jsonschema` package (SKIP without it) | no |
| `tests/device_table_test.py` | the device table export (32.6) compiled with the EVRe library and run; the refusals | the build folder (`evre`); `g++` and the library (`--lib`, `EVRE_LIB`, or `../lib` in the EVRe repository) for the compile part, SKIP without | only its own temporary folder |

**The rule: the GUI test and the API test write only to a fake device.** They write registers of the device bank
and set a register marked danger. Never point them at a real device. `evre_probe` is the only test program meant for
real hardware, and it reads only.

Settings stay apart from the user's:

- The GUI test runs as organisation `teknile`, application `EVReStudioTest`.
- A run with `EVRE_SHOT` set uses the application name `EVReStudio-test`.

A script that runs the tests should stop only the processes it started, by their process id, never by name or
window title. A user may have the Studio open at the same time. The tests use the fixed ports 1210, 1211 (the fake devices'
login test), 1219 and 1220, so only one test run can be active on a machine at a time.

### 26.2 The GUI test

```sh
python3 tests/fake_device.py --port 1210 &     # --map maps/<file>.json for another map
./build/evre_gui_test                           # or: evre_gui_test <file>.json (a map in build/maps/)
```

The test loads the map from `maps/` beside the test executable. It opens a second TCP client to the fake device,
which plays "someone else" writing the same registers, and starts the real `MainWindow` connected to the device.

It finds its registers in the map by these rules:

| Role | Rule | In `example_device.json` |
|---|---|---|
| u8 | the first writable u8 at 0xD000 or above, not danger | FAN_SPEED |
| u8 with names | the same, with `enum` values; one of them other than 0 | LED_MODE |
| danger | the first writable 16-bit danger register at 0xD000 or above | MOTOR_SPEED |
| volts | the first read-only f32 in V | SUPPLY_V |
| amps | the first read-only f32 in A | SUPPLY_I |
| flag | the protocol's CONFIG (0xA004), bit MSG_ENABLE (bit 2) | CONFIG |
| group with `&` | the first group with an `&` in its name | Power & supply |
| login | the map's `"login"` | 0xF000, 16 bytes |

A map without the u8 or the danger register stops the test at once, and so does a window that did not load the
same map (step 1): the run then ends with exit code 1. A map without the others fails the checks that need them:
they are never skipped.

Before the window opens, the second client writes zeros to the login register (the fake device keeps even a
refused token, so this clears what an earlier run left), and the test sets `EVRE_TOKEN` to `example-token`, the
fake device's token. The window reads it at start.

It prepares math lines in its own settings:

- `P = SUPPLY_V * SUPPLY_I`
- one that names no register of the map
- one saved without its fourth field

The steps run in order. Each leaves the window and the device as the next step expects:

1. the window loaded the map the test read: as many registers as the file, the u8 and the danger register among
   them; the combo boxes have a drop-down arrow (the style sheet names the theme's arrow image, and it exists)
2. the login: the token reached the login register, zero-padded to its size, and the Log shows no refusal and no
   warning
3. writes off by default, then on
4. a write read back
5. another client's write shown
6. typed text survives polls; "Value changed while editing" with Cancel and with Write anyway
7. no question when nothing changed
8. the danger confirmation
9. an out-of-range value refused, with a pop-up
10. the pop-up covers no tab, page or sidebar, on every tab, at 1200×720 and 1600×950
11. "Show in Log" ends the pop-up, even after a resize
12. the Log tab
13. stale values grey and back
14. the ⓘ tooltip and the detail line (with a check for broken UTF-8)
15. quick write: value box; named values (LED_MODE's list holds its three values, each entry with its value's
    name, picking `on` writes 1, and FAN_SPEED shows no list); bits (read-modify-write); a danger flag; disabled at Disconnect and enabled again at
    Connect
16. groups: the box of "Power & supply" reads `Power && supply` (a single `&` would be a mnemonic and show as
    `Power _supply`); with it and another group ticked the table shows both and the button starts with
    `Power &&`; Plot shown / Unplot shown
17. the chart: the math line's value at cursor A equals V × I, its area equals mean × time, Measure off by default
    with areas in J and Wh, Hold and Live keep the button's size, a view longer than the memory makes it grow;
    the legend has a chip for every line, its chips keep their places while the values change, and 24 extra lines
    scroll in by the wheel (the time zoom unchanged) and the bar; *Show values*: the pacer's periods, 10 per second
    with nothing saved, at 2 per second a chip's value changes 2 to 4 times in 1.6 s and is saved, at every frame at
    every poll
18. the login refused: a wrong token typed in the token box, Disconnect and Connect; the device received it and
    refused it, and the Log shows *token refused: permission denied*. The Monitor logs the frames meanwhile (its
    frames cleared once the old link is silent): the first frame sent on the new link is the login, a `WRITE_ACK`
    of the whole login register (3.6)
19. the map file: `"login"` saved and loaded again unchanged, and left out by a map that has none
20. the login skipped: the other client writes a marker to the login register (refused, but kept by the fake
    device), then the window is started again with the same map without its `"login"` (a temporary copy) and the
    token still set; the Log shows *the map declares no login register: the token was not sent*, and the device's
    login register still holds the marker (nothing was written)

Three more steps cover several devices on one link (3.9, 3.10) and auto send (13.8):

- **The master and slave addresses** (`masterSlaves`, before the first step), on a link in memory: each request
  carries its slave, an answer completes only a request to the slave it comes from (one from another slave is
  unsolicited), a READ_RESP at the offset asked for but with another count (a frame of the whole block while a read
  of 4 bytes of it waits) is unsolicited too, slave 0 takes a WRITE without acknowledge and refuses a READ or
  WRITE_ACK unsent, and a map with `"slave": 0` is an error.
- **Auto send** (`autoSend`, before the bus step). First without a device: the serial link's cap
  (`IoEngine::autoSendPrescalerFor`: 100 Hz kept at 115200 baud, 4000 Hz cut to 250 Hz, 40 Hz at 9600, TCP as
  asked), the box and its 16 rates in the polling card, and the rate line at 4000 frames and 4000 polls a second on one line with no
  "slower than asked" hint (one shows for the same numbers without auto send). Then it starts `evre_fake_fast` on
  port 1236 as one device and connects the window to it: auto send on at 100 Hz writes CONFIG with bit 3 and
  prescaler 79 (SYS_RESET and DFU 0) and saves `poll/autoSendHz`; the rate line shows about 100 frames a second (within
  15%); with Poll off the read-only values still move and 16 to 40 requests go out in 2 s (CONFIG every 100 ms); a
  Monitor READ of `0xD000`, 4 bytes, gets its 4 bytes while the frames run; a CSV recording of 1.5 s has about 100
  rows a second; off clears bit 3 on the device and the polls cover every register again; on again, then
  Disconnect, clears bit 3 before the link closes, and the box is off, disabled, with its reason; on again, the
  device killed and started again: after the reconnect bit 3 and prescaler 79 are set again by themselves; the
  device's CONFIG cleared by another client: the Log says *the device stopped auto send (reset?)* once, the box
  unticks, it stays off. The window then connects back to the Python fake device.
- **A bus** (`busDevices`, before the login steps). It starts `evre_fake_fast` beside the test on port 1226 as two
  devices of this map (slaves 1 and 2) and writes two bus files in a temporary folder. With the first (D1, D2): the
  table holds both devices' registers named `D1_` and `D2_` and shows the selected one; selecting D2 shows its
  values; a write to `D2_` reaches slave 2 only; the JSON API reads `D1_` and `D2_` by name and the pass-through
  answers frames to slaves 1 and 2 each from its device; the JSON `broadcast` is refused without Allow API writes,
  then reaches both and reads each back; the Map editor names the devices of its map and shows D2's live values; the
  Registers tab's All devices shows both devices' registers; the three device pickers show D2 alike (its dot,
  *D2 · slave 2*, its state) and many names are written in short; a math line reads registers of both; the Monitor
  names the devices in place of its Slave number, and a broadcast from it (*Broadcast · slave 0*) reaches both devices; a broadcast preset made in its dialog and sent from the Broadcast menu
  reaches both; Auto send is disabled, its tooltip says why. With the second (D1, D2 with another device ID, D9 at a
  slave nobody answers): a broadcast into the device bank is refused, one into CONFIG (0xA004) is sent, one into
  CONFIG with AUTO_SEND set is refused and nothing sent; D9 goes offline (the
  Log, the card, in words too) while D1 is still polled. Close bus gives one device again, and the window connects
  back to the Python fake device for the steps after it.

The last check counts Qt's warnings about objects used across threads. It installs a message handler at start and
looks for messages that contain one of these phrases:

- "another thread" (timers started or stopped, socket notifiers enabled or disabled from another thread)
- "different thread" (children or a new parent in a different thread, a timer stopped from a different thread)
- "object's thread" (`moveToThread` called from a thread that does not own the object)
- "Cannot move objects with a parent"
- "only be used with threads started with QThread"
- "QThread: Destroyed while thread is still running"

The window lives in a block of its own and is destroyed before this check, so warnings raised while the window and
its I/O thread shut down count too. One is enough to fail the check (20.7).

Each check prints `PASS` or `FAIL`. The run ends with the counts. With `example_device.json` it runs 276 checks. The
exit code is 0 when all pass, 1 on a failure, and 2 when the map or the fake device is missing.

`EVRE_TEST_SHOT=<prefix>` makes the test save two pictures of the window at the quick-write step:
`<prefix>_fields.png` (CONFIG with its fields) and `<prefix>_bits.png` (with Bits ticked).

### 26.3 The API test

Start the Studio against the fake device with the API on, then run the test in the mode that matches how the Studio
was started:

```sh
python3 tests/fake_device.py &
./build/EVReStudio --tcp 127.0.0.1:1210 --map build/maps/example_device.json --connect --api
python3 tests/api_test.py readonly      # Studio started with --api
python3 tests/api_test.py writes        # ... with --api-writes
python3 tests/api_test.py danger        # ... with --api-writes-danger
```

The test expects the JSON port on 1220 and the EVRe port on 1219, the defaults of `api/jsonPort` and `api/evrePort`.
To keep the user's settings out of it, start the Studio with `EVRE_SHOT=';'`. The run then has settings of its own
and does not quit by itself (26.6).

It picks two read-only f32 registers with a unit, a writable u8 and a writable 16-bit danger register of the device
bank. With the example map these are SUPPLY_V, SUPPLY_I, FAN_SPEED and MOTOR_SPEED. It then runs 24 checks in
`readonly`, 25 in `writes` and `danger`:

- `info` (connected, id echoed, write switches as started)
- `list`
- `get` by name and by address, `decoded`
- unknown register and unknown command
- raw `read`
- a 50 ms `stream` of 20 samples in 0.6 to 2.0 s, tagged
- `set` allowed or refused as the mode says; a read-only register refused; 300 into a u8 refused
- `broadcast` (one device: to slave 0) allowed or refused as the mode says, the danger register by mode, a read-only
  and an unknown register refused
- pass-through `READ` answered with DEVICE_ID
- pass-through `WRITE_ACK` accepted, or refused with ERROR_RESP 3
- the danger register by pass-through

What it writes it sets back to 0. The exit code is 1 on a failure.

### 26.4 The fake devices

2.2 describes what `tests/fake_device.py [--port 1210] [--map file.json] [--token text] [--slave N]` serves. In more detail:

- It keeps a 64 KiB memory. Every 20 ms, the read-only values move: f32 as slow sine waves around 10 ± 5; i16 and
  i32 without bit fields as slower ones, ±100 raw (PRESSURE therefore moves ±1.00 bar); u32 in `ms` counting
  milliseconds since the start.
- A request that touches any byte outside the map's registers is refused with ERROR_RESP 4. A block read over a
  hole therefore makes the Studio split its block.
- It answers its own slave address only (`--slave`, else the map's), listens on 127.0.0.1 only, and takes several
  clients at once.
- Its STATUS has `CAP_AUTO_SEND` (bit 11), but it does not send frames by itself: an AUTO_SEND write is kept and
  nothing more.
- The login: when the map declares `"login"`, that register exists too, outside the map's registers. A write of
  exactly that register (its address, its size) is accepted when it holds the token (`--token`, default
  `example-token`, UTF-8 and zero-padded to the size, as the Studio sends it). A write of anything else there, or
  of part of it, is refused with ERROR_RESP 3 (permission denied). The login is checked, never required: the
  device answers every client, logged in or not. A read of the login register returns the last token written to
  it, accepted or refused, so a test can see what arrived.

`evre_fake_fast [port] [map.json] [token] [--slave N] [--node SLAVE=MAP]...` (default 1210,
`maps/example_device.json` beside the executable and `example-token`) does the same in C++, for measuring the Studio
at thousands of polls a second, and plays several devices on one link: each `--node` adds a device with its own
slave address, map and memory on the same port. It differs from the Python device in these points:

- It refreshes the values at each READ.
- It moves only f32 and u32-in-ms registers.
- It does not answer a WRITE to an unmapped address, nor a refused WRITE of a token (a WRITE_ACK gets its
  ERROR_RESP 3 as from the Python device).
- It sends all answers to one read of the socket in one write.
- It plays a bus: several devices on one port (`--node`), as the GUI test's bus step uses it.
- It plays AUTO_SEND (13.8), per connection: CONFIG (0xA004) holds what was written, HEARTBEAT (bit 0) set as a read
  sees it. A write of CONFIG with AUTO_SEND (bit 3) set makes the device send READ_RESP frames of its read-only
  block (0xD000 up to the end of the last read-only register below the first writable one at or above 0xD000; 22
  bytes in the example map) on that connection, unasked, at 8000 / (prescaler + 1) Hz (a prescaler of 0 taken as 1,
  as some devices do; others take their default 0x4F); a write with bit 3 clear
  stops them. A precise timer of 1 ms or more sends as many frames at each tick as the rate asks for by then, so the
  frames a second come out right above 1000 Hz too. Its STATUS has `CAP_AUTO_SEND` (bit 11), as the Python device's.

- With a map that has no `device_id`, it reports DEVICE_ID 0, where the Python device reports `0x0001`.

Both answer only their own slave address (`--slave`, else the map's `"slave"`): a frame for another slave gets no
answer, as on a bus. A broadcast (slave 0) WRITE is taken and not answered; any other broadcast is dropped.

The API test runs against the Python device only, the GUI test too except its bus and auto send steps, which start
`evre_fake_fast` on ports of their own. `tests/fake_login_test.py` checks the login of
both devices the same way, so they cannot drift apart unnoticed:

```sh
python3 tests/fake_login_test.py build          # [--port 1211] [--map maps/<file>.json]
```

It starts `fake_device.py`, then `build/evre_fake_fast`, one after the other on 127.0.0.1:1211 (not the GUI test's
port), each with the map (it must declare `"login"`) and its default token. On each it checks that a write of the
whole login register holding `example-token` is acknowledged and read back zero-padded; that another token is
refused with ERROR_RESP 3 and kept; that a write of part of the register is refused and changes nothing; and that
`evre_probe` with `EVRE_TOKEN` prints *token: accepted* for the right token and *token: permission denied* for a
wrong one. That is 7 checks per device, 14 in all. It stops only the processes it started. The exit code is 0 when
all pass, 1 on a failure, and 2 when a program or the map is missing or a device did not start.

### 26.5 The probe

```sh
evre_probe tcp <host> <port> <map.json> [cycles]
evre_probe serial <port> <baud> <map.json> [cycles]
```

The probe reads the map's pollable registers in the Studio's blocks, cycle after cycle as fast as the device answers
(100 cycles by default). The timeout is 1000 ms. It then prints the poll rate, the latency, the link's counters and
every numeric value, decoded.

`EVRE_INFLIGHT=n` pipelines n requests (default 1). `EVRE_TOKEN` sends the token to the map's login register first,
encoded as the Studio encodes it (UTF-8, cut or zero-padded to the login size), and prints whether it was accepted.
It is the one write the probe can make. Unlike the Studio, which sends the token only over TCP, the probe sends it on
a serial link too. Without a login in the map it prints *the map declares no login register: the token was not
sent*.

### 26.6 `EVRE_SHOT`

`EVRE_SHOT="file.png;ms;WxH;extras"` is a test aid in `main.cpp`:

- The window opens at W × H.
- After `ms` milliseconds (3000 when left out) it saves a picture of itself to `file.png` and quits.
- The optional extras, comma separated, save `file_<extra>.png`: `side` (the whole sidebar, including the part
  scrolled away), `log` (the Log tab), `help` (the Help window), `map` (the Map editor tab), `fields` (the Map
  editor on the first register with bit fields, its Bit fields page), `mapset` (the Map settings dialog), `busdev`
  (the bus device dialog), `monitor` (the Monitor tab) and `devices` (the Devices on the link card).
- While it is set, the Studio uses the settings of the application `EVReStudio-test`, so the user's are not touched.

Only this window is pictured, never another running instance. With an empty file name, the run keeps its own
settings but takes no picture and does not quit.

### 26.7 The map test and the schema test

`evre_map_test` (QtTest, no window, no device) opens every map in `maps/` beside it, and the files named in
`EVRE_MAP_TEST_FILES` (separated by `;`), and checks:

- **save byte for byte:** each map saved without a change is the same file (and a copy of the map too)
- **edits:** one register changed in a compact map changes one line; in a map written one key per line, only the
  lines of that key; keys the Studio does not know stay; hex value names stay hex; a register added lands in
  address order and a removed one takes nothing else with it; the blank lines between groups stay
- **the keys:** min, max, default (a number or a name), special, decimals, write, persist, notes, protocol, groups,
  `wo` never polled; a new map saved and loaded back; a UTF-8 BOM and Windows line ends kept
- **overlays:** merged on load, saved as differences, `extends` written again from another folder, flattened, a
  loop stopped
- **checks:** a name used twice, registers sharing bytes, min above max, a field past the bits, fields on a float
- **exports:** the Markdown has every register and ASCII diagrams; the C header is compiled with `gcc -Wall -Wextra
  -Werror` and the Python module imported (each when found on PATH, else a warning); CSV out and back gives the same
  registers, and a sheet with only some columns is read
- **bus files (3.9):** loaded, saved with the maps relative to the file, the broadcasts kept and unknown keys kept; a slave address twice,
  slave 0, device names that make register names ambiguous (`D1`, `D1_A`) and a bad name are refused; the next free
  device; a map is not a bus file
- **the broadcast rule (3.10):** into a writable register of one shared map; refused read-only, unmapped, or across
  different maps, except the reserved bank's CONFIG and MSG_CNT; never DEVICE_ID, never past the reserved bank

`tests/schema_test.py` checks the schema itself (draft 2020-12), every map in `maps/` (or the files named), and that
a broken map is refused. It needs the `jsonschema` package and says SKIP without it.

## 27. Design decisions and pitfalls

| Decision or pitfall | Reason |
|---|---|
| All device I/O on its own thread | drawing, dialogs and repaints never delay a poll; the CSV and the chart get every poll |
| The window talks to the engine only through `post()`, queued signals and locked copies | one thread owns each piece of state; no locks spread across the UI |
| `RegTable` has one writer | the I/O thread reads it without a lock; the window's copy can never tear |
| Every `QObject` on the I/O thread is a child of a moved object or created there | a member left in the GUI thread cannot be started from the I/O thread; Qt only warns and the feature silently fails (20.7) |
| Own ticker thread instead of a Qt timer for polling | Qt timers and C++ timed waits round to the 15.6 ms system tick on Windows |
| Due ticks counted, backlog capped at about 20 ms of polls | a slow moment is made up, a stall is not made up in a burst |
| `Qt::PreciseTimer` for answer timeouts, streams and the fallback frame timer | coarse timers drift by about 20 % on Windows |
| Frame clock on `DwmFlush` | a 16 ms timer against a 60 Hz display drops a frame every 0.4 s |
| Only reads overlap in the pipeline | a read after a write of the same register must see the new value |
| Answers matched by slave, function code, offset and count | a pipelined answer finds its request; a late one, or another device's, is reported, not mistaken |
| Keep-alive every 400 ms while idle | some servers drop a client that is silent for about a second |
| Nagle off on TCP (`LowDelayOption`), DTR on serial | each request leaves at once; USB CDC devices often wait for DTR |
| Blocks within a 256-address bank with gaps of at most 8 bytes | fewer requests per poll without reading far across unused space |
| A refused block is split; codes 3, 4 and 5 mark a register unavailable | a device with holes works without any setting; a request that will fail again is not repeated |
| Byte arrays over 32 bytes are not polled | a message buffer would slow every poll |
| The table shows what the device holds, read back after the write, never what was typed | the device may clamp or refuse a value |
| The raw value at edit start (`base`) is compared before writing | another client or the device may have changed it meanwhile |
| Write checks in one place (`MainWindow::onWriteRequested`) for the table and the quick-write panel | the danger and changed-meanwhile questions cannot be bypassed |
| API writes need switches that are never saved | a restart never leaves remote writes on |
| The token is never saved and never on the command line (`EVRE_TOKEN`) | it would show in the settings, the process list or the shell history |
| A map is loaded into a temporary | a bad file leaves the current map untouched |
| The table repaints at most every 50 ms | hundreds of polls a second would otherwise flood the view |
| Samples and monitor lines are capped between frames | a minimised window or a flood of frames cannot eat the memory |
| Chart: bins on absolute time, kept from frame to frame, chunks of 8 to 4096 samples, cosmetic polylines, many lines on threads in stripes or on a graphics card as a layer of the window, a frame budget by the average frame, arrays kept to the RAM set | no shimmer; cost follows pixels, not samples; Qt's fast path; 64 lines of 1000 Hz at 60 fps on a 4K screen; the window answers at any number of lines; no whole-window GPU composition, no child window (chapter 23); the RAM set is what the samples take |
| The engine link is deleted with `deleteLater()` | it may be inside its own `closed` signal when it is dropped |
| `Master::clear()` empties its lists before the callbacks run | a callback may queue a new request |
| API clients held by `shared_ptr`, answers keep a `weak_ptr` | an answer that comes after the client left finds it gone |
| `ApiServer::stop()` iterates over copies | `abort()` emits `disconnected` at once, and its handler edits the list |
| **Pitfall:** `QLatin1String` with non-ASCII text | it turns UTF-8 into mojibake ("Â·"); use `QStringLiteral` or `tr()`; the GUI test checks the detail line for U+00C2 |
| **Pitfall:** `&` in map text on a button, check box or menu | Qt reads it as a mnemonic ("Inputs & outputs" showed as "Inputs _outputs"); pass it through `noMnemonic()` |
| **Pitfall:** a label whose text changes often, in a row of controls | it widens the window; give it `QSizePolicy::Ignored` and elide the text, as the chart info line does |
| **Pitfall:** buttons whose text changes (Hold and Live) | size them for the longest text once, or the row jumps |
| **Pitfall:** the sidebar is 312 px wide, fixed | anything in it must fit; test both themes' button texts |
| **Pitfall:** Qt 6.10 deprecated `invalidateFilter()` | `RegisterFilter` uses `beginFilterChange()` / `endFilterChange()` there, and the old call before 6.10 |
| **Pitfall:** Qt warnings are invisible on Windows GUI builds | they go to the debugger output; the GUI test counts the thread warnings for that reason |
| **Pitfall:** a read that fails keeps the last good value | the table marks the error, but that poll's sample and CSV cell repeat the old value (chapter 29) |

## 28. Glossary

| Term | Meaning |
|---|---|
| EVRe | the register protocol of teknile that the Studio speaks: framed requests over TCP or a serial line |
| frame | one EVRe message: `7B SLAVE FN OFF CNT DATA CRC 7D` |
| slave | the device address byte in a frame, 0 to 255 |
| function code (FN) | what a frame is: READ, READ_RESP, WRITE, WRITE_ACK, WRITE_ACK_RESP, ERROR_RESP |
| CRC-16/X-25 | the frame check: polynomial 0x1021 reflected, init and final XOR 0xFFFF |
| map | the JSON file (`evre-map/1`) that describes a device's registers |
| register | a named address with a type, size, access and presentation |
| danger register | a register marked `"danger"`: every write is confirmed; API writes need an extra switch |
| login register | the map's optional `"login"` address where the token is written after connecting |
| token | the text typed in the token box or given in `EVRE_TOKEN`; never stored |
| bank | 256 addresses with the same high byte, e.g. 0xD0xx |
| block | one read request that covers several registers close together |
| poll | one read of every block, and one sample and CSV row when all are answered |
| poll under way | a poll whose blocks are not all answered yet; several may overlap |
| In flight | the number of requests the master sends before their answers come |
| pipelining | sending several requests before the first answer, as In flight > 1 allows |
| pending | a request sent whose answer has not come yet |
| keep-alive | the idle read of 0xA000 every 400 ms |
| ticker | the thread that marks poll deadlines |
| due tick | a deadline that has passed but whose poll has not started yet |
| backlog | due polls waiting for a free slot, capped at about 20 ms of them |
| continuous mode | poll interval 0 ("max"): the next poll as soon as one ends |
| layout generation | the engine's counter of block layouts; answers of an older layout are ignored |
| map generation | the window's counter of maps sent to the engine; snapshots of another one are ignored |
| version | a row's change counter in `RegTable`; the window copies only rows that moved |
| snapshot | the window's copy of `RegTable`, taken under its mutex |
| stale | a value older than `max(2 × interval, interval + timeout)`, shown grey |
| unavailable | a register the device refused for good (error 3, 4 or 5); no longer polled |
| sample | one (time, value) point of a register, one per poll |
| series / line | one register or math line on the chart |
| key | a line's id on the chart: `regKey(slave, address)` for a register, or `FIRST_CHART_KEY + i` (1 << 24) for math line i |
| math line | a formula over registers drawn and measured like a register's line |
| window (chart) | the time span shown |
| memory | the time span kept; the view moves inside it |
| live / held | the view follows now / stays where it is |
| smooth delay | how far behind now the live edge is drawn, measured from how late samples arrive |
| bin | the samples of one pixel column, by absolute time |
| chunk | min, max, first and last of 8, 64, 512 or 4096 consecutive samples |
| cosmetic pen | a Qt pen whose width is in device pixels, whatever the transform |
| frame clock | the source of one tick per display refresh |
| pass-through | the API port 1219 that speaks EVRe frames |
| JSON lines | the API port 1220: one JSON object per line each way |
| stream | an API client's periodic samples of named registers |
| unsolicited | a frame that answers no pending request |

## 29. Open items

| Item | Notes |
|---|---|
| Poll only the plotted registers | an option to read fewer blocks per poll when only a few registers matter |
| Math lines in the CSV and the API | math lines are computed in the GUI thread for the chart only; CSV rows and API answers carry registers only |
| A failed read repeats the last good value | `setError()` keeps `valid` and `raw`; the poll's sample and CSV cell then repeat the old value; an empty CSV cell for a failed read may be better |
| Login on serial links | only TCP connections send the token; a device that wants a login on a serial port cannot get one |
| API ports only in the settings | `api/evrePort` and `api/jsonPort` have no field in the window and no command-line option |
| Unit tests | the map files and the exports have their own test (`evre_map_test`); `Expr`, value coding and the frame parser are still tested only through the GUI and API tests |
| Translations | user text goes through `tr()`, but no translation files are built or shipped |
| Packaging | no install target, no installer, no Linux bundle |
| macOS | the code has the macOS path (condition-variable ticker, timer frame clock) but has not been built or run there |

---

# Part IV. Making maps

A map is the one place a device's registers are described. The Studio makes and changes maps on the **Map editor**
tab, checks them as you go, and writes them out for the people and programs that implement or use the device: a
Markdown specification, a C header, a Python module or a sheet. This part is about that work; the format itself is
in chapter 16.

## 30. The Map editor

```
+-----------------------------------------------------------+-------------------------+
| search   + Register  Duplicate  Delete  Undo  Redo        | NAME  .  0xD004         |
|                     Import CSV...  Export  Map settings...| live 12.031 V           |
+-----------------------------------------------------------+ General|Values|Bits|Notes
| ! Address  Name  Type  Size  Unit  Access  Write  Group   |  Address   [0xD004   ]  |
|   one row per register, edited in place                   |  Name      [SUPPLY_V ]  |
|   (a change of a selected row goes to every selected row) |  ...                    |
+-----------------------------------------------------------+                         |
| CHECKS . 1 error, 2 warnings                              |                         |
|   o  the name SPEED is also 0xD010's                      |                         |
+-----------------------------------------------------------+-------------------------+
```

### 30.1 The table

One row per register, in address order. The cells are edited in place: double-click, or start typing.

| Column | Edited as |
|---|---|
| `#` | the register's number in the map (rows are in address order), in grey; not edited, and the search does not look at it. A red dot before the number: the checks found an error on the register; amber: a warning. Hover it for the text. |
| Address | `0x…` or decimal, 0x0000 – 0xFFFF |
| Name | any text but empty |
| Type | a list: `u8` … `f32`, `bytes` |
| Size | for `bytes` only; the other types have their own size |
| Unit, Description | free text |
| Access | a list: `ro`, `rw`, `wo` (write-only: never read, so never polled) |
| Write | a list: `normal`, `action` (a write does something, then the register reads back idle), `w1c` (a 1 written to a bit clears it) |
| Group | a list of the map's groups, or a new name typed |
| More | what the other columns do not show: danger, persist, not plotted, hex, scale, range, default, value names, special values, fields, notes |

The search box filters the rows by any column. The Registers tab follows every change at once, and a register keeps
its live value while it is edited, as long as it is read the same way (the same address, type and size).

### 30.2 Bulk edits

Select several rows (Ctrl-click, Shift-click). A cell edited in one of the selected rows is set in **all** of them:
for example the group or the access of twenty registers at once. The address and the name are always set for one
register only. The form at the right shows the selected registers too: a box whose value differs between them is
empty and says *(several)*, and what is typed or picked there goes to all of them.

### 30.3 Adding, duplicating, copying, deleting

| Action | Keys | What it does |
|---|---|---|
| **+ Register** | | a new register after the selected one: the next free address, its type and group, a name `REG_XXXX` |
| **Duplicate** | Ctrl+D | copies of the selected registers at the next free addresses; a name gets `_2` (a name that ends in a number counts on: a copy of `CH_1` is `CH_2`) |
| Copy | Ctrl+C | the selected registers as JSON on the clipboard (a list as in the map file; also plain text) |
| Paste | Ctrl+V | registers from the clipboard: from this map, another one, or JSON typed in a text editor. Where their addresses are free they keep them; otherwise they move as a block, the gaps between them kept, to the first place after the selection they all fit |
| **Delete** | Del | the selected registers out of the map |

On the Registers tab, **+ Register** and *Edit definition…* open the Map editor with the new or the chosen register
selected, and *Remove* is an undoable step too.

### 30.4 Undo and redo

Every change is a step: an edit of a cell or a box, a bulk edit, an add, a paste, a delete, the Map settings, an
import. **Undo** (Ctrl+Z) and **Redo** (Ctrl+Y or Ctrl+Shift+Z) go back and forth through all of them; the buttons
say what they will undo or redo. Typing into one box is one step, not one per letter, until the box loses the focus.
The history is kept until another map is loaded or a new one started. Undoing back to where the map was saved makes
it *not modified* again.

### 30.5 The form

The right side shows the selected register in full, over four pages. With no register selected it shows a note in
the middle instead, in the warning colour: *No register selected*, and how to pick one. The General page scrolls
when the window is short: its 18 rows set no minimum height, so the window fits a 1280 x 720 screen. An empty box
shows its hint (*none*, *none: a number or a value name*), with any register selected.

**General**: address, name, type, size, unit, access, write behaviour, group, description; *persist* (kept across a
reset), *danger* (confirm every write), *show in hex*, *plot* (a line on the chart: untick for a value that does not
change with time, `"plot": false`); scale and offset (shown = raw × scale + offset); decimals
(*auto*, or a fixed number); min, max and default.

- **Min and max** are in shown units. A write past them from the window asks first (6.4); the API refuses it. A
  special value is always allowed.
- **Default** is the value after a reset (with *persist*: the factory value): a number, or one of the register's
  value names. The quick-write panel writes it with **Default** (5.1).

**Values** and **Bit fields**: chapter 31. **Notes**: longer text on the register, as long as needed, Markdown
allowed. It goes into the export (chapter 32).

**A page the selection cannot have.** *Values* is for one register; *Bit fields* for one integer register (not
`f32`, not `bytes`). Otherwise the tab gets a warning sign (hover it: why), and the page shows a note in the middle,
in the warning colour, that says why: *No bit fields: a bytes register is not a number…*, or *select one register
to edit them*. The tab stays where it is: picking a `bytes` register while on Bit fields keeps you there with the
note, and the next integer register shows its bits again. These two tabs always keep room for the sign, so it
coming and going moves no tab.

Over the pages, a **card** shows the register: its name, its address, type and access as chips at the right (each
chip of a fixed width, so they keep their places from one register to the next), and **LIVE**, its value as it is being defined, with the unit and under it the decoded text
(`MODE=run READY`). Change the scale, a value name or a field, and the card shows the device's value read the new
way, at once. The dot before the value is green with a value, amber when the value is past the min or max (the line
under it says which), grey without a value (*not connected, or not read yet*). Every line of the card is one line:
a value too long for it (a byte array, say) is cut short with an ellipsis, so the card keeps its size and the pages
under it never move. Hover the value or the line under it for the whole text. With several registers selected the
card says how many, and that a change goes to all of them.

### 30.6 The checks

Under the table the map is checked after every change: errors first, then warnings. Click one to select its
register (a finding on the map itself opens the Map settings). With nothing found only the title shows,
*CHECKS · nothing found*, and the table takes the room; the list comes back with the first finding.

| Finding | Kind |
|---|---|
| a name used twice, or no name | error |
| a register past 0xFFFF, a size under 1, a scale of 0 | error |
| bit fields on a float or a byte array; a field past the register's bits | error |
| special values on a byte array; *write-1-to-clear* on a float | error |
| min above max | error |
| slave 0 (the broadcast address) | error |
| registers that share bytes | warning |
| fields that share bits, a field without a name | warning |
| value names on a float, names on a register never read | warning |
| a default outside min … max; danger or a write behaviour on a read-only register | warning |
| a writable field in a read-only register | warning |
| a format that is not `evre-map/…`; a USB vendor ID without a product ID | warning |

The checks never stop a save: a map under construction may be incomplete.

### 30.7 Map settings

**Map settings…** edits what the map says about the device, as one undo step:

| Page | Boxes |
|---|---|
| Device | device name, description, device ID (checked when connecting, 3.7), slave address, USB vendor and product ID (mark its port, 3.2), login: the token register and its size (3.6) |
| Protocol | transport (serial, tcp, usb, or several), baud rate, TCP port, answer timeout, notes; the byte order is always little endian |
| Notes | notes on the whole map, and a note per group |

The protocol page is for the reader of the map and of its export: it tells an implementer how the device is reached.
The Studio itself connects as the sidebar says.

### 30.8 Saving

**Save** keeps the file as it was written wherever nothing changed (16.9): a map written by hand, by a script or
by another tool keeps its layout, its key order, its blank lines and the keys the Studio does not know. Only the
registers and settings you changed are written again, each in the layout it had. Keep maps under version control:
the diffs stay small.

## 31. Value names and bit fields

### 31.1 Value names (enum)

The **Values** page lists the names of the register's raw values: `0 = off`, `1 = on`, `2 = blink`.

- **+ Name** adds a row with the next value; **− Name** removes the selected rows (as **+ Field** / **− Field** on the
  Bit fields page).
- **Paste lines** takes rows from the clipboard: `0 off`, `1 = on`, `0x10: boost`, or two columns copied from a
  sheet or a datasheet table.
- **Hex** shows the values as `0x…`, and the map saves them that way.
- A row being filled in (a value, no name yet) changes nothing until it is complete.

The names show in the Decoded column, in the quick-write list, and in the danger question before a write.

### 31.2 Special values

Under the value names: names for **single values of a number**, in shown units. `-1 = not measured` on a load
current, `0 = off` on a watchdog time. The register stays a number (its value shows as a number, its chart line is
drawn), but the Decoded column names these values, the quick-write list offers them, a typed name writes them, and a
write may always set them, whatever min and max say.

### 31.3 Bit fields

The **Bit fields** page draws the register's bits as the Registers tab does, with the live value in the fields:

- **Drag across bit cells** to make a field of those bits.
- **Click a field** to pick it: its row in the list, and its value names below.
- The list: name, bits (`7:4`, or one bit `3`), access and description, edited in place. Access is *—* (as the
  register), `ro`, `rw` or `w1c`.
- **+ Field** makes a one-bit field on the lowest free bit; **− Field** removes the picked one.
- The picked field's **value names** work as in 31.1.

The page is for integer registers (not `f32`, not `bytes`). Fields may leave gaps; fields that share bits are a
warning of the checks. For any other register the page is a note that says why (30.5). A `bytes` register has no
bits drawn, whatever its size: a 255-byte one would be 128 lines of bits, a page taller than the screen (the editor
is as tall as its tallest page, the window with it).

### 31.4 A field on the chart

On the Registers tab, the right-click menu of a register with fields has **Plot a field**: the field becomes a line
of its own, a math line `REG.FIELD = bits(REG, lsb, width)` (9.4). It is drawn, measured and kept like any math
line. A register shown with a scale or an offset does not offer it: its shown value is not its raw bits.

## 32. Exporting and importing

**Export** on the Map editor's toolbar writes the map for the people and the programs that implement or use the
device. Each file says it is generated, and from which map. The functions are in `src/model/map_export.*`, without
any window, so command-line tools use the same.

### 32.1 Markdown specification

The whole map as a document an engineer (or an AI agent) can implement the device or a host from:

- the device and the link: device ID, slave address, transport, baud rate, TCP port, timeout, byte order, login, USB
- the register summary: every register in one table
- each group with its notes, then each register: its facts (type, access, write behaviour, persist, danger, not
  plotted, unit, scale, range, default), its description and notes, its value names and special values
- for a register with fields, an ASCII diagram of its bits and a table of its fields:

```
   15   14   13   12   11   10    9    8    7    6    5    4    3    2    1    0
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
|          AUTO_SEND prescaler          | -  | -  | -  |DFU |AUTO|MSG_|SYS_|HEAR|
+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
```

(Names are cut to fit their bits; the table under the diagram gives them in full.)

### 32.2 C header

`#define`s for firmware, with an optional prefix (asked when exporting: `MYDEV` gives `MYDEV_SPEED_ADDR`):

| Macro | Value |
|---|---|
| `P_NAME_ADDR`, `P_NAME_SIZE` | the address, the size in bytes |
| `P_NAME_MIN`, `_MAX`, `_DEFAULT` | in shown units: an integer, or a `float` literal (`3.65f`) for an `f32` |
| `P_NAME_<VALUE>` | each value name's number; special values marked `/* special */` |
| `P_NAME_<FIELD>_POS`, `_MSK` | each field's position and mask, `P_NAME_<FIELD>_<VALUE>` its values |
| `P_DEVICE_ID`, `P_LOGIN_ADDR`, `P_LOGIN_SIZE` | the map's device ID and login |

Names are made C identifiers (`Power & supply` → `POWER_SUPPLY`); a name that would be defined twice gets `_2`.
The header is guarded, and the tests compile it with `gcc -Wall -Wextra -Werror` (26.7).

### 32.3 Python module

A module for host scripts: `DEVICE`, `DEVICE_ID`, `SLAVE`, `LOGIN`, one constant per register address, and
`REGISTERS`, a dict of every register's definition (address, type, size, access, group, unit, description, write,
persist, danger, plot (only when `False`), scale, offset, min, max, default, `enum`, `special`, `fields` with `lsb`,
`width` and `values`).

### 32.4 CSV and Import CSV

One row per register, one column per key: `addr, name, type, size, unit, access, write, persist, group, desc, notes,
danger, format, scale, offset, decimals, min, max, default, special, enum, fields, plot` (`plot`: `0` for a register
not plotted, empty otherwise; a file without the column: every register plotted). The compact columns:

| Column | Written as |
|---|---|
| `enum`, `special` | `0=off;1=on` |
| `fields` | `NAME@7:4@rw{0=idle;1=run}#description`, fields separated by `\|`; `@access`, `{values}` and `#description` are optional |

In those columns `\ ; = | { } # @` inside a name are written with a `\` before them. Cells with commas, quotes or
line breaks are quoted as RFC 4180 says.

**Import CSV…** reads such a sheet back: the one Export wrote, or one made by hand with at least an `addr` and a
`name` column (any column order, names in any case; the others take their defaults). The registers read either
replace the map's, or join them (a register at an address the map has replaces it). Either way it is one undo
step. A sheet that cannot be read names the line: *line 4: bad address "0xG0"*.

### 32.5 The JSON Schema

`docs/evre-map-1.schema.json` describes the format for tools: editors that complete and check JSON, CI jobs that
validate maps, other programs that read them. It allows the keys the Studio does not know (tools keep them) and
`null` in the optional keys (an overlay removes a key with it). `tests/schema_test.py` checks the maps against it
(26.7).

### 32.6 Device table for the EVRe library

For firmware on the EVRe device library (`lib/EVRe.h` in the EVRe repository): a C++ header that serves the map's
device bank. **Export > Device table for the EVRe library (C++)...** in the Map editor, or
`evre export MAP --to table`. It has:

| Part | What |
|---|---|
| `P_WRITE_MIN`, `P_READ_MAX` | the library's `DEVICE_REG_WRITE_MIN` and `DEVICE_REG_READ_MAX`; also `P_ID`, `P_SLAVE`, `P_RO_SIZE`, `P_RW_SIZE` |
| `p_ro_t`, `p_rw_t` | the read-only and the read-write image: packed structs with a member per register in address order (its name in lower case; a C++ keyword gets `_`), a gap as `_gap_d016[106]`, the defaults as start values in raw units |
| `static_assert`s | every member at its address, and the sizes: a hand edit that moves one does not compile |
| `p_bind(dev, &ro, &rw)` | fills the library's pointer table (a static array, no heap), the slave address, `DEVICE_ID` and the two limits; return it from `protocolConfigure()`, or call it after `protocolInit()` |
| `p_keep_limits(&rw, &seen)` | a host write past a register's min or max (in raw units) put back to its value in `seen`; a special value passes |

`P` is the prefix asked when exporting, else the device's name (`Example device` gives `EXAMPLE_DEVICE_WRITE_MIN` and
`example_device_rw_t`). The comment on top lists what the map says and the library leaves to the device: limits,
action registers, write-1-to-clear, read-only bits of a writable register, write-only registers (the library still
answers a read), persistence, the login. Value names and bit fields are in the C header (32.2), which can be
included beside it.

**The library's rule: read-only first.** The library's permission is one address: everything from
`DEVICE_REG_WRITE_MIN` on is writable, everything below it read-only. So the export needs every read-only register
below every writable one (a writable register is `rw`, `wo`, or has a `rw` or `w1c` field), all inside
`0xD000..0xDFFF` and none overlapping. The protocol bank `0xA000` is the library's own and is left out. A map that
is not like that is refused, and the refusal names what is in the way:

```
LATE (0xD020) is read-only but comes after SETPOINT (0xD010), the first writable register: the EVRe library
makes everything from DEVICE_REG_WRITE_MIN on writable. Move it below 0xD010, or the writable registers above it.
```

`tests/device_table_test.py` exports a map with every kind of register, compiles the header with the library and
`-Wall -Wextra -Werror`, and runs it: reads and writes through `decodePacketInto()`, the boundary, the limits (26.1).

## 33. Overlays

A map can **extend** another: `"extends": "base.json"` (the path relative to this file). The overlay's settings
replace the base's, and its registers change the base's by address:

```json
{
  "format": "evre-map/1",
  "extends": "base/device.json",
  "device": "Device, bench variant",
  "registers": [
    { "addr": "0xD002", "danger": true, "unit": null },
    { "addr": "0xD008", "remove": true },
    { "addr": "0xD100", "name": "BENCH_ONLY", "type": "u8", "access": "rw", "group": "Bench" }
  ]
}
```

| Item | Effect |
|---|---|
| an address the base has | only the keys given change; `null` removes the base's key (here: the unit) |
| `"remove": true` | the base's register at that address goes |
| an address the base does not have | a new register |

The Studio shows the merged map. **Save** writes the overlay only with what differs from its base: a key set back
to the base's value leaves the overlay, a base register deleted in the editor becomes a `remove` item. *Save as* in
another folder writes `extends` again relative to the new place. A base may extend another map, 8 deep at most (a
map that extends itself stops there with an error). The Map settings say which map is the base.

Overlays suit a family of devices (one base, a small file per variant), a bench setup with extra registers, or local
notes on a map that someone else keeps.

## 34. The `evre` command-line tool

`evre` is the Studio's protocol core and map functions without a window: for scripts, CI jobs and quick checks
from a terminal. It is built with the Studio (`build/evre`, `evre.exe` on Windows).

```
evre validate MAP...                        the Map editor's checks (30.6); exit 1 if one is an error
evre export MAP --to md|h|py|csv|table      the exports of chapter 32  [--prefix P] [-o FILE]
evre info  LINK [--map MAP]                 DEVICE_ID, protocol revision, capabilities, CONFIG
evre read  LINK --map MAP NAME...           values, by name (any case) or 0x address
evre read  LINK --addr 0xD000 --count N     raw bytes, no map needed
evre dump  LINK --map MAP                   every register a poll reads, once
evre watch LINK --map MAP NAME... [--interval MS] [--count N]    a CSV line per poll
evre write LINK --map MAP NAME=VALUE... [--force]                written, then read back
evre check LINK --map MAP [--writes] [--force]                   does the device answer as its map says? (34.1)
evre broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]     every device at once, then each read back (3.10)
```

| Option | Meaning |
|---|---|
| `--tcp HOST:PORT` or `--serial PORT[:BAUD]` | the link (baud 115200 when not given) |
| `--bus BUS` | in place of `--map MAP` for `read`, `dump`, `watch`, `write` and `broadcast`: several devices on one link (a bus file, 3.9). The registers are named after their devices (`D1_SPEED`, `D2_SPEED`), each request goes to its device's slave, and every device with a login register is logged in |
| `--slave N`, `--timeout MS` | else the map's slave address and `protocol.timeout_ms` (1, 1000 ms) |
| `--json` | one JSON object per line: for scripts |
| `EVRE_TOKEN` | the login token, sent first when the map has a `login` register. Never an option: a command line can be seen by other users of the computer |

**Writes** are only what `write` is told, all checked before the first one goes out (all or nothing): a read-only
register is refused; a register marked danger and a value past the map's `min` or `max` need `--force`. Each write
is acknowledged and read back (a write-only register is not read back).

**Exit codes:** 0 done; 1 the device or the map said no (an error answer, a refusal, a timeout, a wrong token, a
device ID that is not the map's, a map with an error for `validate`); 2 the command line or a file is wrong.

Examples:

```sh
evre validate maps/*.json                                      # in CI: fail on a map error
evre export maps/device.json --to h --prefix DEV -o device_map.h
evre info --tcp 127.0.0.1:1210 --map maps/example_device.json
evre watch --tcp 127.0.0.1:1210 --map maps/example_device.json SUPPLY_V SUPPLY_I --interval 100 --count 50 > log.csv
evre write --serial COM5:115200 --map maps/example_device.json FAN_SPEED=40
build/evre_fake_fast 1231 maps/example_device.json example-token --slave 1 --node 2=maps/example_device.json &    # a bus of two devices
evre read --tcp 127.0.0.1:1231 --bus maps/example_bus.json D1_FAN_SPEED D2_FAN_SPEED
evre broadcast --tcp 127.0.0.1:1231 --bus maps/example_bus.json D1_FAN_SPEED=0     # both devices at once
```

**`broadcast`** sends one WRITE to slave 0 (no device answers it) where the broadcast rule of 3.10 allows it: into
the reserved bank's writable registers always, anywhere else only when every device has the same map. A register
marked danger, or a value past the map's limits, needs `--force`. Each device that has the register is then read
back; the exit code is 1 when one holds another value, or does not answer.

The tool opens its own link: while the Studio holds a serial port, `evre` cannot open it too. Over TCP both may be
connected when the device or its gateway serves several clients.

### 34.1 `evre check`: does the device answer as its map says?

```
evre check LINK --map MAP [--writes] [--force] [--json]
```

A line per register, `PASS`, `WARN` or `FAIL`, then a summary; exit 1 if anything failed:

| Checked | FAIL / WARN when |
|---|---|
| DEVICE_ID, STATUS | FAIL: another device ID than the map's; WARN: a protocol revision other than 1 |
| every readable register | FAIL: the read is refused or times out, or answers another size than the map's |
| its value | WARN: past `min` / `max`, or an enum value the map has no name for (a special value is fine) |
| a write-only register | WARN: the device answers a read the map says it refuses |
| with `--writes`, each read-write register (not `action`) | the value it holds is written back and read again: FAIL if the write is refused, WARN if it reads back otherwise. Danger registers only with `--force` |

Without `--writes` it only reads: safe on a running device. Run it on a new firmware against its map, in CI against
`evre-sim`, or after changing a map.

## 35. `evre-sim`: a device made from a map

`evre-sim` serves a map as an EVRe device over TCP: for trying a host, a script or the Studio before the hardware
exists, for demonstrations, and for tests. It is built with the Studio (`build/evre-sim`).

```
evre-sim MAP [--port 1210] [--any] [--slave N] [--token T] [--require-login] [--strict] [--state FILE] [--verbose]
```

It answers its own slave address only: the map's `"slave"`, or `--slave N` (1 to 255; another value ends it with exit code 2). A frame for another slave
gets no answer; a broadcast (slave 0) WRITE is taken as a WRITE to it and not answered.

The device does what its map says:

| The map says | The simulator |
|---|---|
| `device_id` | DEVICE_ID; STATUS reports protocol revision 1. The protocol's bank (`0xA000`…) is never moved or reset, even when the map lists its registers |
| `default` | the register's value at the start (else 0) |
| a read-only number | moves: a float as a slow sine wave, an integer as a slow wave, inside `min` … `max` when given; a `u32` in `ms` counts milliseconds. Registers with value names, fields or a default stay put |
| `"access": "wo"` | takes writes, refuses reads (ERROR_RESP 3) |
| `"access": "ro"` | refuses writes (ERROR_RESP 3) |
| `"write": "action"` | holds the value written 200 ms, then reads back idle (its default, else 0) |
| `"write": "w1c"`, a field `"access": "w1c"` | a 1 written clears the bit, a 0 leaves it; such bits start set, like a latched fault |
| a field `"access": "ro"` in a writable register | keeps its bits whatever is written |
| `min`, `max` with `--strict` | a value past them is refused (ERROR_RESP 3) |
| `login` | a write of the whole login register is accepted with the token (`--token`, default `example-token`), else refused; `--require-login`: nothing else is written on a connection before its login |
| `persist` with `--state FILE` | those registers are kept in FILE (JSON) across restarts |
| an address in no register | ERROR_RESP 4 (offset out of range) |

It listens on 127.0.0.1; `--any` opens it to the network. `--verbose` prints every write with its value.

```sh
evre-sim maps/example_device.json --port 1210 --verbose &
evre dump --tcp 127.0.0.1:1210 --map maps/example_device.json
```

`tests/fake_device.py` and `evre_fake_fast` remain the fixed fake devices of the GUI and API tests (26.4).

## 36. The `evre` Python package

`python/evre` is a small package for host scripts that talk to a device directly, without the Studio: frames and
the CRC, a TCP or serial link, a master that waits for each answer, the map (with `extends`), and a `Device` that
reads and writes registers by name in shown units, refuses what the map refuses (read-only; danger and past the
limits without `force=True`) and reads writes back. Standard library only; a serial port needs `pyserial`.
`python/README.md` lists its API.

Several devices on one link: `evre.connect_bus_tcp(host, port, bus_file, token=...)` gives a `Bus` of the devices of
a bus file (3.9). Its registers go by their names on the bus (`bus['D2_FAN_SPEED'] = 40`), `bus['D1']` is a device's
`Device`, and `bus.broadcast(name, value)` sends one frame to every device under the rule of 3.10, then reads each
one back.

Scripts that should share the device with the Studio use the Studio's API instead (chapter 17): the package opens a
link of its own.


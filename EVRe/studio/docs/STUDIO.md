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
    - [2.1 Install or build](#21-install-or-build)
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
    - [7.5 Y range: Auto, Manual and Log](#75-y-range-auto-manual-and-log)
    - [7.6 Normalise and Smooth](#76-normalise-and-smooth)
    - [7.7 Cursors A and B](#77-cursors-a-and-b)
    - [7.8 Mouse and keyboard](#78-mouse-and-keyboard)
    - [7.9 Legend, labels and the info line](#79-legend-labels-and-the-info-line)
    - [7.10 The right-click menu: pictures and export](#710-the-right-click-menu-pictures-and-export)
    - [7.11 Notes](#711-notes)
    - [7.12 Lanes](#712-lanes)
    - [7.13 Trigger](#713-trigger)
    - [7.14 Fast lines](#714-fast-lines)
  - [8. Measurements](#8-measurements)
    - [8.1 The range](#81-the-range)
    - [8.2 The values](#82-the-values)
    - [8.3 Units of the area](#83-units-of-the-area)
    - [8.4 Totals since Clear](#84-totals-since-clear)
    - [8.5 Number format](#85-number-format)
    - [8.6 Histogram and spectrum](#86-histogram-and-spectrum)
  - [9. Math lines](#9-math-lines)
    - [9.1 Making and managing them](#91-making-and-managing-them)
    - [9.2 The grammar](#92-the-grammar)
    - [9.3 Operators and precedence](#93-operators-and-precedence)
    - [9.4 Functions and constants](#94-functions-and-constants)
    - [9.5 Errors](#95-errors)
    - [9.6 How inputs are matched](#96-how-inputs-are-matched)
    - [9.7 The chart key](#97-the-chart-key)
    - [9.8 Examples](#98-examples)
    - [9.9 Fast math lines](#99-fast-math-lines)
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
    - [12.5 Notes beside a recording](#125-notes-beside-a-recording)
    - [12.6 Opening a recording](#126-opening-a-recording)
    - [12.7 Fast streams' recordings (.evrs)](#127-fast-streams-recordings-evrs)
  - [13. Polling performance and tuning](#13-polling-performance-and-tuning)
    - [13.1 What a poll is](#131-what-a-poll-is)
    - [13.2 The interval](#132-the-interval)
    - [13.3 The block merge rules](#133-the-block-merge-rules)
    - [13.4 When the device refuses a block](#134-when-the-device-refuses-a-block)
    - [13.5 Why N reads per poll, and In flight](#135-why-n-reads-per-poll-and-in-flight)
    - [13.6 The "slower than asked" hint](#136-the-slower-than-asked-hint)
    - [13.7 What to expect](#137-what-to-expect)
    - [13.8 Auto send](#138-auto-send)
    - [13.9 Fast streams (Fast EVRe)](#139-fast-streams-fast-evre)
  - [14. Command line, environment variables, settings](#14-command-line-environment-variables-settings)
    - [14.1 Command-line options](#141-command-line-options)
    - [14.2 Environment variables](#142-environment-variables)
    - [14.3 Settings](#143-settings)
    - [14.4 Help, the theme and the language](#144-help-the-theme-and-the-language)
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
    - [16.13 Streams (Fast EVRe)](#1613-streams-fast-evre)
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
    - [18.9 Fast EVRe blocks](#189-fast-evre-blocks)
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
    - [23.10 The trigger](#2310-the-trigger)
    - [23.11 Fast lines](#2311-fast-lines)
  - [24. Extending the Studio](#24-extending-the-studio)
    - [24.1 An API command](#241-an-api-command)
    - [24.2 A register type](#242-a-register-type)
    - [24.3 A link type](#243-a-link-type)
    - [24.4 A tab](#244-a-tab)
    - [24.5 A setting](#245-a-setting)
    - [24.6 General rules for a change](#246-general-rules-for-a-change)
    - [24.7 A text and its translation](#247-a-text-and-its-translation)
  - [25. Building](#25-building)
    - [25.1 Requirements](#251-requirements)
    - [25.2 Windows (Qt + MinGW + CMake + Ninja)](#252-windows-qt--mingw--cmake--ninja)
    - [25.3 Linux](#253-linux)
    - [25.4 CMake targets and options](#254-cmake-targets-and-options)
    - [25.5 Installers and releases](#255-installers-and-releases)
  - [26. Tests](#26-tests)
    - [26.1 Overview](#261-overview)
    - [26.2 The GUI test](#262-the-gui-test)
    - [26.3 The API test](#263-the-api-test)
    - [26.4 The fake devices](#264-the-fake-devices)
    - [26.5 The probe](#265-the-probe)
    - [26.6 `EVRE_SHOT`](#266-evre_shot)
    - [26.7 The map test and the schema test](#267-the-map-test-and-the-schema-test)
    - [26.8 `EVRE_PERF_LOG`](#268-evre_perf_log)
    - [26.9 Fast EVRe without a window](#269-fast-evre-without-a-window)
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
    - [34.2 `evre record`: a fast stream into a file](#342-evre-record-a-fast-stream-into-a-file)
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
| Device&nbsp;check | Reads DEVICE_ID and STATUS at every connect. Warns when the map was made for another device ID. |
| Register&nbsp;table | Live values with units. Decoded bit fields and enum names. Stale values turn grey and changed values glow. Search, group filter, tooltips, a detail line for the selected register. |
| Writes | Only when *Allow writes* is on. Registers marked danger ask first. Every write is read back, and a write asks first when the value changed while you were editing. |
| Quick&nbsp;write | A panel for the selected register: a value box with the map's range, its list of named and special values, a Default button, and its bits drawn as in a datasheet (click a bit to flip it). |
| Map&nbsp;editor | Make a map from nothing or change one: a table edited in place, bulk edits, copy and paste, undo and redo, value names and bit fields on a bit strip, limits, defaults, notes, live checks, a live preview of the value. Saving changes only what was edited (Part IV). |
| Export | The map as a Markdown specification, a C header, a Python module, CSV, or the device table for firmware on the EVRe library; CSV back in; a JSON Schema of the format (chapter 32). |
| Chart | Oscilloscope style: the memory depth is set apart from the view, with Hold / Live, a memory strip, cursors A and B, Auto or Manual Y, Normalise and Smooth. |
| Measurements | Per line: the value at A and B, B − A, and min, max, mean, RMS, standard deviation, peak to peak and area over A..B or over the view, and the total since Clear. Area units follow the line's unit (W → J and Wh, A → A·s and Ah). Columns shown or hidden by a right-click on the header. |
| Math&nbsp;lines | Formulas over registers (`SUPPLY_V * SUPPLY_I`), drawn and measured like registers. |
| Analysis | A line's histogram (Freedman–Diaconis bins) or spectrum (Welch, Hann) over A → B or the view, in a window of its own; a trigger holds the chart on a level crossing, as an oscilloscope (7.13, 8.6). |
| Several&nbsp;devices&nbsp;on&nbsp;one&nbsp;link | A bus file (`evre-bus/1`) puts devices at their slave addresses on one link (RS-485, a gateway). Their registers are named after them (`D1_SPEED`) in the table, chart, CSV and API; a device that stops answering goes offline without slowing the others (3.9). |
| Broadcast | One write to every device at once (slave 0), only where it means the same to each, then each read back (3.10). |
| Auto&nbsp;send | A device that can sends its read-only block by itself at a set rate: one chart point and one CSV row per frame (13.8). |
| Monitor | Every frame sent and received. Raw reads and writes, to any slave address or as a broadcast. |
| Log | Every event in a tab and in a daily file. Pop-ups for warnings and errors; the same message pops up at most every 30 s (11.3). |
| CSV | One row per poll of the registers you choose (per frame with auto send). |
| Recordings | A recording (or an export of the chart) opened in a window of its own with its chart, measurements and notes, while the live chart goes on; the chart's view or A → B exported to CSV; pictures of the chart; notes on the chart, kept beside a recording (7.10, 7.11, 12.6). |
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

This chapter installs or builds the Studio, starts the fake device from `tests/` and walks through a first session: connect, read, plot, measure, write and record.

### 2.1 Install or build

**Install.** Each release on the project's GitHub page (**Releases**) has:

| File | What it is |
|---|---|
| `EVReStudio-<version>-setup.exe` | Windows: an installer for you alone (no administrator needed): a Start menu entry, a desktop icon if you choose, an uninstaller (Settings, Apps). |
| `EVReStudio-<version>-windows.zip` | Windows, portable: unzip anywhere and run `EVReStudio.exe`. |
| `EVReStudio-<version>-x86_64.AppImage` | Linux: make it executable (`chmod +x`) and run it. |
| `evre-tools-<version>-linux-x86_64.tar.gz` | Linux: `evre` and `evre-sim` with the Qt libraries they need (`bin/`, `lib/`). |

The Windows installer and zip hold the Studio, `evre`, `evre-sim`, the Qt runtime, the example map (`maps/`),
README, LICENSE and NOTICE. The installer is not code-signed: Windows SmartScreen may say it does not know it;
choose **More info**, then **Run anyway**.

**Build from source.** You need:

- Qt 6.5 or newer, with the modules Widgets, Network, SerialPort and Test. Test is needed for every build, not only for the GUI test: CMake asks for all four modules at once, so configuring fails without it.
- a C++17 compiler
- CMake 3.21 or newer

From the source folder:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<your Qt 6 folder>
cmake --build build
```

- **Linux:** the distribution's Qt 6 development packages are enough. On Debian and Ubuntu these are `qt6-base-dev`, `qt6-serialport-dev`, `qt6-tools-dev` and `qt6-l10n-tools` (the Linguist tools). You can usually leave out `CMAKE_PREFIX_PATH`.
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

| Field | Range&nbsp;/&nbsp;default | Meaning |
|---|---|---|
| host | text,&nbsp;default&nbsp;`127.0.0.1` | Name or IP address of the EVRe-over-TCP server. Spaces around it are ignored. |
| port | 1&nbsp;–&nbsp;65535,&nbsp;default&nbsp;`1210` | Its TCP port. |
| token | text,&nbsp;empty&nbsp;by&nbsp;default | See 3.6. Shown as dots and never saved. |

The server can be a device with a network port, a TCP gateway in front of a serial device, or another EVRe Studio's pass-through port 1219 (see 17.4). The Studio turns Nagle's algorithm off on the socket, so every request leaves at once.

The Studio sets no connect timeout of its own. A host that does not answer takes as long as the operating system's connect timeout before the pill shows the error.

### 3.2 Serial / USB

| Field | Range&nbsp;/&nbsp;default | Meaning |
|---|---|---|
| port | the&nbsp;ports&nbsp;found | Every serial port the operating system lists. The refresh button beside it (a circular arrow) looks for ports again. |
| Baud | editable&nbsp;list,&nbsp;default&nbsp;`115200` | 9600, 57600, 115200, 230400, 460800, 921600, 2000000, or any rate typed. |

- **Port settings.** The port is opened at 8 data bits, no parity, one stop bit and no flow control. After opening, the Studio sets **DTR** on (many USB CDC devices send nothing until DTR is set) and clears the port's buffers. RTS is left as the driver sets it.
- **The port list.** A USB port is listed as `COM7  · <description>  1234:abcd`: the port name, the driver's description, and the USB vendor and product IDs in hex. A port without USB IDs shows its description, if it has one. With no port at all, the list shows *no ports found*.
- **The map's USB IDs.** If the loaded map declares `"usb": { "vid", "pid" }` (see 16.2), a port with exactly those IDs is listed with the map's device name in place of the driver's description. The Studio picks that port by itself only when no port was selected before. This happens, for example, when the list said *no ports found* until the device was plugged in and the refresh button was clicked.
- **Baud rate.** For USB CDC the baud rate usually does not matter, but for a real UART it must match the device.
- **One program per port.** A serial port can be open in one program only. See 15.5.
- **No token.** The token is sent only over TCP.

### 3.3 Slave and timeout

| Option | Range&nbsp;/&nbsp;default | Meaning |
|---|---|---|
| **Slave** | 1&nbsp;–&nbsp;255,&nbsp;default&nbsp;1 | The EVRe slave address put in every request. It is set from the map's `"slave"` when a map loads, and written back to the map when you save it. It is not a setting of its own. 0 is the broadcast address, which no device answers: a map with `"slave": 0` is an error. On a bus each device has its own (3.9) and the box only shows the selected one's. |
| **Timeout** | 20&nbsp;–&nbsp;10000&nbsp;ms,&nbsp;default&nbsp;500 | How long the Studio waits for an answer before it fails the request with `timeout (<n> ms)`. Radio links and slow gateways need more. |

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
| 2&nbsp;or&nbsp;more | Requests are pipelined. The next ones go out while the answers to the first are still on their way. This is much faster over TCP to a server that accepts several requests at once. |

The value is kept separately for TCP and for serial. The defaults are **4 for TCP** and **1 for serial**, and switching between TCP and Serial / USB loads the value saved for that link. The value is saved each time it changes.

Only reads overlap. A write waits until everything sent before it has been answered, and nothing is sent while a write is unanswered. So a read that follows a write always sees the new value (see 18.6). How In flight sets the poll rate is explained in chapter 13.

### 3.5 Connect, Disconnect and Reconnect by itself

The button under the options changes with the link's state:

| Button&nbsp;text | When | Clicking it |
|---|---|---|
| **Connect** | disconnected | opens the link |
| **Cancel** | connecting | gives up |
| **Disconnect**&nbsp;(red) | connected | closes the link |
| **Stop&nbsp;reconnecting** | waiting&nbsp;to&nbsp;try&nbsp;again&nbsp;(see&nbsp;below) | stops the retries |

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
| *Disconnected* | grey,&nbsp;no&nbsp;tint | No link. |
| *Connecting…* | amber | The link is opening. |
| *Connected&nbsp;·&nbsp;127.0.0.1:1210* | green | The link is open. The text names the link: `host:port`, or `COM7 @ 115200` for serial. |
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

| One&nbsp;device | A bus |
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
| Anywhere&nbsp;else | only when every device on the link has the same register map (the same registers and device ID), and every byte written lies in a writable register of it. |
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
| Search&nbsp;box | Filters the table as you type (see 4.6). |
| Groups&nbsp;button | A menu of the map's groups, to show one, several or all (see 4.7). |
| **Plot&nbsp;shown**&nbsp;/&nbsp;**Unplot&nbsp;shown** | Puts every numeric register the table shows now on the chart, or takes them off (see 4.8). |
| **Allow&nbsp;writes** | Off at every start. On: values can be edited and written. Shown in amber bold while on (see 6.1); it keeps the bold width when off, so the buttons beside it do not move. |
| **+&nbsp;Register** | Adds a register to the map (see 4.11). |

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

| Register | Shown&nbsp;as | Example |
|---|---|---|
| Integer | the&nbsp;number | `12500` |
| Integer with `"format": "hex"`, with bit fields, or with the unit `bitmask` | hex,&nbsp;two&nbsp;digits&nbsp;per&nbsp;byte | `0x1001`, `0x0005` |
| An&nbsp;enum&nbsp;register | the number (the name is in Decoded) | `2` |
| `f32`, or any register with a scale or an offset | a number whose decimals shrink as it grows: 1 from 1000 up, 2 from 100, 3 from 1, 4 below 1 | `1234.5`, `123.45`, `12.345`, `0.4200` |
| A&nbsp;float&nbsp;that&nbsp;is&nbsp;not&nbsp;a&nbsp;number | the&nbsp;text&nbsp;NaN | `NaN` |
| Byte&nbsp;array | the first 24 bytes in hex, ` …` when there are more | `48 45 4C 4C 4F …` |
| Not&nbsp;read&nbsp;yet | a&nbsp;dash | `—` |
| Read failed and no good value yet | *error*&nbsp;in&nbsp;red | |
| Refused&nbsp;by&nbsp;the&nbsp;device&nbsp;for&nbsp;good | *not&nbsp;available*&nbsp;in&nbsp;red&nbsp;(see&nbsp;4.5) | |

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
- Plot shown charts as many as there is room for, in the table's order, without a question; the status bar says how many were left off: *64 registers added to the chart, 34 left off: 64 at most at this rate*. A Plot ticked past the limit (or picked from the right-click menu) stays unticked, and the status bar says *At most 32 registers on the chart at 2000 samples a second: untick one first*. When the rate goes up (a shorter interval, Auto send on or faster), the newest lines come off the chart, and the Log names them. `--plot` past the limit says so in the Log.
- **One cap of 64 lines for every kind.** Registers, math lines (9) and fast lines (7.14) count together: the chart holds 64 lines at most, and the registers get what the rate and the other lines leave. With the chart full, a 65th line of any kind is refused the same way: a register's Plot, a fast channel's tick, a field plotted, *New math line…* (before its dialog) and a math line's *Shown* stay off, and the status bar says *At most 64 lines on the chart, registers, math and fast lines together: untick one first*. A math line edited so that it would be drawn again past the cap is kept, not shown. A recording's window keeps the same cap for its *Lines* menu, the words beside the mouse.
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
| Size&nbsp;(bytes) | For `bytes` only, 1 – 255. Other types have a fixed size. |
| Unit | Free text. |
| Access | read-only / read-write. |
| Group | Pick one of the map's groups or type a new one. Empty becomes `Registers`. |
| Description | Free text. |
| Scale,&nbsp;Offset | Six decimals, ±1e9. |
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
| *Write&nbsp;NAME* | With ⚠ for a danger register: the register the panel writes. |
| Value&nbsp;box | Type a value and press **Enter**, or click **Write**. Placeholder: *value, 0x1F, 0b101, or a name*, or with the map's limits *value (0 … 100 %), or a name*. |
| Named&nbsp;values&nbsp;list | Shown for a register with value names or special values. The special values come first, in shown units, then the value names: `name  (value)`, for example `blink  (2)`. Picking one writes it at once (a value name as its raw number, a special value as its shown value). The list follows the device's value, except while you are choosing in it. |
| **Write** | Writes the value box. |
| **Default** | Shown when the map gives the register a default: writes it (its tooltip says which). |
| **To&nbsp;all&nbsp;devices** | On a bus only (3.9): the value typed, to every device on the link in one broadcast frame, after a confirmation; then each device is read back and the Log says whether every one took it. Enabled only where the broadcast rule allows it (3.10); the tooltip says why not. |
| **Bits** | Shown for an integer register that has no bit fields and no scale or offset. Ticked, it draws the bit view (5.4) for that register too. Saved (`ui/quickBits`). |
| Hint,&nbsp;at&nbsp;the&nbsp;right | *tick Allow writes to write*, or *not connected*. |

The controls are enabled only while **Allow writes** is on **and** the link is up. The hint says which of the two is missing.

Every quick write follows exactly the same path as an edit in the table: the danger confirmation and the other checks of chapter 6. After each attempt, written or not, the named-values list and the bit view show the device's value again. The value box keeps the text you typed, so a refused value can be corrected. It is emptied only when the panel is set up anew: another register is selected, a map is loaded, or **Bits** is switched.

### 5.2 What a value may be

| Typed | Meaning | Example on the example map |
|---|---|---|
| A&nbsp;plain&nbsp;number | The value **as shown**: scale and offset are undone before writing. Use a dot for decimals, in any locale. | `22.5` on SETPOINT; `1.5` on a register with scale 0.01 writes the raw integer 150 |
| `0x…` | Hex: the raw integer, with no scale or offset undone | `0x02` on LED_MODE |
| `0b…` | Binary: the raw integer, with no scale or offset undone | `0b101` |
| A&nbsp;value&nbsp;name | Its number (names match in any case) | `blink` on LED_MODE |
| A&nbsp;special&nbsp;value's&nbsp;name | Its&nbsp;shown&nbsp;value | `off` on a register with `"special": { "0": "off" }` |
| Hex&nbsp;bytes,&nbsp;for&nbsp;a&nbsp;`bytes`&nbsp;register | Exactly `size` bytes. Spaces and other non-hex characters are ignored. | `48 45 4C 4C 4F` on a 5-byte register. The example map's only `bytes` register, MSG_BUFFER, has size 255 and needs all 255 bytes: these 5 are refused with *255 hex bytes needed, got 5*. |

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
| Bit&nbsp;cell | `0` / `1`, or `·` while the value is unknown | Flips that bit only. |
| One-bit field without value names (a flag) | its&nbsp;name | Flips it. |
| Wider field, or one with value names | `NAME = value`, using the value's name when it has one | Opens a menu of its named values (the current one checked) and **Value…**, which asks for a number from 0 to the field's maximum. |
| A&nbsp;bit&nbsp;in&nbsp;no&nbsp;field | a&nbsp;box&nbsp;with&nbsp;`—` | |

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
| Not&nbsp;a&nbsp;valid&nbsp;value | Error | `FAN_SPEED: 300 not written: 300 is out of range for u8 (0 … 255)` |
| Cancelled | Info | `…: write of … cancelled (the value changed meanwhile)` or `… cancelled at the confirmation` |
| Monitor&nbsp;raw&nbsp;write | Info&nbsp;/&nbsp;Error | `raw write 00 64 at 0xD086: OK` or `…: refused: <reason>` |

Errors also pop up (see 11.3).

## 7. The Chart

The Chart tab has two rows of controls, the chart, and the measurements under a movable splitter. The first row is what is shown and kept (Window, Memory, RAM and what the lines need, the Y range); the second what to do (Hold, Measure, Cursors, ƒ Math), how the lines are drawn (the Display menu: Normalise, Smooth, Drawing), the info line, and Clear, Remove all.

### 7.1 The axes row

| Control | What it does |
|---|---|
| **Window** | The time the view shows. Pick a preset or type a length (see below). Default 30 s. |
| **Memory** | How much is kept, like an oscilloscope's memory depth. Pick a preset or type a length. Default 60 s. |
| **RAM** | The most memory the chart's samples take, all the lines together. 2 GB by default; presets 512 MB to 16 GB (those within three quarters of the computer's memory), or any size typed: `3000`, `3000 MB`, `3 GB`. Saved. With many fast lines the Memory holds less than asked (7.4). A cap, not a reservation: with less memory free than it, the chart keeps within what is free (7.4). Its tooltip says the memory free now and, when that limits it, what the chart keeps within. |
| Memory&nbsp;note | Beside RAM, muted: what the lines need to keep the Memory set, at the rates their samples come now: *needs 1.4 GB*. More than the RAM, in amber, with what fits: *needs 2.8 GB, keeps 22 min*. Less memory free than the RAM, in amber: *only 2.1 GB free: keeps about 40 s* (7.4). Updated twice a second while the Chart tab is shown; empty until a line has two samples. |
| **Y&nbsp;range**&nbsp;Auto&nbsp;/&nbsp;Manual&nbsp;/&nbsp;Log | Auto follows the lines. Manual uses the **min** and **max** fields. Log draws the values on a logarithmic scale, its range Auto or typed (7.5). With Lanes on, the row is the **current lane's**, chosen by its unit in the list after the label (*Y&nbsp;range&nbsp;[A&nbsp;▾]*, 7.12). |
| **min**,&nbsp;**max** | The Y range. In Auto they are grey and show what the chart does, to four digits (*4.2*, not *4.20007*), the whole part always (*17420*, not *1.742e+04*). Typing either one switches to Manual, which keeps six digits; in Log it keeps Log, its range typed (both above 0). |

- **Window presets:** 1 s, 5 s, 10 s, 30 s, 1 min, 2 min, 5 min, 10 min, 30 min, 1 h.
- **Memory presets:** 10 s, 30 s, 1 min, 2 min, 5 min, 10 min, 30 min, 1 h, 2 h.
- **Typed lengths:** a number with an optional unit: `us` or `µs`, `ms`, `s` or `sec`, `m` or `min` (minutes), `h`. A bare number is seconds. A comma works as the decimal point. Examples: `45`, `2.5 s`, `500 ms`, `50 us`, `3 min`, `1 h`. Text that is not a length puts the field back to what is shown.

The limits (below a millisecond the view is for fast lines, 7.14: a polled line has a sample every millisecond at
best):

| Setting | Shortest | Longest |
|---|---|---|
| Window,&nbsp;typed | 10&nbsp;µs | 24 h |
| Window,&nbsp;with&nbsp;the&nbsp;mouse&nbsp;wheel | 10&nbsp;µs | the memory |
| Memory | 1&nbsp;s | 24 h |

A Window longer than the Memory grows the Memory to hold it. A Memory shorter than the Window shrinks the Window.

Window, Memory, Smooth and the Y mode (Log too) with its range are saved at each change.

### 7.2 The actions row

| Button | What it does |
|---|---|
| **❚❚&nbsp;Hold**&nbsp;/&nbsp;**▶&nbsp;Live** | Hold stops the view where it is while the memory keeps filling. Live follows *now* again. It is one button of fixed size, and it is filled in the accent colour while held. |
| **Measure** | Shows the measurement table (chapter 8). Off by default, and saved. |
| **Cursors** | Cursor mode: clicks place cursors A and B (7.7). Turning it on turns Measure on, and turning Measure off turns Cursors off. Turning it off takes A and B off the chart. |
| **Clear&nbsp;cursors** | Removes A and B. |
| **ƒ&nbsp;Math** | The math lines menu (chapter 9). The button shows the count of active lines: *ƒ Math (2)*. |
| **Display** | A menu of how the lines are drawn. The button keeps its text; its tooltip says what is on now: *Normalise off · Smooth on · Hover values on · drawn by the GPU: NVIDIA Quadro T1000*. In the menu: |
| -&nbsp;**Normalise** | Each line scaled to its own range (7.6). |
| -&nbsp;**Smooth** | A small display delay so that the lines scroll without steps (7.6). On by default. |
| -&nbsp;**Lanes** | A plot per unit, stacked, each with its own Y range (7.12). Saved. |
| -&nbsp;**Fold&nbsp;all&nbsp;lanes**,&nbsp;**Open&nbsp;all&nbsp;lanes** | With Lanes on: every lane folded, or opened again; each disabled when there is nothing to do (7.12). |
| -&nbsp;**Trigger** | A row under the actions: hold the chart when a line crosses a level (7.13). |
| -&nbsp;**Hover&nbsp;values** | The box of every line's value beside the mouse over the chart. On by default. Off: only the crosshair's line and its dots (the box can cover the cursors' tags). Saved. |
| -&nbsp;**Time&nbsp;grid** | **Auto (divisions below 1 s)**, the default; **Clock times**; **Divisions**: 10 fixed divisions labelled by their offset from the right edge or from T, with a *1 ms/div* readout above the plot (7.9). Saved. |
| -&nbsp;**Drawing** | Who draws the lines: **Auto (a dedicated GPU if there is one, else the CPU)**, the default; each graphics adapter found by name (*Dedicated GPU: NVIDIA Quadro T1000*, *Internal GPU: Intel(R) UHD Graphics 630*); or **CPU**. A card draws many fast lines at the display's rate (23.7). The processor's graphics is offered but draws slower than the CPU on a large screen. Saved; the Log says which draws, and when a card fails the CPU takes over and the Log says why. A card picked (or at start) takes a moment to open, up to about a second while it wakes: the CPU draws meanwhile and the window answers; the tooltip then says *CPU, opening the GPU: …*. The info line ends with *GPU* or *CPU*. On a system without Direct3D 11 (Linux): Auto and CPU. |
| Info&nbsp;line | *32/64 plotted · 2 math · 60 fps · 3.2 ms · delay 12 ms · GPU*: every line on the chart, registers, math and fast lines together, of as many as it may hold now (64 at most, the registers at the rate now, 4.8), the math and fast lines among them when there are any, frames drawn per second, the average time to draw one, and the Smooth delay (7.9). Narrow, whole parts go, never letters: first the time to draw one, then the word *plotted*, then the delay (then the fps, *GPU*/*CPU*, the math lines); the count stays longest. The tooltip holds all of it and says what each number is. |
| **Clear** | Empties every line and the memory, and starts the totals since Clear again (8.4). The lines go on from now. |
| **Remove&nbsp;all** | Takes every register off the chart (every Plot is unticked). Math lines stay. |

### 7.3 Window and memory, Hold and Live

The chart keeps the last *Memory* seconds of every line, and shows *Window* seconds of them.

- **Live**, the view ends at *now* and scrolls.
- **Held**, the view stays at a fixed time while new samples keep filling the memory. The top right of the chart says *held: -12.5 s · Live to follow*: how far the view's end lies behind now. While the trigger is on (7.13) the button is **Run** / **Stop** and the top right says the trigger's state instead.

Ways to look back:

- **Drag** the chart left or right to pan through the memory. Dragging holds the view by itself. Dragging back until the view's end is within 0.2 % of the window from now goes Live again.
- **Hold**, then drag or use the memory strip.
- **Live** (the same button) jumps back to now.

Data older than the memory is dropped in whole blocks, a little behind the memory depth, so the chart need not move its arrays at every poll. All the lines together share a budget of memory, RAM (below), and each line keeps at most 16 million samples (at 1000 polls a second about 4.4 hours).

### 7.4 The memory strip

Under the time labels, a thin strip shows the **whole memory depth**. Every line is drawn there thin and faded, each in its own range. The part the view shows is marked on it.

- While the memory fills up, the data grows in from the right. Where there is room, the strip says how much is kept so far, for example *filling: 20 s of 60 s kept* with the default memory. Lengths below 120 s are written in seconds, from 120 s in minutes with one decimal (*2.5 min*), and from 7200 s in hours (*2.0 h*).
- The samples kept have a budget for all the lines together: **RAM** on the Chart tab's first row, beside Memory, 2 GB by default (a sample takes about 23 bytes: its time and value, and its share of the min/max summaries). Pick 512 MB to 16 GB, or type any size (*3000*, *3000 MB*, *3 GB*); it is kept within 256 MB and three quarters of the computer's memory, and saved. Many fast lines can need more than the memory asked for: with 2 GB, 98 lines at 500 Hz fill it in about 32 minutes, at 4000 Hz in about 4. The oldest samples then go, an eighth of each line at a time, and the strip says so in the warn colour (amber): *RAM budget reached: keeping the last 4.0 min of 30.0 min*, and while a recording runs *… · the recording keeps everything* (its file keeps every sample, whatever the chart lets go). The longest words that fit are shown, whole (then *RAM budget reached* alone); the strip's tooltip says what the budget does and that a recording keeps every sample, and so do the RAM box's. So the time kept goes down by an eighth and fills up again (with 12 min that fit: 12, then 10.5, then 12). A line keeps at most 16 million samples whatever the RAM. RAM is what the samples take: the Studio itself needs about 150 MB more (the window, its pictures, the graphics card's buffers), so with RAM 1 GB Task Manager shows it at about 1.14 GB once the memory is full.
- **The RAM is a cap, not a reservation.** With 16 GB set and 2 GB free, filling on would make Windows page to disk:
  the whole computer slows, the chart stutters, and an allocation can fail. So every 3 s the chart reads the memory
  free (Windows: the available physical memory; Linux: `MemAvailable`), and keeps within the lower of the RAM set and
  what it holds now plus the free memory, less a reserve of 1 GB (a tenth of the computer's memory when that is more),
  64 MB at least. Its oldest go as for a RAM lowered (no freeze), before the computer pages. The note beside RAM says
  so in amber, *only 2.1 GB free: keeps about 40 s* (the time from what is left and the rates now), the RAM box's
  tooltip says the memory free and what the chart keeps within, and the strip's *RAM budget reached* tooltip adds
  that the free memory limits the budget now. Memory free again: the RAM set holds again, and nothing more is let go.
  The RAM list stays as it is: 16 GB can still be picked, and the free memory protects it.
- Click or drag on the strip: the view centres at that time and holds. Taken by its box, the view follows the mouse
  from where it was (no jump). At a short window (10 ms of a minute) the box is a sliver no mouse can take: a
  **handle** 12 px wide is drawn over it, centred on the view, with two grip lines, and it drags as the box does. Over
  the box or its handle the mouse is a pointing hand, the box is lit and a tooltip says *The view: drag it along the
  memory · Wheel: a window earlier or later*.
- The **wheel** over the strip moves the view a whole window earlier (up) or later (down), and holds; down at now it
  is live again.

### 7.5 Y range: Auto, Manual and Log

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

**Log** draws the values on a logarithmic scale: a value ten times larger stands one decade higher, whatever its size,
so a current of 1 µA and one of 1 A can be read on the same chart.

- **The grid.** A line at every decade, labelled with an SI prefix: *1 µ, 10 µ, 100 µ, 1 m … 100 m*, then *1, 10,
  100, 1000, 10000* as the chart writes values, then *100 k, 1 M …*. Faint lines mark 2 to 9 of each decade while a
  decade is at least 24 px tall. Over less than two decades the faint lines are labelled too (or 2 and 5 of them,
  when there is no room for all).
- **Auto** spans the positive values in view: from the smallest above 0 to the largest, at most **9 decades** under
  the largest (a value further down sits on the bottom edge), at least one decade, with 8 % of the decades as margin.
  It grows and shrinks as the linear Auto does, in decades.
- **Values of 0 or less** have no logarithm: they sit on the bottom edge of the plot.
- **Manual.** Typing min and max keeps Log, with that range; both must be above 0 (a min of 0 or less is refused and
  the fields show the range again). Ctrl + wheel zooms around the value under the mouse in decades; a double-click goes
  back to the Auto range, still on the Log scale. The top right shows *Y log* or *Y log, manual*.
- **The memory strip** draws each line on a Log scale too, in its own range of positive values.
- **Log and Normalise exclude each other.** Picking Log turns Normalise off, and Normalise turns Log off (the range
  mode, Auto or Manual, stays). Auto or Manual in the list are linear again.

**With Lanes** (7.12) each lane has a range of its own. The row then shows and sets the **current lane's** range, chosen
in the list right after the label (*Y range [A ▾] [Auto ▾]*): every lane by its unit, in the chart's order, a folded
one marked *(folded)*. Choosing one there, or a click on a lane's value labels, makes it current, and the list
follows the chart's click (its tooltip: *The lane these Y settings apply to · or click a lane's values on the
chart*). Without Lanes the list is hidden and the row is the plot's. A lane not in Auto says so with a
tag at the top of its value labels, *Manual* in amber or *Log*; a click on the tag sets it back to Auto.

### 7.6 Normalise and Smooth

**Normalise** scales each line into the chart by its own range in the view, so lines of different units can be compared by their shapes. The value labels become percentages (0 % – 100 %). The min and max fields are disabled while Normalise is on; the Y range list stays, and picking Log from it turns Normalise off (7.5). Normalise turns Log off. Normalise is not saved.

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

Once both are placed, a bar joins their tags at the top of the plot and says the time between them, |B − A|, whether A or B is the earlier:

- **The text.** Four significant digits below a minute, in the first unit where they stay below 1000: *123 µs*, *3.525 ms*, *12.35 s* (999.96 µs is written *1 ms*). From a minute: *1 min 23.4 s*; from an hour: *2 h 05 min*. The line above the measurements says the same span to the millisecond (*A → B = 4.457 s*, chapter 8); the bar is there to read at a glance.
- **A cursor off the view.** The bar ends at the plot's edge on that side, and its text stays the whole time between them. With both off, one each side, it runs across the plot. With both off on one side there is no bar.
- **Close cursors.** When the text does not fit between the tags, it stands beside the right tag (beside the left one when the plot ends there). No bar is drawn between the tags then: a sliver of it would only look like a mistake.

```
   +-+ +-----------------------+ +-+
   |A| |       4.457 s         | |B|          the text in the bar
   +-+ +-----------------------+ +-+
    |                             |

   +-+ +-+ +-+ +--------+
   |A| | | |B| | 300 ms |                     too close: the text beside B
   +-+ +-+ +-+ +--------+
```

Cursors are fixed **times**, not screen positions. In a live view they move left with the data. Once both are placed, the measurements cover A → B instead of the view.

**Their times.** A tag's tooltip gives the cursor's clock time (*Cursor A at 14:03:12.345*). While the trigger holds
the view on a crossing (7.13), it also says how far the cursor is from **T**, as an oscilloscope's cursors measure from
the trigger point (*Cursor A at 14:03:12.345 · T -0.250 ms*), and the line above the measurements adds both after the
span (*A → B = ... · A: T -0.250 ms · B: T +1.750 ms*). B − A is the same either way. The distance from T has three
decimals in the unit the window is written in (µs below a 1 ms window, ms below 1 s, then s). The tags stay letters,
so the bar between them keeps its room; live, or without a crossing in view, the tooltip gives the clock time alone.

### 7.8 Mouse and keyboard

| Where | Action | Effect |
|---|---|---|
| Chart | Drag,&nbsp;left&nbsp;button | Pan through the memory, and hold. |
| Chart | Wheel | Zoom the time by 1.25 per notch. Live, the right edge stays at now. Held, the zoom is around the time under the mouse. Below a 1 s window (the time grid in divisions, 7.9) a notch is the next window of 1, 2 or 5 per division (10 ms, 5 ms, 2 ms ... in; 20 ms, 50 ms ... out), as a scope's time/div knob; Auto leaves the divisions at 1 s, and from there the notch is 1.25 again. |
| Chart | Ctrl&nbsp;+&nbsp;wheel | Zoom Y around the mouse (switches to Manual); with Lanes, the lane under the mouse. |
| Lanes'&nbsp;value&nbsp;labels | Wheel | Scroll the lanes up and down when they do not fit (7.12). |
| Lanes'&nbsp;scroll&nbsp;bar | Drag&nbsp;/&nbsp;click | Drag the handle; a click above or below it moves one plot height (7.12). |
| Lane's&nbsp;▾&nbsp;or&nbsp;unit&nbsp;name | Click | Fold the lane (7.12). |
| Lane's&nbsp;⋯ | Click | The lane's menu: its Y range (Auto, Manual…, Log) and Fold lane (7.12). |
| Folded&nbsp;strip&nbsp;or&nbsp;its&nbsp;▸ | Click | Open the lane again (7.12). |
| Separator&nbsp;between&nbsp;lanes | Drag&nbsp;/&nbsp;double-click | The lane above taller or lower, the one below giving or taking / every lane its equal share again (7.12). |
| Chart | Double-click | Y back to Auto (on a note's tag: edit the note, 7.11). |
| Lane's&nbsp;value&nbsp;labels | Right-click | The lane's Y range: Auto, Manual…, Log, All lanes: Auto; Fold lane / Open lane (7.12). |
| Lane's&nbsp;value&nbsp;labels | Click&nbsp;/&nbsp;double-click | Make it the current lane: the toolbar's Y range shows and sets its range / that lane back to Auto (7.12). |
| Lane's&nbsp;range&nbsp;tag | Click | That lane back to Auto, linear (*Manual* or *Log* at the top of its value labels, 7.12). |
| Legend&nbsp;chip | Click&nbsp;/&nbsp;right-click | The line's menu, under the chip: its Histogram or Spectrum (8.6), and Trigger on this line, ticked for the line watched (7.13). The chip's ▾ says so; over it the mouse is a pointing hand, the ▾ is lit and the tooltip names the entries. |
| Trigger's&nbsp;level&nbsp;marker,&nbsp;tab&nbsp;or&nbsp;line | Drag | Move the level, from where it was taken (7.13); the marker (*T▸*) lies left of the plot, the tab (*0.4 A ↑*) right of it, both lit under the mouse, their tooltips the level in words. |
| Trigger&nbsp;tab's&nbsp;arrow | Click | The next edge: rising, falling, either (7.13). |
| Trigger's&nbsp;T&nbsp;▼&nbsp;flag&nbsp;above&nbsp;the&nbsp;plot | Drag&nbsp;/&nbsp;double-click | The crossing's place in the window, 0 to 90 %, from where it was taken; a double-click puts it back at 50 %, the default; its tooltip says when the line crossed (7.13). |
| Chart | Right-click | The chart's menu: Copy picture, Save picture, Export to CSV, Add note here, Open recording (7.10). |
| Note's&nbsp;tag | Drag / double-click / click, Delete | Move the note / edit its text / remove it (7.11). |
| Legend&nbsp;(chips&nbsp;overflow) | Wheel | Scroll the chips, 60 px per notch. The time zoom is left alone. |
| Legend&nbsp;scroll&nbsp;bar | Click&nbsp;/&nbsp;drag | Bring the thumb under the mouse, then drag it. |
| Legend&nbsp;arrows | Click | Scroll half a row that way. |
| Chart,&nbsp;cursor&nbsp;mode | Click&nbsp;/&nbsp;drag | Place or move cursor A, then B (7.7). |
| Chart | Hover | Crosshair: a dashed line at the mouse, a dot on each line that has a sample within 1/20 of the window, and a box with the clock time (`14:03:12.345`), how long ago (`-2.40 s`; while the trigger holds the view on a crossing, how far from T instead: `T +1.234 ms`, 7.13) and every line's value, in the short number format of 7.9. With many lines the values stand in as many columns as the plot's height needs (64 lines: two in a 700 px plot); the box stays inside the plot. The values change at the **Show values** pace, as the legend's, and at once when the mouse moves; the dots follow the lines at every frame. Each column has room for the longest name, the widest number (right-aligned) and the longest unit, so the box keeps its size and place while the digits change. |
| Memory&nbsp;strip | Click&nbsp;/&nbsp;drag | Centre the view there, and hold. Taken by its box (or the 12 px handle at a short window), the view follows the mouse from where it was. |
| Memory&nbsp;strip | Wheel | The view a window earlier (up) or later (down), held (7.4). |
| Measurement&nbsp;table's&nbsp;header | Right-click | Show or hide columns (8). |
| Anywhere | F1 | Help. |
| Register&nbsp;table | Enter&nbsp;/&nbsp;Esc&nbsp;/&nbsp;F2 | Write the edited value / cancel the edit / start an edit. |

The chart takes one key: **Delete** (or Backspace) removes the note clicked last (7.11).

### 7.9 Legend, labels and the info line

- **Legend.** Across the top, a chip per line shows its name and latest value: `SUPPLY_V  12.1 V` for a supply of 12.05 V. With no line yet, the plot says *Tick "Plot" on any register to chart it*.
- **The chip's menu.** Each chip ends in a **▾**: a click anywhere on the chip (or a right-click) opens the line's menu under it, *Histogram of …*, *Spectrum of …* and *Trigger on this line* (8.6, 7.13). Over a chip the mouse is a pointing hand, the ▾ is lit and the tooltip says *Click or right-click: Histogram, Spectrum, Trigger on this line* (a recording's window: without the trigger).
- **How often the values change.** At the pace chosen in *Show values* (13.2): 10 times a second by default, like the Registers table. A number that changes at every frame cannot be read. The lines still move at every frame.
- **Fixed places.** A chip's width comes from the line's name, its unit and room for the widest number the legend writes (`-0.000e+00`). It never depends on the value, so a changing value cannot move the chips after it. The value is right-aligned in its room, with the unit after it: only the digits change.
- **More lines than the row holds.** The chips use the row left of the state text. When they need more, a thin scroll bar appears under them, and an arrow at each end of the row marks where more chips lie. Scroll with the mouse wheel over the row (a sideways wheel too), drag the bar's thumb, click the bar to bring the thumb there, or click an arrow to move half a row. The scroll stays where you leave it while the values change; after a resize or a line removed it is kept within what the chips need. The bar sits inside the legend's row, so the plot does not move when it appears. When all chips fit, there is no bar and the wheel over the row zooms the time, as over the plot.
- **The state, top right.** What holds the view or changes how it reads: *held: -12.5 s · Live to follow*,
  *Y log, manual*, *cursors: click / drag*. While the trigger is on it says the trigger's state alone, in the row's
  words (*Normal · waiting*, *Normal · triggered*, *Auto · free running*, *Stopped · Run to arm*, 7.13), amber only
  when stopped. It never lies over the legend: the chips end where
  its text begins, 16 px before it, so the row's arrow and the last chip are never under it, and a chip cut at the
  row's end is cut before the arrow. Its room is at most 40 % of the plot (more for its shortest form rather than
  cut, always leaving the legend its first chip and both arrows). When the text is longer, whole parts are dropped
  in turn: *Live to follow*, then *click / drag* (*cursors* stays), then *manual* (*Y log* stays); the time held
  always stays. Below a 1 s window the time/div readout sits in this row just left of the state, inside the same
  room; it keeps its place (the state's parts go first), and the chips end before it. The whole text is its
  tooltip. In Arabic it reads from the right. With Lanes it says nothing about them: the lanes' ▾ and ⋯ show the
  fold and the menu.
- **Number format of the legend and the crosshair.** Both show about four significant digits, whatever the register's own format: no decimal from 100 up, one from 10 up, two from 1 up and three below 1. Values from 100 000 up or below 0.001 are written in exponent form (`1.235e+05`). Zero is `0`.
- **Value labels.** They use 1-2-5 steps, about five of them.
- **Time labels.** From a 1 s window up they show the **clock time**, as precise as the grid step needs (`14:03:12`, `14:03:12.5`, `14:03:12.35`, `14:03:12.345`). The grid lines are fixed to clock times, so they move with the data.
- **Divisions below 1 s.** Below a 1 s window the time axis is a scope's graticule: **10 fixed divisions** across the
  plot, whose lines stand still while the wave moves (clock times at 10 ms marched across the view). The labels are
  **offsets**, in the unit that fits the window (s, ms, µs): live, or held by you, from the right edge (*-10 ms*,
  *-9 ms* ... *0*); held by the trigger on a crossing, from **T** (*-2 ms* ... *0* ... *+8 ms*, the *0* under the
  crossing; the grid is laid from T, so a line always falls on it). Labels stand every 1, 2 or 5 divisions as their
  width needs, *0* always among them. Above the plot, in the state's row at its right end (just left of the state when
  there is one), a readout in a box of its own says the division and the clock time at 0: *1 ms/div · 18:07:34.263*
  (T's time while held on a crossing); being off the time axis, it hides none of its labels. Live,
  its clock time is written again at most twice a second, so it does not run; its tooltip says what it is. The
  wheel steps the window through 1, 2 and 5 per division (7.8), so the readout and the labels stay round; a window
  typed (*30 ms*) is kept, and the readout then says its exact division (*3 ms/div*). In Arabic every offset and the
  readout are one left-to-right piece, the unit beside its number.
- **Display → Time grid.** *Auto (divisions below 1 s)*, the default; *Clock times* (as before, at every window);
  *Divisions* (at every window: a 10 s window reads *1 s/div*, *-10 s* ... *0*). Saved (`chart/timeGrid`, 14.3).
- **Info line.** It starts with every line on the chart, registers, math and fast lines together, of as many as it may hold now (*32/64 plotted*: 64 at most, the registers at the rate now), and the math and fast lines among them, then shows *fps*, the frames drawn in the last second, and *ms*, the average time to draw one frame. With Smooth on it also shows *delay*. It ends with who draws, *GPU* or *CPU* (Drawing, 7.2). When the row is too narrow, whole parts go in this order: the time to draw, the word *plotted*, the delay, then the fps, who draws and the math lines (*32/64 · 60 fps · GPU*); a part is never cut in the middle. Its tooltip holds the whole text. It is updated twice a second, only while the Chart tab is shown. Frames that take more than about 60 % of a refresh skip one now and then, as many as needed, so the chart never takes more than about 60 % of the window's time: with very many lines the fps drops, but the rest of the window keeps answering (23.6).

Frames follow the display's refresh. On Windows the Studio waits for each refresh of the compositor. Without a compositor (a remote session, a screen that is off), and on other systems, a 16 ms timer paces the frames instead (see 20.6).

### 7.10 The right-click menu: pictures and export

A right-click on the chart opens its menu:

| Item | What it does |
|---|---|
| **Copy&nbsp;picture** | The chart as shown, legend, axes and memory strip included, onto the clipboard. |
| **Save&nbsp;picture…** | The same as a PNG file (the suggested name is `chart_<yyyyMMdd_HHmmss>.png`). Its size is the chart's in the screen's pixels: 2700 px wide for a 1200 px chart at 225 %. |
| **Export&nbsp;to&nbsp;CSV…** | The samples of every line on the chart, plotted registers, math lines and fast lines, over the view, or between the cursors (A → B) when both are placed. |
| **Add&nbsp;note&nbsp;here** | A note at the time under the mouse (7.11). |
| **Open&nbsp;recording…** | A recording in a window of its own (12.6). |
| **Recent&nbsp;recordings** | The last 8 recordings opened or exported, newest first. |

**The pictures are painted by the CPU**, the plot too, also while a graphics card draws the chart on the screen: they
are what the CPU would show, the same lines, grid and tags.

**The export** is in the recording's format (12.3): `time_s,datetime,NAME [unit],…`, a column per line (a math line
as named on the chart, `ƒ P [W]`), so it opens as a recording too (12.6). Only the samples the chart keeps are
there (its Memory, 7.3), as they are, not resampled:

- **Rows.** The samples are merged by time. A row starts at the earliest sample not written yet; a sample of each
  other line joins it when it is no later than a quarter of that line's own interval after it (its median gap):
  the registers of one poll share their row, and a line of another rate gets rows of its own. A row's `time_s` is its
  earliest sample's; a cell with no sample is empty. A fast line's records (7.14) are each a row, the channels of
  one stream sharing it; at most 16 million of a line (the first in the span), as for a polled line's memory.
- **Notes.** The notes in the span are written beside it, `<file>.notes.json` (12.5).
- **On a thread.** The samples are copied at once (a memory copy); writing them as text runs on a thread of its own,
  so the window goes on. When it takes more than a moment, a progress dialog shows, with **Cancel**: a cancelled
  export removes its file. Another export waits until this one ends (its menu item is disabled meanwhile).
- The Log says *chart exported: 1000 rows to <file>*, and the file joins the recent recordings.

### 7.11 Notes

A note marks a moment on the chart with a few words: *pump on*, *valve shut*.

- **Add one:** right-click the chart at that time, **Add note here**, and type its text. It is a dashed line in the
  warning colour at that time and a tag with the text at the bottom of the plot (a long text is cut to 180 px; the
  tag stands left of its line at the plot's right edge).
- **Move it:** drag its tag. **Edit it:** double-click its tag (an empty text removes it). **Remove it:** click its
  tag (its edge turns to the accent colour) and press **Delete**.
- Notes are times, as the cursors are: in a live view they move left with the data, and a note older than the
  memory is no longer shown. **Clear** leaves them.
- A graphics card draws them too: the line on the card, the tag as a picture, as the cursors' tags.
- **Kept beside a recording.** While a CSV recording runs, the live chart's notes from its start on are written
  beside it at every change, `<recording>.notes.json` (12.5); an export writes the notes of its span beside it. A
  recording opened shows its notes, and saves them again at every change. Notes made otherwise are not saved.

### 7.12 Lanes

**Display → Lanes** gives each unit a plot of its own, stacked under each other: volts in one, amps in the next, a
power in a third, each read on its own scale instead of a 12 V line flattening a 0.5 A one.

- **One lane per unit**, however many, in the order the lines came, of equal height, 10 px apart. Lines without a
  unit share a lane of their own (*no unit*). The unit stands up the left edge of each lane's labels.
- **Lanes stay readable:** an open lane is at least **80 px** high, room for two value labels. When the lanes do not
  fit (eight units on a laptop screen), they keep that height and **scroll** up and down inside the plot: the
  **wheel** over the value labels (Ctrl + wheel there is still the lane's zoom; the wheel over the plot still zooms
  the time), or the **scroll bar** in the right margin, shown only then: drag its handle, or click above or below it
  to move one plot height; over the bar the handle brightens and a tooltip says so. A lane cut by the plot's top or
  bottom edge is drawn cut, but no text is cut: its value labels stay whole inside the part in view (and never run
  into the next lane's across the gap), and a folded strip cut by the edge writes nothing until it is whole. Turning
  Lanes on starts at the top.
- **Fold a lane:** a **click on its ▾ button** at the top of its unit column, or on its unit name (or **Fold lane** in
  its menu), folds it into a strip 22 px high: its
  unit, then each of its lines with its colour dot, name and latest value in view (as the legend writes it), cut
  with … when they do not fit. No grid, lines or value labels; the crosshair's box leaves its lines out. The open
  lanes share the height that is left. A **click anywhere on the strip** or its ▸ (or **Open lane** in its menu, also
  by a right-click on the strip) opens it again; a click on a strip places no cursor and adds no note. Over a button,
  a unit name or a strip the mouse becomes a pointing hand, the button (a small rounded shape, so it looks like one)
  lights up, and a tooltip says *Fold lane* or
  *Open lane* (*Y range and lane options* over the ⋯ button). **Display → Fold all lanes** and **Open all lanes** (shown
  with Lanes on) fold or open them all, a way back when everything is folded. The value labels' tooltip names what
  the mouse does there: the wheel scrolls the lanes (while they do not fit), Ctrl + wheel zooms the lane, a click
  makes it the current lane, a double-click sets it to Auto, a right-click has its Y range and Fold lane. Folds are kept by
  unit and saved, so a unit folded stays folded when it comes back, at the next start too; a recording's window
  keeps its own.
- **Each lane its own Y range,** kept by its unit (the same unit finds its range again, also at the next start):
  a **click on its ⋯ button**, under its ▾, opens the lane's menu under the button: **Auto**, **Manual…** (its min
  and max asked), **Log** (7.5), **All lanes: Auto** and **Fold lane**; a right-click on its value labels opens the
  same menu. The ⋯ shows on every open lane with room for it and a short unit name (not on a lane cut to a sliver by
  the plot's edge). **Ctrl + wheel** over a lane zooms that lane (Manual); a **double-click** in it or on its value
  labels sets it to Auto.
- **A range is never hidden:** a lane whose range is not Auto has a **tag** at the top of its value labels, beside
  its buttons: *Manual* in amber (the warn colour: its range does not follow the lines) or *Log* in the accent
  colour; no value label is drawn under it. Its tooltip gives the range (*This lane's Y range is manual: 4.94 to 17.1
  A · Click: back to Auto*); over it the mouse is a pointing hand and the tag lights up; a **click** sets the lane to
  Auto (linear). A range set long ago (a Ctrl + wheel, saved by unit) comes back tagged at the next start.
- **The current lane drives the toolbar:** with Lanes on, the Y range row (7.1) shows and sets the range of the
  **current lane**, chosen in the **lane list** right after *Y range* (*Y range [A ▾] [Auto ▾] min max*: every lane
  by its unit as the chart orders them, a folded one marked *(folded)*), and its unit name is lit in the accent colour
  (with more than one lane). The first lane is current until another is chosen in that list or by a **click on its
  value labels**, its ⋯ or its tag; the list and the row follow at once, and the list follows the lanes as units come
  and go. Auto, Manual, Log, min and max there act on that lane alone. Without lanes
  the row is the plot's again.
- **Display → All lanes: Auto** (shown with Lanes on, enabled while a lane is not in Auto) sets every lane back to
  Auto, linear.
- **A line between two lanes**, in the middle of the gap, from the value labels across the plot, in the colour of a
  control's edge (3:1 to the chart in both themes; the border colour, 1.3:1, was too faint): the lanes read as plots
  of their own, and the value labels of one do not run on into the next. Only between lanes in view.
- **Resize a lane by its border:** over a separator between two open lanes the mouse becomes the vertical-resize
  cursor, the line lights up in the accent colour, and a tooltip says *Drag: this lane's height · Double-click: equal
  heights*. Dragging it makes the lane above taller or lower, the one below giving or taking as much; neither goes
  under 80 px. The heights are kept by unit as shares of the room, so a resized window keeps their proportions, and
  they are saved (a recording's window keeps its own). The lanes always fill the plot: a lane whose share would be
  under 80 px (the window made smaller, the others dragged tall) is held at 80 and the others share what is left by
  their weights, so the last lane never runs below the plot; they scroll only when every open lane is at 80 px. A **double-click** on a separator gives every lane its equal
  share again.
- **One time axis** under the plot; the grid's times run through every open lane.
- **Across the whole plot,** however the lanes are scrolled: cursors A and B, their span and the A-B bar (at the
  plot's top), the notes (their tags at its bottom), the crosshair's line with a dot on every line in its own lane
  (none for a lane scrolled away), and **one** box of all the values. The trigger's level line and its tag show
  while the trigger's lane is open and in view.
- **A line stays in its lane:** a Manual range narrower than its values cuts it at the lane's edge.
- **Normalise** scales each line into its own lane. Log of a lane and Normalise exclude each other as on the plot.
- The memory strip is as without lanes. Measurements, export and notes are the same.
- A graphics card draws all the lanes in one frame, as the CPU does.

### 7.13 Trigger

A click (or a right-click) on a line's chip in the legend, **Trigger on this line**, or **Display → Trigger** holds
the chart when a line crosses a level, as an oscilloscope's trigger: a spike, a step, a start-up, caught and held for a look, or a
repeating wave held still.

**On the chart.** One line is watched at a time; each line keeps its own level and edge.

- **The level** is a dashed line in the line's colour: in the line's own lane with Lanes (7.12), over the one plot in
  the line's Y scale without. It runs from its **marker** *T▸* in a column left of the plot (between the value labels
  and the plot, so it covers none of them) to its **tab** in a margin right of the plot, so nothing of the trigger
  covers the newest samples: *0.4 A ↑*, the level in the line's unit and the edge. The marker, the tab and the flag
  above the plot (below) are one family: boxes of one height, corners, border and font, on a raised surface with the
  line's colour as their border, each joined to a pointer of one size in the line's colour; the marker's and the
  tab's points meet the dashed line at the level's height. The level is written as it is set (6 significant digits),
  the same in the row's box, the tab and the drag. The pointers of the marker and the tab are solid while the view
  holds a crossing of this level, hollow after the level is changed until a crossing at the new one (and before the
  first). Their tooltips name the line, the level and the edge in words: *I_LOAD 0.4 A, rising* (the marker's then
  says *Drag: the trigger level · the edge: in the Trigger row*). **Drag** the marker, the tab or the line up or
  down: the level follows from where it was taken, and the trigger watches the new level at once, before the mouse is
  let go; the marker and the tab are lit together. With Lanes they sit in their lane's band, and a lane scrolled
  partly out of view cuts them as it cuts the lane; beside the lanes' scroll bar the tab lies right of it. The
  margin, the marker's column and the flag's strip above the plot are there only while the trigger is on: the plot is
  that much smaller then.
- **A level beyond the lane's range** (10 A on a line that shows 2 A to 7 A) stays pinned to the lane's top or
  bottom edge, so it can be found and dragged back, but its line is dotted instead of dashed, the marker's and the
  tab's pointers become ▲ or ▼ in their boxes, and their tooltips say *▲ I_LOAD 10 A, rising (above range)*. A press on it keeps the level until the
  mouse moves a few pixels up or down (a press to read the tooltip changes nothing). The lane's Auto range is not
  widened for it. When the line does not reach the level in the view, the row says *waiting: level above the line's
  range* (or below) instead of *waiting for a crossing*: Normal would wait for ever.
- **The arrow** at the tab's right end (↑ rising, ↓ falling, ↕ either) is a button: a **click** takes the next edge.
  Over the tab the mouse is a pointing hand, the tab is lit (the arrow's part, behind a thin divider, more under the
  mouse), and the tooltip says
  what a drag and a click do: *Drag: the trigger level · Click the arrow: the edge (rising, falling, either)*.
- **The crossing's place** in the window is a **T ▼** flag in a thin strip above the plot, under the legend: a T in
  the family's box over a ▼ in the line's colour whose point touches the plot's top edge right over the crossing; its
  tooltip says when the line crossed (*Trigger point: I_LOAD crossed 0 A, rising, at 14:03:12.345*), then what a drag
  and a double-click do. It is at 50 % from the left by default (the middle, as a scope's; a place you
  set is kept). **Drag** it left or right, 0 to 90 %, from where it was taken, or **double-click** it to put it back
  at 50 % (its tooltip says so); held on a crossing, the view moves with it. Over it the mouse is a pointing hand and
  the flag is lit.
- **A line watched for the first time** starts at the middle of what it shows in the view, rising; **Find level** in
  the row sets that again. Its level and edge are kept by its name: another line chosen and this one again finds them
  as they were.

**The row** under the actions is the keyboard's way to the same settings, and shows the same state:

| Control | What it does |
|---|---|
| Line | The line watched: any register, math line or fast line (7.14) on the chart, each with its colour dot before its name (as on its chip). Another line chosen brings its own level and edge. |
| Rising&nbsp;/&nbsp;Falling&nbsp;/&nbsp;Either | Rising: from below the level to it or above it; Falling: from above to it or below; Either: both. |
| level | The level, in the line's unit (written after the box, muted): the dashed line on the chart. |
| Find&nbsp;level | Sets the level halfway between the line's lowest and highest in view: the rule a line watched for the first time starts with. |
| Auto&nbsp;/&nbsp;Normal&nbsp;/&nbsp;Single | The mode (below). |
| hold-off | After a crossing, no other counts for this long: *window* (the window's length, the default: a picture per window) or a time typed, 0 to 10 s (`5 ms`, `0`). |
| position | The crossing's place in the window, 0 to 90 %, 50 % by default: the T ▼ flag above the plot (the label's tooltip says so; a double-click on the flag puts it back at 50 %). |
| Arm&nbsp;/&nbsp;Force | In Single: **Arm** waits for one more crossing; the primary button while Single holds its crossing. While Normal or Single waits it reads **Force** (a scope's Force Trigger): a click holds the view now as a crossing would, at the newest sample, its flag there; Single is then complete and the button reads Arm again. Hidden in Auto. One button, as wide as either word, so the row keeps its length. |
| State | *waiting for a crossing* (*waiting: level above the line's range*), *Normal · waiting, last at 14:03:12* (Normal back to waiting after a capture: when the last one was, written once), *Normal · triggered* (*Auto · triggered*: no number while it runs), *Auto · free running*, *Single · complete at 14:03:12.345*, *Stopped · Run to arm* (a finished Single is *complete*; *Stopped* is your Stop alone). It takes the room the row leaves: a longer text is cut ("...") and is whole in its tooltip, so no state widens the window (in Arabic either). |
| Off | After Force / Arm, before the state: turns the trigger off, as unticking **Display → Trigger** (the same action). |

Where one line does not hold all of the row's controls and some room for the state (a narrow window, longer
words in Arabic, wider fonts), the row takes **two lines**: the line, the edge, the level, Find level and the mode
on the first; the hold-off, the position, Arm / Force, Off and the state on the second. No control is hidden or
squeezed, and the main window's narrowest stays 1280 px in each language with the row shown. Force / Arm coming and
going with the state never moves the row between one line and two.

**The modes**, as a scope's:

- **Auto** holds on each crossing; when none comes for a window's length after the next one could count (the
  hold-off and the view's fill passed, whichever is later), it runs live until the next, by the samples' time: a line
  whose samples come late still holds from crossing to crossing.
- **Normal** holds on each crossing and stays held until the next one, however long. While it waits the view stands
  still (the last capture, or the view as it was when armed): only Auto rolls.
- **Single** holds on the first crossing and stops; **Arm** for another. It too waits on a still view.
- While Normal or Single waits, **Force** (in Arm's place) holds the view now, as if the line crossed: what the line
  does is seen without a level it reaches.

The next crossing counts once the hold-off has passed and the view held is full, whichever is later.

**Run and Stop.** While the trigger is on, the toolbar's **Hold** / **Live** is **Stop** / **Run**: one button, so the
two cannot disagree. **Stop** holds the picture: no crossing counts, a crossing waiting for its view and the hold-off
are dropped, and the view and its T stay. **Run** arms again in the mode, from now (Auto runs live, Normal and Single
wait on a still view): the first crossing after it counts at once. While
stopped, another line, edge or mode in the row is what Run will arm; the picture stays (another line's T goes).
Dragging the chart or the memory strip while the trigger runs is a Stop (a press that does not move it is not); a zoom
is not, and a view the trigger holds stays held through it, even while it still fills after the crossing. After
Single's crossing the button says Run (as Arm). While you have stopped it, its **Run** is drawn in the warn colour
(amber, in both themes), as the corner's *Stopped · Run to arm*: one state, one colour in both places. With the
trigger off it is Hold / Live again.

**One state, one voice.** The row and the chart's state corner (7.9) say the same state, and change only when it
does, never at each crossing or each frame. The corner shows the trigger's state alone, in the muted colour, amber
only when stopped:

| State | The row | The corner |
|---|---|---|
| Waiting | *waiting&nbsp;for&nbsp;a&nbsp;crossing*; Normal after a capture *Normal&nbsp;·&nbsp;waiting,&nbsp;last&nbsp;at&nbsp;14:03:12* | *Normal · waiting* (*Single · waiting*) |
| Triggered | *Normal&nbsp;·&nbsp;triggered* (*Auto&nbsp;·&nbsp;triggered*) | *Normal · triggered* (*Auto · triggered*): the same words, nothing added while the view still fills (the now edge shows it) |
| Free&nbsp;running | *Auto&nbsp;·&nbsp;free&nbsp;running* | *Auto · free running* |
| Single's&nbsp;crossing | *Single&nbsp;·&nbsp;complete&nbsp;at&nbsp;14:03:12.345* | *Single · complete*, while the view still fills *Single · complete, capturing after T*; back at now (a drag to the right end) *Single · complete · Arm to wait* |
| Stopped | *Stopped&nbsp;·&nbsp;Run&nbsp;to&nbsp;arm* | *Stopped · Run to arm*, amber |

*Triggered* stays while crossings keep coming, until none has for a window plus the hold-off, and for a second at
least: a wave that keeps crossing a short window never flips it to *waiting* (or Auto to *free running*) between its
crossings. Its text is the same at every crossing and frame, and so is its place, in the row and in the corner (the
owner saw both dance: a rate whose digits changed twice a second, and *capturing after T* coming and going at each
re-trigger). No number is written while the trigger runs, as on the reference scopes; the crossing's time is
written only in Single's state and in Normal's *last at* (to the second, set when the state changes).

- **Where it holds.** The crossing's time is found straight between the two samples around it. The view holds with
  that time at its place in the window, and the **T ▼** flag above the plot points at the crossing's time: its place
  in the window, which can be dragged. No T is drawn on the curve: the flag above and the level's marks at the plot's
  sides show the crossing, and the curve stays clear. A level moved later leaves the flag where the line crossed (the
  pointers are hollow until a crossing at the new one). The part after it fills as the
  samples come (Single says *capturing after T*), and a faint "now" edge in every lane marks where the data ends, so the empty
  part does not read as missing data. Only crossings after the trigger was armed count, not ones already in the
  memory.
- **A steady picture.** In a window shorter than a second, held on a crossing, the next crossing's view is shown once
  it is full: a repeating wave stands still, a whole picture each time. The first crossing, Single, and a window of a
  second or more hold at once and fill as the samples come.
- **Short windows lock by themselves.** Below a 100 ms window a live chart with the trigger off runs Auto on its
  busiest line: a fast line before any polled one (the first of them), else the line with the most samples in the
  window; the level at that line's middle in view (Find level's rule, taken again each second while it runs free),
  rising. A line with fewer than 20 samples in the window is not watched, and with none busier the lock does not run
  (the corner shows nothing for it): a slow polled line plotted first crosses now and then, and the lock flipped
  between free running and locked. The line is chosen again when the lines change or the one watched has too few
  samples, not at each frame. An untriggered wave at 20 ms left ghosts and labels over each other, as the eye blended frames that
  each showed the wave elsewhere; locked, it stands still whenever it crosses. The corner says *Auto (short window)*
  while it locks and *Auto · free running* while the line does not cross (after a second without a crossing), as a
  **badge**: the accent colour (the Live button's blue, not the amber of Stopped) on a tint of it, rounded, so the
  view's state is seen at a glance; its tooltip says what locks and that Display → Lock short windows turns it off. It is
  an aid, not your trigger: no row, no tab, no flag, and the toolbar's button stays Hold. Your trigger takes over
  when you turn it on; **Hold**, a pan, or a window of 100 ms or more ends it (Live brings it back).
  **Display → Lock short windows** (on by default, `chart/autoShortWindows`, 14.3) turns it off. It costs no
  binning beyond what Auto costs.
- **A fast line**'s crossing is looked for between each two of its records, never across a gap, by the engine as each
  block comes (13.9): the view holds on it at the next frame, at its record's time.
- **What works on it.** It is a held view: the measurements (chapter 8), Export to CSV, the pictures, the histogram
  and the spectrum all take it. **Stop** keeps it (a crossing waiting for its view, a steady picture's, is dropped);
  **Run** arms again. **Clear** keeps the trigger armed; a change of the window's
  length (typed, the wheel) counts the next crossing after the new view's fill.
- **Turning it off.** **Off** beside Force / Arm in the row, **Display → Trigger** unticked, or the chip menu's **Trigger on this
  line** unticked (it is ticked for the line watched, and only for it): one switch, the three always in step.
- **A line watched that leaves the chart** (its Plot unticked, its math line or its stream gone) stops the trigger:
  the row says *no line to watch* and its list shows the line's name greyed. The line saved (`chart/triggerLine`)
  stays, and when the line comes back the trigger arms on it again, in its mode. It never moves to another line
  by itself: the chart would hold on a line nobody chose.
- **Times from T.** While the view is held on a crossing in view, times are read from T, as a scope's: the time
  axis below 1 s counts its divisions from T (*-2 ms ... 0 ... +8 ms*, 7.9), the hover box says *T +1.234 ms* beside
  the clock time (in place of how long ago), and the cursors' tags and the line above the measurements say how far
  each cursor is from T (*A: T -0.250 ms · B: T +1.750 ms*, 7.7). Live (Auto running free) or with the crossing out of
  view, clock times as before.
- The line, each line's level and edge, the mode, the hold-off and the place are kept (`chart/trigger…`, 14.3); the
  trigger itself is off at each start. A recording's window has no trigger (its chips' menus do not offer it):
  nothing comes after its end.

### 7.14 Fast lines

A channel of a fast stream (13.9) is a line like a register's: its **Plot** tick is in the *Fast streams* card, under
its stream's row, with the channel's newest value beside it. The line is named by stream and channel, *ADC.I_LOAD*,
and the chart's info line counts it apart: *2/62 · 2 fast · CPU*.

- **Every record, at its own time.** A stream's records keep their numbers and the times the clock's fit gives them
  (13.9), not the times they arrived. In each pixel column the line covers the lowest and the highest record in it,
  so a view of an hour and one of 100 µs cost the same, and nothing hides: one record of a spike in ten million shows
  at every zoom, in its column's top and in Auto's range. Zoomed in until a column holds a record or less, each record
  is a point at its own time, joined by straight lines.
- **Lost records break the line.** Nothing is drawn across a gap: the line ends before it and starts again after it.
  The mouse over the gap says how many records are missing: *ADC.I_LOAD: 512 sample(s) lost here*, or *the stream
  started again here* where it did (its numbers begin again; what came between cannot be counted).
- **Below a millisecond.** The view zooms in to 10 µs, with the wheel or typed (`50 us`, 7.1). The time labels gain
  the digits the grid needs, *14:03:12.34567* or *14:03:12.345678*, fewer of them so that each stays whole with a gap
  beside it; one that would be cut at the chart's edge is left out. The crosshair writes its time to the microsecond
  in a view shorter than 10 ms.
- **The rest, as for any line:** its legend chip with its newest value, its lane by its unit (7.12), the crosshair's
  value, the cursors, notes, pictures, the memory strip, Hold and Live.
- **In a math line.** A formula over the channels of one stream (`ADC.I_LOAD * ADC.V_BUS`) is a *fast math line*,
  computed for every record of the stream and kept as a fast line's records (9.9): drawn, measured, triggered and
  exported like the channels themselves. Channels of two streams are refused (two streams, two clocks).
- **The memory.** The records are kept as they came, a few bytes each (two `i16` channels: 4 bytes a record, and
  a quarter of a byte for the summaries the chart draws from, 23.11), once for all the plotted channels of a stream.
  In the RAM budget each fast line counts as one line, as a polled one (7.3): two fast lines and two polled ones share
  it in quarters. Past the Memory, or past its share, a stream's oldest records go in whole pieces of 65 536. A RAM
  cut (4 GB filled to 512 MB) takes effect at the next block without a pause: the memory let go is freed a little
  at each frame (23.11).
- **At most 64 lines** on the chart, fast, polled and math lines together (one cap, 4.8): a tick past that is taken
  back, and the status bar says *At most 64 lines on the chart, registers, math and fast lines together: untick one
  first*.
- **On a bus** the card is greyed (13.9), its Plot ticks too.

**Measured as any line.** A fast line has its row in the Measure table (chapter 8), its total since Clear (8.4), its
histogram and spectrum (8.6), the trigger (7.13) and Export to CSV (7.10). Nothing is measured across a gap: the
area, the mean and the RMS cover each part without one, and a cursor in a gap reads `—`. The statistics come from the
same summaries as the drawing, so a range of an hour at a million records a second is measured within a frame.

## 8. Measurements

**Measure** shows a table under the chart with one row per line: every plotted register, every active math line and every fast line (7.14). **A right-click on the table's header** lists the columns, each with a tick: untick one to hide it (*Line* always stays). The choice is kept (`chart/measureColumns`); every column is shown by default. The table is computed every 250 ms while the Chart tab is shown, and at once when the cursors move; while a cursor is dragged, at most every 100 ms, A, B and B − A alone (the rest at the 250 ms pace), and all of it at the place it is left. With Measure off, nothing is computed.

A line above the table says what is measured:

- *Measured between the cursors: A → B = 2.500 s*, or
- *Measured over the view: 30.000 s (Cursors: measure between two points)*, which says *(place cursor A on the chart)* or *…B…* while cursor mode is on;
- then, while the Since Clear column is shown, since when the totals run, by the clock and how long: *· totals since 14:03:12 (1 h 12 min)* (8.4).

### 8.1 The range

- **Both cursors placed, at different times:** the range is from the earlier to the later cursor.
- **Otherwise:** the range is the view, as drawn in the last frame, live or held.

### 8.2 The values

Let *t₀ … t₁* be the range. The samples inside it are *(tᵢ, vᵢ)* for *i = 0 … n−1*, with times rising.

| Column | Formula |
|---|---|
| **at&nbsp;A**,&nbsp;**at&nbsp;B** | The line's value at the cursor's time, **interpolated linearly** between the two samples around it. `—` when the cursor is not placed, or lies before the line's first or after its last sample. |
| **B&nbsp;−&nbsp;A** | at B − at A, when both exist. |
| **Min**,&nbsp;**Max** | The smallest and largest sample inside the range. The edges are not interpolated. |
| **Area&nbsp;∫&nbsp;dt** | Trapezoids between neighbouring samples: Σ ½ (vᵢ + vᵢ₋₁)(tᵢ − tᵢ₋₁), in *unit × seconds*. |
| **Area&nbsp;/&nbsp;3600** | The same area in *unit × hours*. |
| **Mean** | Time-weighted: Area ÷ (t_last − t_first), where t_first and t_last are the first and last **samples** inside the range. |
| **RMS** | √( Σ ½ (vᵢ² + vᵢ₋₁²)(tᵢ − tᵢ₋₁) ÷ (t_last − t_first) ). |
| **Std&nbsp;dev** | The standard deviation, time-weighted as the mean: √( mean of (v − K)² − (mean of (v − K))² ), each mean by the same trapezoids, K the range's first sample. The shift by K keeps the ripple: a 12 V line with 1 mV of ripple reads 0.707 mV, where 144 V² less 144 V² would leave only the rounding. |
| **Peak-peak** | Max − Min. |
| **Since&nbsp;Clear** | The area under the line since the chart's Clear, in *unit × hours* (8.4). Not over the range: all of it. |

- **One sample only:** Mean and RMS are that sample, Std dev and Peak-peak 0, and the area is 0.
- **No samples:** the row shows `—`.

**A fast line** (7.14) is measured by the same formulas over its records, with nothing across a gap: the area, the
mean, the RMS and the standard deviation sum the trapezoids of each part without a gap (their times the parts'
own), and a cursor in a gap reads `—` (no value is made up there). Its records are evenly spaced, so a part's
trapezoids come from its sums, Δt × (Σ vᵢ − (v_first + v_last) ÷ 2), and the sums, the minimum and the maximum from
the summaries of 256 and 4096 records (23.11): what the table costs does not grow with the range.

**Time-weighted** means that a sample counts for the time it lasts. If polls come unevenly, a long gap weighs more than a short one. The mean is therefore the true average of the signal as drawn, not the average of the samples.

The area and mean cover only the time between the first and the last sample inside the range. They are not stretched to the exact cursor times.

### 8.3 Units of the area

The area's unit follows the line's unit:

| Line&nbsp;unit | Area&nbsp;∫&nbsp;dt | Area / 3600, Since Clear |
|---|---|---|
| `W` | J | Wh |
| `A` | A·s | Ah |
| `mA` | mA·s | mAh |
| none | ·s | ·h |
| any&nbsp;other,&nbsp;e.g.&nbsp;`bar` | bar·s | bar·h |

Examples:

- **Energy.** Add the math line `P = SUPPLY_V * SUPPLY_I` with unit `W` (chapter 9). Put A and B around a test run. The P row's area shows the energy in J and in Wh.
- **Charge.** The SUPPLY_I row (unit `A`) gives the charge in A·s and in Ah.

### 8.4 Totals since Clear

The **Since Clear** column is each line's area from the chart's **Clear** on (or from the start, or from when the
line came), in hours: a power in W gives Wh, a current in A gives Ah, any other unit *unit·h*. It is the energy or
the charge of a whole test run, however long, without placing cursors.

- **From every sample as it comes,** summed by trapezoids when the sample is added to the chart, not from what the
  memory keeps: the memory may hold one minute, the total covers hours, and the trims (7.4) take nothing from it.
- **A gap of more than 1 s** between two samples of a line (no polls, the device gone) is not bridged: the time
  without samples adds nothing.
- **A fast line** sums its records as its blocks come, with nothing across a gap (records lost); from the records
  that come after it was ticked.
- **Reset only by Clear** (and by another map, whose lines are others). A line taken off the chart and put back keeps
  its total (the time it was off adds nothing). Math lines have their totals too.
- The line above the table says since when: *totals since 14:03:12 (1 h 12 min)*, the clock time of the first sample
  after the Clear and how long ago that was.

### 8.5 Number format

Values show five significant digits from 1e6 up and below 1e-3. Between those, they show 1 to 4 decimals depending on size: 1 from 1000, 2 from 100, 3 from 1, 4 below 1. Each value carries the line's unit. The table's cells can be selected and copied.

### 8.6 Histogram and spectrum

**Click a line's chip** in the legend (its **▾**), or right-click it: **Histogram of …** or **Spectrum of …**. It is computed over A → B
when both cursors are placed, else over the view, and shown in a small window of its own; the chart goes on, the
window keeps what it was given. Its title says what: *Spectrum of SUPPLY_I — A → B, 2.5 s*.

- **Histogram.** How the line's samples spread: bins of equal width by the Freedman–Diaconis rule, 2 × IQR ÷ n^⅓
  (IQR: the spread of the middle half), which a few outliers do not widen; values that do not spread between their
  quartiles (a few levels) get √n bins, a single value one bin; at most 2000 bins. The bars count samples, not time.
  A fast line gives every record in the range, at most 16 million (the first; the title then says *its first …*).
- **Spectrum.** Which frequencies the line holds, as amplitudes in its own unit: a sine of 2 V reads 2 V at its
  frequency, the mean at 0 Hz. Polls are uneven, so the samples are first resampled to even steps at their mean
  rate (straight between neighbours). Then Welch's method: segments of a power of two samples, about 8 of them
  overlapping by half, each with a Hann window, their power averaged; from 0 Hz up to half the rate, as many
  frequencies as half a segment. The line above the plot says the rate, the segments, their spacing and the peak:
  *10000 samples, resampled to 1000 Hz · 8 segments of 2048, Hann, 50 % overlap · 0.4883 Hz apart · peak 62.5 Hz:
  2 V*. **Log** puts the amplitude on a log scale, 6 decades under the peak. Fewer than 16 samples give no spectrum.
  A fast line's records are evenly spaced already: they are taken as they are, not resampled (*39710 samples at 10000
  Hz, evenly spaced · …*). Even steps hold only within a part without a gap, so over a range with a gap the spectrum
  takes the longest such part, and the title says so: *Spectrum of ADC.I_LOAD — A → B, 1.98 s: 1.39 s of it without
  a gap*. Rates are written whole up to ten million (*10000 Hz*).
- **The readout.** The mouse over the plot shows a dashed line and, above the plot, the bin and its count
  (*12.05 … 12.06 V: 341 samples (13.6 %)*), or the frequency and its amplitude (*62.5 Hz: 2 V*).
- **Copy picture**, **Save picture…** (PNG) and **Export CSV…**: `from [unit],to [unit],count,share_percent`, or
  `frequency [Hz],amplitude [unit]`.

## 9. Math lines

A math line is a formula over registers, drawn and measured like a register's own line. For example, a power: `P [W] = SUPPLY_V * SUPPLY_I`. A formula over the channels of one fast stream, `P [W] = ADC.I_LOAD * ADC.V_BUS`, is a fast math line, computed for every record of that stream (9.9).

### 9.1 Making and managing them

**ƒ Math → New math line…** opens a dialog with three fields:

- **Name**: offered as `P` for a new line
- **Unit**: offered as `W`; it sets the measurements' area units (8.3)
- **Formula**

**Completion.** While a name is typed in the formula, a list under the box offers what it may become: the map's
numeric registers (the ones a formula can read), each with its unit and description, the fast streams' channels
(`ADC.I_LOAD`, with its unit and *fast, every record of ADC*), and the functions and constants, each function with
its parameters (`atan2(y, x)`). The best first: names that start with what is typed,
then names with a part after `_` or `.` that does (`I` finds SUPPLY_**I**), then names that contain it, in any
case. Up and Down pick one, **Enter** or **Tab** takes it, **Esc** closes the list. A register goes in as its name, a
function as `name()` with the cursor inside the brackets. Only the word at the cursor is replaced: the rest of the
formula stays. The functions and constants come from the parser's own table (`Expr::builtins`), so the list never
offers one the formula would refuse.

The formula is checked against the map as you type. The dialog shows either *OK: reads SUPPLY_V, SUPPLY_I* in green (a fast math line: *OK: reads ADC.I_LOAD, ADC.V_BUS · computed for every record of stream ADC*), or what is wrong in red. **OK** needs a valid formula and a name. The Log records *math line P = SUPPLY_V * SUPPLY_I*.

The **ƒ Math** menu lists every line as `P = SUPPLY_V * SUPPLY_I`. A line whose formula does not compile against the current map shows its error: `X = FOO * 2  (no register "FOO" in the map)`. Each line has a submenu:

- **Shown**: on or off. It is disabled while the formula has an error.
- **Edit…**
- **Remove**

Math lines are kept in the settings (`chart/math`; a recording's window keeps its own, `recording/math`, 12.6), not in the map. They are saved at every change and come back at the next start. They are compiled again whenever the map changes. A line that names registers the current map does not have shows its error and is not drawn. It works again with a map that has them.

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
| 1 | `( … )`,&nbsp;function&nbsp;calls | |
| 2 | `^` | power, right-associative: `2^3^2` = 2⁹ = 512. The right side may carry a sign: `2^-1` = 0.5. |
| 3 | unary&nbsp;`-`,&nbsp;`+` | applies to the whole power: `-2^2` = −4 |
| 4 | `*`,&nbsp;`/` | left to right |
| 5 | `+`,&nbsp;`-` | left to right |

### 9.4 Functions and constants

Function names match in any case.

| Function | Arguments | Result |
|---|---|---|
| `abs(x)` | 1 | \|x\| |
| `sqrt(x)` | 1 | √x |
| `exp(x)` | 1 | eˣ |
| `log(x)`,&nbsp;`ln(x)` | 1 | natural logarithm |
| `log10(x)` | 1 | base-10 logarithm |
| `sin`,&nbsp;`cos`,&nbsp;`tan` | 1 | radians |
| `asin`,&nbsp;`acos`,&nbsp;`atan` | 1 | radians |
| `atan2(y, x)` | 2 | the angle of (x, y), in radians |
| `min(a, b)`,&nbsp;`max(a, b)` | 2 | |
| `pow(a, b)` | 2 | aᵇ |
| `floor`,&nbsp;`ceil`,&nbsp;`round` | 1 | `round` rounds halves away from zero |
| `sign(x)` | 1 | −1, 0 or 1 |
| `clamp(x, lo, hi)` | 3 | x limited to [lo, hi]; the limits may be in either order |

| Constant | Value |
|---|---|
| `pi`&nbsp;(any&nbsp;case) | 3.14159… |
| `e`&nbsp;(lower&nbsp;case&nbsp;only) | 2.71828… |

### 9.5 Errors

| Message | Cause |
|---|---|
| `empty` | No formula. |
| `no register "X" in the map` | An unknown name, or a register that is not numeric (a byte array). |
| `X and Y: two streams, two clocks: not in this version` | The formula names channels of two fast streams (`ADC.I_LOAD * PWR.P_IN`). A fast math line follows the records of one stream (9.9); two streams have two clocks, and their records do not fall at the same times. |
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

A fast math line is not matched by poll times: it follows its stream's records, and a register in it is held (9.9).

### 9.7 The chart key

Inside the chart, each line has a numeric key:

- a register's line uses `regKey(slave, address)`: the slave in bits 16 to 23, the address in bits 0 to 15
- math line *i* uses `FIRST_CHART_KEY + i` (`FIRST_CHART_KEY` = 1 << 24), clear of every register key
- a fast math line *i* is a fast line of the chart's stream `MathLines::fastStream(i)` (`FAST_STREAM` = 1024 + *i*,
  clear of the map's streams): its key is `ChartView::fastKey(1024 + i, 0)` (`ChartTab::fastMathKey`)

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
| P_load | W | `ADC.I_LOAD * ADC.V_BUS` | The load's power at every record of the stream ADC: a fast math line (9.9). |
| I_cal | A | `ADC.I_LOAD * CAL_GAIN` | A fast channel times a polled calibration factor, held at its last polled value. |

### 9.9 Fast math lines

A formula whose names include channels of a fast stream (7.14, `STREAM.CHANNEL`), all of **one** stream, is a fast
math line. The dialog says so: *OK: reads ADC.I_LOAD, ADC.V_BUS · computed for every record of stream ADC*.

- **Every record, at its own time.** A stream's record holds all its channels at one instant, so the line has a value
  for each record, at the record's time, from the channels of that record: at a million records a second, a million
  values a second. Lost records break it as they break the channels' lines.
- **A register is held.** A register in the formula (a calibration factor, `ADC.I_LOAD * CAL_GAIN`) is held at its
  last polled value for each record. Until the register's first poll there is nothing to hold, and no record is
  computed: the line begins at the first block after it. In a recording's window the value polled at or before each
  record is taken (before the file's first poll, the first).
- **No number, no record.** A record whose result is not a finite number (`sqrt` of a negative value, a division by
  zero) is left out: the line breaks there, as over lost records.
- **Like a fast line.** Its records are kept as a fast line's (a 32-bit float each, about seven digits, and the
  summaries the chart draws from): drawn, binned from its own summaries, measured (chapter 8), triggered (7.13: its
  crossing is found as its records are made), exported (a row per record, 7.10), its histogram and spectrum. Its
  records count in the RAM budget as one fast line (7.14), and it is one of the 64 lines (4.8). It is named *ƒ P*
  and counted with the math lines in the info line.
- **From the next block.** A fast math line made, shown again or edited starts with the stream's next block; the
  stream must be on (13.9), its channels need not be plotted.
- **Recorded.** Record CSV writes the stream's records beside the CSV (`run.ADC.evrs`, 13.9); a recording's window
  computes its own fast math lines (`recording/math`, 12.6) from them, record by record.
- **Two streams, two clocks.** Channels of two streams are refused: *ADC.I_LOAD and PWR.P_IN: two streams, two
  clocks: not in this version* (9.5).

## 10. The Monitor tab

The Monitor shows the frames on the link and sends single requests by hand.

### 10.1 The request row

| Control | Meaning |
|---|---|
| **Slave** | The device the request goes to: the map's slave, or on a bus the selected device's (on a bus a list of the devices by name, as in 3.9, and *Broadcast · slave 0* last). With one device it is a number that follows the sidebar's Slave, and **0 (broadcast)** is the broadcast address (3.10); leaving slave 0 restores the function chosen before. Slave 0: the function locks to WRITE (no ack), the rule of 3.10 is checked first (*!! no broadcast: ...*), every device takes it and none answers. |
| Function | **READ**, **WRITE + ack** (`WRITE_ACK`) or **WRITE (no ack)** (`WRITE`). |
| **Address** | `0xA000` (the default), or a decimal address. 0 – 0xFFFF. Named *Address* beside it. |
| **Count**&nbsp;/&nbsp;**Bytes** | Named after the function: *Count* for READ, *Bytes* for a WRITE. For READ: the byte count, 1 – 65535, in decimal or `0x…` hex (default `2`). For WRITE: the value as hex bytes, **the low byte first** (the protocol is little endian): `2C 01` writes 300 (0x012C). `2C 01`, `0x2C 0x01`, `2C,01` and `2c01` all work; anything else is refused (*!! not hex bytes*): a decimal `300` is not taken as `03 00`. The box's hint says which it wants, and switching to a WRITE empties READ's count. |
| **Send**&nbsp;(or&nbsp;Enter&nbsp;in&nbsp;either&nbsp;box) | Sends the request. |
| **Log&nbsp;frames** | Shows every frame sent and received. Off at every start. |
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
| `TX`&nbsp;/&nbsp;`RX` | Sent by the Studio / received from the link. |
| bytes | The whole frame in hex, from the start byte `7B` to the end byte `7D` (layout in 18.1). |
| note&nbsp;(RX&nbsp;only) | The function name: `READ_RESP`, `WRITE_ACK_RESP` or `ERROR_RESP`. For an error answer the code is the data byte before the CRC (codes in 18.3). |
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
| `recording opened: <path>` | Info |
| `chart exported: 1000 rows to <path>`, `chart picture saved to <path>` | Info |
| `chart not exported to <path>: <why>` (cancelled too), `notes not saved beside <path>: <why>` | Warning |
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
   - **It covers nothing.** It sits in the free space right of the tab bar (in Arabic, right to left, the tabs are on the right and it sits left of them), as one line, shortened to fit, its coloured edge where its text begins. It is placed again whenever the tab bar moves or changes size, the direction or the language changes, and the window is resized. Hover it for the full text. **Show in Log** opens the Log tab and closes the pop-up, and a later resize does not bring it back. When that space is narrower than 260 pixels, the status bar shows the message instead, for the same time.
   - **It never blocks.** No pop-up waits for a click.

### 11.4 The Log tab's counter

While another tab is shown, the Log tab's title counts the warnings and errors logged since you last looked: **Log (3)**. Opening the tab sets the count back to zero. Info lines and repeated lines are not counted.

## 12. CSV recording

### 12.1 Starting and stopping

**● Record CSV**, in the *Polling & recording* card, asks for a file. Beside it, **Open** opens a recording (12.6). The suggested name is `evre_<yyyyMMdd_HHmmss>.csv` in your home folder. An existing file is overwritten.

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

### 12.5 Notes beside a recording

The chart's notes (7.11) are kept in a file beside the recording, its name with `.notes.json` added
(`run.csv.notes.json`). While a recording runs, the live chart's notes from its start on are written there at
every change and when it stops; an export writes the notes of its span. No note: no file.

```json
{
    "format": "evre-notes/1",
    "notes": [
        { "time_s": 130.5, "datetime": "2026-10-05T09:00:30.500", "text": "valve shut" }
    ]
}
```

`time_s` is on the recording's own clock, the column of the same name; `datetime` is there to be read.

### 12.6 Opening a recording

A recording, or an export of the chart (7.10), opens in a **window of its own**: its chart, with Measure, cursors,
math lines, notes and the right-click menu as on the Chart tab. The live chart goes on meanwhile, and several
recordings can be open.

- **Open it:** **Open** beside **● Record CSV** (*Open recording…*, or one of the last 8 recordings), **Open
  recording…** or **Recent recordings** on the chart's right-click menu, or drop a `.csv` (or a fast stream's `.evrs`,
  12.7) on the window.
- **Read on a thread.** A progress dialog shows while a big file is read, with **Cancel**. A file that is not a
  recording (its first line is not `time_s,datetime,…`) says so.
- **The RAM applies.** A recording takes about 39 bytes a sample: the chart's 23 (7.4) and the window's own copy of
  the file's values (16), from which a math line or a field added later is computed. When the file's samples need
  more than the chart's RAM, it asks: *Keep the last part: about the last 12 min of 1 h 30 min?* **Keep the last
  part** reads from that place on; **Cancel** opens nothing.
- **The window.** Its title is the file's name and its span: *run.csv · 2026-10-05 09:00:00 – 10:30:00 (1 h 30 min)*.
  The line above the chart says the same, with the lines, the rows and the columns left out. The chart is held on
  the whole recording: there is no Live, Memory, RAM, Clear or Remove all, and Smooth is off; Window, the wheel, a
  drag and the memory strip move through it as through a held chart. Its row of Window and Y range keeps Window on
  the left and the Y range on the right, as on the Chart tab (the hidden Memory and RAM leave a gap, not boxes and
  labels spread over the row).
- **At once, whole.** It opens with the file's lines, their values in the legend and its Y range (the min and max
  boxes show the range of its first frame, in Auto's grey). No frames come to a held chart by themselves: a change
  is painted when it is made, and a window made bigger is painted whole on either drawing path (on a card, a frame
  the system let go is drawn again: 23.7). A theme switched in the main window reaches it too.
- **The columns** come by their titles, `NAME [unit]` (a title without `[…]` has no unit). Each is a line; the first
  64 are plotted, and the **Lines** menu shows or hides each. A cell that is empty is no sample. A column with a cell
  that is not a number (a byte array's hex) is left out.
- **With a map loaded,** a column named as one of its registers takes that register's definition: its value names
  and fields. **Lines** then lists the fields of such a register (*Fields of CONFIG*) and plots one as a math line
  (`CONFIG.MSG_ENABLE`, 31.4). A byte array of the map is left out.
- **Its own settings.** The window's chart keeps its choices under `recording/…` (14.3): its Window, Y range, Measure
  and columns, Display, and its **own math lines** (`recording/math`), computed from the file's values.
- **Notes** beside the file (12.5) are shown, and saved again at every change.

### 12.7 Fast streams' recordings (.evrs)

A fast stream (13.9) cannot go into the CSV: a row per sample would be about 60 MB of text a second at a million
samples a second. While **● Record CSV** runs, each stream that sends is recorded **beside the CSV, as it came**:
`run.csv` gets `run.ADC.evrs`, one file a stream. Nothing is converted and nothing is lost: gaps stay gaps.

- **Written by the I/O thread**, the blocks as they arrive, from the first block after the recording starts to
  **■ Stop recording**; whether the chart shows the stream or not. With the CSV's flush (every 250 ms) its data goes
  to the disk. The Log says at the stop: *fast stream ADC recorded: run.ADC.evrs (1517 blocks, 5.9 MB)*.
- **The clock.** The file's time marks are the clock's fit (13.9) on the Studio's clock, the CSV's `time_s`: a
  recording opened with its CSV lays both on one time axis.
- **The format** (`evre-fast-rec/1`, FAST_PLAN.md section 8.3): pieces, each a name of 4 bytes and a length before
  its body. `EVRS` first, the head in JSON: the format, the map's device, the stream as the map writes it, the start's
  local time and the Studio's clock then (`start_s`). `TIME`: a record's number (u64) and its time (f64), before
  the first block of every start and then about once a second. `BLK `: a block as it came, its 8-byte header and its
  records. A reader skips a piece it does not know. `evre record` (34.x) writes the same, its clock the seconds since
  it began.

**Opening one.** **Open recording…** (the dialog lists `*.csv *.evrs`), the recent recordings, or a `.evrs` dropped on
the window:

- **A CSV** opens with the streams' recordings beside it (`run.*.evrs`): its columns and a fast line per channel
  (7.14), on its clock. **A `.evrs` alone** opens with its fast lines only, on the wall clock of its head.
- **Mapped, not read.** The file is mapped into memory and only the summaries are made (23.11), on the same thread
  and progress dialog as the CSV: a recording larger than the RAM opens, and the RAM question (12.6) counts only the
  CSV.
- **The line above the chart** adds each stream: *· ADC: 1 517 000 samples, 1 024 lost*.
- **A file cut off** (a crash, a full disk) opens up to its last whole piece, and that line says *(the file ends cut
  off: read up to its last whole piece)*. A file that is not a recording says so.
- **Lines** lists the channels too, each with its tick. A fast line in the window is measured, exported and analysed
  as on the live chart (7.14); its total since Clear covers the whole file.
- **From Python:** `evre.read_recording('run.ADC.evrs')` (the `python/` package) gives the samples' numbers, times
  and values in the map's units, the gaps and the starts.

## 13. Polling performance and tuning

### 13.1 What a poll is

A **poll** reads every polled register of the map once. Registers are not asked one by one: they are merged into a few **block reads**, one READ request each. A poll is done when all its blocks have answered.

The sidebar shows the result, for example *10.0 polls/s, 15 registers in 3 reads*, beside the interval asked for. The status bar shows the link's numbers only (latency, traffic, errors), not the rate a second time.

### 13.2 The interval

**Poll** (ticked at every start; the tick is never saved) and **every** *interval*, in the *Polling & recording* card, set the polling:

| Setting | Behaviour |
|---|---|
| Interval | 0.05 – 60000 ms (default 100): polls start on a clock at that interval. The field has two decimals; anything below 0.05 ms is run at 0.05 ms. |
| **max**&nbsp;(0) | Polls run back to back. The next one starts as soon as a slot is free. |
| **Poll**&nbsp;unticked | No polling. The keep-alive (18.7) still runs, and values turn grey. |

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
| The&nbsp;box | Offered only with one device on the link (not on a bus: devices sending by themselves would collide on a shared link), once connected and STATUS (read at connect) has `CAP_AUTO_SEND`. Otherwise disabled; its tooltip says why. Never saved: it changes the device, so it is off at every start. |
| The&nbsp;rate | The 16 rates the device makes exactly, 8000 Hz divided by 2, 4, 5, 8, 10, 16, 20, 25, 32, 40, 50, 80, 100, 125, 160, 200 (prescaler + 1): 4000, 2000, 1600, 1000, 800, 500, 400, 320, 250, 200, 160, 100, 80, 64, 50, 40 Hz. The prescaler is the device timer's reload and a reload of 0 stops a timer, so it is never 0 (a device replaces a 0: by 1, or by its default 0x4F); the protocol's rates are 4000 Hz to 40 Hz. Default 100 Hz, saved (`poll/autoSendHz`). A change while on is written to the device at once. |

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

### 13.9 Fast streams (Fast EVRe)

A device that takes samples on its own clock (a current at 100 000 samples a second, say) can send them in numbered
blocks: **Fast EVRe**, a layer above the protocol (PROTOCOL.md, "Fast EVRe"). Each block is a READ_RESP nobody asked
for, at the first address of the stream's window, a span of the device bank that the map gives to the stream
(`streams` in the map, 16.x and MAP_FORMAT.md section 9). A block is a header of 8 bytes (the number of its first
sample, how many it holds, two flags) and its samples, every channel of one instant together. Three things that sound
alike stay apart: **Auto send** (13.8) is the device's read-only block at a timer's rate; the API's **`stream`**
command (17) sends values at a period to an API client; a **fast stream** is one of the map's `streams`.

The **Fast streams** card, under *Device map*, shows a row for each stream of the map (it is hidden for a map
without one), headed by the stream's name (its description in the tooltip):

| Part | Behaviour |
|---|---|
| **▶&nbsp;Start&nbsp;stream** | Switches the stream on: the stream's `rate_reg` (if the map names one) is read for the rate the device was set to, then its `enable` register is written 1 with WRITE_ACK. A stream without `enable` is the device's own business: Start only listens for its blocks. The button turns red, **■&nbsp;Stop&nbsp;ADC**, which writes 0. Never saved: it changes the device, so every stream is off at every start. Its tooltip says what it writes. |
| The&nbsp;rate | *off · 10.0 k samples/s* (the map's rate) while off; *waiting for the first block*; then *10.0 k samples/s (+32 ppm)*: the samples a second as the Studio's clock measures the device's, and the correction against the rate the device was set to (below). Its tooltip counts the samples and blocks since Start, the bad blocks, and the samples not shown (below). |
| Lost | *lost 0*, or *lost 1 024* in amber: samples the device numbered that never arrived, counted from the blocks' numbers (a gap, never filled in). |
| Channels | Under the stream's row, each channel: its **Plot** tick (its line on the chart, 7.14) and its newest value with its unit, at the pace of *Show values*. The tick's tooltip says what it draws and gives the channel's `desc`. |
| Greyed | With no link (*not connected*) or a bus (*not on a bus*: a device sending by itself would collide with the others on a shared line). The button's tooltip says why. |

What the Studio does with a stream on:

- **The blocks:** a READ_RESP from the device's slave at a stream's window that answers no request is that stream's
  block, recognised before the auto send test. It is checked by the block's rules: its count must be exactly 8 +
  samples x the record's size (else it is a bad block, counted, none of it used); a flag or a spare byte this Studio
  does not know marks a newer kind of block (counted, none of it used, the Log says so once); the number of its first
  sample against the end of the block before gives the samples lost; a number that goes back, or the START flag, is a
  new start of the stream. The numbers are kept in 64 bits across the device's 32-bit wrap.
- **Time:** a sample's time is its number and the rate, laid on the Studio's clock (the chart's and the CSV's time
  base) from the first block's arrival, and corrected slowly from the blocks' arrival times: a block can arrive late,
  never early, so the earliest arrival of each second is taken as the truth. The correction moves the rate by at
  most 10 ppm a second (faster only when the clock is more than 10 ms off, a device far from its map's rate) and
  never steps the time. A device 200 ppm fast or slow stays within 2 ms of the Studio's clock (the unit test checks it
  after one minute and after ten); the correction shown comes within 20 ppm of the truth. A new start gets a new fit.
- **The device's host watchdog:** while a stream is on, CONFIG is read every 100 ms whatever the polling is, as for
  auto send (13.8): a device that stops its streams after 2 s without a request never does.
- **No block:** when not one block has come 2 s after the device took the enable, the stream is switched off again (0
  written), the button turns back to Start, and the Log says *fast stream ADC switched off: no block came in 2 s*. A
  stream that sent blocks and then went silent as long has its enable read once: 0 (a reset of the device, another
  host) is told in the Log, *the device stopped fast stream ADC (ADC_STREAM reads 0: a reset?)*, and it stays off.
- **Disconnect and closing the window** write 0 to every stream that may be sending, straight on the link and flushed
  before it closes, the link drained, as for auto send's off. A lost link cannot; the device's watchdog is then the
  safety. Disconnect also turns the buttons back to Start, so nothing goes on again at the next connect.
- **A lost link** keeps the streams wanted: after the reconnect (Reconnect by itself) they are switched on again.
- **The Monitor** names a block *READ_RESP (fast stream ADC)* (with *Log frames* ticked).
- **A map edited** keeps each stream's state by name; a stream removed from the map while on is told 0 first.

- **The trigger** (7.13) on a fast line: the engine looks for the crossing in each block as it comes, at its record,
  and hands it to the chart with the block, which holds on it at the next frame.
- **Polls beside a stream.** A poll's answer comes on the same link after the blocks the device sent before it.
  Measured with `evre_fake_fast` at a million records a second (4 MB/s) and SUPPLY_V polled every 100 ms (and at
  *max*): no timeout in 60 s, the answers in about 0.6 ms, also with every core of the PC busy and with the window
  held: the stream itself does not make the polls time out in the Studio. A real device whose stream fills most of
  its link, or that answers only between its blocks, makes the answers late: when the Log reports timeouts while a
  stream runs, raise **Timeout** (3.3) or lower the stream's rate.
- **To the window.** The blocks wait for the window's next frame in a queue of at most 64 MB (at a million samples a
  second of 4 bytes, 16 s). A window that stalls longer loses the oldest: their samples are counted as *not shown* in
  the rate's tooltip, and their line breaks there. A frame takes the blocks into the chart for about 8 ms at most:
  after the window was held (a title bar's button pressed, a dialog closed with its X) the chart moves on at once,
  and the blocks that piled up follow over the next frames, in their order; none of them is lost (beyond 64 MB
  waiting, a frame takes them all).

The chart draws the channels (7.14), and a CSV recording records the streams beside it (12.7). The API gives them to
other programs (17.3, *Fast streams*: a channel's newest record and each period's min, max and mean; 17.4: the blocks
as they came); the Registers tab lists only the registers. `evre record` (34.x) writes a stream's blocks to a file (`.evrs`) and `evre info` lists a map's streams.

## 14. Command line, environment variables, settings

### 14.1 Command-line options

```
EVReStudio [--tcp host:port | --serial COMx[:baud]] [--map file.json | --bus bus.json]
           [--plot NAME,NAME] [--tab registers|chart|monitor|map] [--connect] [--fast NAME,NAME]
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
| `--fast NAME,NAME` | Starts these fast streams (13.9; names in any case) once connected, as their **Start stream** does: for a script that reads them through the API (17.3). A name the map has no stream of is left, and the Log says which. Like Start, it is not remembered. |
| `--interval ms` | The poll interval. `0` is *max*, `0.25` is 4000 polls/s. |
| `--inflight n` | In flight, for the link type chosen, as if you typed it: it is saved like a typed value. |
| `--record file.csv` | Starts a CSV recording at once. Rows come as polls complete. |
| `--api` | Ticks Serve API. |
| `--api-writes` | Ticks Allow API writes. Implies `--api`. |
| `--api-writes-danger` | Also ticks including ⚠ registers. Implies both of the above. |
| `--help`,&nbsp;`--version` | Prints the help or the version, and ends. |

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
10. fast streams
11. connect

The link fields, the interval and the other sidebar values set by options are saved on close like typed values. An option therefore also changes what the next plain start uses.

### 14.2 Environment variables

| Variable | Effect |
|---|---|
| `EVRE_TOKEN` | Fills the token box at start (see 3.6). This is the only way to pass a token without typing it: a command line is visible to other users of the computer. |
| `EVRE_SHOT` | A test aid that saves a picture of the window and quits (see 26.6). While it is set, the Studio uses separate settings (`EVReStudio-test`), so yours are not touched. |
| `EVRE_PERF_LOG` | A timing aid: every 500 ms a line of what the chart's frames cost is appended to the file it names (see 26.8). Nothing when unset. |

### 14.3 Settings

The Studio saves its settings with Qt's `QSettings`, under the organisation `teknile` and the application `EVReStudio`:

- **Windows:** the registry, `HKEY_CURRENT_USER\Software\teknile\EVReStudio`
- **Linux:** `~/.config/teknile/EVReStudio.conf`

| Key | Default | Saved | Meaning |
|---|---|---|---|
| `link/tcp` | `true` | on&nbsp;close | TCP (true) or Serial / USB. |
| `link/host` | `127.0.0.1` | on&nbsp;close | TCP host. |
| `link/port` | `1210` | on&nbsp;close | TCP port. |
| `link/serial` | empty | on&nbsp;close | Serial port name. |
| `link/baud` | `115200` | on&nbsp;close | Baud rate. |
| `link/timeout` | `500` | on&nbsp;close | Answer timeout, ms. |
| `link/reconnect` | `true` | on&nbsp;close | Reconnect by itself. |
| `link/inflightTcp` | `4` | on&nbsp;change | In flight for TCP. |
| `link/inflightSerial` | `1` | on&nbsp;change | In flight for serial. |
| `poll/interval` | `100` | on&nbsp;close | Poll interval, ms (0 = max). |
| `poll/autoSendHz` | `100` | on&nbsp;change | Auto send's rate, Hz (13.8): one of 8000 / (prescaler + 1) for the 16 prescalers offered (4000 Hz to 40 Hz); another number, an 8000 saved by an older version too, takes the nearest. |
| `api/on` | `false` | on&nbsp;close | Serve API. |
| `api/network` | `false` | on&nbsp;close | Network (not only this computer). |
| `api/evrePort` | `1219` | never&nbsp;(read&nbsp;only) | The EVRe pass-through port. Change it by editing the settings. |
| `api/jsonPort` | `1220` | never&nbsp;(read&nbsp;only) | The JSON port. Change it by editing the settings. |
| `map/bus` | empty | on&nbsp;close | The bus file to open at start (3.9), in place of `map/file`: the bus last opened or saved. Empty: none (a map opened, made, or the bus closed). |
| `map/file` | empty | on&nbsp;close | The map to open at start (an absolute path): the map last loaded or saved. A load or a save only notes it; the key is written when the window closes normally, so a program that ends otherwise keeps the old value. |
| `ui/dark` | `true` | on&nbsp;close | Dark theme. |
| `ui/language` | `system` | on&nbsp;change | The language: `system`, `en` or `ar`; applied at the next start (14.4). |
| `ui/geometry` | none | on&nbsp;close | The window's size and place. |
| `ui/decodedColumn` | `false` | on&nbsp;change | Decoded column shown. |
| `ui/quickBits` | `false` | on&nbsp;change | Quick write: Bits ticked. |
| `ui/popups` | `true` | on&nbsp;change | Log: Pop-ups. |
| `ui/valueRate` | `10` | on&nbsp;change | Show values: values per second on screen (2, 5, 10, 30; 0 = every frame). Another number reads as 10. |
| `chart/window` | `30` | on&nbsp;change | Window, seconds. |
| `chart/memory` | `60` | on&nbsp;change | Memory, seconds. |
| `chart/drawing` | `0` | on&nbsp;change | Drawing: 0 Auto, 1 a dedicated card, 2 the processor's graphics, 3 the CPU (`ChartView::Drawing`). |
| `chart/ramMB` | `2048` | on&nbsp;change | RAM: the most memory the chart's samples take, all the lines together, MB (7.4); kept within 256 and three quarters of the computer's memory. |
| `chart/smooth` | `true` | on&nbsp;change | Smooth. |
| `chart/hoverValues` | `true` | on&nbsp;change | Hover values: the crosshair's box. |
| `chart/autoShortWindows` | `true` | on&nbsp;change | Lock short windows: below a 100 ms window a live chart with the trigger off locks on its busiest line (7.13). |
| `chart/timeGrid` | `0` | on&nbsp;change | Time grid: 0 Auto (divisions below a 1 s window), 1 Clock times, 2 Divisions (`ChartView::TimeGrid`, 7.9). |
| `chart/yAuto` | `true` | on&nbsp;change | Y range Auto. |
| `chart/yLog` | `false` | on&nbsp;change | Y range Log (7.5); with `chart/yAuto` for its range. |
| `chart/yMin`,&nbsp;`chart/yMax` | `0`,&nbsp;`1` | on&nbsp;change&nbsp;(Manual) | The Manual Y range. |
| `chart/measure` | `false` | on&nbsp;change | Measure shown. |
| `chart/lanes` | `false` | on&nbsp;change | Lanes (7.12). |
| `chart/triggerLine`, `chart/triggerMode` | none,&nbsp;`1` | on&nbsp;change | The trigger's line (by name) and mode (0 Single, 1 Normal, 2 Auto) (7.13). The trigger is off at each start. |
| `chart/triggerLevels` | empty | on&nbsp;change | Each line's trigger level and edge, by its name: one text each, `name⇥level⇥edge` (0 rising, 1 falling, 2 either) (7.13). When it is not there, the one level and edge saved before (`chart/triggerLevel`, `chart/triggerEdge`) are taken for the line saved. |
| `chart/triggerHoldoff` | `-1` | on&nbsp;change | The trigger's hold-off, seconds, 0 to 10; -1: the window's length (7.13). |
| `chart/triggerPosition` | `0.5` | on&nbsp;change | The crossing's place in the window, 0 to 0.9 of it from its left (7.13): a tab with none saved starts at the middle, a saved place is kept. |
| `chart/laneY` | empty | on&nbsp;change | The lanes' Y ranges by unit: one text each, `unit⇥auto⇥log⇥min⇥max` (1 or 0 for auto and log). |
| `chart/lanesFolded` | empty | on&nbsp;change | The folded lanes, by unit: a list of units (7.12). |
| `chart/laneHeights` | empty | on&nbsp;change | The lanes' heights by unit, as shares of the room: one text each, `unit⇥weight` (1 is the equal share; a lane not listed has 1) (7.12). |
| `chart/measureColumns` | empty | on&nbsp;change | The measurement columns hidden, by key (`atA`, `atB`, `diff`, `min`, `max`, `mean`, `rms`, `std`, `p2p`, `area`, `areaHours`, `total`); empty: all shown (8). |
| `chart/math` | empty | on&nbsp;change | Math lines: one text per line, `name⇥unit⇥formula⇥1\|0`. The last field means shown, and a line without it counts as shown. |
| `recording/…` | as&nbsp;`chart/…` | on&nbsp;change | The recording windows' chart (12.6): the same keys as `chart/` (`recording/window`, `recording/math`, …); Memory, RAM and Smooth are not used there. |
| `recording/recent` | empty | at&nbsp;each&nbsp;open&nbsp;or&nbsp;export | The last 8 recordings opened or exported, newest first. |

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

### 14.4 Help, the theme and the language

The foot of the sidebar holds two buttons, the language and the version line (*v1.0.0 · teknile*):

- **Help** (tooltip *Help (F1)*) opens the built-in help, a short form of Part I, in a window of its own that does not block the main window. **F1** anywhere does the same. Its pages: Getting started, Connecting, Polling & speed, Registers & writes, Chart & recording, Device maps, Map editor, API (MATLAB, LabVIEW, Python), Monitor, Log & pop-ups, Command line, Keys & mouse.
- The **theme button** switches between the dark and the light theme at once: the window, the table's glow colour and the chart follow without a restart. Its text names the other theme: *☀  Light theme* while the dark one is shown, *☾  Dark theme* while the light one is. The Studio starts dark; the theme in use is saved on close (`ui/dark`).
- **Language**: **System** (the default: the computer's language when the Studio has it, else English), **English**
  or **العربية** (Arabic), each in its own words. It is saved at once (`ui/language`) and applied at the next start: a
  window rebuilt in another language half-way would keep the texts made before. While the choice is not the language
  running, **Restart now** under it closes the Studio (asking about an unsaved map as a close does) and starts it
  again with the same command line.

**Arabic** translates every text of the window, its messages and the Help pages, and lays the window out right to
left: the sidebar on the right, the rows of controls and the tables mirrored, the Help's paragraphs right to left.
What reads left to right in any language stays so: the chart (time runs to the right; its legend, labels and
crosshair), the analysis windows' plots, the bit view, the Monitor's frames and its address and bytes fields, and
within any text the numbers, units, register names, addresses and code. Numbers keep Western digits and a decimal
point (`12.05 V`, not the Arabic-Indic digits of an Arabic system's locale). A number and its unit are one left to
right piece (between U+2066 and U+2069, invisible): `ltrPiece` wraps them where the code joins them (the times and
durations of `secondsText` and `durationText`, the memory strip's, the sizes of *needs …*), and the translations
wrap the ones they write (*%1 fps*, *%1 ms*, the Help's *10 s* and *2 V*), so *0.886 s* never reads *s 0.886*. Counts take Arabic's plural forms
(*4 أجهزة*, *11 جهازًا*); a text with two counts names them as labels (*الخطوط: 5 · الصفوف: 600*). Qt's own buttons
(OK, Cancel in its dialogs) come from Qt's `qtbase_ar.qm` when it is installed beside Qt. The Log file's lines are
written in the language running. The exports a person reads (the Markdown specification) follow the language; the
files programs read (CSV, JSON, the C header, the Python module) do not change.

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

| Log&nbsp;or&nbsp;panel&nbsp;says | Meaning |
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
| `device_id` | number&nbsp;or&nbsp;string | `0`&nbsp;(not&nbsp;checked) | The expected DEVICE_ID (`0xA000`). A mismatch at connect logs a warning (3.7). Written as `"0x1001"` or `4097`. |
| `desc` | string | `""` | A line on the map, in the export. |
| `notes` | string | `""` | Longer text on the map (Markdown), in the export. |
| `slave` | number | `1` | The slave address, loaded into the Slave box. Saving the map writes the box's value back. |
| `usb` | object | none | `{ "vid": "0x1234", "pid": "0xABCD" }`: the device's USB vendor and product IDs, in hex or decimal. The serial port list names the port with these IDs after the map's device, and picks it when no port was selected (3.2). |
| `login` | object | none | `{ "addr": "0xF000", "size": 16 }`: the register the token is written to after a TCP connect (3.6). `addr` must be 1 – 0xFFFF (a JSON number is not range-checked, see 16.10). `size` is 1 – 65535 bytes, 16 when left out. Without `login`, no token is sent. |
| `protocol` | object | none | `{ "transport": "serial", "baud": 115200, "tcp_port": 1210, "timeout_ms": 200, "notes": "…" }`: how the device is reached, for the reader of the map and its export. Every key is optional. The Studio connects as the sidebar says. |
| `groups` | object | none | `{ "Power": { "notes": "…" } }`: notes on a group, by its name. |
| `extends` | string | none | An overlay: the map this one changes, relative to this file (chapter 33). |
| `registers` | array | empty | The registers (16.3). |
| `streams` | array | none | Fast EVRe: the device's sample streams (16.13). |

Unknown keys are kept: a save writes them back as they were (16.9).

### 16.3 Register keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `addr` | string&nbsp;or&nbsp;number | required | The address, 0 – 0xFFFF: `"0xD004"` (hex, any case), `"53252"` or `53252`. Only a text address is range-checked; a JSON number is cut to 16 bits without a message (16.10). |
| `name` | string | the&nbsp;address,&nbsp;e.g.&nbsp;`"0xD004"` | The name used in the table, the chart, math lines, CSV and the API. Keep it unique. The API and math lines find registers by name, in any case, and take the first match. |
| `type` | string | `"u16"` | 16.4. Also accepts the C names `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `float`, in any case. |
| `size` | number | `1` | Byte count, for `bytes` only. Other types have a fixed size. |
| `unit` | string | `""` | Shown beside the value, and used in CSV titles and chart legends. `W`, `A` and `mA` set special area units in the measurements (8.3). The unit `bitmask` shows an integer in hex. |
| `access` | string | `"ro"` | `"ro"`, `"rw"`, or `"wo"`: write-only, **never read, so never polled** (a key, a command). Any other text containing `w` means read-write. |
| `write` | string | normal | `"action"`: a write does something, then the register reads back idle; `"w1c"`: a 1 written to a bit clears it, a 0 leaves it. For the reader and the export; the Studio writes such registers as any other. |
| `persist` | boolean | `false` | Kept across a reset (non-volatile); `default` is then the factory value. |
| `plot` | boolean | `true`&nbsp;(numbers) | `false`: a fixed value, an ID or a command, not worth a line: no Plot box in the table, left out by *Plot shown* and `--plot`. Math lines may still read it. |
| `notes` | string | `""` | Longer text (Markdown), shown in the Map editor and in the export. |
| `group` | string | `"Registers"` | For the groups filter and the table's Group column. |
| `desc` | string | `""` | Description: shown in tooltips, the detail line and the danger dialog. Searched by the search box. |
| `scale` | number | `1` | shown = raw × scale + offset (16.5). |
| `offset` | number | `0` | See `scale`. |
| `format` | string | none | `"hex"`: show an integer in hex. No other value has an effect. |
| `danger` | boolean | `false` | Every write from the window asks first (6.3), and API clients also need *including ⚠ registers* (17.2). |
| `decimals` | number | automatic | The shown value with this many decimals (0 – 15). |
| `min`,&nbsp;`max` | number | none | The shown value's limits for writes, in shown units: the window asks before writing past them, the API refuses (6.4). |
| `default` | number&nbsp;or&nbsp;string | none | The value after a reset, in shown units, or one of the register's value names. The quick-write panel's **Default** writes it. |
| `special` | object | none | Names of single values of a number, in shown units: `{ "-1": "not measured" }`. Decoded shows the name, a write may always set them (31.2). |
| `enum` | object | none | Names of values (16.6). |
| `fields` | array | none | Bit fields (16.7). |

### 16.4 Types

All multi-byte values are **little endian** (the low byte first).

| Type | Size | Range | Notes |
|---|---|---|---|
| `u8` | 1 | 0&nbsp;…&nbsp;255 | |
| `i8` | 1 | −128&nbsp;…&nbsp;127 | |
| `u16` | 2 | 0&nbsp;…&nbsp;65535 | |
| `i16` | 2 | −32768&nbsp;…&nbsp;32767 | |
| `u32` | 4 | 0&nbsp;…&nbsp;4294967295 | |
| `i32` | 4 | −2147483648&nbsp;…&nbsp;2147483647 | |
| `f32` | 4 | IEEE&nbsp;754&nbsp;single | Shown with decimals by size (4.2). NaN shows `NaN`. |
| `bytes` | `size` | any&nbsp;bytes | Shown as hex (the first 24 bytes). Written as exactly `size` hex bytes. Not plotted. Polled only up to 32 bytes: longer ones are read with *Read now*. |

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
| `"extends" …`,&nbsp;`base map <file>: …` | The base cannot be read or is not a map, or maps extend each other more than 8 deep. |
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
| MSG_CNT,&nbsp;MSG_BUFFER | A counter, and a 255-byte buffer that is too long to poll (read it with *Read now*). |
| UPTIME | A `u32` in ms. |
| SUPPLY_V,&nbsp;SUPPLY_I,&nbsp;TEMPERATURE | `f32` with units. The fake device moves them as sine waves. The group "Power & supply" has an `&`, which the menus show as it is (4.7). |
| STATE | A packed state word: a two-bit field with value names and two flags. |
| PRESSURE | A scaled integer: `i16` in hundredths of a bar, shown in bar. |
| SETPOINT,&nbsp;FAN_SPEED | Writable settings. |
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

### 16.13 Streams (Fast EVRe)

`streams` lists the device's sample streams (13.9): for each, the window its blocks come from, the rate, the
registers that switch it, and the channels of one record. MAP_FORMAT.md section 9 is the full description;
`maps/example_fast.json` has one:

```json
"streams": [
  { "name": "ADC", "addr": "0xDC00", "size": 1024, "rate": 10000, "rate_reg": "ADC_RATE",
    "enable": "ADC_STREAM", "group": "Power", "desc": "load current and bus voltage, sampled together",
    "channels": [
      { "name": "I_LOAD", "type": "i16", "unit": "A", "scale": 0.0005 },
      { "name": "V_BUS", "type": "i16", "unit": "V", "scale": 0.001 }
    ] }
]
```

| Key | Meaning |
|---|---|
| `name` | unique among the map's streams and registers; a channel's line is `ADC.I_LOAD` |
| `addr`,&nbsp;`size` | the window: its first address in the device bank (`0xD000`..`0xDFFF`) and its bytes, the largest block with its 8-byte header. No register may lie over it (a poll would read it whole) |
| `rate` | samples a second as the device is built |
| `rate_reg` | a register whose shown value is the rate now: read before Start, and the clock's fit starts from it |
| `enable` | a writable register: Start writes 1, Stop 0. Without it the Studio only listens |
| `channels` | in record order, packed, little endian: `name`, `type` (`u8` to `f32`, default `i16`, never `bytes`), `unit`, `scale`, `offset`, `decimals`, `desc` as a register's |

A block holds up to (`size` - 8) / record size records: 254 in the example (a record of two `i16` is 4 bytes). The
checks (30.6, `evre validate`) refuse a window outside the device bank, over a register or another window, or too
small for the header and one record; a stream without channels, a `bytes` channel, a rate not above 0; an `enable`
or `rate_reg` that names no register, an `enable` a host cannot write; a name used twice. The Map editor edits them
on Map settings' **Streams** page (30.7); its save writes `streams` again only when they changed, after the
registers in a new map.

## 17. The API

When **Serve API** is on, the Studio shares the device it is connected to with other programs. Every request of every client goes through the Studio's own request queue, the same queue as its polls. Clients and the Studio therefore never collide on the port, and a device that takes one client at a time still serves them all.

| Port | Protocol | For |
|---|---|---|
| **1220** | JSON&nbsp;lines,&nbsp;by&nbsp;register&nbsp;name | Scripts: Python, MATLAB, LabVIEW, anything with a TCP socket. |
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
| **Allow&nbsp;API&nbsp;writes** | Off: clients can only read and stream. On: `set`, `write` and pass-through writes are allowed, except to danger registers. Shown in amber while on. |
| **including&nbsp;⚠&nbsp;registers** | Enabled only while Allow API writes is on. Also allows writes that touch a register marked `danger`. Shown in red while on. Unticking Allow API writes unticks it. |

Both are off at every start, and neither is ever saved. They can be set at start with `--api-writes` and `--api-writes-danger`.

Reading a fast stream (17.3, *Fast streams*; the blocks of 17.4) needs neither switch: nothing is written to the device.

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

**Names.** Wherever a request names registers, a name is the map's name in any case, or an address as text (`"0xD00C"`) that is exactly the start of a map register. Answers use the map's names. On a bus (3.9) the names carry their device's: `"D2_SUPPLY_V"` reads that register of D2, whatever its slave address; `list` gives each register's `"slave"`; an address as text, and `read` / `write` by address, are the selected device's. A fast stream's channel is named `STREAM.CHANNEL` (`"ADC.I_LOAD"`, any case), as its line on the chart. JSON objects in answers list their keys in alphabetical order.

**Values.**

| Register | Value in answers |
|---|---|
| Integer | a JSON integer |
| `f32`,&nbsp;or&nbsp;scaled&nbsp;/&nbsp;offset | a JSON number |
| `bytes` | a string of hex digits without spaces (`"48454c4c4f"`) |
| No&nbsp;valid&nbsp;value,&nbsp;or&nbsp;not&nbsp;finite | `null` |

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
| `writes`,&nbsp;`danger_writes` | The two switches. `danger_writes` is true only when both are on. |
| `evre_port`,&nbsp;`json_port` | The ports served. |

#### `list`

Every register of the map, including those that are not polled, and the map's fast streams (`"streams"`, an empty
list for a map without them; see *Fast streams* below).

```
{"cmd":"list"}
{"ok":true,"registers":[{"access":"ro","addr":"0xA000","desc":"which device this is","group":"Protocol",
 "name":"DEVICE_ID","plot":false,"size":2,"type":"u16"}, ...,
 {"access":"rw","addr":"0xD086","danger":true,"desc":"moves the motor: confirmed on every write","group":"Settings",
 "name":"MOTOR_SPEED","size":2,"type":"i16","unit":"rpm"}],"streams":[]}
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
- **A fast channel** (`"ADC.I_LOAD"`): its newest record's value, without reading anything; see *Fast streams* below.
- **Errors:**
  - `no register "X" in the map`, or for a name with a dot `no register or fast channel "X.Y" in the map (a fast
    channel is STREAM.CHANNEL, as "list" names it)`
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
- **Fast channels** in the names: a line of their min, max and mean every `period_ms`, a line of its own kind beside
  the samples; see *Fast streams* below.

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

#### Fast streams

A map's fast streams (13.9) are read through the same commands, by their channels' names (`STREAM.CHANNEL`). Only
while the stream is on in the Studio (its **Start stream**, or `--fast` at start, 14.1): the API does not switch it,
and needs no write switch for it.

| Command | For&nbsp;fast&nbsp;streams |
|---|---|
| `list` | `"streams"`: each stream's `name`, `rate` (records a second: as the device was set to once a block came, else the map's), `on` (the Studio takes its blocks), `desc` when the map has one, and `channels`, each its `name` (`STREAM.CHANNEL`), `type`, and `unit` and `desc` when the map sets them. |
| `get` | A channel: its **newest record**'s value in `"values"` (scale and offset applied), and that record's time in `"times"`, in seconds since 1970 (the stream's clock in the Studio, 13.9, laid on the PC's). Registers in the same request are read as always. |
| `stream` | A channel: every `period_ms` (whole milliseconds, **10** at least; when left out `ms`, else 100) one line with each named channel's records of that period: `n` (how many), `min`, `max`, `mean` and `first` (the number of its first record, 64 bits from the stream's start, as in a recording). A period holds the records that came in it (a block comes a little after its records were taken); a period without one gives `n` 0 and nulls. `t` is the period's end, in seconds since 1970, as a sample's. Registers in the same request keep their samples every `ms`: two kinds of line, told apart by `values` and `fast`. |

```
{"cmd":"list"}
{"ok":true,"registers":[...],"streams":[{"channels":[{"name":"ADC.I_LOAD","type":"i16","unit":"A"},
 {"name":"ADC.V_BUS","type":"i16","unit":"V"}],"desc":"load current and bus voltage, sampled together",
 "name":"ADC","on":true,"rate":10000}]}
{"cmd":"get","names":["ADC.I_LOAD","SUPPLY_V"]}
{"ok":true,"times":{"ADC.I_LOAD":1790170000.412},"values":{"ADC.I_LOAD":0.8145,"SUPPLY_V":12.05}}
{"cmd":"stream","names":["ADC.I_LOAD","SUPPLY_V"],"ms":50,"period_ms":100,"id":"f"}
{"fast":1,"id":"f","ms":50,"ok":true,"period_ms":100,"streaming":2}
{"stream":"f","t":1790170000.475,"values":{"SUPPLY_V":12.0}}
{"fast":{"ADC.I_LOAD":{"first":4120000,"max":6.5,"mean":0.0021,"min":-6.5,"n":1000}},"stream":"f","t":1790170000.512}
```

- The confirmation of a `stream` with fast channels has `fast` (their count) and `period_ms` (the period used); `ms`
  only when registers are named.
- A stream switched off meanwhile: its channels' lines go on with `n` 0. No line is sent while the Studio has no
  link.
- **Errors:** `fast stream ADC is off: start it in EVRe Studio (Fast streams), or start the Studio with --fast ADC`
  (`get` and `stream`), and `fast stream ADC is on, but no record has come yet` (`get`).
- Every record, as it came: port 1219 (17.4), or a recording's `.evrs` file (12.7) read with
  `evre.read_recording`.

#### An unknown command

It gets `unknown cmd "x": info, list, get, set, stream, stop, read, write, broadcast`.

#### Several clients

Any number of clients can be connected at once. Each has its own connection, its own stream and its own `id`s. All their requests share the Studio's queue with its polling, so heavy API traffic lowers the poll rate and polls lower the API's rate. The status line of the API card counts the clients and the requests (JSON lines and EVRe frames).

### 17.4 Port 1219: EVRe pass-through

The pass-through accepts the device's own frames (layout in 18.1) and forwards them through the Studio's queue. An EVRe client that works with the device or with a TCP gateway works here unchanged. It can even be another EVRe Studio connected over TCP to port 1219: its polls, keep-alive and writes pass through this Studio's queue.

| Request&nbsp;from&nbsp;the&nbsp;client | What&nbsp;the&nbsp;Studio&nbsp;does | Answer to the client |
|---|---|---|
| `READ` | Forwards it as a READ of the same offset and count. | `READ_RESP` with the device's bytes, or `ERROR_RESP` with the device's code. |
| `WRITE_ACK`,&nbsp;writes&nbsp;allowed | Forwards&nbsp;it&nbsp;as&nbsp;a&nbsp;`WRITE_ACK`. | `WRITE_ACK_RESP`, or `ERROR_RESP` with the device's code. |
| `WRITE_ACK`,&nbsp;writes&nbsp;refused&nbsp;(17.2) | Nothing is sent to the device. The check comes before the link check, so this holds with no link too. | `ERROR_RESP` code **3** (permission denied), at once. |
| `WRITE`,&nbsp;writes&nbsp;allowed | Forwards it as a `WRITE_ACK`: the device always acknowledges, so the queue can order it. | none, as for a `WRITE` on the wire |
| `WRITE`,&nbsp;writes&nbsp;refused | Nothing&nbsp;is&nbsp;sent. | none |
| A `READ`, or an allowed write, while the Studio has no link | Nothing&nbsp;is&nbsp;sent. | none: the client times out, as with a silent device |
| A&nbsp;timeout&nbsp;or&nbsp;a&nbsp;lost&nbsp;link | | none |
| Any other function code (an answer sent to the server) | Ignored. | none |
| A frame with a bad CRC or end byte | Dropped. | none |

How frames are handled:

- **Answers mirror the request.** They carry the request's slave address, offset and count, as the device itself would answer.
- **The Studio's own slave address is used, with one device.** The Studio forwards to its device with the slave address in its Slave box, not the client's.
- **On a bus, the frame's slave address is used** (3.9): a frame goes to the device it names, and the answer comes as from that device. A broadcast `WRITE` (slave 0) goes out as one when the rule of 3.10 and the write switches allow it for every device, and is never answered; a broadcast `READ` or `WRITE_ACK` is not sent.
- **Frames are not merged.** Each request is one request to the device.
- **Pipelining works.** A client may pipeline requests. The Studio's In flight then limits how many are on the link at once.

**A fast stream's blocks** (13.9) pass through to a client that asks for them the way it would ask the device: by
writing the stream's `enable` register (exactly that register, `WRITE_ACK` or `WRITE`). The blocks then come as the
device sent them, READ_RESP frames at the stream's window nobody asked for, with the slave address the client named
(as an answer); a client that does not read them gets none past 8 MiB waiting (their numbers tell it what it missed).
So an EVRe client that streams from the device streams through the Studio unchanged, the `evre` package's
`Device.stream` (36) included.

| The&nbsp;client&nbsp;writes | The&nbsp;Studio&nbsp;streams&nbsp;it | What happens |
|---|---|---|
| `enable`&nbsp;1 | yes | Taken by the Studio, not sent: the client's blocks start, acknowledged (`WRITE_ACK_RESP`). No write switch is needed: the device is not written. |
| `enable`&nbsp;0 | yes | Taken by the Studio, not sent: the client's blocks end, acknowledged. No client switches the Studio's stream off. |
| `enable`&nbsp;1 | no | The client's own write: to the device as any write (the switches of 17.2). Once the device acknowledges it, the client gets the blocks the device sends; the client keeps the device's watchdog fed itself (13.9). |
| `enable`&nbsp;0 | no | To the device as any write, and the client's blocks end; but when its blocks came from the Studio's stream (started while the Studio streamed it), only its blocks end: nothing is sent. |

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

Run it directly: `python3 examples/python/evre_studio_client.py [NAME ...]`. It also has `streams()` and
`fast_stream(channels, period_ms)` (fast streams), and lists the map's streams when it runs.

The `evre` package (36) has the same client, `evre.connect_studio()`, with `get`, `fast_value` (a fast channel's
newest record and its time), `stream` (each line, a sample or a fast channel's `Summary`) and `fast_stream`:

```python
import evre

with evre.connect_studio() as studio:            # 127.0.0.1:1220
    print([s["name"] for s in studio.streams()])   # ['ADC']
    for t, summary in studio.fast_stream(["ADC.I_LOAD"], period_ms=100):
        print(t, summary["ADC.I_LOAD"].min, summary["ADC.I_LOAD"].max)
        break                                    # leaving the loop sends {"cmd":"stop"}
```

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
| 7&nbsp;+&nbsp;n | 2 | CRC | CRC-16/X-25 over bytes 0 … 6 + n, low byte first (18.4). |
| 9&nbsp;+&nbsp;n | 1 | END | `0x7D` |

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
| `0xAB` | READ_RESP | CNT&nbsp;bytes | The answer to READ. |
| `0xEA` | WRITE | CNT&nbsp;bytes | Write CNT bytes at OFF, with no answer. |
| `0xEB` | WRITE_ACK | CNT&nbsp;bytes | Write CNT bytes at OFF, answered. |
| `0xEC` | WRITE_ACK_RESP | none | The answer to WRITE_ACK: written. |
| `0xEE` | ERROR_RESP | 1&nbsp;byte,&nbsp;the&nbsp;error&nbsp;code | The answer to any request that failed. |

How the Studio uses them:

- **READ** for polls, reads on request, the device ID and the keep-alive.
- **WRITE_ACK** for every write made in the window, by the API or for the login.
- **WRITE** only for the Monitor's *WRITE (no ack)*, which counts as done when sent.

### 18.3 Error codes

| Code | Name,&nbsp;as&nbsp;the&nbsp;Studio&nbsp;shows&nbsp;it | How the Studio reacts |
|---|---|---|
| 0 | no&nbsp;error | |
| 1 | invalid&nbsp;packet | the register shows *error*, and is tried again |
| 2 | unknown&nbsp;function&nbsp;code | the register shows *error*, and is tried again |
| 3 | permission&nbsp;denied | **refused for good**: *not available* for a single register |
| 4 | offset&nbsp;out&nbsp;of&nbsp;range | **refused for good** |
| 5 | count&nbsp;out&nbsp;of&nbsp;range | **refused for good** |
| 12 | length&nbsp;mismatch | the register shows *error*, and is tried again |
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
| TX&nbsp;/&nbsp;RX | Frames sent and received. |
| Timeouts | Requests that got no answer in time. |
| Errors | Error answers from the device. |
| Bad&nbsp;frames | Frames with a wrong CRC or end byte (18.5). |
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
4. **Fast streams** that were on before a lost link are switched on again (13.9): their `rate_reg` read, their
   `enable` written 1.
5. **Polls** start at the chosen interval.

**Link details:**

- **TCP:** Nagle's algorithm is off (low-delay option), so each request leaves at once.
- **Serial:** 8N1 with no flow control. DTR is set on after opening, and the port's buffers are cleared.
- **Link loss:** a lost link fails every waiting request with *cancelled* or *not connected*. The window then reconnects if *Reconnect by itself* is on (3.5).

### 18.9 Fast EVRe blocks

A fast stream's block (13.9, PROTOCOL.md "Fast EVRe") is an ordinary READ_RESP frame that answers no request: the
master reports it as unsolicited (18.6), and the engine takes it as a block when it comes from the device's slave at
a stream's window (`addr`). The frame's data is the block:

| Offset | Member | Meaning |
|---|---|---|
| 0 | `first`&nbsp;(u32) | the number of the block's first record, counted from the stream's start, dropped records included; wraps at 2^32 |
| 4 | `count`&nbsp;(u16) | the records in the block |
| 6 | `flags`&nbsp;(u8) | bit 0 START (the first block since the stream started), bit 1 LOST (records dropped just before it); the other bits 0 |
| 7 | `spare`&nbsp;(u8) | 0 |
| 8 | records | `count` records of the map's channels, packed, little endian |

The rules (`fast::fastBlock`, `fast::FastStream::take`, unit-tested in `evre_fast_test`):

- The frame's count is exactly 8 + `count` x record size, else the block is bad: counted, no record used.
- An unknown flag bit or a `spare` not 0 is a newer kind of block: counted, no record used, said once.
- With d = (`first` - expected) mod 2^32: d below 2^31 is d records lost (0: none); anything else, or START, or the
  first block seen (a Studio that joins a running stream), is a new start. The numbers are kept in 64 bits.
- A bad block's `first` is not trusted: its records show as lost at the next good block.

A block is not a register read: the window lies in no register, so polls never read it, and a READ of it is the
device's choice to answer or refuse (`evre-sim` refuses it, 4).

# Part III. Internals

This part is for people who maintain or extend the Studio. It describes how the code is built and why. Part I
describes the tool as a user sees it. Part II covers the map format, the API and the protocol.

## 19. Architecture

### 19.1 Layers

The Studio has five modules. `evre` and `model` form the base: each uses only what is above it in the table. `io`
and `api` depend on each other: the engine creates and owns the `ApiServer`, and the `ApiServer` works on the
engine's `RegTable` (`io/reg_table.h`) and its master. `ui` uses all of them.

| Module | Directory | Depends&nbsp;on | Holds |
|---|---|---|---|
| `evre` | `src/evre/` | Qt&nbsp;Core,&nbsp;Network,&nbsp;SerialPort | frames and the CRC, the links (TCP, serial), the master (queue, pipelining, timeouts) |
| `model` | `src/model/` | `evre`&nbsp;(byte&nbsp;order&nbsp;helpers) | the device map and value coding, formulas, math lines, the register table model |
| `io` | `src/io/` | `evre`,&nbsp;`model`,&nbsp;`api` | the I/O engine: the connection, the poller, CSV, the table the I/O thread writes |
| `api` | `src/api/` | `evre`,&nbsp;`model`,&nbsp;`io/reg_table.h` | the API server (EVRe pass-through, JSON lines) |
| `ui` | `src/ui/` | all&nbsp;of&nbsp;the&nbsp;above | the window, the tabs, the chart, the theme, help |

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
| window&nbsp;→&nbsp;engine | `IoEngine::post(std::function<void()>)`, which calls `QMetaObject::invokeMethod(engine, f, Qt::QueuedConnection)` | every request: map, link, polling, plotted set, CSV, writes, reads, API switches |
| engine&nbsp;→&nbsp;window | signals, queued because the sender lives in the other thread | `opened`, `closed`, `deviceInfo`, `loginRefused`, `loginSkipped`, `readDone`, `writeDone`, `recordStarted`, `recordStopped`, `apiStarted` |
| window&nbsp;reads&nbsp;engine&nbsp;state | copies&nbsp;under&nbsp;a&nbsp;mutex | `table().snapshot()`, `takeSamples()`, `takeFrames()`, `stats()` |

The posted lambdas capture their arguments by value. They run in the order they were posted, so a `setMap` posted
before `connectTcp` is applied first.

A request that is answered carries an id. `MainWindow::engineRead` and `engineWrite` store the callback in
`pendingReads_` or `pendingWrites_`, keyed by `nextRequestId_`, and post the request. The engine answers with
`readDone(id, ...)` or `writeDone(id, ...)`. The lambdas that `startEngine()` connects to these two signals take the
callback out of the hash and run it in the GUI thread.

### 19.3 Who owns what

| Object | Owner&nbsp;/&nbsp;parent | Thread | Notes |
|---|---|---|---|
| `QThread ioThread_` | `MainWindow`&nbsp;(child) | GUI | object name `evre-io`, started with `QThread::HighPriority` |
| `IoEngine` | none; moved with `moveToThread(ioThread_)` | I/O | deleted by `deleteLater` when the thread finishes |
| `RegTable` | `IoEngine`&nbsp;member&nbsp;(not&nbsp;a&nbsp;`QObject`) | written&nbsp;in&nbsp;I/O | see 19.4 |
| `evre::Master` | `IoEngine`&nbsp;(child) | I/O | moved along with the engine |
| `ApiServer` | `IoEngine`&nbsp;(child) | I/O | its two `QTcpServer`s are its children |
| `QTimer statsTimer_`, `offlineTimer_` (the offline retry, 3.9), `heartbeatTimer_` (auto send, 13.8), `QFile csvFile_` | `IoEngine`&nbsp;(children) | I/O | |
| `evre::Link` | `IoEngine::link_`&nbsp;(`std::unique_ptr`) | I/O | created with `new` in `IoEngine::connectTcp` / `connectSerial`, which run in the I/O thread; `attach()` takes it over; its socket or port is its child |
| API&nbsp;client&nbsp;sockets | the `QTcpServer` that accepted them | I/O | created by `nextPendingConnection()` in the I/O thread |
| API&nbsp;stream&nbsp;timers | `ApiServer`&nbsp;(child) | I/O | created in the I/O thread by `startStream()` |
| ticker&nbsp;`std::thread` | `IoEngine` | its&nbsp;own | only while polling at an interval > 0 |
| `RegisterModel` | `MainWindow`&nbsp;(child) | GUI | the window's copy of the values; its text colours come from the window (`setColors`), the model knows no theme |
| `FrameClock` | `MainWindow`&nbsp;(child) | GUI | on Windows its waiting `std::thread` |
| chart&nbsp;data&nbsp;(`ChartView::series_`) | `ChartView` | GUI | read by the chart's own threads (`pool_`) during a paint, while the GUI thread waits for them (23.6); written only by the GUI thread |
| `DeviceMap map_` | `MainWindow`&nbsp;member | GUI | the engine gets copies of the definitions |

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
| frame&nbsp;waiter&nbsp;(Windows) | waits for each display refresh (`DwmFlush`) and posts a tick to the window | `FrameClock::start()` (plain `std::thread`) |

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
| answer&nbsp;timeout | the time left for the oldest pending request, `Qt::PreciseTimer` | `Master` |
| keep-alive | 400&nbsp;ms | `Master` |
| statistics | 250&nbsp;ms | `IoEngine` |
| API&nbsp;stream | the client's `ms`, at least 5, default 100, `Qt::PreciseTimer` | `ApiServer` |
| table&nbsp;repaint | 50&nbsp;ms,&nbsp;single&nbsp;shot | `RegisterModel` |
| status&nbsp;bar&nbsp;and&nbsp;sidebar | 500&nbsp;ms | `MainWindow` |
| measurements | 250 ms; at most every 100 ms while the cursors move | `ChartTab` |
| reconnect | 500 ms after a lost link, 2000 ms after a failed connect | `MainWindow` |
| frame&nbsp;clock&nbsp;fallback | 16&nbsp;ms,&nbsp;`Qt::PreciseTimer` | `FrameClock` |

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
| `src/evre/frame.h`,&nbsp;`.cpp` | function codes, CRC-16/X-25, `build()`, `Parser`, byte order helpers, names for codes and errors |
| `src/evre/link.h`,&nbsp;`.cpp` | `Link` interface, `TcpLink`, `SerialLink` |
| `src/evre/master.h`,&nbsp;`.cpp` | `Master`: queue, pipelining, answer matching, timeouts, keep-alive, statistics |
| `src/evre/registers.h` | namespace `evre`: the reserved bank's addresses (DEVICE_ID, STATUS, CONFIG and its bits), the STATUS capability bits, the read-only block at 0xD000, the AUTO_SEND base rate (8000 Hz) and its prescaler (1 to 255, default 0x4F; 40 Hz the least the protocol names) |
| `src/io/reg_table.h` | `RegValue`, `RegTable`: the I/O thread's table (19.4) |
| `src/io/engine.h`,&nbsp;`.cpp` | `IoEngine`: connect sequence, login, device ID, ticker, blocks and polls, samples, CSV, monitor lines, reads and writes asked for, API control, auto send, fast streams |
| `src/io/fast_stream.h`,&nbsp;`.cpp` | `fast::`: a fast stream's block read and checked (`fastBlock`), its running state (`FastStream`: the 64-bit numbers, starts, losses, counts), the clock's fit (`FastClock`), and for the fake devices a stream as a device sends it (`FastSource`) and a connection's streams with a host watchdog (`FastSender`); the chart's trigger on a channel, looked for in each block (`TriggerWatch`, `Crossing`, `TriggerScan`) |
| `src/model/device_map.h`,&nbsp;`.cpp` | `RegType`, `BitField`, `RegDef`, `DeviceMap`, `MapIssue`; decode, format and encode of values, limits, special values; addresses; block-merge rule; pollable rule; chart keys `regKey(slave, addr)`, `regKeySlave`, `regKeyAddr`; `requestSlave` |
| `src/model/map_file.cpp` | `DeviceMap::load`, `save`, `toJson`: reading with `extends`, and writing back only what changed; `registersToJson` / `registersFromJson` (the clipboard) |
| `src/model/map_check.cpp` | `checkMap`: the Map editor's checks |
| `src/model/json_doc.h`,&nbsp;`.cpp` | `jsondoc`: JSON as an ordered tree that remembers where each value was in its text; rendering (pretty, compact) and `patchSequence` |
| `src/model/map_export.h`,&nbsp;`.cpp` | Markdown, C header, Python, CSV and device table export, CSV import, `identifier` |
| `src/model/map_document.h`,&nbsp;`.cpp` | `MapDocument`: the map being edited, its undo history, uids, the checks' cache |
| `src/model/bus_file.h`,&nbsp;`.cpp` | `BusFile`, `BusDevice`: several devices on one link (`evre-bus/1`); `checkBus`, `nextBusDevice`, `busRegisterName`, `broadcastNames` (a register by the map's or the bus name), `broadcastRefusal` (the broadcast rule); `nextBusDevice` gives slave 0 when all 255 are taken |
| `src/model/expr.h`,&nbsp;`.cpp` | `Expr`: the formula parser (recursive descent to postfix) and its stack machine |
| `src/model/math_lines.h`,&nbsp;`.cpp` | `MathLine`, `MathLines`: formulas over registers, kept in the settings, evaluated per frame; a fast math line's per record (`evaluateRecords`, 9.9) |
| `src/model/analysis.h`,&nbsp;`.cpp` | `analysis::`: `fft` (radix-2, our own), `histogram` (Freedman–Diaconis), `spectrum` (resampled, Welch, Hann) |
| `src/model/fast_store.h`,&nbsp;`.cpp` | `fast::Store`: a fast stream's records as the chart keeps them (pieces of 65 536, segments, starts with their time marks, the summaries of every 256 and 4096 records; `dropFront` hands what it lets go to `Released` for the chart to free a slice a frame; `fillFrom`: a fast math line's values over a recording's records), 23.11 |
| `src/model/fast_recording.h`,&nbsp;`.cpp` | `fast::RecordingWriter`: a fast stream's recording, `.evrs` (pieces `EVRS`, `TIME`, `BLK `), written by `evre record` and the engine beside a CSV; `fast::readRecording` / `Recording`: one read, the file mapped, into a mapped store (12.7); `recordingFileFor` (`run.csv` -> `run.ADC.evrs`), `recordingsBeside` |
| `src/model/recording_file.h`,&nbsp;`.cpp` | `recording::`: a recording's CSV read (`estimate`, `read`) and written (`write`, the chart's export), the notes beside it (`loadNotes`, `saveNotes`); `ChartNote` |
| `src/model/register_model.h`,&nbsp;`.cpp` | `RegisterModel` (the table's model), `RegisterFilter` (search and groups) |
| `src/api/api_server.h`,&nbsp;`.cpp` | `ApiServer`: EVRe pass-through on 1219, JSON lines on 1220, streams, write permissions, `broadcastWriteRefusal` (a pass-through broadcast under the rule) |
| `src/ui/main_window.h`,&nbsp;`.cpp` | `MainWindow`: builds the window, wires the parts to the engine, sync per frame, writes, CSV, API, log |
| `src/ui/sidebar.h`,&nbsp;`.cpp` | `Sidebar`: the connection, devices, map, polling and recording, fast streams, and API cards; link settings |
| `src/ui/main_window_bus.cpp` | `MainWindow`'s bus part (the same class): the bus file, its devices, the pickers, broadcasts |
| `src/ui/limit_spin_box.h`,&nbsp;`.cpp` | `LimitSpinBox`: a number box held to the map's limits |
| `src/ui/elided_label.h`,&nbsp;`.cpp` | `ElidedLabel`: one line, cut with an ellipsis, the whole text in its tooltip (`setFullText`, `fullText`, `isCut`); the Map editor's banner, the Devices card's info line, the status bar's hint, the link state pill (`setElideMode`: cut in the middle; `setFullText`'s `shorter`: a shorter text shown whole first, the address alone) |
| `src/ui/bus_panel.h`,&nbsp;`.cpp` | `BusPanel`: the Devices on the link card: one device, or the devices of a bus and their state |
| `src/ui/bus_device_dialog.h`,&nbsp;`.cpp` | `BusDeviceDialog`: one device of a bus: name, slave, map, polled, its own token; checked while typing |
| `src/ui/bus_preset_dialog.h`,&nbsp;`.cpp` | `BusPresetDialog`: a broadcast kept in the bus file: name, register, value; checked while typing |
| `src/ui/registers_tab.h`,&nbsp;`.cpp` | `RegistersTab`: toolbar, table, menus, groups, Plot shown, map editing, detail line |
| `src/ui/value_delegate.h`,&nbsp;`.cpp` | `ValueDelegate`: the Value column's drawing (the ⓘ mark) and editor (`base`) |
| `src/ui/quick_write_panel.h`,&nbsp;`.cpp` | `QuickWritePanel`: value box, named values, bit view for the selected RW register |
| `src/ui/bit_view.h`,&nbsp;`.cpp` | `BitView`: a register drawn bit by bit; click to flip or pick a field value |
| `src/ui/map_editor_tab.h`,&nbsp;`.cpp` | `MapEditorTab`: toolbar, the register table, checks list, clipboard, export and import; on a bus the banner (whose map, `showDevices`) and the Live values from picker |
| `src/ui/map_table_model.h`,&nbsp;`.cpp` | `MapTableModel`: the Map editor's table, edited in place, bulk edits |
| `src/ui/register_editor.h`,&nbsp;`.cpp` | `RegisterEditor`: the form (General, Values, Bit fields, Notes) and the live line |
| `src/ui/name_table.h`,&nbsp;`.cpp` | `NameTable`: value names or special values, Paste lines, Hex |
| `src/ui/field_editor.h`,&nbsp;`.cpp` | `FieldEditor`: bit fields on the bit strip, their list and value names |
| `src/ui/map_settings_dialog.h`, `.cpp` | `MapSettingsDialog(doc, onBus, parent)`: device, protocol, notes and fast streams of the map, one undo step; on a bus its Slave box is disabled; the Streams page (`streamsPage`, `addStream` / `removeStream` / `addChannel` / `removeChannel`, `streamChecks`: the tests) |
| `src/ui/chart_tab.h`,&nbsp;`.cpp` | `ChartTab`: chart controls, measurements table, math-line menu, the right-click menu (pictures, export, notes), chart settings |
| `src/ui/analysis_window.h`,&nbsp;`.cpp` | `AnalysisWindow`: a line's histogram or spectrum in a window of its own, its plot, readout, picture and CSV |
| `src/ui/recording_window.h`,&nbsp;`.cpp` | `RecordingWindow`: a recording opened in a window of its own, its reading on a thread, the recent recordings |
| `src/ui/chart_widget.h`,&nbsp;`.cpp` | `ChartView` (the chart) and `ChartWidget` (its wrapper) |
| `src/ui/gpu_lines.h`,&nbsp;`.cpp` | `GpuLines`: the chart's plot drawn by a graphics card and shown as a layer of the window (Direct3D 11, a swap chain, DirectComposition) |
| `src/ui/math_line_dialog.h`,&nbsp;`.cpp` | `MathLineDialog`: name, unit and formula, checked while typing |
| `src/ui/formula_completer.h`,&nbsp;`.cpp` | `FormulaCompleter`: the list of registers and functions while a formula is typed |
| `src/ui/monitor_tab.h`,&nbsp;`.cpp` | `MonitorTab`: frame log, hand-typed READ or WRITE |
| `src/ui/event_log.h`,&nbsp;`.cpp` | `EventLog` (Log tab and daily file), `Notice` (the pop-up) |
| `src/ui/frame_clock.h`,&nbsp;`.cpp` | `FrameClock`: one tick per display refresh |
| `src/ui/language.h`,&nbsp;`.cpp` | `language::`: the choice (`ui/language`), the translators (the Studio's and Qt's), the direction and the numbers' locale |
| `translations/evre_studio_ar.ts` | Arabic: every text and Help page (`qt_add_translations`, built into the program at `:/i18n`) |
| `src/ui/value_pace.h`,&nbsp;`.cpp` | `ValuePace` (the *Show values* choices and setting), `ValuePacer`: how often the numbers on screen change |
| `src/ui/help_dialog.h`,&nbsp;`.cpp` | `HelpDialog`: the help pages, kept as HTML in the source |
| `src/ui/theme.h`,&nbsp;`.cpp` | `ThemeColors`, `Theme::apply`: Fusion style, palettes, style sheet, the combo boxes' arrow image |
| `src/ui/ui_helpers.h`,&nbsp;`.cpp` | time lengths as text and back, `durationText` (the cursors' A-B bar: *3.525 ms*, *1 min 23.4 s*), `ltrPiece` (a number and its unit one left to right piece in Arabic), `noMnemonic`, `coloredSpan`, card, muted label, segment button, `repolish`, `setHighlighted`, `monospaceFont`, `mediaIcon`, `warningIcon` (a tab's warning sign), `refreshIcon`, `confirmed` (a yes/no question), `noWindowAnimation` (a dialog without the compositor's animations, 27), `mapsFolder`, `stateDot` and `fillDevicePicker` (one look for every device picker), `studioIcon` (the teknile mark, every window's icon) |
| `tests/gui_test.cpp` | `evre_gui_test`: the real window driven by QtTest against the fake device |
| `tests/map_test.cpp` | `evre_map_test`: the map files (save byte for byte, edits, overlays, keys, checks, streams, exports) without a window |
| `tests/fast_test.cpp` | `evre_fast_test`: Fast EVRe without a window: the block's rules, a fuzz, the clock's fit, the fake devices' source, the frames `lib/fast` builds |
| `tests/fast_lib_test.py` | the device's helper `lib/fast` compiled with the library (C++11 to 20, -O0 to -Os) and run; no heap, its stack; its frames through `evre_fast_test`; PROTOCOL.md's example block |
| `maps/example_fast.json` | the example map with a fast stream (`ADC`), served by `evre_fake_fast` in the fast tests |
| `cli/evre.cpp` | `evre`: the command-line tool (chapter 34) |
| `cli/evre_sim.cpp` | `evre-sim`: a device made from a map (chapter 35) |
| `tests/sim_test.py` | `evre-sim` driven with `evre`: every behaviour of chapter 35 |
| `python/evre/` | the `evre` Python package: frames, link and master, the map, a device by register name, a fast stream's recording (`fast.py`, `read_recording`), the Studio's JSON API (`studio.py`, `connect_studio`) (`python/README.md`) |
| `python/tests/test_evre.py` | the Python package (unittest), with a session against `evre-sim`, a bus on `evre_fake_fast`, and fast streams' recordings: one made by hand (the numbers across a gap and the 32-bit wrap, a new start, the times from the marks, a bad block, a cut, a CSV refused) and one `evre record` writes from `evre_fake_fast` losing every 5th block (the device's waves, the gaps); the same stream live with `dev.stream` (its blocks for a second, the numbers and gaps, the waves, switched off at the end) |
| `tests/cli_test.py` | `evre` end to end, against its own fake device on port 1212 |
| `tests/schema_test.py` | the maps against `docs/evre-map-1.schema.json` (needs the `jsonschema` package) |
| `docs/evre-map-1.schema.json` | the JSON Schema of `evre-map/1` |
| `tests/api_test.py` | the API end to end, in three modes, and the fast streams in a mode of their own |
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
- Fast EVRe: `setFastTrigger(stream, watch)` (posted: the chart's trigger on a channel, -1 none; each block's crossings
  in `FastBlock::crossings`, the blocks still queued scanned again by `rescanWaiting`, public for the tests, 23.10; `fastTriggerWatch(stream)`, what it watches now, for the tests); `setFastStream(stream, on)` (posted; the stream's index in `DeviceMap::streams`); signals
  `fastStreamSet(stream, on, rate, err)`, `fastStreamNote(stream, text, stopped)` and `fastRecorded(text)` (a stream's
  recording beside the CSV closed, 12.7: `recordBlock` writes it); `Stats::fast`, one entry a
  stream (its state, the fitted rate and ppm, records/s, records, blocks, lost, bad and newer blocks, starts).

**Auto send (13.8).** `setAutoSend` keeps the wish; `applyAutoSend()` reads CONFIG and writes it (only after
STATUS, read at connect, shows `CAP_AUTO_SEND`). `onUnsolicited()` takes the frames from `evre::Master::unsolicited`:
a READ_RESP at `0xD000` from the device's slave fills the rows wholly inside it (`streamRows_`), counts for the
frames/s and is a sample tick (`appendSamples`, `writeCsvRow`). When its coverage changes, `rebuildBlocks()` leaves
those rows out of the polls, and `pollDone()` no longer makes samples or CSV rows. `heartbeat()` (the
`heartbeatTimer_`, a child, `Qt::PreciseTimer`, every 100 ms) reads CONFIG, one read at a time. `disconnectLink()`
sends the clearing WRITE straight on the link and flushes it (`Link::flush`) before closing. An answer to an older
switch (`autoSendRequest_`) is ignored.

**Fast streams (13.9).** `setMap()` makes a `FastRun` per stream of the one device's map (none on a bus), keeping each
wish and state by name; a stream that left the map while on is told 0. `setFastStream` keeps the wish;
`applyFast()` reads the `rate_reg`, then writes the `enable` (1 or 0, WRITE_ACK; the blocks are taken from just
before the write, the first may come ahead of its answer). `onUnsolicited()` gives a stream's block to `takeBlock()`
before the AUTO_SEND test (`fastStreamOf`: the device's slave, a stream's window), which runs the block's rules
(`fast::FastStream::take`, 18.9) and lays it on `now()`'s clock. `heartbeat()` runs while AUTO_SEND or a stream is
on. `watchFast()` (every 500 ms) switches a stream off when no block came `FIRST_FRAME_MS` after the enable was
taken, and reads the enable of a stream gone silent as long. `disconnectLink()` sends each 0 straight on the link
(`sendFastOffs`, flushed, drained) and keeps the wishes; `onOpened()` switches the wanted ones on again after the
login. The Monitor's line of a block names it (`fastStreamOfRaw`). An answer to an older switch (`FastRun::request`)
is ignored. Each block taken goes to the window as a `FastBlock` (its stream, first number, count, new start, lost,
records, and the clock's newest time mark when one was made), queued under the engine's mutex: `takeFastBlocks()`
hands them over at the window's frame. The queue holds at most `FAST_QUEUE_BYTES` = 64 MB; past it the oldest go,
their new start and mark carried to the next, counted in `Stats::Fast::notShown`.

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
and skipped, auto send, fast streams, thread warnings) and the API test; `fast_stream` by `evre_fast_test`.

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

- `load(key)` and `save` use the setting `chart/math` (`recording/math` for a recording's chart): one text per line, `name \t unit \t formula \t 1|0`. A missing
  fourth field means shown, for entries written by older versions.
- `compile(registers)` compiles every line. A formula that does not parse, or that names a register the map does
  not have or one that is not numeric, keeps its reason in `error` and is not drawn. A formula that names no
  register at all (`2 * pi`) compiles with an empty `error`, but it has no inputs, so `evaluate` never computes a
  point for it (9.6).
- `registersRead()` lists the registers the active lines read.
- `evaluate(samples, sink)` computes the points for one frame.
- Line i is keyed `FIRST_CHART_KEY + i` (1 << 24) on the chart, clear of every register key (`regKey`). At most 64 lines are drawn, and they count in the chart's one cap of 64 lines (4.8): `ChartTab::drawMathLines` draws a line only while the chart has room, and *New math line…*, a field and *Shown* are refused past it (`roomForLine`, signal `statusMessage` for the status bar).

**`analysis` (`analysis.*`).** Nothing of the chart; no library.

- `fft(data, inverse)`: iterative radix-2 Cooley–Tukey in place (bit-reversed order, then the butterflies); a length
  that is not a power of two is left as it is.
- `histogram(values)`: sorted once; the quartiles straight between neighbours; the bin width `2 IQR / cbrt(n)`, or
  `(max − min) / ceil(sqrt(n))` when the IQR is 0; `MAX_BINS` (2000); the largest value in the last bin.
- `spectrum(times, values)`: the mean rate `(n − 1) / span`, the values resampled at it; a segment of the largest power
  of two at most a 4.5th of them (8 at least, `MAX_SEGMENT` 65536 at most), so about 8 segments with a hop of half;
  the periodic Hann window; the power `|X|²` averaged; the amplitude `2 sqrt(P) / Σw` (once at 0 and half the rate).

**`recording` (`recording_file.*`).** Nothing of the chart or a window; the reading and the writing run on a
thread of their caller's, with a cancel flag and a progress callback (every 4096 rows).

- `estimate(path)` reads the header and the first 200 rows (their length gives the rows of the whole file) and the
  last 64 KB (the last row's time): a file of gigabytes is sized in a moment. `RecordingWindow` asks from it whether
  to keep the last part.
- `read(path, from)` reads the rows from the first whole row after byte `from`. A row whose time does not parse, or
  goes back, is skipped. The first row's `datetime` less its `time_s` is the time base's zero (`epochMs`). A cell
  that is not a number takes its column out (`skipped`).
- `write(path, lines, epochMs)` merges the lines' samples into rows (7.10): each line's tolerance is `ROW_SHARE`
  (0.25) of its median gap. Opened in text mode, as the recording is (the platform's line endings). Cancelled or
  failed, the file is removed.
- `splitTitle` / `title`: `NAME [unit]` and back (a comma becomes a semicolon).
- `saveNotes` / `loadNotes`: `<file>.notes.json`, `evre-notes/1` (12.5); written whole with `QSaveFile`; no note: no
  file.

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
- The engine gives it its functions: `isConnected`, `linkName`, `deviceName`, `broadcastRefusal` and `fastStreams`
  (each stream of the map as the engine has it: on or off, its rate, its newest record and that record's time).
- Fast streams: the engine hands it each frame at a stream's window (`passBlock`, to the pass-through clients that
  wrote the stream's enable, 17.4) and each good block's records of a stream the Studio takes (`fastRecords`, into the
  periods of the JSON streams that name its channels).
- JSON commands: `info`, `list`, `get`, `set`, `stream`, `stop`, `read` and `write`, one handler function each.
- Reads go through `readRegisters()`, which merges blocks and writes the table. Streams use one `QTimer` per client
  and skip a tick while a sample is still being read, so a slow device lowers the rate instead of piling up
  requests. `set` checks and encodes every value before the first write (all or nothing), writes in order, and
  replies with the values read back.

*Tested by:* `tests/api_test.py` in three modes (26.3).

### 22.6 Module `ui`

| Class | Responsibility | Main&nbsp;functions&nbsp;and&nbsp;signals | Tested by |
|---|---|---|---|
| `MainWindow` | puts the parts together; the only object that talks to the engine | `applyStartup`, `sync`, `onWriteRequested`, `loadMap`, `pushMap`, `pushPlotted`, `engineRead`, `engineWrite`, `logEvent`, `refreshStatus`; fast streams: `showFastStreams`, `updateFastOffer`, `onFastStreamSet`, `stopFastStreams`, `engineFastWatch` (tests: the engine's trigger watch on a stream) | GUI test |
| `Sidebar` | holds the choices and shows the states; the window does the work | getters (`host`, `port`, `token`, `inFlight`, ...), `show...` functions; signals `connectClicked`, `slaveChanged`, `pollingChanged`, `timingChanged`, `inFlightChanged`, `apiServeChanged`, `apiWritesChanged`, ..., `suggestedInFlight` (the poll hint's In flight, never past `IoEngine::MAX_POLLS_UNDER_WAY` x blocks); Auto send (13.8): `autoSendOn`, `autoSendHz`, `setAutoSendHz`, `setAutoSendOn` (without the signal), `setAutoSendOffered(offered, why, shortWhy)` (shortWhy: the greyed rate list's reason), signal `autoSendChanged`; Fast streams (13.9): `setFastStreams` (a row a stream, the card hidden without one), `setFastOn` (without the signal), `setFastOffered(offered, why, shortWhy)`, `fastOn`, `fastCard`, `fastButton`, `fastRateText`, `fastLostText` (for the tests), signal `fastStreamToggled(stream, on)`; a channel's Plot tick (7.14): `setFastPlot` (without the signal), `fastPlot`, `clearFastPlots`, `setFastPlotsEnabled(enabled, why)`, `showFastValues`, `fastPlotBox` / `fastValueText` / `fastRateTip` (for the tests), signal `fastPlotToggled(stream, channel, on)` | GUI test (Connect, Disconnect, Poll, Auto send, Fast streams) |
| `RegistersTab` | the table and its tools; edits the map on the model | `setConnected`, `setShown`, `refreshStatus`; signals `writeRequested`, `readRequested`, `writesAllowedChanged`, `unplotAllRequested`, `mapEdited`, `statusMessage` | GUI test |
| `ValueDelegate` | draws the value and the ⓘ mark; the editor keeps `base` | `createEditor`, `setEditorData` (once), `setModelData` (`WriteRole`) | GUI test (typed text kept, tooltips) |
| `QuickWritePanel` | writes the selected RW register: typed, a named value, a field; only asks | `showRegister`, `setConnected`, `setBroadcastRule`; signals `writeRequested`, `broadcastRequested` | GUI test (value, bits, danger flag, link state) |
| `BusPanel` | the Devices on the link card; shows, the window does the work | `showBus`, `pickerDevices` (the devices with their state, for the pickers), `namesText`, `setBroadcastBlocked` (the kept broadcasts off, with the reason); signals `newBusClicked`, `openBusClicked`, `saveBusClicked`, `closeBusClicked`, `addDeviceClicked`, `editDeviceClicked`, `removeDeviceClicked`, `deviceSelected`, `presetChosen`, `newPresetClicked`, `removePresetClicked` | GUI test (bus step) |
| `BusPresetDialog` | one broadcast preset; OK only when the value is one the register takes | `result` | GUI test (bus step) |
| `BusDeviceDialog` | one device of a bus; OK only when `checkBus` finds nothing | `result` | map test (`checkBus`) |
| `BitView` | the register drawn bit by bit, 16 bits a line (a number register only, 64 bits at most) | `setRegister`, `setValue`, `bitCell`, `fieldCell`; signal `writeField(lsb, width, value)` | GUI test |
| `RegisterDialog` | one&nbsp;definition&nbsp;by&nbsp;hand | `result()` | screenshot extra `regdlg` |
| `ChartTab` | chart controls, measurements, math lines, chart settings | the constructor's settings group (`chart`, or `recording`), `setRecording` / `showSpan` (a recording's chart), `showChartMenu` / `chartMenu` (the right-click), `showLaneMenu` / `laneMenu` / `editLaneRange` (a lane's Y range, its fold), `showLaneActions` (Fold all / Open all lanes), `showLineMenu` / `lineMenu` / `openAnalysis` (a line's histogram or spectrum), `triggerOnLine` (its chip's Trigger on this line), `triggerState` (the trigger row's state), `watchFreeMemory` / `effectiveRamMB` / `setTestFreeMemory` (the RAM against the free memory, 23.3), signal `fastTriggerChanged` (to the engine), `picture` / `copyPicture` / `savePicture` (by the CPU), `exportCsv` / `exporting` / `cancelExport` (on a thread; signal `exported`), fast lines (7.14): `setFastStreams` / `plotFastChannel` / `fastPlotted` / `fastLines` / `appendFast`, fast math lines (9.9): `fastMathKey` / `isFastMathKey`, `mathLines` / `addMathLine` / `removeMathLine`, `fillFastMath` (a recording's), `fastMathNs` (tests), `addNoteAt` / `editNote` (their text asked; signal `notesChanged`), signal `openRecordingRequested`, `setRegisters`, `plotRegister`, `clearLines`, `frame`, `setShown`, `refreshStatus`, `ramNeedText` (static: the memory note's text), `setRegisterLimit`, `fastLines` / `mathLinesShown` / `lineCount` / `lineCapText` (one cap of 64 lines for every kind, 4.8; signal `statusMessage`, a math line refused), `infoText` (the info line; with a width, what fits of it, whole parts dropped), `displayState` (the Display menu in words), `measureUpdates` / `measureFullUpdates` / `measureInfoChanges` / `measureFills` (tests: the measurements made, all of the table, the line over it written anew, the table filled from the threads), `measureTick` (the 250 ms timer's, 23.8), `writePerfLine` (`EVRE_PERF_LOG`, 26.8); signals `mathRegistersChanged`, `unplotAllRequested`, `logged` | GUI test |
| `ChartView`&nbsp;/&nbsp;`ChartWidget` | the&nbsp;chart&nbsp;(chapter&nbsp;23) | fast lines (7.14, 23.11): `FIRST_FAST_KEY` / `fastKey` / `isFastKey`, `setFastStream` / `removeFastStream` / `setFastStore` (a recording's mapped store) / `clearFastStreams` / `appendFast` / `markFast` / `fastStore`, `fastGapAt` (a gap's tooltip), `lastBins` / `timeLabels` (tests: a line's bins and the time labels as last drawn); `setTrigger` / `stopTrigger` / `armTrigger` / `stopRun` / `runTrigger` / `triggerRunning` (Run and Stop) / `forceTrigger` (Force) / `triggerPhase` / `triggerCapturing` / `triggerRate` (the state, 7.13) / `nowEdges` (tests: the now edges as last drawn) / `setTriggerLevel` / `setTriggerEdge` / `triggerSettings` / `setTriggerSettings` / `triggerSettingsTexts` / `setTriggerSettingsTexts` / `setTriggerPosition` / `setTriggerHoldoff` / `holdoffSeconds` / `triggerStateText` / `triggerOn` / `triggerArmed` / `triggeredAt` / `triggerLevel` / `triggerEdge` / `triggerMode` / `triggerKey` / `triggerTag` / `triggerLineY` / `triggerLevelTag` / `triggerEdgeButton` / `triggerTagText` / `triggerPositionMark` / `triggerHolds` / `TriggerSettings` / `triggerPosition` / `triggerHoldoff` / `triggerMarkHovered` / `triggerEdgeHovered` / `triggerTagOpen` / `triggerHandleSolid` / `triggerLevelOffScale` / `triggerLevelBeyondLine` / `triggerTagHovered` / `levelText` (a level as set, 6 digits) / `midRange` (Find level, a line's first level) / `triggerPending` (tests: a crossing waiting for its view) / `fastColumnsBinned` (tests) (`TRIGGER_AT`, `TRIGGER_AT_MAX`, `MAX_HOLDOFF`, `STEADY_WINDOW`; 23.10; signals `triggered`, `triggerSettingsChanged`, `triggerPositionChanged`, `triggerRunChanged`), `fastCrossings` / `fastTriggerWatch` (a fast line's trigger, the engine's; signal `fastTriggerChanged`), `polledColumnsBinned` (tests), `lineSamples` (a fast line's: `withoutGap`, the longest part without a gap; false: not all of the range), `chipAt` / `chipButtonRect` / `hoveredChip` (a chip's ▾ and the chip under the mouse; signal `lineMenuRequested`, a click or a right-click on a chip), `setLanes` / `lanes` / `laneCount` / `laneLabel` / `laneRect` / `laneAtY` / `laneLines` / `laneFolded` / `setLaneFolded` / `foldedLanes` / `setFoldedLanes` / `foldedText` / `laneScroll` / `setLaneScroll` / `laneContentHeight` / `laneScrollBarRect` / `laneScrollHandleRect` (`LANE_MIN_H`, `LANE_FOLDED_H`) / `laneFoldButtonRect` / `hoveredLane` / `laneMenuButtonRect` / `hoveredLaneMenu` / `foldedLaneCount` / `setAllLanesFolded` / `toolTipAt` / `laneBarHovered` / `laneSeparators` / `valueLabelRects` / `stateText` / `stateFullText` / `stateRect` / `laneHeights` / `setLaneHeights` / `resetLaneHeights` (the weights; a lane whose share is under `LANE_MIN_H` held there, the others sharing the rest, so they fill the plot) / `separatorAt` / `hoveredSeparator` / `laneYAuto` / `laneYLog` / `laneYLo` / `laneYHi` / `setLaneYAuto` / `setLaneYManual` / `setLaneYLog` / `laneScales` / `setLaneScales` / `laneYOfValue` (7.12; signals `laneMenuRequested`, `laneYChanged`, `laneFoldsChanged`, `laneHeightsChanged`), `notes` / `setNotes` / `addNote` / `setNoteText` / `removeNote` / `selectedNote` / `noteTag` (7.11; signals `notesChanged`, `noteEditRequested`), `menuRequested` (a right-click), `samples(t0, t1)` (the export's), `showSpan` / `setRecording` / `showLastValues` / `viewSpan` (a recording's chart), `timeAt`, `append`, `frame`, `setWindow`, `setMemory`, `setLive`, `stats` (with `std`, `p2p`, `total`), `range`, `setYLog` / `yLog`, `total` / `totalsSince` (since Clear), `valueLabels` / `yOfValue` (tests: the last frame's Y axis), `pointsPerLine`, `pointsKept`, `bytesNeeded`, `memoryFull`, `setRecordingOn` / `memoryStripText` / `memoryStripTextColor` / `memoryStripTip` (the strip's words for the RAM budget reached, 7.4), `releaseSome` (a frame's slice of what the fast stores' trims let go, `RELEASE_NS`, 23.11), `memoryHandleRect` / `memoryHandleHovered` (`MEMORY_HANDLE_W`: the view's box on the strip, or its handle at a short window, 7.4), `bytesHeld` (tests: the arrays' memory), `setRamBudget` / `ramBudget` (MB; `DEFAULT_RAM_MB`, `MIN_RAM_MB`), `setDrawThreads` (tests: 1 = the GUI thread alone), `setDrawing` / `drawing` / `drawingName` / `drawsOnGpu` / `openingGpu` (who draws the plot; a card opened on a thread), `setHoverValues` / `hoverValues` (the crosshair's box), `refresh` (an update, not of the plot while the card shows it), `plotOnCard` / `gpuPicture` (tests: the card's layer shown, its last frame), `paints` (tests: the frames painted), `binnings` / `lineBuilds` / `setLineReuse` (tests: a held view's lines reused, 23.6), `measureAsync` / `measuring` / `measureKey` / `fullStatsOnWindowThread` (the measurements on the chart's threads, 23.8), `takePerfStats` (the timing aid, 26.8), `legendMeasures` (tests: the legend's chips measured), `FrameBudget` (the frame budget, tests), `stats(keys, cursorsOnly)` (several lines on threads; A and B alone while a cursor is dragged), `draggingCursor` (a cursor held by the mouse), `readoutRowsPerColumn` (static: the crosshair box's rows a column), `readoutBuilds` / `readoutSize` (tests: the crosshair's box made, its size), `spanBarText` / `spanBarRect` / `spanBarTextRect` (tests: the A-B bar as last painted, 7.7); signals `drawingFailed`, `drawingChanged`, `windowChangedByUser`, `yChangedByUser`, `liveChanged`, `memoryChanged`, `cursorsChanged`; `chartAxisLabel` (a value axis label, its step's decimals) | GUI test (math line value and area, hold and live, memory grows; many lines: threads draw the same picture, a spike in an hour shows, the samples' budget, their arrays' memory within it, the memory needed and its note, the RAM box; bins kept from frame to frame, the GPU's frame the CPU's picture, a picture of the chart drawn by the CPU, the layer away after the window painted and back after two frames, the card opened on a thread, the frame budget's rate, the legend's chips measured once, the mouse painted by the next frame, the crosshair's box at most every 50 ms while the mouse moves, a dragged cursor measured at most every 100 ms, Cursors off clearing A and B, the lines measured on threads, the RAM lowered trimming in one go, the memory full on many lines trimming over a few frames, lines filling together growing at different moments, a dragged cursor's A and B alone until it is let go, the last line off (the layer away once the window has the CPU's whole frame), the mouse over the plot on a card, the crosshair's box made at the values' pace and its size steady, Hover values, the Display menu: Drawing, Normalise, Smooth, Hover values, its marks; the A-B bar: its text `durationText` of B − A, the text beside a tag when the span is narrow, a cursor off the view ending it at the plot's edge; fast lines: a spike at every zoom, records at their own times, the labels below a millisecond, a gap and its tooltip, the RAM shared, lanes, legend and crosshair) |
| `GpuLines` | the chart's plot on a graphics card (23.7) | `adapters` (static), `open`, `name`, `present` (a `Frame`: background, `Layer`s of segments, `Sprite` pictures; into the window's layer at its pixels), `setShown` / `shown` (the layer over the window or not), `lastPicture` (read back: under the layer when it is shown; tests) | GUI test (the frame against the CPU's picture, the layer shown and taken away; skipped without an adapter) |
| `MathLineDialog` | name, unit, formula; OK only when valid | `result()`, `setFastStreams` (their channels offered and read; one stream's: a fast math line, said, 9.9) | GUI test (with its completion) |
| `AnalysisWindow` | a line's histogram or spectrum (8.6) | the constructor's `even` (a fast line's records: the spectrum not resampled), `kind`, `histogram` / `spectrum`, `summary`, `readoutAt` / `readout`, `setLogScale`, `plot`, `picture` / `copyPicture` / `savePicture`, `exportCsv` | GUI test |
| `language`&nbsp;(namespace) | the&nbsp;window's&nbsp;language&nbsp;(14.4) | `codes`, `saved` / `save`, `resolve` (System to `en` or `ar`), `apply` (the translators, the direction, Western digits), `current` | GUI test |
| `RecordingWindow` | a recording in a window of its own (12.6): its columns as lines (matched with the map), its notes; the streams' `.evrs` beside it, or one alone (12.7): `fastRecordings` | `open` / `choose` (static: estimate, the RAM question, read on a thread, the window), `recentFiles` / `remember` / `fillRecentMenu`, `windows` / `closeAll`, `chartTab`, `definitions`, `skipped`; signal `logged` | GUI test |
| `FormulaCompleter` | the formula box's completion: the word at the cursor, ranked candidates | `rank`,&nbsp;`wordStart`,&nbsp;`shown`,&nbsp;`addStreams` | GUI test |
| `MonitorTab` | frame&nbsp;log&nbsp;and&nbsp;single&nbsp;requests | `addFrames`, `showAnswer`, `showSent` (a WRITE without ack), `parseHexBytes` (what a WRITE takes), `setSlave`, `setDevices` (a bus: the devices by name); signals `logFramesToggled`, `readRequested`, `writeRequested` | GUI test (READ, the checks of what is typed, WRITE + ack, WRITE without ack, Enter, Clear) |
| `EventLog`&nbsp;/&nbsp;`Notice` | log tab and daily file; one-line pop-up in the tab bar's row | `add`, `setShown`; signals `unseenChanged`, `popUp`; `Notice::post`, `place` (right of the tabs, left of them in right-to-left; again when the tab bar moves or resizes, `eventFilter`, and on a direction or language change, `changeEvent`); signals `showLogClicked`, `noRoom` | GUI test (pop-up covers nothing, Show in Log, right-to-left) |
| `FrameClock` | ticks&nbsp;per&nbsp;display&nbsp;refresh | `start`,&nbsp;`stop`;&nbsp;signal&nbsp;`tick` | runs in every test |
| `HelpDialog` | the&nbsp;help&nbsp;pages | `showTopic` | GUI test (every page with its text, the command line page's options); screenshot extra `help` |
| `Theme`,&nbsp;`ui_helpers` | look&nbsp;and&nbsp;shared&nbsp;helpers | `Theme::apply`, `colors`, `isDark`, `switched` (a widget's colours after a switch of the look), `studioIcon` | GUI test (contrast of both looks, focus ring, hover edges, check marks, colours after a switch, the icon) |

Widgets that tests or the theme find carry fixed object names. Examples: `registers`, `measures`, `quickWrite`,
`qwValue`, `qwEnum`, `bitView`, `groups`, `plotShown`, `hold`, `measure`, `math`, `cursors`, `eventLog`, `notice`,
`detail`, `sideScroll`, `sidebar`, `sidebarHelp`, `pill`, `primary`, `danger`, `valuePace`, `formScroll`, `helpTopics`,
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
| `Series` | name, unit, colour; `times` and `values` (rising times); `chunks[4]`: one `Chunk` per full 8, 64, 512 and 4096 samples (`CHUNK_SIZE`); the last value for the legend; a fast line: `fast` (its stream's `fast::Store`, shared by the stream's lines) and `channel`, its `times` and `values` empty (23.11) |
| `Chunk` | `t0`, `t1`, `min`, `max`, `first`, `last` of 8, 64, 512 or 4096 consecutive samples |
| `series_` | `QMap<int, Series>`: key = `regKey(slave, address)` (slave << 16 | address), `FIRST_CHART_KEY + i` (1 << 24) for math line i, or `fastKey(stream, channel)` = `FIRST_FAST_KEY` (2 << 24) + 256 x stream + channel |
| `Bin` | one pixel column: its column number, sample count, first and last time, first, last, min and max value; `gap`: a fast line's records were lost (or it started again) just before it |
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
  strip says *RAM budget reached* in the warn colour (7.4; `memoryStripText` / `memoryStripTextColor` /
  `memoryStripTip`, with `setRecordingOn` from the window while a recording runs) until the samples are cleared or
  the lines change.
- **The free memory limits the budget** (`ChartTab::watchFreeMemory`, every `FREE_WATCH_MS` = 3 s on the window's
  thread): `effectiveRamMB` = min(the RAM set, held + free - `ramReserveMB()`), at least `RAM_FLOOR_MB` (64), with
  *held* = `bytesHeld()` + `bytesReleasing()` (what the trims let go and `releaseSome` has not freed yet: still taken,
  so it counts as the chart's, else every reading before the frees would lower the limit again) and the reserve
  max(1 GB, a tenth of the computer's memory). Below the RAM set it is the view's `setRamLimit`, and `ramInUse()` (the
  lower of the two) is what `pointsPerLine` and `trimFast` share: the same trims as a budget lowered. The free memory
  moves all the time: a new limit is set only for a step of a twentieth (64 MB at least), or on and off, so the lines
  are not trimmed a little at every reading. `setTestFreeMemory` (tests; `EVRE_TEST_FREE_MB` for a look) gives a free
  memory that what the chart lets go comes back to, as a computer's does.
- **The arrays keep to the budget.** What is dropped moves the rest of a line's arrays to their front
  (`dropFront`), and they grow by about doubling only up to the line's share (`roomForOne`), each line by its own
  step, 2 to 2.44 times (`spread`, by the order the lines came), so lines that fill together grow at different
  moments (all at once, 64 lines at RAM 1 GB moved 0.5 GB in one frame at their last growth); a share that shrank (more
  lines, less RAM) lets the room go at the next trim. Qt's vectors keep the room freed at their front, and when the
  end comes with two thirds of them in use they double rather than move: the samples took up to twice the RAM set
  (2 GB for 1 GB, 64 lines at 1000 Hz). `bytesHeld()` (tests) adds up what the arrays hold. Measured 2026-10-04,
  RAM 1 GB, 64 lines of 1000 Hz (a fake device), Memory 1 h: full after about 12 minutes, then flat at 1.14 GB of
  private working set (Task Manager's Memory) for the 13 minutes after.

The memory is 1 s to 86 400 s (`MIN_MEMORY`, `MAX_SPAN`). The view is 10 µs (`MIN_WINDOW`, with the wheel or typed:
`MIN_TYPED_WINDOW` in `chart_tab.cpp`; below a millisecond for fast lines, 23.11) to 86 400 s. A view longer than the memory makes the memory grow to it.

### 23.4 The smooth delay

Samples arrive in bursts: per poll, per display frame, per TCP packet. If the right edge were exactly "now", the
newest part of the line would jitter in and out of view. With Smooth on, the edge is `now − delay`.
`updateDelay()` computes the delay:

| Constant | Value | Meaning |
|---|---|---|
| gap | now − the newest sample of any line | measured each frame; gaps < 0 or > 1 s are ignored (no data is coming) |
| `PEAK_DECAY` | 0.05&nbsp;s&nbsp;per&nbsp;s | the peak gap is forgotten this fast |
| target | `min(0.5 s, peak gap × 1.1 + 3 ms)` | the delay aimed for |
| `RISE_TIME` | 0.15&nbsp;s | time constant when the delay must grow |
| `FALL_TIME` | 2.0&nbsp;s | time constant when it may shrink |

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
- **Lanes** (`setLanes`). `plotLayout()` gives the frame's plots, one `Lane` each: the whole plot with every line,
  or a lane per unit, however many, stacked `LANE_GAP` (10 px) apart. A folded lane (`Lane::folded`, from
  `lanesFolded_`, the units folded) is `LANE_FOLDED_H` (22 px); the open ones share what is left by their weights
  (`laneWeights_`, by unit, 1 when not set: the equal share), never less than `LANE_MIN_H` (80 px, two value labels)
  (`laneHeights`: the equal share is at least `LANE_MIN_H`, and a weight's unit is it times the open lanes over the
  weights). A separator dragged (`Drag::LaneBorder`, `separatorAt`: its gap's middle a few pixels either way, between
  two open lanes) sets the weights of the lanes above and below it from their heights when the drag began, plus and
  minus the mouse's travel, clamped so neither is under `LANE_MIN_H`, divided by that unit: their sum, and so the
  others' heights, stay as they were. A double-click there clears the weights (`resetLaneHeights`); the settings keep
  them (`laneHeights` / `setLaneHeights`, signal `laneHeightsChanged`). The separator under the mouse
  (`hoverSeparator_`) is drawn in the accent colour, by the CPU and by the card alike. When the stack is taller than the plot, the lanes go
  on below it and scroll: `laneScroll_`, px from the top, clamped to `laneContentHeight()` minus the plot's height
  wherever the layout is made (a resize, a line added or removed, a fold) and stored clamped at each frame, 0 when
  Lanes is turned on. Every `Lane`'s rect is in the widget's coordinates after the scroll, so it may lie partly or
  wholly outside the plot; `laneVisible` gives its part in the plot, and every drawing skips a lane without one
  and cuts the others to it. Each has its own `Axes` (its rect, the frame's times, its range) and its own `YScale`
  (Auto, Manual, Log, and Auto's state): the plot's `y_`, a lane's `laneScales_[key]` by its unit, so a lane keeps
  its range while lines come and go (`laneScales` / `setLaneScales`: the settings' texts). `updateYRange` and
  `followData` run per plot over its lines, folded or out of view too (its range is ready when it shows); the
  binning is the same for all (one time axis). The frame keeps its plots (`lanesShown_`) for the mouse: `laneAtY`
  finds the lane in view under it (a lane out of view is nowhere) for Ctrl + wheel, the double-click and the
  right-click on its labels or its strip (`laneMenuRequested`). A press on an open lane's value labels
  (`pressLaneLabels`, before `pressLanes`, and not one of the lanes' own presses, so its double-click still sets
  Auto) makes it the current lane (`currentLane_`, its unit; the first lane when none: `currentLane`,
  `setCurrentLane`, signal `currentLaneChanged`), whose range the Chart tab's Y range row shows and sets
  (`showYRange`, `applyYFields`, `showYControls`). A lane whose `YScale` is not Auto and linear gets a range tag
  (`rangeTag`: in the value labels' column at the top of its part in view, `tagFont`, `RANGE_TAG_H`); `drawGrid`
  draws it on the CPU (outside the card's layer, as the lanes' buttons) and leaves out the value labels under it; a
  press on it sets the lane to Auto, linear (`rangeTagAt`, `hoverTag_`). `setAllLanesYAuto` / `allLanesYAuto` are
  All lanes: Auto. `pressLanes` takes the lanes' own clicks before the
  cursors and the pan: the scroll bar (`laneScrollBarRect`, painted in `RIGHT_PAD` outside the card's layer by
  `drawLaneBar`, not a `QScrollBar`: nothing goes over the layer; its handle `Drag::LaneBar`), a unit name
  (`LANE_UNIT_W`, folds) and a folded strip (opens); the wheel over the value labels scrolls by `LANE_WHEEL_STEP`
  (40 px). Each lane's fold button (`laneFoldButtonRect`: `LANE_BUTTON_H`, 16 px, at the top of an open lane's unit
  column, or the strip's height for a folded one) is drawn by `drawGrid` as a small triangle, outside the card's layer
  (as the labels are) on a rounded shape, highlighted while `hoverLane_` (set by the mouse's move, cleared when it
  leaves) is its lane (`hoverBar_` does the same for the scroll bar's handle). Under it the lane's menu button
  (`laneMenuButtonRect`, 16 px, 2 px below; `laneButtons` places both, the menu's only with `LANE_NAME_MIN`, 24 px,
  left for the unit name) is drawn the same way with three dots, highlighted while `hoverMenu_` is its lane; a click
  on it emits `laneMenuRequested` with the point under the button; a value label of a lane is kept whole
  inside the lane's part in view (moved at most 8 px off its line, none where 16 px do not fit);
  the unit name takes the column under it. `toolTipAt` gives the lanes' tooltips (`QEvent::ToolTip`).
  `setAllLanesFolded` folds or opens every lane now shown (Fold all / Open all). The separators (`separatorsY`: the
  middle of each gap whose middle is in the plot) are a 1 px line in `control`: `drawGrid` draws them from the value
  labels to the plot's right edge, the card adds its part from its layer's left edge as grid segments
  (`laneSeparators`: as last painted, for the tests). A folded strip is a picture (`foldedPicture`, of its part in the plot, made again when its texts change):
  its unit, then each line's dot, name and value (`foldedItems`: live, the legend's value; held, the last sample in
  view), cut with … where the room ends. The crosshair leaves a folded lane's lines out of its box.
- **Log** (`setYLog`, `y_.log`; not while normalised). `Axes::setRange` keeps log10 of the range's ends, and
  `Axes::y` maps log10 of the value between them; a value <= 0 maps to the bottom edge. Every drawing takes y from
  `Axes`: the CPU's lines, the card's segments (`plotOnGpu`), the crosshair's dots and the memory strip (each line its
  own `Axes` on the Log scale, its positive range), so the card cannot draw otherwise. Auto (`followData`) works in
  decades: the bins carry the smallest positive value they are known to hold (`BinnedLine::posLo`: a bin's min, else
  its first, last or max, enough for a range), the top at most `MAX_DECADES` (9) above the bottom, one decade at
  least, the margins and the shrink in decades. Ctrl + wheel zooms in decades. Manual must be above 0
  (`setYManual` returns false). `gridTicks` gives the decades and, while a decade is 24 px or more, the faint 2..9
  (`GridTicks::minor`, drawn by both paths in the grid's colour at 45 %); the labels come from `chartLogLabel`.

### 23.6 The drawing fast path

`paintEvent` (`paintFrame`) draws in this order:

1. card
2. grid (on a card: its labels only; the card draws the plot, 23.7); with lanes also each lane's fold and menu
   buttons and unit name, and the separators between lanes
3. cursor span, then the lines (each in its plot: lanes, its lane's `Axes`, clipped to its part in the plot), then
   the lines of the notes, the trigger's level and the cursors, the folded strips over them, then their tags and the
   A-B bar (`Marks::Lines`, then `Marks::Tags`: the card's order, its pictures over all its layers) (not on a card)
4. the lanes' scroll bar, in the right pad (on a card too: it is outside the layer)
5. memory strip
6. legend (the chips clipped to their part of the row, then the scroll bar and arrows when they overflow)
7. crosshair (not on a card)
8. state text

The following choices keep a frame cheap on a high-DPI screen:

- **A held view reuses its lines.** Dragging a cursor, a note or the trigger's level over a held view changes no
  line. `viewBins` keeps the last binning while the view's times and columns, the lines' generation and each line's
  samples in view (as absolute sample numbers, `dropped` plus the index: samples are only added at the end and let
  go at the start) are the same; `linesKey` (the binning, the plots' rects and Y ranges, folds, Normalise, the theme,
  the scaling) decides whether the lines are drawn again. On the CPU, held, the lines are a picture
  (`drawLinesPicture`, on whole device pixels as a stripe of `drawLines`) drawn onto the chart at every frame; on a
  card, its line segments (`gpuLines_`) are sent again as they are. Only the marks, the pictures and the rest are
  made anew. Live, the view moves at every frame and both are done every frame. The tests count both
  (`binnings`, `lineBuilds`) and compare the picture with one drawn without the reuse (`setLineReuse(false)`): an
  antialiased edge drawn into the clear picture and then onto the chart is rounded once more, at most 2 of 255.

- **Lines as cosmetic polylines.** Qt's raster engine has a fast path for 1-device-pixel antialiased cosmetic
  lines, and none for a wide antialiased stroke. `strokePolyline` draws the polyline with a cosmetic pen of width 0,
  `copies = max(2, round(1.5 × devicePixelRatio))` times. It shifts the copies by one device pixel side by side, and
  also above and below: a plus shape about 1.5 logical pixels thick. One wide antialiased stroke cost more than
  20 ms a frame for four lines at 225 % scaling on a 4K screen (comment in `chart_widget.h`). The memory strip uses
  a single thin copy.
- **The card.** A plain fill, then four small corner shapes in the window's colour. One antialiased rounded
  rectangle the size of the chart costs milliseconds at 4K.
- **The grid** is drawn without antialiasing: crisp 1 px lines, cheaper. With clock times, time grid lines sit at
  multiples of the time step on the time base, so they are fixed to wall-clock times and move with the data. Labels
  are `HH:mm:ss`, with `.z`, `.zz` or `.zzz` as the step needs.
- **Divisions** (`divisionsShown`: Time grid on Divisions, or on Auto below `DIVISIONS_BELOW` = 1 s). `gridTicks`
  puts 0 at the right edge, or at T while `timesFromT` (the user's trigger, the view not live, the last crossing in
  view), and a line every tenth of the plot from there (`DIVISIONS`), as fractions of the plot's width from 0's x,
  each rounded to a millionth of a pixel. Not from the times: they are large numbers of seconds, and their rounding
  moved a line lying on a pixel's edge by a pixel now and then. `GridTicks::lineX` (the lines inside the plot; its
  edges are the plot's own) is what both paths draw, the CPU in `drawGrid` and the card as grid segments, so the
  card's lines lie where the CPU's do (a picture check). `drawDivisionLabels` writes the offsets (`offsetText`,
  each an isolated left-to-right piece) every 1, 2 or 5 divisions as their width needs, and the readout
  (`divisionReadoutText`: the division and the clock time at 0, its clock written again at most every
  `DIVISION_CLOCK_MS` = 500 ms while live). `drawState` draws the readout in the state's row above the plot, left of
  the state (alone at the row's right end without one), so no time label is left out for it (one hid *-2 ms*); its
  box is `divisionReadoutWidth` wide, its text measured with every digit a 0, so the legend's end does not follow the
  clock's digits, and `fitState` keeps that room inside `STATE_SHARE`, dropping the state's parts first. The
  wheel steps the window by `divisionWindow` (the next 1, 2 or 5 per division), part notches of a touchpad adding up
  to one (`wheelNotches_`).
- **Times from T** (U-7). `timeOrigin` is T while `timesFromT` holds for the view, else NaN; `fromTText` writes a time
  as its distance from it (*T -0.250 ms*, three decimals in the window's unit, one left-to-right piece). The
  crosshair's box puts it in its time row in place of how long ago (the row's width kept for the widest of both, so
  the box does not move), a cursor's tag gives it in its tooltip, and the Chart tab's measure line after the span.
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
  again only when the data has moved a pixel on the strip (`strip.columnSeconds()`) and at least `STRIP_REDRAW_S`
  (a second) has passed since the last drawing (its lines bin the whole memory: for a minute's strip that was 15
  times a second, a frame's worth each), or when the lines or the memory changed, or the size; between those, the
  image is drawn. The view's mark on it is drawn at every frame.
- **The legend** draws only the chips in its part of the row, not every chip under a clip, into a picture
  (`legendImage_`) made again only when its key changes: the lines, the values' tick, the scroll, the size,
  the scaling, the theme or the chip under the mouse (its ▾ lit, `hoverChip_`). The chips lie above the plot,
  outside the card's layer, so this one picture serves both drawing paths. Between those the picture is drawn (1.3 ms a frame at 4K drawn each time). The chips'
  widths (`legendLayout`, also used at every mouse move for the pointer's shape) are measured once while the lines
  and the font stay (`chipWidths_`): 64 names measured at every frame and every mouse move held the chart near 52
  frames a second with the mouse moving. The row ends `STATE_GAP` (16 px) before the corner (the time/div
  readout, else the state's text): `fitState` takes
  the first of `stateVariants` (the whole, then *Live to follow*, *click / drag* and *manual* dropped in turn) that
  fits `STATE_SHARE` (40 %) of the plot, or the shortest whole while the legend keeps its first chip and arrows,
  else the shortest ending in … . It measures the time held as the widest number, so the row's end does not follow
  its digits. `drawState` writes it in the application's direction (a right-to-left mark either side of each dot,
  so a part's Latin end and the next part's Latin start do not run together); the chips are clipped short of the
  row's arrows.
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
  system is still busy with is dropped, the next comes a few ms later. A held view has no next frame coming (a
  recording's window, 12.6, never has): a dropped frame (`GpuLines::droppedFrames`) is painted again 16 ms later
  (`DROPPED_AGAIN_MS`), until one reaches the layer. Without it the layer kept its last frame: after the window was
  made bigger, at its old size, and the window's own plot beside it blank (the card's part is not painted by the CPU)
  until something painted again: the black bar over the new part of a recording's window (2026-10-08). The chart
  itself paints only what is around
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
  (`GpuLines::Sprite`): the cursors' tags (`tagPicture`), the notes' tags (`notePicture`, kept by their text, width
  and look; their dashed lines with the cursors'), the A-B bar with its text (`spanBarPicture`), the crosshair's dots
  (`dotPicture`) and its box (`readout_`). A picture stays on the card while it is the same `QImage`
  (`cacheKey`): the dots and tags once, the box when it is made again (23.6), the A-B bar when its text, its length or
  where the text stands changes (a live view moves both cursors alike, so the bar keeps its picture while it scrolls).
- **The A-B bar** (7.7) is laid out once for both drawings (`spanBar`: the bar, the text's box, whether the text is
  inside it) and painted by one function (`drawSpanBar`): by the CPU in `drawCursors`, and for the card into a
  picture of the bar and its text, placed on whole pixels as the tags are. So the two drawings show the same bar.
- **Lanes on the card.** Every lane in one frame: each lane's grid from its own ticks, each line's segments made
  with its lane's `Axes` and cut to its lane's rows in the plot (`clipSegmentY` to the lane's rect within the plot's,
  the line's width of room): the card has no clip of its own, and a line past a Manual range would draw into the next
  lane, a lane scrolled half out above the plot's top. A lane out of view or folded gets no segments; a folded strip
  is the CPU's picture (`foldedPicture`), the first of the frame's pictures, so it lies over the marks' dashed lines
  and under their tags as the CPU paints it. The separators between lanes are grid segments in `control`, from the
  layer's left edge (the CPU paints them up to there). Without lanes the layer's edge cuts the one plot.
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
- RMS = √(∫ v² dt ÷ time);
- the standard deviation from the same trapezoids over v − K, K the first sample of the range (shifted sums: a
  12 V line with 1 mV of ripple squared whole loses the ripple to the rounding of 144 V²), and the peak to peak,
  max − min, in the same pass;
- the total since Clear (`Stats::total`, `total(key)`): not over the range. `append` sums each line's trapezoids as
  the samples come (`Series::total`, `totalT`, `totalV`), a gap over `TOTAL_GAP` (1 s) not bridged, so the memory's
  trims take nothing from it. `clearData` resets them, and `clearSeries` (another map); a line removed leaves its
  total in `keptTotals_`, taken back when a line of the same key, name and unit comes again. `totalsSince()` is the
  time of the first sample after the reset.

The table shows the area in unit·s and unit·h (8.3), and the total in unit·h. Its columns are
`ChartTab::MeasureColumn`; a hidden one (`chart/measureColumns`, the header's menu `measureColumns_`) is not fitted. It is updated every 250 ms while the Chart tab is shown and
Measure is on, when the tab is shown, and when the cursors move: at once, then at most every 100 ms while they move
(`ChartTab::measureSoon`, `MEASURE_FOLLOW_MS`), the last place always measured. While a cursor is dragged, only
A, B and B − A follow it (`stats(keys, cursorsOnly)`, the columns not fitted again); the rest over A → B comes once it
is let go (`cursorsChanged` at the release) and at the 250 ms pace: all of it at every step held the chart near 51
frames a second with 64 lines. Each update writes the cells with the table's updates off and fits the columns
before they are on again (`setUpdatesEnabled`): one repaint per update, not one per cell; the line over the table
(`measureInfo_`) is written only when its text changes, a new text laying the panel out again. A dragged cursor moves at every
mouse move, and 64 lines over 5 minutes of 1000 Hz samples measured at each held the chart near 17 frames a second.

All of it is measured on the chart's threads without the window thread waiting (`ChartView::measureAsync`; 64 lines
over 5 minutes of 1000 Hz took about 35 ms on it, two frames): the lines' arrays are handed to the threads as Qt's
shared vectors (copied only when changed), and while they read, the samples given to `append` wait in a queue
(`heldSamples_`, the trims they bring with them) and go in when the result comes back; a line taken off or the chart
cleared meanwhile leaves the threads their copy. One measurement at a time: one asked for while another runs waits,
the newest replacing an older (`measureNext_`). The table is filled when the result is in (`ChartTab::fillMeasures`;
lines that came or went meanwhile: measured again). A, B and B − A while a cursor is dragged stay on the window thread
(cheap). The 250 ms timer (`measureTick`) measures all of it again only when what it depends on changed: the key
(`ChartView::measureKey` and the columns hidden) holds the range, the cursors (one not placed as −inf: NaN is never
equal to itself), the lines, each line's samples in the range as absolute sample numbers (`dropped` plus the index:
samples after the range or let go before it change nothing; with the cursors, the samples beside them too), and
Normalise. Otherwise only the "Since Clear" column follows (`total`, running sums); and nothing while a cursor is
dragged. A live view's range moves at every frame, so it keeps the 250 ms rhythm, on the threads.

Math lines are evaluated in the GUI thread from the frame's samples (`MathLines::evaluate`). A point is made for
each sample of the line's first input register. The other inputs are matched by **identical time**, which works
because a poll gives all its registers the same time (20.8). A poll that lacks one of the inputs gives no point.
Example: the line `P [W] = SUPPLY_V * SUPPLY_I` needs both registers plotted or sampled. `pushPlotted()` makes sure
they are sampled even when they are not plotted.

A fast math line (9.9; `MathLine::stream` >= 0, `channels[i]` the stream's channel of input *i*, -1 for a register)
is computed in `ChartTab::appendFast`, on the window's thread, from each block as the window appends it, so it
shares `sync()`'s `FAST_APPEND_NS` budget (23.11): `MathLine::evaluateRecords` decodes the channels of each record and
runs the formula, the registers' held values (`held_`, the last sample `frame()` brought) as constants. The values go
as 4-byte floats, run by run between the records that give no number, through `ChartView::appendFast` and `markFast`
into a store of their own (`fast::Store` of the chart's stream `MathLines::fastStream(i)`, one f32 channel, made by
`setFastStream` with a name of the line's name, unit, formula and stream, so an edit starts a new store), with the
block's record numbers and the stream's time marks: its times are the stream's. A store that has not seen the
stream's current start gets one of its own and the stream's newest mark (`streamStarts_`, `streamMarks_`). The
trigger on it is looked for there too, by a `fast::TriggerScan` of its own (`mathScan_`) over each run, the engine
told to watch nothing (`fastTriggerChanged(-1)`). At a million records a second one such line (`ADC.I_LOAD *
ADC.V_BUS`) took 30 to 35 ms a second of the window's thread on the test machine (the timing aid's `math`, 26.8),
and the chart kept its 55 frames a second. A recording's window computes its fast math lines at the end of its
feed (`ChartTab::fillFastMath`), from the stream's mapped store, with `fast::Store::fillFrom`: the source's starts
and marks, its segments, and a new segment after each record left out.

### 23.9 Frame budget

At 60 Hz, a frame lasts 16.7 ms. The work in the GUI thread per frame is:

- `sync()`: a snapshot, the rows that moved, the samples, the monitor lines;
- the chart's paint;
- the table's repaint, at most 20 times a second.

While frames come, a change of the chart waits for the next one (`refresh()`: the mouse moves up to 1000 times a
second). When no frame came for 250 ms (`FRAMES_STOPPED_MS`) it is painted at once; a change made within those
250 ms is painted when they are over (`framesStopped_`) if no frame came after all. A recording's window has
frames only while it is fed (`frame()`): the change right after (the samples a measurement held back, the card
opened) stayed off the screen until something else painted. A measurement that shares no line's arrays with the
threads holds no samples back (`startMeasure`): the recording's window measures as it is made, before its lines
come, and its file went in only when that measurement was done. A held view's frame tells the Y boxes its ranges
when they changed (`yRangesShown`), so Auto's boxes show the range of the frame on the screen at once.

The chart measures itself. The info line on the Chart tab shows frames per second, the average paint time (a
running average, 0.9 old + 0.1 new) and the smooth delay. A paint time close to the frame time shows up as fps below
the display rate. Samples are not lost when frames drop: the engine keeps them until the next take.

### 23.10 The trigger

- **The line watched gone.** `ChartTab::fillTriggerLines` leaves the list with no line chosen (its name the
  placeholder) when the line saved is not on the chart, never the first line in its place; `refreshStatus` then
  stops the trigger (`stopTrigger`) and arms it again when the line is back. The chip menu's entry is checkable,
  ticked for `triggerKey`; unticking it and the row's Off untick `trigger_` (Display -> Trigger), whose toggle does
  the rest.
- **Each line its own.** `TriggerSettings { level, edge }` are kept in `triggerSettings_` by the line's name (the
  settings' `triggerSettingsTexts`, the Chart tab's `chart/triggerLevels`); a line never set takes `midRange` (the
  middle of its range as last binned, else its newest value) and Rising. The row's **Find level** sets the level to
  the same `midRange` (public for it: one rule for both). `Trigger` holds the rest: the line watched
  (`key`), the mode, the last crossing (`at`), `armedFrom`, `pending`, `stopped` (by the user) and `since` (armed
  last from there).
- **A polled line's crossing is seen in `append`**: for the line watched, while armed and for samples after
  `armedFrom`, the sample before and this one around the level in the edge's direction (Rising: before below, this at
  or above; Falling the other way; Either both). The crossing's time is straight between them and must lie after
  `armedFrom`. `armTrigger` sets `armedFrom` to the line's newest sample: a crossing already kept does not count; in
  Normal and Auto not before the last crossing's hold-off has passed (an Arm took a crossing inside it).
- **A fast line's crossing is found by the engine** as each block comes (`IoEngine::takeBlock`, on its thread):
  `fast::TriggerScan` (io/fast_stream.h), one in each stream's `FastRun`, looks at each pair of the watched channel's
  records in one segment (the block's first with the last of the block before only when nothing was lost and the
  stream did not start again) and gives the crossing's record in the block and where between the two records the
  level lies (`Crossing`), its time from the stream's clock. What it watches is a `TriggerWatch` (the channel, the
  level and edge, `from`, `rearm` and a `serial`), made by `ChartView::fastTriggerWatch` and handed on at every change
  (`postWatch`: a new serial) through `ChartTab::fastTriggerChanged` and the window's `post` to
  `IoEngine::setFastTrigger`. The crossings go with their block (`FastBlock::crossings`); `ChartTab::appendFast` gives
  them to `fastCrossings` after the block's records and its mark, with where the block begins in the store
  (`appendFast`'s return), so a crossing's time is the store's, between its two records; one found for an older
  serial is not used. The engine follows the window's rule for the next crossing (`rearm`: the larger of the hold-off
  and the view's fill; Single: off after the first), so Normal and Auto go on without waiting for the window. The
  window's per-frame look at the records (before: `scanFastTrigger`) is gone.
- **No crossing lost between the two.** A new watch would see only the blocks after it, while a frame's blocks (more
  after the window's thread was held) wait in the queue, after the window's newest record, which the arm counts from:
  `setFastTrigger` scans them again (`IoEngine::rescanWaiting`), the first paired with the record before it
  (`FastBlock::before`, `TriggerScan::pairWith`). A block the queue drops (past `FAST_QUEUE_BYTES`) never reaches the
  window: its last crossing arms the scan again from it (`TriggerScan::dropped`), else Single would wait for ever. A
  crossing for this arm that `fastCrossings` cannot hold on (its record before is not in the store, as after a Clear,
  or at or before the one held on) posts the watch again, and so does Clear. `from` is on the stream's clock: the
  store shifts a new start that would begin before the last one ended (`Epoch::shift`), the clock does not, so
  `fastTriggerWatch` takes `Store::newestShift` off and `markFast` posts again when a start is shifted.
- **A watched stream that goes.** Nothing of the watch reaches a stream, a run or a line that is gone. The engine's
  answers to a stream's switch look the run up again by its place, its name and the switch (`fastRunAsked`): a map
  loaded meanwhile may have fewer streams or another one there. `setMap` drops the queued blocks whose place now holds
  another stream or another layout, and gives a stream of another layout a fresh `TriggerScan`; `rescanWaiting` reads
  a block only when its records fill its count in the stream's layout. On the window's side, removing the watched line
  (or every line, or every fast line) and a new store for the watched stream post the watch again: the line gone, the
  engine watches nothing, and `fastCrossings` drops the crossings of a line that is not on the chart. A Disconnect, a
  Connect and a stopped stream keep the watch: once the blocks come back it counts their crossings as before (an
  Arm takes the next one).
- **Changes while dragging** (`watchDue_`): a drag of the level or of the crossing's place changes the settings at
  each mouse move but posts at the next `frame()` (and at the release), once: each post is a new arm, and one per move
  dropped every crossing found meanwhile. The polled line's check in `append` reads the level at once.
- **The modes** (`fireTrigger`): the view holds with the crossing at `triggerPosition_` of the window (`TRIGGER_AT`,
  50 %, by default, the middle as on the reference scopes, for a tab with no `chart/triggerPosition` saved;
  `TRIGGER_AT_MAX` 90 %): `viewEnd_` is after now while the samples after it come, so the plot
  fills from the left as an oscilloscope's does. Single then waits for `armTrigger`; Normal and Auto count the next
  crossing after `armedFrom` = the crossing + the larger of `holdoffSeconds()` (`triggerHoldoff_`, -1: the window's
  length) and the view's fill, by the samples' time. Auto in `frame()`: armed, held by a crossing (`Trigger::holding`,
  set by `fireTrigger`, cleared by `setLive`, `holdAt` and `showSpan`: the user's Hold, a pan, the memory strip; a
  zoom keeps it), and no crossing a window's length after `armedFrom` by `triggerTime()` (the line's newest sample's
  time, as `armedFrom`; by the clock a fast line at a short window ran live at every other frame) for `triggeredSpan`
  after `lastCrossing` (the last held or `pending`): `setLive(true)`, so Auto runs free exactly when its state says so.
  `triggerArmed` and `triggerPhase` take the same time. Every change of `window_`
  goes through `putWindow` (`setWindow`, `setMemory`, `showSpan`, the wheel's `zoomTime`): held on a crossing, Normal
  and Auto count the next after the new fill, and the engine is given it.
- **A steady picture** (`crossed`, `firePending`): a window under `STEADY_WINDOW` (1 s) already held on a crossing
  keeps a later crossing in `pending` until the line's newest sample reaches the end of that crossing's view, then
  holds on it (`frame()` looks at every frame, and so does the next crossing): every picture is whole, and a
  repeating wave stands still. A 1 kHz sine in a 10 ms window at 60 frames a second showed 25 of 33 pictures half
  drawn before, none since (26.2). The binning of a re-trigger needed nothing: the view's bins are kept on absolute
  columns (23.2, 23.11), so a crossing that moves the view bins only the new columns; 50 re-triggers with no hold-off
  and the crossing at 90 % bin 40 columns each of a polled line's 200 in view, and the samples' columns of a fast
  line (100 a frame of 998 in view) (`polledColumnsBinned`, `fastColumnsBinned`). With the hold-off a window, the
  views do not overlap: the pending view's fast lines are binned while it fills (`binPending`, 23.11). `pending` is
  dropped by Live, Stop, Arm, Clear and a change of the level, edge, hold-off or place: else it pulled a live view
  back to an older crossing.
- **Run and Stop** (`stopRun`, `runTrigger`): Stop clears `armed` (the engine's watch goes off with it), drops
  `pending`, sets `stopped` and holds a live view as shown (`holdAsShown`);
  `at` and the view stay. Run is `armTrigger(false)`: armed from the newest sample without the last crossing's
  hold-off (a scope's Stop clears it; an Arm keeps it), Auto `setLive(true)`, Normal and Single `holdAsShown` when
  live, so only Auto rolls while it waits. `setTrigger` while stopped (the row's line, edge or mode) sets the key and
  the mode and stays stopped; another key drops `at`. `holdAt` (a pan, the memory strip) and `showSpan` call
  `stopRun` while armed, when the end asked differs from the view's (compared before the clamp to now: a view still
  capturing ends past now); the wheel's `zoomTime` passes `user = false`, and a view the trigger holds (`holding`)
  is zoomed in place, its end kept up to the crossing's own view at the new window, never taken live. Each change emits `triggerRunChanged`, and Single's crossing too: the Chart tab's button
  (`showHoldButton`) is Run / Stop while `triggerOn()`, by `triggerRunning()` (armed), else Hold / Live. While
  `triggerPhase()` is Stopped it has `stopped` = true, which the style sheet draws in the warn colour
  (`QPushButton#hold[stopped="true"]`: `warn` on its tint, the corner's amber), its play icon in `warn` too
  (`themeChanged` draws it again in the other theme); after Single's crossing (Done) it is the plain Run.
- **Force** (`forceTrigger`): only in Waiting, not in Auto: `fireTrigger(triggerTime())`, the view held at the watched
  line's newest sample as a crossing would hold it (its T there, `atLevel` the level), then `postWatch` so a fast
  line's engine counts from the new `armedFrom`. The row's `triggerArm` is one button for Arm and Force
  (`showTriggerState`: Force while Waiting in Normal or Single, Arm in Single otherwise, hidden in Auto; its
  `force` property says which a click is), its width fixed to the wider of the two words, primary or not, so the
  row keeps its length.
- **The state** (`triggerPhase`): Off, Stopped, Done (Single after its crossing), FreeRunning (Auto live), Triggered
  (`lastCrossing`, the later of `at` and `pending`, after `since`, and the line's newest sample within
  `triggeredSpan` of it: a window plus the larger of the hold-off and the fill, `TRIGGERED_AT_LEAST` (1 s) at least,
  so a short window's crossings never flip it to Waiting between them), else Waiting. `triggerStateText` (the corner)
  and `ChartTab::triggerState` (the row) read only this and, for Single alone, `triggerCapturing` (held on the
  crossing, `holding`, and the newest sample before `viewEnd_`): Normal and Auto say *Normal · triggered* in both
  places at every crossing (*capturing after T* came and went at each re-trigger of a short window, and the row's
  rate of crossings changed its digits; both are gone). Normal waiting with an `at` (a capture held before) is *Normal · waiting, last at
  14:03:12*, from `at` to the second: `at` changes only with a crossing, so the text is fixed until the state
  changes; a level beyond the line's range still says that first. While the trigger is on, `stateVariants` is its state alone, the corner amber only
  for Stopped. `showTriggerState` writes the label only when its text changed.
- **The now edge** (`nowEdgeLines`): while `triggerCapturing`, a 1 px line at the newest sample's time in every open
  lane in view (cut by the plot as the lanes are), in the muted colour at 55 %, under the marks' lines (the cursors',
  the notes', the level's): the CPU draws them before those, the card a layer of its own from the same lines before
  the marks' layer (`nowEdges` for the tests).
- **The room** (`plotRect`): while `triggerMarked()` (the user's trigger on, not a short window's lock) the plot is
  `TRIGGER_STRIP_H` (26 px) lower at its top, for the flag's strip under the legend, `TRIGGER_LEFT_W` (21 px)
  narrower at its left, for the level's marker's column (the value labels, `valueLabelRects_`, end left of it), and
  its right pad is
  `TRIGGER_PAD` (98 px) instead of `RIGHT_PAD`, for the tab: `TRIGGER_TAB_X` (15 px, past the lanes' scroll bar at
  `LANE_BAR_X` + `LANE_BAR_W`) right of the plot, `TRIGGER_TAB_W` x `TRIGGER_TAB_H` (80 x 24 px, about three times
  the 32 x 18 px handle it replaced; a longer level is cut with "..." and whole in its tooltip). All three lie outside the card's layer (the plot and 2 px around it), so the CPU
  draws them on both paths, as the lanes' bar, from the rects `triggerGeometry` gave the frame.
- **Drawn** by both paths from the same place (`triggerGeometry`: the line's plot, the level's height kept within it,
  the level's tab right of the plot at that height, kept within the lane's rect and cut to the part of it in view
  (`triggerLevelTag_`; `triggerTabFull_` the whole), its arrow part `TRIGGER_TAB_BUTTON` wide at its right end; the
  level's marker, `TRIGGER_LEFT_W` x `TRIGGER_TAB_H`, its right 2 px left of the plot at the tab's height
  (`triggerLevelMark_` cut, `triggerMarkFull_` whole); where the line crossed (`triggerTag_`, 14 x 16 px around it,
  when the crossing is in view and the lane's part in view is 16 px high: x of `at`, y of `atLevel` by the lane's
  axes, normalised through the line's own range, kept inside the lane's part in view; a level moved later leaves it
  there), not drawn: it is the flag's x): the CPU draws a dashed line, the card the same dashes with the cursors'.
  `drawTriggerTab` (after the plot, before the lanes' bar) carries the dashed line on across both gaps to the points
  and draws `levelMarkPicture` and `levelTagPicture`, each clipped to its part in view, their boxes in the middle of
  their grab areas, on whole device pixels: the marker's *T*; the tab's `triggerTabLabel` (*0.4 A*: `levelText` and
  the unit) centred and elided to its room, a thin divider (the line's colour at 60 %) and the arrow (`edgeSymbol`)
  in its part, filled stronger under the mouse (`hoverEdge_`); their pointers at the line's height (solid by
  `triggerHandleSolid`, the level equal to `Trigger::atLevel`, set by `fireTrigger`; off scale ▲ or ▼ in the box
  instead), both lit while `hoverLevel_` or dragging. The pictures come from `markPicture`, the family's one shape:
  a `TRIGGER_MARK_H` (18 px) box with 4 px corners and a border of 1 px (2 lit) in whole device pixels, on
  `surface2` (`border` lit), joined to a `TRIGGER_POINT` x `TRIGGER_POINT_W` (5 x 10 px) pointer as one outline
  (`QPainterPath::united`), drawn in device pixels so the outlines are crisp at 225 %, the text in the chart's small
  font (`labelFont`). `triggerTagText` is the tab's *0.4 A ↑*, `triggerTagLabel` the tooltips' words. A level whose y lies outside
  the lane's rect (`triggerOffScale_`, 1 above, -1 below) is clamped to it and drawn as `Qt::DotLine` with flat ends,
  the card's dashes 1.2 px long with gaps of 2.4; its marker's and tab's pointers are ▲ or ▼ and their tooltips say
  *(above range)*.
  A press on it keeps
  `dragStartY_`, and the drag changes the level only once the mouse has moved `startDragDistance` up or down: its y
  is the edge's, whose value is not the level. The Auto range is computed
  from the lines alone, so a level never widens it. `triggerBeyond_` compares the level with the line's own range in
  view, for the row's *waiting: level above the line's range*. `levelText` writes a level with 6 significant digits,
  for the tag, the drag and the row's box (`showLineSettings`; `applyTrigger` writes the box back only when it says
  another); `chartNumber` stays for measured values. The crossing's place (`drawTriggerMark`,
  `flagPicture`) is a flag in the strip: a `TRIGGER_MARK_W` (16 px) wide box of the family with a *T* over its
  pointer, solid, centred on the crossing's x (`triggerTag_`, on a whole device pixel; the place in the window when
  the crossing is not in view), the pointer's tip on the card's layer's top, 2 px over the plot, so the layer hides
  none of it; the strip is the flag's 23 px, 1 px under the legend and 2 px over the plot. `triggerMark_` (box and
  pointer) takes the mouse; the CPU draws it on both paths. The old triangle on the
  time labels' row is gone.
- **The mouse.** A press on the tab's arrow takes the next edge; on the tab, the marker or within 4 px of the level's line
  (`Drag::Level`) moves the level by `Axes::value`, from where it was taken (`levelGrab_`; normalised: through the
  line's own range); on the flag (`Drag::Position`) the place, 0 to 90 %, from where it was taken (`positionGrab_`).
  At the release `triggerSettingsChanged` or `triggerPositionChanged`, which the Chart tab saves; a double-click on
  the flag (`mouseDoubleClickEvent`) sets `TRIGGER_AT` and emits `triggerPositionChanged`. `toolTipAt` says what each
  does (the tab's: its words, then *Drag: the trigger level · Click the arrow: ...*; the marker's: its words, then
  *Drag: the trigger level · the edge: in the Trigger row*; the flag's: `triggerPointText` (the line, `atLevel` in its
  unit, `atEdge`, the time) when a crossing is held, then *Drag: where the crossing sits in the window · Double-click:
  back to 50 %*).
- **The row** (`buildTriggerRow`): the line's unit in a label after the level's box (`triggerUnit`, muted by the style
  sheet), set by `showLineSettings`; **Find level** (`triggerFindLevel`) after it; each line in the list with its
  colour dot (`stateDot`, 10 px, as its chip; `fillTriggerLines`); the place's label *position* with its tooltip; the state an `ElidedLabel`
  (`QSizePolicy::Ignored` across, cut to its room, the whole text in its tooltip), so its text never sets the row's
  least width: the window grew by 6 px in English and 32 in Arabic once a state was written.
- **A short window's lock** (`updateShortLock`, at each `frame()`): with `shortLockOn_` (Display's *Lock short
  windows*, `chart/autoShortWindows`), no user's trigger, a live view (`live_`, or one the lock holds), a window under
  `SHORT_LOCK_WINDOW` (100 ms), not a recording and a line busy enough, the trigger is Auto on `lockKey_` with
  `Trigger::automatic` set and its own `lockSettings_` (the line's `midRange`, Rising; the level taken again each
  `TRIGGERED_AT_LEAST` while it runs free), so the user's levels kept by name stay as they are; `watchedSettings`
  gives the lock's, and the polled line's check in `append` reads it. `lockKey_` is `busiestLine()`: the first fast
  line with `SHORT_LOCK_SAMPLES` (20) in the window (`samplesInWindow`: the samples after the line's newest less the
  window, a binary search), else the line with the most there, at least that many; -1: none, and the lock ends as
  for no line. It is chosen again only when `seriesGeneration_` moved (the lines changed) or the line watched has too
  few, so it does not move from line to line at each frame (`shortLockKey()`, tests). Everything else is Auto's own work (the
  crossings, `pending`, `binPending`): nothing binned of its own. `triggerOn()`, `triggerKey()` and `triggerMarked()`
  leave the lock out (no row, Run / Stop, tab, flag, room or now edge), and `live()` counts it as live, so the
  toolbar's button stays Hold: Hold (`setLive(false)`), a pan (`holdAt`) or a span shown (`showSpan`) call
  `endShortLock` and hold the view as shown (no lock until Live); a longer window, the setting off or no line end it
  and go live; `setTrigger` (the user's) clears `automatic` and takes over. The corner says *Auto (short window)*
  while Triggered and *Auto · free running* otherwise (`triggerStateText`), with the other parts of the state
  (`stateVariants` leaves out *held*). `drawState` draws that last part as a badge (`stateBadge_`: the accent on a
  tint of it, `BADGE_PAD` either side, `BADGE_GAP` from the other words, at the end the words are read to); its width
  counts in the state's room (`stateWidth` in `fitState`), and a state too narrow even for its shortest text is
  written plain, cut.

### 23.11 Fast lines

A fast stream's records (7.14) are not kept as `times` and `values`: at a million a second that would be 16 bytes a
record and a time for each. `fast::Store` (`src/model/fast_store.*`) keeps them as they came:

```
 pieces_      [ 65 536 records ][ 65 536 records ][ ... filling ]      records as the blocks held them
 segments_    begin 0, record 0, epoch 0 | begin 70 000, record 70 512, epoch 0 | ...    a gap or a new start
 epochs_      each start's time marks (record, time, period) and its shift      time = mark + records x period
 summaries    per channel: min, max, sum, sum of squares of every 256 (small) and every 4096 (large) records
```

- **In.** The window's `sync()` takes the engine's `FastBlock`s (22.3) and gives each to `ChartTab::appendFast`,
  then `ChartView::appendFast` (`Store::append`: a new start begins an epoch, records lost begin a segment) and
  `markFast` (`Store::mark`, the clock's mark of 13.9, about once a second). A stream's store is made by
  `setFastStream` (kept when the map loaded again has the same stream; a stream gone from the map takes its store
  with it, `removeFastStream`, so the same stream later starts afresh) and shared by its plotted channels
  (`Series::fast`); the window is the only writer, the chart's threads read it while they bin, between two writes.
  `sync()` appends blocks for `FAST_APPEND_NS` (8 ms) at most, then the frame goes on: after the window's thread was
  held (Windows holds it while a title bar's button is pressed, a dialog closed with its X) the first frame paid for
  the whole pile (700 ms of a million records a second, about 2.8 MB: the paint 30 to 70 ms late). `takeFastBlocks`
  still hands over the whole queue; the blocks not appended wait in the window (`fastRest_`, in their order) and go
  first at the next sync. The chart paints first, then takes the rest: the first sync after the thread was held (no
  sync for `FAST_HELD_MS`, 50 ms) appends none, so the frame it asks for shows the view moved on at once; and a sync
  that comes before the frame asked for at the one before was painted (Qt's paint request waits behind the events
  posted meanwhile, the frame clock's tick among them) appends none either: 8 ms more there only put that paint off
  (done 33 ms after the hold, not 25). A sync waits so once, never two in a row (`ChartView::paints`), so a chart
  that does not paint (hidden, over its `FrameBudget`) still takes the rest. That rest is held to `FAST_QUEUE_BYTES`
  too: past it a sync appends them all, whatever the time, so nothing is dropped in the window and the counts
  (taken, lost, not shown) stay the engine's. `fastSyncsLeftOver()` counts the syncs that left some (26.2, *fast
  speed*).
- **Trims.** `ChartView::trimFast`, after each block: past the Memory, or from a sixteenth short of the stream's
  share of the RAM down to seven eighths of it, `Store::dropFront` drops whole pieces from the front, and the
  summaries' arrays are copied to their new size. The store is at its new size at once, for every frame after. What
  it let go (the pieces, the summaries' old arrays: `Store::Released`) is not freed there but kept in `released_` and
  freed by `releaseSome` at each `frame()`, for at most `RELEASE_NS` (3 ms) a frame. Freed in one go, a RAM cut from
  4 GB filled to 512 MB held the window's thread about 2 s (3.5 GB let go; on the test machine 1 GB to 256 MB took 74 ms
  in one append); freed on another thread, the frees held the heap and the memory's pages while the chart's threads
  binned the memory strip, and the next paint took 30 to 50 ms. Now, 2 GB filled cut to 256 MB: the longest append
  4.6 ms, `frame()` 10.8 ms (one summary's array freed), the paint 10.8 ms (26.2, *the RAM cut*).
- **Times.** A record's time is its epoch's mark before it plus the records since times the mark's period; before
  the first mark, the first's backwards. A new start whose first time falls before the end of the one before is
  shifted after it (`Epoch::shift`): times never go back, so `lowerBound` / `upperBound` (a binary search on the
  segments by their last record's time, then the record from the mark, made exact by a step) hold.
- **Binning** (`binFast`, for the view and the memory strip alike): for each column, from the first record in it to
  the first of the next column (`lowerBound`) or the end of its segment, whichever comes first; the bin's min and max
  from `Store::minMax`, which reads the large summaries for whole runs of 4096, the small for 256, the records only
  at the two ends. A bin never spans a gap, and one that begins after it has `gap` set. The cost follows the columns,
  not the records: a view of an hour at a million records a second bins as fast as one of 100 µs.
- **Columns kept from frame to frame.** A column is a whole number of `columnSeconds` from time 0, so a live view,
  which moves by a column or two a frame, shares most of its columns with the frame before: `binFast` is given the
  last frame's `BinnedLine` of the same line and keeps its bins of whole columns that lie inside what this frame
  bins (never a frame's first or last bin, which may be part of a column), binning only the columns at the two
  ends. The kept bins hold their records' numbers, so they are dropped when a trim took their records, when the
  column width changed, or when the store's `timeVersion` moved (a clear, a new start, a start's shift: the times
  of records already kept can change then; a new mark changes only the times after it, so it does not count). On
  a 4K screen at 225 % with two lines of a million records a second this took the binning from 2.8 ms a frame to
  0.1 (the timing aid's `columns`: 2 to 3 a frame, 26.8). The memory strip is binned whole (`binSeries`), as its
  60 px see the whole memory anyway.
- **A held view filling, and the view a crossing waits for.** A view held after its crossing (Single, or a window
  of a second or more) stands while the records after T reach further columns: the frame before's bins of its
  complete columns are all kept, and a frame bins only the columns its new records reach, the first column and the
  open one at the data's end (a 2.5 s view filling at 60 frames a second: about 7 columns of 1 000 a line a frame;
  a polled line keeps its columns the same way, `binViewSeries`). A window under a second (23.10) holds a view only
  once it is full, so the next crossing's view fills behind the one shown, and the frame that showed it binned all
  of it (a whole view of each fast line every 100 ms at a 100 ms window: 1 134 columns a frame in the timing aid on
  a 4K laptop, and those frames came late). `binPending` bins the waiting view's fast lines at each frame while the
  view shown stands (its lines reused, 23.6), keeping its complete columns the same way, and `viewBins` hands those
  bins to the frame that shows it: that frame bins what came since the frame before (72 columns at most instead of
  1 069 in the GUI test). The work in all stays a view's columns a view, spread over the frames the view fills in.
  A polled line keeps one binning of its own (`Series::viewBins`), the view shown's, and is binned when its view is
  shown, as before. The timing aid's `columns` counts the memory strip's binning too (its whole width, once a
  second).
- **Drawing.** `toPolyline` makes the points as for any line and returns where the line breaks (`breaks`: a bin
  with `gap`); `strokePieces` strokes the pieces between them, a lone point as a dot. On a card the segments across
  a break are left out. A fast line is mostly bars (a column of a noisy signal covers its whole range): `fillBands`
  draws its bars snapped to whole device pixels with `fillRect` and no antialiasing (`crisp`), the same pixels on a
  stripe and on the window, under a millisecond a line where antialiased bars took 4.5.
- **Mapped** (a recording, 12.7). `appendMapped` leaves a block's records in the mapped file and keeps a span per
  block (`spans_`) and, per 256 records, the span of its first (`spanOfChunk_`): `recordAt` takes a step or two from
  there. Only the summaries and the lists are made; the store holds the file (`keep`), is not trimmed, and Clear
  leaves it; `bytes()` counts the lists, not the file.
- **Memory.** `trimFast` drops whole pieces past the Memory (5 % let grow first, as `dropExpired`) or past the
  stream's share of the RAM budget: `ramMB / lines x its plotted lines`, at `bytesPerRecord()` (the record's bytes
  and its summaries'). `bytesNeeded`, `bytesHeld` and `pointsKept` count the store; `memorySpan` and the smooth delay
  take a fast line's newest time. Summaries' arrays are squeezed after a trim.
- **Who reads a line's samples.** Every place that reads `times` or `values` has a fast branch: adding a line,
  Clear, the memory and its note, the trigger's arm time and its crossings' times, the view's bins key, binning, the folded
  lane's value, the crosshair, the legend's value, both drawing paths and the memory strip, the measurements and
  their key, `lineSamples` (the histogram and the spectrum) and `samples` (Export to CSV).
- **Measuring** (8.2). `statsOfFast` takes the range's records by `lowerBound` / `upperBound`, the min and max from
  `Store::minMax`, and for each segment in the range the trapezoids from `Store::sums`: dt x (sum - (first + last) /
  2), dt the segment's duration over its records less one; the same with the values shifted by the first (the
  standard deviation). `fastValueAt` gives a cursor's value straight between the two records around it, NaN in a gap.
  `startMeasure` computes a fast line's row on the window thread before the threads start (the store is written by
  that thread, and the summaries make it cheap: two lines of 10 million records in 0.05 ms). `measureKey` counts a
  fast line's records in the range as a polled line's.
- **Totals since Clear** (`sumFast`): when a block comes and when a time mark comes, the trapezoids from the line's
  last summed record (`Series::totalTo`, counted since the store began) to its newest with a time, before the trim.
- **The trigger** is the engine's (23.10, `fast::TriggerScan`): each block as it comes, each two records of one
  segment; `fastCrossings` takes a crossing's time from the store, straight between its two records.
- **Samples out.** `fastSamples` copies the records of a range into arrays (at most `MAX_POINTS`, the first), or with
  `withoutGap` the longest segment's part: the spectrum's, which takes them as they are (`analysis::spectrum(…, even)`;
  `Spectrum::resampled` false).
- **The time axis.** `MIN_WINDOW` is 10 µs. `timeLabel` writes microseconds for a grid step below a millisecond
  (five decimals for tens of µs), and `gridTicks` takes a larger step there while two labels would come closer than
  their width and 24 px; `drawGrid` leaves out a label that would be cut at the chart's edge.
- **Speed.** Two fast lines of a million records a second over a 10 s window: about 7 ms a frame on the GUI test's
  machine (bin 2.6, lines 3.3, the memory strip 0.5), the target 8 ms (26.2, *fast speed*).

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

### 24.7 A text and its translation

1. Every text a person reads goes through `tr()` (in a class with `Q_OBJECT`) or
   `QCoreApplication::translate("context", …)`; a table of texts uses `QT_TRANSLATE_NOOP` and `tr()` where it is shown
   (the Help's `TOPICS`). Units, symbols, register names and code are not translated. A count in a sentence takes
   `tr("%n device(s)", nullptr, n)` so that Arabic gets its plural forms.
2. `cmake --build build --target update_translations` runs `lupdate` over `src/`: the new or changed texts enter
   `translations/evre_studio_ar.ts` as unfinished.
3. Translate them in Qt Linguist or in the file: keep every `%1`, `%n`, `%CODE%`, `%NAME%` marker and HTML tag, give a
   numerus message its six Arabic forms (0, 1, 2, 3–10, 11–99, 100–102), and mark each finished. The GUI test fails on
   an unfinished or empty message and on one that loses a placeholder, a marker or a tag (26.2).
4. The build runs `lrelease` and puts the `.qm` into the program (`:/i18n`); nothing is installed beside it.

`translations/README.md` says the same for a translator.

## 25. Building

### 25.1 Requirements

| Item | Version |
|---|---|
| CMake | 3.21 or later |
| C++&nbsp;compiler | C++17. Tested with GCC (MinGW on Windows, GCC on Linux). Clang and MSVC are not tested; MSVC in particular defines `M_PI` and `M_E`, which `src/model/expr.cpp` uses, only with `_USE_MATH_DEFINES`, so it may need that define. MSVC keeps its own warning defaults. |
| Qt&nbsp;6 | 6.5 or later, components Widgets, Network, SerialPort, Test, and LinguistTools (Qt Tools: `lupdate`, `lrelease`) for the translations |
| Generator | Ninja (recommended) or any CMake generator |
| Windows&nbsp;only | the system libraries `winmm` (1 ms timer resolution) and `dwmapi` (`DwmFlush`), linked by the build |

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
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/EVReStudio
```

`CMakeLists.txt` asks only for Widgets, Network, SerialPort, Test and LinguistTools, which `qt6-base-dev`,
`qt6-serialport-dev` and `qt6-tools-dev` provide (`qt6-l10n-tools` holds `lupdate` and `lrelease`). The GUI test needs a display; on a machine without one, run it under a virtual X server such as `xvfb-run`. Use `xvfb-run` also when the test's window would share a desktop someone works on (WSLg on Windows): a click elsewhere takes the keyboard focus, a cell editor then closes and commits, and a step can stop at a question nobody answers.

A serial port needs read and write access for the user. On most distributions that means membership in the group
that owns `/dev/ttyACM*` and `/dev/ttyUSB*` (often `dialout`).

**Deployment.** There is no install target. Run the program from the build folder, or copy `EVReStudio` together
with `maps/` beside it. The target machine needs the same Qt 6 runtime packages; the release's AppImage (25.5) brings
its own.

### 25.4 CMake targets and options

| Target | Kind | Built&nbsp;from | Notes |
|---|---|---|---|
| `evre_protocol` | static&nbsp;library | `src/evre/*` (with `registers.h`), `src/model/device_map.*` and `src/model/bus_file.*` | Qt Core, Network, SerialPort only; the core, the probe and the fast fake device link it |
| `evre_studio_core` | static&nbsp;library | every other file under `src/` except `main.cpp` | links `evre_protocol`; the program and the GUI test link it; a new source file is one more line here (or in `evre_protocol`) |
| `evre_maps` | custom&nbsp;target | `maps/` | copies `maps/` beside the programs at every build, so an edited map is there even when nothing was linked again |
| `EVReStudio` | executable | `src/main.cpp`&nbsp;+&nbsp;core | depends on `evre_maps` |
| `evre_gui_test` | executable | `tests/gui_test.cpp` + core + Qt Test | depends on `evre_maps`: it opens the maps beside it |
| `evre_probe` | executable | `tests/evre_probe.cpp` + `evre_protocol` | Qt Core, Network, SerialPort only |
| `evre` | executable | `cli/evre.cpp`&nbsp;+&nbsp;`evre_protocol` | the command-line tool (chapter 34) |
| `evre-sim` | executable | `cli/evre_sim.cpp`&nbsp;+&nbsp;`evre_protocol` | the simulator (chapter 35) |
| `evre_map_test` | executable | `tests/map_test.cpp` + `evre_protocol` + Qt Test | depends on `evre_maps` |
| `evre_fake_fast` | executable | `tests/fake_device_fast.cpp` + `evre_protocol` | depends on `evre_maps`: without a map argument it opens the example map beside it |

The project defines no options of its own. The usual CMake variables apply: `CMAKE_BUILD_TYPE`, `CMAKE_PREFIX_PATH`
(where Qt is) and the generator. The version comes from `project(EVReStudio VERSION 1.0.0)`. It reaches the code as
`EVRE_STUDIO_VERSION`, is shown in the sidebar's footer, and is printed by `--version`. AUTOMOC is on.

The Studio's icon is the teknile mark (`packaging/icons/evre-studio.svg`, made from the logo's mark on its dark rounded
square; the PNGs of 16 to 256 px and `evre-studio.ico` are drawn from it). The PNGs are in the core's resources at
`:/icons` (`studioIcon()`, every window's); on Windows `packaging/windows/evre_studio.rc.in` becomes the executable's
icon and version (`configure_file`, `enable_language(RC)`).

### 25.5 Installers and releases

`.github/workflows/release.yml` makes the files a release offers. It runs on a tag `vX.Y.Z`, on a pull request that
touches it or `packaging/`, and by hand (`workflow_dispatch`); never on other pushes.

| Job | What it does |
|---|---|
| `version` | The version from `project(VERSION)`. On a tag: stops with a message unless the tag is that version and `EVRe/CHANGELOG.md` has a section `## X.Y.Z`. |
| `windows` | Qt 6.8.3 MinGW (as `ci.yml`): `EVReStudio`, `evre`, `evre-sim` built; `windeployqt` with the compiler runtime and the translations, `qtbase_ar.qm` too; the installer (`packaging/windows/evre_studio.iss`, Inno Setup 6: per user, no administrator, Start menu, optional desktop icon, uninstaller) and the portable zip of the same folder. |
| `linux` | On Ubuntu 22.04 (an older glibc: it runs on more systems): the AppImage with `linuxdeploy` and its Qt plugin (`packaging/linux/evre-studio.desktop`, the icons), started under xvfb with `EVRE_SHOT` to check it pictures its window; `evre` and `evre-sim` with their Qt libraries in a tarball, `evre validate` run from it. |
| `release` | On a tag only: a GitHub release with the files and the changelog's section as its text, and a note that the installer is not code-signed. |

On a pull request the files are the run's artifacts, nothing is published. Making a release: `EVRe/CONTRIBUTING.md`.

## 26. Tests

### 26.1 Overview

| Program | Covers | Needs | Writes? |
|---|---|---|---|
| `evre_gui_test` | the&nbsp;real&nbsp;window,&nbsp;end&nbsp;to&nbsp;end | `tests/fake_device.py` on 127.0.0.1:1210 | yes, a danger register included |
| `tests/api_test.py` | the&nbsp;API&nbsp;server,&nbsp;four&nbsp;modes | the Studio connected to the fake device, API on; the mode `fast` starts its own Studio and `evre_fake_fast` | yes, in the modes `writes` and `danger` |
| `evre_probe` | the protocol core against a real device | a&nbsp;device&nbsp;or&nbsp;gateway | no register values; only the login, if asked |
| `tests/fake_device.py` | a fake device for the tests and for trying the Studio | Python&nbsp;3,&nbsp;standard&nbsp;library | serves writes |
| `evre_fake_fast` | a fast fake device, to measure the Studio itself | built&nbsp;with&nbsp;the&nbsp;project | serves writes |
| `tests/fake_login_test.py` | the login of both fake devices, and the probe's | the build folder (`evre_fake_fast`, `evre_probe`) | only to the fake devices' login register |
| `evre_map_test` | the map files, the exports (26.7) | nothing; `gcc` and `python` on PATH compile and import the exports | only its own temporary folder |
| `tests/cli_test.py` | `evre`: validate, export, info, read, dump, watch, write, the token, the refusals; `--bus` and `broadcast` on two devices; a map's streams in `info` and `validate`, `record` (34.2), `check --writes` on a stream (34.1) | the build folder; it starts `fake_device.py` on 1212, `evre_fake_fast` as two devices on 1232 and with the fast map on 1238, itself | yes, to its own fake devices |
| `tests/sim_test.py` | `evre-sim`: defaults, moving values, wo, ro, action, w1c, ro fields, strict, login, persist, a fast stream (its enable, rate register, `evre record`, watchdog, test aids) | the build folder (`evre-sim`, `evre`); it starts the simulator on 1213 itself | yes, to its own simulator |
| `tests/schema_test.py` | the maps against the JSON Schema; a stream's keys and refusals; MAP_FORMAT.md's stream keys against the schema (26.7) | the `jsonschema` package (SKIP without it) | no |
| `evre_fast_test` | Fast EVRe without a window (26.9): the block's rules, a fuzz, the clock's fit, the fake devices' source | nothing | no |
| `tests/fast_lib_test.py` | the device's helper `lib/fast` (26.9) | the build folder (`evre_fast_test`); `g++` and the library (`--lib`, `EVRE_LIB`, or `../lib`), SKIP without | only its own temporary folder |
| `tests/device_table_test.py` | the device table export (32.6) compiled with the EVRe library and run; the refusals | the build folder (`evre`); `g++` and the library (`--lib`, `EVRE_LIB`, or `../lib` in the EVRe repository) for the compile part, SKIP without | only its own temporary folder |

**The rule: the GUI test and the API test write only to a fake device.** They write registers of the device bank
and set a register marked danger. Never point them at a real device. `evre_probe` is the only test program meant for
real hardware, and it reads only.

Settings stay apart from the user's:

- The GUI test runs as organisation `teknile`, application `EVReStudioTest`.
- A run with `EVRE_SHOT` set uses the application name `EVReStudio-test`.

A script that runs the tests should stop only the processes it started, by their process id, never by name or
window title. A user may have the Studio open at the same time. The tests use the fixed ports 1210, 1211 (the fake devices'
login test), 1219 and 1220, and the GUI test's own 1226 (a bus), 1236 (auto send) and 1240 (fast streams), so only one
test run can be active on a machine at a time.

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
| u8&nbsp;with&nbsp;names | the same, with `enum` values; one of them other than 0 | LED_MODE |
| danger | the first writable 16-bit danger register at 0xD000 or above | MOTOR_SPEED |
| volts | the&nbsp;first&nbsp;read-only&nbsp;f32&nbsp;in&nbsp;V | SUPPLY_V |
| amps | the&nbsp;first&nbsp;read-only&nbsp;f32&nbsp;in&nbsp;A | SUPPLY_I |
| flag | the protocol's CONFIG (0xA004), bit MSG_ENABLE (bit 2) | CONFIG |
| group&nbsp;with&nbsp;`&` | the first group with an `&` in its name | Power & supply |
| login | the&nbsp;map's&nbsp;`"login"` | 0xF000, 16 bytes |

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
11. "Show in Log" ends the pop-up, even after a resize; in right-to-left (Arabic, `noticeRightToLeft`) the pop-up
    sits left of the tabs, at 1200×720, 1600×950 and the window's size, and right of them again in left to right,
    never over the tab bar, the page or the sidebar (with `EVRE_TEST_SHOT` set: `<prefix>_notice_ar.png`)
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

Four more steps cover several devices on one link (3.9, 3.10), auto send (13.8) and fast streams (13.9):

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
- **Fast streams** (`fastStreams`, after the bus step). The example map has no stream: no card. It starts
  `evre_fake_fast` with `maps/example_fast.json` on port 1240 and loads that map: the card shows a row for `ADC`,
  greyed with *not connected*; connected, the button is offered (a pointing hand, a tooltip naming what it writes)
  and the card says *off · 10.0 k samples/s*. Start writes 1 to `ADC_STREAM` on the device, the button turns red
  (*■ Stop stream*), the card shows 9.9 to 10.1 k samples/s with its correction in ppm and *lost 0*, and the Log says
  so; the row is headed by the stream's name with the map's description as its tooltip, and the card sits before
  *Polling & recording*;
  with Poll off 24 to 45 requests go out in 3 s (CONFIG every 100 ms) and after those 3 s the stream still runs (the
  fake device's 2 s watchdog never stops it); with *Log frames* the Monitor names a block *READ_RESP (fast stream
  ADC)*; Stop writes 0. On again, then Disconnect: 0 written before the link closes, the button Start, greyed.
  `--fast adc,NOPE` (the command line): the stream on once connected, the button Stop, *NOPE* said in the Log. On
  again, the device killed and started again losing every 5th block (`--fast-lose 5`): after the reconnect the
  stream is on again by itself and the card counts the samples lost, in amber. A device that takes the enable and
  never sends (the same map without its stream): off again after 2 s (0 written), *no block came in 2 s* in the
  Log. A bus of two such devices: the card greyed, *not on a bus*. The card's button and numbers fit the sidebar in
  English and Arabic at 1.23 M samples/s and *lost 123 456 789*; in Arabic both number lines are laid out right to
  left, like the card's title, and each number keeps its prefix, its unit and its groups left to right (*10.0 k*,
  not *k 10.0*; asked of the text's layout). The window then loads the example map again (no
  card) and connects back to the Python fake device; the chart keeps no store of the stream (`fastStore(0)`: none), so
  the same stream loaded again starts afresh. With the stream on, the Plot tick of `ADC.I_LOAD` (a pointing
  hand, a tooltip naming it) puts its line on the chart: its records kept, its newest value in amperes beside the
  tick, *· 1 fast* in the chart's info line. One cap of 64 lines (`chartOneCap`): the chart filled with math lines
  (fields of ADC_STREAM), the tick of `ADC.V_BUS` is taken back with the same words in the status bar and the info
  line reads *64/64 plotted · N math · 1 fast*; the math lines removed, the tick is taken. Measured: its row in the Measure table reads the device's 50 Hz sine of
  6.55 A at about 4.6 A RMS; the trigger's line list offers it (its tooltip names fast lines); armed from its chip's
  menu at 0 A rising, the engine finds a crossing as the blocks come and the view holds at its time, between its two
  records (the test says how long after the crossing it held); its chip's menu offers
  Histogram and Spectrum, and the spectrum takes its samples as they are (*evenly spaced*, the rate written whole, no
  *e+*), its peak within one step of 50 Hz. Recorded (12.7): **Record CSV** for 1.5 s writes `fast.ADC.evrs` beside
  `fast.csv` and the Log says so; the recording opens with it, a fast line of its samples (about 15 000, none lost)
  equal to those the live chart took at the same times, and its window's own fast math line (`recording/math`,
  `ADC.I_LOAD * ADC.V_BUS`) computed from them record by record; the `.evrs` alone opens on the same wall clock (within 50 ms)
  and says its samples; a copy cut 3 bytes into its last piece opens and says it was cut off. The trigger armed on
  the line in Normal (from its chip's menu, 0 A rising) through Disconnect, Connect, Arm and the stream stopped and
  started: the engine watches what the window asks (the same channel and arm, asked on its thread with
  `MainWindow::engineFastWatch`) and, after an Arm, the view holds again; the line removed while armed, the engine watches
  nothing; no crash. The tick off takes the line off.
- **Fast speed** (`fastSpeed`, after the fast streams step). `evre_fake_fast` with `--fast-rate 1000000` on port
  1240: both channels plotted over a 10 s window, the Chart tab shown, the stream on for 20 s. About 20 million
  records taken, none lost, no bad block, none left unshown (the rate's tooltip), the rate 0.95 to 1.04 M samples/s;
  and over the last 10 s the chart's paint (the timing aid's, 26.8) at most 8 ms a frame on average. Then the
  window's thread held 700 ms (`QThread::msleep`, as Windows holds it while a title bar's button is pressed): the
  chart paints again within two frames, no paint after it over 40 ms, and at least 6 frames in each 200 ms slot
  after the first; the blocks that piled up were appended over more than one frame (a sync left some for the next,
  `fastSyncsLeftOver`), none lost and none left unshown (it prints the first paint's delay, the frames and longest
  paint of five slots, and the syncs that left blocks). Then a fast math line `ADC.I_LOAD * ADC.V_BUS` for 10 s:
  every record of the stream computed, the paint at most 8 ms a frame on average, its cost (`fastMathNs`) under a
  quarter of the window's thread (it prints the ms a second, 9.9). Then the example map again, connected to the
  Python fake device.

Fast lines without a device (`chartFastLines`, after the chart's many-lines steps), a chart of its own fed records
as the window feeds it:

- **A spike** of one record in 10 million (1000 s at 10 kHz) shows at every window from all of it to 1 ms, in its
  column's max and in Auto's range.
- **Columns kept**: a 10 s view moved by 2.5 columns bins only the columns at its ends (at most 12 for two lines
  of 1000; `fastColumnsBinned`), and its bins equal those of a binning from nothing (`freshBins`).
- **Single records**: in a view of 1 ms each record is a bin of its own at its own time.
- **Below a millisecond**: a view of 50 µs (the shortest is 10 µs) labelled in microseconds, the labels different;
  a view of 2 ms: its labels whole, at least their width and 20 px apart.
- **A gap**: 500 records lost in the middle of 200 ms: the bin after them says so, no pixel of the line in the gap,
  and the mouse over it reads *ADC.I_LOAD: 500 sample(s) lost here* (nothing beside it).
- **The RAM shared**: 256 MB for a polled line and a fast one of 32 `i32` channels: the fast line's store trimmed to
  its half (the memory full), the polled line's share the other half. The strip then says *RAM budget reached:
  keeping the last … of 1.0 h* in the warn colour (pixels of it on the picture), dark and light, with *· the recording
  keeps everything* while a recording runs; its tooltip says what the budget does. With `EVRE_TEST_SHOT` set it saves
  `<prefix>_ram_budget_<dark|light>_<idle|recording>.png`.
- **With the rest**: lanes by unit beside a polled line, the legend with the newest values, the crosshair.
- **On a card** (Windows): the card's picture of two fast lines like the CPU's (93 % of its 24 px blocks alike; skipped
  where there is none).

The Map settings' Streams page (`mapStreamsPage`, after the Map editor's step), a dialog of its own on the fast
example: the stream ADC listed with its window `0xDC00`, its 2 channels, *A sample: 4 bytes · at most 254 samples a
block*, no check failing, its buttons with a pointing hand and a tooltip; in English and Arabic at the dialog's least
size every label, button and column header whole; **+ Stream** gives `S2` and *stream S2: it has no channel*, **+
Channel** takes that away, its window over `0xD000` is told at once (*shares bytes with the register UPTIME*);
OK puts two streams into the map as one undo step, undone one again. A math line `SUPPLY_V * adc.i_load` reads *OK:
reads SUPPLY_V, adc.i_load · computed for every record of stream ADC* (9.9), `ADC.I_LOAD * PWR.P_IN` is refused with
*… two streams, two clocks: not in this version* (9.5), `SUPPLY_V * 2` reads `OK`, and `I_LO` typed offers
`ADC.I_LOAD`.

Fast lines measured (`chartFastMeasure`, after `chartFastLines`), records of 10 kHz fed as the window feeds them:

- **Between the cursors**, over 2 s with 0.1 s lost in the middle: min, max, mean, RMS, std (12 V with 3 mV of
  ripple), peak to peak, the area and the values at A and B equal a plain loop over the records (within 1e-6; the
  std within 0.1 %), nothing across the gap; a cursor in the gap reads nothing.
- **Within a frame**: two lines of 10 million records each measured in less than 16 ms (0.05 ms here).
- **Totals since Clear**: 10 s with a gap, a Memory of 1 s: the total equals the plain loop over every record fed,
  the gap not bridged, since the first record; after Clear it starts again.
- **The trigger**, found as the engine finds it (`fast::TriggerScan` on each block, the crossings handed to the view
  with it): Rising through 0.5 A after 0 A, 50 records lost and then 1 A: no crossing across the gap; the step later
  fires half way between its two records.
- **The export**: 1000 records of two channels are 1000 rows, the values the records'.
- **Spectrum and histogram** in a chart tab: over a range with a gap the spectrum takes its longest part without one
  and the window's title says *1.98 s: 1.39 s of it without a gap*; the histogram counts every record.

Fast math lines (`chartFastMath`, after `chartFastMeasure`, 9.9), chart tabs of their own (settings group
`fastMathTest`) fed blocks as the window feeds them:

- **The parser**: `ADC.I_LOAD * ADC.V_BUS` is a fast line of stream 0, `adc.i_load * SUPPLY_V` too with the register
  as an input, `SUPPLY_V * 2` is not; `ADC.I_LOAD * PWR.P_IN` is refused with *two streams, two clocks: not in this
  version*.
- **The product**: 3000 records, the line's record by record (every 7th, 429) at the stream's times and equal to the
  product of the two channels.
- **A register held**: `ADC.I_LOAD * SUPPLY_V`, no record before SUPPLY_V's first poll, then each record times the
  last polled value (12.5, then 13.0).
- **No number**: `sqrt(ADC.I_LOAD)` leaves out the records of a negative current, gaps there.
- **Drawn**: its bins' min and max are its records'; named *ƒ P*.
- **Measured**: Measure's min, max and mean between the cursors equal a plain loop over its records.
- **Exported**: 1000 records are 1000 rows at the channels' times.
- **Triggered**: Rising through 6 W on a step from 0 to 12 W fires between records 299 and 300, found as its records
  are made; the engine is told to watch no stream.
- **A recording's chart**: `fillFastMath` from a stream's store, SUPPLY_V held by time (before the first sample: the
  first).
- **Its RAM**: 16 lines on a 256 MB budget, 6.5 million records: its store trimmed to its sixteenth; the stream's
  store keeps none while none of its channels is plotted.
- **The cap**: with 63 lines a fast math line is the 64th (a math line in the info line), a second is refused.

Phase-two steps, before the Map editor's: the recording format and a recording window.

- **Recording files** (`recordingFiles`): three lines written (two a millisecond apart share 10 rows, a lone sample
  has its own: 11 rows), the header `time_s,datetime,VOLTS [V],AMPS [A],SLOW; X` and the first row exactly, read back
  the same (its time base's zero from the first row); a column of hex left out; the estimate from the head and the
  tail within 5 % of the rows; a read from the middle keeps the last part; the notes saved and read back, none
  left: no file.
- **The right-click** (`chartMenuAndPictures`): its six items in order; Save picture's PNG and the clipboard's
  picture the chart's size in pixels.
- **Export** (`chartExport`): the view (1000 rows, two lines of a poll in each, the first row's time and values),
  A → B with a note inside and one outside (only the inside one beside it), the file first in Recent recordings; 40
  lines of 150 000 samples: the call returns at once, a second export is refused meanwhile, the progress dialog
  shows, its Cancel ends it and removes the file.
- **Notes** (`chartNotes`): Add note here answered, its tag at the time clicked, at the bottom of the plot; dragged
  100 px it moves to that time; a double-click edits it; clicked and Delete removes it.
- **Dialogs without the window animations** (27): the Add note dialog, the lane's Manual… dialog, the export's
  progress and the Math line dialog are made with `noWindowAnimation` (their `noAnimation` property); *New math
  line…* from the Math menu opens the dialog, and closed as by its X it goes, the Math button as before
  (`formulaCompletion`).
- **Recording windows** (`recordingWindows`, the window connected to the fake device): a recording with the map's
  SUPPLY_V, SUPPLY_I, LED_MODE, MSG_BUFFER and CONFIG and an UNKNOWN column with every tenth cell empty, a note beside
  it and a math line in `recording/math`: titled with its name and span, held on 59.9 s without Hold; five lines
  (MSG_BUFFER left out), LED_MODE with its value names, UNKNOWN made from its title with 540 samples; the math line
  over all 600 rows; the note shown; a field of CONFIG from the Lines menu plotted from the file; a note added saved
  beside it; a second dropped on the window while the live chart goes on; RAM 0: the question, the last part kept
  (nothing), Cancel opens nothing; the recent list holds the last 8; **Open** beside Record CSV; a note added while
  recording is written beside the recording at once and shown when it is opened. With `EVRE_TEST_SHOT` set it saves
  `<prefix>_recording.png`.
- **The recording's window revisited** (`recordingViewer`, in the recording windows' step): opened with Measure on,
  the file's 600 samples on the chart and the legend's value at once, and the Y boxes showing its first frame's range
  under 500 ms (not 0 and 1); a change right after a feed's frames painted though no frame follows; the Y row packed
  (its labels and list no wider than they need); made bigger on the CPU, the window's own pixels its picture at once;
  on a card (Windows), the frame of the window made bigger let go as if the system were busy (`dropNextGpuFrame`),
  then drawn again, the layer at the new size and the screen's plot the CPU's picture (97 % of the blocks); the
  theme switched, its boxes in the new theme at once. With `EVRE_TEST_SHOT` set the recording windows' step and the
  fast streams' save `<prefix>_viewer_regs_*.png` and `<prefix>_viewer_fast_*.png` (dark and light, Lanes, measured,
  smaller and bigger, Arabic) for a look.

**Lanes** (`chartLanes`, on a Chart tab of its own): eight lines of four units (two each, V around 12, A around
0.5, W around 6, none around 100): four lanes in that order, of equal height, stacked, each with its own Auto range
and a value placed inside its lane, saved, the Y range row disabled; a right-click on the second lane's labels: its
title and Auto, Manual…, Log, Fold lane; Manual 0 .. 5 typed, Log on the
third (equal decades), each lane alone, kept and found again by a new tab; Ctrl + wheel over the first changes it
alone, a double-click sets it back to Auto; the first lane Manual 11.9 .. 12.1: no pixel of its lines between the
lanes; the A-B bar over the first lane and a note's tag at the bottom of the last; the lanes drawn on threads as on
one (the same pixels within a row's worth); Lanes off: one plot, the Y row back. With `EVRE_TEST_SHOT` set it saves
`<prefix>_lanes.png`.

**Lanes that fit** (`chartLanesFit`, on a Chart tab of its own settings, `lanesFit`): every numeric register of the
map plotted, and two math lines over its volts and amps in W and Ω: ten lanes, none shared, each at least 80 px,
taller than the plot, the scroll bar shown; the wheel over the value labels scrolls by a step, held at the top and at
the bottom (the last lane's bottom on the plot's), the wheel over the plot still zooms the time; the first lane
scrolled out of view found at no height of the plot; the bar's handle dragged half its travel scrolls half way, a
click under it one plot height; a click on the volts lane's unit name folds it (22 px, the others still open, the
strip's text its unit, then SUPPLY_V and the legend's value), a click on the strip with cursor mode on opens it and
places no cursor; the fold saved (`lanesFolded`), kept across Lanes off and on and found by a new tab; with four lanes
nothing scrolls and no bar shows, Fold lane in the lane's menu makes the others taller, Open lane from a right-click
on the strip brings them back. With `EVRE_TEST_SHOT` set it saves `<prefix>_lanes_fit.png` (scrolled half a lane,
one folded). On Windows the card's picture is compared with the CPU's as for two lanes, with eight more units
scrolled half a lane and the third folded.

**The fold made visible** (`chartLanesFoldButton`, settings `lanesButton`, the same ten lanes): a button at the top of
every lane's unit column in view (16 px, none for a lane out of view); a click on it folds the lane, one on the strip's
▸ opens it; over a button the pointing hand, the button's pixels changed, the hovered lane, the tooltip *Fold lane*
shown by a tooltip event, *Open lane* on a strip and its ▸, no highlight once the mouse is over the plot; nothing
about Lanes in the state corner; Display's Fold all lanes and Open all lanes: shown with Lanes on, Fold all enabled and
Open all not, then every lane folded (saved, the ten strips fit) and the other way round, both hidden with Lanes off;
the value labels' tooltip with the wheel's part while the lanes scroll and without it with four lanes; a button's
shape at rest (not the background); over the scroll bar the pointing hand, its handle's pixels changed and its
tooltip, none once the mouse is over the plot. The lane's menu button: under the fold button of every open lane
wholly in view (16 px, 2 px below it), none on a folded strip, a button's shape at rest; a click on it shows the
lane's menu (Auto, Manual…, Log, Fold lane, nothing else) with its top at the button's bottom, the click itself
folding nothing, and its Fold lane folding the lane; over it the pointing hand, its pixels changed, it hovered and
not the fold button, the tooltip *Y range and lane options*, no highlight once the mouse is over the plot. With
`EVRE_TEST_SHOT` set it saves `<prefix>_lane_menu_light.png` and `_lane_menu_dark.png` (the ⋯ of the third lane
under the mouse).

**Separators** (`chartLanesSeparators`, settings `lanesSeparators`): with ten lanes, one folded, and scrolled half a
lane, a separator for each gap whose middle is in the plot, at that middle, in the `control` colour at the value
labels, at the plot's left edge and in its middle; none with Lanes off. No text cut: scrolled so the first lane is cut
by the plot's top, every value label's box (`valueLabelRects`) whole inside its lane's part in view; a folded strip
half above the plot writes nothing (`foldedText` empty), whole it writes its lines.

**A lane's border** (`chartLaneBorders`, settings `laneBorders`, four lanes that fit): over the first separator the
resize cursor, the hovered separator, the line in the accent colour beside the mouse, the tooltip; off it none of
them; dragged 30 px down the first lane 30 px taller and the second 30 px lower, the others the same, nothing to scroll,
the two weights saved; dragged far down the second held at 80 px, far up the first at 80 px; weights of 6, 0.3, 0.3 and 0.3 (the
shares of the last three under 80 px) hold those at 80 and give the first the rest, every lane in the plot, no scroll
bar, and a tab too short for 80 px each puts all at 80 with the scroll bar; a new tab of the same
settings finds the first lane's height; a double-click on a separator makes every lane equal and the setting empty.
On Windows the card's picture is compared with the CPU's with two lanes resized.

**The state corner fits** (`chartStateFits`, a chart of its own with eight lines): with the view held, Log Y with a
manual range and cursor mode, the chart is narrowed from 1700 px in 10 px steps down to 760 px (below its
width at the main window's narrowest, which it prints). At every width the state's room and the legend's row (its
arrows included) do not overlap, the state starts after the row ends, and it is at most 40 % of the chart unless it
is the shortest form. Its texts come in order: the whole, without *Live to follow*, with *cursors* alone, with *Y log*
alone; at 1700 px nothing is dropped; at the narrowest *click / drag* at least is dropped; its tooltip is the whole
text. All of it
again in Arabic. With `EVRE_TEST_SHOT` set it saves `<prefix>_state_en_light.png`, `_state_en_dark.png`,
`_state_ar_light.png` and `_state_ar_dark.png` at the main window's narrowest.

**A held view's lines reused** (`heldViewReuse`, 16 lines of 1000 Hz, the view held over them): cursor A dragged 20
steps bins nothing and draws no line again; a sample in the view, another window or another size bins again; a
Manual Y range or Normalise draws the lines again; the picture with the lines reused is the one drawn without the
reuse, its antialiased edges within 2 of 255.

**The measure table while a cursor is dragged** (`measureTableRepaints`, Measure on, a held view): eight steps of a
dragged cursor, the table's paints at most one per update; the line over the table written at most once while the
cursor moves (its text the same), and not at all for four updates with the same text.

**Measurements on the chart's threads** (`measureInBackground`, 8 lines of 1000 Hz, Measure on, the view held over
them): a sample in the range measures all of it once more, 2 s of samples after the view's end (the clock going on)
none; cursor A dragged for a second starts no full measurement, its release one; the table's min, max, mean and RMS
those `stats(keys)` gives on the window thread, and the window thread waited for no full measurement
(`fullStatsOnWindowThread` unchanged).

**The timing aid** (`perfLog`): with `EVRE_PERF_LOG` set, a Chart tab fed frames for 1.3 s writes at least two
lines, each with its group, `fps`, `paint`, `max`, the stages, `binned`, `measure`, `threads` and `polls` (26.8).

**The icon** (`studioIcon`): the application's icon has every size from 16 to 256 px, is a rounded square (its corner
clear, its middle not), and is the main window's and the Help's.

**Analysis and trigger** (`analysisMath`, `analysisWindows`, `chartTrigger`): the FFT of an impulse is flat, a sine of 8
periods in 256 lands in bin 8 with N/2, and comes back; 0 .. 999 makes 10 bins of 99.9, one value one bin, few levels
√n bins; a sine of 2 V at 62.5 Hz polled 1 ms ± 0.3 ms apart reads 2 V (± 0.05) at 62.5 Hz with 8 segments of 2048,
its mean of 1 V at 0 Hz, up to half the rate, and 10 samples give none. A right-click on a line's chip offers its
Histogram and Spectrum; the histogram's window over the view with its readout; the spectrum over A → B of 1 s: its
title, its peak (62.5 Hz at 3 V) in the line above the plot and under the mouse, its CSV (a row a frequency) and its
picture. The trigger on a 1 Hz sine at 1 kHz, level 0.5: armed, it holds at the crossing at 100 + 1/12 s within 1 µs,
the view from 0.2 s before it to 0.8 s after, its marker at 20 % of the plot, the measurements over it; Normal (a
hold-off of 0.9 s) is armed again once the view is full and the hold-off has passed, and holds on the next period's;
Single falling holds at 102 + 5/12 and stays; Arm,
Either: the next crossing; the level's line dragged to 0.8 moves the level, its box and its setting; off: the row
hidden. With `EVRE_TEST_SHOT` set it saves `<prefix>_histogram.png`, `<prefix>_spectrum.png` and `<prefix>_trigger.png`.

**Trigger v2** (`chartTriggerLines`, `chartTriggerModes`, `chartTriggerSteady`, `chartTriggerFast`, `chartTriggerAuto`,
`chartTriggerRunStop`, `chartTriggerReview`, `chartTriggerLook`, `chartTriggerFlow`, `chartTriggerScopes`, on Chart tabs of their own). Per line:
two lines of other units; a line never set starts at its mid-range, rising; a right-click on its chip offers *Trigger
on this line*, which turns the trigger on, armed on it, the row showing the same; each line keeps its level and edge
(another chosen starts at its own, the first's come back), saved by name and read back by a new tab; the level's tag
in a margin right of the plot at its line's height (*0.6 A ↓*, its tooltip *AMPS 0.6 A, falling*), dragged by it,
taken off its middle, without lanes and in its own lane's band with Lanes on: the line follows the mouse's move,
within a pixel (the row and the setting follow); a pointing hand, the tab lit and its tooltip over it, its arrow lit
under the mouse, a click takes the next edge; a level
dragged keeps all its digits through a change of the mode in the row; the hold-off and the place read back by a new
tab, the one level saved before per-line levels taken over by its line, not over levels saved since. Modes: Auto
runs live (*Auto · free running*), holds on a step (*Auto · triggered*), is ready after the
hold-off (still held, still *Auto · triggered*), runs live a window's length later and holds on the next
step; the crossing's place (20 % saved for these steps): the flag above the plot at 20 % (a hand, a tooltip), dragged to 50 % the held view moves with it,
clamped to 90 % and 0 %, the row's box follows and moves it; at 90 % with the level above the plot the level's tab
lies right of the plot, off the crossing, and the flag right over it; the hold-off: a 1 kHz sine in a 10 ms window holds 40 to
51 times in 0.5 s (once a window, not once a cycle), and with a hold-off of 0 and the crossing at 90 % at least 200.
A steady picture: the same sine fed as at 60 frames a second, 51 holds: every picture held after the first is whole;
the re-trigger's binning: 50 re-triggers with no hold-off and the crossing at 90 %, fed 1 ms a frame, bin about the
new samples' columns (at most twice the samples fed and 4 a frame polled, 1.2 times and 4 a frame fast, at least
0.8 of the columns the view moved by), not the whole view. On a fast line, the engine played by a `fast::TriggerScan`
given each watch posted: the level dragged in ten moves posts once, at the next frame, and Single holds at the new
level before the release; the place dragged posts once a frame and only when it moved; Clear with Single armed (a
crossing at a block's first record, its record before cleared) and the next crossing holds; a new start 20 ms early
(the store shifts it) and a step 7 ms into it holds; the wheel's zoom posts the new re-arm and the next crossing counts
after the new fill; the scan alone: a crossing dropped with its block arms Single again, the blocks waiting scanned
again for a new watch. Auto (`chartTriggerAuto`): samples 50 ms late, a 1 kHz sine in a 10 ms window stays held for
60 frames; Stop drops a crossing waiting for its view and keeps the view and its T through 30 crossings, Run's next
hold comes after Run; Stop and a pan in Auto stay for 3 s with no crossing, Run runs live. Run and Stop
(`chartTriggerRunStop`): in each mode the toolbar's button is Stop while the trigger runs and Run when not; Stop
keeps the view and its T through crossings, Run arms from now (Auto live, Normal and Single still); a pan is a Stop;
Normal waiting on a level the sine never reaches keeps the same view for 1.5 s; the row's and the corner's texts in
every state exactly as the table in 7.13; while a view fills after its crossing the now edge is at the newest
sample's x, its column drawn in both themes, one in each of two lanes with Lanes on, none once full; the button
hidden in Auto, Force while Normal waits, shown in Single and primary while Single holds; after Run in Normal the row
*Normal · waiting, last at* the crossing's time; 50 re-triggers of a 1 kHz sine (one every 3 ms)
leave the row and the corner (both *Normal · triggered*, no rate) unchanged; the trigger off gives Hold /
Live back. The review's fixes (`chartTriggerReview`): Run after a Stop with a 10 s hold-off counts the first crossing
at once; the row's edge and
mode changed while stopped keep the stop, the view and its T, and Run arms Auto live; a wheel zoom over a view still
capturing after T keeps it held (Normal armed and capturing, Single complete and capturing); a press with a 1 px
jitter on a capturing view stops nothing; a press on an off-scale level's tab with a jitter keeps 2 V, a drag
moves it to 0.6; cursor A on the now edge's column is on top of it in at least 45 % of its rows, on the CPU and on
the card alike. What the eye sees (`chartTriggerLook`): the line's unit after the level's box and a level written alike in
the box and the tag (0.523456789 as 0.523457); the row's *position* label and its tooltip, no *at*; a level of 10 A
on a 0.25 to 0.75 A line pinned to the plot's top edge, dotted (runs of the line's colour about 1.6 px long against
6.3 for the dashes), its tab *T 10 A ↑* at the plot's top, its tooltip *▲ LOOK 10 A, rising (above range)*, the row
*waiting: level above the line's range*, the Auto range as before; -10 A at the bottom edge with ▼, dragged back by
its tab into the range; the tab labelled at rest, right of the plot, at least 2.5 times the old 32 x 18 px handle,
lit under the mouse with a pointing hand and its tooltip, the same size while dragged (the dragged level as set),
unlit after; its pointer hollow before a crossing, solid on the level crossed, hollow after a change, solid at the
next crossing (its middle pixel the line's colour or the surface); no T on the curve: over the crossing the level's
line drags (lit, a vertical arrow), and the flag over it has the tooltip *Trigger point: LOOK crossed 0.6 A, rising,
at ...* first;
the tab's least width the same while waiting, off scale and after Single's crossing, in English and in Arabic, the
state cut with its whole text in the tooltip. On a fast line (`fastStreams`) the steps after Connect arm Single for the next crossing (Arm is
Single's: in Normal it keeps the last crossing's hold-off, the 30 s window there). The Help's Chart page says how
the trigger works (*Trigger on this line*, the modes, Run and Stop, the hold-off, the marker and the tab, a level off
scale, the flag over the crossing, the short windows' lock). On Windows the card's picture of the level's line at the plot's right end
(where its handle sat before the tab moved out of the plot) is compared with the CPU's (93 % of the 24 px blocks, the
lines under it included), and of the dashed line's 8 px strip from the plot's left to there, at 11.5 where only two
of the 12 lines reach; the tab lies right of the card's layer. The language step measures the main window's minimum width with the trigger's row
shown too. A recording window offers no trigger (no chip menu entry, no Display entry). The trigger's flow: Off
at the row's end and the chip's checkable entry turn it off as Display -> Trigger does; the line watched removed
stops it (*no line to watch*, the line saved kept) and it arms again on that line when it is back; each chip's ▾
(at its right end, lit under the mouse with a pointing hand and its tooltip; a click opens the menu); in Arabic
the times, durations, sizes, fps and ms, the fill time and the Help's *10 s* and *2 V* are pieces between U+2066
and U+2069, English unchanged. With `EVRE_TEST_SHOT` set it saves `<prefix>_trigger_lane.png` (the tab in its lane) and
`<prefix>_trigger_marks_dark.png` / `_light.png` (the flag, the marker and the tab), `_trigger_marks_lanes_*` the
same with Lanes on, and `_trigger_lane_dark.png` / `_light.png` (two lanes, the marks in the second).
What the reference scopes add (`chartTriggerScopes`): with no place saved the flag is at 50 %, a saved 0.35 is
kept by a new tab, and a double-click on the flag puts it back at 50 % (its tooltip ends *Double-click: back to
50 %*; the box and the setting follow); the row's Line list has the line's colour dot (10 px, its middle the line's
colour); Find level on a sine of +-1 sets about 0, equal to `midRange`, and the box shows it; the crossing's place (the
flag's x, no T drawn) at the crossing (within a pixel of the level's y and the crossing's x), staying there with the
flag over it when the level moves,
and in the watched line's lane with Lanes on; the signal stopped, Normal back to waiting says *Normal · waiting,
last at hh:mm:ss* in the row, the same a second later, and *Normal · waiting* in the corner; Force while Normal
waits holds at the newest sample (*triggered*, its T there, the button hidden), and while Single waits too (Single
complete, the button Arm and primary), one width throughout; Run in the warn colour while stopped (at least 30
pixels of `warn` in the button, light and dark), none while running.
The marks outside the data (`chartTriggerMarksOutside`, the owner's test of the marks): with the trigger on the plot
is 26 px lower at its top, 21 px narrower at its left and 55 to 100 px narrower at its right (80), and gets all back when
it is off; the level's tab *0.4 A ↑* right of the plot, never over it, at the line's height; over it a pointing hand, the tab lit and its
tooltip *TAB 0.4 A, rising* then *Drag: the trigger level · Click the arrow: ...*; dragged from its lower part the
level follows the mouse's move (to -0.2, the box too); its arrow clicked takes falling, either, rising; the level's marker
*T▸* left of the plot, 21 px wide, its right 2 px left of the plot at the tab's height, right of every value label,
the line's colour on the level's row at its point and on its box's border, lit with the tab under the mouse with a hand
and its tooltip (*TAB ...*, then *Drag: the trigger level · the edge: in the Trigger row*), dragged from 4 px under
its top the level follows (to 0.3, the tab with it), with Lanes in its lane and clear of the labels; the flag
(a T over a ▼) in the strip above the plot, 16 px wide, its pointer's tip on the layer's top 2 px over the plot at the
crossing's x, pixels of the line's colour at the point and of the box's border above it, a hand and its tooltip
(*Trigger point: TAB crossed 0.4 A, rising, at ...*, then *Drag: where the crossing sits in the window · Double-click:
back to 50 %*), dragged 30 % of the plot from 6 px off its middle moves the place by
0.3 (the box follows, the flag still over the crossing), clamped to 90 % and 0 %, a double-click back to 50 %; no pixel of
the line's colour under the plot at the place (the old triangle gone). The state that does not dance
(`chartTriggerSteadyState`): a 50 Hz sine re-triggering Normal at 10, 20 and 100 ms windows, 100 frames each after
the first capture (84, 50 and 15 re-triggers): the row and the corner say *Normal · triggered*, their texts and
geometry the same in every frame; the signal moved away from the level, *Normal · triggered* stays for 0.95 to 1.2 s
after the last crossing, then *Normal · waiting* (the row: *Normal · waiting, last at* ...), the same for 60 frames
more. Short windows (`chartShortLock`): at 1 s no lock; a live 20 ms view of a 50 Hz sine with the trigger off locks
on its line's middle (0.2) by itself, *Auto (short window)* in the corner, no row, tab, flag or T, the button Hold;
across 30 frames the view's end moves (13 times) by whole periods only; flat for 1.5 s it runs free (*Auto · free
running*, live) and locks again when the wave returns; Display's *Lock short windows* off (saved false) and on again;
at 100 ms no lock; Hold ends it (the button Live), Live locks again; the user's trigger takes over (its row and tab)
and the lock comes back after it. The time grid (`chartTimeGrid`): at 1 s the clock-time labels as before and no
readout; at a 10 ms live window, 30 frames of a 70 Hz sine: the 9 lines inside the plot at its tenths, the same in
every frame while the view's end moves 0.5 s; the labels *-10 ms* ... *-8 ms* ... *0*, the *0* at the right edge; the
readout *1 ms/div · HH:mm:ss.zzz* in the state's row above the plot, at its right end, clear of the legend, all 11 labels
drawn, its clock time written at most twice a second, its tooltip; the wheel from 10 ms: 5 ms, 10 ms, 20 ms (*2 ms/div*), from 0.5 s: 1 s, 1.25 s, 1 s, 0.5 s; held by
the trigger (Normal, 20 %): *0* within a pixel of the crossing, *-2 ms* and *+4 ms*, the readout's clock time T's,
the readout left of the trigger's state, over neither it nor the legend;
Display's Time grid: Clock times at 10 ms, Divisions at 10 s (*1 s/div*, *-5 s*, *0*), saved and taken by a new tab,
Auto; in Arabic each offset and the readout an isolated left-to-right piece. On a card (`chartBinsAndGpu`) the
divisions are compared with the CPU's picture block by block. Times from T (`chartTimesFromT`): held by Normal on a
crossing at 10 ms, the hover box a division after T reads *T +1.000 ms* (to 0.01 ms) beside its clock time; cursors
0.25 ms before and 1.75 ms after T: their tags' tooltips *Cursor A at HH:mm:ss.zzz · T -0.250 ms* and *... B ... T
+1.750 ms*, the measure line *... · A: T -0.250 ms · B: T +1.750 ms*; live with the trigger off the box says how
long ago, the tags their clock time alone, the measure line no T.

**Languages** (`languages`, after the Help step): every `.ts` in `translations/` has each message translated, finished
and not empty, Arabic's numerus messages six forms, and each translation (each form) the English's `%1` placeholders,
`%CODE%` and `%NAME%` markers and HTML tags (`%n` in at least one form); the sidebar's language list (System, English,
العربية) saves `ui/language`, Restart now shows while the choice is not the language running; a second main window in
English and in Arabic is at most 1280 px wide at its narrowest; in Arabic the window is right to left with the chart
and the bit view left to right and the sidebar right to left, a text and the plurals of 4 and 11 devices are Arabic,
numbers have Western digits, the Help's first page is Arabic and its code blocks are made, the chart's and a
histogram's value labels are at the right of their boxes by the plot; English again after. The
rest of the test runs in English. With `EVRE_TEST_SHOT` set it saves `<prefix>_arabic.png`, `<prefix>_arabic_chart.png`
and `<prefix>_arabic_help.png`.

Some chart steps run on a Chart tab of their own, its clock standing still and moved by the test, fed samples
made up for the check:

- **Std dev and peak to peak** (`chartReadouts`): 12 V with 1 mV of 50 Hz ripple over 1 s reads 0.707 mV (within 1 %)
  and 2 mV; the two columns follow RMS, every column is shown by default; the header's right-click lists the columns,
  Std dev unticked hides it, a new tab finds it hidden (`chart/measureColumns`), ticked again shows it.
- **Totals** (`chartTotals`): 10 s of 2 A at 1 kHz with a Memory of 1 s sum 19.998 A·s while the memory is trimmed; a
  second more after a gap of 3 s adds 1.998, not the gap; a line taken off and put back keeps its total; the Since
  Clear column in Ah and the line *totals since <clock> (1 h 12 min)*; Clear starts them again. In the window's chart
  step, the math line P has its total too.
- **Log Y** (`chartLogScale`): the labels (*1 µ, 10 µ, 100 m, 1, 10000, 100 k, 2 M, 3 n*); a line from 1 m to 1 k
  with values at -1: a label at each decade, every decade as tall, values <= 0 on the bottom edge, saved; a value of
  1e-15 added: at most 9 decades; a typed min of -1 refused, 0.01 .. 100 taken; Log and Normalise turn each other off;
  Auto again is linear. With `EVRE_TEST_SHOT` set it saves `<prefix>_log.png`.
- **The info line** (`chartInfoLine`): narrowed pixel by pixel, the paint time goes first, then *plotted*, then the
  delay; no width gives a part cut in the middle or an ellipsis; the tooltip starts with the whole text.
- **The short window's lock on the busiest line** (`chartShortLockBusiest`, charts of their own): a polled line of
  10 polls a second plotted first and a fast line of 50 000 records a second (a 50 Hz sine, its crossings found by a
  `fast::TriggerScan` as the engine finds them) at a 20 ms window: the lock watches the fast line and the corner says
  *Auto (short window)* at each of 100 frames; the polled line alone (2 samples in the window): no lock, nothing in
  the corner.
- **One cap of 64 lines** (`chartOneCap`): the chart filled with math lines (fields of SUPPLY_V): the info line
  *64/64 plotted · N math*; a register's Plot, a 65th field and *New math line…* (no dialog) refused with the same
  words in the status bar (a fast channel's tick: in the fast streams step, where the map has a stream); a math line's *Shown* off makes room for a register, on again it is
  refused, its tick taken back; the lines removed, the registers' limit as before.
- **The RAM cut** (`chartRamCut`, a chart of its own): a fast line of two `i16` channels filled to its share of
  2 GB (as much as the machine fills in 20 s; about 1.8 GB in 4 s here), then RAM 256 MB: at the next block the store
  is at its new share; over 60 blocks, each with its `frame()` and a paint, no append, frame or paint over 20 ms. It
  prints the times (here: the longest append 4.6 ms, frame 10.8 ms, paint 10.8 ms).
- **The free memory** (`chartRamFree`): `effectiveRamMB` (held and free less the reserve, the floor, the RAM set at
  most) and the note *only 2.1 GB free: keeps about 40 s*; then a Chart tab of its own, a fast line filled to its
  share of 512 MB and a free memory given that leaves the chart 256 MB: the store down to it at the next block, no
  append, frame or paint over 20 ms over 60 blocks, the note *only ... free: keeps about ...* in the warn colour, the
  RAM box's tooltip (*Free now: ...*) and the strip's (*The free memory limits the budget now*); the free memory back:
  the RAM set again, nothing more let go over 20 blocks, the note *needs ...*. With `EVRE_TEST_SHOT` set it saves
  `<prefix>_ram_free_light.png` and `_dark.png`.
- **The memory strip's handle** (`memoryStripHandle`, a chart of its own, 10 ms of a minute held): a handle 12 px
  wide; the mouse over it a pointing hand, lit, the tooltip; taken, no jump, dragged 100 px the view 100 px of the
  strip later; a click 300 px left of it takes the view there; the wheel over the strip a window later, two notches up
  two windows earlier. With `EVRE_TEST_SHOT` set it saves `<prefix>_strip_handle.png`.

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

Each check prints `PASS` or `FAIL`. The run ends with the counts. With `example_device.json` it runs 557 checks. The
exit code is 0 when all pass, 1 on a failure, and 2 when the map or the fake device is missing.

`EVRE_TEST_SHOT=<prefix>` makes the test save two pictures of the window at the quick-write step:
`<prefix>_fields.png` (CONFIG with its fields) and `<prefix>_bits.png` (with Bits ticked), and one of a chart on the
Log scale, `<prefix>_log.png`, one of a recording's window, `<prefix>_recording.png`, and the Fast streams card at
its widest numbers and off, in English and Arabic and in both themes
(`<prefix>_fast_<en|ar>_<on|off>_<dark|light>.png`), and the window with two
fast lines of a million records a second, over 10 s (`<prefix>_fast_chart.png`) and 2 ms
(`<prefix>_fast_records.png`), a recording opened with its fast stream (`<prefix>_fast_recording.png`), and the Map
settings' Streams page in English and Arabic (`<prefix>_map_streams_en.png`, `_ar.png`).

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
bank. With the example map these are SUPPLY_V, SUPPLY_I, FAN_SPEED and MOTOR_SPEED. It then runs 25 checks in
`readonly`, 26 in `writes` and `danger`:

- `info` (connected, id echoed, write switches as started)
- `list`, and its `"streams": []` (the example map has none); a fast channel's name not found there
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

The fast streams have a mode of their own, which starts what it needs and stops it again (by its processes):

```sh
python3 tests/api_test.py fast build      # on Linux under xvfb-run, as the GUI test
```

It starts `evre_fake_fast` on port 1246 with `maps/example_fast.json` and an EVRe Studio connected to it with `--api`
(writes off; `EVRE_SHOT`, so settings of its own, 26.6), twice, and runs 15 checks:

- the stream off: `list` names it and its channels (off, 10000 a second); `get` and `stream` of a channel refused with
  the reason; an unknown channel; the pass-through's write of its enable is the client's own, refused (ERROR_RESP 3)
- started with `--fast ADC`: `list` says on; `get` of two channels and a register, their time within 1 s of now
- the pass-through: the enable written 1 with writes off, acknowledged, and the blocks come; written 0, they end while
  the Studio's stream goes on
- `stream` of two channels and a register: both kinds of line, tagged; about 10 000 records a second, each period
  starting where the one before ended; the min, max and mean of every period equal to those of the records the
  pass-through brought, record by record
- the `evre` package: `streams`, `fast_value`, `fast_stream`, `get`; `Device.stream` on port 1219 with writes off

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

- It plays Fast EVRe (13.9), per connection: a map's `streams` are served as the map says. A write of a stream's
  `enable` register that leaves it not 0 starts the stream: a sine per channel (50 Hz for the first, 100 Hz for the
  second ..., at 40 % of an integer type's range), at the map's rate, in blocks sent when full or 10 ms old, from a
  1 ms timer; 0 stops it; so does a host silent for 2 s (its host watchdog, which then sets the enable back to 0)
  and the connection's end. A stream's `rate_reg` holds the rate it runs at. The test aids (`FastSource::Options`):
  `--fast-lose N` (every N-th block is not sent: its records keep their numbers, the next block says LOST),
  `--fast-ppm P` (the sample clock P parts in a million fast; negative, slow), `--fast-first K` (the first block
  starts at record K: just below 2^32 walks a host across the wrap), `--fast-rate R` (R records a second in place of
  the map's rate; the rate register says so). `maps/example_fast.json` is the map it serves in the fast tests;
  `tests/fake_device.py` serves no stream. A device more than a second behind (a stall) drops what it could not
  send, and its next block says LOST.
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
  registers, and a sheet with only some columns is read; the fast example's stream in each (`exportStreams`): its
  window, rate, record and channels in the Markdown, `EX_ADC_FAST_ADDR` and `EX_ADC_V_BUS_OFFSET` compiled, `STREAMS`
  imported
- **bus files (3.9):** loaded, saved with the maps relative to the file, the broadcasts kept and unknown keys kept; a slave address twice,
  slave 0, device names that make register names ambiguous (`D1`, `D1_A`) and a bad name are refused; the next free
  device; a map is not a bus file
- **the broadcast rule (3.10):** into a writable register of one shared map; refused read-only, unmapped, or across
  different maps, except the reserved bank's CONFIG and MSG_CNT; never DEVICE_ID, never past the reserved bank
- **streams (16.13):** the fast example map's stream read key by key; a changed rate changes only the streams' text;
  a new map writes them after the registers; a missing key or an unknown channel type stops the load; every
  refusal of the checks (outside the device bank at either end, over a register, over another stream, too small,
  no channel, a `bytes` channel, a rate of 0, an `enable` or `rate_reg` that names nothing, an `enable` a host cannot
  write, a name used twice, no name)

`tests/schema_test.py` checks the schema itself (draft 2020-12), every map in `maps/` (or the files named), and that
a broken map is refused; a stream that is valid, and the refusals the schema can say (the window outside the device
bank, too small, a rate of 0, no channel, a `bytes` channel, a key missing); and that MAP_FORMAT.md's stream and
channel tables list exactly the schema's keys. It needs the `jsonschema` package and says SKIP without it.

### 26.8 `EVRE_PERF_LOG`

`EVRE_PERF_LOG=<file>` is a timing aid (`ChartTab::writePerfLine`), for measuring the chart on a given computer:
every 500 ms each Chart tab (the live one, and a recording's window) appends one line to the file:

```
14:03:12.500 chart fps 58.0 paint 6.12 max 14.30 ms | bin 0.50 lines 2.10 segments 0.00 present 0.00 marks 0.40 strip 0.30 legend 0.20 grid 1.10 ms | binned 3/29 | measure 1.20 ms x 5 threads 9.80 ms | polls 1000/s fast 1000000/s columns 2.3 | math 17.50 ms
```

- `fps`: frames painted a second; `paint`, `max`: the paint's average and longest, ms.
- The stages, ms a frame on average (`ChartView::takePerfStats`): `bin` the view's binning, `lines` the CPU's lines,
  `segments` the card's segment list, `present` the card's present, `marks` the cursors, notes, trigger, strips and
  crosshair, `strip` the memory strip, `legend` the legend, `grid` the chart's frame, the grid with its labels, the
  lane bar and the state corner. What the stages leave of `paint` is the Y ranges and the layout.
- `binned N/M`: frames that binned the view of the frames painted (a held view whose lines are reused bins none).
- `measure`: the measurement table's updates, the window thread's time in all and their count; `threads`: the
  chart's threads' time on the full measurements (23.8).
- `polls`: polls a second (the samples of the register with the most); `fast`: the fast streams' records a second.
- `math`: the window thread's time computing fast math lines (9.9) in the 500 ms, ms.
- `columns`: the columns of fast lines binned a frame on average. A live view moves by a column or two a frame
  and keeps the columns it shares with the frame before (23.11), so this stays a few, not the view's thousand; a
  held view that is reused bins none; a zoom, a trim past the kept columns or a new start bin the view whole once.

Unset, nothing is timed into a file. The aid does not change the settings.

### 26.9 Fast EVRe without a window

```sh
./build/evre_fast_test
python3 tests/fast_lib_test.py build            # [--lib DIR]: the EVRe library (EVRE_LIB, or ../lib)
```

`evre_fast_test` (QtTest, no window, no device) checks `src/io/fast_stream.*` and the chart's store,
`src/model/fast_store.*`:

- **the block's rules (18.9):** the header read; a count that does not fit (one byte more or less, a header cut, a
  record of another size) is bad, counted, no record used; an unknown flag or a spare not 0 is a newer kind; START,
  numbers that follow, a gap with and without LOST, a number that goes back without START (a restart), START again;
  the wrap at 2^32 (the 64-bit number goes on, 10 lost across it counted); a host that joins late; a bad block's
  records counted as lost at the next good one
- **a fuzz:** 200 000 blocks of random bytes (a third with a header that looks right): no crash, and records only
  from a block whose size is exactly right; the counts add up
- **the clock's fit (13.9):** ten minutes of blocks from a device 200 ppm fast, 200 ppm slow and on time, each
  arriving late by a random delay (now and then by 20 ms): after one minute and after ten its records lie within
  2 ms of the true time and the correction shown is within 20 ppm of the truth; no time mark steps the time; the
  rate moves at most 10 ppm a second. It prints the error at both times (0.1 to 0.3 ms on Linux)
- **a restart** of the stream: the clock starts again from the block's arrival, a block's length before it
- **the fake devices' source:** blocks when full or 10 ms old, the values each record's number gives; `--fast-lose`
  and `--fast-first` (the numbers, the LOST flags, the wrap without a restart); `--fast-rate` and `--fast-ppm` (1 000
  000 a second, 1000 ppm fast: 100 000 to 100 100 records in 100 ms)
- **the helper's frames** (`helperFrames`), when `tests/fast_lib_test.py` hands them over in `EVRE_FAST_FRAMES`:
  through the Studio's parser and the block's rules, each with the number, count and flags the helper was asked for
  and its records' values; skipped otherwise
- **the store (23.11):** its min and max, sum and sum of squares over random spans, through the summaries, equal to
  a plain loop over the records, with gaps among them; the gaps where the numbers jump and only there
  (`storeSummaries`); `lowerBound` and `upperBound` against the records' times at 2000 random times across a gap,
  the records lost before it (`storeBounds`); a new start whose clock begins before the last one ended, shifted
  after it, its gap not counted (`storeNewStart`); a trim drops whole pieces, what stays reads the same, its min and
  max still right, the bytes it says it holds what its arrays hold (`storeTrim`)
- **a recording (12.7):** written as the Studio writes it from the fake devices' source (every 7th block lost, a
  restart) and read back mapped: as many records as a store fed live, the same values, the same gaps and records
  lost, times within 0.1 ms (1.8e-15 s here), the same min and max; a copy cut 5 bytes into its last piece reads one
  block less and says it is cut; a CSV is refused; `recordingsBeside` finds it beside its CSV
  (`recordingReadBack`)

`tests/fast_lib_test.py` compiles `EVRe/lib/fast/evre_fast.cpp` with the library and a test program as C++11, 14, 17
and 20 at -O0, -O1, -O2, -O3 and -Os, the helper with `-Wall -Wextra -Wpedantic -Werror`, and runs each build: a
window below the device bank, past it, too small, a record of 0 bytes and a null pointer refused, no frame after a
refusal, a window up to 0xDFFF taken; 254 records of 4 bytes in 1024; a frame of 10 records whole (58 bytes, START,
the CRC); the largest block (1034 bytes); 255 records refused with nothing written and the numbers kept; no frame
from slave 0; `evre_fast_lost()` (the numbers go on, the next block says LOST); a block of no records; the wrap; a
start again. Then: no heap (`nm -u`: the helper calls only `GetCrc16`), its stack at most 64 bytes a call
(`-fstack-usage`), its frames through `evre_fast_test`, and PROTOCOL.md's example block (after
`<!-- fast-example -->`) equal byte for byte to the one the helper builds. Without `g++` or the library it says SKIP.

## 27. Design decisions and pitfalls

| Decision&nbsp;or&nbsp;pitfall | Reason |
|---|---|
| All&nbsp;device&nbsp;I/O&nbsp;on&nbsp;its&nbsp;own&nbsp;thread | drawing, dialogs and repaints never delay a poll; the CSV and the chart get every poll |
| The window talks to the engine only through `post()`, queued signals and locked copies | one thread owns each piece of state; no locks spread across the UI |
| `RegTable`&nbsp;has&nbsp;one&nbsp;writer | the I/O thread reads it without a lock; the window's copy can never tear |
| Every `QObject` on the I/O thread is a child of a moved object or created there | a member left in the GUI thread cannot be started from the I/O thread; Qt only warns and the feature silently fails (20.7) |
| Own ticker thread instead of a Qt timer for polling | Qt timers and C++ timed waits round to the 15.6 ms system tick on Windows |
| Due ticks counted, backlog capped at about 20 ms of polls | a slow moment is made up, a stall is not made up in a burst |
| `Qt::PreciseTimer` for answer timeouts, streams and the fallback frame timer | coarse timers drift by about 20 % on Windows |
| Frame&nbsp;clock&nbsp;on&nbsp;`DwmFlush` | a 16 ms timer against a 60 Hz display drops a frame every 0.4 s |
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
| A&nbsp;map&nbsp;is&nbsp;loaded&nbsp;into&nbsp;a&nbsp;temporary | a bad file leaves the current map untouched |
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
| **Pitfall:** a painter on a widget takes the application's direction | not the widget's: in Arabic a left to right widget's right-aligned labels went to its left edge; the chart's frame and the analysis plots set `Qt::LayoutDirectionAuto`, as a painter on a picture has it (placed as aligned, an Arabic text still right to left) |
| **Pitfall:** Qt warnings are invisible on Windows GUI builds | they go to the debugger output; the GUI test counts the thread warnings for that reason |
| **Pitfall:** a dialog closed with its title bar's X stalls the live chart on Windows | while the X is held, Windows runs a loop of its own on the window's thread (the chart waits; the fast blocks queue, 23.11), and the compositor (DWM) then plays the dialog's close animation. The Chart tab's dialogs are made with `noWindowAnimation` (`ui_helpers`: `DwmSetWindowAttribute(DWMWA_TRANSITIONS_FORCEDISABLED)`, nothing on Linux), which takes the animation away; the held loop cannot be |
| **Pitfall:** a read that fails keeps the last good value | the table marks the error, but that poll's sample and CSV cell repeat the old value (chapter 29) |

## 28. Glossary

| Term | Meaning |
|---|---|
| EVRe | the register protocol of teknile that the Studio speaks: framed requests over TCP or a serial line |
| frame | one EVRe message: `7B SLAVE FN OFF CNT DATA CRC 7D` |
| slave | the device address byte in a frame, 0 to 255 |
| function&nbsp;code&nbsp;(FN) | what a frame is: READ, READ_RESP, WRITE, WRITE_ACK, WRITE_ACK_RESP, ERROR_RESP |
| CRC-16/X-25 | the frame check: polynomial 0x1021 reflected, init and final XOR 0xFFFF |
| map | the JSON file (`evre-map/1`) that describes a device's registers |
| register | a named address with a type, size, access and presentation |
| danger&nbsp;register | a register marked `"danger"`: every write is confirmed; API writes need an extra switch |
| login&nbsp;register | the map's optional `"login"` address where the token is written after connecting |
| token | the text typed in the token box or given in `EVRE_TOKEN`; never stored |
| bank | 256 addresses with the same high byte, e.g. 0xD0xx |
| block | one read request that covers several registers close together |
| poll | one read of every block, and one sample and CSV row when all are answered |
| poll&nbsp;under&nbsp;way | a poll whose blocks are not all answered yet; several may overlap |
| In&nbsp;flight | the number of requests the master sends before their answers come |
| pipelining | sending several requests before the first answer, as In flight > 1 allows |
| pending | a request sent whose answer has not come yet |
| keep-alive | the idle read of 0xA000 every 400 ms |
| ticker | the thread that marks poll deadlines |
| due&nbsp;tick | a deadline that has passed but whose poll has not started yet |
| backlog | due polls waiting for a free slot, capped at about 20 ms of them |
| continuous&nbsp;mode | poll interval 0 ("max"): the next poll as soon as one ends |
| layout&nbsp;generation | the engine's counter of block layouts; answers of an older layout are ignored |
| map&nbsp;generation | the window's counter of maps sent to the engine; snapshots of another one are ignored |
| version | a row's change counter in `RegTable`; the window copies only rows that moved |
| snapshot | the window's copy of `RegTable`, taken under its mutex |
| stale | a value older than `max(2 × interval, interval + timeout)`, shown grey |
| unavailable | a register the device refused for good (error 3, 4 or 5); no longer polled |
| sample | one (time, value) point of a register, one per poll |
| series&nbsp;/&nbsp;line | one register or math line on the chart |
| key | a line's id on the chart: `regKey(slave, address)` for a register, or `FIRST_CHART_KEY + i` (1 << 24) for math line i |
| math&nbsp;line | a formula over registers drawn and measured like a register's line |
| window&nbsp;(chart) | the time span shown |
| memory | the time span kept; the view moves inside it |
| live&nbsp;/&nbsp;held | the view follows now / stays where it is |
| smooth&nbsp;delay | how far behind now the live edge is drawn, measured from how late samples arrive |
| bin | the samples of one pixel column, by absolute time |
| chunk | min, max, first and last of 8, 64, 512 or 4096 consecutive samples |
| cosmetic&nbsp;pen | a Qt pen whose width is in device pixels, whatever the transform |
| frame&nbsp;clock | the source of one tick per display refresh |
| pass-through | the API port 1219 that speaks EVRe frames |
| JSON&nbsp;lines | the API port 1220: one JSON object per line each way |
| stream | an API client's periodic samples of named registers |
| unsolicited | a frame that answers no pending request |

## 29. Open items

| Item | Notes |
|---|---|
| Poll&nbsp;only&nbsp;the&nbsp;plotted&nbsp;registers | an option to read fewer blocks per poll when only a few registers matter |
| Math lines in the CSV and the API | math lines are computed in the GUI thread for the chart only; CSV rows and API answers carry registers only |
| A failed read repeats the last good value | `setError()` keeps `valid` and `raw`; the poll's sample and CSV cell then repeat the old value; an empty CSV cell for a failed read may be better |
| Login&nbsp;on&nbsp;serial&nbsp;links | only TCP connections send the token; a device that wants a login on a serial port cannot get one |
| API&nbsp;ports&nbsp;only&nbsp;in&nbsp;the&nbsp;settings | `api/evrePort` and `api/jsonPort` have no field in the window and no command-line option |
| Unit&nbsp;tests | the map files and the exports have their own test (`evre_map_test`); `Expr`, value coding and the frame parser are still tested only through the GUI and API tests |
| Translations | user text goes through `tr()`, but no translation files are built or shipped |
| Packaging | no CMake install target; the installers are made by the release workflow (25.5), and they are not code-signed |
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
| Unit,&nbsp;Description | free text |
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
| **+&nbsp;Register** | | a new register after the selected one: the next free address, its type and group, a name `REG_XXXX` |
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
| a&nbsp;name&nbsp;used&nbsp;twice,&nbsp;or&nbsp;no&nbsp;name | error |
| a register past 0xFFFF, a size under 1, a scale of 0 | error |
| bit fields on a float or a byte array; a field past the register's bits | error |
| special values on a byte array; *write-1-to-clear* on a float | error |
| min&nbsp;above&nbsp;max | error |
| slave&nbsp;0&nbsp;(the&nbsp;broadcast&nbsp;address) | error |
| registers&nbsp;that&nbsp;share&nbsp;bytes | warning |
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
| Streams | the map's fast streams (Fast EVRe, 13.9, 16.13): the list on the left (**+&nbsp;Stream**, **Remove**), the selected one's name, its window (**Address** and **Size**), its rate in samples a second, its **Enable** and **Rate register** (the map's registers offered, any name typed), its description, and its channels in a table: name, type (`u8` … `i32`, `f32`), unit, scale, offset, decimals, description (**+&nbsp;Channel**, **Remove&nbsp;channel**) |

**The Streams page** says under the channels what a sample takes and how many fit a block (*A sample: 4 bytes · at
most 254 samples a block*), and under everything the map's checks of the streams as they are typed (16.13): a
window outside the device bank or over a register or another stream, too small for one sample, no channel, a channel
of bytes or a scale of 0, an enable a host cannot write, a rate register that cannot be read. **+ Stream** makes
`S2` (or the next number) with the first free 256 bytes of the device bank from `0xDC00` down, 1000 samples a
second and no channel yet; **+ Channel** adds `CH1` (`i16`) at the end of a sample. OK puts the streams into the map
with the other settings, one undo step; Save writes them after the registers (16.13).

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
- the fast streams (16.13), each its window, rate, a record's size and the records a block, its enable and rate
  registers, and a table of its channels with each one's byte in a record, after a paragraph on the block (the
  header's fields and flags)
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
| `P_NAME_ADDR`,&nbsp;`P_NAME_SIZE` | the address, the size in bytes |
| `P_NAME_MIN`,&nbsp;`_MAX`,&nbsp;`_DEFAULT` | in shown units: an integer, or a `float` literal (`3.65f`) for an `f32` |
| `P_NAME_<VALUE>` | each value name's number; special values marked `/* special */` |
| `P_NAME_<FIELD>_POS`,&nbsp;`_MSK` | each field's position and mask, `P_NAME_<FIELD>_<VALUE>` its values |
| `P_DEVICE_ID`, `P_LOGIN_ADDR`, `P_LOGIN_SIZE` | the map's device ID and login |
| `P_S_FAST_ADDR`,&nbsp;`_SIZE`,&nbsp;`_RATE` | a fast stream `S`'s window and rate (records a second); `_FAST`, not `_STREAM`: an enable register is often named so |
| `P_S_FAST_RECORD_SIZE`,&nbsp;`_RECORDS_PER_BLOCK` | a record's bytes, the most records a block holds |
| `P_S_<CHANNEL>_OFFSET` | a channel's byte in a record, its type, unit and scale in the comment |

Names are made C identifiers (`Power & supply` → `POWER_SUPPLY`); a name that would be defined twice gets `_2`.
The header is guarded, and the tests compile it with `gcc -Wall -Wextra -Werror` (26.7).

### 32.3 Python module

A module for host scripts: `DEVICE`, `DEVICE_ID`, `SLAVE`, `LOGIN`, one constant per register address, and
`REGISTERS`, a dict of every register's definition (address, type, size, access, group, unit, description, write,
persist, danger, plot (only when `False`), scale, offset, min, max, default, `enum`, `special`, `fields` with `lsb`,
`width` and `values`), and `STREAMS`, a dict of the fast streams (address, size, rate, `record_size`, enable,
rate_reg, description, `channels` each with its name, type, `at` (its byte in a record), unit, scale and offset).

### 32.4 CSV and Import CSV

One row per register, one column per key: `addr, name, type, size, unit, access, write, persist, group, desc, notes,
danger, format, scale, offset, decimals, min, max, default, special, enum, fields, plot` (`plot`: `0` for a register
not plotted, empty otherwise; a file without the column: every register plotted). The compact columns:

| Column | Written as |
|---|---|
| `enum`,&nbsp;`special` | `0=off;1=on` |
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
| `P_WRITE_MIN`,&nbsp;`P_READ_MAX` | the library's `DEVICE_REG_WRITE_MIN` and `DEVICE_REG_READ_MAX`; also `P_ID`, `P_SLAVE`, `P_RO_SIZE`, `P_RW_SIZE` |
| `p_ro_t`,&nbsp;`p_rw_t` | the read-only and the read-write image: packed structs with a member per register in address order (its name in lower case; a C++ keyword gets `_`), a gap as `_gap_d016[106]`, the defaults as start values in raw units |
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
| an&nbsp;address&nbsp;the&nbsp;base&nbsp;has | only the keys given change; `null` removes the base's key (here: the unit) |
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
evre info  LINK [--map MAP]                 DEVICE_ID, protocol revision, capabilities, CONFIG; the map's streams
evre read  LINK --map MAP NAME...           values, by name (any case) or 0x address
evre read  LINK --addr 0xD000 --count N     raw bytes, no map needed
evre dump  LINK --map MAP                   every register a poll reads, once
evre watch LINK --map MAP NAME... [--interval MS] [--count N]    a CSV line per poll
evre write LINK --map MAP NAME=VALUE... [--force]                written, then read back
evre check LINK --map MAP [--writes] [--force]                   does the device answer as its map says? (34.1)
evre broadcast LINK --map MAP|--bus BUS NAME=VALUE [--force]     every device at once, then each read back (3.10)
evre record LINK --map MAP --stream NAME -o FILE [--seconds S]   a fast stream's blocks into a .evrs file (34.2)
```

| Option | Meaning |
|---|---|
| `--tcp HOST:PORT` or `--serial PORT[:BAUD]` | the link (baud 115200 when not given) |
| `--bus BUS` | in place of `--map MAP` for `read`, `dump`, `watch`, `write` and `broadcast`: several devices on one link (a bus file, 3.9). The registers are named after their devices (`D1_SPEED`, `D2_SPEED`), each request goes to its device's slave, and every device with a login register is logged in |
| `--slave N`,&nbsp;`--timeout MS` | else the map's slave address and `protocol.timeout_ms` (1, 1000 ms) |
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
| DEVICE_ID,&nbsp;STATUS | FAIL: another device ID than the map's; WARN: a protocol revision other than 1 |
| every&nbsp;readable&nbsp;register | FAIL: the read is refused or times out, or answers another size than the map's |
| its&nbsp;value | WARN: past `min` / `max`, or an enum value the map has no name for (a special value is fine) |
| a&nbsp;write-only&nbsp;register | WARN: the device answers a read the map says it refuses |
| with `--writes`, each read-write register (not `action`) | the value it holds is written back and read again: FAIL if the write is refused, WARN if it reads back otherwise. Danger registers only with `--force` |
| with&nbsp;`--writes`,&nbsp;each&nbsp;fast&nbsp;stream | switched on for 2 s (its enable written 1, CONFIG read every 100 ms) and off again, four lines under its name: FAIL when no block comes or the first does not say START; when the numbers do not follow (a record lost, a restart, a bad block); when the rate, from the first block's arrival to the last's, is more than 2 % from the device's (its `rate_reg`, else the map's); when a block still comes 300 ms after the 0 was taken |

Without `--writes` it only reads: safe on a running device. Run it on a new firmware against its map, in CI against
`evre-sim`, or after changing a map.

### 34.2 `evre record`: a fast stream into a file

```
evre record LINK --map MAP --stream NAME -o FILE [--seconds S] [--json]
```

A fast stream (13.9) of the map, by its name (any case), into a `.evrs` file: the stream's `rate_reg` read (if the
map names one), its `enable` written 1, then every block from the device's slave at the stream's window checked by
the block's rules (18.9) and written as it came, until `--seconds` have passed or Ctrl+C. Meanwhile CONFIG is read
every 100 ms (the device's host watchdog). At the end the `enable` is written 0 and one line sums it up:

```
ADC: 30235 records in 290 blocks, 0 lost, 0 bad, 0 of a newer kind, 3.0 s; 10000.1 records/s (11 ppm); done
```

(`--json`: one object with `records`, `blocks`, `lost`, `bad_blocks`, `newer_blocks`, `starts`, `seconds`, `rate`,
`ppm`, `stopped`.) When no block came in 2 s, or the link closed, it stops and the exit code is 1; a stream the map
does not have, or one with an error in the map's checks, is 2. `evre info` lists the map's streams (*fast stream
ADC: window 0xDC00, 1024 bytes, 10000 records/s, 254 records a block; I_LOAD [A] i16, V_BUS [V] i16; enable
ADC_STREAM*; with `--json` a `streams` list), and `evre validate` checks them.

**The file**, `.evrs`, is pieces, each a name of 4 ASCII bytes and a length (u32, little endian) before its body, so
a reader skips a piece it does not know and stops at one cut off:

| Piece | Body |
|---|---|
| `EVRS` | first, JSON (UTF-8): `"format": "evre-fast-rec/1"`, `"device"` (the map's), `"stream"` (the stream's object as the map writes it), `"start"` (local time, ISO 8601 with milliseconds) |
| `TIME` | 16 bytes: a record number (u64, the 64-bit count of 18.9) and the writer's clock for it in seconds (f64): one before the first block of every start, then about one a second, as the clock's fit had it |
| `BLK ` | one block as it came: its 8-byte header and its records (the fourth byte of the name is a space) |

Nothing is converted and nothing is lost: gaps stay gaps. A reader lays the records on the recording's clock from
the `TIME` pieces: between two marks, linearly. Python reads it in a few lines (`struct`).

```sh
build/evre_fake_fast 1238 maps/example_fast.json &
evre record --tcp 127.0.0.1:1238 --map maps/example_fast.json --stream ADC -o run.ADC.evrs --seconds 10
```

## 35. `evre-sim`: a device made from a map

`evre-sim` serves a map as an EVRe device over TCP: for trying a host, a script or the Studio before the hardware
exists, for demonstrations, and for tests. It is built with the Studio (`build/evre-sim`).

```
evre-sim MAP [--port 1210] [--any] [--slave N] [--token T] [--require-login] [--strict] [--state FILE] [--verbose]
         [--fast-lose N] [--fast-ppm P] [--fast-first K] [--fast-rate R]
```

It answers its own slave address only: the map's `"slave"`, or `--slave N` (1 to 255; another value ends it with exit code 2). A frame for another slave
gets no answer; a broadcast (slave 0) WRITE is taken as a WRITE to it and not answered.

The device does what its map says:

| The&nbsp;map&nbsp;says | The simulator |
|---|---|
| `device_id` | DEVICE_ID; STATUS reports protocol revision 1. The protocol's bank (`0xA000`…) is never moved or reset, even when the map lists its registers |
| `default` | the register's value at the start (else 0) |
| a&nbsp;read-only&nbsp;number | moves: a float as a slow sine wave, an integer as a slow wave, inside `min` … `max` when given; a `u32` in `ms` counts milliseconds. Registers with value names, fields or a default stay put |
| `"access": "wo"` | takes writes, refuses reads (ERROR_RESP 3) |
| `"access": "ro"` | refuses writes (ERROR_RESP 3) |
| `"write": "action"` | holds the value written 200 ms, then reads back idle (its default, else 0) |
| `"write": "w1c"`, a field `"access": "w1c"` | a 1 written clears the bit, a 0 leaves it; such bits start set, like a latched fault |
| a field `"access": "ro"` in a writable register | keeps its bits whatever is written |
| `min`,&nbsp;`max`&nbsp;with&nbsp;`--strict` | a value past them is refused (ERROR_RESP 3) |
| `login` | a write of the whole login register is accepted with the token (`--token`, default `example-token`), else refused; `--require-login`: nothing else is written on a connection before its login |
| `persist`&nbsp;with&nbsp;`--state FILE` | those registers are kept in FILE (JSON) across restarts |
| an&nbsp;address&nbsp;in&nbsp;no&nbsp;register | ERROR_RESP 4 (offset out of range); a READ of a stream's window too |
| `streams` | each served on every connection as `evre_fake_fast` serves it (26.4): its `enable` written not 0 starts it (a sine per channel at the map's rate, in blocks when full or 10 ms old), 0 stops it, and so do 2 s without a request (the host watchdog, which sets the `enable` back) and the connection's end; its `rate_reg` holds the rate it runs at. The test aids `--fast-lose`, `--fast-ppm`, `--fast-first`, `--fast-rate` as there |

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
`python/README.md` lists its API. The master takes a frame for its answer only when the slave, the offset and the
count are the request's (18.6), so a frame the device sends by itself (auto send's block) is never taken for the
answer to a read of part of it.

A fast stream (13.9): `dev.stream('ADC', seconds=2)` switches it on (its enable written 1, not waited for), yields
each block as it comes (`first`, `count`, `lost`, `new_start`, `values` per channel in the map's units), reads CONFIG
every 100 ms meanwhile without waiting for the answers, and writes 0 at the end, also when the loop breaks. A
recording (12.7): `evre.read_recording('run.ADC.evrs')` gives each sample's number, time and values, the gaps and
the starts; a file cut off is read up to its last whole piece.

Several devices on one link: `evre.connect_bus_tcp(host, port, bus_file, token=...)` gives a `Bus` of the devices of
a bus file (3.9). Its registers go by their names on the bus (`bus['D2_FAN_SPEED'] = 40`), `bus['D1']` is a device's
`Device`, and `bus.broadcast(name, value)` sends one frame to every device under the rule of 3.10, then reads each
one back.

Scripts that should share the device with the Studio use the Studio's API instead (chapter 17): the package opens a
link of its own. `evre.connect_studio()` is the client of the Studio's JSON port: `get`, `set`, `stream`, and a fast
stream's channels as each period's min, max and mean (`fast_stream`, 17.3); `evre.connect_tcp('127.0.0.1', 1219, map)`
reaches the device through the pass-through, `dev.stream` included (17.4).


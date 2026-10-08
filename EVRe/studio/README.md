# EVRe Studio

by **teknile**

EVRe Studio is a desktop register tool for any device that speaks the **EVRe** register protocol, over **TCP** or a
**serial / USB CDC** port. A JSON **device map** tells it what the device holds. The Studio polls the registers
live, shows and decodes them, charts them like an oscilloscope, records them to CSV, writes them with safety
checks, and shares the device with other programs through a local API. It runs on **Windows** and **Linux**
(C++17, Qt 6).

EVRe is a protocol of **teknile**.

## Features

- **Links:** TCP (any EVRe-over-TCP server or gateway) or a serial / USB CDC port; slave address, answer timeout,
  pipelined requests (*In flight*), reconnect by itself. An optional login token is written to a login register
  the map declares.
- **Device check:** DEVICE_ID and STATUS are read at every connect; a warning when the map is for another device.
- **Several devices on one link:** a bus file (`evre-bus/1`) puts devices at their slave addresses on one link
  (RS-485, a gateway); their registers are named after them (`D1_SPEED`, `D2_STATUS`) in the table, chart, CSV and
  API; a device that stops answering goes offline without slowing the others. Broadcasts (slave 0) only where they
  mean the same to every device.
- **Register table:** live values with units, decoded bit fields and enum names, search and group filter, tooltips
  with the raw bytes and the value's age, a detail line for the selected register. Changed values glow, values
  that are not refreshed turn grey.
- **Writes** only with *Allow writes* on. Registers marked `"danger"` ask first, a value that changed while you
  were editing asks first, and every write is read back: the table always shows what the device reports.
- **Quick write** for the selected register: a value box with the map's range, its named and special values, a
  Default button, and its bits drawn as in a datasheet (click a bit or a field to change it, read-modify-write).
- **Map editor:** make a map from nothing or change one, without touching JSON (fast streams too, on Map settings'
  Streams page): a table edited in place, bulk
  edits, copy and paste between maps, undo and redo, value names and bit fields on a bit strip, limits, defaults,
  special values, notes, live checks, and a live preview of the device's value as it is being defined. A save
  changes the file only where it was edited, so hand-written maps keep their layout. Overlays (`"extends"`) change
  a base map in a small file of their own.
- **`evre` on the command line:** validate and export maps, and read, dump, watch, write or record (a fast stream) a
  device from a script or a CI job (`evre validate maps/*.json`, `evre watch --tcp host:1210 --map m.json SUPPLY_V`).
- **`evre-sim`:** any map served as a device over TCP, behaving as the map says (defaults, write-only, action,
  write-1-to-clear, limits, login, persistence): try a host before the hardware exists.
- **`evre` for Python** (`python/`): a device by register name with its map, standard library only:
  `dev = evre.connect_tcp(host, port, 'map.json'); dev['SUPPLY_V']; dev['LED_MODE'] = 'blink'`; a fast stream's
  recording read with `evre.read_recording('run.ADC.evrs')`, a stream live with `dev.stream('ADC')`.
- **Export** for the people and programs that implement or use the device: a Markdown specification (with ASCII
  bit diagrams), a C header, a Python module, CSV; CSV back in. A JSON Schema of the format for other tools. For
  firmware on the EVRe device library, a **device table**: the register images as packed structs, their addresses
  checked at compile time, and the function that serves them (`evre export map.json --to table`).
- **English and Arabic** (العربية, right to left; the chart, numbers and register names stay left to right), the Help
  pages too; chosen at the bottom of the sidebar.
- **Chart** like an oscilloscope: memory depth apart from the view, Hold / Live, a memory strip, cursors A and B,
  Auto, Manual or Log Y, lanes (a plot per unit, each its own Y range; they scroll and fold when there are many), Normalise, Smooth scrolling that follows the display refresh, the values of every line beside
  the mouse (Hover values). Many fast lines at the display's rate: 64 lines of 1000 Hz at 60 fps on a 4K screen,
  the plot drawn by a graphics card when there is one (Windows, Direct3D 11), on the CPU otherwise. The samples
  kept stay within a RAM budget you set.
- **Measurements** per line: value at A and B, B − A, min, max, mean, RMS, standard deviation, peak to peak and
  the area under the line (W → J and Wh, A → A·s and Ah), and each line's total since Clear (Wh, Ah) summed from
  every sample; columns chosen by a right-click on the header.
- **Recordings:** the chart's view or A → B exported to CSV, pictures of the chart, notes on it; a recording or an
  export opened in a window of its own (its chart, measurements, notes, math lines) while the live chart goes on.
- **Analysis:** a line's histogram or spectrum (Welch, our own FFT) over A → B in a window of its own; a trigger that
  holds the chart on a level crossing, Auto, Normal or Single, as an oscilloscope.
- **Math lines:** formulas over registers (`SUPPLY_V * SUPPLY_I`), drawn and measured like registers.
- **Auto send:** a device that can sends its read-only block by itself at a set rate; each frame is one chart
  point and one CSV row.
- **Fast streams** (Fast EVRe): a device that samples on its own clock sends numbered blocks; the sidebar starts
  and stops a map's streams, shows the samples a second fitted to the Studio's clock and counts every sample lost.
  Their channels are lines on the chart, from an hour down to single samples 10 µs apart, a spike of one sample
  in millions never hidden, the line broken where samples were lost, measured as any line (between the cursors in a
  frame, whatever the span). A CSV recording records the streams beside it (`run.ADC.evrs`, as they came), and the
  recording window opens them, mapped, larger than the RAM too. `evre record` writes a stream's blocks to a
  `.evrs` file.
- **CSV recording:** one row per poll of the registers you choose (one row per frame with auto send).
- **Monitor:** every frame sent and received, raw reads and writes, to any slave address or as a broadcast.
- **Log:** every event in a tab and a daily file; pop-ups for warnings and errors that never block, and that hold
  back a repeat of the same message for 30 s.
- **API:** port 1220 speaks JSON lines by register name (Python, MATLAB, LabVIEW, …); port 1219 is an EVRe
  pass-through for existing EVRe clients. Both share the Studio's one request queue with its polls.
- **Fast:** all device I/O runs on its own thread with a high-resolution poll clock; registers are merged into
  block reads and polls can overlap. About 4000 polls/s against the fast test device on the same computer.

## Quick start

### Build

You need Qt 6.5 or newer with Widgets, Network, SerialPort and Test (Test is required by every build, not only the
GUI test) and the Linguist tools (`lupdate`, `lrelease`: the translations are built into the program), a C++17
compiler and CMake 3.21 or newer.

**Linux** (Debian / Ubuntu):

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Windows** (Qt for MinGW 64-bit with the Qt Serial Port module; MinGW, CMake and Ninja from the Qt installer):

```sh
cmake -S . -B C:/b/evre -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>/<version>/mingw_64
cmake --build C:/b/evre
windeployqt --release --no-translations C:/b/evre/EVReStudio.exe
```

Keep the build folder in a short path on Windows: long object file paths can exceed the 260-character limit.
The build copies `maps/` next to the executable, at every build.

### Run with the fake device

`tests/fake_device.py` is a small EVRe device in Python (standard library only) that serves the registers of
`maps/example_device.json` on `127.0.0.1:1210`, the Studio's default link:

```sh
python3 tests/fake_device.py
./build/EVReStudio
```

Click **Connect**. The pill turns green, the device ID `0x1001` appears under it, and the Registers tab shows live
values. Tick **Plot** on SUPPLY_V and SUPPLY_I and open the **Chart** tab.

The example map declares a login register, and the fake device accepts the token `example-token` there (another
one with `--token`). Start the Studio with `EVRE_TOKEN=example-token` to log in; any other token is refused, and the
Log says so.

## Command line

```
EVReStudio [--tcp host:port | --serial COMx[:baud]] [--map file.json | --bus bus.json]
           [--plot NAME,NAME] [--tab registers|chart|monitor|map] [--connect]
           [--interval ms] [--inflight n] [--record file.csv]
           [--api] [--api-writes] [--api-writes-danger]
```

For example, connected at start with two lines on the chart:

```sh
EVReStudio --tcp 127.0.0.1:1210 --map maps/example_device.json --plot SUPPLY_V,SUPPLY_I --tab chart --connect
```

A login token comes from the environment variable `EVRE_TOKEN`, never from the command line. `--help` lists every
option.

## Documentation

- **[docs/STUDIO.md](docs/STUDIO.md)** is the full documentation: Part I shows how to use the Studio, Part II is the
  reference (the device map format, the API, the protocol as the Studio uses it), Part III describes the internals
  for people who maintain or extend it, Part IV is about making maps: the Map editor, exporting, overlays.
- **[docs/MAP_FORMAT.md](docs/MAP_FORMAT.md)** is the map format's contract for any tool that reads or writes
  maps, and **[docs/evre-map-1.schema.json](docs/evre-map-1.schema.json)** its JSON Schema.
- **Help (F1)** in the Studio has a short form of Part I.

## Device maps

A map is a JSON file (`evre-map/1`) that lists a device's registers: address, name, type, unit, access (read-only,
read-write, write-only), group, scale and offset, value names, special values, bit fields, limits and defaults,
write behaviour (action, write-1-to-clear), persistence, notes, and `"danger"` for registers that move, power or
reset something. Top-level keys describe the device: its ID, USB IDs, login register, protocol and notes.
`maps/example_device.json` shows every kind of register; chapter 16 of the documentation describes the format and
Part IV how to make one in the Map editor.

## Examples

`examples/` holds API clients that use the example map's registers:

- `examples/python/evre_studio_client.py`: a small client class (standard library only), runnable as a script
- `examples/matlab/evre_studio_demo.m`: reads, writes, streams and plots (R2020b or newer)
- `examples/README.md`: the JSON commands, and step-by-step notes for LabVIEW

## Tests

| Test | What |
|---|---|
| `evre_gui_test` | drives the real window with QtTest against `tests/fake_device.py` (536 checks with the example map: the login, the Map editor, limits and fields, a bus, broadcast, auto send and fast streams included; it also starts `evre_fake_fast` for the bus, auto send and fast streams steps) |
| `evre_map_test` | the map files without a window (25 tests): saved byte for byte, edits, overlays, keys, checks, streams, and the exports, streams included (the C header compiled with gcc, the Python module imported) |
| `tests/schema_test.py` | the maps against the JSON Schema, a stream's refusals, MAP_FORMAT.md's stream keys (needs the `jsonschema` package) |
| `evre_fast_test` | Fast EVRe without a window (22 tests): the block's rules, a fuzz, the clock's fit against a device 200 ppm fast or slow, the fake devices' source, the chart's store of records and its summaries, a recording written and read back |
| `tests/fast_lib_test.py` | the device's helper `lib/fast` compiled with the EVRe library as C++11 to 20 at -O0 to -Os and run, no heap, its stack, its frames through the Studio's parser, PROTOCOL.md's example block (44 checks; needs `g++` and the library) |
| `tests/cli_test.py` | the `evre` command line end to end (51 checks), against its own fake device, a bus of two devices and a fast stream on `evre_fake_fast` (recorded, and checked by `evre check --writes`) |
| `tests/sim_test.py` | `evre-sim` driven with `evre`: every behaviour the map describes, a fast stream included (29 checks) |
| `tests/device_table_test.py` | the device table export compiled with the EVRe library and run (24 checks; needs `g++` and the library) |
| `python/tests/test_evre.py` | the Python package: frames, maps, overlays, answers matched to their requests, a session against `evre-sim`, a bus on `evre_fake_fast`, fast streams' recordings (one made by hand, one `evre record` writes) and a stream live (`dev.stream`) |
| `tests/api_test.py` | the API end to end, in three modes: `readonly`, `writes`, `danger` (24, 25 and 25 checks) |
| `evre_probe` | the protocol core without the window, for checking a real device; it only reads, apart from the login token when `EVRE_TOKEN` is set |
| `evre_fake_fast` | a fast fake device in C++, for measuring the Studio |
| `tests/fake_login_test.py` | the login of both fake devices, and of `evre_probe`, checked the same way (14 checks) |

```sh
python3 tests/fake_device.py &
./build/evre_gui_test
```

The GUI test and the API test write registers, a danger register included: run them only against the fake device.
Chapter 26 of the documentation describes them in full.

## Licence

Apache License 2.0, as the rest of EVRe: `LICENSE` and `NOTICE` at the top of the EVRe repository. The Studio uses
Qt under the LGPLv3; a binary release ships the Qt libraries as separate files with the LGPLv3 text.

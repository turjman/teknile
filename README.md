<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="EVRe/docs/assets/teknile-logo-reversed.svg">
    <img src="EVRe/docs/assets/teknile-logo.svg" alt="teknile" width="320">
  </picture>
</p>

<p align="center">
  <a href="https://github.com/turjman/teknile/actions/workflows/ci.yml"><img src="https://github.com/turjman/teknile/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue.svg" alt="License: Apache-2.0"></a>
  <img src="https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-informational.svg" alt="Platforms: Windows, Linux">
  <img src="https://img.shields.io/badge/C%2B%2B-17-informational.svg" alt="C++17">
  <img src="https://img.shields.io/badge/Qt-6.5%2B-41cd52.svg" alt="Qt 6.5+">
</p>

# teknile

Open tools for embedded devices. The first is **EVRe**, a register protocol for
microcontrollers, with a desktop tool to watch, chart, record and drive any device that
speaks it.

<p align="center"><img src="EVRe/docs/assets/studio.png" alt="EVRe Studio: live registers charted from the example device" width="900"></p>

## What is inside

| Part | What it is |
|---|---|
| **[EVRe&nbsp;protocol](EVRe/docs/PROTOCOL.md)** | A small single-master register protocol: one request returns a whole block of mixed-type telemetry over UART, RS-485, I²C, SPI, USB CDC or TCP. CRC-16, pipelining, broadcast, device-initiated streaming (auto send). |
| **[Device&nbsp;library](EVRe/lib/)** | `EVRe.h` and `EVRe.cpp`: C++, two files, no dependencies beyond `stdint` / `stdlib`, about 3 kB of code. |
| **[EVRe&nbsp;Studio](EVRe/studio/)** | The desktop tool (C++17, Qt 6, Windows and Linux): live registers with decoded bit fields, safe writes, an oscilloscope-style chart with cursors and measurements, math lines, CSV recording, a map editor with exports, several devices on one bus, and an API for Python, MATLAB and LabVIEW. |
| **[Map&nbsp;format](EVRe/studio/docs/MAP_FORMAT.md)** | `evre-map/1`: one JSON file describes a device's registers for every tool, with a [JSON Schema](EVRe/studio/docs/evre-map-1.schema.json). |
| **[Command&nbsp;line](EVRe/studio/cli/)** | `evre` and `evre-sim`: validate and export maps, read, watch and write a device, check a device against its map, and serve a map as a simulated device. |
| **[Python&nbsp;package](EVRe/studio/python/)** | A device by register name with its map, standard library only. |

## Download

The Windows installer, a portable zip, the Linux AppImage and the command-line tools of each version are on the
[Releases](https://github.com/turjman/teknile/releases) page.

## Quick start

Build the Studio (Qt 6.5 or newer with Widgets, Network, SerialPort and Test; CMake 3.21+; a C++17 compiler):

```sh
cmake -S EVRe/studio -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Try it without hardware: start the fake device, then connect the Studio to it.

```sh
python3 EVRe/studio/tests/fake_device.py --port 1210
build/EVReStudio --tcp 127.0.0.1:1210 --map build/maps/example_device.json --connect
```

Windows notes, the command line and every option: [EVRe Studio's README](EVRe/studio/README.md).

## Documentation

| Document | What it holds |
|---|---|
| [Protocol](EVRe/docs/PROTOCOL.md) | frames, function codes, CRC, the register model, errors, test vectors, the device API, a conformance checklist |
| [EVRe&nbsp;Studio&nbsp;guide](EVRe/studio/docs/STUDIO.md) | every feature, the reference, the internals and the tests |
| [Map&nbsp;format](EVRe/studio/docs/MAP_FORMAT.md) | the contract for any tool that reads or writes maps |
| [Examples](EVRe/studio/examples/) | a device from MATLAB, LabVIEW and Python through the Studio |
| [Changelog](EVRe/CHANGELOG.md) | what each version holds |

## Repository layout

```
EVRe/
  docs/        the protocol
  lib/         the device library (C++)
  studio/      EVRe Studio: src/, tests/, docs/, maps/, cli/ (evre, evre-sim), python/, examples/
```

## Contributing

Issues and pull requests are welcome. Read [CONTRIBUTING.md](EVRe/CONTRIBUTING.md) first: every
behaviour change comes with a test and its documentation. Please follow the
[Code of Conduct](CODE_OF_CONDUCT.md), and report security problems as described in
[SECURITY.md](SECURITY.md), not in a public issue.

## License

[Apache License 2.0](LICENSE). EVRe is a protocol of **teknile**; see [NOTICE](EVRe/NOTICE).

# Contributing to EVRe

EVRe is a protocol of **teknile**: a device library (`lib/`), its specification (`docs/PROTOCOL.md`), the map
format (`studio/docs/MAP_FORMAT.md`), and the tools around it in `studio/`: EVRe Studio, the `evre` command line,
the `evre-sim` simulator and the `evre` Python package.

## Before a change

- **The protocol and the map format are contracts.** A change to `docs/PROTOCOL.md` or to `evre-map/1` must keep
  every existing device and map working: the map format grows only by new optional keys (MAP_FORMAT.md, section 2);
  anything else is a new format name.
- **The protocol never interprets data.** EVRe moves bytes; what they mean (types, limits, names, tokens,
  persistence) belongs to the layer above: EVRe Guard, the map, or the device (PROTOCOL.md, "Layers"). Nothing
  that interprets data goes into `lib/EVRe.*`. The library may learn *where* bytes are and *whether* they may be
  read or written, and it may ask the layer above (the handlers) and tell it which frame they run for (the slave
  id, `RX_SLAVE_ID`: framing, not meaning); it never learns *what* the bytes are.
- **One device, many tools.** A map key or a protocol rule is implemented the same way in the Studio, `evre`,
  `evre-sim` and the Python package, and described in MAP_FORMAT.md, the JSON Schema and STUDIO.md.

## The code

- Follow the code around the change: C++17 and Qt 6 in `studio/`, the naming and the comment style there (a comment
  says why, not what). User text goes through `tr()`, non-ASCII text in `QStringLiteral`.
- Device I/O only on the I/O thread (STUDIO.md, chapter 19 and 20.7).
- Build without warnings with GCC on Linux and Windows (`-Wall -Wextra`), and with Qt 6.5 or later.
- LF line ends (`.gitattributes` enforces them, whatever `core.autocrlf` says), UTF-8. The library's files are LF
  too since 1.1.
- Diagrams in documentation: ASCII only (`+ - | < > ^ v`).

## The library

- **The public API only grows.** Every name in `EVRe.h` stays, with its meaning and its value: functions and their
  signatures, macros, enums and their values, struct members, `base_t`, `SALVE_ID_REG`, `crctab16`, the frame index
  macros, and the weak `protocolConfigure` contract. Add, never rename or remove. A new member of `evre_base_t` goes
  after the last one, so every older member keeps its offset; a new error code is appended, below `0xFF`.
  `tests/api_compat.cpp` uses every name the way existing devices and hosts do, and stops compiling when one goes.
  One exception was decided for 1.1: the struct tag `protocol_base` became `evre_base`. `base_t`, `evre_base_t` and
  a `protocol_base` typedef name the struct; `struct protocol_base` no longer compiles (C++ cannot alias a tag).
  And 1.1 parenthesised the frame index and bit macros, so inside a larger expression they mean what they say
  (`DATAx(0) + 1` was 7, it is 8). Both are in PROTOCOL.md, "Migrating from 1.0".
- C++11, for older embedded toolchains. No allocation in the `*Into` forms, and a small stack: the decoder often
  runs in an interrupt. No warning with `-Wall -Wextra` at any optimisation level.
- **The order of the decoder's steps is part of the protocol** (PROTOCOL.md, "The order of the checks"): a change to
  it is a behaviour change, and is announced as one.
- **A change of behaviour brings its proof.** Its class goes into `run_lib_tests.py` (`TRANSCRIPT_CLASSES` for the
  frames of the 1.0 transcript, `FUZZ_CLASSES` and a tag in `fuzz_test.cpp` for the fuzz), and the class checks the
  new answer of every frame it covers. A `FEATURES` check in `lib_test.cpp` fails without the change and passes with
  it. A change that is meant to change nothing (a refactor) passes the fuzz with identical output.

## Tests

Every change comes with its tests, and all of them pass on Linux and Windows (`.github/workflows/ci.yml` runs them):

| Test | Run |
|---|---|
| the&nbsp;library | `python tests/run_lib_tests.py`: the 1.0 transcript, features, EVRe Guard, public names, warnings (`--old DIR`: the 1.0 library, by default the frozen copy in `tests/lib_1.0`) |
| a&nbsp;library&nbsp;change | `python tests/run_lib_tests.py --fuzz <the library before the change> [--fuzz-cases 300000]`: against the library before it, every line the same, or in a decided class and checked |
| the&nbsp;public&nbsp;names&nbsp;alone | `g++ -std=c++11 -Wall -Wextra -Werror -I lib tests/api_compat.cpp lib/EVRe.cpp -o api_compat && ./api_compat` |
| `protocolConfigure` | `tests/configure_test.cpp`: `protocolInit` with a device's own `protocolConfigure`, run by `run_lib_tests.py` on Linux; on MinGW, where an override of a weak function is not reliable, only compiled |
| map&nbsp;files,&nbsp;exports | `build/evre_map_test` |
| JSON&nbsp;Schema | `python studio/tests/schema_test.py` (needs `jsonschema`) |
| command&nbsp;line | `python studio/tests/cli_test.py build` |
| simulator | `python studio/tests/sim_test.py build` |
| device&nbsp;table&nbsp;with&nbsp;the&nbsp;library | `python studio/tests/device_table_test.py build` (needs `g++`; finds `lib/`) |
| Python&nbsp;package | `EVRE_BUILD=build python -m unittest discover -s studio/python/tests` |
| the&nbsp;window | `python studio/tests/fake_device.py &` then `build/evre_gui_test` |
| the&nbsp;API | the Studio with `--api` against the fake device, then `python studio/tests/api_test.py readonly` |

The GUI and API tests write registers, a danger register included: run them only against the fake device or
`evre-sim`, never a real one.

## Documentation

A change to what the Studio does updates `studio/docs/STUDIO.md` and the help pages (`studio/src/ui/help_dialog.cpp`);
a change to a map key updates MAP_FORMAT.md and the schema too. A change to the library updates PROTOCOL.md where a
reader looks for it (the frame, the order of the checks, the errors, the Device API, the checklist, and "Migrating
from 1.0" for anything an existing device or host must know). `CHANGELOG.md` gets a line.

## Making a release

Only the owner tags. A tag builds and publishes everything (`.github/workflows/release.yml`, STUDIO.md 25.5):

1. Set the version in `studio/CMakeLists.txt`: `project(EVReStudio VERSION X.Y.Z ...)`.
2. Give `CHANGELOG.md` a section `## X.Y.Z` (its first line may say the date) with what the version holds: it becomes
   the release's text.
3. Merge both into `main`, then tag that commit and push the tag: `git tag vX.Y.Z` and `git push origin vX.Y.Z`.

The workflow stops with a message when the tag is not the CMake version or the changelog has no such section. A pull
request that touches the workflow or `studio/packaging/` builds the same files as artifacts, to try them first.

## License

EVRe is under the Apache License 2.0 (`LICENSE`, `NOTICE`). A contribution is made under the same license
(section 5 of it). A new source file starts with the line `SPDX-License-Identifier: Apache-2.0` in a comment, as
the others do.

## What never goes in

Names, addresses, maps, paths or data of a particular product or customer: examples use the fake device
(`studio/maps/example_device.json`) or neutral names. Secrets (tokens, keys) never go in a file, a test or a command
line.

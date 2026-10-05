# Contributing to EVRe

EVRe is a protocol of **teknile**: a device library (`lib/`), its specification (`docs/PROTOCOL.md`), the map
format (`studio/docs/MAP_FORMAT.md`), and the tools around it in `studio/`: EVRe Studio, the `evre` command line,
the `evre-sim` simulator and the `evre` Python package.

## Before a change

- **The protocol and the map format are contracts.** A change to `docs/PROTOCOL.md` or to `evre-map/1` must keep
  every existing device and map working: the map format grows only by new optional keys (MAP_FORMAT.md, section 2);
  anything else is a new format name.
- **One device, many tools.** A map key or a protocol rule is implemented the same way in the Studio, `evre`,
  `evre-sim` and the Python package, and described in MAP_FORMAT.md, the JSON Schema and STUDIO.md.

## The code

- Follow the code around the change: C++17 and Qt 6 in `studio/`, the naming and the comment style there (a comment
  says why, not what). User text goes through `tr()`, non-ASCII text in `QStringLiteral`.
- Device I/O only on the I/O thread (STUDIO.md, chapter 19 and 20.7).
- Build without warnings with GCC on Linux and Windows (`-Wall -Wextra`), and with Qt 6.5 or later.
- LF line ends (`.gitattributes` enforces them), UTF-8.
- Diagrams in documentation: ASCII only (`+ - | < > ^ v`).

## Tests

Every change comes with its tests, and all of them pass on Linux and Windows (`.github/workflows/ci.yml` runs them):

| Test | Run |
|---|---|
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
a change to a map key updates MAP_FORMAT.md and the schema too. `CHANGELOG.md` gets a line.

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

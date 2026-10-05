# Working on this repository

`EVRe/` holds the EVRe register protocol: the device library (`EVRe/lib`), EVRe Studio, the
desktop tool (`EVRe/studio`, C++17 / Qt 6, Windows and Linux), their docs and tests.
Apache-2.0. The Studio's full guide is `EVRe/studio/docs/STUDIO.md`; read its chapter 24
(extending the Studio) and chapter 26 (tests) before changing it.

## Rules for every change

- Work on a branch and open a pull request; never push to `main`.
- Each behaviour change gets a GUI check in `EVRe/studio/tests/gui_test.cpp`, and the check
  count in `EVRe/studio/README.md` and STUDIO.md 26.2 is updated.
- Docs move with the code: STUDIO.md (the user part, chapter 22's class and file tables, 23 for
  the chart), the Help pages (`src/ui/help_dialog.cpp`), and a settings row for a new setting.
- User-visible text goes through `tr()`; other literals use `QStringLiteral` / `QLatin1String`.
  Units and symbols are not translated.
- Text files are UTF-8 with LF line endings. Never write the section sign: "section 7.6". Diagrams
  use only `+ - | < > ^ v`.
- No names of private projects, customers or devices anywhere: examples use
  `studio/maps/example_device.json`.
- Change only what the task needs: no refactoring of code around it, no new libraries.
- `EVRe/lib` (the protocol library) is not changed without the owner's OK. The protocol knows
  nothing about what the data means; names, units and limits live in the map (a higher layer).
- Match the code around you: its comment style (sentences about why), naming and idiom.

## Build and test on Linux

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build qt6-base-dev qt6-serialport-dev \
    libgl1-mesa-dev xvfb python3-pip
pip install jsonschema

R=$PWD; S=$R/EVRe/studio; B=/tmp/studio-build    # run from the repository's root
cmake -S $S -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build $B
cd $B
python3 $S/tests/schema_test.py
./evre_map_test
python3 $S/tests/fake_device.py --port 1210 --map $S/maps/example_device.json & FAKE=$!
sleep 1.5; QT_QPA_PLATFORM=xcb xvfb-run -a -s '-screen 0 1920x1200x24' ./evre_gui_test example_device.json
kill $FAKE
python3 $S/tests/cli_test.py $B
EVRE_LIB=$R/EVRe/lib python3 $S/tests/device_table_test.py $B
python3 $S/tests/sim_test.py $B
python3 $S/tests/fake_login_test.py $B
(cd $S && EVRE_BUILD=$B python3 -m unittest discover -s python/tests)
```

The GUI test must end with
"N passed, 0 failed" and no Qt threading warnings (`QObject::`, `QBasicTimer`, `Timers cannot`).

## What cannot be tested here

The graphics-card drawing (`src/ui/gpu_lines.*`, Direct3D) and the display-paced frame clock
(`src/ui/frame_clock.cpp`, DwmFlush) exist only on Windows. Keep them compiling, keep the CPU
path and the card's pictures in step, and say in the pull request what needs a Windows check.

## The pull request

List what changed for the user, the checks added, the tests run with their results, and anything
left for a Windows run.

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
# The cloud environment's setup script installs the tools and Qt 6.8 in /opt/qt/current
# (CMAKE_PREFIX_PATH points there). Elsewhere: build-essential cmake ninja-build libgl1-mesa-dev
# xvfb, Qt 6.5 or newer with SerialPort, and pip install jsonschema.

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

## Work plan (approved by the owner, October 2026)

Work through the phases below **in order, without stopping to ask**: the decisions are taken.
Each phase gets its own branch, made from the previous phase's branch (the first from `main`),
and one pull request into `main`; a later PR says "after #N". Finish a phase completely (code, a
check per behaviour, STUDIO.md, Help, the check count, every Linux test) before starting the next.
If the session has to stop, push what is done and say in that phase's PR where it stopped.

Keep token use lean: read the parts of files you need (grep, then read the lines), not whole
files; STUDIO.md is long, read the chapters a phase touches. Build once per change set, not per
edit. The owner checks each PR on Windows (the graphics card, the frame clock, screenshots) and
merges; never merge yourself.

Decisions already taken: a recording opens in a window of its own; Log Y and Normalise exclude
each other (choosing one turns the other off); the measurement table gets a column chooser;
running totals reset only with the chart's Clear; notes are saved beside a recording in
`<recording>.notes.json`; Arabic uses Western digits and a decimal point.

### Phase 1: readouts and two fixes (branch `phase1-readouts`)

1. **Std dev and peak-to-peak**: `ChartView::Stats` gets `std` and `p2p`, two columns after RMS,
   computed in the same pass with shifted sums (a 12 V line with 1 mV of ripple must read right).
   Right-clicking the measurement table's header shows or hides columns, saved
   (`chart/measureColumns`, all shown by default).
2. **Totals since Clear**: per line, value x dt (trapezoid) from every sample as it is appended
   (`ChartView::append`), not from the kept memory, so trims lose nothing; a gap over 1 s is not
   bridged; reset by the chart's Clear (`clearData`). A "Since Clear" column in hour units
   (`areaUnit(unit, true)`: Ah, Wh, unit-h); the measure panel's line adds "totals since 14:03:12
   (1 h 12 min)" (`durationText`). Math lines included.
3. **Log Y**: the Y range list gets "Log" beside Auto and Manual. `Axes::y` maps log10; decade
   grid lines labelled with SI prefixes (1 u, 10 u ... 1, 10, 100 written as the chart writes
   values) and faint lines at 2..9; Auto spans the positive values in view, at most 9 decades;
   values <= 0 sit on the bottom edge; Manual min and max must be > 0 in Log. Both drawing paths
   take y from `Axes`, so the card follows: keep it that way, also for the memory strip.
4. **Frame clock on battery**: on a laptop on battery `DwmFlush` returns every ~76 ms (13 Hz) and
   the whole window ran at 13 fps. In `FrameClock`: when DwmFlush comes back later than 34 ms three
   times in a row, tick from the 16 ms timer and keep calling DwmFlush on the thread; when it comes
   back within 25 ms ten times in a row, stop the timer. Put the decision in a small
   platform-independent piece (fed the wait times, says timer or not) with a GUI check that runs on
   Linux; the DwmFlush call stays Windows-only.
5. **Info line** (`ChartTab::refreshStatus`): when it does not fit, drop whole parts, in this order:
   the paint time, the word "plotted", the delay; never cut letters; the full text in the tooltip.

### Phase 2: files (branch `phase2-files`)

1. **Right-click menu on the chart** (there is none yet): Copy picture, Save picture... (PNG of the
   chart as shown, painted by the CPU path so it works while the card draws), Export to CSV...
   (the view, or A -> B when both cursors are placed: the samples kept, plotted and math lines,
   in the recording's format `time_s, datetime, NAME [unit]`; samples of different lines closer
   than a fraction of their interval share a row; a big export runs on a thread with progress and
   Cancel), Add note here, Open recording...
2. **Open a recording**: also "Open recording..." beside "Record CSV" in the sidebar's Polling &
   recording card, a .csv dropped on the window, and the last 8 files. It opens a window of its own
   holding a second `ChartTab` (it needs only a clock, the RegDefs, `plotRegister` and
   `frame(samples)`) fed from the file: held (no Live), titled with the file's name and span, its
   own settings prefix; several can be open; the live chart keeps running. The file is read on a
   thread with progress; columns by their title `NAME [unit]`; with a map loaded, value names and
   fields are matched by register name; byte-array columns skipped; an empty cell is no sample;
   the RAM budget applies (a bigger file: ask to keep the last part). Exports open the same way.
3. **Notes**: right-click, Add note here: a labelled marker at that time, drawn by the CPU and as a
   picture on the card like the cursor tags; drag to move, double-click to edit, Delete to remove.
   Saved in `<recording>.notes.json` beside a recording (written while recording and with exports,
   read by the recording window).

### Phase 3: lanes (branch `phase3-lanes`)

Display menu, Lanes: one plot per unit, stacked, equal heights, at most 8 (further units share the
last); each lane its own Y range (Auto, Manual, Log, by right-clicking its value labels); one time
axis; cursors, the A-B bar and notes across all lanes; one crosshair box; the memory strip as now.
Both drawing paths: the card draws every lane in one frame (each lane's `Axes` when the segments are
made).

### Phase 4: analysis (branch `phase4-analysis`)

1. **Histogram and spectrum**: right-click a line's legend chip, Histogram or Spectrum over A -> B
   (or the view): a small window with its own plot and a cursor readout. Histogram bins by
   Freedman-Diaconis. Spectrum: resample to even steps at the mean rate (polls are uneven), Welch
   with a Hann window and 50 % overlap, up to half the rate, amplitude in the line's unit; a
   radix-2 FFT of our own (no new library); export picture and CSV.
2. **Trigger** (Display menu): a line (register or math) crossing a level, rising, falling or
   either; the level a dashed line you can drag; Single holds on the first, Normal holds each and
   re-arms; the trigger point at 20 % of the window; the view holds there with a marker;
   measurements and export work on it.

### Phase 8: languages (branch `phase8-languages`)

1. CMake `qt_add_translations` (LinguistTools), `translations/*.ts`, the .qm embedded; Qt's own
   `qtbase_xx.qm` loaded; a Language choice at the bottom of the sidebar beside Help and the theme
   switch (System, English, العربية), setting `ui/language`, applied at the next start with a
   "Restart now" button.
2. The Help pages through the same files: `TOPICS` html in `QT_TRANSLATE_NOOP`, `tr()` when shown;
   a test that every translation keeps the English's HTML tags, `%CODE%` markers and `%1`
   placeholders.
3. Arabic: the window right-to-left; the chart, legend, bit view, Monitor and hex text, numbers,
   units and register names stay left-to-right; plural forms; translate every string and the Help
   (about 10,200 words). Attach a review sheet (an HTML table, English | Arabic) to the PR, not
   committed.
4. Checks: every .ts complete (no unfinished entries), placeholders and tags match, the main
   window's minimum width at most 1280 px in each language; the GUI test stays in English.

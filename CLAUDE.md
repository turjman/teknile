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

## For the human eye (the owner's rule for every screen, chart, table and diagram)

Judge each change by what a person sees, not only by what the code does; the owner reviews the
screenshots.

- **Grouping is visible.** Separate things look separate: stacked plots, panels and groups have a
  separator, a gap or a frame, so two axes never read as one (the lanes' "15 10 5 15 10 5").
- **Readable sizes.** A plot, lane or list keeps enough height for its labels (at least two value
  labels on an axis); when things do not fit, scroll or fold rather than squeeze; text is never
  cut mid-letter, overlapping or drawn over lines.
- **Every action is discoverable.** A click target looks clickable (a button shape, an icon such
  as "▾" / "▸", a pointing-hand cursor and a highlight on hover) and has a tooltip; nothing is
  reachable only by a right-click, a modifier key or a hidden spot. A right-click menu repeats
  what is visible, it is not the only way.
- **Both themes and both languages.** Check contrast in dark and light (faint grey on faint grey
  fails), and the layout in English and Arabic (right-to-left, longer words).
- **Tables and diagrams in the docs.** A short cell (a name, a key, a setting, a value) stays on one
  line (`&nbsp;` between its words; longer text goes in the last column); every table has a header
  row; an ASCII diagram is aligned in a monospaced block and fits in 100 columns.
- **Show it.** A PR that changes what is seen says what to look at, and the Windows run adds
  dark and light screenshots of it.

## Build and test on Linux

```sh
# The cloud environment's setup script installs the tools and Qt 6.8 in /opt/qt/current
# (CMAKE_PREFIX_PATH points there). Elsewhere: build-essential cmake ninja-build libgl1-mesa-dev
# xvfb, Qt 6.5 or newer with SerialPort and LinguistTools, and pip install jsonschema.

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

Phases 1-4 and 8 are merged into `main`. **Phase 9 is open** (branch `phase9-lanes-fit`, PR #7).
**Then, in this order: phase 0, phase 7, phase 10**, each on its own branch made from the one
before (phase 0's from `phase9-lanes-fit`) and its own PR ("after #7", ...). Phases 5 (fast streams)
and 6 (macOS) are kept for later: do not start them.

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

### Phase 9: lanes that fit (branch `phase9-lanes-fit`)

The owner's picture: 8 units on a laptop screen, each lane about 50 px high, one or two value
labels, five lines on top of each other, and the shared last lane ("% · ...", 0-200) flattening
the lines in it. Lanes must stay readable however many units are plotted.

1. **A lane per unit, a minimum height, scrolling.** `MAX_LANES` and the shared last lane go:
   every unit gets its own lane. An open lane is at least `LANE_MIN_H` = 80 px high (so it has
   at least two value labels); when the lanes do not fit, they keep that height and the lanes
   scroll up and down inside the plot (`laneScroll_`, px from the top, clamped whenever the
   layout changes: resize, a line added or removed, a fold; set back to 0 when Lanes is turned
   on). Scrolling: the wheel over the lanes' value labels (left of the plot; Ctrl + wheel there
   stays the lane's Y zoom, the wheel over the plot stays the time zoom), and a scroll bar painted
   in the right pad (`RIGHT_PAD`), only when the lanes do not fit, draggable, a click above or
   below its handle moves one plot height. Painted, not a `QScrollBar` widget: nothing goes over
   the card's layer. A lane cut by the plot's top or bottom edge is drawn cut (lines, grid, value
   labels and its unit name only inside the plot); a lane out of view is not drawn at all and has
   no rect for `laneAtY`. The time axis, the memory strip, the cursors, the A-B bar, the notes,
   the crosshair and its box stay on the whole plot; the trigger's level line and tag only when
   the trigger's lane is in view. `plotLayout()` returns every lane with its rect in widget
   coordinates after the scroll; the drawing paths skip or cut by the plot rect.
2. **Fold a lane.** A click on a lane's unit name (the rotated text left of its value labels)
   folds it; a click anywhere on a folded lane opens it again; the lane menu (right-click on the
   value labels) gets "Fold lane" / "Open lane" too. A folded lane is a strip `LANE_FOLDED_H` =
   22 px high: its unit, then for each of its lines the colour dot, the name and the latest value
   in view (as the legend writes it), cut with "..." when they do not fit; no grid, no lines, no
   value labels; the crosshair box and cursor readouts skip it. Open lanes share the height left
   (equal, at least `LANE_MIN_H`). Folds are kept by unit, saved under the tab's prefix
   (`settingKey("lanesFolded")`, a string list), so a recording window keeps its own. A click on
   a folded strip does not place a cursor or add a note.
3. **Both drawing paths.** The card already cuts each line to its lane's rows: cut to the lane's
   rect intersected with the plot rect, skip lanes out of view, and draw a folded strip's dots
   and text on the CPU only (as text is now). Keep the card's picture equal to the CPU's.
4. **API for the tests and the Chart tab**: `laneFolded(int)`, `setLaneFolded(int, bool)`,
   `laneScroll()`, `setLaneScroll(double)`, `laneContentHeight()` (all lanes stacked, gaps
   included), `laneScrollBarRect()` (empty when the lanes fit); `laneRect` may now be partly or
   wholly outside the plot.
5. **Checks** (in `gui_test.cpp`, Linux and Windows): with the example map, plot lines of at
   least 10 units (add math lines with other units if the map has fewer): 10 lanes, none shared,
   each at least 80 px; the content is taller than the plot, the scroll bar shows; the wheel over
   the value labels scrolls by a step and is clamped at both ends; a drag of the scroll bar's
   handle scrolls; a lane out of view gets no `laneAtY`; a click on a unit name folds that lane
   (22 px, the other lanes taller, the strip lists its lines with values), a click on the strip
   opens it; the fold is saved and comes back after `setLanes(false)` / `setLanes(true)` and in
   a new `ChartTab`; with few lanes nothing scrolls and no bar shows. On Windows the card's
   picture check: scrolled half a lane and one lane folded, the strips compared as in the
   existing lanes check.
6. **Docs and texts.** STUDIO.md (the lanes section of the chart chapter, 23 for the layout, the
   settings row for `lanesFolded`, the check count), the Help's chart page, the header comment
   on `MAX_LANES` replaced. Every new text in `translations/evre_studio_ar.ts` with its Arabic
   (the glossary in `translations/README.md`: lane مسار; commands as verbal nouns, so "Fold lane"
   طيّ المسار, "Open lane" فتح المسار), no unfinished entries.
7. **Make it visible (the owner's review of PR #7, on the same branch and PR).** The owner did not
   find the fold: a rotated unit name looks like a label, and the lane menu is only a right-click.
   - A **fold button on every lane**: a "▾" at the top of each open lane's unit column (above its
     rotated unit name), matching the "▸" a folded strip already shows; a click on either folds or
     opens (the unit name and the strip keep working as now).
   - **Hover**: over the button, the unit name and a folded strip, the pointing-hand cursor, the
     button drawn highlighted, and a tooltip "Fold lane" / "Open lane".
   - **The hint** in the chart's state corner (beside "cursors: click / drag"): with Lanes on, "lanes:
     ▾ folds" instead of "lanes".
   - **Display menu**: "Fold all lanes" and "Open all lanes" under Lanes, shown only with Lanes on,
     each disabled when there is nothing to do (a way back when everything is folded).
   - **Tooltip on the lanes' value labels**: "Wheel: scroll the lanes · Ctrl + wheel: zoom this lane ·
     Right-click: its Y range and Fold lane" (only the parts that apply: no scrolling when they fit).
   - Both drawing paths: the buttons are drawn as the unit names are (CPU text, the card as now), the
     card's picture check stays at its level.
   - Checks: the button's rect per lane, a click on it folds and on the strip's opens, the hover
     cursor and tooltip, the hint text, Fold all / Open all (enabled states, folds saved), the value
     labels' tooltip with and without scrolling. Docs (STUDIO.md 7.12, the mouse table, chapter 23),
     the Help's chart page, the check count, the Arabic texts (glossary terms), no unfinished entries.
     Push to `phase9-lanes-fit` (PR #7) and add a short "UI/UX follow-up" section to the PR's text.
8. **A separator between lanes (the owner: the lanes read as one chart).** The value labels run on
   ("15 10 5 15 10 5") as if one axis. In each gap between two lanes (open or folded) a 1 px line,
   centred in `LANE_GAP`, from the left edge of the value labels to the plot's right edge, in the
   theme's border colour (a step stronger than the grid lines; a theme token if there is none),
   both themes. Only between lanes in view (cut by the plot like the lanes); none without Lanes.
   Both drawing paths (the card: with the grid's segments), the picture check at its level. A check
   that the separators are where the gaps are (their y per gap) and in the colour, the docs (7.12,
   23), and a Help sentence.
9. **Markdown tables: no short cell on two lines (the owner, after the root README fix 406a99d).**
   On GitHub a table squeezes its columns, and a short name such as "EVRe protocol" or "Ctrl + wheel"
   wraps onto two lines. Go through every tracked `*.md` (about 1,200 table rows, 981 in STUDIO.md)
   and fix each table the way 406a99d fixed `README.md` (look at its diff):
   - In every column but the last (the prose one), a cell of up to about 32 visible characters is
     kept on one line: `&nbsp;` between its words, outside backticks (`Ctrl&nbsp;+&nbsp;wheel`,
     `**[Map&nbsp;format](...)**`, `` `chart/lanes`&nbsp;(bool) ``). Links and code stay as they are.
   - A first-column cell longer than that: shorten it to a name and move the rest (a code name,
     a remark) to the start of the next column, as "Map format `evre-map/1`" became "Map format".
   - A header row that is empty (`| | |`) gets real column names.
   - Change nothing else: no rewording, no reordering; the tables' content and the anchors stay.
   - Check: a small script (not committed) that lists every table cell of a non-last column with a
     plain space and at most 32 visible characters must list none; the Python test that reads the
     README's check count, the Help test and every Linux test still pass. One commit, "Docs: tables'
     short cells on one line", pushed to PR #7, and a line about it in the PR's text.

### Phase 0: speed of the cursor drag and the fill (branch `phase0-speed`)

Measured on the owner's laptop (4K at 225 %, NVIDIA T1000, 64 lines, Normalise on, the view held):
dragging a cursor runs at 54-56 fps with a 1 min window but 45 fps with a 5 min window. While a
cursor is dragged only A, B and B - A are measured (round 8); the rest of the cost is not known yet.
The owner's Windows run measures; this phase gives it the tool and fixes the known waste.

1. **A timing aid**, `EVRE_PERF_LOG=<file>` (a test aid like `EVRE_SHOT`, nothing when unset): every
   500 ms one line: frames painted per second, the paint's average and longest ms, its stages
   (binning, the card's segment list, the present, the marks, the legend), the measure table's
   update ms and count, polls per second. Documented with the other test aids (STUDIO.md 26).
2. **A held view where only the marks move reuses its lines.** Dragging a cursor, a note or the
   trigger's level over a held view changes no line: keep the frame's binned lines and the card's
   line segments (CPU: the lines' picture) and rebuild only the marks and sprites. The key: the
   view's times, the plot's size and dpr, every Y range (lanes included), Normalise / Log, the
   lines' generation, the theme, the lanes' scroll and folds. A test hook counts binnings; a check:
   20 drag steps over a held view bin 0 times, a change of any key bins again, the picture is the
   same as without the reuse (CPU and, on Windows, the card).
3. **The measure table during a drag**: one repaint per update (updates off while the cells are
   written), `measureInfo_` set only when its text changes; a check on the counts.
4. **The fill**: report, with the timing aid, the longest paint while 64 lines fill at 1000 Hz for
   3 minutes (a line's array grows by copying). Fix it only if one paint is over 30 ms and the fix
   is small (no new storage scheme); otherwise say so in the PR.
5. Docs (STUDIO.md 23 for the reuse, 26 for the aid), Help only if something visible changes. In
   the PR: what the owner's run should measure (drag at 1 min and 5 min, with and without
   Normalise, CPU and GPU), and the before numbers above.

### Phase 7: installers and releases (branch `phase7-installers`)

1. **`.github/workflows/release.yml`**: on a tag `v*` it builds and publishes; on a pull request that
   touches the workflow or `EVRe/studio/packaging/` it builds the same files as workflow artifacts and
   publishes nothing; `workflow_dispatch` too. It never runs on other pushes, so it cannot turn a
   push to `main` red. On a tag, it first checks that the tag equals the CMake `project(VERSION)` and
   that `EVRe/CHANGELOG.md` has that version's section, and stops with a clear message if not.
2. **Windows** (the same Qt 6.8.3 MinGW as `ci.yml`): Release build, `windeployqt` with the compiler
   runtime and the translations (`qtbase_ar.qm` too); an **Inno Setup** installer
   (`packaging/windows/evre_studio.iss`): per-user by default (no admin), Start menu entry, optional
   desktop icon, uninstaller, version from CMake; it holds the Studio, `evre`, `evre-sim`, the
   example map, README, LICENSE. Also a portable `.zip` of the same folder.
   `EVReStudio-<version>-setup.exe` and `EVReStudio-<version>-windows.zip`.
3. **Linux**: an **AppImage** built on `ubuntu-22.04` (an older glibc, so it runs on more systems)
   with `linuxdeploy` and its Qt plugin, a `.desktop` file and the icon;
   `EVReStudio-<version>-x86_64.AppImage`, and `evre-tools-<version>-linux-x86_64.tar.gz` with
   `evre` and `evre-sim`. A check in the workflow: the AppImage starts under xvfb with `EVRE_SHOT` and
   writes its picture.
4. **The icon**: there is none yet. Make a simple one (the Studio's accent colour, "EV" in white, a
   rounded square) as SVG plus `.ico` (16-256 px) and PNGs, set as the window icon and the
   executable's icon (a `.rc` on Windows). Say in the PR that it is a placeholder for the owner's
   branding.
5. **The release's text**: that version's section of `EVRe/CHANGELOG.md`; the files above; a note that
   the installer is not code-signed (Windows SmartScreen asks "Run anyway").
6. **Docs**: STUDIO.md's installing section (installer, zip, AppImage, building from source), the
   README (a "Download" line pointing to Releases), `EVRe/CONTRIBUTING.md` "Making a release" (bump
   the CMake version, the CHANGELOG section, tag `vX.Y.Z`, push the tag). Do not tag or publish
   anything yourself: the owner tags.
7. In the PR: the artifacts' names and sizes from the PR's own run, for the owner's install test.

### Phase 10: small items (branch `phase10-small`)

1. **No A-B bar sliver**: `ChartView::spanBar` draws the bar when the span between the tags is at
   least 2 px, even when its text does not fit and is put beside the tags; then only a sliver shows.
   Draw the bar only when the text fits inside it; otherwise only the text's tag beside. A check.
2. **Drag a lane's border** (after phase 9's separators): over a separator the cursor becomes the
   vertical-resize cursor (the human-eye rules: a highlight on hover, a tooltip "Drag: this lane's
   height · Double-click: equal heights"); dragging it resizes the lane above (the one below gives or
   takes; neither under `LANE_MIN_H`); the heights kept by unit under the tab's prefix
   (`settingKey("laneHeights")`), a double-click on a separator sets all back to equal. Both drawing
   paths; checks (drag, limits, saved, double-click, the card's picture on Windows); docs, Help,
   Arabic.

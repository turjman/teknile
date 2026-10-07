/* SPDX-License-Identifier: Apache-2.0 */
/* The help pages: plain HTML kept in the source, one per topic, styled with the
 * theme's colours each time a page is shown (so they follow a theme switch). */
#include "ui/help_dialog.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <iterator>

#include "io/engine.h"
#include "ui/theme.h"

namespace {

struct Topic {
	const char *title; /* translated when listed */
	/* UTF-8; %CODE% opens a code block (a styled <pre>), which the page closes with </pre>; translated when shown, as
	 * the title (translations keep the tags, %CODE% and the other %NAME% markers) */
	const char *html;
};

const Topic TOPICS[] = {
	{ QT_TRANSLATE_NOOP("HelpDialog", "Getting started"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Getting started</h2>
<p>EVRe Studio talks to any device that speaks <b>EVRe</b>, over <b>TCP</b> or a <b>serial / USB</b> port: one
device, or several on one link. It shows every register of the device's <b>map</b> live, charts them, records them
to CSV, writes them, and lets other programs reach the device through it (<i>API</i>).</p>
<ol>
<li><b>Connection</b> (left): choose TCP or Serial / USB, fill in the address or pick the port, <b>Connect</b>.
The pill turns green and the device ID appears under it (<i>not read yet</i> until it is).</li>
<li><b>Registers</b> tab: the values update at the poll rate (<i>Polling &amp; recording</i>).</li>
<li>Tick <b>Plot</b> on registers and open the <b>Chart</b> tab.</li>
<li><b>● Record CSV</b> writes every register ticked <b>Log</b>, once per poll.</li>
</ol>
<p>The last map you opened loads by itself (the first time: the first one in <code>maps/</code>; a map given on the
command line is for that run only). For another device, <b>Device map → Open…</b>, or build one: <b>New</b>, then
<b>+ Register</b>.</p>
<h3>What else is there</h3>
<ul>
<li><b>Registers</b>: search, groups (tick several), <b>Plot shown</b> (Plot on every register shown, as many as the
chart holds at the poll rate: <i>Chart &amp; recording</i>); select a register to see it in full and to <b>write</b>
it quickly under the table (a value, its enum list, its bits drawn as in a datasheet).</li>
<li><b>Chart</b>: like an oscilloscope: memory depth apart from the view, <b>Hold</b> / <b>Live</b>, <b>Measure</b>
(min, max, mean, RMS, the area under a line: W → Wh), <b>Cursors</b> A and B, <b>ƒ Math</b> lines (SUPPLY_V *
SUPPLY_I), and <b>Display</b>: how the lines are drawn, by a graphics card when the computer has one.</li>
<li><b>Monitor</b>: every frame sent and received, raw reads and writes.</li>
<li><b>Map editor</b>: make a map from nothing or change one, check it, and export it (a specification, a C header,
a Python module).</li>
<li><b>Log</b>: what happened (connects, writes and their results, errors), also saved to a file; warnings and errors
pop up beside the tabs for a few seconds.</li>
<li>Several devices on one link (RS-485, a gateway): <b>Connecting</b>.</li>
<li>Slower polls than asked? <b>Polling &amp; speed</b> explains reads, latency and <b>In flight</b>.</li>
<li>At the bottom of the sidebar: <b>Help</b> (F1), the switch to the <b>light</b> or <b>dark</b> theme, and the
<b>Language</b> (System, English, العربية), applied at the next start.</li>
<li><b>Keyboard</b>: Tab and Shift+Tab move between the controls, and a ring shows where you are (a click shows
none). Every key and mouse action: <b>Keys &amp; mouse</b>.</li>
</ul>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Connecting"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Connecting</h2>
<h3>TCP</h3>
<p>Host and port of any EVRe-over-TCP server: a device with a network port, a TCP gateway to a device,
or another EVRe Studio's API (port 1219).
A device that asks for a <b>token</b> gets it from the token box: after connecting, the Studio writes it to
the <b>login register</b> the map declares (<code>"login"</code>, see <i>Device maps</i>). It is not stored.
With no login register in the map the token is not sent, and the Log says so.</p>
<h3>Serial / USB</h3>
<p>Ports are listed with their USB IDs. A map that names its device's USB ID (<code>"usb"</code>) marks
that port with the device's name, and picks it by itself when no port was chosen before.
Baud does not matter for USB CDC; it does for a real UART.
A port can be open in <b>one program</b> only: close other tools first ("Access denied" otherwise).</p>
<h3>Options</h3>
<ul>
<li><b>Slave</b>: the EVRe address (1 for most devices). 0 is the broadcast address, which no device answers.
On a bus each device has its own (below).</li>
<li><b>Timeout</b>: how long an answer may take. Slow radio links need more.</li>
<li><b>Interval</b> (Polling): time between polls, down to 0.05 ms; <b>max</b> (0) polls back to back.
Type it, or use the arrows / wheel: they step by a tenth (10 → 9, 1 → 0.9 → … 0.1 → 0.09).
0.25 ms is 4000 polls/s: the device and the link must keep up, the poll rate below says what they do,
and <i>why</i> when it is slower than asked (in amber).
Polls run on their own thread: the window drawing never slows them, and CSV gets every one.</li>
<li><b>Show values</b> (Polling): how often the numbers on screen change, in the table and in the chart's
legend: 2, 5, <b>10</b> (default) or 30 per second, or every frame. A number that changes at every frame
cannot be read. The polls, the CSV file and the chart's lines are not slowed by it.</li>
<li><b>In flight</b>: requests sent before their answers come. <b>1</b> for a device on a UART;
<b>4</b> (default for TCP) pipelines them, so a poll of several blocks costs one round trip
instead of one per block. Kept separately for TCP and serial.
A poll is one read per block, so polls <b>overlap</b> only with In flight ≥ 2 × blocks: with 3 blocks
and In flight 4 there is one poll at a time and the rate is 1 / latency (≈ 200/s at 5 ms over Wi-Fi).
Faster needs about <i>polls/s × latency</i> polls at once: In flight 12 (4 polls) for ~800/s at 5 ms.
More in <b>Polling &amp; speed</b>.</li>
<li><b>Reconnect by itself</b>: after a lost link, try again (0.5 s after a drop, every 2 s after a failed
attempt).</li>
</ul>
<h3>Several devices on one link</h3>
<p>An RS-485 line or a gateway may carry several devices, each at its own slave address. <b>Devices on the
link</b> (sidebar): <b>New bus</b> makes a bus of the map's device; <b>+ Device</b> adds one (a name, a slave
address, a map: several devices may share one); <b>Bus file &gt; Save</b> keeps it as a bus file
(<code>evre-bus/1</code>), opened again at the next start. A row of the list reads <i>D1 · slave 1 · map</i>.</p>
<ul>
<li>Every register is named after its device: <b>D1_SUPPLY_V</b>, <b>D2_STATUS</b>. The chart, math lines, CSV and
the API use those names: any device's register by its name alone.</li>
<li>The Registers tab shows the device selected in the list, or <b>All devices</b> (its own picker); the Map
editor edits the selected device's map, says which devices share it, and <b>Live values from</b> picks whose values
its live line shows. Every picker shows a device as the list does: its dot (green answers, red offline, grey not
connected), then <i>D1 · slave 1</i>; a long name is cut, its row's tooltip has it whole.</li>
<li>A device with a token of its own: type it in its <b>Edit…</b> dialog (never saved).</li>
<li>A device that misses %OFFLINE_MISSES% answers in a row is <b>offline</b> (red dot): left out of the polls, asked
again every %OFFLINE_RETRY_S% s, so the others keep their rate. Its timeouts are not logged again while it is offline;
another device's are.</li>
</ul>
<h3>Broadcast</h3>
<p>Slave <b>0</b> reaches every device at once, and none answers. The Studio sends one only when it means the same to
every device: into the reserved bank's writable registers (<code>0xA004</code> … <code>0xA105</code>: CONFIG, the
messages) always; anywhere else only when every device on the link has the same map. Never one that switches
CONFIG's AUTO_SEND on: every device would send by itself at once, over the others. <b>To all devices</b> in quick
write broadcasts the value typed, then reads every device back; the Monitor's Slave <b>0</b> sends raw bytes; the
<b>Broadcast</b> menu of the Devices card keeps broadcasts by name in the bus file (<i>Stop all</i>), each sent after a
confirmation, then each device read back. They are offered while connected with <b>Allow writes</b> on; a preset's
tooltip says why when it is not.</p>
<h3>If it does not connect</h3>
<ul>
<li><i>Connection timed out</i>: wrong address, or this PC is not on that network.</li>
<li><i>Connects, then drops after a few seconds</i>: the server wants a token and it is missing or wrong, or the
map has no <code>"login"</code> to send it to (see the Log).</li>
<li><i>Access denied</i> (serial): another program holds the port.</li>
<li>Registers "not available": the device refused the address (a register the map has and this device
or this link does not). Not an error.</li>
</ul>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Polling & speed"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Polling &amp; speed</h2>
<p>A <b>poll</b> reads every register of the map once. The registers are not asked one by one: they are
merged into a few <b>block reads</b>, and the sidebar says how many: <i>15 registers in 3 reads</i>.</p>
<h3>How the reads are made</h3>
<ul>
<li>Registers in the same <b>256-byte page</b> (the same high byte of the address: 0xA0.., 0xD0..) with
gaps of <b>8 bytes or less</b> between them become <b>one read</b>.</li>
<li>A read never crosses a page: devices often keep separate banks there, and some refuse a read across them.</li>
<li>If the device still refuses a block, it is split into single registers; an address it does not have is
dropped (<i>not available</i>) and not asked again.</li>
<li>Byte arrays larger than 32 bytes (a message buffer) are not polled: right-click → <b>Read now</b>.</li>
</ul>
<p>The number of reads comes from the map's layout, not from a setting. Registers in the same 256-byte page
with gaps of at most 8 bytes are read together, so a map whose registers sit in three separate address ranges
needs three reads per poll. The example map (<code>maps/example_device.json</code>) is one:
<code>0xA000–0xA006</code> (protocol: DEVICE_ID, STATUS, CONFIG…), <code>0xD000–0xD015</code> (UPTIME … PRESSURE)
and <code>0xD080–0xD087</code> (SETPOINT … MOTOR_SPEED); the last two share a page, but more than 8 bytes lie
between them.</p>
<h3>What sets the rate</h3>
<ul>
<li><b>Interval</b>: the poll you ask for (10 ms = 100 polls/s, 1 ms = 1000/s, <b>max</b> = back to back).</li>
<li><b>Latency</b> (status bar): how long one answer takes: well under 1 ms on USB or this PC, 1–5 ms over Wi-Fi.</li>
<li><b>In flight</b>: how many requests may wait for their answers at once. A poll is one request per read, so
polls can overlap only when In flight covers several of them: <i>polls at once = In flight ÷ reads</i>.
With 3 reads and In flight 4, one poll at a time: the rate is 1 ÷ latency (1000/s at 1 ms, 200/s at 5 ms).
In flight 12 lets 4 polls overlap: up to 4× that.</li>
</ul>
<p>When the polls are slower than asked, the status bar says why, in amber (hover it for the whole text):</p>
<ul>
<li><i>Slower than asked: a poll is 36 reads, sent 1 at a time…</i> or <i>…In flight 4 runs 1 poll at once…</i>:
raise <b>In flight</b> to the number it suggests.</li>
<li><i>Slower than asked: … ms per answer is the link's limit</i>: In flight is already high enough; the link or the
device is the limit (a longer interval, or fewer registers in the map).</li>
<li><i>… on the serial link …</i>: a serial link answers one request at a time; a longer interval, fewer registers, or
a faster baud rate.</li>
</ul>
<p>Measured: 4000 polls/s to a fast test device on this PC. Over a link with several ms per answer the rate
follows the rule above, polls at once ÷ latency: one poll at a time at 5 ms per answer is about 200 polls/s.
A <b>serial</b> port (a UART) takes one request at a time: keep In flight at <b>1</b> there. In flight is kept
separately for TCP and serial.</p>
<p>Polls run on their own thread: the window, the chart and the table never slow them, and the CSV and the
chart get every poll.</p>
<h3>Auto send</h3>
<p>A device whose STATUS has <b>CAP_AUTO_SEND</b> can send its read-only block (0xD000 on) by itself, with no
request and no answer time. Tick <b>Auto send</b> in the polling card and pick a rate: the 16 the device makes
exactly, 8000 Hz ÷ (prescaler + 1), from 4000 Hz down to 40 Hz (the prescaler is the device timer's reload, never 0;
default 100 Hz; the rate is remembered, the tick is not: it changes the device). Not offered, the greyed list says why
(<i>not connected</i>, <i>not offered</i>, <i>not on a bus</i>). No frame within 2 s (a gateway that does not pass
them on): it is switched off again, the Log says so, and the registers are polled.</p>
<ul>
<li>The Studio writes CONFIG: AUTO_SEND (bit 3) and the prescaler, MSG_ENABLE kept, SYS_RESET and DFU 0.</li>
<li>Each frame fills the table and is one chart point and <b>one CSV row</b>; the polls read only the rest.
The sidebar shows both, frames and polls a second: <i>Auto send 99.8/s · 10.0 polls/s</i>.</li>
<li>CONFIG is read every 100 ms meanwhile, even with Poll off, so the device's host watchdog stays fed. If the
device clears it by itself (a reset), the Log says so once and the box unticks.</li>
<li>On a <b>serial</b> link the frames may take at most 70% of the link: a faster rate is lowered, and the Log
says so.</li>
<li><b>Disconnect</b> (and closing the Studio) switches it off on the device first. After a lost link it is switched
on again by itself.</li>
<li>One device only: on a bus the box is disabled (devices sending by themselves would collide), and a broadcast
that switches AUTO_SEND on is refused.</li>
</ul>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Fast streams"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Fast streams</h2>
<p>A device that takes samples on its own clock (a current at 100 000 samples a second, say) can send them in
numbered <b>blocks</b>, with no request and no answer time: <b>Fast EVRe</b>, a layer above the protocol. Each block
says the number of its first sample, so every sample lost on the way is counted, never filled in.</p>
<p>The map describes the streams, in <code>"streams"</code>: where the blocks come from (a window of the device bank),
how fast, the register that switches the stream, and what one sample holds (its channels):</p>
%CODE%"streams": [
  { "name": "ADC", "addr": "0xDC00", "size": 1024, "rate": 10000,
    "rate_reg": "ADC_RATE", "enable": "ADC_STREAM",
    "channels": [ { "name": "I_LOAD", "type": "i16", "unit": "A", "scale": 0.0005 },
                  { "name": "V_BUS", "type": "i16", "unit": "V", "scale": 0.001 } ] } ]</pre>
<p>In the Map editor they are on <b>Map settings…</b>, the <b>Streams</b> page: each stream's window, rate and
channels, the map's checks under them. <code>evre check --writes</code> switches each on for 2 s and checks its START,
its numbers and its rate; Python reads one live with <code>dev.stream('ADC')</code>.</p>
<p>A map with streams shows the <b>Fast streams</b> card in the sidebar, a row for each:</p>
<ul>
<li>Each stream's row is headed by its name. <b>▶ Start stream</b> writes 1 to the stream's enable register (after
reading its rate register, if the map names one); the button turns red, <b>■ Stop stream</b>, which writes 0. A
stream without an enable register is only
listened to. Not remembered: every stream is off at every start.</li>
<li>Under it: the samples a second as the Studio's clock measures them, with the correction in parts in a million
(<i>10.0 k samples/s (+32 ppm)</i>), and the samples lost (<i>lost 1 024</i>, in amber).</li>
<li>CONFIG is read every 100 ms while a stream runs, even with Poll off, so the device's host watchdog stays fed.</li>
<li><b>Disconnect</b> (and closing the Studio) switches every stream off on the device first. After a lost link it
is switched on again by itself.</li>
<li>No block within 2 s: switched off again, and the Log says so. A device that stops a stream by itself (a reset)
is told once in the Log.</li>
<li>One device only: on a bus the card is greyed (a device sending by itself would collide with the others).</li>
<li>The Monitor names a block <i>READ_RESP (fast stream ADC)</i>.</li>
</ul>
<p>Under the stream, each channel has a <b>Plot</b> tick and its newest value. Ticked, the channel is a line on the
chart, <i>ADC.I_LOAD</i>, like a register's: its legend chip, its lane, the crosshair, the cursors. Every sample
keeps its own time: a view of an hour shows the lowest and highest sample of each pixel column, so a spike of one
sample in millions is never hidden, and zoomed in (down to 10 µs: the wheel, or type <code>50 us</code> in Window)
each sample is a point of its own. Where samples were lost the line breaks; the mouse over the gap says how many.
The samples are kept as they came, a few bytes each, within the chart's RAM, where a fast line counts as one line.</p>
<p>A fast line is measured as any line: its row in <b>Measure</b> (nothing across a gap; a cursor in a gap reads —),
its total since Clear, its histogram and spectrum (the spectrum takes the samples as they are, over the longest part
without a gap), the trigger and Export to CSV (a row per sample).</p>
<p><b>Fast math lines</b>: a formula over the channels of one stream, <code>ADC.I_LOAD * ADC.V_BUS</code> in W, is
computed for every sample of that stream at its own time (<b>ƒ Math → New math line…</b>; the dialog says <i>computed
for every record of stream ADC</i>). A register in it is held at its last polled value. It is drawn, measured,
triggered and exported as a fast line, and one of the chart's lines. Channels of two streams are refused: two
streams, two clocks.</p>
<p><b>Recorded</b>: while <b>Record CSV</b> runs, each stream that sends is written beside the CSV as it came,
<code>run.csv</code> and <code>run.ADC.evrs</code>. <b>Open recording</b> opens the CSV with them on one time axis, or a
<code>.evrs</code> alone; the file is mapped, not read into memory, so a recording larger than the RAM opens, and one
cut off opens up to its last whole piece. Python reads one with <code>evre.read_recording</code>.
<code>evre record</code> (the command-line tool) writes a stream's blocks to a <code>.evrs</code> file as they came.
Not to be mixed up with <b>Auto send</b> (the read-only block at a timer's rate, the <i>Polling &amp; speed</i>
page) or the API's <code>stream</code> command (values at a period for an API client).</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Registers & writes"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Registers &amp; writes</h2>
<p>One row per register: address, name, <b>value</b> (bold), unit, <b>decoded</b> bit fields or enum name,
type and access. Search box and group filter at the top. Hover a row for its description, raw bytes,
the decoded fields one per line, and its age.</p>
<p>A value with bit fields or an enum name has an <b>ⓘ</b> beside it: hover the ⓘ to see them decoded, one per line
(the rest of the value shows its usual tooltip). The <b>Decoded</b> column is off by default (most registers have
none); right-click the table or its header → <b>Decoded column</b> to show it.</p>
<p>Nothing is cut off: the columns fit their longest text (a scroll bar if the window is narrower), and the
<b>selected register</b> is shown in full under the table (value, raw bytes, every decoded field,
description). Right-click → <b>Fit columns</b> shrinks them back after a long value.</p>
<ul>
<li>A value that just changed <b>glows</b> for a moment.</li>
<li>A value <b>grey</b> is not refreshed: older than two poll intervals, or one interval plus the timeout
when that is longer, plus the time between two updates of <b>Show values</b> (polling off, link lost).</li>
<li><i>error</i> / <i>not available</i> in red: the read failed / the device does not have that address.</li>
</ul>
<h3>Writing</h3>
<ol>
<li>Tick <b>Allow writes</b> (off at every start).</li>
<li>Double-click an <b>rw</b> value, type, <b>Enter</b>. Numbers, <code>0x1F</code>, <code>0b101</code>, or an enum
name.</li>
</ol>
<h3>Quick write</h3>
<p>Select an <b>rw</b> register: under the table, <b>Write NAME</b> has</p>
<ul>
<li>a value box: type, <b>Enter</b> or <b>Write</b>;</li>
<li>for an enum register, a list of its values: pick one and it is written;</li>
<li>for a register with bit fields, its <b>bits drawn as in a datasheet</b>: a cell per bit (click to flip that
bit only), each flag by its name (click to flip it), and each wider field with its value (click for its named
values, or <i>Value…</i>);</li>
<li><b>Bits</b>: the same bit view for an integer register without fields, the highest bit first.</li>
</ul>
<p>On a bus, <b>To all devices</b> sends the value typed to every device in one broadcast frame, where the
broadcast rule allows it (see <b>Connecting</b>), after a confirmation, and reads each one back; the Log says which
devices took it. It is offered once a value is typed.</p>
<p>A bit or field is written read-modify-write from the value the device holds, so the other bits are kept.
Every quick write goes the same way as a table edit: <b>Allow writes</b> must be on, ⚠ registers ask first.</p>
<ul>
<li>Registers marked <b>rw ⚠</b> move, power or change something: every write asks first.</li>
<li>The table never shows what you typed, only what the device reports: the register is read back at once
(a clamped value reads clamped), and every poll re-reads it.</li>
<li>If the device, or another client, changed the value while you were editing, you are asked before
yours overwrites it.</li>
</ul>
<p>Right-click a row: <b>Plot</b> (or <b>Remove from chart</b>), <b>Plot a field</b> (a register with bit fields: one
field as a line of its own), <b>Read now</b>, <b>Copy value</b>, <b>Edit definition…</b>, <b>Remove</b>. Anywhere in
the table: <b>Plot all shown</b>, <b>Remove shown from the chart</b>, <b>Remove all from the chart</b>, <b>Fit
columns</b>, <b>Decoded column</b>, <b>Log all</b> / <b>Log none</b>, <b>Add register…</b>.</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Chart & recording"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Chart &amp; recording</h2>
<p>Tick <b>Plot</b> on any numeric registers (a register the map marks fixed, an ID or a setting, has no Plot box).
The chart shows them on one time axis, with the latest value of each in the legend. Move the mouse over it to read
every line at that moment: a box beside the mouse holds every line's value (its numbers change at the <b>Show
values</b> pace, as the legend's, and the box keeps its size).</p>
<p>Each line keeps its place in the legend; only its digits change, at the <b>Show values</b> pace
(10 per second by default) while the line itself moves at every frame. When the lines do not all fit, scroll
the legend with the <b>mouse wheel</b> over it, the <b>bar</b> under it, or the arrows at its ends. The top right
says what holds the view or changes its reading (held, Y log or manual, cursors, the trigger); when the row is short
it drops its hints first (Live to follow, click / drag), never runs over the legend, and its tooltip has it whole.</p>
<p><b>How many lines</b>: the chart takes 64,000 samples a second, so 64 registers at 1000 polls a second, 32 at
2000, 16 at 4000 (with <i>Auto send</i>, at its rate). Past that a Plot tick is refused, and when the rate goes up
the lines plotted last come off; the status bar and the Log say which. <b>One cap of 64 lines</b> holds for every
kind: registers, math lines and fast stream channels (the <i>Fast streams</i> page) count together, and a 65th of
any kind (a Plot, a channel's tick, a field, <i>New math line…</i>, a math line's <i>Shown</i>) is refused with the
same words in the status bar. A fast channel is one of the 64 lines, not of the samples a second. The info line
shows how many lines are on the chart of how many it may hold: <i>32/64 plotted</i>.</p>
<h3>The first row: what is shown and kept</h3>
<ul>
<li><b>Window</b>: how much time is shown. Pick one, or type any length: <code>45</code> (seconds),
<code>2.5 s</code>, <code>500 ms</code>, <code>3 min</code>, <code>1 h</code>. The <b>mouse wheel</b> on the chart
zooms it, around the mouse when held.</li>
<li><b>Memory</b>: how much is kept (as an oscilloscope's memory depth), <b>Window</b> the part shown: keep 1 min,
look at 10 s. <b>Drag</b> the chart to look back through the memory, or click / drag on the <b>memory strip</b> under it
(the whole memory depth, the view marked; while it fills up, the data grows from the right and the strip says how
much is kept). Drag its box, or at a short window the handle drawn on it; the <b>wheel</b> over it moves the view a
window earlier or later.</li>
<li><b>RAM</b>: the most memory the samples of all the lines take together (2 GB by default; pick one or type any
size, 3000 or 3 GB). With many fast lines the memory holds less than asked, and the strip says so in amber:
<i>RAM budget reached: keeping the last 4.0 min of 30.0 min</i>, and while a recording runs <i>· the recording
keeps everything</i> (the chart lets the oldest go, the recording's file keeps every sample). The oldest eighth goes
at a time, so the time kept steps down by an eighth and fills up again (12 min, 10.5, 12).
Beside it, a note says what the lines need for the Memory set (<i>needs 1.4 GB</i>), in amber with what fits when that
is more than the RAM (<i>needs 2.8 GB, keeps 22 min</i>). The RAM is a cap, not a reservation: with less memory free
than it, the chart keeps within what is free (leaving 1 GB, or a tenth of the computer's memory), so its oldest go
before the computer pages to disk, and the note says so in amber (<i>only 2.1 GB free: keeps about 40 s</i>). The
Studio itself takes about 150 MB more than the RAM set.</li>
<li><b>Y range</b>: <b>Auto</b> follows what is shown (grows at once, shrinks gently: no jumping), or
<b>Manual</b> with the min and max typed beside it (typing one sets Manual). <b>Ctrl + wheel</b> zooms Y
around the mouse; a <b>double-click</b> goes back to Auto. <b>Log</b>: a logarithmic scale, a line at each decade
(<i>1 µ, 10 µ … 1, 10, 100 … 100 k</i>), faint ones at 2 to 9: Auto spans the positive values shown (9 decades at
most), or type a min and max above 0; values of 0 or less sit on the bottom edge. Log and <b>Normalise</b> exclude
each other: choosing one turns the other off.</li>
</ul>
<h3>The second row: what to do</h3>
<ul>
<li><b>Hold</b> stops the view where it is (the memory keeps filling); <b>▶ Live</b> follows now again. Dragging
holds too. While the trigger is on, the same button is <b>Stop</b> / <b>▶ Run</b> (below).</li>
<li><b>Measure</b> shows the measurements under the chart (below). <b>Cursors</b> on (turns Measure on): click the
chart for cursor <b>A</b>, again for <b>B</b>, drag them. A bar between their tags at the top of the plot says the
time between them (<i>3.525 ms</i>, <i>12.35 s</i>, <i>1 min 23.4 s</i>); with a cursor off the view it ends at the
plot's edge, and when the cursors are too close for the text, the text stands beside the tags. A tag's tooltip gives
its clock time; while the trigger holds the view on a crossing, how far from <b>T</b> too (<i>T -0.250 ms</i>), as an
oscilloscope's cursors read, and so does the line over the measurements (<i>A: T -0.250 ms · B: T +1.750 ms</i>).
<b>Clear cursors</b> removes them, and so does turning <b>Cursors</b> off.</li>
<li><b>ƒ Math</b>: lines made from a formula (below).</li>
<li><b>Display</b>, a menu of how the lines are drawn (its tooltip says what is on): <b>Normalise</b>, every line
scaled to its own range, to compare shapes of different units; <b>Lanes</b>, a plot per unit stacked under each other,
a line between two lanes, each at least 80 px high: when they do not fit they scroll (the wheel over their values, or
the bar at the right); a click on a lane's ▾ (above its unit name) or on its unit name folds it into a strip of its
lines and their values, a click on the strip opens it again, and <b>Fold all lanes</b> / <b>Open all lanes</b> under
Lanes do it for all; drag the line between two lanes to make the one above taller or lower (a double-click on it: all
equal again; the lanes always fill the plot, none lower than 80 px); each lane has its own Y range: a click on a lane's ⋯ (under its ▾), or a right-click on its values, gives
Auto, Manual…, Log, All lanes: Auto or Fold lane, Ctrl + wheel over it
zooms it, a double-click sets it to Auto; a lane not in Auto has a <i>Manual</i> (amber) or <i>Log</i> tag at the top of
its values, and a click on the tag sets it back to Auto; a click on a lane's values makes it the current lane, whose
range the Y range row shows and sets (<i>Y range (A)</i>), and <b>All lanes: Auto</b> under Lanes sets them all back;
one time axis, the cursors, notes and crosshair across them all;
<b>Smooth</b> (on by default): the picture is delayed
by a few ms (measured from how late samples arrive, shown in the info line), so the line always reaches the right
edge and scrolls without steps; <b>Hover values</b> (on by default): the box of values beside the mouse, off to see
only the crosshair and its dots; <b>Drawing</b>: who draws the plot: <b>Auto</b> (a dedicated graphics card when there
is one, else the CPU), a card by name, or the <b>CPU</b>. A card draws the whole plot (the lines, the grid, the
cursors, the crosshair) and shows it itself: many fast lines at the display's rate, 64 lines of 1000 Hz at 60 frames
a second on a 4K screen. A card takes a moment to start (up to a second while it wakes): the CPU draws meanwhile. If
the card fails, the CPU takes over and the Log says why. The processor's own graphics is offered too, but on a large
screen it draws slower than the CPU.</li>
<li>The info line, left of <b>Clear</b>: the lines on the chart of how many it may hold, the math lines, frames drawn
per second and the time one takes, the Smooth delay, and who draws (<i>GPU</i> or <i>CPU</i>). When it is narrow,
whole parts go (the time to draw, the word <i>plotted</i>, the delay first); its tooltip holds all of it. Frames follow the
display refresh; while frames take long, one is skipped now and then (as many as needed), so the rest of the window
always answers.</li>
<li><b>Clear</b> empties the lines and the memory and starts the totals again; <b>Remove all</b> takes every register
off the chart.</li>
</ul>
<p>From a 1 s window up the time labels are the clock time and move with the lines. Below 1 s the time axis is an
oscilloscope's: 10 fixed divisions whose lines stand still while the wave moves, labelled by their offset from the
right edge (<i>-8 ms</i> … <i>0</i>), or from <b>T</b> while the trigger holds the view on a crossing (<i>0</i> under
it, <i>+4 ms</i>). At the axis's right end <i>1 ms/div · 14:03:12.345</i> says a division's length and the clock time
at 0; the wheel steps the window through 1, 2 and 5 per division. <b>Display → Time grid</b>: Auto (divisions below
1 s), Clock times or Divisions. The crosshair shows the time and how long ago (held on a crossing: how far from T, <i>T +1.234 ms</i>). Long windows and fast lines are drawn
from min/max summaries, so they cost no more than short ones.</p>
<h3>Measurements</h3>
<p><b>Measure</b> (off by default) shows a table under the chart, for every line: the value at cursor <b>A</b> and
<b>B</b>, <b>B − A</b>, and over A → B (or over the view without cursors) the <b>min</b>, <b>max</b>, <b>mean</b>,
<b>RMS</b>, the <b>standard deviation</b> (the ripple, whatever the level: 12 V with 1 mV of ripple reads 0.707 mV),
<b>peak to peak</b> and the <b>area under the line</b> (∫ value dt, by trapezoids between the samples): a power in
<b>W</b> gives <b>J</b> and <b>Wh</b>, a current in <b>A</b> gives <b>A·s</b> and <b>Ah</b>. The splitter above the
table moves. While a cursor is dragged, A, B and B − A follow it; the rest is measured again once it is let go.</p>
<p><b>Since Clear</b>: each line's total since the chart's <b>Clear</b>, in Wh, Ah or unit·h, summed from every sample
as it comes, so it covers hours while the memory keeps minutes; a gap of more than a second between samples adds
nothing. The line above the table says since when: <i>totals since 14:03:12 (1 h 12 min)</i>. A line taken off the
chart and put back keeps its total.</p>
<p><b>Right-click the table's header</b> to show or hide its columns; the choice is kept.</p>
<h3>Histogram and spectrum</h3>
<p><b>Click a line's chip</b> in the legend (its <b>▾</b>) or right-click it: <b>Histogram</b> (how its values spread, bins by the
Freedman–Diaconis rule) or <b>Spectrum</b> (which frequencies it holds, as amplitudes in its unit: a 2 V sine reads
2 V; resampled to even steps, Welch with a Hann window, up to half the rate), over A → B or the view, in a window of
its own with a readout under the mouse, a picture and CSV.</p>
<h3>Trigger</h3>
<p><b>Click a line's chip → Trigger on this line</b> (or <b>Display → Trigger</b>): the chart holds when that
line crosses its level, as an oscilloscope. The level is a dashed line in the line's lane from its <b>T▸</b> marker
left of the chart to its tab right of it, <i>0.4 A ↑</i> (the level in the line's unit and the edge), so nothing
covers the newest samples; their pointers are solid while the picture held crossed this level. <b>Drag</b> the
marker, the tab or the line to move the level; <b>click</b> the tab's arrow (↑ ↓ ↕) for rising, falling or either;
their tooltips name the line. A level beyond the lane's range stays on its edge, dotted, their pointers ▲ or ▼ and
<i>(above range)</i> in their tooltips. Each line keeps its own level and edge.</p>
<p><b>Auto</b> holds on each crossing; when none comes for a window's length after the hold-off, it runs live until
the next. <b>Normal</b> holds on each crossing and stays held until the next one, however long. <b>Single</b> holds on
the first crossing and stops; <b>Arm</b> for another. Normal and Single wait on a still picture; only Auto rolls.
While they wait, <b>Force</b> (in Arm's place) holds the view now, as if the line crossed; Normal waiting after a
capture says when the last one was. <b>Find level</b> puts the level halfway between the line's lowest and highest
in view. The
<b>hold-off</b> (the window's length by default, 0 to
10 s) is the time after a crossing in which no other counts. The <b>T ▼</b> flag above the chart, its arrow over
the crossing, is its place in the window (its tooltip says when the line crossed and at what level), 50 % by default: drag it (0 to 90 %), double-click it for 50 % again. In a window under a second the next
picture shows once it is whole, so a repeating wave stands still. A fast line's crossing is found as its blocks come.
The row under the actions sets the same; the measurements, export and pictures take the view held.</p>
<p>While the trigger is on, <b>Hold</b> / <b>Live</b> is <b>Stop</b> / <b>Run</b>: <b>Stop</b> holds the picture
and its T, no crossing counts (the button's <b>Run</b> is amber while stopped); <b>Run</b> arms again in the mode,
from now. Dragging the chart stops it too. The row
and the chart's top right say the same state (<i>Normal · waiting</i>, <i>Normal · triggered</i>, <i>Stopped · Run
to arm</i> ...), and change only when it does, with no number while it runs; while the view still fills after the
crossing, a faint line marks where the data ends.</p>
<p><b>Short windows lock by themselves</b>: below a 100 ms window a live chart with the trigger off holds on each
rising crossing of its busiest line's middle (a fast line first, else the one with the most samples in the window; a
line with fewer than 20 there is not watched) (<i>Auto (short window)</i>; <i>Auto · free running</i> while it does not
cross; a blue badge in the corner), so a wave stands still instead of blurring. Your trigger takes over when it is on, Hold ends it, and
<b>Display → Lock short windows</b> turns it off.</p>
<p><b>Off</b> in the row turns the trigger off, as unticking <b>Display → Trigger</b> or the chip's
<b>Trigger on this line</b>, which is ticked for the line watched. A line watched that leaves the chart stops the
trigger (<i>no line to watch</i>, its name greyed in the row's list); it arms again when the line comes back, never
on another line by itself.</p>
<h3>Math lines</h3>
<p><b>ƒ Math → New math line…</b>: a name, a unit and a formula over register names, e.g. <code>SUPPLY_V *
SUPPLY_I</code> in W (the power; its area is the energy). <code>+ − * / ^ ( )</code>, <code>pi</code>, and abs sqrt
exp log log10 sin cos tan asin acos atan atan2 min max pow floor ceil round sign clamp, and bits(x, lsb, width) for a
bit field (right-click a register with fields → <i>Plot a field</i> makes such a line). Type a few letters of a name and
a list offers the registers (with their units) and functions: Up and Down pick, Enter or Tab takes one, Esc closes it;
a function goes in as <code>name()</code>, the cursor inside. It is drawn and measured like
a register, from the same polls; the registers it reads are sampled for it even when they are not plotted. Kept for
the next start; the menu shows, edits and removes them.</p>
<p>A formula over the channels of one fast stream (<code>ADC.I_LOAD * ADC.V_BUS</code>) is computed for every sample
of that stream, a register in it held at its last polled value: see <i>Fast streams</i>.</p>
<h3>CSV</h3>
<p><b>● Record CSV</b> asks for a file, then writes one row per poll (one per frame with <i>Auto send</i>):
<code>time_s</code> (since start), <code>datetime</code>, then every register ticked <b>Log</b> (all by default), as
the values shown (scaled). Columns are fixed when the recording starts. <b>■ Stop recording</b> closes the file.</p>
<h3>Right-click on the chart</h3>
<ul>
<li><b>Copy picture</b>, <b>Save picture…</b> (PNG): the chart as shown, drawn by the CPU.</li>
<li><b>Export to CSV…</b>: the samples of every line over the view, or between the cursors A → B when both are
placed, in the recording's format (a row per poll); a big one runs on its own with a progress bar and Cancel.</li>
<li><b>Add note here</b>: a labelled marker at that time, a tag at the bottom of the plot. Drag the tag to move it,
double-click it to edit, click it and press <b>Delete</b> to remove it. While recording, the notes are written beside
the file (<code>run.csv.notes.json</code>), and an export takes the notes of its span.</li>
<li><b>Open recording…</b> and <b>Recent recordings</b> (also <b>Open</b> beside Record CSV, or drop a .csv on the
window): a recording or an export in a window of its own, with its chart, measurements, notes and math lines of its
own; the live chart goes on. A file bigger than the chart's RAM asks to keep its last part. With a map loaded, its
registers' value names and fields are matched by name: <b>Lines</b> plots a register's field.</li>
</ul>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Device maps"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Device maps (JSON)</h2>
<p>A map lists a device's registers. <b>Open…</b> / <b>Save</b> / <b>Save as…</b> / <b>New</b>; it is made and
changed on the <b>Map editor</b> tab (its own help page). The file is plain JSON, and a save changes it only where
it was edited:</p>
%CODE%{ "format": "evre-map/1", "device": "My device", "device_id": "0x1001", "slave": 1,
  "usb": { "vid": "0x1234", "pid": "0xABCD" },
  "login": { "addr": "0xF000", "size": 16 },
  "registers": [
    { "addr": "0xD004", "name": "SUPPLY_V", "type": "f32", "unit": "V",
      "access": "ro", "group": "Power", "desc": "supply voltage" },
    { "addr": "0xD010", "name": "STATE", "type": "u16", "access": "ro",
      "fields": [ { "name": "MODE", "bits": "1:0",
                    "values": { "0": "idle", "1": "run", "2": "fault" } } ] },
    { "addr": "0xD085", "name": "LED_MODE", "type": "u8", "access": "rw",
      "enum": { "0": "off", "1": "on", "2": "blink" } },
    { "addr": "0xD086", "name": "MOTOR_SPEED", "type": "i16", "access": "rw", "danger": true }
  ] }</pre>
<ul>
<li><b>type</b>: <code>u8 i8 u16 i16 u32 i32 f32</code>, or <code>bytes</code> with <code>"size"</code>. Little
endian.</li>
<li><b>scale</b> / <b>offset</b>: shown = raw × scale + offset (writes are converted back).</li>
<li><b>format</b>: <code>"hex"</code> shows the value in hex.</li>
<li><b>danger</b>: <code>true</code> = confirm every write, and API clients need the ⚠ switch.</li>
<li><b>plot</b>: <code>false</code> = a fixed value (an ID, a command): no Plot box, left out by Plot shown.</li>
<li><b>usb</b> (top level): the device's USB VID/PID, to mark and pick its port.</li>
<li><b>login</b> (top level, optional): the register the token box is written to after connecting, and its size in
bytes (the token is cut or padded with zeros to it). Without it, no token is sent.</li>
<li><b>access</b>: <code>ro</code>, <code>rw</code>, or <code>wo</code> (written only, never polled);
<b>write</b>: <code>action</code> or <code>w1c</code>; <b>persist</b>: kept across a reset.</li>
<li><b>min</b> / <b>max</b> / <b>default</b> (shown units), <b>decimals</b>; <b>special</b>:
<code>{ "-1": "not measured" }</code>, names for single values of a number.</li>
<li><b>notes</b> (a register, the map), <b>protocol</b> (transport, baud, tcp_port, timeout_ms),
<b>groups</b> (notes per group), and on a field <b>access</b> and <b>desc</b>.</li>
<li><b>extends</b>: <code>"base.json"</code> makes the map an overlay that changes another one.</li>
<li><b>streams</b> (top level, optional): a device's fast streams, each its window, rate, switches and channels
(the <i>Fast streams</i> page).</li>
<li>Keys the Studio does not know are kept. <code>docs/evre-map-1.schema.json</code> describes the format.</li>
</ul>
<p>Registers close together are read in one request (same 256-byte page, gaps up to 8 bytes).
A block the device refuses is split; an address it refuses is dropped.</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Map editor"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Map editor</h2>
<p>Make a map from nothing, or change one. Every change is an undo step: <b>Undo</b> (Ctrl+Z), <b>Redo</b>
(Ctrl+Y).</p>
<ul>
<li><b>The table</b>: one row per register, edited in place (double-click or type). With several rows selected, a
cell set in one of them is set in all (a bulk edit). The red or amber dot: what the checks found.</li>
<li><b>+ Register</b>, <b>Duplicate</b> (Ctrl+D), <b>Delete</b> (Del); Ctrl+C / Ctrl+V copy and paste registers as
JSON, also between maps.</li>
<li><b>The form</b> at the right: <i>General</i> (type, access, write behaviour, group, persist, danger, plot,
scale, decimals, min, max, default), <i>Values</i> (value names, special values; <i>Paste lines</i> takes "0 off"),
<i>Bit fields</i> (drag across bits to make a field, click one to edit it), <i>Notes</i>. The card over them shows
the name, address, type, access and the LIVE value read the way it is being defined (the dot: green with a value,
amber past a limit, grey without one; hover it for a long value in full).
With no register selected, a note in the middle says so. A page the register cannot have (<i>Bit fields</i> of
a bytes or f32 register, <i>Values</i> with several selected) has a warning sign on its tab: hover it, or open the
page, for why.</li>
<li><b>Checks</b> under the table: names used twice, registers sharing bytes, fields past the bits, min above
max… Click one to go there.</li>
<li><b>Map settings…</b>: device, IDs, slave, USB, login, protocol, notes on the map and its groups, and the fast streams (the <i>Streams</i> page).</li>
<li><b>Export</b>: a Markdown specification, a C header, a Python module or CSV, for whoever implements or uses
the device, and the <i>device table</i> for firmware on the EVRe library (the images, their addresses checked, a
bind function; the library needs every read-only register below the writable ones). <b>Import CSV…</b> reads a
sheet back.</li>
</ul>
<p>On the Registers tab, <b>+ Register</b> and <i>Edit definition…</i> come here. The live values stay while a
register is edited, as long as it is read the same way.</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "API (MATLAB, LabVIEW, Python)"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>API: other programs through the Studio</h2>
<p>Tick <b>Serve API</b>. The Studio then shares the device it is connected to: every request goes through
the Studio's one queue, so clients and the Studio never collide on the port.</p>
<table cellpadding="4">
<tr><td><b>1219</b></td><td>EVRe pass-through: the same frames as the device. Existing EVRe clients work
unchanged.</td></tr>
<tr><td><b>1220</b></td><td>JSON lines by register name: send one JSON object per line, get one back.</td></tr>
</table>
<p>Only this PC can connect, unless <b>Network</b> is ticked.
<b>Writes</b> are refused until <b>Allow API writes</b> is ticked; ⚠ registers also need
<b>including ⚠ registers</b>. Neither is remembered.</p>
<p>Several devices on the link: the JSON names carry the device's (<code>D2_SUPPLY_V</code>); port 1219 sends a frame
to the slave it names.</p>
<h3>JSON commands</h3>
%CODE%{"cmd":"info"}
{"cmd":"list"}
{"cmd":"get","names":["SUPPLY_V","STATE"]}
   -> {"ok":true,"values":{"SUPPLY_V":12.05,"STATE":5},
       "decoded":{"STATE":"MODE=run  READY"}}
{"cmd":"set","values":{"LED_MODE":2}}      -> {"ok":true,"values":{"LED_MODE":2}}  (read back)
{"cmd":"stream","names":["SUPPLY_V","SUPPLY_I"],"ms":50}
   -> {"t":1790170000.12,"values":{"SUPPLY_V":12.0,"SUPPLY_I":0.8}}  every 50 ms
{"cmd":"stop"}
{"cmd":"read","addr":"0xD000","count":16}  -> {"ok":true,"hex":"..."}
{"cmd":"write","addr":"0xD085","hex":"02"}
{"cmd":"broadcast","name":"D1_SPEED","value":0}  every device at once, then each read back</pre>
<p>Add <code>"id"</code> to any request: it comes back in the answer. Errors: <code>{"ok":false,"error":"..."}</code>.
Names are the map's (any case), or an address such as <code>"0xD00C"</code>.</p>
<h3>MATLAB</h3>
%CODE%c = tcpclient("127.0.0.1", 1220);
configureTerminator(c, "LF");
writeline(c, jsonencode(struct("cmd","get","names",{{"SUPPLY_V","SUPPLY_I"}})));
r = jsondecode(readline(c));
r.values.SUPPLY_V</pre>
<h3>Python</h3>
%CODE%import json, socket
s = socket.create_connection(("127.0.0.1", 1220)); f = s.makefile("rw")
f.write(json.dumps({"cmd": "get", "names": ["SUPPLY_V"]}) + "\n"); f.flush()
print(json.loads(f.readline())["values"]["SUPPLY_V"])</pre>
<h3>LabVIEW</h3>
<p><i>TCP Open Connection</i> (127.0.0.1, 1220) → <i>TCP Write</i> the JSON text plus <code>\n</code> →
<i>TCP Read</i> in <b>CRLF</b> mode → <i>Unflatten From JSON</i> into a cluster
(e.g. <code>ok</code> boolean, <code>values</code> cluster with a <code>SUPPLY_V</code> double).</p>
<p>Full examples: <code>examples/</code> next to the program's source.</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Monitor"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Monitor</h2>
<p><b>Log frames</b> shows every frame sent and received (off by default: at fast polling it is a lot of text).
<b>Clear</b> empties the list.</p>
<p>The request row sends one raw request: <b>READ</b> a <b>Count</b> of bytes from an <b>Address</b>, or <b>WRITE +
ack</b> / <b>WRITE (no ack)</b> its <b>Bytes</b> (needs <i>Allow writes</i>): the value as hex bytes, the low byte
first, so <code>2C 01</code> writes 300 (0x012C). <b>Send</b>, or <b>Enter</b> in the address box. The answer comes
in the list: the bytes read, or <i>OK</i> with the time it took. A WRITE without ack shows <i>sent</i>: no answer comes
to say it arrived; READ it to see.</p>
<p><b>Slave</b>: the device it goes to (on a bus, the devices by name). <b>0 (broadcast)</b>, or <i>Broadcast · slave
0</i> on a bus, is a broadcast: a WRITE only, as the broadcast rule allows (see <b>Connecting</b>), which every device
takes and none answers. A device chosen again gets back the function chosen before.</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Log & pop-ups"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Log &amp; pop-ups</h2>
<p>The <b>Log</b> tab lists what happened, with the time: connecting, connected, lost (and why), the
device ID, a token refused, every <b>write</b> (register, value, bytes, address) and its result: written,
<b>refused</b> by the device (with its reason), not written (a bad value), cancelled. Also read errors
of a register (when they start and when it reads again), timeouts, error answers, bad frames, maps
loaded, CSV recordings, the API server.</p>
<ul>
<li>Warnings in amber, errors in red. While you are on another tab, the tab title counts them: <b>Log (3)</b>.</li>
<li>Every line also goes to <code>logs/studio_&lt;date&gt;.log</code> beside the program (one file a day);
<b>Open folder</b> shows it.</li>
<li><b>Show info</b> off: only warnings and errors in the tab (the file keeps everything).</li>
<li><b>Pop-ups</b>: warnings and errors also pop up for a few seconds (5 s, an error 8 s) in the free space
beside the tabs (right of them, or left of them in Arabic), where they cover nothing; <i>Show in Log</i> opens the tab and closes the pop-up. They do not
block anything. When the window is too narrow for them there, the status bar shows them instead.</li>
</ul>
<p>No spam: the same line again and again (a reconnect every 2 s) is counted, not repeated; the same message
(numbers aside) pops up at most every 30 s; one pop-up is shown at a time, the newest, with <i>+N more</i> for the
others that came while it was up. All of them are in the Log; hover the pop-up for its full text.</p>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Command line"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Command line</h2>
%CODE%EVReStudio [--tcp host:port | --serial COMx[:baud]] [--map file.json | --bus bus.json]
           [--plot NAME,NAME] [--tab registers|chart|monitor|map] [--connect]
           [--interval ms] [--inflight n] [--record file.csv] [--api] [--api-writes] [--api-writes-danger]</pre>
<ul>
<li>A server token comes from the environment variable <code>EVRE_TOKEN</code>, never from the command line.</li>
<li><code>--map</code> or <code>--bus</code> (several devices on the link) is for that run only: the next plain start
opens the one chosen last.</li>
<li><code>--plot</code> ticks Plot on those registers. One the map marks fixed, or past what the chart holds at the
poll rate, is left off, and the Log says which.</li>
<li><code>--inflight n</code> sets <b>In flight</b> (requests sent before their answers come), as typing it in
the box does.</li>
<li><code>--record</code> starts a CSV recording at once: every poll, as with the button.</li>
<li><code>--api-writes</code> is the same as ticking <i>Allow API writes</i>; <code>--api-writes-danger</code> also
the ⚠ switch.</li>
</ul>
)HTML") },
	{ QT_TRANSLATE_NOOP("HelpDialog", "Keys & mouse"), QT_TRANSLATE_NOOP("HelpDialog", R"HTML(
<h2>Keys &amp; mouse</h2>
<table cellpadding="4">
<tr><td><b>Anywhere</b></td><td><b>F1</b> this help · <b>Tab</b> / <b>Shift+Tab</b> the next / previous control</td></tr>
<tr><td><b>Registers</b></td><td><b>Double-click</b> or <b>F2</b> edits an rw value, <b>Enter</b> writes it, <b>Esc</b>
cancels · right-click: the menu of the row and the table</td></tr>
<tr><td><b>Chart</b></td><td><b>Wheel</b> zooms the time (below 1 s: the next 1, 2 or 5 per division) · <b>Ctrl + wheel</b> zooms Y around the mouse (Manual) ·
<b>double-click</b> Y back to Auto · <b>drag</b> looks back through the memory, and holds · with <b>Cursors</b> on, a
<b>click</b> places A, then B, a <b>drag</b> moves the nearer; the mouse on a cursor's tag: its time (from T while
the trigger holds the view)</td></tr>
<tr><td><b>Legend</b></td><td><b>Wheel</b> over it, its <b>bar</b> or its <b>arrows</b> scroll it when the lines do not
all fit</td></tr>
<tr><td><b>Memory strip</b></td><td><b>Click</b> / <b>drag</b>: the view goes there, and holds (drag its box or
handle: from where it was) · <b>wheel</b>: a window earlier or later</td></tr>
<tr><td><b>Chart, right-click</b></td><td>pictures, Export to CSV, Add note here, Open recording</td></tr>
<tr><td><b>Note</b></td><td><b>drag</b> its tag to move it · <b>double-click</b> to edit · <b>click</b>, then
<b>Delete</b> to remove</td></tr>
<tr><td><b>Measurements</b></td><td><b>Right-click</b> the header: show or hide columns</td></tr>
<tr><td><b>Legend</b></td><td><b>Click</b> or <b>right-click</b> a line's chip (its ▾): its Histogram or
Spectrum, Trigger on this line</td></tr>
<tr><td><b>Trigger</b></td><td><b>Drag</b> its level's T▸ marker (left of the chart), tab (right of it) or dashed line · <b>click</b> the tab's
arrow: rising, falling, either · <b>drag</b> the T ▼ flag above the chart: where the crossing sits · <b>double-click</b>
it: 50 %</td></tr>
<tr><td><b>Lanes</b></td><td><b>Click</b> a lane's ⋯ or <b>right-click</b> its values: its Y range (Auto, Manual…,
Log, All lanes: Auto), Fold lane · <b>click</b> its values: the current lane (the Y range row) · <b>click</b> its
Manual or Log tag: Auto ·
<b>wheel</b> over the values scrolls the lanes · <b>Ctrl + wheel</b> zooms the lane · <b>double-click</b> it or its
values: Auto ·
<b>click</b> its ▾ or unit name: fold it, the strip: open it · <b>drag</b> the line between two lanes: their heights
(<b>double-click</b> it: equal)</td></tr>
<tr><td><b>Map editor</b></td><td><b>Ctrl+Z</b> / <b>Ctrl+Y</b> undo / redo · <b>Ctrl+D</b> duplicate · <b>Ctrl+C</b> /
<b>Ctrl+V</b> copy / paste registers (as JSON, also between maps) · <b>Del</b> delete</td></tr>
<tr><td><b>Math line</b></td><td>The list of names: <b>Up</b> / <b>Down</b> pick, <b>Enter</b> or <b>Tab</b> takes one,
<b>Esc</b> closes it</td></tr>
</table>
)HTML") },
};

/* The opening tag of a code block: %CODE% in a page becomes this. */
QString codeBlockTag(const ThemeColors &c) {
	return QStringLiteral("<pre style='background:%1; border:1px solid %2; padding:10px; "
			"font-family:\"Cascadia Mono\",Consolas,monospace; font-size:9pt'>").arg(c.bg.name(), c.border.name());
}

/* Headings, inline code and list spacing, the same on every page. A heading in the text colour: in the muted one it
 * read as switched off. */
QString pageStyle(const ThemeColors &c) {
	return QStringLiteral("<style>h2{margin-top:0} h3{margin-bottom:2px; color:%1} "
			"code{font-family:'Cascadia Mono',Consolas,monospace; color:%2} li{margin-bottom:3px}</style>")
			.arg(c.text.name(), c.accent.name());
}

} // namespace

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle(tr("EVRe Studio — Help"));
	resize(980, 700);
	topics_ = new QListWidget;
	topics_->setObjectName(QStringLiteral("helpTopics")); /* a card as the page beside it (theme) */
	topics_->setFixedWidth(250); /* the longest title and the card's padding: no scroll bar under it */
	topics_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	for (const Topic &topic : TOPICS) topics_->addItem(tr(topic.title));
	page_ = new QTextBrowser;
	page_->setOpenExternalLinks(true);

	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(14);
	layout->addWidget(topics_);
	layout->addWidget(page_, 1);

	connect(topics_, &QListWidget::currentRowChanged, this, &HelpDialog::showTopic);
	topics_->setCurrentRow(0);
}

void HelpDialog::showTopic(int index) {
	if (index < 0 || index >= int(std::size(TOPICS))) return;
	const ThemeColors &colors = Theme::colors();
	QString html = tr(TOPICS[index].html);
	html.replace(QLatin1String("%CODE%"), codeBlockTag(colors));
	/* the engine's own numbers, so the page says what it does */
	html.replace(QLatin1String("%OFFLINE_MISSES%"), QString::number(IoEngine::OFFLINE_AFTER_TIMEOUTS));
	html.replace(QLatin1String("%OFFLINE_RETRY_S%"), QString::number(IoEngine::OFFLINE_RETRY_MS / 1000.0));
	page_->setHtml(pageStyle(colors) + html);
	/* right to left (Arabic): every paragraph so, also one that starts with a Latin word ("RAM: …", which the text
	 * would lay out left to right by itself); the code blocks stay left to right */
	if (!isRightToLeft()) return;
	QTextCursor cursor(page_->document());
	for (QTextBlock block = page_->document()->begin(); block.isValid(); block = block.next()) {
		QTextBlockFormat format = block.blockFormat();
		format.setLayoutDirection(format.nonBreakableLines() ? Qt::LeftToRight : Qt::RightToLeft);
		cursor.setPosition(block.position());
		cursor.setBlockFormat(format);
	}
}

# EVRe Studio: several slaves on one link, and broadcast

Design record: several slaves on one link, and broadcast. Built and merged
2026-10-03; the user guide is STUDIO.md 3.9, 3.10 (and 13.8 auto send).

This file keeps the reasons behind the design. Where it and STUDIO.md differ,
STUDIO.md describes what the Studio does.

Contents

1. The starting point
2. What the protocol gives and demands
3. The design in one page
4. Why a bus file
5. Why D1_ names
6. The broadcast rule, and why
7. Decisions
8. What was built differs from the plan
9. Risks

---

## 1. The starting point

The Studio was built around one device per window: one map, one `RegTable`,
one block layout, one login, one device-ID check, and one `evre::Master` with
one slave address put in every frame.

Four faults were found while planning, all fixed by this work:

| # | Where | What | Effect before the fix (fixed) |
|---|---|---|---|
| F-1 | `Master::findPending` | An answer was matched by function code and offset only, never by its slave. | Harmless with one device. With two, an answer from slave 3 could complete a request sent to slave 2. |
| F-2 | Sidebar,&nbsp;Map&nbsp;settings | Slave 0 was accepted as a device address; `checkMap` had no rule for it. | Every READ went to broadcast, nobody answered, every request timed out. |
| F-3 | API&nbsp;pass-through&nbsp;(:1219) | The&nbsp;client's&nbsp;slave&nbsp;was&nbsp;ignored. | A client could not reach another slave, and a broadcast WRITE from a client went to one device as a plain write. |
| F-4 | The&nbsp;fake&nbsp;devices | They answered any slave, slave 0 too. | Tests could not catch F-1 or F-2. |

## 2. What the protocol gives and demands

- Every frame carries a slave address, and every answer the address it answers
  for. A device ignores a frame for another slave.
- **Slave 0 is broadcast:** WRITE only, never answered. READ and WRITE_ACK to 0
  are not allowed (every device would answer at once). No device has address 0.
- **EVRe 1.0:** a broadcast WRITE is taken wherever a unicast WRITE would be.
- **EVRe 1.1 (draft):** by default a broadcast reaches the reserved bank only;
  the device bank (0xD000 and up) takes one only when the device sets
  `ACCEPT_BROADCAST_D000` (STATUS bit 14). A refused broadcast is silent.
- **A hole the host cannot see through:** a 1.0 device and a 1.1 device without
  the opt-in both report bit 14 clear and revision 1. The first takes a
  device-bank broadcast, the second drops it. So no host can promise that a
  broadcast was taken; it can only read back and see.
- **On a UART bus (RS-485)** one transmitter talks at a time: one request in
  flight across the whole bus. A device that does not answer costs a full
  timeout on every poll unless it is left out.
- **Behind a TCP gateway** answers from different slaves may come out of
  order, so matching must use the slave (F-1).

## 3. The design in one page

```
                 +----------------------- one window ------------------------+
                 |                                                           |
  bus file  ---> |  Link (TCP or serial)                                     |
  (optional)     |    |                                                      |
                 |  Master: one queue, one In flight                         |
                 |    |   answers matched by slave + fn + offset + count     |
                 |    +-------------+-------------+-------------+            |
                 |    | D1 slave 1  | D2 slave 2  | D3 slave 7  |  slave 0   |
                 |    | motor.json  | motor.json  | supply.json |  broadcast |
                 |    | login, ID   | login, ID   | login, ID   |  (writes)  |
                 |                                                           |
                 |  One register table holds every device's registers       |
                 |  (D1_SPEED, D2_SPEED, D3_VOUT), each with its slave.      |
                 |  Registers tab and Map editor: the selected device (or    |
                 |  All devices). Chart, math lines, CSV, API: every device. |
                 +-----------------------------------------------------------+
```

- One link, one Master. The slave moves from the Master into each request
  (`readFrom`, `writeTo`, `writeNoAckTo`).
- One poll reads every polled device in turn through the one queue, and never
  merges a read across two devices.
- A device that times out 3 times in a row goes offline: it leaves the polls
  and is asked for its DEVICE_ID again, one offline device per 2 s tick. The
  others keep their rate.
- Without a bus file the Studio works on one device, as before.

## 4. Why a bus file

A map describes a device; a bus file (`evre-bus/1`) describes an installation:
which devices share a link, at which slave address, with which map. Keeping the
two apart means:

- the map format does not change, and one map serves any number of identical
  devices at different addresses;
- the installation can be shared, and the command line and the Python package
  read the same file;
- meaning stays in the map and wiring in the bus file; nothing new goes into the
  protocol.

The alternatives were a list in the Studio's settings (not shareable) and a
device list inside a map (mixing device and installation).

## 5. Why D1_ names

Every register is named after its device: `D1_SUPPLY_V`, `D2_STATUS`. A name
alone then reaches any register of any device, without a slave address, in the
table, the chart, math lines, CSV columns and the API. Existing code that works
by name (math formulas, CSV headers, JSON commands) needed no new argument.

`_` was chosen over `.` because it is valid in every identifier the exports
and formulas use. Device names must keep names unambiguous: no device name
followed by `_` may start another (`D1` and `D1_A` would make `D1_A_X` read two
ways).

Inside, a register's key is `regKey(slave, addr)`, and math lines start at
`FIRST_CHART_KEY` (1 << 24), clear of every register key.

## 6. The broadcast rule, and why

A broadcast is never answered, and the same address may mean different things in
different maps. A write that sets a speed on one device could set a voltage on
another. So the Studio sends a broadcast only where it means the same to every
device:

| Where | Allowed |
|---|---|
| The reserved bank's writable registers, 0xA004 .. 0xA105 (CONFIG, MSG_CNT, MSG_BUFFER) | always: every EVRe device has them |
| 0xA000 .. 0xA003 (DEVICE_ID, STATUS) | never: read-only everywhere |
| Anywhere&nbsp;else | only when every device on the link has the same register map, and every byte written lies in a writable register of it |
| CONFIG&nbsp;with&nbsp;AUTO_SEND&nbsp;set | never: every device would start sending at once, over the others |

Because nothing confirms a broadcast (section 2), every broadcast the Studio
sends by register is followed by a read-back of each device, and the Log says
which device holds something else. The same rule guards the Quick write, the
kept broadcasts, the JSON `broadcast` command, `evre broadcast`, the Python
package and the API pass-through.

## 7. Decisions

| # | Question | Decided |
|---|---|---|
| D-1 | Where&nbsp;the&nbsp;device&nbsp;list&nbsp;lives | A bus file, `evre-bus/1` |
| D-2 | How the Registers tab shows several devices | One selected device, or All devices in one table |
| D-3 | Pass-through slave with one device | Unchanged: any slave goes to the Studio's device; on a bus the client's slave is used |
| D-4 | A&nbsp;device&nbsp;that&nbsp;stops&nbsp;answering | Offline after 3 timeouts in a row, retried every 2 s, the others keep polling |
| D-5 | Proof&nbsp;after&nbsp;a&nbsp;broadcast | A read-back of each device |
| D-6 | An address that means different things on different devices | Refused (section 6) |
| D-7 | Broadcast&nbsp;permission | Behind Allow writes (Allow API writes for the API), plus the danger confirmation |
| D-8 | Slave&nbsp;0&nbsp;in&nbsp;maps&nbsp;and&nbsp;the&nbsp;sidebar | An error: 0 is the broadcast address |
| D-9 | Tokens | One per device, typed in its Edit dialog and never saved; the Connection card's token otherwise |
| D-10 | Polling&nbsp;per&nbsp;device | On or off per device (`"poll"`), one shared interval |
| D-11 | Kept&nbsp;broadcasts | In the bus file (`"broadcasts"`) |
| D-12 | Python | A `Bus` class, built with the rest |

## 8. What was built differs from the plan

Built otherwise:

- One combined register table (each register carries its slave) instead of a
  table per device in the engine. `IoEngine::setMap` takes the devices; there is
  no `Node` type and no `setNodes`. Keys are `regKey(slave, addr)`, not
  `node << 16 | addr`.
- Names use `_` (`D1_SPEED`), not `.` (`left.SPEED`).
- The bus file's list key is `"devices"`, not `"nodes"`. A kept broadcast holds
  `"register"` (the map's own name) and `"value"`, not `"addr"` and `"type"`.
- An offline device's dot is red; grey means not connected. The dot shows
  answers / offline / not connected only, not ID mismatch or login refused (the
  Log and the line under the pill say those).
- With one device the Devices card shows a line and New bus / Open bus, not a
  single row.
- Broadcast by register: To all devices in the Quick write panel, Slave 0 in the
  Monitor, and the Broadcast menu of kept broadcasts, instead of a Broadcast
  panel. A broadcast cannot skip a device, so there is no list of targets to
  untick and no STATUS bit 9 / 14 column.
- One token per device in its Edit dialog, with the Connection card's token as
  the fallback, instead of a "same token for all" tick.
- On a bus the pass-through drops a client's broadcast that is not a WRITE,
  without an answer, instead of ERROR_RESP 2.
- New settings: only the last bus file (`map/bus`); the selected device is not
  saved.

Added beyond the plan: the Registers tab's device picker with All devices; the
Map editor names the devices of the map it edits and picks whose live values it
shows; the JSON `broadcast` command; `evre --bus` and `evre broadcast`; the
Python `Bus` class; per-device login and device-ID warnings; `--slave` in every
fake device and in `evre-sim`.

Not built:

- `docs/BUS_FORMAT.md` and a bus schema (the format is in STUDIO.md 3.9).
- The `"link"` key in the bus file.
- `--absent` and `--bus-1.1` in the fakes, and `--node` in `evre-sim`
  (`evre_fake_fast --node` plays a bus).
- The JSON `"device"` key, and nodes in `list` and `info` (names reach every
  device).
- `--verify --slaves` in `evre broadcast` (it always reads back).
- A token per device in the sidebar.
- A chart picker grouped by device, and completion of `D1_` prefixes.

## 9. Risks

- **The engine's hottest path** (blocks, samples, keys) changed. It is guarded
  by the full test suite with new bus checks, and by poll-rate measurements
  against `evre_fake_fast`.
- **A serial bus with a missing device** is covered by the offline logic and
  the tests; a real RS-485 line remains to be tried on hardware.
- **1.0 and 1.1 broadcasts** cannot be told apart (section 2). The read-back is
  the answer.

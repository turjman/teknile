# The EVRe register map format, `evre-map/1`

A map describes one kind of EVRe device: how it is reached and what its registers are. It is the one source the
tools around EVRe share: EVRe Studio makes, edits and monitors with it, generates a specification, a C header, a
Python module and a sheet from it, and other tools read it the same way.

This document is the format's contract, for anyone who writes a tool that reads or writes maps. The JSON Schema
[`evre-map-1.schema.json`](evre-map-1.schema.json) says the same in a form tools can check. How EVRe Studio uses a
map is in [STUDIO.md](STUDIO.md), chapter 16 and Part IV.

## 1. The file

- JSON (RFC 8259), UTF-8. A UTF-8 byte order mark is allowed and should be kept by a tool that rewrites the file.
- One JSON object at the top level.
- The file name is free; `.json` is usual.

## 2. Rules for tools

1. **Unknown keys.** A reader ignores keys it does not know, at any level. A writer that rewrites a map keeps them,
   with their values, where the file had them.
2. **Additions only.** `evre-map/1` grows only by new optional keys. A change that would make an old map mean
   something else gets a new format name (`evre-map/2`).
3. **Numbers and addresses.** A 16-bit number (an address, a device ID, a USB ID) is a JSON string, `"0xD004"` (hex,
   any case) or `"53252"`, or a JSON integer. Out of 0 – 0xFFFF it is an error.
4. **Byte order.** Every multi-byte register value is little endian, as EVRe sends it.
5. **Shown units.** `min`, `max`, `default` and the keys of `special` are in *shown* units:
   shown = raw × `scale` + `offset`. The names of `enum` and of a field's `values` are keyed by the *raw* number.
6. **Keep the text.** A tool that saves a map it did not change should write the same bytes, and a changed map
   should differ only where it was changed. (EVRe Studio does; it is what keeps maps readable in version control.)

## 3. The top level

| Key | Type | Default | Meaning |
|---|---|---|---|
| `format` | string | `"evre-map/1"` | the format name |
| `device` | string | `""` | the device's name |
| `desc` | string | `""` | one line on the map |
| `notes` | string | `""` | longer text on the map (Markdown) |
| `device_id` | 16-bit&nbsp;number | none | the value of DEVICE_ID (`0xA000`) the map is for |
| `slave` | integer 1 – 255 (0 is the broadcast address) | `1` | the slave address |
| `usb` | `{ "vid", "pid" }` | none | the device's USB vendor and product ID (16-bit numbers) |
| `login` | `{ "addr", "size" }` | none | after connecting, a token is written to `addr` (not 0) as UTF-8, cut or zero-padded to `size` bytes (1 – 65535, default 16) |
| `protocol` | object | none | how the device is reached: `transport` (`"serial"`, `"tcp"`, `"usb"`, or several: `"serial, tcp"`), `baud`, `tcp_port`, `timeout_ms` (integers), `notes` |
| `groups` | object | none | notes per group: `{ "Power": { "notes": "…" } }` |
| `extends` | string | none | an overlay: the path of the map this one changes, relative to this file (section 7) |
| `registers` | array | `[]` | the registers (section 4) |
| `streams` | array | none | the device's sample streams, Fast EVRe (section 9) |

## 4. A register

| Key | Type | Default | Meaning |
|---|---|---|---|
| `addr` | 16-bit&nbsp;number | **required** | the first byte's address |
| `name` | string | the&nbsp;address | unique in the map; tools find registers by it |
| `type` | string | `"u16"` | section 5 |
| `size` | integer&nbsp;1&nbsp;–&nbsp;65535 | by&nbsp;the&nbsp;type | bytes; given for `bytes` only |
| `unit` | string | `""` | the shown value's unit |
| `access` | `"ro"`,&nbsp;`"rw"`,&nbsp;`"wo"` | `"ro"` | read-only, read-write, or write-only (never read: a host does not poll it) |
| `write` | `"normal"`,&nbsp;`"action"`,&nbsp;`"w1c"` | `"normal"` | `action`: a write does something, then the register reads back idle; `w1c`: a 1 written to a bit clears it, a 0 leaves it |
| `persist` | boolean | `false` | kept across a reset (non-volatile) |
| `group` | string | `"Registers"` | registers that belong together |
| `desc` | string | `""` | one line |
| `notes` | string | `""` | longer text (Markdown): what a write does, sequences, examples |
| `danger` | boolean | `false` | a write moves, powers, switches or resets something: a host confirms it |
| `plot` | boolean | `true` | `false`: a value that does not change with time (an ID, a version, a setting); a host does not offer it as a line on a chart. Numbers only; a `bytes` register is never plotted |
| `format` | `"hex"` | none | show the value in hex |
| `scale`,&nbsp;`offset` | numbers | `1`,&nbsp;`0` | shown = raw × scale + offset |
| `decimals` | integer&nbsp;−1&nbsp;–&nbsp;15 | automatic | the shown value's decimals |
| `min`,&nbsp;`max` | numbers | none | the shown value's limits for a write (section 6) |
| `default` | number&nbsp;or&nbsp;string | none | the value after a reset (with `persist`: the factory value); a number in shown units, or one of the register's `enum` or `special` names |
| `special` | object | none | names for single values of a number, keyed by shown value: `{ "-1": "not measured" }` |
| `enum` | object | none | names of raw values: `{ "0": "off", "0x10": "boost" }` (keys decimal or `0x` hex) |
| `fields` | array | none | bit fields (section 5.2) |

Registers must not share bytes. Two registers at the same address are allowed by the grammar but ambiguous.

## 5. Values

### 5.1 Types

| Type | Size | Value |
|---|---|---|
| `u8`,&nbsp;`i8` | 1 | unsigned, two's complement |
| `u16`,&nbsp;`i16` | 2 | |
| `u32`,&nbsp;`i32` | 4 | |
| `f32` | 4 | IEEE 754 single |
| `bytes` | `size` | raw bytes, no number |

The C names `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t` and `float` are read as the same
types. Writers use the short names.

### 5.2 Bit fields

```json
"fields": [
  { "name": "MODE", "bits": "1:0", "values": { "0": "idle", "1": "run" } },
  { "name": "FAULT", "bits": "4", "access": "w1c", "desc": "latched: write 1 to clear" }
]
```

| Key | Meaning |
|---|---|
| `name` | required |
| `bits` | required: `"high:low"` (either order) or one bit, `"3"`; bit 0 is the least significant bit of the little-endian value |
| `access` | `"ro"`, `"rw"` or `"w1c"` where the field differs from its register |
| `desc` | one line |
| `values` | names of the field's raw values |

Fields are for integer types; they stay inside the register's bits and should not share bits.

### 5.3 Names, special values, limits

- An `enum` names raw values: the register *is* one of them. A value without a name is shown by its number.
- `special` names single values of a number; the register stays a number (a load current of `-1` "not measured").
- Where both name the same value, `special` wins.
- A `default` given as a name is the value of that name.

## 6. Writing

- A write to an `ro` register is refused by hosts; `wo` and `rw` registers are written.
- `min` and `max` bound what a host lets a user write. A special value is always allowed. They describe the
  device's contract; the device itself may be stricter.
- `write: "action"` registers are written to trigger; reading them back shows their idle value, not what was written.
- `write: "w1c"` (or a `w1c` field): a host writes 1 to the bits to clear and 0 elsewhere, not a read-modify-write
  of the value it read.

## 7. Overlays

A map with `"extends": "<path>"` changes another map (its base), which is read first (it may extend another,
8 levels at most):

1. The overlay's top-level keys replace the base's; `null` removes the base's key. `registers` and `extends`
   are not copied.
2. Each overlay register is matched by `addr` to a base register:
   - found, with `"remove": true`: the base register goes;
   - found: its keys replace the base register's keys, `null` removes one (`addr` itself stays);
   - not found: a new register.
3. The result is a map like any other.

A tool that saves an overlay writes only what differs from its base.

## 8. Examples

A minimal map:

```json
{ "format": "evre-map/1", "device": "Minimal device",
  "registers": [ { "addr": "0xD000", "name": "UPTIME", "type": "u32", "unit": "ms" } ] }
```

A register with most keys:

```json
{ "addr": "0xD040", "name": "OUTPUT_V", "type": "f32", "unit": "V", "access": "rw", "persist": true,
  "group": "Output", "desc": "output voltage set point", "danger": true,
  "min": 0.0, "max": 24.0, "default": 12.0, "decimals": 2,
  "notes": "Taken at the next output start. Values past max are refused by the device." }
```

A status register with fields and a command register:

```json
{ "addr": "0xD010", "name": "STATUS", "type": "u16", "group": "System",
  "fields": [ { "name": "STAGE", "bits": "2:0", "values": { "0": "off", "1": "ramp", "2": "run", "3": "hold" } },
              { "name": "FAULT", "bits": "8", "access": "w1c" } ] },
{ "addr": "0xD0F0", "name": "COMMAND", "type": "u8", "access": "wo", "write": "action", "danger": true,
  "group": "System", "enum": { "1": "start", "2": "stop" } }
```

`maps/example_device.json` in EVRe Studio is a complete map of the fake test device.

## 9. Streams (Fast EVRe)

A device that takes samples on its own clock may send them in numbered blocks: Fast EVRe, a layer above the
protocol (PROTOCOL.md, "Fast EVRe"). Each block is a `READ_RESP` nobody asked for, at the first address of the
stream's *window*: a span of the device bank that the map gives to the stream and that no register uses. The map
says where the blocks come from, how fast, and what a record holds:

```json
"streams": [
  { "name": "ADC", "addr": "0xDC00", "size": 1024, "rate": 10000, "enable": "ADC_STREAM",
    "group": "Power", "desc": "load current and bus voltage, sampled together",
    "channels": [
      { "name": "I_LOAD", "type": "i16", "unit": "A", "scale": 0.0005 },
      { "name": "V_BUS",  "type": "i16", "unit": "V", "scale": 0.001 }
    ] }
]
```

A stream:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | **required** | unique among the map's streams and registers |
| `addr` | 16-bit&nbsp;number | **required** | the window's first address, in the device bank `0xD000`..`0xDFFF` |
| `size` | integer | **required** | the window's bytes: the largest block, its 8-byte header included |
| `rate` | number | **required** | records a second, as the device is built |
| `rate_reg` | string | none | a register whose shown value is the rate now, read at the start and after a write to it |
| `enable` | string | none | a writable register: a host writes 1 to start the stream and 0 to stop it. Without it the stream is the device's own business: a host only listens |
| `group`,&nbsp;`desc`,&nbsp;`notes` | strings | as&nbsp;a&nbsp;register's | for people |
| `channels` | array | **required** | what one record holds, in order, packed, little endian |

A channel:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | **required** | unique in its stream. Tools name the line `STREAM.CHANNEL`, as they name a register's field |
| `type` | string | `"i16"` | a number type of section 5.1: `u8` to `f32`. Never `bytes` |
| `unit`,&nbsp;`scale`,&nbsp;`offset`,&nbsp;`decimals`,&nbsp;`desc` | as&nbsp;a&nbsp;register's | | shown = raw × scale + offset |

A record is one instant of all the stream's channels; its size is the sum of its channels' sizes. A block holds
up to (`size` − 8) / record size records.

A map is wrong (a checker refuses it) when a stream has: a window outside `0xD000`..`0xDFFF`; a window that shares
a byte with a register or another stream's window; a window smaller than the header and one record; no channel; a
`bytes` channel; a rate that is not above 0; an `enable` or `rate_reg` that names no register, or an `enable` that
names one a host cannot write; a name used twice.

No register is declared over a window: a tool that polled it would read the whole window at every poll.

`maps/example_fast.json` in EVRe Studio is a map with a stream: the fast fake test device.

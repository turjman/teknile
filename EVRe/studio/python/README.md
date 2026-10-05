# evre for Python

EVRe devices from Python, by register name, with the device's map (`evre-map/1`, see
[../docs/MAP_FORMAT.md](../docs/MAP_FORMAT.md)). Standard library only; a serial port needs `pyserial`.

```sh
pip install ./python            # or: pip install ./python[serial]
```

```python
import evre

with evre.connect_tcp('127.0.0.1', 1210, 'maps/example_device.json', token='example-token') as dev:
    print(dev['SUPPLY_V'])                 # 12.031  (shown units: raw x scale + offset)
    print(dev.decoded('STATE'))            # {'MODE': 'run', 'READY': 1, 'ALARM': 0}
    dev['LED_MODE'] = 'blink'              # a value name, a number, a shown value
    print(dev.write('FAN_SPEED', 40))      # 40: written, then read back
    print(dev.read_all())                  # every register a poll reads, in block reads
```

| Call | What it does |
|---|---|
| `connect_tcp(host, port, map, token=, slave=, timeout=)`, `connect_serial(port, baud, map, ...)` | a `Device`; the map is a path or a `DeviceMap`; the token is written to the map's login register first |
| `dev[name]`, `dev.read(*names)`, `dev.read_all()` | shown values (one, a dict, all) |
| `dev.decoded(name)` | a special or enum name, or the fields as a dict |
| `dev.write(name, value, force=False)`, `dev[name] = value` | refused for a read-only register; a danger register and a value past the map's min or max need `force=True` |
| `dev.read_raw(addr, count)`, `dev.write_raw(addr, data)`, `dev.device_id()` | bytes, no map needed |
| `evre.DeviceMap.load(path)` | the map, with `extends` resolved: `map[name]` is a `Register` with `encode`, `decode`, `decoded`, `limit_problem`, `fields`, `enum`, `special`, `min`, `max`, `default` |
| `evre.build`, `evre.Parser`, `evre.crc16` | frames by hand |
| `evre.EvreError` | the device's ERROR_RESP (`.code`), a timeout, or a refusal by the map |

An answer is matched by its slave, offset and count, as the protocol asks. A frame the device sends by itself
(AUTO_SEND's block) is skipped, also when a read asks for part of that block.

Several devices on one link: a bus file (`evre-bus/1`, as EVRe Studio writes it), each device at its own slave
address with its own map. A device without `"slave"` is at slave 1; a bus without a map for a device, or without
devices, is refused. `bus.broadcast` takes a register by the map's own name (`FAN_SPEED`) or a bus name
(`D1_FAN_SPEED`). `evre.frame.BROADCAST` (0) is the broadcast address; `evre.frame` also holds the reserved bank's
addresses and bits.

The example below runs against a bus of two devices played by the fast fake device of EVRe Studio:

```sh
build/evre_fake_fast 1231 maps/example_device.json example-token --slave 1 --node 2=maps/example_device.json
```

```python
with evre.connect_bus_tcp('127.0.0.1', 1231, 'maps/example_bus.json', token='example-token') as bus:
    print(bus['D1_SUPPLY_V'], bus['D2']['SUPPLY_V'])   # a register by its name on the bus, or a device's Device
    bus['D2_FAN_SPEED'] = 40
    print(bus.broadcast('FAN_SPEED', 0))              # {'D1_FAN_SPEED': 0, 'D2_FAN_SPEED': 0}: sent once, read back
```

| Call | What it does |
|---|---|
| `connect_bus_tcp(host, port, bus_file, token=, timeout=, tokens=)`, `connect_bus_serial(port, bus_file, baud, ...)` | a `Bus`; `token` for every device with a login register, `tokens={'D2': '...'}` for one whose token is another |
| `bus[device]`,&nbsp;`bus.devices` | a device's `Device` |
| `bus['D1_NAME']`, `bus.read(*names)`, `bus.write(name, value, force=False)` | registers by their names on the bus |
| `bus.broadcast(name, value, force=False)`, `bus.broadcast_refusal(addr, count)` | one frame to every device (slave 0), then each read back; the rule: the reserved bank's writable registers always, elsewhere only when every device has the same map; never CONFIG with AUTO_SEND (bit 3) on, which would make every device send by itself at once |

A fast stream (Fast EVRe: the device sends its samples in numbered blocks by itself), live:

```python
with evre.connect_tcp('127.0.0.1', 1240, 'maps/example_fast.json', token='example-token') as dev:
    for block in dev.stream('ADC', seconds=2):   # its enable written 1, then 0 at the end (also on a break)
        print(block.first, block.count, block.lost, block.values['I_LOAD'][:3])
```

`dev.stream(name, seconds=None, heartbeat=0.1)` yields a `Block` per block that comes: `first` (its first record's
number, 64 bits from the stream's start), `count`, `lost` (records missing before it), `new_start`, `values`
(`{channel: [value, ...]}` in the map's units) and `arrival` (`time.monotonic()`). CONFIG is read every `heartbeat`
seconds meanwhile (the device's host watchdog), the answers not waited for. A bad block, or one of a newer kind, is
skipped.

A fast stream's recording (a `.evrs` file EVRe Studio writes beside its CSV, or `evre record` writes):

```python
rec = evre.read_recording('run.ADC.evrs')
print(rec.stream['name'], len(rec.times), 'samples,', rec.lost, 'lost')   # ADC 1517000 samples, 1024 lost
current = rec.values['I_LOAD']   # in the map's units, one per sample
t = rec.times                    # seconds on the writer's clock (EVRe Studio: the CSV's time_s)
```

| Attribute | What it holds |
|---|---|
| `numbers`,&nbsp;`times` | each sample's record number (64 bits, from its start) and time |
| `values` | `{channel: [value, ...]}`, raw x scale + offset |
| `gaps`,&nbsp;`lost`,&nbsp;`starts` | `[(index after the gap, records lost)]`, their sum, the index of each start's first sample |
| `head`,&nbsp;`stream`,&nbsp;`device`,&nbsp;`start`,&nbsp;`start_s` | the file's head: the stream as the map writes it, the wall-clock start, the writer's clock then |
| `blocks`,&nbsp;`bad_blocks`,&nbsp;`cut` | blocks read and refused; `cut`: the file ends inside a piece (read up to the last whole one) |

Tests: `python -m unittest discover -s python/tests`, with `EVRE_BUILD=<build folder>` to include a session
against `evre-sim` serving the example map, a bus of two devices on `evre_fake_fast`, and a recording `evre record`
writes from it, and the stream live with `dev.stream`.

# EVRe Studio API examples

EVRe Studio shares the device it is connected to. Tick **Serve API** (or start
it with `--api`); writes also need **Allow API writes**, and registers marked
⚠ need **including ⚠ registers** as well. Neither write switch is remembered.

| Port | What |
|---|---|
| 1220 | JSON lines by register name: one JSON object per line each way |
| 1219 | EVRe pass-through: the device's own frames, for existing EVRe code |

Only this PC can connect unless **Network** is ticked.

The examples use the registers of `maps/example_device.json` (`SUPPLY_V`,
`SUPPLY_I`, `STATE`, `LED_MODE`); put your own map's names in their place.

## JSON commands

```
{"cmd":"info"}                                            device, link, write switches
{"cmd":"list"}                                            every register of the map
{"cmd":"get","names":["SUPPLY_V","STATE"]}                fresh values (+ "decoded" bit fields)
{"cmd":"set","values":{"LED_MODE":2}}                     write, then read back
{"cmd":"stream","names":["SUPPLY_V","SUPPLY_I"],"ms":50}  a line every 50 ms: {"t":..,"values":{..}}
{"cmd":"stop"}                                            end the stream
{"cmd":"read","addr":"0xD000","count":16}                 raw bytes, as hex
{"cmd":"write","addr":"0xD085","hex":"02"}                raw write
{"cmd":"broadcast","name":"D1_SPEED","value":0}           every device on the link at once, then each read back
```

Several devices on one link (a bus in the Studio): every register is named after its device, `D1_SPEED`,
`D2_SPEED`, so the commands above reach any device's register by name.

Any request may carry `"id"`; it comes back in the answer. A failure is
`{"ok":false,"error":"..."}` and the connection stays open. Names are the
map's, in any case, or an address such as `"0xD00C"`.

## Python

`python/evre_studio_client.py`: a small class (`get`, `set`, `stream`) and a
demo. Standard library only.

## MATLAB

`matlab/evre_studio_demo.m`: info, values, a write, a 5 s stream plotted.
Needs R2020b or newer (`tcpclient`, `writeline`, `readline`).

## LabVIEW

No toolkit needed:

1. **TCP Open Connection**: address `127.0.0.1`, port `1220`.
2. **TCP Write**: the request text followed by a line feed, e.g.
   `{"cmd":"get","names":["SUPPLY_V"]}` + `\n`.
3. **TCP Read**: mode **CRLF**, bytes to read 65536, timeout 3000 ms. One
   answer per line.
4. **Unflatten From JSON** (LabVIEW 2013+) into a cluster matching the answer,
   for example `ok` (Boolean) and `values` (a cluster with `SUPPLY_V`, a DBL).
5. For a stream, send `{"cmd":"stream","names":["SUPPLY_V"],"ms":20}` once, then
   keep reading lines in a loop; send `{"cmd":"stop"}` or close the connection
   to end it.

## Existing EVRe code

Point it at port 1219 instead of the device: the frames are the same
(`7B SLAVE FN OFF CNT DATA CRC 7D`). Writes follow the same switches; a
refused `WRITE_ACK` gets `ERROR_RESP` code 3 (permission denied).

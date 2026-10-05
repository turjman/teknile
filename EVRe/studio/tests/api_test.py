#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""End-to-end test of EVRe Studio's API server (JSON :1220, EVRe :1219).

Run EVRe Studio connected to tests/fake_device.py (NEVER a real device: this
writes, a danger register included), with the API on, then:

    python api_test.py readonly   # Studio started with --api
    python api_test.py writes     # --api-writes
    python api_test.py danger     # --api-writes-danger

The registers it uses are found in the map, as in gui_test.cpp: two
read-only f32 registers with a unit, the first writable u8 and the first
writable 16-bit danger register of the device bank (0xD000 and up).
Standard library only.
"""
import json
import socket
import struct
import sys
import time

HOST = '127.0.0.1'
JSON_PORT, EVRE_PORT = 1220, 1219
READ, READ_RESP, WRITE_ACK, WRITE_ACK_RESP, ERROR_RESP = 0xAA, 0xAB, 0xEB, 0xEC, 0xEE
PERMISSION_DENIED = 3  # the ERROR_RESP code of a refused write

MODE = sys.argv[1] if len(sys.argv) > 1 else 'readonly'
passed = failed = 0


def check(ok, what):
    global passed, failed
    print('%s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    passed, failed = (passed + 1, failed) if ok else (passed, failed + 1)


class JsonClient:
    """one connection to the JSON port: one object per line each way"""

    def __init__(self):
        self.sock = socket.create_connection((HOST, JSON_PORT), timeout=3)
        self.file = self.sock.makefile('rw', encoding='utf-8', newline='\n')

    def ask(self, **request):
        self.file.write(json.dumps(request) + '\n')
        self.file.flush()
        return self.next_line()

    def next_line(self):
        return json.loads(self.file.readline())

    def close(self):
        self.sock.close()


def crc16(data):
    """CRC-16/X-25 over everything before the CRC"""
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x8408 if c & 1 else c >> 1
    return c ^ 0xFFFF


def frame(fn, addr, cnt, data=b''):
    """7B SLAVE FN OFF(2) CNT(2) DATA CRC(2) 7D, little endian, to slave 1"""
    body = struct.pack('<BBBHH', 0x7B, 1, fn, addr, cnt) + data
    return body + struct.pack('<H', crc16(body)) + b'\x7D'


def evre_request(fn, addr, cnt, data=b''):
    """one request on the pass-through port, on a connection of its own: the answer frame, or None"""
    s = socket.create_connection((HOST, EVRE_PORT), timeout=2)
    s.sendall(frame(fn, addr, cnt, data))
    rx = b''
    try:
        while True:
            rx += s.recv(4096)
            if len(rx) >= 10:
                data_len = {READ_RESP: cnt, WRITE_ACK_RESP: 0, ERROR_RESP: 1}.get(rx[2], 0)
                if len(rx) >= 10 + data_len:
                    return rx[:10 + data_len]
    except socket.timeout:
        return None
    finally:
        s.close()


def first(registers, what, condition):
    """the first register that meets condition; the test stops when the map has none"""
    found = next((r for r in registers if condition(r)), None)
    if found is None:
        sys.exit('the map has no %s' % what)
    return found


def pick_registers(registers):
    """the registers the test uses, each as the list gives it (a dict with its
    'name', 'addr', ...): two floats, a u8 and a danger register"""
    device = [r for r in registers if int(r['addr'], 16) >= 0xD000]
    floats = [r for r in device if r['type'] == 'f32' and r['access'] == 'ro' and r.get('unit')]
    if len(floats) < 2:
        sys.exit('the map has no two read-only f32 registers with a unit')
    float1, float2 = floats[0], floats[1]
    u8 = first(device, 'writable u8',
               lambda r: r['access'] == 'rw' and r['type'] == 'u8' and not r.get('danger'))
    danger = first(device, 'writable 16-bit danger register',
                   lambda r: r['access'] == 'rw' and r.get('danger') and r['type'] in ('i16', 'u16'))
    print('registers used: %s %s, u8 %s, danger %s' % (float1['name'], float2['name'], u8['name'], danger['name']))
    return float1, float2, u8, danger


def test_info(j):
    info = j.ask(cmd='info', id=7)
    check(info.get('ok') and info.get('connected') and info.get('id') == 7, 'info: connected, id echoed')
    check(info.get('writes') == (MODE != 'readonly') and info.get('danger_writes') == (MODE == 'danger'),
          'info: write switches as started (%s)' % MODE)


def test_list(j):
    """the map's registers; the ones the test uses picked from them"""
    registers = j.ask(cmd='list').get('registers', [])
    float1, float2, u8, danger = pick_registers(registers)
    check(len(registers) >= 5 and float1.get('unit') and danger.get('danger'),
          'list: %d registers with units, danger marked' % len(registers))
    fixed = [r['name'] for r in registers if 'plot' in r]
    check(fixed and all(r['plot'] is False for r in registers if 'plot' in r) and 'DEVICE_ID' in fixed,
          'list: "plot": false on the registers the map marks not plottable (%s), no key on the others'
          % ', '.join(fixed))
    return float1, float2, u8, danger


def test_get(j, float1):
    """values by name and by address, errors; returns DEVICE_ID as a raw read gives it"""
    raw_id = j.ask(cmd='read', addr='0xA000', count=2)['hex']
    device_id = int(raw_id[2:4] + raw_id[0:2], 16)
    name = float1['name']
    g = j.ask(cmd='get', names=[name, 'DEVICE_ID', 'STATUS'])
    values = g.get('values', {})
    check(g.get('ok') and isinstance(values.get(name), float) and values.get('DEVICE_ID') == device_id,
          'get: %s %.3f, DEVICE_ID 0x%X (= raw read)' % (name, values.get(name, 0), values.get('DEVICE_ID', 0)))
    check('STATUS' in g.get('decoded', {}), 'get: bit fields decoded (STATUS)')
    check(j.ask(cmd='get', name=float1['addr']).get('ok'), 'get by address "%s"' % float1['addr'])
    unknown = j.ask(cmd='get', names=['NOPE'])
    check(not unknown.get('ok') and 'NOPE' in unknown.get('error', ''), 'unknown register: a clear error')
    check(not j.ask(cmd='frobnicate').get('ok'), 'unknown cmd: an error, the connection stays')
    raw = j.ask(cmd='read', addr='0xA000', count=2)
    check(raw.get('ok') and len(raw.get('hex', '')) == 4, 'raw read 0xA000: %s' % raw.get('hex'))
    return device_id


def test_stream(j, float1, float2):
    name1, name2 = float1['name'], float2['name']
    started = j.ask(cmd='stream', names=[name1, name2], ms=50, id='s1')
    check(started.get('ok') and started.get('ms') == 50, 'stream started at 50 ms')
    t0 = time.time()
    samples = [j.next_line() for _ in range(20)]
    seconds = time.time() - t0
    check(all(name1 in m['values'] and name2 in m['values'] and m.get('stream') == 's1' for m in samples),
          'stream: 20 samples with both values, tagged')
    check(0.6 < seconds < 2.0, 'stream rate: 20 samples in %.2f s (50 ms asked)' % seconds)


def test_writes(j, float1, u8, danger):
    """each write allowed or refused as the mode says; what was written is put back"""
    w = j.ask(cmd='set', values={u8['name']: 2})
    if MODE == 'readonly':
        check(not w.get('ok') and 'off' in w.get('error', ''), 'set refused: API writes are off')
    else:
        check(w.get('ok') and w['values'].get(u8['name']) == 2, 'set %s 2: ok, read back 2' % u8['name'])
        j.ask(cmd='set', values={u8['name']: 0})
    d = j.ask(cmd='set', values={danger['name']: 25})
    if MODE == 'danger':
        check(d.get('ok') and d['values'].get(danger['name']) == 25, 'set %s (danger) 25: ok' % danger['name'])
        j.ask(cmd='set', values={danger['name']: 0})
    else:
        check(not d.get('ok'), 'set %s (danger) refused: %s' % (danger['name'], d.get('error', '')))
    check(not j.ask(cmd='set', values={float1['name']: 1}).get('ok'), 'set a read-only register: refused')
    check(not j.ask(cmd='set', values={u8['name']: 300}).get('ok') if MODE != 'readonly' else True,
          'set 300 into a u8: refused')
    if MODE != 'readonly' and u8.get('max') is not None:
        past = j.ask(cmd='set', values={u8['name']: int(u8['max']) + 1})
        check(not past.get('ok') and 'maximum' in past.get('error', ''),
              'set past the map\'s max: refused (%s)' % past.get('error', ''))


def test_broadcast(j, float1, u8, danger):
    """{"cmd":"broadcast"}: one device here (the Studio is started with a map), so it goes to slave 0 and the
    device takes it as any EVRe device does; the same switches as a set, the register read back"""
    b = j.ask(cmd='broadcast', name=u8['name'], value=3)
    if MODE == 'readonly':
        check(not b.get('ok') and 'off' in b.get('error', ''), 'broadcast refused: API writes are off')
    else:
        check(b.get('ok') and b.get('values') == {u8['name']: 3},
              'broadcast %s 3: sent to slave 0, read back %s' % (u8['name'], b.get('values', b.get('error'))))
        j.ask(cmd='broadcast', name=u8['name'], value=0)
    d = j.ask(cmd='broadcast', name=danger['name'], value=25)
    if MODE == 'danger':
        check(d.get('ok') and d['values'].get(danger['name']) == 25, 'broadcast %s (danger) 25: ok' % danger['name'])
        j.ask(cmd='broadcast', name=danger['name'], value=0)
    else:
        check(not d.get('ok'), 'broadcast %s (danger) refused: %s' % (danger['name'], d.get('error', '')))
    check(not j.ask(cmd='broadcast', name=float1['name'], value=1).get('ok'), 'broadcast to a read-only register: refused')
    unknown = j.ask(cmd='broadcast', name='NOPE', value=1)
    check(not unknown.get('ok') and 'NOPE' in unknown.get('error', ''), 'broadcast to an unknown register: a clear error')


def test_pass_through(device_id, u8, danger):
    """the EVRe port: the device's own frames, writes under the same switches"""
    a = evre_request(READ, 0xA000, 2)
    check(a is not None and a[2] == READ_RESP and a[7:9] == struct.pack('<H', device_id),
          'pass-through READ 0xA000: READ_RESP = DEVICE_ID')
    u8_addr, danger_addr = int(u8['addr'], 16), int(danger['addr'], 16)
    a = evre_request(WRITE_ACK, u8_addr, 1, b'\x01')
    if MODE == 'readonly':
        check(a is not None and a[2] == ERROR_RESP and a[7] == PERMISSION_DENIED,
              'pass-through WRITE_ACK refused: ERROR_RESP 3 (permission)')
    else:
        check(a is not None and a[2] == WRITE_ACK_RESP, 'pass-through WRITE_ACK %s: WRITE_ACK_RESP' % u8['name'])
        evre_request(WRITE_ACK, u8_addr, 1, b'\x00')
    a = evre_request(WRITE_ACK, danger_addr, 2, b'\x10\x00')
    check(a is not None and (a[2] == WRITE_ACK_RESP) == (MODE == 'danger'),
          'pass-through write of the danger register: %s' % ('accepted' if MODE == 'danger' else 'refused'))
    if MODE == 'danger':
        evre_request(WRITE_ACK, danger_addr, 2, b'\x00\x00')


def main():
    j = JsonClient()
    test_info(j)
    float1, float2, u8, danger = test_list(j)
    device_id = test_get(j, float1)
    test_stream(j, float1, float2)
    j.close()  # stops the stream, by leaving: late samples die with the connection
    j = JsonClient()
    test_writes(j, float1, u8, danger)
    test_broadcast(j, float1, u8, danger)
    test_pass_through(device_id, u8, danger)
    print('\n%s: %d passed, %d failed' % (MODE, passed, failed))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()

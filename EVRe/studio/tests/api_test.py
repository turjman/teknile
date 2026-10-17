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

    python api_test.py fast <build folder>

The fast streams (Fast EVRe): starts its own evre_fake_fast (port 1246) with
maps/example_fast.json and an EVRe Studio connected to it with --api (writes
off), first with the stream off, then started with --fast ADC, and stops both
(by their process). The Studio needs a screen (on Linux: xvfb-run).
Standard library only (and the evre package beside it, for the fast mode).
"""
import json
import os
import socket
import struct
import subprocess
import sys
import threading
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
        """the answer: a stream's line that comes first (a sample read before a stop came late) is skipped, answers
        have no time"""
        self.file.write(json.dumps(request) + '\n')
        self.file.flush()
        while True:
            answer = self.next_line()
            if 't' not in answer:
                return answer

    def next_line(self):
        return json.loads(self.file.readline())

    def stop(self):
        """ends a stream: its lines up to the stop's answer are read and dropped"""
        self.ask(cmd='stop')

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


def test_no_streams(j):
    """a map without fast streams: "list" says so, and a fast channel's name is not found"""
    streams = j.ask(cmd='list').get('streams')
    nope = j.ask(cmd='get', names=['ADC.I_LOAD'])
    check(streams == [] and not nope.get('ok') and 'no register or fast channel "ADC.I_LOAD"' in nope.get('error', ''),
          'list: "streams": [] for a map without them; get of a fast channel there: %s' % nope.get('error'))


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


# ------------------------------------------------------------------------------------------------- fast streams

FAST_PORT = 1246
STREAM_WINDOW = 0xDC00


class Studio:
    """evre_fake_fast with the fast example map, and an EVRe Studio connected to it with the API on; stopped by
    their processes (only these two)"""

    def __init__(self, build, fast_streams):
        exe = '.exe' if os.name == 'nt' else ''
        self.map = os.path.join(build, 'maps', 'example_fast.json')
        if not os.path.exists(self.map):
            self.map = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'maps', 'example_fast.json')
        self.fake = subprocess.Popen([os.path.join(build, 'evre_fake_fast' + exe), str(FAST_PORT), self.map,
                                      'example-token'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        env = dict(os.environ)
        # a test run: settings of its own, and it quits by itself after 120 s whatever happens here
        env['EVRE_SHOT'] = '%s;120000;1400x900' % os.path.join(build, 'api_fast.png')
        args = [os.path.join(build, 'EVReStudio' + exe), '--tcp', '127.0.0.1:%d' % FAST_PORT, '--map', self.map,
                '--connect', '--api']
        if fast_streams:
            args += ['--fast', fast_streams]
        time.sleep(0.5)
        self.studio = subprocess.Popen(args, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def connect(self, seconds=20):
        """a JSON client once the Studio is connected to the device"""
        end = time.time() + seconds
        while time.time() < end:
            try:
                j = JsonClient()
                if j.ask(cmd='info').get('connected'):
                    return j
                j.close()
            except (OSError, ValueError):
                pass
            time.sleep(0.25)
        sys.exit('EVRe Studio did not connect to evre_fake_fast in %d s' % seconds)

    def stop(self):
        for p in (self.studio, self.fake):
            p.kill()
            p.wait(10)


class RawBlocks(threading.Thread):
    """a pass-through client that asked for the stream's blocks: every record by its number, (I_LOAD, V_BUS) raw"""

    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.create_connection((HOST, EVRE_PORT), timeout=2)
        self.records = {}
        self.blocks = 0
        self.answers = []
        self.lock = threading.Lock()
        self.running = True

    def send(self, fn, addr, cnt, data=b''):
        self.sock.sendall(frame(fn, addr, cnt, data))

    def run(self):
        rx = b''
        while self.running:
            try:
                rx += self.sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            while True:
                start = rx.find(b'\x7b')
                if start < 0:
                    rx = b''
                    break
                rx = rx[start:]
                if len(rx) < 7:
                    break
                fn, addr, cnt = rx[2], struct.unpack_from('<H', rx, 3)[0], struct.unpack_from('<H', rx, 5)[0]
                length = 10 + {READ_RESP: cnt, ERROR_RESP: 1}.get(fn, 0)
                if len(rx) < length:
                    break
                one, rx = rx[:length], rx[length:]
                whole = crc16(one[:-3]) == struct.unpack_from('<H', one, length - 3)[0]
                if fn == READ_RESP and addr == STREAM_WINDOW and whole:
                    data = one[7:7 + cnt]
                    first, count = struct.unpack_from('<IH', data, 0)
                    with self.lock:
                        for k, (i_load, v_bus) in enumerate(struct.iter_unpack('<hh', data[8:8 + 4 * count])):
                            self.records[first + k] = (i_load, v_bus)
                        self.blocks += 1
                else:
                    with self.lock:
                        self.answers.append(one)

    def answer(self, seconds=2):
        end = time.time() + seconds
        while time.time() < end:
            with self.lock:
                if self.answers:
                    return self.answers.pop(0)
            time.sleep(0.01)
        return None

    def close(self):
        self.running = False
        self.sock.close()


def fast_off(build):
    """the stream off in the Studio: "list" says so; get and stream are refused with the reason; a pass-through
    switch of it is the client's own write to the device, refused with writes off"""
    studio = Studio(build, None)
    try:
        j = studio.connect()
        answer = j.ask(cmd='list')
        streams = answer.get('streams')
        adc = streams[0] if streams else {}
        names = [(c.get('name'), c.get('unit'), c.get('type')) for c in adc.get('channels', [])]
        check(len(streams or []) == 1 and adc.get('name') == 'ADC' and adc.get('rate') == 10000
              and adc.get('on') is False
              and names == [('ADC.I_LOAD', 'A', 'i16'), ('ADC.V_BUS', 'V', 'i16')]
              and any(r['name'] == 'ADC_STREAM' for r in answer.get('registers', [])),
              'fast: list names the stream ADC (10000 a second, off) and its channels ADC.I_LOAD [A], ADC.V_BUS [V] '
              'beside the registers')
        g = j.ask(cmd='get', names=['adc.i_load'])
        off = 'fast stream ADC is off: start it in EVRe Studio (Fast streams), or start the Studio with --fast ADC'
        check(not g.get('ok') and g.get('error') == off, 'fast: get of a channel of a stream that is off: %s' % g.get('error'))
        st = j.ask(cmd='stream', names=['SUPPLY_V', 'ADC.V_BUS'])
        check(not st.get('ok') and 'is off' in st.get('error', ''), 'fast: stream of it refused the same way')
        nope = j.ask(cmd='get', names=['ADC.NOPE'])
        check(not nope.get('ok') and 'no register or fast channel "ADC.NOPE"' in nope.get('error', ''),
              'fast: an unknown channel: %s' % nope.get('error'))
        a = evre_request(WRITE_ACK, int(next(r for r in answer['registers'] if r['name'] == 'ADC_STREAM')['addr'], 16),
                         1, b'\x01')
        check(a is not None and a[2] == ERROR_RESP and a[7] == PERMISSION_DENIED,
              'fast: a pass-through write of ADC_STREAM while the Studio does not stream: the client\'s own write, '
              'refused with API writes off (ERROR_RESP 3)')
        j.close()
    finally:
        studio.stop()


def fast_on(build):
    """the stream started with --fast: get, the min/max/mean lines against the records the pass-through brings, the
    pass-through's blocks asked for and ended without touching the device, and the evre package on both ports"""
    studio = Studio(build, 'ADC')
    try:
        j = studio.connect()
        end = time.time() + 10
        g = {}
        while time.time() < end and not g.get('ok'):
            g = j.ask(cmd='get', names=['ADC.I_LOAD', 'adc.v_bus', 'SUPPLY_V'])
            time.sleep(0.1)
        streams = j.ask(cmd='list').get('streams', [])
        check(streams and streams[0].get('on') is True and streams[0].get('rate') == 10000,
              'fast: started with --fast ADC: list says on, at 10000 a second')
        values, times = g.get('values', {}), g.get('times', {})
        now = time.time()
        check(g.get('ok') and abs(values.get('ADC.I_LOAD', 99)) <= 6.5 and abs(values.get('ADC.V_BUS', 99)) <= 13
              and isinstance(values.get('SUPPLY_V'), float) and sorted(times) == ['ADC.I_LOAD', 'ADC.V_BUS']
              and all(abs(t - now) < 1.0 for t in times.values()),
              'fast: get of two channels and a register: the newest record\'s values (%s, %s) and their time (%.3f s '
              'from now), the register read' % (values.get('ADC.I_LOAD'), values.get('ADC.V_BUS'),
                                                 times.get('ADC.I_LOAD', 0) - now))

        # the pass-through: the blocks asked for by writing the enable, with writes off (nothing reaches the device)
        enable = int(next(r for r in j.ask(cmd='list')['registers'] if r['name'] == 'ADC_STREAM')['addr'], 16)
        raw = RawBlocks()
        raw.start()
        raw.send(WRITE_ACK, enable, 1, b'\x01')
        a = raw.answer()
        time.sleep(0.5)
        check(a is not None and a[2] == WRITE_ACK_RESP and raw.blocks > 20,
              'fast: pass-through, ADC_STREAM written 1 with API writes off: acknowledged, the stream\'s blocks come '
              '(%d in 0.5 s)' % raw.blocks)

        # the JSON stream: registers every 50 ms, the channels' min, max and mean every 100 ms, two kinds of line
        started = j.ask(cmd='stream', names=['ADC.I_LOAD', 'SUPPLY_V', 'ADC.V_BUS'], ms=50, period_ms=100, id='f')
        check(started.get('ok') and started.get('streaming') == 3 and started.get('ms') == 50
              and started.get('fast') == 2 and started.get('period_ms') == 100,
              'fast: stream of two channels and a register: started (%s)' % started)
        t0 = time.time()
        lines = []
        while time.time() - t0 < 2.0:
            lines.append(j.next_line())
        seconds = time.time() - t0
        j.stop()
        samples = [m for m in lines if 'values' in m]
        summaries = [m for m in lines if 'fast' in m]
        check(len(samples) >= 25 and len(summaries) >= 14 and all(m.get('stream') == 'f' for m in lines)
              and all(sorted(m['fast']) == ['ADC.I_LOAD', 'ADC.V_BUS'] for m in summaries),
              'fast: %d register samples and %d min/max/mean lines in %.1f s, each its own kind, tagged'
              % (len(samples), len(summaries), seconds))
        counts = [m['fast']['ADC.I_LOAD']['n'] for m in summaries]
        rate = sum(counts[1:]) / (summaries[-1]['t'] - summaries[0]['t']) if len(summaries) > 2 else 0
        follows = all(b['fast'][c]['first'] == a['fast'][c]['first'] + a['fast'][c]['n']
                      for a, b in zip(summaries, summaries[1:]) for c in ('ADC.I_LOAD', 'ADC.V_BUS')
                      if a['fast'][c]['n'] and b['fast'][c]['n'])
        check(9000 < rate < 11000 and follows,
              'fast: %.0f records a second in the lines; each period starts where the one before ended' % rate)
        # each line against the records the pass-through brought: the same min, max and mean
        compared = differ = 0
        with raw.lock:
            records = dict(raw.records)
        for m in summaries:
            for c, (column, scale) in (('ADC.I_LOAD', (0, 0.0005)), ('ADC.V_BUS', (1, 0.001))):
                one = m['fast'][c]
                numbers = range(one['first'], one['first'] + one['n']) if one['n'] else []
                if not numbers or any(k not in records for k in numbers):
                    continue
                shown = [records[k][column] * scale for k in numbers]
                compared += 1
                if (abs(min(shown) - one['min']) > 1e-9 or abs(max(shown) - one['max']) > 1e-9
                        or abs(sum(shown) / len(shown) - one['mean']) > 1e-9):
                    differ += 1
        check(compared >= 20 and differ == 0,
              'fast: min, max and mean of %d periods equal to the records the pass-through brought (%d differ)'
              % (compared, differ))

        # the pass-through's 0: the client's blocks end, the Studio's stream goes on
        raw.send(WRITE_ACK, enable, 1, b'\x00')
        a = raw.answer()
        time.sleep(0.3)
        before = raw.blocks
        time.sleep(0.5)
        still = j.ask(cmd='get', names=['ADC.I_LOAD'])
        check(a is not None and a[2] == WRITE_ACK_RESP and raw.blocks == before
              and j.ask(cmd='list')['streams'][0]['on'] is True and still.get('ok')
              and still['times']['ADC.I_LOAD'] > times['ADC.I_LOAD'],
              'fast: pass-through, ADC_STREAM written 0: acknowledged, its blocks end; the Studio\'s stream goes on')
        raw.close()
        j.close()

        # the evre package: the JSON API's calls, and Device.stream through the pass-through as from the device
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'python'))
        import evre
        with evre.connect_studio() as s:
            names = [c['name'] for c in s.streams()[0]['channels']]
            value, t = s.fast_value('ADC.I_LOAD')
            got = []
            for t_line, summary in s.fast_stream(['ADC.I_LOAD'], period_ms=50):
                got.append(summary['ADC.I_LOAD'])
                if len(got) == 6:
                    break
            after = s.get('SUPPLY_V', 'ADC.V_BUS')
        check(names == ['ADC.I_LOAD', 'ADC.V_BUS'] and abs(value) <= 6.5 and abs(t - time.time()) < 1.0
              and all(x.n > 300 and x.min <= x.mean <= x.max for x in got[1:])
              and sorted(after) == ['ADC.V_BUS', 'SUPPLY_V'],
              'fast: the evre package: streams(), fast_value(), fast_stream() (%s), get() after it'
              % ', '.join('%d' % x.n for x in got))
        blocks = []
        dev = evre.connect_tcp(HOST, EVRE_PORT, studio.map)
        for block in dev.stream('ADC', seconds=1.0):
            blocks.append(block)
        dev.close()
        count = sum(b.count for b in blocks)
        check(len(blocks) > 20 and 8000 < count < 12000 and sum(b.lost for b in blocks) == 0
              and all(abs(v) <= 6.5 for b in blocks for v in b.values['I_LOAD']),
              'fast: the evre package\'s Device.stream on port 1219 (writes off): %d blocks, %d records in 1 s, none '
              'lost' % (len(blocks), count))
    finally:
        studio.stop()


def main():
    if MODE == 'fast':
        if len(sys.argv) < 3:
            sys.exit('python api_test.py fast <build folder>')
        fast_off(sys.argv[2])
        fast_on(sys.argv[2])
        print('\n%s: %d passed, %d failed' % (MODE, passed, failed))
        sys.exit(1 if failed else 0)
    j = JsonClient()
    test_info(j)
    float1, float2, u8, danger = test_list(j)
    test_no_streams(j)
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

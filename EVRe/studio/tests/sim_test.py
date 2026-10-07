#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""evre-sim, driven with the evre command line: the device does what its map says.

    python sim_test.py <build folder> [--port 1213]

Writes a map with every behaviour into a temporary folder, starts
<build>/evre-sim on 127.0.0.1:<port> (its own process, stopped at the end),
and checks: defaults, moving read-only values inside min..max, write-only,
read-only, action, write-1-to-clear (register and field), a read-only field,
--strict limits, --require-login, --state (persist across a restart), --slave (another slave gets no answer, a
broadcast WRITE is taken, an address outside 1..255 refused), a fast stream (Fast EVRe: switched by its enable
register, its rate register, its blocks recorded with evre record, its host watchdog).
Exit code: 0 all passed, 1 a check failed, 2 a program is missing."""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

passed = failed = 0

MAP = {
    "format": "evre-map/1", "device": "Simulated device", "device_id": "0x1234",
    "login": {"addr": "0xF000", "size": 16},
    "registers": [
        {"addr": "0xD000", "name": "TEMP", "type": "f32", "unit": "C", "min": 20, "max": 30},
        {"addr": "0xD004", "name": "LEVEL", "type": "u16", "access": "rw", "default": 42, "max": 100, "persist": True},
        {"addr": "0xD006", "name": "KEY", "type": "u16", "access": "wo"},
        {"addr": "0xD008", "name": "GO", "type": "u8", "access": "rw", "write": "action", "default": 0},
        {"addr": "0xD009", "name": "FAULTS", "type": "u8", "access": "rw", "write": "w1c",
         "fields": [{"name": "OVER", "bits": "0"}, {"name": "UNDER", "bits": "1"}]},
        {"addr": "0xD00A", "name": "CTRL", "type": "u8", "access": "rw",
         "fields": [{"name": "MODE", "bits": "1:0"}, {"name": "BUSY", "bits": "4", "access": "ro"},
                    {"name": "LATCH", "bits": "7", "access": "w1c"}]},
        {"addr": "0xD00C", "name": "SERIAL", "type": "u32"},
        {"addr": "0xD010", "name": "S_ON", "type": "u8", "access": "rw"},
        {"addr": "0xD014", "name": "S_RATE", "type": "u32", "unit": "Hz"},
    ],
    "streams": [
        {"name": "S", "addr": "0xD800", "size": 64, "rate": 2000, "rate_reg": "S_RATE", "enable": "S_ON",
         "channels": [{"name": "A", "type": "u16"}, {"name": "B", "type": "f32"}]},
    ]}


def crc16(data):
    """CRC-16/X-25, as EVRe frames carry it"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def frame(slave, fn, addr, count, data=b''):
    body = bytes([0x7B, slave, fn]) + struct.pack('<HH', addr, count) + data
    return body + struct.pack('<H', crc16(body)) + b'\x7D'


def stream_checks(start, evre, link, value):
    """a fast stream: evre record, the rate register, a READ of its window, the host watchdog"""
    sim = start()
    try:
        check(value('S_RATE') == 2000 and value('S_ON') == 0, 'stream: its rate register holds the map\'s rate, off')
        rc, _, err = evre('read', link[0], link[1], '--addr', '0xD800', '--count', '8')
        check(rc == 1 and 'out of range' in err, 'stream: a READ of its window is refused (4)')
        out_file = os.path.join(tempfile.mkdtemp(), 's.evrs')
        rc, out, err = evre('record', *link, '--stream', 'S', '-o', out_file, '--seconds', '1.5', '--json')
        summary = json.loads(out.splitlines()[0]) if rc == 0 and out.strip() else {}
        check(rc == 0 and 2500 <= summary.get('records', 0) <= 3600 and summary.get('lost') == 0
              and summary.get('bad_blocks') == 0, 'stream: evre record, 1.5 s at 2000 a second: %s %s'
              % (summary.get('records'), err.strip()))
        check(value('S_ON') == 0, 'stream: off again after the recording')
        # the host watchdog: switched on over a connection that then says nothing; the blocks stop after 2 s
        s = socket.create_connection(('127.0.0.1', int(link[1].split(':')[1])), 2)
        s.sendall(frame(1, 0xEB, 0xD010, 1, b'\x01'))
        s.settimeout(0.2)
        began, arrivals, buf = time.time(), [], b''
        while time.time() - began < 3.5:
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while b'\x7b\x01\xab\x00\xd8' in buf:
                arrivals.append(time.time() - began)
                buf = buf[buf.index(b'\x7b\x01\xab\x00\xd8') + 5:]
        s.close()
        last = max(arrivals) if arrivals else 0
        check(len(arrivals) > 50 and 1.8 <= last <= 2.6, 'stream: a silent host: the blocks stop after 2 s (the last '
              'at %.2f s, %d blocks)' % (last, len(arrivals)))
        check(value('S_ON') == 0, 'stream: ... and its enable register reads 0 again')
    finally:
        sim.kill()
        sim.wait()
    sim = start('--fast-rate', '5000', '--fast-lose', '4')
    try:
        check(value('S_RATE') == 5000, 'stream: --fast-rate 5000: its rate register says so')
        out_file = os.path.join(tempfile.mkdtemp(), 's.evrs')
        rc, out, _ = evre('record', *link, '--stream', 'S', '-o', out_file, '--seconds', '1.5', '--json')
        summary = json.loads(out.splitlines()[0]) if rc == 0 and out.strip() else {}
        total = summary.get('records', 0) + summary.get('lost', 0)
        check(rc == 0 and 6500 <= total <= 8500 and 0.15 <= summary.get('lost', 0) / max(1, total) <= 0.3,
              'stream: --fast-lose 4: a quarter of the records lost and counted (%s of %s)'
              % (summary.get('lost'), total))
    finally:
        sim.kill()
        sim.wait()


def check(ok, what):
    global passed, failed
    print('%s %s' % ('PASS' if ok else 'FAIL', what))
    sys.stdout.flush()
    if ok:
        passed += 1
    else:
        failed += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('--port', type=int, default=1213)
    opts = ap.parse_args()
    ext = '.exe' if os.name == 'nt' else ''
    sim_exe, cli = os.path.join(opts.build, 'evre-sim' + ext), os.path.join(opts.build, 'evre' + ext)
    for exe in (sim_exe, cli):
        if not os.path.exists(exe):
            print('missing: %s' % exe)
            return 2
    tmp = tempfile.mkdtemp()
    map_file, state = os.path.join(tmp, 'sim.json'), os.path.join(tmp, 'state.json')
    with open(map_file, 'w', encoding='utf-8') as f:
        json.dump(MAP, f)
    link = ['--tcp', '127.0.0.1:%d' % opts.port, '--map', map_file]

    def start(*extra):
        p = subprocess.Popen([sim_exe, map_file, '--port', str(opts.port)] + list(extra),
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', opts.port), 0.2).close()
                return p
            except OSError:
                time.sleep(0.1)
        p.kill()
        raise SystemExit('evre-sim did not start')

    def evre(*args, token='example-token'):
        r = subprocess.run([cli] + list(args), capture_output=True, text=True, timeout=30,
                           env=dict(os.environ, EVRE_TOKEN=token))
        return r.returncode, r.stdout, r.stderr

    def value(name):
        rc, out, _ = evre('read', *link, name, '--json')
        rows = [json.loads(l) for l in out.splitlines() if l.strip()]
        return rows[0].get('value') if rc == 0 and rows else None

    stream_checks(start, evre, link, value)
    sim = start('--state', state)
    try:
        rc, out, _ = evre('info', *link, '--json')
        info = json.loads(out) if rc == 0 else {}
        check(info.get('device_id') == '0x1234' and info.get('revision') == 1, 'info: the map\'s device ID, revision 1')
        check(value('LEVEL') == 42, 'a register starts at its default (LEVEL 42)')
        temps = [value('TEMP') for _ in range(5)]
        check(all(t is not None and 20 <= t <= 30 for t in temps), 'a read-only float moves inside min..max: %s'
              % ['%.2f' % t for t in temps if t is not None])
        rc, _, err = evre('read', *link, 'KEY')
        check(rc == 1 and 'permission' in err, 'a write-only register refuses a read')
        rc, _, _ = evre('write', *link, 'KEY=7')
        check(rc == 0, '... and takes a write (not read back)')
        rc, _, err = evre('write', '--tcp', '127.0.0.1:%d' % opts.port, '--map', map_file, 'SERIAL=1')
        check(rc == 1 and 'read-only' in err, 'a read-only register: refused (by evre already)')

        rc, out, _ = evre('write', *link, 'GO=1', '--json')
        held = json.loads(out.splitlines()[0]).get('value') if rc == 0 else None
        time.sleep(0.4)
        check(held == 1 and value('GO') == 0, 'an action register: holds 1, then reads back idle (0)')

        check(value('FAULTS') == 3, 'w1c bits start set (FAULTS 0x03)')
        evre('write', *link, 'FAULTS=0x01')
        check(value('FAULTS') == 2, 'a 1 written clears that bit, a 0 leaves the other (0x02)')
        start_ctrl = value('CTRL')
        check(start_ctrl == 0x80, 'CTRL: its w1c field LATCH starts set (0x80)')
        evre('write', *link, 'CTRL=0x93')  # MODE=3, BUSY (ro) 1, LATCH 1 (clears)
        check(value('CTRL') == 0x03, 'fields: MODE written, the ro BUSY bit kept, LATCH cleared (0x03)')

        # persist, across a restart
        evre('write', *link, 'LEVEL=77')
    finally:
        sim.kill()
        sim.wait()
    sim = start('--state', state)
    try:
        check(value('LEVEL') == 77, 'persist: LEVEL 77 after a restart with --state')
    finally:
        sim.kill()
        sim.wait()

    # strict limits, and a required login
    sim = start('--strict', '--require-login')
    try:
        rc, _, _ = evre('write', *link, 'LEVEL=150', '--force')
        check(rc == 1, '--strict: a value past max is refused by the device, even with --force')
        rc, _, _ = evre('write', *link, 'LEVEL=50', token='')
        check(rc == 1, '--require-login: no token, no write')
        rc, _, _ = evre('write', *link, 'LEVEL=50', token='wrong-token')
        check(rc == 1, '--require-login: a wrong token, no write')
        rc, _, _ = evre('write', *link, 'LEVEL=50')
        check(rc == 0 and value('LEVEL') == 50, '--require-login: with the token, written')
    finally:
        sim.kill()
        sim.wait()

    # another slave address: a frame for any other gets no answer, a broadcast WRITE is taken
    sim = start('--slave', '5')
    try:
        rc, _, err = evre('read', *link, '--slave', '4', '--timeout', '300', 'LEVEL', token='')
        check(rc == 1 and 'timeout' in err, '--slave 5: a request for slave 4 gets no answer')
        rc, out, _ = evre('broadcast', *link, '--slave', '5', 'LEVEL=60', '--json')
        back = json.loads(out.splitlines()[0]).get('value') if rc == 0 and out.strip() else None
        check(back == 60, '--slave 5: a broadcast WRITE (slave 0) is taken, read back 60')
    finally:
        sim.kill()
        sim.wait()
    for bad in ('0', '256', 'x'):
        r = subprocess.run([sim_exe, map_file, '--port', str(opts.port), '--slave', bad], capture_output=True,
                           text=True, timeout=10)
        check(r.returncode == 2 and '1 to 255' in r.stderr, '--slave %s: refused (1 to 255)' % bad)
    print('\n%d passed, %d failed' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

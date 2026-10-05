#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The evre command-line tool against its own fake device (tests/fake_device.py).

    python cli_test.py <build folder> [--port 1212]

Starts the fake device on 127.0.0.1:<port> (not the GUI test's 1210) with the
example map, runs <build folder>/evre with every command, and stops the device
(by its own process, nothing else). The writes go to the fake device only.
Exit code: 0 all passed, 1 a check failed, 2 the tool or the device is missing."""
import argparse
import json
import os
import math
import socket
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MAP = os.path.join(HERE, '..', 'maps', 'example_device.json')
FAST_MAP = os.path.join(HERE, '..', 'maps', 'example_fast.json')
TOKEN = 'example-token'  # fake_device.py's default
passed = failed = 0


def check(ok, what):
    global passed, failed
    print('%s %s' % ('PASS' if ok else 'FAIL', what))
    sys.stdout.flush()
    if ok:
        passed += 1
    else:
        failed += 1


def bus_checks(opts, tool, run, lines_json):
    """--bus: two devices of the example map on one link (evre_fake_fast --node), their registers named D1_, D2_"""
    fast = os.path.join(opts.build, 'evre_fake_fast.exe' if os.name == 'nt' else 'evre_fake_fast')
    if not os.path.exists(fast):
        check(False, 'bus: evre_fake_fast is missing')
        return
    port = opts.port + 20
    link = ['--tcp', '127.0.0.1:%d' % port]
    with tempfile.TemporaryDirectory() as tmp:
        bus = os.path.join(tmp, 'bus.json')
        with open(bus, 'w', encoding='utf-8') as f:
            json.dump({'format': 'evre-bus/1', 'devices': [{'name': 'D1', 'slave': 1, 'map': os.path.abspath(MAP)},
                                                           {'name': 'D2', 'slave': 2, 'map': os.path.abspath(MAP)}]}, f)
        fake = subprocess.Popen([fast, str(port), os.path.abspath(MAP), TOKEN, '--slave', '1',
                                 '--node', '2=' + os.path.abspath(MAP)], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        try:
            for _ in range(50):
                try:
                    socket.create_connection(('127.0.0.1', port), 0.2).close()
                    break
                except OSError:
                    time.sleep(0.1)
            rc1, _, _ = run('write', *link, '--bus', bus, 'D1_FAN_SPEED=11')
            rc2, _, _ = run('write', *link, '--bus', bus, 'D2_FAN_SPEED=22')
            rc, out, _ = run('read', *link, '--bus', bus, 'D1_FAN_SPEED', 'D2_FAN_SPEED', '--json')
            values = {r['name']: r.get('value') for r in lines_json(out)} if rc == 0 else {}
            check(rc1 == 0 and rc2 == 0 and values == {'D1_FAN_SPEED': 11, 'D2_FAN_SPEED': 22},
                  'bus: --bus writes and reads D1_ and D2_ registers, each on its device (%s)' % values)
            rc, out, _ = run('dump', *link, '--bus', bus, '--json')
            names = {r['name'] for r in lines_json(out)} if rc == 0 else set()
            check('D1_SUPPLY_V' in names and 'D2_SUPPLY_V' in names, 'bus: dump reads every device')
            rc, out, _ = run('broadcast', *link, '--bus', bus, 'D1_FAN_SPEED=33', '--json')
            values = {r['name']: r.get('value') for r in lines_json(out)}
            check(rc == 0 and values == {'D1_FAN_SPEED': 33, 'D2_FAN_SPEED': 33},
                  'bus: broadcast reaches every device and reads each back (%s)' % values)
            rc, out, _ = run('broadcast', *link, '--bus', bus, 'FAN_SPEED=44', '--json')
            values = {r['name']: r.get('value') for r in lines_json(out)}
            check(rc == 0 and values == {'D1_FAN_SPEED': 44, 'D2_FAN_SPEED': 44},
                  'bus: broadcast by the map\'s own name (FAN_SPEED) as well (%s)' % values)
            rc, _, err = run('broadcast', *link, '--bus', bus, 'D1_DEVICE_ID=5')
            check(rc == 1 and 'read-only' in err, 'bus: no broadcast to DEVICE_ID (%s)' % err.strip())
            run('broadcast', *link, '--bus', bus, 'FAN_SPEED=0')
        finally:
            fake.kill()
            fake.wait()


def slave_checks(opts, run, lines_json):
    """fake_device.py at another slave (--slave 3): it answers only its own address and takes a broadcast; the
    broadcast of one device (--map) and the broadcast rule's refusals"""
    port = opts.port + 24  # 1236: clear of the fake bus launchers that may run beside (1230 .. 1233)
    link = ['--tcp', '127.0.0.1:%d' % port, '--map', MAP]
    try:
        socket.create_connection(('127.0.0.1', port), 0.2).close()
        check(False, 'slave checks: port %d is taken by another program' % port)
        return
    except OSError:
        pass  # free, as it should be
    fake = subprocess.Popen([sys.executable, os.path.join(HERE, 'fake_device.py'), '--port', str(port), '--map', MAP,
                             '--slave', '3'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', port), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        rc, _, err = run('read', *link, '--slave', '2', '--timeout', '300', 'FAN_SPEED', token='')
        check(rc == 1 and 'timeout' in err, 'fake device at slave 3: a request for slave 2 gets no answer (%s)' % err.strip())
        rc, out, _ = run('read', *link, '--slave', '3', 'FAN_SPEED', '--json')
        check(rc == 0 and lines_json(out), 'fake device at slave 3: answers its own address')
        rc, out, _ = run('broadcast', *link, '--slave', '3', 'FAN_SPEED=7', '--json')
        values = {r['name']: r.get('value') for r in lines_json(out)}
        check(rc == 0 and values == {'FAN_SPEED': 7},
              'broadcast --map (one device): slave 0 taken by the device, read back (%s)' % values)
        run('broadcast', *link, '--slave', '3', 'FAN_SPEED=0')
        rc, _, err = run('broadcast', *link, '--slave', '3', 'CONFIG=0x4F08', '--force')
        check(rc == 1 and 'AUTO_SEND' in err, 'broadcast that switches AUTO_SEND on: refused (%s)' % err.strip())
        rc, out, err = run('broadcast', *link, '--slave', '3', 'CONFIG=0x4F00', '--force', '--json')
        check(rc == 0, 'broadcast of CONFIG with AUTO_SEND clear: sent (%s)' % (out.strip() or err.strip()))
        rc, _, err = run('broadcast', *link, '--slave', '3', 'SUPPLY_V=1')
        check(rc == 1 and 'read-only' in err, 'broadcast to a read-only register: refused (%s)' % err.strip())
    finally:
        fake.kill()
        fake.wait()


def read_evrs(path):
    """a .evrs file's pieces: [(name, body)], up to the last whole one"""
    pieces = []
    data = open(path, 'rb').read()
    at = 0
    while at + 8 <= len(data):
        name, size = data[at:at + 4].decode('ascii', 'replace'), struct.unpack_from('<I', data, at + 4)[0]
        if at + 8 + size > len(data):
            break
        pieces.append((name, data[at + 8:at + 8 + size]))
        at += 8 + size
    return pieces


def wave(channel, record, rate):
    """the fake devices' i16 wave (src/io/fast_stream.cpp, FastSource::wave)"""
    return round(13000 * math.sin(2 * math.pi * 50.0 * (channel + 1) * record / rate + channel))


def fast_checks(opts, run, lines_json):
    """Fast EVRe: evre info lists a map's streams, validate checks them, evre record writes a .evrs (evre_fake_fast)"""
    fast = os.path.join(opts.build, 'evre_fake_fast.exe' if os.name == 'nt' else 'evre_fake_fast')
    if not os.path.exists(fast):
        check(False, 'fast: evre_fake_fast is missing')
        return
    port = opts.port + 26  # 1238
    link = ['--tcp', '127.0.0.1:%d' % port, '--map', FAST_MAP]
    rc, out, _ = run('validate', FAST_MAP)
    check(rc == 0 and '0 error(s)' in out, 'fast: validate the fast example map: no errors')
    with tempfile.TemporaryDirectory() as tmp:
        bad = os.path.join(tmp, 'bad_stream.json')
        doc = json.load(open(FAST_MAP, encoding='utf-8'))
        doc['streams'][0]['addr'] = '0xD000'  # over UPTIME
        doc['streams'][0]['enable'] = 'UPTIME'  # read-only
        with open(bad, 'w', encoding='utf-8') as f:
            json.dump(doc, f)
        rc, out, _ = run('validate', bad, '--json')
        texts = [i['text'] for i in lines_json(out) if i['error']]
        check(rc == 1 and any('shares bytes with the register UPTIME' in t for t in texts)
              and any('cannot write' in t for t in texts), 'fast: validate refuses a window over a register and an '
              'enable that cannot be written (%s)' % texts)
        for args, what in (([], 'the map\'s rate'), (['--fast-lose', '5', '--fast-first', '4294967000'],
                                                     'every 5th block lost, across the wrap')):
            fake = subprocess.Popen([fast, str(port), os.path.abspath(FAST_MAP)] + args, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
            try:
                for _ in range(50):
                    try:
                        socket.create_connection(('127.0.0.1', port), 0.2).close()
                        break
                    except OSError:
                        time.sleep(0.1)
                if not args:
                    rc, out, _ = run('info', *link, '--json')
                    info = (lines_json(out) or [{}])[0]
                    streams = info.get('streams', [])
                    check(rc == 0 and len(streams) == 1 and streams[0]['name'] == 'ADC' and streams[0]['addr'] == '0xDC00'
                          and [c['name'] for c in streams[0]['channels']] == ['I_LOAD', 'V_BUS'],
                          'fast: info lists the map\'s stream and its channels')
                    rc, out, _ = run('info', *link)
                    check('fast stream   ADC: window 0xDC00, 1024 bytes, 10000 records/s' in out,
                          'fast: info in words: %s' % [l for l in out.splitlines() if 'fast' in l])
                    rc, _, err = run('record', *link, '--stream', 'NOPE', '-o', os.path.join(tmp, 'x.evrs'))
                    check(rc == 2 and 'no stream NOPE' in err, 'fast: record a stream the map has not: exit 2')
                target = os.path.join(tmp, 'run.ADC.evrs')
                rc, out, err = run('record', *link, '--stream', 'adc', '-o', target, '--seconds', '2', '--json')
                summary = (lines_json(out) or [{}])[0]
                pieces = read_evrs(target) if os.path.exists(target) else []
                head = json.loads(pieces[0][1]) if pieces and pieces[0][0] == 'EVRS' else {}
                blocks = [b for n, b in pieces if n == 'BLK ']
                check(rc == 0 and head.get('format') == 'evre-fast-rec/1' and head.get('device') == 'Example fast device'
                      and head.get('stream', {}).get('name') == 'ADC' and 'start' in head,
                      'fast: record (%s): the head (%s)' % (what, err.strip()))
                check(len(pieces) > 1 and pieces[1][0] == 'TIME' and len(pieces[1][1]) == 16,
                      'fast: record: a time mark before the first block')
                # the blocks as they came: the numbers, the losses, the values
                expected, lost, records, wrong = None, 0, 0, 0
                for b in blocks:
                    first, count, flags, spare = struct.unpack_from('<IHBB', b)
                    if expected is not None:
                        lost += (first - expected) % (1 << 32)
                    expected = (first + count) % (1 << 32)
                    records += count
                    for k in (0, count - 1) if count else ():
                        v = struct.unpack_from('<hh', b, 8 + 4 * k)
                        number = (first + k) % (1 << 32)
                        if abs(v[0] - wave(0, number, 10000)) > 1 or abs(v[1] - wave(1, number, 10000)) > 1:
                            wrong += 1
                check(records == summary.get('records') and lost == summary.get('lost') and wrong == 0
                      and 15000 <= records + lost <= 25000, 'fast: record: %d records in %d blocks, %d lost, as the '
                      'summary says, each value as the device made it' % (records, len(blocks), lost))
                if args:
                    check(lost >= 3 * 254 and summary.get('starts') == 1,
                          'fast: record: the lost blocks counted (%d records), no restart at the wrap' % lost)
                rc, out, _ = run('read', *link, 'ADC_STREAM', '--json')
                check((lines_json(out) or [{}])[0].get('value') == 0, 'fast: record switched the stream off at the end')
            finally:
                fake.kill()
                fake.wait()
        # nothing sends: a stream without an enable register (only listened to) at the plain fake device
        quiet = os.path.join(tmp, 'listen.json')
        doc = json.load(open(FAST_MAP, encoding='utf-8'))
        del doc['streams'][0]['enable']
        with open(quiet, 'w', encoding='utf-8') as f:
            json.dump(doc, f)
        rc, _, err = run('record', '--tcp', '127.0.0.1:%d' % opts.port, '--map', quiet, '--stream', 'ADC', '-o',
                         os.path.join(tmp, 'none.evrs'), '--seconds', '3')
        check(rc == 1 and 'no block came in 2 s' in err, 'fast: record from a device that does not stream: exit 1 (%s)'
              % err.strip())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('--port', type=int, default=1212)
    opts = ap.parse_args()
    tool = os.path.join(opts.build, 'evre.exe' if os.name == 'nt' else 'evre')
    if not os.path.exists(tool):
        print('missing: %s' % tool)
        return 2
    link = ['--tcp', '127.0.0.1:%d' % opts.port]

    def run(*args, token=TOKEN):
        r = subprocess.run([tool] + list(args), capture_output=True, text=True, timeout=30,
                           env=dict(os.environ, EVRE_TOKEN=token))
        return r.returncode, r.stdout, r.stderr

    def lines_json(text):
        return [json.loads(line) for line in text.splitlines() if line.strip()]

    device = subprocess.Popen([sys.executable, os.path.join(HERE, 'fake_device.py'), '--port', str(opts.port),
                               '--map', MAP], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):  # until it listens
            try:
                socket.create_connection(('127.0.0.1', opts.port), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        else:
            print('the fake device did not start on port %d' % opts.port)
            return 2

        # without a device
        rc, out, _ = run('validate', MAP)
        check(rc == 0 and '0 error(s)' in out, 'validate the example map: no errors (%s)' % out.strip().splitlines()[-1])
        with tempfile.TemporaryDirectory() as tmp:
            bad = os.path.join(tmp, 'bad.json')
            with open(bad, 'w', encoding='utf-8') as f:
                json.dump({'format': 'evre-map/1', 'registers': [
                    {'addr': '0x10', 'name': 'A', 'type': 'u16'}, {'addr': '0x11', 'name': 'A', 'type': 'u8'}]}, f)
            rc, out, _ = run('validate', bad, '--json')
            issues = lines_json(out)
            check(rc == 1 and any(i['error'] and 'is also' in i['text'] for i in issues),
                  'validate a map with a name used twice: exit 1, the error in JSON')
            for kind, marker in (('md', '# Example device'), ('h', '#define'), ('py', 'REGISTERS = {'), ('csv', 'addr,name')):
                target = os.path.join(tmp, 'out.' + kind)
                rc, _, _ = run('export', MAP, '--to', kind, '--prefix', 'ex', '-o', target)
                text = open(target, encoding='utf-8').read() if os.path.exists(target) else ''
                check(rc == 0 and marker in text, 'export --to %s: written (%d bytes)' % (kind, len(text)))
        rc, _, err = run('export', MAP, '--to', 'pdf')
        check(rc == 2 and '--to' in err, 'export --to pdf: refused as a usage error (exit 2)')

        # against the device
        rc, out, _ = run('info', *link, '--map', MAP, '--json')
        info = (lines_json(out) or [{}])[0]
        check(rc == 0 and info.get('device_id') == '0x1001' and info.get('map_matches'),
              'info: device ID 0x1001, the map matches')
        rc, out, _ = run('read', *link, '--map', MAP, 'SUPPLY_V', 'led_mode', '--json')
        values = {v['name']: v for v in lines_json(out)}
        check(rc == 0 and isinstance(values.get('SUPPLY_V', {}).get('value'), float) and 'LED_MODE' in values,
              'read by name (any case): SUPPLY_V %s' % values.get('SUPPLY_V', {}).get('value'))
        rc, out, _ = run('read', *link, '--addr', '0xA000', '--count', '2')
        check(rc == 0 and out.strip() == '01 10', 'read raw 0xA000: "%s"' % out.strip())
        rc, out, _ = run('dump', *link, '--map', MAP, '--json')
        dumped = lines_json(out)
        polled = [r for r in json.load(open(MAP, encoding='utf-8'))['registers']
                  if r.get('type') != 'bytes' or r.get('size', 1) <= 32]
        check(rc == 0 and len(dumped) == len(polled), 'dump: every polled register (%d of %d)' % (len(dumped), len(polled)))
        rc, out, _ = run('watch', *link, '--map', MAP, 'SUPPLY_V', 'SUPPLY_I', '--interval', '50', '--count', '4')
        rows = out.strip().splitlines()
        check(rc == 0 and len(rows) == 5 and rows[0].startswith('time_s,SUPPLY_V [V]'), 'watch: a header and 4 rows')

        # writes: explicit, checked, read back. A wrong token stops before anything is written (the fake
        # device checks a login but does not require one)
        rc, _, err = run('write', *link, '--map', MAP, 'FAN_SPEED=6', token='not-the-token')
        rc2, out2, _ = run('read', *link, '--map', MAP, 'FAN_SPEED', '--json')
        check(rc == 1 and 'token refused' in err and (lines_json(out2) or [{}])[0].get('value') != 6,
              'a wrong token: refused, nothing written (%s)' % err.strip())
        rc, out, _ = run('write', *link, '--map', MAP, 'FAN_SPEED=5', '--json')
        w = (lines_json(out) or [{}])[0]
        check(rc == 0 and w.get('value') == 5, 'write FAN_SPEED=5: read back 5')
        rc, _, err = run('write', *link, '--map', MAP, 'FAN_SPEED=150')
        check(rc == 1 and 'maximum' in err, 'write past the map\'s max: refused (%s)' % err.strip())
        rc, _, err = run('write', *link, '--map', MAP, 'MOTOR_SPEED=10')
        check(rc == 1 and 'danger' in err, 'write a danger register without --force: refused')
        rc, out, _ = run('write', *link, '--map', MAP, 'MOTOR_SPEED=10', '--force', '--json')
        check(rc == 0 and (lines_json(out) or [{}])[0].get('value') == 10, 'write it with --force: read back 10')
        run('write', *link, '--map', MAP, 'MOTOR_SPEED=0', 'FAN_SPEED=0', '--force')
        rc, _, err = run('write', *link, '--map', MAP, 'SUPPLY_V=1')
        check(rc == 1 and 'read-only' in err, 'write a read-only register: refused')
        # check: the device answers as its map says
        rc, out, _ = run('check', *link, '--map', MAP, '--json')
        results = lines_json(out)
        check(rc == 0 and results and all(r['result'] != 'FAIL' for r in results),
              'check (reads): %d registers, no failure' % len(results))
        rc, out, _ = run('check', *link, '--map', MAP, '--writes', '--json')
        writable = [r for r in lines_json(out) if 'writable' in r['text']]
        check(rc == 0 and len(writable) >= 3 and any('danger' in r['text'] for r in lines_json(out)),
              'check --writes: the writable registers written back (%d), the danger one skipped' % len(writable))
        with tempfile.TemporaryDirectory() as tmp:
            wrong = os.path.join(tmp, 'wrong.json')
            doc = json.load(open(MAP, encoding='utf-8'))
            doc['device_id'] = '0x2002'
            doc['registers'].append({'addr': '0xD300', 'name': 'MISSING', 'type': 'u16'})
            with open(wrong, 'w', encoding='utf-8') as f:
                json.dump(doc, f)
            rc, out, _ = run('check', *link, '--map', wrong, '--json')
            failed_names = {r['register'] for r in lines_json(out) if r['result'] == 'FAIL'}
            check(rc == 1 and failed_names == {'DEVICE_ID', 'MISSING'},
                  'check with a wrong map: DEVICE_ID and the missing register fail (%s)' % sorted(failed_names))
        rc, _, err = run('read', '--tcp', '127.0.0.1:1', '--map', MAP, 'SUPPLY_V', '--timeout', '300')
        check(rc == 1 and err.strip(), 'no device at the port: exit 1, why (%s)' % err.strip())
        bus_checks(opts, tool, run, lines_json)
        slave_checks(opts, run, lines_json)
        fast_checks(opts, run, lines_json)
    finally:
        device.kill()
        device.wait()
    print('\n%d passed, %d failed' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

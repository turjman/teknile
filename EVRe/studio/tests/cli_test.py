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
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MAP = os.path.join(HERE, '..', 'maps', 'example_device.json')
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


def login_checks(opts, run):
    """a device that requires a login (evre_fake_fast --login-required): code 13 is named "login required";
    with the token the read works"""
    fast = os.path.join(opts.build, 'evre_fake_fast.exe' if os.name == 'nt' else 'evre_fake_fast')
    if not os.path.exists(fast):
        check(False, 'login: evre_fake_fast is missing')
        return
    port = opts.port + 26  # 1238
    link = ['--tcp', '127.0.0.1:%d' % port]
    fake = subprocess.Popen([fast, str(port), os.path.abspath(MAP), TOKEN, '--login-required'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', port), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)
        rc, _, err = run('read', *link, '--map', MAP, 'FAN_SPEED', token='')
        check(rc == 1 and 'login required' in err, 'login required: without a token, code 13 is named (%s)' % err.strip())
        rc, out, _ = run('read', *link, '--map', MAP, 'FAN_SPEED')
        check(rc == 0 and 'FAN_SPEED' in out, 'login required: with the token, the read works')
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
            # EVRe Guard's table: FILE.h and FILE.cpp; --check: 0 while they are as the map exports them
            base = os.path.join(tmp, 'example_guard')
            rc, _, _ = run('export', MAP, '--to', 'guard', '-o', base)
            made = all(os.path.exists(base + ext) for ext in ('.h', '.cpp'))
            rc_check, out, _ = run('export', MAP, '--to', 'guard', '-o', base, '--check')
            check(rc == 0 and made and rc_check == 0 and 'as the map exports it' in out,
                  'export --to guard: the .h and the .cpp; --check finds them fresh')
            with open(base + '.h', 'a', encoding='utf-8') as f:
                f.write('\n')
            rc_check, _, err = run('export', MAP, '--to', 'guard', '-o', base, '--check')
            check(rc_check == 1 and 'export it again' in err, 'export --check: a file changed since: exit 1, which one')
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
        rc, _, err = run('write', *link, '--map', MAP, 'SETPOINT=nan', '--force')
        check(rc == 1 and 'not a finite number' in err, 'write NaN to an f32: never sent, even with --force (%s)' % err.strip())
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
        login_checks(opts, run)
        slave_checks(opts, run, lines_json)
    finally:
        device.kill()
        device.wait()
    print('\n%d passed, %d failed' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

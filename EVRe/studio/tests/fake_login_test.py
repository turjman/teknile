#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The login of both fake devices, checked the same way, through the probe too.

    python fake_login_test.py <build folder> [--port 1211] [--map ../maps/example_device.json]

Starts tests/fake_device.py, then <build folder>/evre_fake_fast, one after the
other on 127.0.0.1:<port>, each with the map (it must declare "login") and its
default token, example-token. On each device it checks:

  - a write of the whole login register holding the token: acknowledged
  - a read of the login register: the token, zero-padded to the size
  - a write of the whole register holding another token: ERROR_RESP 3
    (permission denied), and a read then returns that token (kept though refused)
  - a write of part of the register: ERROR_RESP 3, and the register unchanged
  - evre_probe with EVRE_TOKEN=example-token prints "token: accepted"
  - evre_probe with a wrong EVRE_TOKEN prints "token: permission denied"

So the two fake devices cannot drift apart without a check failing, and the
probe's login is covered too. The port is not the GUI test's (1210), so it
may run beside a fake device already on 1210.

It stops only the processes it started. Exit code: 0 all passed, 1 a check
failed, 2 a program or the map is missing, or a device did not start.
Standard library only.
"""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True  # importing fake_device leaves no __pycache__ in tests/
from fake_device import (DEFAULT_TOKEN, END, ERROR_RESP, PERMISSION_DENIED, READ, READ_RESP, START,  # noqa: E402
                         WRITE_ACK, WRITE_ACK_RESP, crc16, frame)

WRONG_TOKEN = 'not-the-token'
PROBE_CYCLES = '3'

passed = failed = 0


def check(ok, what):
    global passed, failed
    print('%s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    if ok:
        passed += 1
    else:
        failed += 1


def padded(token, size):
    """a token as the Studio sends it: UTF-8, cut or zero-padded to the size (STUDIO.md section 3.6)"""
    encoded = token.encode('utf-8')[:size]
    return encoded + bytes(size - len(encoded))


class Client:
    """plain blocking EVRe over one socket: one request, one answer"""

    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.buf = b''

    def close(self):
        self.sock.close()

    def request(self, fn, addr, cnt, data=b''):
        """the answer as (fn, data), or (None, b'') when none came within 3 s"""
        self.sock.sendall(frame(1, fn, addr, cnt, data))  # both fakes are slave 1 (the map's)
        try:
            while True:
                answer = self.take_answer()
                if answer:
                    return answer
                chunk = self.sock.recv(4096)
                if not chunk:
                    return None, b''
                self.buf += chunk
        except socket.timeout:
            return None, b''

    def take_answer(self):
        """one whole answer frame from the start of the buffer, None if not all of it is in yet"""
        i = self.buf.find(bytes([START]))
        if i < 0 or len(self.buf) - i < 7:
            return None
        self.buf = self.buf[i:]
        fn = self.buf[2]
        cnt = struct.unpack_from('<H', self.buf, 5)[0]
        data_len = {READ_RESP: cnt, WRITE_ACK_RESP: 0, ERROR_RESP: 1}.get(fn, 0)
        total = 10 + data_len
        if len(self.buf) < total:
            return None
        f, self.buf = self.buf[:total], self.buf[total:]
        if f[-1] != END or struct.unpack('<H', f[-3:-1])[0] != crc16(f[:-3]):
            return fn, None  # a broken answer: no data a check could accept
        return fn, f[7:7 + data_len]

    def read(self, addr, cnt):
        fn, data = self.request(READ, addr, cnt)
        return data if fn == READ_RESP else None

    def write_ack(self, addr, data):
        return self.request(WRITE_ACK, addr, len(data), data)


def start(command, port):
    """starts a device and waits until it takes connections: the process, or None"""
    try:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError as e:
        print('cannot start %s: %s' % (command[0], e))
        return None
    deadline = time.time() + 10
    while time.time() < deadline and process.poll() is None:
        try:
            socket.create_connection(('127.0.0.1', port), timeout=0.5).close()
            return process
        except OSError:
            time.sleep(0.1)
    stop(process)
    print('%s did not start on 127.0.0.1:%d' % (command[0], port))
    return None


def stop(process):
    """only the process started here, by its handle"""
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def probe_says(probe, port, map_path, token):
    """what evre_probe prints about the token (its "token: ..." line), empty if nothing"""
    env = dict(os.environ, EVRE_TOKEN=token)
    try:
        run = subprocess.run([probe, 'tcp', '127.0.0.1', str(port), map_path, PROBE_CYCLES], env=env,
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as e:
        return 'no answer from the probe: %s' % e
    lines = [line for line in run.stdout.splitlines() if line.startswith('token: ')]
    return lines[0] if lines else ''


def check_device(name, port, login_addr, login_size, probe, map_path):
    client = Client(port)
    token = padded(DEFAULT_TOKEN, login_size)
    wrong = padded(WRONG_TOKEN, login_size)

    fn, _ = client.write_ack(login_addr, token)
    check(fn == WRITE_ACK_RESP, '%s: the whole login register with the token: acknowledged' % name)
    check(client.read(login_addr, login_size) == token, '%s: ... read back: the token, zero-padded' % name)

    fn, data = client.write_ack(login_addr, wrong)
    check(fn == ERROR_RESP and data == bytes([PERMISSION_DENIED]),
          '%s: another token: ERROR_RESP 3 (permission denied)' % name)
    check(client.read(login_addr, login_size) == wrong, '%s: ... read back: that token, kept though refused' % name)

    fn, data = client.write_ack(login_addr, token[:4])
    check(fn == ERROR_RESP and data == bytes([PERMISSION_DENIED]) and client.read(login_addr, login_size) == wrong,
          '%s: part of the login register: ERROR_RESP 3, the register unchanged' % name)
    client.close()

    said = probe_says(probe, port, map_path, DEFAULT_TOKEN)
    check(said == 'token: accepted', '%s: evre_probe with the token: "token: accepted" (%s)' % (name, said))
    said = probe_says(probe, port, map_path, WRONG_TOKEN)
    check(said == 'token: permission denied',
          '%s: evre_probe with a wrong token: "token: permission denied" (%s)' % (name, said))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build', help='the folder with evre_fake_fast and evre_probe')
    ap.add_argument('--port', type=int, default=1211)
    ap.add_argument('--map', default=os.path.join(HERE, '..', 'maps', 'example_device.json'))
    args = ap.parse_args()

    exe = '.exe' if os.name == 'nt' else ''
    fast = os.path.join(args.build, 'evre_fake_fast' + exe)
    probe = os.path.join(args.build, 'evre_probe' + exe)
    map_path = os.path.abspath(args.map)
    for path in (fast, probe, map_path):
        if not os.path.isfile(path):
            print('missing: %s' % path)
            return 2
    with open(map_path, encoding='utf-8') as f:
        login = json.load(f).get('login')
    if not login:
        print('the map %s declares no login register' % map_path)
        return 2
    login_addr, login_size = int(login['addr'], 16), login.get('size', 16)

    devices = [
        ('fake_device.py', [sys.executable, os.path.join(HERE, 'fake_device.py'), '--port', str(args.port),
                            '--map', map_path]),
        ('evre_fake_fast', [fast, str(args.port), map_path]),
    ]
    for name, command in devices:
        process = start(command, args.port)
        if not process:
            return 2
        try:
            check_device(name, args.port, login_addr, login_size, probe, map_path)
        finally:
            stop(process)

    print('\n%d passed, %d failed' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

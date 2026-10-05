#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A fake EVRe device over TCP, for trying EVRe Studio without hardware.

    python fake_device.py [--port 1210] [--map ../maps/example_device.json]
                          [--token example-token] [--slave N]

It serves the registers of a map (default: maps/example_device.json) from a
64 KiB memory: DEVICE_ID from the map, read-only float registers moving as
slow sine waves, read-only 16/32-bit signed integers without bit fields as
slower ones, read-only u32 registers in ms counting the milliseconds since the
start, read-write registers kept as written. Addresses that are in no register
of the map are refused with ERROR_RESP 4 (offset out of range), so a block
read over a hole makes EVRe Studio split its block.

The login: when the map declares "login" (addr, size), that register exists
too, outside the map's registers. A write of exactly that register is
accepted (WRITE_ACK_RESP) when it holds the device's token (--token, UTF-8,
zero-padded to the size) and refused with ERROR_RESP 3 (permission denied)
when it holds anything else; a write of part of it is refused as well. The
login is checked, never required: the device answers every client, logged
in or not. A read of the login register returns the last token written to
it, accepted or refused, so a test can see what arrived.

The device answers its own slave address only (--slave, else the map's
"slave", else 1); a frame for another slave gets no answer. A broadcast
(slave 0) WRITE is taken and not answered; any other broadcast is dropped.
Standard library only.
"""
import argparse
import json
import math
import os
import socket
import struct
import threading
import time

READ, READ_RESP, WRITE, WRITE_ACK, WRITE_ACK_RESP, ERROR_RESP = 0xAA, 0xAB, 0xEA, 0xEB, 0xEC, 0xEE
PERMISSION_DENIED = 3    # the ERROR_RESP code for a wrong token
OFFSET_OUT_OF_RANGE = 4  # the ERROR_RESP code for an address in no register
DEFAULT_TOKEN = 'example-token'  # the token the device accepts, unless --token says otherwise
START, END = 0x7B, 0x7D
PACKING = {'u8': '<B', 'i8': '<b', 'u16': '<H', 'i16': '<h', 'u32': '<I', 'i32': '<i', 'f32': '<f'}
SIZES = {t: struct.calcsize(p) for t, p in PACKING.items()}


def crc16(data):
    """CRC-16/X-25 over everything before the CRC"""
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x8408 if c & 1 else c >> 1
    return c ^ 0xFFFF


BROADCAST = 0  # the slave address every device hears, and none answers


def frame(slave, fn, addr, cnt, data=b''):
    """7B SLAVE FN OFF(2) CNT(2) DATA CRC(2) 7D, little endian"""
    body = struct.pack('<BBBHH', START, slave, fn, addr, cnt) + data
    return body + struct.pack('<H', crc16(body)) + bytes([END])


class Device:
    """The memory behind the map's registers, shared by all connections."""

    def __init__(self, map_path, token=DEFAULT_TOKEN, slave=None):
        with open(map_path, encoding='utf-8') as f:
            m = json.load(f)
        self.slave = slave if slave is not None else int(m.get('slave', 1))
        self.regs = m['registers']
        self.mem = bytearray(0x10000)
        self.valid = bytearray(0x10000)  # 1 = some register (or the login register) covers this byte
        self.lock = threading.Lock()
        self.t0 = time.time()
        for r in self.regs:
            a = int(r['addr'], 16)
            for i in range(r.get('size') or SIZES[r['type']]):
                self.valid[a + i] = 1
        # the login register: None when the map declares none
        self.login_addr, self.login_size = None, 0
        if 'login' in m:
            self.login_addr = int(m['login']['addr'], 16)
            self.login_size = m['login'].get('size', 16)
            self.valid[self.login_addr:self.login_addr + self.login_size] = bytes([1]) * self.login_size
        encoded = token.encode('utf-8')[:self.login_size]
        self.token = encoded + bytes(self.login_size - len(encoded))  # as the Studio sends it
        self.put(0xA000, 'u16', int(m.get('device_id', '0x0001'), 16))  # DEVICE_ID
        self.put(0xA002, 'u16', 0x3F01)  # STATUS: protocol revision 1, capability bits

    def put(self, addr, type_name, value):
        struct.pack_into(PACKING[type_name], self.mem, addr, value)

    def animate(self):
        """moves the read-only values (called every 20 ms)"""
        t = time.time() - self.t0
        with self.lock:
            for k, r in enumerate(self.regs):
                if r['access'] == 'rw' or r['type'] == 'bytes':
                    continue
                a = int(r['addr'], 16)
                if r['type'] == 'u32' and r.get('unit') == 'ms':
                    self.put(a, 'u32', int(t * 1000) & 0xFFFFFFFF)
                elif r['type'] == 'f32':
                    self.put(a, 'f32', 10 + 5 * math.sin(t * (0.3 + 0.07 * (k % 9)) + k))
                elif r['type'] in ('i16', 'i32') and 'fields' not in r:
                    self.put(a, r['type'], int(100 * math.sin(t * 0.5 + k)))

    def touches_login(self, addr, cnt):
        """the request covers some byte of the login register"""
        return (self.login_addr is not None and addr < self.login_addr + self.login_size
                and self.login_addr < addr + cnt)

    def handle(self, slave, fn, addr, cnt, data):
        """one request -> the answer frame (b'' for a WRITE, which gets none,
        for another slave's request and for a broadcast)"""
        if slave == BROADCAST:
            if fn == WRITE:
                self.handle(self.slave, fn, addr, cnt, data)
            return b''
        if slave != self.slave:
            return b''
        if cnt == 0 or addr + cnt > 0x10000 or not all(self.valid[addr:addr + cnt]):
            return frame(self.slave, ERROR_RESP, addr, cnt, bytes([OFFSET_OUT_OF_RANGE]))
        with self.lock:
            if fn == READ:
                return frame(self.slave, READ_RESP, addr, cnt, bytes(self.mem[addr:addr + cnt]))
            if self.touches_login(addr, cnt):
                return self.log_in(fn, addr, cnt, data)
            self.mem[addr:addr + cnt] = data
        return frame(self.slave, WRITE_ACK_RESP, addr, cnt) if fn == WRITE_ACK else b''

    def log_in(self, fn, addr, cnt, data):
        """a write to the login register: kept when it is the whole register,
        accepted only when it holds the device's token (the lock is held)"""
        whole = addr == self.login_addr and cnt == self.login_size
        if whole:
            self.mem[addr:addr + cnt] = data
        if not whole or data != self.token:
            return frame(self.slave, ERROR_RESP, addr, cnt, bytes([PERMISSION_DENIED]))
        return frame(self.slave, WRITE_ACK_RESP, addr, cnt) if fn == WRITE_ACK else b''


def take_requests(buf):
    """the whole request frames at the start of buf, as (slave, fn, addr, cnt, data),
    and the bytes left for later. Bytes before a start byte, an unknown
    function code and a frame with a bad end byte or CRC are dropped."""
    requests = []
    while True:
        i = buf.find(bytes([START]))
        if i < 0:
            return requests, b''
        buf = buf[i:]
        if len(buf) < 7:
            return requests, buf
        fn = buf[2]
        addr, cnt = struct.unpack_from('<HH', buf, 3)
        data_len = cnt if fn in (WRITE, WRITE_ACK) else 0 if fn == READ else -1
        if data_len < 0:
            buf = buf[1:]
            continue
        total = 10 + data_len
        if len(buf) < total:
            return requests, buf
        f, buf = buf[:total], buf[total:]
        if f[-1] == END and struct.unpack('<H', f[-3:-1])[0] == crc16(f[:-3]):
            requests.append((f[1], fn, addr, cnt, f[7:7 + data_len]))


def serve(conn, dev):
    """one client: requests in, answers out, until it disconnects"""
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    buf = b''
    while True:
        chunk = conn.recv(4096)
        if not chunk:
            return
        requests, buf = take_requests(buf + chunk)
        for slave, fn, addr, cnt, data in requests:
            answer = dev.handle(slave, fn, addr, cnt, data)
            if answer:
                conn.sendall(answer)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=1210)
    ap.add_argument('--map', default=os.path.join(here, '..', 'maps', 'example_device.json'))
    ap.add_argument('--token', default=DEFAULT_TOKEN, help='the login token it accepts (default %(default)s)')
    ap.add_argument('--slave', type=int, default=None, help='its slave address (default: the map\'s, else 1)')
    args = ap.parse_args()
    dev = Device(args.map, args.token, args.slave)

    def tick():
        while True:
            dev.animate()
            time.sleep(0.02)
    threading.Thread(target=tick, daemon=True).start()

    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('127.0.0.1', args.port))
    s.listen(4)
    login = ', login at 0x%04X' % dev.login_addr if dev.login_addr is not None else ''
    print('fake EVRe device on 127.0.0.1:%d, slave %d (%d registers%s)' % (args.port, dev.slave, len(dev.regs), login),
          flush=True)
    while True:
        c, _ = s.accept()
        threading.Thread(target=serve, args=(c, dev), daemon=True).start()


if __name__ == '__main__':
    main()

# SPDX-License-Identifier: Apache-2.0
"""A link to an EVRe device and a master on it: one request at a time, each
waiting for its answer (the simple way for scripts)."""
import socket
import time

from . import frame as f


class EvreError(Exception):
    """The device answered ERROR_RESP (`code`), or no answer came (`code` None)."""

    def __init__(self, message, code=None):
        super().__init__(message)
        self.code = code


class TcpLink:
    def __init__(self, host, port, timeout=1.0):
        self.sock = socket.create_connection((host, port), timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.name = '%s:%d' % (host, port)

    def send(self, data):
        self.sock.sendall(data)

    def receive(self, timeout):
        self.sock.settimeout(max(timeout, 0.001))
        try:
            data = self.sock.recv(65536)
        except socket.timeout:
            return b''
        if not data:
            raise EvreError('%s closed the connection' % self.name)
        return data

    def close(self):
        self.sock.close()


class SerialLink:
    """A serial or USB CDC port (needs the pyserial package)."""

    def __init__(self, port, baud=115200, timeout=1.0):
        import serial  # only here: TCP needs no extra package
        self.port = serial.Serial(port, baud, timeout=timeout)
        self.name = '%s@%d' % (port, baud)

    def send(self, data):
        self.port.write(data)

    def receive(self, timeout):
        self.port.timeout = max(timeout, 0.001)
        waiting = max(1, self.port.in_waiting)
        return self.port.read(waiting)

    def close(self):
        self.port.close()


class Master:
    """Requests to one slave over a link, one at a time."""

    def __init__(self, link, slave=1, timeout=1.0):
        self.link, self.slave, self.timeout = link, slave, timeout
        self.parser = f.Parser()

    def _request(self, fn, addr, count, data=b'', expect=None):
        self.link.send(f.build(self.slave, fn, addr, count, data))
        if expect is None:
            return None
        deadline = time.monotonic() + self.timeout
        while True:
            answer = self.parser.next()
            while answer is None:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise EvreError('no answer from %s to 0x%02X at 0x%04X' % (self.link.name, fn, addr))
                self.parser.feed(self.link.receive(left))
                answer = self.parser.next()
            # every answer echoes its request's offset and count (PROTOCOL.md): another one is a late answer to an
            # earlier request, or a frame the device sent by itself (AUTO_SEND's block is not a read of part of it)
            if answer.slave != self.slave or answer.addr != addr or answer.count != count:
                continue
            if answer.fn == f.ERROR_RESP:
                code = answer.data[0] if answer.data else 0
                raise EvreError('0x%04X: %s' % (addr, f.ERRORS.get(code, 'error %d' % code)), code)
            if answer.fn == expect:
                return answer

    def read(self, addr, count):
        """`count` bytes at `addr`"""
        return self._request(f.READ, addr, count, expect=f.READ_RESP).data

    def write(self, addr, data, ack=True):
        """`data` written at `addr`; with ack, waits for the device to confirm it"""
        self._request(f.WRITE_ACK if ack else f.WRITE, addr, len(data), bytes(data),
                      expect=f.WRITE_ACK_RESP if ack else None)

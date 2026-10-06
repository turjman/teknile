# SPDX-License-Identifier: Apache-2.0
"""EVRe frames: building, the CRC, and a parser for a byte stream.

    7B  SLAVE  FN  OFF_L OFF_H  CNT_L CNT_H  DATA...  CRC_L CRC_H  7D

The CRC is CRC-16/X-25 (reflected 0x1021, init 0xFFFF, final XOR 0xFFFF) over
every byte before it, the 7B included. There is no byte stuffing: a parser
finds a frame by its start byte, its size (from FN and CNT), its CRC and its
end byte.
"""
import struct

START, END = 0x7B, 0x7D
READ, READ_RESP = 0xAA, 0xAB
WRITE, WRITE_ACK, WRITE_ACK_RESP = 0xEA, 0xEB, 0xEC
ERROR_RESP = 0xEE
BROADCAST = 0  # the slave address every device hears and none answers: a WRITE only

# The registers the protocol itself defines, the same on every device (as EVRe Studio's src/evre/registers.h).
DEVICE_ID, STATUS, CONFIG = 0xA000, 0xA002, 0xA004
RESERVED_FIRST, RESERVED_WRITABLE, RESERVED_END = 0xA000, 0xA004, 0xA106  # [first, end); DEVICE_ID, STATUS read-only
STATUS_REVISION_MASK = 0x00FF
CAP_ERROR_FRAME, CAP_BROADCAST, CAP_MSG, CAP_AUTO_SEND = 0x0100, 0x0200, 0x0400, 0x0800
CAP_DFU, CAP_STATIC, CAP_BROADCAST_D000 = 0x1000, 0x2000, 0x4000  # CAP_BROADCAST_D000: EVRe 1.1 (draft)
CONFIG_HEARTBEAT, CONFIG_SYS_RESET, CONFIG_MSG_ENABLE, CONFIG_AUTO_SEND, CONFIG_DFU = 0x01, 0x02, 0x04, 0x08, 0x10
CONFIG_PRESCALER_SHIFT = 8  # the AUTO_SEND prescaler: bits 15..8
READ_ONLY_BLOCK = 0xD000  # the device bank starts with its read-only block: what AUTO_SEND sends
AUTO_SEND_BASE_HZ = 8000  # AUTO_SEND's frames a second: AUTO_SEND_BASE_HZ / (prescaler + 1)

ERRORS = {1: 'invalid packet', 2: 'unknown function code', 3: 'permission denied',
          4: 'offset out of range', 5: 'count out of range', 12: 'length mismatch', 13: 'login required'}

HEADER, TRAILER = 7, 3


def crc16(data):
    """CRC-16/X-25 of `data` (bytes): crc16(b'123456789') == 0x906E."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def build(slave, fn, addr, count, data=b''):
    """One frame, as bytes."""
    head = struct.pack('<BBBHH', START, slave, fn, addr, count) + bytes(data)
    return head + struct.pack('<H', crc16(head)) + bytes([END])


def data_length(fn, count):
    """the DATA bytes a frame with this function code has; None if it is none"""
    if fn in (READ_RESP, WRITE, WRITE_ACK):
        return count
    if fn == ERROR_RESP:
        return 1
    if fn in (READ, WRITE_ACK_RESP):
        return 0
    return None


class Frame:
    __slots__ = ('slave', 'fn', 'addr', 'count', 'data')

    def __init__(self, slave, fn, addr, count, data):
        self.slave, self.fn, self.addr, self.count, self.data = slave, fn, addr, count, data

    def __repr__(self):
        return 'Frame(fn=0x%02X, addr=0x%04X, count=%d, data=%s)' % (self.fn, self.addr, self.count, self.data.hex())


class Parser:
    """Feed it bytes as they come; next() gives the frames found, one at a time."""

    def __init__(self):
        self.buffer = bytearray()
        self.bad_frames = 0

    def feed(self, data):
        self.buffer += data

    def next(self):
        """the next whole frame, or None until more bytes come"""
        while True:
            start = self.buffer.find(START)
            if start < 0:
                self.buffer.clear()
                return None
            del self.buffer[:start]
            if len(self.buffer) < HEADER:
                return None
            _, slave, fn, addr, count = struct.unpack_from('<BBBHH', self.buffer)
            length = data_length(fn, count)
            if length is None:
                del self.buffer[:1]  # that 7B was data
                continue
            total = HEADER + length + TRAILER
            if len(self.buffer) < total:
                return None
            crc, end = struct.unpack_from('<HB', self.buffer, total - TRAILER)
            if crc != crc16(self.buffer[:total - TRAILER]) or end != END:
                self.bad_frames += 1
                del self.buffer[:1]
                continue
            frame = Frame(slave, fn, addr, count, bytes(self.buffer[HEADER:HEADER + length]))
            del self.buffer[:total]
            return frame

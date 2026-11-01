#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The USB captures Listen is tested on, made here byte by byte: nothing is recorded from a device.

    python make_captures.py [--out DIR]         default: captures/ beside this script

Every scene is written twice: <scene>.pcap as USBPcap writes it (link type 249, the 27-byte bulk headers, submissions
and completions as the driver logs them), and <scene>.usbmon, the 64-byte mon_bin_hdr and data of each event as
MON_IOCX_GETX gives them (the container is ours, the bytes are what the live source hands its decoder). The frames
come from the Python package and the example maps. There is no clock and no random number here: the same script writes
the same files, byte for byte.

    scene          the device (address 5 on bus 2)              what it holds
    cdc_poll       bulk IN 0x81, OUT 0x01                       a program polls at 10 Hz for 2 s; see poll_scene
    cdc_autosend   bulk IN 0x81, OUT 0x01                       CONFIG written on, AUTO_SEND frames at 100 Hz for 1 s
    cdc_fast       bulk IN 0x81, OUT 0x01                       a stream's enable written, its blocks, 2 of 20 missing
    ftdi_poll      bulk IN 0x81, OUT 0x02, 64-byte packets      cdc_poll behind an FTDI bridge
    composite      bulk IN 0x83, OUT 0x03 (the other: 0x81, 01) two CDC functions on one device
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'python'))

from evre import frame  # noqa: E402
from evre.device_map import DeviceMap  # noqa: E402

BASE_S = 1790000000     # the scenes' time 0, any fixed second
BUS, ADDRESS, OTHER_ADDRESS = 2, 5, 6
INTERRUPT, CONTROL, BULK = 1, 2, 3  # the numbers of both captures
IN = 0x80

IN_BUFFER = 4096        # what a host driver asks of a bulk IN transfer
OUT_DONE_US = 125       # a submitted transfer is done a micro-frame later
REPLY_US = 300          # the device's answer after a request

PCAP_MAGIC, LINK_USBPCAP, SNAP_LENGTH = 0xA1B2C3D4, 249, 65535
URB_CONTROL, URB_BULK_OR_INTERRUPT = 0x0008, 0x0009
INFO_COMPLETION = 0x01
STAGE_SETUP, STAGE_DATA, STAGE_STATUS = 0, 1, 2
EINPROGRESS = -115

FTDI_PACKET = 64
FTDI_STATUS = b'\x01\x60'  # the chip's modem and line status: clear to send, nothing wrong


class Event:
    """One line of a capture: a transfer submitted ('S') or completed ('C')."""

    def __init__(self, time, urb, kind, device, endpoint, transfer, data=b'', length=None, setup=None):
        self.time, self.urb, self.kind = time, urb, kind
        self.device, self.endpoint, self.transfer = device, endpoint, transfer
        self.data, self.setup = bytes(data), setup
        self.length = len(self.data) if length is None else length


class Scene:
    """The transfers of a scene, in any order; finish() lays them on the time line."""

    def __init__(self):
        self.events = []
        self.reads = {}     # (device, endpoint, transfer) -> the transfers it completed, (time, data)
        self.urbs = 0x1000

    def urb(self):
        self.urbs += 1
        return self.urbs

    def bulk_out(self, time, device, endpoint, data):
        urb = self.urb()
        self.events.append(Event(time, urb, 'S', device, endpoint, BULK, data))
        self.events.append(Event(time + OUT_DONE_US, urb, 'C', device, endpoint, BULK, length=len(data)))

    def bulk_in(self, time, device, endpoint, data):
        self.reads.setdefault((device, endpoint, BULK), []).append((time, data))

    def interrupt_in(self, time, device, endpoint, data):
        self.reads.setdefault((device, endpoint, INTERRUPT), []).append((time, data))

    def control(self, time, device, setup, data=b''):
        """a transfer on endpoint 0, its data the way the setup's bit 7 says"""
        urb, from_device, endpoint = self.urb(), setup[0] & IN, setup[0] & IN
        self.events.append(Event(time, urb, 'S', device, endpoint, CONTROL, b'' if from_device else data,
                                 length=len(data), setup=setup))
        self.events.append(Event(time + OUT_DONE_US, urb, 'C', device, endpoint, CONTROL, data if from_device else b'',
                                 length=len(data)))

    def finish(self):
        """every event in time order; an IN transfer waits at the controller from the moment the one before it ended"""
        events = list(self.events)
        for (device, endpoint, transfer), reads in sorted(self.reads.items()):
            begun = 0
            for time, data in sorted(reads, key=lambda read: read[0]):
                urb = self.urb()
                events.append(Event(begun, urb, 'S', device, endpoint, transfer, length=IN_BUFFER))
                events.append(Event(time, urb, 'C', device, endpoint, transfer, data))
                begun = time + 1
        return sorted(events, key=lambda event: event.time)


def stamp(time):
    return BASE_S + time // 1000000, time % 1000000


def pcap(events):
    """the stream USBPcapCMD writes"""

    def packet(event, info, data, stage=None):
        function = URB_CONTROL if event.transfer == CONTROL else URB_BULK_OR_INTERRUPT
        extra = b'' if stage is None else bytes([stage])
        head = struct.pack('<HQIHBHHBBI', 27 + len(extra), event.urb, 0, function, info, BUS, event.device,
                           event.endpoint, event.transfer, len(data)) + extra
        return struct.pack('<IIII', *stamp(event.time), len(head) + len(data), len(head) + len(data)) + head + data

    out = bytearray(struct.pack('<IHHiIII', PCAP_MAGIC, 2, 4, 0, 0, SNAP_LENGTH, LINK_USBPCAP))
    for event in events:
        info = INFO_COMPLETION if event.kind == 'C' else 0
        if event.transfer != CONTROL:
            out += packet(event, info, event.data)
        elif event.kind == 'S':  # the driver logs a control transfer by its stages, in a header a byte longer
            out += packet(event, info, event.setup, STAGE_SETUP)
            if event.data:
                out += packet(event, info, event.data, STAGE_DATA)
        else:
            if event.data:
                out += packet(event, info, event.data, STAGE_DATA)
            out += packet(event, info, b'', STAGE_STATUS)
    return bytes(out)


def usbmon(events):
    """the records of /dev/usbmonN"""
    out = bytearray()
    for event in events:
        seconds, microseconds = stamp(event.time)
        from_device = event.endpoint & IN
        if event.kind == 'S' and from_device:
            tag = b'<'  # the kernel's word for "no data yet"
        elif event.kind == 'C' and not from_device:
            tag = b'>'
        else:
            tag = b'\0'
        out += struct.pack('<QccBBHcc', event.urb, event.kind.encode(), bytes([event.transfer]), event.endpoint,
                           event.device, BUS, b'\0' if event.setup else b'-', tag)
        out += struct.pack('<qiiII', seconds, microseconds, EINPROGRESS if event.kind == 'S' else 0, event.length,
                           len(event.data))
        out += (event.setup or bytes(8)) + struct.pack('<iiII', 8 if event.transfer == INTERRUPT else 0, 0, 0, 0)
        out += event.data
    return bytes(out)


class Device:
    """A map's registers as a device holds them: its answers are read from here, each value in its map's type."""

    def __init__(self, map_name, slave=None):
        self.map = DeviceMap.load(os.path.join(HERE, '..', 'maps', map_name))
        self.slave = self.map.slave if slave is None else slave
        self.values = {r.name: r.default or 0 for r in self.map if r.is_number}
        self.values['DEVICE_ID'] = self.map.device_id
        self.values['STATUS'] = 0x3F01  # as tests/fake_device.py: protocol revision 1, its capability bits

    def span(self, first, last):
        """the addresses from one register to the end of another"""
        first, last = self.map[first], self.map[last]
        return first.addr, last.addr + last.size - first.addr

    def bytes_of(self, addr, count):
        data = bytearray(count)
        for register in self.map:
            if register.is_number and addr <= register.addr and register.addr + register.size <= addr + count:
                value = register.encode(self.values[register.name])
                data[register.addr - addr:register.addr - addr + register.size] = value
        return bytes(data)

    def frame(self, fn, addr, count, data=b''):
        return frame.build(self.slave, fn, addr, count, data)

    def read(self, addr, count):
        return self.frame(frame.READ, addr, count)

    def answer(self, addr, count):
        return self.frame(frame.READ_RESP, addr, count, self.bytes_of(addr, count))


class CdcLink:
    """A board's own USB: its bytes go on the bulk endpoints as they are."""

    def __init__(self, scene, device=ADDRESS, in_endpoint=0x81, out_endpoint=0x01, notify_endpoint=0x82):
        self.scene, self.device = scene, device
        self.in_endpoint, self.out_endpoint, self.notify_endpoint = in_endpoint, out_endpoint, notify_endpoint

    def to_device(self, time, data):
        self.scene.bulk_out(time, self.device, self.out_endpoint, data)

    def from_device(self, time, data):
        self.scene.bulk_in(time, self.device, self.in_endpoint, data)

    def exchange(self, time, request, answer):
        self.to_device(time, request)
        self.from_device(time + REPLY_US, answer)

    def line_setup(self, time):
        """what a program's open of the port does: SET_LINE_CODING (115200 8N1) and SET_CONTROL_LINE_STATE"""
        self.scene.control(time, self.device, bytes.fromhex('2120000000000700'), bytes.fromhex('00C20100000008'))
        self.scene.control(time + 500, self.device, bytes.fromhex('2122030000000000'))

    def idle(self, time):
        """a SERIAL_STATE notification on the interrupt endpoint"""
        self.scene.interrupt_in(time, self.device, self.notify_endpoint, bytes.fromhex('A1200000000002000300'))


class FtdiLink(CdcLink):
    """An FTDI bridge: 2 status bytes at the start of every IN packet, and the chip answers every read."""

    def from_device(self, time, data):
        room = FTDI_PACKET - len(FTDI_STATUS)
        packets = (data[at:at + room] for at in range(0, len(data), room))
        super().from_device(time, b''.join(FTDI_STATUS + packet for packet in packets))

    def line_setup(self, time):
        """the device descriptor (VID 0403, PID 6001) and the vendor's request for the baud rate, 115200"""
        descriptor = bytes.fromhex('1201000200000040') + struct.pack('<HHH', 0x0403, 0x6001, 0x0600) + bytes(4)
        self.scene.control(time, self.device, bytes.fromhex('8006000100001200'), descriptor)
        self.scene.control(time + 500, self.device, bytes.fromhex('40031A0000000000'))

    def idle(self, time):
        """the chip's status poll: nothing to say but its status"""
        self.scene.bulk_in(time, self.device, self.in_endpoint, FTDI_STATUS)


def poll_scene(scene, link, other_link):
    """A program polls the example device: DEVICE_ID, a long read, then both blocks at 10 Hz for 2 s, SUPPLY_V a ramp.
    Among the 20 rounds, one each of: a WRITE_ACK of FAN_SPEED and its answer, a WRITE, a read refused (ERROR_RESP), a
    late answer, an answer nobody asked for, 12 bytes of text, an answer split across two transfers, two answers in one
    transfer. Around them: the port's setup on endpoint 0, the idle traffic, a second device's bulk traffic."""
    device = Device('example_device.json')
    other = Device('example_device.json', slave=2)
    rows = {'ro': device.span('UPTIME', 'PRESSURE'), 'rw': device.span('SETPOINT', 'MOTOR_SPEED')}

    link.line_setup(1000)
    identity = device.span('DEVICE_ID', 'STATUS')
    link.exchange(5000, device.read(*identity), device.answer(*identity))
    message = device.map['MSG_BUFFER'].addr, 100  # longer than a full-speed packet: FTDI's status bytes fall inside it
    link.exchange(10000, device.read(*message), device.frame(frame.READ_RESP, *message, bytes(range(100))))

    for i in range(20):
        time = 20000 + i * 100000
        device.values.update(UPTIME=time // 1000, SUPPLY_V=12.0 + 0.1 * i, SUPPLY_I=0.5, TEMPERATURE=25.0 + 0.05 * i,
                             STATE=0b0101, COUNTER=i, PRESSURE=1.5)
        ro, rw = device.answer(*rows['ro']), device.answer(*rows['rw'])
        if i == 15:  # one answer split across two transfers
            link.to_device(time, device.read(*rows['ro']))
            link.from_device(time + REPLY_US, ro[:12])
            link.from_device(time + REPLY_US + 500, ro[12:])
            link.exchange(time + 1000, device.read(*rows['rw']), rw)
        elif i == 17:  # both requests before either answer, both answers in one transfer
            link.to_device(time, device.read(*rows['ro']))
            link.to_device(time + 150, device.read(*rows['rw']))
            link.from_device(time + 1300, ro + rw)
        else:
            link.exchange(time, device.read(*rows['ro']), ro)
            link.exchange(time + 1000, device.read(*rows['rw']), rw)

        late = time + 10000
        if i == 3:
            fan = device.map['FAN_SPEED']
            device.values['FAN_SPEED'] = 45
            link.exchange(late, device.frame(frame.WRITE_ACK, fan.addr, fan.size, fan.encode(45)),
                          device.frame(frame.WRITE_ACK_RESP, fan.addr, fan.size))
        elif i == 5:
            led = device.map['LED_MODE']
            device.values['LED_MODE'] = 2
            link.to_device(late, device.frame(frame.WRITE, led.addr, led.size, led.encode(2)))
        elif i == 7:
            msg_cnt = device.map['MSG_CNT']
            link.exchange(late, device.read(msg_cnt.addr, msg_cnt.size),
                          device.frame(frame.ERROR_RESP, msg_cnt.addr, msg_cnt.size, bytes([3])))
        elif i == 9:  # past any timeout: the program has given up on it
            status = device.map['STATUS']
            link.to_device(late, device.read(status.addr, status.size))
            link.from_device(late + 700000, device.answer(status.addr, status.size))
        elif i == 11:
            state = device.map['STATE']
            link.from_device(late, device.answer(state.addr, state.size))
        elif i == 13:
            link.from_device(late, b'hello, world')

        link.idle(time + 40000)
        link.idle(time + 80000)
        other_link.exchange(time + 20000, other.read(*rows['rw']), other.answer(*rows['rw']))
    return scene


def cdc_poll():
    scene = Scene()
    return poll_scene(scene, CdcLink(scene), CdcLink(scene, OTHER_ADDRESS))


def ftdi_poll():
    scene = Scene()
    return poll_scene(scene, FtdiLink(scene, out_endpoint=0x02), CdcLink(scene, OTHER_ADDRESS, out_endpoint=0x02))


def cdc_autosend():
    """CONFIG's AUTO_SEND bit on at 100 frames a second, then what the device sends by itself: its read-only block."""
    scene, device = Scene(), Device('example_device.json')
    link = CdcLink(scene)
    link.line_setup(1000)
    config = device.map['CONFIG']
    prescaler = frame.AUTO_SEND_BASE_HZ // 100 - 1
    value = frame.CONFIG_AUTO_SEND | prescaler << frame.CONFIG_PRESCALER_SHIFT
    link.exchange(5000, device.frame(frame.WRITE_ACK, config.addr, config.size, config.encode(value)),
                  device.frame(frame.WRITE_ACK_RESP, config.addr, config.size))
    block = device.span('UPTIME', 'PRESSURE')
    for i in range(100):
        time = 10000 + i * 10000
        device.values.update(UPTIME=time // 1000, SUPPLY_V=12.0, COUNTER=i, PRESSURE=1.5)
        link.from_device(time, device.answer(*block))
    return scene


def cdc_fast():
    """A stream's enable written, then 20 blocks in PROTOCOL.md's layout. Two of them never reach the capture: their
    numbers skip, and that is all that says so (the device lost nothing, so no flag)."""
    scene, device = Scene(), Device('example_fast.json')
    link = CdcLink(scene)
    link.line_setup(1000)
    enable = device.map['ADC_STREAM']
    link.exchange(5000, device.frame(frame.WRITE_ACK, enable.addr, enable.size, enable.encode(1)),
                  device.frame(frame.WRITE_ACK_RESP, enable.addr, enable.size))
    stream = device.map.doc['streams'][0]
    addr = int(stream['addr'], 16)
    records = 50  # 5 ms at the map's 10000 records a second; short, to keep the file small
    for block in range(20):
        first = block * records
        if block in (7, 13):
            continue
        flags = 0x01 if block == 0 else 0  # START
        data = struct.pack('<IHBB', first, records, flags, 0)
        for record in range(first, first + records):
            data += struct.pack('<hh', record % 1000, 12000 + record % 100)
        link.from_device(10000 + block * 5000, device.frame(frame.READ_RESP, addr, len(data), data))
    return scene


def composite():
    """Two CDC functions on one device: both talk, each on its own endpoints, and only ours is to be heard."""
    scene, device = Scene(), Device('example_device.json')
    other = Device('example_device.json', slave=2)
    ours = CdcLink(scene, in_endpoint=0x83, out_endpoint=0x03, notify_endpoint=0x84)
    theirs = CdcLink(scene)
    ours.line_setup(1000)
    block = device.span('SETPOINT', 'MOTOR_SPEED')
    for i in range(10):
        time = 5000 + i * 10000
        device.values['SETPOINT'] = other.values['SETPOINT'] = 20.0 + i
        ours.exchange(time, device.read(*block), device.answer(*block))
        theirs.exchange(time + 2000, other.read(*block), other.answer(*block))
        ours.idle(time + 4000)
        theirs.idle(time + 6000)
    return scene


SCENES = {'cdc_poll': cdc_poll, 'cdc_autosend': cdc_autosend, 'cdc_fast': cdc_fast, 'ftdi_poll': ftdi_poll,
          'composite': composite}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', default=os.path.join(HERE, 'captures'))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for name, make in SCENES.items():
        events = make().finish()
        for suffix, write in (('pcap', pcap), ('usbmon', usbmon)):
            data = write(events)
            with open(os.path.join(args.out, '%s.%s' % (name, suffix)), 'wb') as f:
                f.write(data)
            print('%s.%s: %d events, %d bytes' % (name, suffix, len(events), len(data)))


if __name__ == '__main__':
    main()

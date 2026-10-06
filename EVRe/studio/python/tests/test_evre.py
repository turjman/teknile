# SPDX-License-Identifier: Apache-2.0
"""The evre package (unittest): frames, the map, and a device - evre-sim serving the example map.

    python -m unittest discover -s python/tests          (from EVRe Studio's folder)
    EVRE_BUILD=<build folder>: where evre-sim is; without it the device tests are skipped
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True  # no __pycache__ beside the sources
sys.path.insert(0, os.path.join(HERE, '..'))
import evre  # noqa: E402
from evre import frame  # noqa: E402

MAP = os.path.join(HERE, '..', '..', 'maps', 'example_device.json')
PORT = 1214


class Frames(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(evre.crc16(b'123456789'), 0x906E)

    def test_build_and_parse(self):
        request = evre.build(1, frame.READ, 0xD000, 16)
        self.assertEqual(request[:7], bytes([0x7B, 0x01, 0xAA, 0x00, 0xD0, 0x10, 0x00]))
        self.assertEqual(request[-1], 0x7D)
        answer = evre.build(1, frame.READ_RESP, 0xD000, 4, b'\x7b\x7d\x01\x02')
        p = evre.Parser()
        p.feed(b'\x00\x7b junk' + answer[:5])  # garbage, then half a frame
        self.assertIsNone(p.next())
        p.feed(answer[5:])
        got = p.next()
        self.assertEqual((got.fn, got.addr, got.data), (frame.READ_RESP, 0xD000, b'\x7b\x7d\x01\x02'))
        self.assertIsNone(p.next())

    def test_bad_crc_skipped(self):
        good = evre.build(1, frame.WRITE_ACK_RESP, 0x10, 2)
        bad = bytearray(good)
        bad[-2] ^= 0xFF
        p = evre.Parser()
        p.feed(bytes(bad) + good)
        self.assertEqual(p.next().fn, frame.WRITE_ACK_RESP)
        self.assertEqual(p.bad_frames, 1)


class Maps(unittest.TestCase):
    def setUp(self):
        self.map = evre.DeviceMap.load(MAP)

    def test_registers(self):
        with open(MAP, encoding='utf-8') as f:
            self.assertEqual(len(self.map), len(json.load(f)['registers']))
        self.assertEqual(self.map.device_id, 0x1001)
        self.assertEqual(self.map['supply_v'].type, 'f32')  # any case
        self.assertIs(self.map[0xD004], self.map['SUPPLY_V'])

    def test_values(self):
        pressure = self.map['PRESSURE']  # i16, scale 0.01
        self.assertEqual(pressure.encode(1.5), (150).to_bytes(2, 'little'))
        self.assertAlmostEqual(pressure.decode((150).to_bytes(2, 'little')), 1.5)
        led = self.map['LED_MODE']
        self.assertEqual(led.encode('blink'), b'\x02')
        self.assertEqual(led.decoded(b'\x02'), 'blink')
        state = self.map['STATE']
        self.assertEqual(state.decoded((0b0101).to_bytes(2, 'little')), {'MODE': 'run', 'READY': 1, 'ALARM': 0})
        fan = self.map['FAN_SPEED']
        self.assertIn('maximum', fan.limit_problem(150))
        self.assertIsNone(fan.limit_problem(50))
        self.assertEqual(fan.default, 30)
        with self.assertRaises(ValueError):
            fan.encode(300)

    def test_guard_rules(self):
        """what a device with EVRe Guard refuses is not sent: NaN and the infinities, an f32 past the largest float;
        a register that clamps (past_limits) takes a value past its limits; code 15 has its name"""
        setpoint = self.map['SETPOINT']  # f32, -20 .. 120
        for bad in (float('nan'), float('inf'), -float('inf'), 1e39):
            with self.assertRaises(ValueError):
                setpoint.encode(bad)
        self.assertEqual(len(setpoint.encode(3.4e38)), 4)
        self.assertIn('maximum', setpoint.write_limit_problem(150))
        clamped = evre.Register({'addr': '0xD000', 'name': 'S', 'type': 'i16', 'access': 'rw', 'min': -100,
                                            'max': 100, 'past_limits': 'clamp'})
        self.assertTrue(clamped.clamps and not setpoint.clamps)
        self.assertIsNone(clamped.write_limit_problem(150))
        self.assertIn('maximum', clamped.limit_problem(150))
        self.assertEqual(frame.ERRORS[15], 'value refused')

    def test_overlay(self):
        with tempfile.TemporaryDirectory() as tmp:
            # the base beside the overlay, extended by a relative path (no relative path joins two drives)
            shutil.copy(MAP, tmp)
            overlay = os.path.join(tmp, 'overlay.json')
            with open(overlay, 'w', encoding='utf-8') as f:
                json.dump({'extends': os.path.basename(MAP), 'device': 'Variant', 'registers': [
                    {'addr': '0xD084', 'max': 80, 'unit': None}, {'addr': '0xD012', 'remove': True},
                    {'addr': '0xD200', 'name': 'NEW', 'type': 'u8'}]}, f)
            m = evre.DeviceMap.load(overlay)
            self.assertEqual(m.device, 'Variant')
            self.assertEqual(m['FAN_SPEED'].max, 80)
            self.assertEqual(m['FAN_SPEED'].unit, '')
            self.assertRaises(KeyError, lambda: m['COUNTER'])
            self.assertEqual(m['NEW'].addr, 0xD200)


class _NoLink:
    """a link that is never used: a bus file read without a device"""
    name = 'none'

    def send(self, data):
        raise AssertionError('nothing should be sent')

    def close(self):
        pass


class BusFiles(unittest.TestCase):
    """a bus file as the Bus reads it, without a device"""

    def bus(self, devices):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'bus.json')
            with open(path, 'w', encoding='utf-8') as fh:
                json.dump({'format': 'evre-bus/1', 'devices': devices}, fh)
            return evre.Bus(_NoLink(), path)

    def test_slave_defaults_to_1(self):
        bus = self.bus([{'name': 'D1', 'map': os.path.abspath(MAP)}])  # as EVRe Studio reads it
        self.assertEqual(bus['D1'].master.slave, 1)
        with self.assertRaises(evre.EvreError):  # two devices at 1
            self.bus([{'name': 'D1', 'map': os.path.abspath(MAP)}, {'name': 'D2', 'map': os.path.abspath(MAP)}])
        with self.assertRaises(evre.EvreError):  # the broadcast address
            self.bus([{'name': 'D1', 'slave': 0, 'map': os.path.abspath(MAP)}])
        with self.assertRaises(evre.EvreError):  # no map
            self.bus([{'name': 'D1', 'slave': 1}])

    def test_broadcast_part_of_a_number(self):
        """a device with EVRe Guard refuses part of a number: a broadcast of one is never sent"""
        bus = self.bus([{'name': 'D1', 'slave': 1, 'map': os.path.abspath(MAP)}])
        self.assertIsNone(bus.broadcast_refusal(0xD080, 4))   # SETPOINT, f32, whole
        self.assertIn('only part of SETPOINT', bus.broadcast_refusal(0xD080, 2))
        self.assertIn('only part of SETPOINT', bus.broadcast_refusal(0xD082, 3))

    def test_empty_bus(self):
        bus = self.bus([])
        self.assertIsInstance(bus.broadcast_refusal(0xD084, 1), str)
        with self.assertRaises(evre.EvreError):
            bus.broadcast('FAN_SPEED', 0)

    def test_constants(self):
        self.assertEqual((frame.BROADCAST, frame.DEVICE_ID, frame.CONFIG), (0, 0xA000, 0xA004))
        self.assertEqual((frame.CAP_AUTO_SEND, frame.CONFIG_AUTO_SEND, frame.AUTO_SEND_BASE_HZ), (0x0800, 0x0008, 8000))


class _CannedLink:
    """a link whose device says only what it is given: the bytes a master receives, piece by piece"""
    name = 'canned'

    def __init__(self, *pieces):
        self.pieces = list(pieces)

    def send(self, data):
        pass

    def receive(self, timeout):
        return self.pieces.pop(0) if self.pieces else b''

    def close(self):
        pass


class Answers(unittest.TestCase):
    """an answer is matched by its slave, offset and count (PROTOCOL.md): a frame the device sends by itself is none"""

    def test_auto_send_frame_is_not_the_answer(self):
        block = evre.build(1, frame.READ_RESP, 0xD000, 16, bytes(range(16)))  # AUTO_SEND: the read-only block, unasked
        answer = evre.build(1, frame.READ_RESP, 0xD000, 4, b'\xAA\xBB\xCC\xDD')
        master = evre.Master(_CannedLink(block + answer), slave=1, timeout=0.5)
        self.assertEqual(master.read(0xD000, 4), b'\xAA\xBB\xCC\xDD')
        with self.assertRaises(evre.EvreError) as none:  # the frame alone answers nothing
            evre.Master(_CannedLink(block), slave=1, timeout=0.05).read(0xD000, 4)
        self.assertIsNone(none.exception.code)

    def test_another_requests_error_is_skipped(self):
        late = evre.build(1, frame.ERROR_RESP, 0xD084, 2, b'\x05')  # the refusal of an earlier, longer write
        ack = evre.build(1, frame.WRITE_ACK_RESP, 0xD084, 1)
        evre.Master(_CannedLink(late + ack), slave=1, timeout=0.5).write(0xD084, b'\x01')  # not this write's: no error
        own = evre.build(1, frame.ERROR_RESP, 0xD084, 1, b'\x03')
        with self.assertRaises(evre.EvreError) as refused:  # its own refusal still raises, with the code
            evre.Master(_CannedLink(own), slave=1, timeout=0.5).write(0xD084, b'\x01')
        self.assertEqual(refused.exception.code, 3)


BUILD = os.environ.get('EVRE_BUILD')
SIM = os.path.join(BUILD or '', 'evre-sim.exe' if os.name == 'nt' else 'evre-sim')


@unittest.skipUnless(BUILD and os.path.exists(SIM), 'EVRE_BUILD does not name a build folder with evre-sim')
class DeviceOverTcp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sim = subprocess.Popen([SIM, MAP, '--port', str(PORT)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', PORT), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.sim.kill()
        cls.sim.wait()

    def setUp(self):
        self.dev = evre.connect_tcp('127.0.0.1', PORT, MAP, token='example-token')

    def tearDown(self):
        self.dev.close()

    def test_read(self):
        self.assertEqual(self.dev.device_id(), 0x1001)
        self.assertIsInstance(self.dev['SUPPLY_V'], float)
        both = self.dev.read('SUPPLY_V', 'SUPPLY_I')
        self.assertEqual(set(both), {'SUPPLY_V', 'SUPPLY_I'})
        everything = self.dev.read_all()
        self.assertIn('SETPOINT', everything)
        self.assertEqual(everything['FAN_SPEED'], 30)  # its default, as the simulator starts it

    def test_write(self):
        self.assertEqual(self.dev.write('FAN_SPEED', 40), 40)
        self.dev['LED_MODE'] = 'blink'
        self.assertEqual(self.dev.decoded('LED_MODE'), 'blink')
        with self.assertRaises(evre.EvreError):
            self.dev.write('FAN_SPEED', 150)  # past the map's max
        self.assertEqual(self.dev.write('FAN_SPEED', 150, force=True), 150)
        with self.assertRaises(evre.EvreError):
            self.dev.write('MOTOR_SPEED', 10)  # danger
        with self.assertRaises(evre.EvreError):
            self.dev.write('SUPPLY_V', 1)  # read-only
        self.dev.write('FAN_SPEED', 0)

    def test_device_errors(self):
        with self.assertRaises(evre.EvreError) as caught:
            self.dev.read_raw(0x0100, 2)  # in no register
        self.assertEqual(caught.exception.code, 4)


FAST = os.path.join(BUILD or '', 'evre_fake_fast.exe' if os.name == 'nt' else 'evre_fake_fast')
BUS_PORT = 1215


@unittest.skipUnless(BUILD and os.path.exists(FAST), 'EVRE_BUILD does not name a build folder with evre_fake_fast')
class BusOverTcp(unittest.TestCase):
    """two devices of the example map on one link (evre_fake_fast --node), as a bus file names them"""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.bus_file = os.path.join(cls.tmp.name, 'bus.json')
        with open(cls.bus_file, 'w', encoding='utf-8') as fh:
            json.dump({'format': 'evre-bus/1', 'devices': [{'name': 'D1', 'slave': 1, 'map': os.path.abspath(MAP)},
                                                           {'name': 'D2', 'slave': 2, 'map': os.path.abspath(MAP)}]}, fh)
        cls.fake = subprocess.Popen([FAST, str(BUS_PORT), MAP, 'example-token', '--slave', '1', '--node', '2=' + MAP],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', BUS_PORT), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.fake.kill()
        cls.fake.wait()
        cls.tmp.cleanup()

    def setUp(self):
        self.bus = evre.connect_bus_tcp('127.0.0.1', BUS_PORT, self.bus_file, token='example-token')

    def tearDown(self):
        self.bus.close()

    def test_names(self):
        self.bus['D1_FAN_SPEED'] = 11
        self.bus['D2']['FAN_SPEED'] = 22
        self.assertEqual(self.bus.read('D1_FAN_SPEED', 'D2_FAN_SPEED'), {'D1_FAN_SPEED': 11, 'D2_FAN_SPEED': 22})
        self.assertEqual(self.bus['D2'].device_id(), 0x1001)
        with self.assertRaises(evre.EvreError):
            self.bus['D7_FAN_SPEED']

    def test_broadcast(self):
        self.assertEqual(self.bus.broadcast('FAN_SPEED', 33), {'D1_FAN_SPEED': 33, 'D2_FAN_SPEED': 33})
        self.assertEqual(self.bus.broadcast('D2_FAN_SPEED', 0), {'D1_FAN_SPEED': 0, 'D2_FAN_SPEED': 0})
        self.assertIsNone(self.bus.broadcast_refusal(0xA004, 2))      # CONFIG: every device has it
        self.assertIsNotNone(self.bus.broadcast_refusal(0xA000, 2))   # DEVICE_ID: read-only
        with self.assertRaises(evre.EvreError):
            self.bus.broadcast('UPTIME', 5)                           # read-only in the map
        with self.assertRaisesRegex(evre.EvreError, 'AUTO_SEND'):
            self.bus.broadcast('CONFIG', 0x4F08, force=True)          # every device sending by itself at once
        self.assertEqual(self.bus.broadcast('CONFIG', 0x4F00, force=True), {'D1_CONFIG': 0x4F01, 'D2_CONFIG': 0x4F01})
        self.bus.broadcast('CONFIG', 0, force=True)

    def test_tokens(self):
        """tokens={name: token} wins over token= for those devices; a refused token stops the connect"""
        with evre.connect_bus_tcp('127.0.0.1', BUS_PORT, self.bus_file, token='wrong-token',
                                  tokens={'D1': 'example-token', 'D2': 'example-token'}) as bus:
            self.assertEqual(bus['D2'].device_id(), 0x1001)
        with self.assertRaises(evre.EvreError):
            evre.connect_bus_tcp('127.0.0.1', BUS_PORT, self.bus_file, token='example-token', tokens={'D2': 'wrong-token'})


LOGIN_PORT = 1216


@unittest.skipUnless(BUILD and os.path.exists(FAST), 'EVRE_BUILD does not name a build folder with evre_fake_fast')
class LoginRequired(unittest.TestCase):
    """a device that requires a login (evre_fake_fast --login-required): code 13, named "login required" """

    @classmethod
    def setUpClass(cls):
        cls.fake = subprocess.Popen([FAST, str(LOGIN_PORT), MAP, 'example-token', '--login-required'],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                socket.create_connection(('127.0.0.1', LOGIN_PORT), 0.2).close()
                break
            except OSError:
                time.sleep(0.1)

    @classmethod
    def tearDownClass(cls):
        cls.fake.kill()
        cls.fake.wait()

    def test_name(self):
        self.assertEqual(frame.ERRORS[13], 'login required')

    def test_without_and_with_a_session(self):
        with evre.connect_tcp('127.0.0.1', LOGIN_PORT, MAP) as dev:
            with self.assertRaisesRegex(evre.EvreError, 'login required') as caught:
                dev.read_raw(0xA000, 2)
            self.assertEqual(caught.exception.code, 13)
        with evre.connect_tcp('127.0.0.1', LOGIN_PORT, MAP, token='example-token') as dev:
            self.assertEqual(dev.device_id(), 0x1001)


if __name__ == '__main__':
    unittest.main()

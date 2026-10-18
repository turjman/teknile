# SPDX-License-Identifier: Apache-2.0
"""Several devices on one link: a bus file ("evre-bus/1", as EVRe Studio writes it), each device at its own slave
address with its own map, all sharing the link.

    bus = evre.connect_bus_tcp('127.0.0.1', 1231, 'maps/example_bus.json', token='example-token')
    bus['D1']['SUPPLY_V']            # a device by its name, then a register as on a Device
    bus['D2_FAN_SPEED'] = 40         # a register by its name on the bus: the device's name, _, the register's
    bus.broadcast('FAN_SPEED', 0)    # every device at once, then each read back: {'D1_FAN_SPEED': 0, ...}

A broadcast (slave 0) reaches every device and none answers, so it must mean the same to all of them: into the
reserved bank's writable registers (0xA004 .. 0xA105: CONFIG, the messages) always; anywhere else only when every
device has the same map. The rule is EVRe Studio's (docs/STUDIO.md 3.10). A broadcast names its register by the
map's own name (FAN_SPEED) or by a device's (D1_FAN_SPEED), as EVRe Studio's API and command line do.
"""
import json
import os

from . import frame as f
from .device import Device
from .device_map import DeviceMap
from .link import EvreError, SerialLink, TcpLink


def _map_identity(device_map):
    """what makes two maps the same for a broadcast: the device ID and every register as written"""
    return json.dumps([device_map.device_id, device_map.doc.get('registers', [])], sort_keys=True)


class Bus:
    """The devices of a bus file on one link; see the top of this file."""

    def __init__(self, link, bus_file, token=None, timeout=None, tokens=None):
        """bus_file: its path. token: for every device whose map has a login register; tokens: {name: token} for the
        devices whose token is another"""
        with open(bus_file, encoding='utf-8') as fh:
            doc = json.load(fh)
        if not str(doc.get('format', '')).startswith('evre-bus/'):
            raise EvreError('%s: not a bus file ("format": "evre-bus/1")' % bus_file)
        folder = os.path.dirname(os.path.abspath(bus_file))
        self.link, self.name, self.devices = link, doc.get('name', ''), {}
        tokens = tokens or {}
        slaves = set()
        for entry in doc.get('devices', []):
            # a device without "slave" is at 1, as EVRe Studio reads the file
            name, slave, map_file = entry.get('name', ''), entry.get('slave', 1), entry.get('map', '')
            if not isinstance(slave, int) or not 1 <= slave <= 255 or slave in slaves or not name or name in self.devices:
                raise EvreError('%s: device %r at slave %r: names and slaves (1 to 255) must be given and unique'
                                % (bus_file, name, slave))
            if not map_file:
                raise EvreError('%s: device %s: no map' % (bus_file, name))
            slaves.add(slave)
            path = map_file if os.path.isabs(map_file) else os.path.join(folder, map_file)
            self.devices[name] = Device(link, DeviceMap.load(path), slave, timeout, tokens.get(name, token))

    def close(self):
        self.link.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ---- by name

    def _split(self, name):
        """'D2_FAN_SPEED' -> (the device D2, 'FAN_SPEED')"""
        for device in sorted(self.devices, key=len, reverse=True):  # the longest device name first
            prefix = device + '_'
            if name.lower().startswith(prefix.lower()):
                return self.devices[device], name[len(prefix):]
        raise EvreError('no device for %s: a register on the bus is named <device>_<register>' % name)

    def __getitem__(self, key):
        """a device by its name (bus['D1']), or a register's value by its name on the bus (bus['D1_SUPPLY_V'])"""
        if key in self.devices:
            return self.devices[key]
        device, reg = self._split(key)
        return device.read(reg)

    def __setitem__(self, key, value):
        device, reg = self._split(key)
        device.write(reg, value)

    def read(self, *names):
        """values by their names on the bus: one value, or a dict for several"""
        values = {name: self[name] for name in names}
        return values[names[0]] if len(names) == 1 else values

    def write(self, name, value, force=False):
        device, reg = self._split(name)
        return device.write(reg, value, force)

    # ---- every device at once

    def broadcast_refusal(self, addr, count):
        """why a broadcast WRITE of count bytes at addr must not go out; None: it may (the rule above)"""
        if count < 1:
            return 'nothing to write'
        end = addr + count
        if f.RESERVED_FIRST <= addr and end <= f.RESERVED_END:
            return 'DEVICE_ID and STATUS are read-only on every device' if addr < f.RESERVED_WRITABLE else None
        maps = [d.map for d in self.devices.values()]
        if not maps:
            return 'no device on the bus'
        if len({_map_identity(m) for m in maps}) > 1:
            return 'the devices have different register maps: outside the reserved bank it would mean different things'
        for at in range(addr, end):
            if not any(r.writable and r.addr <= at < r.addr + r.size for r in maps[0]):
                return '0x%04X .. 0x%04X is not all writable registers of the map' % (addr, end - 1)
        for r in maps[0]:  # a number is written whole: a device with EVRe Guard refuses part of one (3)
            if r.is_number and r.addr < end and addr < r.addr + r.size and (r.addr < addr or r.addr + r.size > end):
                return '0x%04X .. 0x%04X writes only part of %s: a device with EVRe Guard refuses it' % (addr, end - 1, r.name)
        return None

    def _broadcast_register(self, name):
        """the register a broadcast names: by a device's name for it (D1_FAN_SPEED), else by the map's own name
        (FAN_SPEED) on the first device that has it - EVRe Studio's order"""
        try:
            device, reg = self._split(name)
            return device.map[reg]
        except (EvreError, KeyError):
            pass
        for device in self.devices.values():
            try:
                return device.map[name]
            except KeyError:
                continue
        raise EvreError('no register %s on the bus' % name)

    def broadcast(self, name, value, force=False):
        """value to every device in one frame (slave 0, nobody answers), then each device's register read back:
        {bus name: value}. name: a register of the map (FAN_SPEED), or any device's (D1_FAN_SPEED)."""
        reg = self._broadcast_register(name)
        if not reg.writable:
            raise EvreError('%s is read-only' % reg.name)
        data = reg.encode(value)
        refusal = self.broadcast_refusal(reg.addr, len(data))
        at = f.CONFIG - reg.addr
        if not refusal and 0 <= at < len(data) and data[at] & f.CONFIG_AUTO_SEND:
            refusal = 'a broadcast must not switch AUTO_SEND on: every device would send by itself at once'
        if refusal:
            raise EvreError('no broadcast: %s' % refusal)
        if reg.is_number and not force:
            problem = reg.write_problem(data)
            if problem:
                raise EvreError('%s = %s is %s (the map\'s limit; force=True sends it)' % (reg.name, value, problem))
        if reg.danger and not force:
            raise EvreError('%s is marked danger: force=True to broadcast it' % reg.name)
        self.link.send(f.build(f.BROADCAST, f.WRITE, reg.addr, len(data), data))
        back = {}
        for device_name, device in self.devices.items():
            for r in device.map:
                if r.addr == reg.addr and r.readable:
                    back['%s_%s' % (device_name, r.name)] = r.decode(device.master.read(r.addr, r.size))
        return back


def _bus_on(link, bus_file, token, timeout, tokens):
    """a Bus on a link opened for it: the link is closed again when the bus cannot be made (a refused token)"""
    try:
        return Bus(link, bus_file, token, timeout, tokens)
    except BaseException:
        link.close()
        raise


def connect_bus_tcp(host, port, bus_file, token=None, timeout=1.0, tokens=None):
    """a Bus over TCP (a gateway, or a device server with several devices behind it)"""
    return _bus_on(TcpLink(host, port, timeout), bus_file, token, timeout, tokens)


def connect_bus_serial(port, bus_file, baud=115200, token=None, timeout=1.0, tokens=None):
    """a Bus over a serial port (an RS-485 line; pyserial)"""
    return _bus_on(SerialLink(port, baud, timeout), bus_file, token, timeout, tokens)

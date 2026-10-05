# SPDX-License-Identifier: Apache-2.0
"""A device: a link, a master and a map; registers read and written by name."""
from . import frame as f
from .device_map import DeviceMap
from .link import EvreError, Master, SerialLink, TcpLink

MAX_BLOCK_GAP = 8  # as EVRe Studio merges reads: the same 256-byte bank, gaps up to 8 bytes


class Device:
    """
        dev = evre.connect_tcp('127.0.0.1', 1210, 'maps/example_device.json', token='...')
        dev['SUPPLY_V']            # 12.03
        dev['LED_MODE'] = 'blink'  # a name, a number, a shown value
        dev.read_all()             # {name: value} of every register a poll reads
    """

    def __init__(self, link, device_map=None, slave=None, timeout=None, token=None):
        self.map = device_map
        protocol = device_map.protocol if device_map else {}
        timeout = timeout if timeout is not None else protocol.get('timeout_ms', 1000) / 1000.0
        slave = slave if slave is not None else (device_map.slave if device_map else 1)
        self.master = Master(link, slave, timeout)
        self.link = link
        if token is not None and device_map and device_map.login:
            addr, size = device_map.login
            self.master.write(addr, token.encode('utf-8')[:size].ljust(size, b'\0'))

    def close(self):
        self.link.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ---- raw

    def read_raw(self, addr, count):
        return self.master.read(addr, count)

    def write_raw(self, addr, data):
        self.master.write(addr, data)

    def device_id(self):
        return int.from_bytes(self.master.read(f.DEVICE_ID, 2), 'little')

    # ---- by name

    def read(self, *names):
        """the shown values of these registers: one value, or a dict for several"""
        regs = [self.map[n] for n in names]
        values = self._read_registers(regs)
        return values[regs[0].name] if len(regs) == 1 else values

    def decoded(self, name):
        """what the register's value means: a name, or its fields"""
        reg = self.map[name]
        return reg.decoded(self.master.read(reg.addr, reg.size))

    def write(self, name, value, force=False):
        """a value written (a number in shown units, or a value name), then read back. A read-only register
        is refused; a danger register and a value past min or max need force=True."""
        reg = self.map[name]
        if not reg.writable:
            raise EvreError('%s is read-only' % reg.name)
        data = reg.encode(value)
        if reg.is_number and not force:
            problem = reg.write_limit_problem(reg.decode(data))
            if problem:
                raise EvreError('%s = %s is %s (the map\'s limit; force=True writes it)' % (reg.name, value, problem))
        if reg.danger and not force:
            raise EvreError('%s is marked danger: force=True to write it' % reg.name)
        self.master.write(reg.addr, data)
        return reg.decode(self.master.read(reg.addr, reg.size)) if reg.readable else None

    def __getitem__(self, name):
        return self.read(name)

    def __setitem__(self, name, value):
        self.write(name, value)

    def read_all(self):
        """{name: shown value} of every readable register (byte arrays up to 32 bytes), read in blocks"""
        return self._read_registers([r for r in self.map if r.readable and (r.is_number or r.size <= 32)])

    def _read_registers(self, regs):
        values = {}
        blocks = []
        for reg in sorted(regs, key=lambda r: r.addr):
            last = blocks[-1] if blocks else None
            if (last and (reg.addr & 0xFF00) == (last[0] & 0xFF00) and reg.addr - (last[0] + last[1]) <= MAX_BLOCK_GAP
                    and reg.addr + reg.size - last[0] <= 255):
                last[1] = max(last[1], reg.addr + reg.size - last[0])
                last[2].append(reg)
            else:
                blocks.append([reg.addr, reg.size, [reg]])
        for addr, size, members in blocks:
            try:
                data = self.master.read(addr, size)
                for reg in members:
                    values[reg.name] = reg.decode(data[reg.addr - addr:reg.addr - addr + reg.size])
            except EvreError as e:
                if e.code is None or len(members) == 1:
                    raise
                for reg in members:  # a merged read over a gap the device refuses: one read each
                    values[reg.name] = reg.decode(self.master.read(reg.addr, reg.size))
        return values


def load_map(path):
    return DeviceMap.load(path) if isinstance(path, str) else path


def connect_tcp(host, port, device_map=None, token=None, slave=None, timeout=None):
    """a Device over TCP; device_map: a path or a DeviceMap"""
    m = load_map(device_map) if device_map is not None else None
    t = timeout if timeout is not None else ((m.protocol.get('timeout_ms', 1000) / 1000.0) if m else 1.0)
    return Device(TcpLink(host, port, t), m, slave, timeout, token)


def connect_serial(port, baud=115200, device_map=None, token=None, slave=None, timeout=None):
    """a Device over a serial or USB CDC port (pyserial)"""
    m = load_map(device_map) if device_map is not None else None
    t = timeout if timeout is not None else 1.0
    return Device(SerialLink(port, baud, t), m, slave, timeout, token)

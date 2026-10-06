# SPDX-License-Identifier: Apache-2.0
"""The register map, evre-map/1 (see docs/MAP_FORMAT.md): registers, their
values as numbers and names, and overlays ("extends")."""
import json
import math
import os
import struct

TYPES = {'u8': ('<B', 1), 'i8': ('<b', 1), 'u16': ('<H', 2), 'i16': ('<h', 2), 'u32': ('<I', 4),
         'i32': ('<i', 4), 'f32': ('<f', 4), 'bytes': (None, 0)}
C_NAMES = {'uint8_t': 'u8', 'int8_t': 'i8', 'uint16_t': 'u16', 'int16_t': 'i16', 'uint32_t': 'u32',
           'int32_t': 'i32', 'float': 'f32'}


def word(value):
    """a 16-bit number as the map writes it: "0xD004", "53252" or 53252"""
    if isinstance(value, str):
        value = int(value.strip(), 16) if value.strip().lower().startswith('0x') else int(value.strip())
    if not 0 <= value <= 0xFFFF:
        raise ValueError('not a 16-bit number: %r' % value)
    return value


def number_key(key):
    key = key.strip()
    return int(key, 16) if key.lower().startswith('0x') else (float(key) if '.' in key or 'e' in key.lower() else int(key))


class Field:
    def __init__(self, spec):
        self.name = spec['name']
        high, _, low = spec['bits'].partition(':')
        high, low = int(high), int(low or high)
        self.lsb, self.width = min(high, low), abs(high - low) + 1
        self.access = spec.get('access')
        self.desc = spec.get('desc', '')
        self.values = {number_key(k): v for k, v in (spec.get('values') or {}).items()}

    @property
    def mask(self):
        return ((1 << self.width) - 1) << self.lsb

    def of(self, raw):
        """this field's value in the register's raw integer"""
        return (raw >> self.lsb) & ((1 << self.width) - 1)


class Register:
    def __init__(self, spec):
        self.spec = spec
        self.addr = word(spec['addr'])
        self.name = spec.get('name', '0x%04X' % self.addr)
        self.type = C_NAMES.get(spec.get('type', 'u16').strip().lower(), spec.get('type', 'u16').strip().lower())
        if self.type not in TYPES:
            raise ValueError('%s: unknown type %r' % (self.name, self.type))
        self.size = int(spec.get('size', 1)) if self.type == 'bytes' else TYPES[self.type][1]
        access = spec.get('access', 'ro').lower()
        self.writable, self.readable = 'w' in access, access != 'wo'
        self.write_kind = spec.get('write', 'normal')
        self.persist = bool(spec.get('persist', False))
        self.danger = bool(spec.get('danger', False))
        self.plot = bool(spec.get('plot', True))  # False: a fixed value, not a line on a chart
        self.unit, self.group = spec.get('unit', ''), spec.get('group', 'Registers')
        self.desc, self.notes = spec.get('desc', ''), spec.get('notes', '')
        self.scale, self.offset = float(spec.get('scale', 1)), float(spec.get('offset', 0))
        self.min, self.max = spec.get('min'), spec.get('max')
        # "past_limits": "clamp": the device takes a value past min or max and clamps it (EVRe Guard lets it through)
        self.clamps = str(spec.get('past_limits') or 'refuse').strip().lower() == 'clamp'
        self.closed = bool(spec.get('closed', False))  # only the value names, specials and an action's idle value
        self.reserved_zero = bool(spec.get('reserved_zero', False))  # the bits no field covers are written 0
        self.enum = {number_key(k): v for k, v in (spec.get('enum') or {}).items()}
        self.special = {float(number_key(k)): v for k, v in (spec.get('special') or {}).items()}
        self.fields = [Field(s) for s in spec.get('fields') or []]
        default = spec.get('default')
        if isinstance(default, str):
            default = self.value_of_name(default)
        self.default = default

    def __repr__(self):
        return 'Register(%s @ 0x%04X, %s)' % (self.name, self.addr, self.type)

    @property
    def is_number(self):
        return self.type != 'bytes'

    # ---- bytes <-> values

    def raw_int(self, data):
        """the raw integer in the bytes (a float: its bits)"""
        if self.type == 'f32':
            return struct.unpack('<I', data[:4])[0]
        return struct.unpack(TYPES[self.type][0], data[:self.size])[0]

    def decode(self, data):
        """bytes -> the shown value: raw x scale + offset (bytes: the bytes themselves)"""
        if not self.is_number:
            return bytes(data)
        raw = struct.unpack(TYPES[self.type][0], data[:self.size])[0]
        return raw * self.scale + self.offset if (self.scale != 1 or self.offset != 0) else raw

    def encode(self, value):
        """a shown value (or a value or special name) -> bytes; ValueError if it does not fit the type"""
        if not self.is_number:
            data = bytes(value)
            if len(data) != self.size:
                raise ValueError('%s: %d bytes needed' % (self.name, self.size))
            return data
        if isinstance(value, str):
            value = self.value_of_name(value)
        if not math.isfinite(value):  # no device takes NaN or an infinity (EVRe Guard refuses them, 15)
            raise ValueError('%s: %s is not a finite number' % (self.name, value))
        raw = (value - self.offset) / (self.scale or 1)
        if self.type == 'f32':
            try:
                data = struct.pack('<f', raw)
            except OverflowError:
                data = b''
            if not data or not math.isfinite(struct.unpack('<f', data)[0]):
                raise ValueError('%s: %s is past the largest f32 value' % (self.name, value))
            return data
        raw = int(round(raw))
        try:
            return struct.pack(TYPES[self.type][0], raw)
        except struct.error:
            raise ValueError('%s: %s is out of range for %s' % (self.name, value, self.type))

    def value_of_name(self, name):
        """the shown value a special value's or an enum's name stands for"""
        for value, special in self.special.items():
            if special.lower() == name.lower():
                return value
        for raw, enum in self.enum.items():
            if enum.lower() == name.lower():
                return raw * self.scale + self.offset
        raise ValueError('%s has no value named %r' % (self.name, name))

    def decoded(self, data):
        """what the value means: a special or enum name, or {field: value (or its name)}"""
        if not self.is_number:
            return None
        shown = self.decode(data)
        for value, name in self.special.items():
            if math.isclose(shown, value, rel_tol=1e-6, abs_tol=1e-9):
                return name
        raw = self.raw_int(data)
        if self.enum:
            return self.enum.get(raw, '? (%d)' % raw)
        if self.fields:
            return {fl.name: fl.values.get(fl.of(raw), fl.of(raw)) for fl in self.fields}
        return None

    def limit_problem(self, shown):
        """why a shown value is past min or max, or None (a special value never is)"""
        if any(math.isclose(shown, v, rel_tol=1e-6, abs_tol=1e-9) for v in self.special):
            return None
        if self.min is not None and shown < self.min and not math.isclose(shown, self.min, rel_tol=1e-6):
            return 'below the minimum %s %s' % (self.min, self.unit)
        if self.max is not None and shown > self.max and not math.isclose(shown, self.max, rel_tol=1e-6):
            return 'above the maximum %s %s' % (self.max, self.unit)
        return None

    def write_problem(self, data):
        """why these bytes should not be sent, as a device with EVRe Guard would refuse them, or None: a value outside a
        closed set, a bit no field covers set (reserved_zero), past min or max (not for a register that clamps)"""
        shown, raw = self.decode(data), self.raw_int(data)
        if self.type.startswith('i'):
            raw = struct.unpack(TYPES[self.type][0], data[:self.size])[0]
        if self.reserved_zero and self.fields:
            covered = 0
            for fl in self.fields:
                covered |= fl.mask
            if raw & ((1 << (8 * self.size)) - 1) & ~covered:
                return "outside the fields' bits"
        if self.closed:
            idle = round(((self.default or 0) - self.offset) / (self.scale or 1))
            if raw not in self.enum and not any(math.isclose(shown, v, rel_tol=1e-6, abs_tol=1e-9) for v in self.special) \
                    and not (self.write_kind == 'action' and raw == idle):
                return 'outside the closed set of values'
        return None if self.clamps else self.limit_problem(shown)


class DeviceMap:
    """A map file, read with its base when it "extends" one."""

    def __init__(self, doc, path=None):
        self.doc, self.path = doc, path
        self.format = doc.get('format', 'evre-map/1')
        self.device = doc.get('device', '')
        self.device_id = word(doc['device_id']) if doc.get('device_id') is not None else None
        self.slave = int(doc.get('slave', 1))
        login = doc.get('login')
        self.login = (word(login['addr']), int(login.get('size', 16))) if login else None
        self.protocol = doc.get('protocol') or {}
        self.registers = sorted((Register(r) for r in doc.get('registers', [])), key=lambda r: r.addr)
        self._by_name = {r.name.lower(): r for r in self.registers}

    @classmethod
    def load(cls, path, _depth=0):
        with open(path, encoding='utf-8-sig') as f:
            doc = json.load(f)
        if not isinstance(doc, dict):
            raise ValueError('%s: not a map (the file is not a JSON object)' % path)
        base_name = doc.get('extends')
        if base_name:
            if _depth >= 8:
                raise ValueError('"extends" goes more than 8 maps deep')
            base = cls.load(os.path.join(os.path.dirname(os.path.abspath(path)), base_name), _depth + 1).doc
            doc = merge(base, doc)
        return cls(doc, path)

    def __getitem__(self, key):
        """a register by name (any case) or by address"""
        if isinstance(key, int):
            for r in self.registers:
                if r.addr == key:
                    return r
            raise KeyError('no register at 0x%04X' % key)
        if key.lower().startswith('0x'):
            return self[int(key, 16)]
        try:
            return self._by_name[key.lower()]
        except KeyError:
            raise KeyError('no register %s in the map' % key)

    def __iter__(self):
        return iter(self.registers)

    def __len__(self):
        return len(self.registers)


def merge(base, overlay):
    """an overlay on its base (MAP_FORMAT.md section 7)"""
    doc = {k: v for k, v in base.items() if k != 'registers'}
    for k, v in overlay.items():
        if k in ('registers', 'extends'):
            continue
        if v is None:
            doc.pop(k, None)
        else:
            doc[k] = v
    regs = [dict(r) for r in base.get('registers', [])]
    for item in overlay.get('registers', []):
        addr = word(item['addr'])
        match = next((r for r in regs if word(r['addr']) == addr), None)
        if item.get('remove'):
            if match is not None:
                regs.remove(match)
            continue
        if match is None:
            regs.append(dict(item))
            continue
        for k, v in item.items():
            if k == 'addr':
                continue
            if v is None:
                match.pop(k, None)
            else:
                match[k] = v
    doc['registers'] = regs
    return doc

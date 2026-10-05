#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EVRe Guard's table export (evre export MAP --to guard), compiled with the library and the Guard, and driven.

    python guard_table_test.py <build folder> [--lib DIR] [--skip m32,arm-none-eabi-g++,avr-g++]

1. A neutral test map with every type, a negative scale, an offset, limits beyond the type, a special outside the
   limits, an f32 limit that is no exact float (3.65), limits past the largest f32, a -0 special, a bytes register,
   a w1c register, a read-only register inside a writable run, a gap, "past_limits": "clamp", an action register
   with a default, a "closed" action register (its idle value listed), a "reserved_zero" register with fields, and a
   login register in the device bank. It is exported with <build>/evre, compiled with the
   library (lib/EVRe.cpp: --lib, else $EVRE_LIB, else ../../lib) and EVRe Guard (lib/guard) into a device, and
   driven through decodePacketInto: every limit at its edges (acknowledged, 3 or 15, the edges worked out here from
   the map's numbers and GUARD_PLAN.md's rounding rules, not from the export), the memory unchanged after a refusal,
   the login first.
2. The typed constants equal the entries' limits; an f32 limit past the largest float is FLT_MAX; a -0 special is
   listed as +0.
3. keep_limits() of the device table (--to table) and the Guard agree on 10 000 random values per register, where
   both apply (not on a register the device clamps).
4. A device struct tied by static_asserts to the C header export (--to h, _ADDR and _SIZE) builds; one member moved
   by a byte fails the build.
5. Each export error fires on its map; --check gives its exit codes (for --to guard and --to h); --to table equals
   tests/golden/example_device_table.h byte for byte.
6. The generated table compiled on the build matrix with -Wall -Wextra -Wpedantic -Werror: g++ as C++11 to C++20,
   and -m32, arm-none-eabi-g++ (Cortex-M7, Cortex-M0) and avr-g++ where they are there (or named on --skip).
7. --to table --lib 1.1: the test map (with its login register), which 1.0 refuses for its read-only register after
   a writable one, exported as one image on ranges with the Guard's entries; a device bound by it answers every
   vector of 1. as the device of 1. does; its ranges are one per run; an entry moved off its member fails the build
   with that register's name; the header on the build matrix; --check; --lib's misuse; example_device.json equals
   tests/golden/example_device_table_11.h byte for byte.
Without the library or g++ the compile parts say SKIP.
Exit code: 0 all passed, 1 a check failed, 2 a program is missing."""
import argparse
import json
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
passed = failed = 0

MAP = {
    "format": "evre-map/1", "device": "Guard test", "device_id": "0x4321",
    "login": {"addr": "0xD030", "size": 8},
    "registers": [
        {"addr": "0xA004", "name": "CONFIG", "type": "u16", "access": "rw", "group": "Protocol"},
        {"addr": "0xD000", "name": "UPTIME", "type": "u32", "unit": "ms"},
        {"addr": "0xD004", "name": "TEMP_SET", "type": "i16", "access": "rw", "scale": -0.1, "offset": 20, "unit": "C",
         "min": -50, "max": 50},
        {"addr": "0xD006", "name": "LEVEL", "type": "u8", "access": "rw", "min": -10, "max": 300},
        {"addr": "0xD007", "name": "MODE_I8", "type": "i8", "access": "rw", "min": -100, "max": 100,
         "special": {"-128": "unset"}},
        {"addr": "0xD008", "name": "VOLT", "type": "f32", "access": "rw", "unit": "V", "min": 0, "max": 3.65,
         "special": {"-1": "off"}},
        {"addr": "0xD00C", "name": "BIG", "type": "f32", "access": "rw", "min": -1e39, "max": 1e39,
         "special": {"-0": "zero"}},
        {"addr": "0xD010", "name": "COUNT", "type": "u16", "access": "rw", "scale": 0.5, "min": 0.75, "max": 100.2},
        {"addr": "0xD012", "name": "NAME", "type": "bytes", "size": 6, "access": "rw"},
        {"addr": "0xD018", "name": "CLEAR", "type": "u8", "access": "rw", "write": "w1c"},
        {"addr": "0xD019", "name": "STATUS_RO", "type": "u8"},
        {"addr": "0xD01A", "name": "SPEED", "type": "i16", "access": "rw", "min": -100, "max": 100,
         "past_limits": "clamp"},
        {"addr": "0xD01E", "name": "GO", "type": "u8", "access": "rw", "write": "action", "default": 0, "min": 0,
         "max": 3},
        {"addr": "0xD01F", "name": "TOTAL", "type": "u32", "access": "rw", "min": 10, "max": 4e9},
        {"addr": "0xD023", "name": "SIGNED", "type": "i32", "access": "rw", "min": -2e9, "max": 2e9},
        {"addr": "0xD027", "name": "COMMAND", "type": "u8", "access": "rw", "write": "action", "closed": True,
         "max": 15, "enum": {"1": "go", "2": "stop", "5": "home"}},
        {"addr": "0xD028", "name": "CTRL", "type": "u16", "access": "rw", "reserved_zero": True,
         "fields": [{"name": "MODE", "bits": "1:0"}, {"name": "LEVEL", "bits": "11:8"}]},
    ]}

TOKEN = b'token123'
U8, I8, U16, I16, U32, I32 = '<B', '<b', '<H', '<h', '<I', '<i'


def f32(value):
    return struct.pack('<f', value)


def f32bits(bits):
    return struct.pack('<I', bits)


def next_up(data):
    """the next float above a positive one (its bits + 1)"""
    return f32bits(struct.unpack('<I', data)[0] + 1)


# (what, address, bytes, the code a device with the Guard answers). The edges come from the map's numbers and the
# rounding rules (GUARD_PLAN.md section 6), worked out by hand: TEMP_SET raw = (shown - 20) / -0.1, so -50 .. 50 is
# raw 700 .. -300, swapped; COUNT raw = shown / 0.5, 0.75 .. 100.2 is 1.5 .. 200.4, rounded inward to 2 .. 200.
VECTORS = [
    ('TEMP_SET raw -301: below the swapped min', 0xD004, struct.pack(I16, -301), 15),
    ('TEMP_SET raw -300 (shown 50, the map\'s max)', 0xD004, struct.pack(I16, -300), 0),
    ('TEMP_SET raw 700 (shown -50, the map\'s min)', 0xD004, struct.pack(I16, 700), 0),
    ('TEMP_SET raw 701', 0xD004, struct.pack(I16, 701), 15),
    ('LEVEL 0 (min -10: the type\'s end)', 0xD006, struct.pack(U8, 0), 0),
    ('LEVEL 255 (max 300: the type\'s end)', 0xD006, struct.pack(U8, 255), 0),
    ('MODE_I8 -101', 0xD007, struct.pack(I8, -101), 15),
    ('MODE_I8 -100', 0xD007, struct.pack(I8, -100), 0),
    ('MODE_I8 100', 0xD007, struct.pack(I8, 100), 0),
    ('MODE_I8 101', 0xD007, struct.pack(I8, 101), 15),
    ('MODE_I8 -128, a special outside the limits', 0xD007, struct.pack(I8, -128), 0),
    ('MODE_I8 -127, next to the special', 0xD007, struct.pack(I8, -127), 15),
    ('VOLT 0', 0xD008, f32(0.0), 0),
    ('VOLT -0.0 against a min of 0', 0xD008, f32bits(0x80000000), 0),
    ('VOLT the negative subnormal below 0', 0xD008, f32bits(0x80000001), 15),
    ('VOLT 3.65 as a host sends it (the nearest float)', 0xD008, f32(3.65), 0),
    ('VOLT the next float above 3.65', 0xD008, next_up(f32(3.65)), 15),
    ('VOLT -1 "off", a special below the min', 0xD008, f32(-1.0), 0),
    ('VOLT NaN', 0xD008, f32bits(0x7FC00000), 15),
    ('VOLT +infinity', 0xD008, f32bits(0x7F800000), 15),
    ('BIG FLT_MAX (max 1e39: the largest float)', 0xD00C, f32bits(0x7F7FFFFF), 0),
    ('BIG -FLT_MAX', 0xD00C, f32bits(0xFF7FFFFF), 0),
    ('BIG -infinity', 0xD00C, f32bits(0xFF800000), 15),
    ('COUNT raw 1 (shown 0.5, below 0.75)', 0xD010, struct.pack(U16, 1), 15),
    ('COUNT raw 2 (shown 1, the first step at or above 0.75)', 0xD010, struct.pack(U16, 2), 0),
    ('COUNT raw 200 (shown 100, the last step at or below 100.2)', 0xD010, struct.pack(U16, 200), 0),
    ('COUNT raw 201', 0xD010, struct.pack(U16, 201), 15),
    ('NAME, 2 of its 6 bytes', 0xD014, b'xy', 0),
    ('NAME whole', 0xD012, b'abcdef', 0),
    ('CLEAR 0xFF (w1c: no limits)', 0xD018, struct.pack(U8, 0xFF), 0),
    ('STATUS_RO, read-only inside the writable run', 0xD019, b'\x01', 3),
    ('SPEED -101 (clamp: any value)', 0xD01A, struct.pack(I16, -101), 0),
    ('SPEED 32767 (clamp)', 0xD01A, struct.pack(I16, 32767), 0),
    ('the gap 0xD01C..0xD01D', 0xD01C, b'\x00\x00', 3),
    ('SPEED and the gap after it', 0xD01A, b'\x00\x00\x00\x00', 3),
    ('GO 3 (action, max 3)', 0xD01E, struct.pack(U8, 3), 0),
    ('GO 4', 0xD01E, struct.pack(U8, 4), 15),
    ('TOTAL 9', 0xD01F, struct.pack(U32, 9), 15),
    ('TOTAL 10', 0xD01F, struct.pack(U32, 10), 0),
    ('TOTAL 4000000000 (above 0x7FFFFFFF)', 0xD01F, struct.pack(U32, 4000000000), 0),
    ('TOTAL 4000000001', 0xD01F, struct.pack(U32, 4000000001), 15),
    ('SIGNED -2000000001', 0xD023, struct.pack(I32, -2000000001), 15),
    ('SIGNED -2000000000', 0xD023, struct.pack(I32, -2000000000), 0),
    ('SIGNED 2000000000', 0xD023, struct.pack(I32, 2000000000), 0),
    ('SIGNED 2000000001', 0xD023, struct.pack(I32, 2000000001), 15),
    ('VOLT, its first two bytes', 0xD008, b'\x00\x00', 3),
    ('VOLT, its last three bytes', 0xD009, b'\x00\x00\x00', 3),
    ('TEMP_SET .. MODE_I8, all good', 0xD004, struct.pack(I16, 0) + struct.pack(U8, 1) + struct.pack(I8, 5), 0),
    ('TEMP_SET .. MODE_I8, the last bad: none stored', 0xD004, struct.pack(I16, 1) + struct.pack(U8, 2) + struct.pack(I8, 120), 15),
    ('TOTAL .. SIGNED, the first bad: none stored', 0xD01F, struct.pack(U32, 1) + struct.pack(I32, 0), 15),
    ('the login register in a session: part 1 refuses a write over it', 0xD030, b'\x00' * 4, 3),
    ('COMMAND 1 "go" (closed)', 0xD027, struct.pack(U8, 1), 0),
    ('COMMAND 5 "home"', 0xD027, struct.pack(U8, 5), 0),
    ('COMMAND 0, the idle value of an action (no default)', 0xD027, struct.pack(U8, 0), 0),
    ('COMMAND 3: inside min .. max, not in the closed set', 0xD027, struct.pack(U8, 3), 15),
    ('CTRL 0x0F03: MODE and LEVEL bits', 0xD028, struct.pack(U16, 0x0F03), 0),
    ('CTRL 0x0004: bit 2, no field covers it', 0xD028, struct.pack(U16, 0x0004), 15),
    ('CTRL 0x8000: bit 15, no field covers it', 0xD028, struct.pack(U16, 0x8000), 15),
]

PROGRAM = r'''
#include <cstdio>
#include <cstring>

#include "EVRe.h"
#include "evre_guard.h"
#include "evre_guard_desc.h"
#include "guard_test_guard.h"

static uint8_t ro[4], rw[0x26], loginMemory[8];
static const evre_range_t ranges[] = { { 0xD000, 4, ro, 0 }, { 0xD004, 0x26, rw, 1 }, { 0xD030, 8, loginMemory, 1 } };
static const uint8_t token[8] = { 't', 'o', 'k', 'e', 'n', '1', '2', '3' };
static uint64_t clockMs = 1;
static uint64_t now() { return clockMs; }
static const evre_guard_config_t config = { 0xD030, 8, token, nullptr, 0, 3, 1000, 8000, 60000, now };
static evre_base_t dev;
static evre_guard_t guard;
static evre_guard_check_t check;
static int failed = 0;

static uint8_t onWrite(evre_base_t *d, uint16_t off, const uint8_t *data, uint16_t cnt) {
	return evre_guard_write_checked(&guard, &check, d, off, data, cnt);
}

static void result(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) failed++;
}

/* a WRITE_ACK as a host sends it: the code; *answered the answer's function code */
static uint8_t write(uint16_t off, const uint8_t *data, uint16_t cnt, uint8_t *answered, uint8_t *code) {
	uint8_t req[64], answer[64];
	uint16_t n = 0, got = 0;
	req[n++] = 0x7B;
	req[n++] = 1;
	req[n++] = WRITE_ACK;
	req[n++] = uint8_t(off);
	req[n++] = uint8_t(off >> 8);
	req[n++] = uint8_t(cnt);
	req[n++] = uint8_t(cnt >> 8);
	std::memcpy(req + n, data, cnt);
	n = uint16_t(n + cnt);
	const uint16_t crc = GetCrc16(req, n);
	req[n++] = uint8_t(crc);
	req[n++] = uint8_t(crc >> 8);
	req[n++] = 0x7D;
	const uint8_t ret = decodePacketInto(&dev, req, n, answer, sizeof answer, &got);
	*answered = got ? answer[2] : 0;
	*code = got == 11 ? answer[7] : 0;
	return ret;
}

struct Vector {
	const char *what;
	uint16_t off, cnt;
	uint8_t data[8];
	uint8_t want;
};
#include "vectors.h"

int main() {
	dev.SALVE_ID_REG = 1;
	dev.D_RANGES = ranges;
	dev.D_RANGE_CNT = 3;
	result(protocolInit(&dev) == NO_ERROR && evre_guard_init(&guard, &config) == NO_ERROR, "the device and its login");
	result(evre_guard_check_init(&check, &guard_test_table, &dev) == NO_ERROR, "the generated table: init takes it");
	dev.WRITE_HANDLER = onWrite;
	uint8_t answered = 0, code = 0;
	const uint8_t one[2] = { 0, 0 };
	result(write(0xD004, one, 2, &answered, &code) == LOGIN_REQUIRED && code == LOGIN_REQUIRED,
			"without a session: 13, no value looked at");
	result(write(0xD030, token, 8, &answered, &code) == NO_ERROR && answered == WRITE_ACK_RESP, "the login");
	for (const Vector &v : VECTORS) {
		uint8_t before[sizeof rw];
		std::memcpy(before, rw, sizeof rw);
		const uint8_t ret = write(v.off, v.data, v.cnt, &answered, &code);
		const bool ok = v.want == 0 ? ret == NO_ERROR && answered == WRITE_ACK_RESP
				: ret == v.want && answered == ERROR_RESP && code == v.want && std::memcmp(before, rw, sizeof rw) == 0;
		char line[200];
		std::snprintf(line, sizeof line, "%s: %s (got %u)", v.what, v.want == 0 ? "acknowledged, stored" : v.want == 15
				? "15, nothing stored" : "3, nothing stored", ret);
		result(ok, line);
	}
	return failed ? 1 : 0;
}
'''

# 7: the same device on library 1.1's table (ranges, the image and the Guard's entries in one header). The map
# gets its login register, so the image holds it and a range serves it.
MAP11 = dict(MAP, registers=MAP['registers'] + [
    {"addr": "0xD030", "name": "LOGIN", "type": "bytes", "size": 8, "access": "wo"}])
PROGRAM11 = PROGRAM.replace('#include "guard_test_guard.h"', '#include "guard_test_table.h"').replace(
    'static uint8_t ro[4], rw[0x26], loginMemory[8];\n'
    'static const evre_range_t ranges[] = { { 0xD000, 4, ro, 0 }, { 0xD004, 0x26, rw, 1 }, { 0xD030, 8, loginMemory, 1 } };\n',
    'static guard_test_image_t image;\n'
    'uint8_t protocolConfigure(base_t *d) { return guard_test_bind(d, &image); }\n').replace(
    '\tdev.D_RANGES = ranges;\n\tdev.D_RANGE_CNT = 3;\n', '').replace(
    'evre_guard_check_init(&check, &guard_test_table, &dev)', 'guard_test_check_init(&check, &dev)').replace(
    'sizeof rw', 'sizeof image').replace('before, rw, sizeof', 'before, &image, sizeof').replace(
    'int main() {', 'int main() {\n\tresult(GUARD_TEST_RANGES == 4u, "[1.1] four ranges: UPTIME, TEMP_SET .. CLEAR, STATUS_RO, SPEED '
    '.. the login");')

# keep_limits (the device table) against the Guard, value by value; the table knows neither a
# closed set nor reserved bits, so COMMAND and CTRL stay out
AGREE_MAP = {
    "format": "evre-map/1", "device": "Agree",
    "registers": [r for r in MAP["registers"] if r["addr"] not in ("0xA004", "0xD000", "0xD019", "0xD00C", "0xD01A",
                                                                            "0xD027", "0xD028")]
    + [{"addr": "0xD000", "name": "UPTIME", "type": "u32"}]}

AGREE_PROGRAM = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "EVRe.h"
#include "evre_guard_desc.h"
#include "agree_guard.h"
#include "agree_table.h"

static agree_ro_t ro;
static agree_rw_t rw;
static base_t dev;
static evre_guard_check_t check;
static uint32_t state = 12345;
static uint32_t next() { state = state * 1103515245u + 12345u; return state >> 1; }

int main() {
	protocolInit(&dev);
	agree_bind(&dev, &ro, &rw);
	if (evre_guard_check_init(&check, &agree_table, &dev) != NO_ERROR) {
		std::printf("FAIL the table on the device table's pointer table: init\n");
		return 1;
	}
	std::printf("PASS the Guard's table on the device table's pointer table: init takes it\n");
	int failed = 0;
	for (uint16_t i = 0; i < agree_table.n_regs; ++i) {
		const evre_guard_desc_t &reg = agree_table.regs[i];
		if (reg.type == EVRE_GUARD_BYTES) continue;
		unsigned disagree = 0;
		for (int k = 0; k < 10000; ++k) {
			uint8_t data[4];
			uint32_t v = next() ^ (next() << 16);
			if (k % 4 == 0) v = reg.min + (k % 3) - 1; /* the limits and their neighbours */
			if (k % 4 == 1) v = reg.max + (k % 3) - 1;
			for (int b = 0; b < 4; ++b) data[b] = uint8_t(v >> (8 * b));
			if (reg.type == EVRE_GUARD_F32 && (data[3] & 0x7F) == 0x7F && (data[2] & 0x80)) continue; /* NaN, inf: below */
			const bool guardTakes = evre_guard_check_write(&check, &dev, reg.addr, data, reg.size) == NO_ERROR;
			/* the value written into rw, another one in seen: keep_limits puts seen's back when it refuses */
			agree_rw_t seen = rw;
			uint8_t *written = reinterpret_cast<uint8_t *>(&rw) + (reg.addr - AGREE_WRITE_MIN);
			uint8_t *old = reinterpret_cast<uint8_t *>(&seen) + (reg.addr - AGREE_WRITE_MIN);
			for (int b = 0; b < reg.size; ++b) old[b] = uint8_t(data[b] ^ 0x5A);
			std::memcpy(written, data, reg.size);
			agree_keep_limits(&rw, &seen);
			const bool kept = std::memcmp(written, data, reg.size) == 0;
			if (guardTakes != kept) disagree++;
		}
		std::printf("%s register at 0x%04X: the Guard and keep_limits agree on 10000 values (%u differ)\n",
				disagree ? "FAIL" : "PASS", reg.addr, disagree);
		if (disagree) failed++;
	}
	return failed ? 1 : 0;
}
'''


def check(ok, what):
    global passed, failed
    print('%s %s' % ('PASS' if ok else 'FAIL', what))
    sys.stdout.flush()
    if ok:
        passed += 1
    else:
        failed += 1


def library(opt):
    for folder in filter(None, [opt, os.environ.get('EVRE_LIB'), os.path.join(HERE, '..', '..', 'lib')]):
        if all(os.path.exists(os.path.join(folder, f)) for f in ('EVRe.h', 'EVRe.cpp', 'guard/evre_guard_desc.cpp')):
            return os.path.abspath(folder)
    return None


def export(cli, folder, map_obj, name, to='guard', extra=()):
    path = os.path.join(folder, name + '.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(map_obj, f)
    out = os.path.join(folder, name + ('_guard' if to == 'guard' else '_table.h' if to == 'table' else '.h'))
    r = subprocess.run([cli, 'export', path, '--to', to, '-o', out] + list(extra), capture_output=True, text=True)
    return r, out


def vectors_h(vectors):
    lines = ['static const Vector VECTORS[] = {']
    for what, addr, data, want in vectors:
        lines.append('\t{ "%s", 0x%04Xu, %du, { %s }, %du },' % (what.replace('"', '\\"'), addr, len(data),
                                                              ', '.join('0x%02X' % b for b in data), want))
    lines.append('};')
    return '\n'.join(lines) + '\n'


def entries(source):
    """the generated .cpp's entries: name -> (type, min, max, n_values, first_value)"""
    out = {}
    for m in re.finditer(r'\{ 0x([0-9A-F]+)u, (\d+)u, (EVRE_GUARD_\w+), (?:0u|EVRE_GUARD_CLOSED), (\d+)u, 0u, (\d+)u, 0u, '
                         r'0x([0-9A-F]+)UL, 0x([0-9A-F]+)UL, 0x[0-9A-F]{8}UL \}, /\* (\w+):', source):
        out[m.group(8)] = (m.group(3), int(m.group(6), 16), int(m.group(7), 16), int(m.group(4)), int(m.group(5)))
    return out


def constant_bits(ctype, literal):
    literal = literal.strip()
    if ctype == 'float':
        return struct.unpack('<I', struct.pack('<f', float(literal.rstrip('f'))))[0]
    value = int(literal.rstrip('u').replace('(', '').replace(')', '').replace(' - 1', '')) - (1 if ' - 1' in literal else 0)
    return value & 0xFFFFFFFF


def compile_run(cc, folder, lib, sources, name, flags=(), run=True):
    exe = os.path.join(folder, name + ('.exe' if os.name == 'nt' else ''))
    cmd = [cc, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror'] + list(flags) + \
        ['-I', folder, '-I', lib, '-I', os.path.join(lib, 'guard')] + sources + \
        [os.path.join(lib, 'EVRe.cpp'), os.path.join(lib, 'guard', 'evre_guard.cpp'),
         os.path.join(lib, 'guard', 'evre_guard_desc.cpp'), '-o', exe]
    c = subprocess.run(cmd, capture_output=True, text=True)
    if c.returncode or not run:
        return c, None
    return c, subprocess.run([exe], capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('--lib', help='the folder with EVRe.h, EVRe.cpp and guard/')
    ap.add_argument('--skip', default='', help='compilers of the matrix not to use: m32, arm-none-eabi-g++, avr-g++')
    opts = ap.parse_args()
    skip = set(s.strip() for s in opts.skip.split(',') if s.strip())
    cli = os.path.join(opts.build, 'evre' + ('.exe' if os.name == 'nt' else ''))
    if not os.path.exists(cli):
        print('missing: %s' % cli)
        return 2
    lib = library(opts.lib)
    cc = shutil.which('g++')
    with tempfile.TemporaryDirectory() as folder:
        r, base = export(cli, folder, MAP, 'guard_test')
        header_path, source_path = base + '.h', base + '.cpp'
        check(r.returncode == 0 and os.path.exists(header_path) and os.path.exists(source_path),
              'exported: guard_test_guard.h and .cpp (%s)' % r.stderr.strip()[:200])
        header = open(header_path, encoding='utf-8').read() if os.path.exists(header_path) else ''
        source = open(source_path, encoding='utf-8').read() if os.path.exists(source_path) else ''
        got = entries(source)
        names = [reg['name'] for reg in MAP['registers']]
        check(set(got) == set(names) - {'CONFIG', 'UPTIME', 'STATUS_RO'},
              'an entry for each register a host writes, none for the reserved bank, a read-only register or the login (%s)'
              % sorted(got))
        check(list(got) == [n for n in names if n in got], 'the entries in address order')
        check(got.get('LEVEL', (0, 1, 1))[1:3] == (0, 0xFF), 'LEVEL: limits past the type are the type\'s ends')
        check(got.get('BIG', (0, 0, 0))[1:3] == (0xFF7FFFFF, 0x7F7FFFFF), 'BIG: limits past the largest f32 are -FLT_MAX .. FLT_MAX')
        check('0x00000000UL, /* BIG: 0 "zero" */' in source, 'BIG: the -0 special is listed as +0 (0x00000000)')
        check(got.get('VOLT', (0, 0, 0))[2] == struct.unpack('<I', f32(3.65))[0], 'VOLT: the max 3.65 is its nearest float')
        check(got.get('SPEED', (0, 0, 0))[1:3] == (0xFFFF8000, 0x00007FFF) and 'GUARD_TEST_SPEED_RAW_MIN = -100' in header,
              'SPEED (clamp): the type\'s full range in the entry, the map\'s limits in its typed constants')
        check(got.get('CLEAR', (0, 1, 1))[1:3] == (0, 0xFF), 'CLEAR (w1c): no limits')
        check('{ 0xD027u, 1u, EVRE_GUARD_U8, EVRE_GUARD_CLOSED, 4u, 0u,' in source and '0x00000000UL, /* COMMAND: 0 idle */' in source,
              'COMMAND (closed): EVRE_GUARD_CLOSED, its three names and its idle value 0 listed')
        check('0x0000F0FCUL }, /* CTRL:' in source, 'CTRL (reserved_zero): zero_bits 0xF0FC, the bits no field covers')
        check(got.get('TEMP_SET', (0, 0, 0))[1:3] == (0xFFFFFED4, 700),
              'TEMP_SET (scale -0.1, offset 20): min and max swapped in raw, -300 .. 700')
        check(got.get('COUNT', (0, 0, 0))[1:3] == (2, 200), 'COUNT (scale 0.5): 0.75 .. 100.2 rounded inward to raw 2 .. 200')
        # the typed constants equal the entries (but where the device clamps)
        constants = dict((m.group(2), (m.group(1), m.group(3))) for m in
                         re.finditer(r'constexpr (\w+) GUARD_TEST_(\w+)_RAW_(?:MIN|MAX) = ([^;]+);', header))
        same = True
        for m in re.finditer(r'constexpr (\w+) GUARD_TEST_(\w+)_RAW_(MIN|MAX) = ([^;]+);', header):
            ctype, name, which, literal = m.groups()
            entry = got.get(name)
            if entry and name != 'SPEED' and constant_bits(ctype, literal) != entry[1 if which == 'MIN' else 2]:
                same = False
                print('     %s_RAW_%s = %s, the entry %08X' % (name, which, literal, entry[1 if which == 'MIN' else 2]))
        check(same and len(constants) >= 8, 'the typed constants equal the entries\' limits (%d registers)' % len(constants))

        if not lib or not cc:
            print('SKIP the compile and run part: %s' % ('no g++' if not cc else 'no library with guard/ (--lib, EVRE_LIB)'))
        else:
            with open(os.path.join(folder, 'vectors.h'), 'w') as f:
                f.write(vectors_h(VECTORS))
            with open(os.path.join(folder, 'program.cpp'), 'w') as f:
                f.write(PROGRAM)
            c, run = compile_run(cc, folder, lib, [os.path.join(folder, 'program.cpp'), source_path], 'program', ['-Wpedantic'])
            check(c.returncode == 0, 'the device with the generated table builds (-Wall -Wextra -Wpedantic -Werror) %s'
                  % c.stderr.strip()[:600])
            if run:
                for line in run.stdout.splitlines():
                    if line.startswith(('PASS', 'FAIL')):
                        check(line.startswith('PASS'), '[device] ' + line.split(' ', 1)[1])
                check(run.returncode == 0, 'the device program passed')

            # keep_limits and the Guard agree
            r1, agree_base = export(cli, folder, AGREE_MAP, 'agree')
            r2, agree_table = export(cli, folder, AGREE_MAP, 'agree', 'table')
            check(r1.returncode == 0 and r2.returncode == 0, 'the agreement map: both exports (%s%s)' % (r1.stderr, r2.stderr))
            with open(os.path.join(folder, 'agree.cpp'), 'w') as f:
                f.write(AGREE_PROGRAM)
            c, run = compile_run(cc, folder, lib, [os.path.join(folder, 'agree.cpp'), agree_base + '.cpp'], 'agree')
            check(c.returncode == 0, 'the agreement program builds %s' % c.stderr.strip()[:600])
            if run:
                for line in run.stdout.splitlines():
                    if line.startswith(('PASS', 'FAIL')):
                        check(line.startswith('PASS'), '[keep_limits] ' + line.split(' ', 1)[1])

            # a device's own struct tied to the C header export
            r, c_header = export(cli, folder, MAP, 'layout', 'h')
            check(r.returncode == 0, 'the C header export')
            tied = ['#include <cstddef>', '#include <cstdint>', '#include "layout.h"',
                    'struct __attribute__((packed)) Settings {', '\tint16_t temp_set;', '\tuint8_t level;', '\tint8_t mode_i8;',
                    '\tfloat volt;', '};']
            asserts = ['static_assert(0xD004u + offsetof(Settings, %s) == %s_ADDR && sizeof(Settings::%s) == %s_SIZE, "%s");'
                       % (m, m.upper(), m, m.upper(), m) for m in ('temp_set', 'level', 'mode_i8', 'volt')]
            good = '\n'.join(tied + asserts + ['int main() { return 0; }']) + '\n'
            moved = good.replace('\tuint8_t level;', '\tuint8_t pad;\n\tuint8_t level;')
            results = []
            for name, text in (('tied', good), ('moved', moved)):
                with open(os.path.join(folder, name + '.cpp'), 'w') as f:
                    f.write(text)
                results.append(subprocess.run([cc, '-std=c++11', '-I', folder, '-c', os.path.join(folder, name + '.cpp'), '-o',
                                               os.path.join(folder, name + '.o')], capture_output=True, text=True).returncode)
            check(results == [0, 1] if len(results) == 2 else False,
                  'a device struct tied by static_asserts to the C header export builds; one member moved by a byte fails')

            # the build matrix
            matrix = [('g++', 'g++', [], ('c++11', 'c++14', 'c++17', 'c++20')),
                      ('m32', 'g++', ['-m32'], ('c++11', 'c++20')),
                      ('arm-none-eabi-g++', 'arm-none-eabi-g++', ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16',
                                                                  '-mfloat-abi=hard'], ('c++11', 'c++17')),
                      ('arm-none-eabi-g++', 'arm-none-eabi-g++', ['-mcpu=cortex-m0', '-mthumb'], ('c++11', 'c++17')),
                      ('avr-g++', 'avr-g++', ['-mmcu=atmega2560', '-I', os.path.join(lib, '..', 'tests', 'avr')],
                       ('c++11', 'c++17'))]
            builds = bad = 0
            for name, compiler, flags, stds in matrix:
                if name in skip:
                    print('     skipped (--skip): %s' % name)
                    continue
                if not shutil.which(compiler):
                    check(False, 'the build matrix: %s is there (or name it on --skip)' % name)
                    continue
                for std in stds:
                    for opt in ('-O0', '-Os', '-O2'):
                        builds += 1
                        r = subprocess.run([compiler, '-std=' + std, opt] + flags + ['-Wall', '-Wextra', '-Wpedantic', '-Werror',
                                           '-I', folder, '-I', lib, '-I', os.path.join(lib, 'guard'), '-c', source_path, '-o',
                                           os.devnull], capture_output=True, text=True)
                        if r.returncode:
                            bad += 1
                            print('     %s %s %s: %s' % (compiler, std, opt, r.stderr.strip()[:300]))
            check(builds and not bad, 'the generated table on the build matrix: %d builds, -Wall -Wextra -Wpedantic -Werror, '
                  '%d failed' % (builds, bad))

            # 7. library 1.1's table: the same vectors through the image on ranges
            r10, _ = export(cli, folder, MAP11, 'guard_test', 'table')
            r, table11 = export(cli, folder, MAP11, 'guard_test', 'table', ('--lib', '1.1'))
            check(r10.returncode == 1 and 'read-only but comes after' in r10.stderr and r.returncode == 0,
                  '--to table refuses the test map (STATUS_RO after a writable register), --lib 1.1 exports it (%s)'
                  % r.stderr.strip()[:200])
            text11 = open(table11, encoding='utf-8').read() if os.path.exists(table11) else ''
            check('D_RANGES = ranges' in text11 and 'D000 = ' not in text11 and 'keep_limits' not in text11
                  and set(entries(text11)) == set(got) and 'LOGIN: its entry' not in text11,
                  '[1.1] the device bank on ranges, no pointer table and no keep_limits; the Guard\'s entries are '
                  '--to guard\'s, the login\'s aside')
            with open(os.path.join(folder, 'program11.cpp'), 'w') as f:
                f.write(PROGRAM11)
            c, run = compile_run(cc, folder, lib, [os.path.join(folder, 'program11.cpp')], 'program11', ['-Wpedantic'])
            check(c.returncode == 0, '[1.1] the device bound by the table builds (-Wall -Wextra -Wpedantic -Werror) %s'
                  % c.stderr.strip()[:600])
            if run:
                for line in run.stdout.splitlines():
                    if line.startswith(('PASS', 'FAIL')):
                        check(line.startswith('PASS'), '[1.1 device] ' + line.split(' ', 1)[1])
                check(run.returncode == 0, '[1.1] the device program passed: every vector answered as on the device of 1.')
            # an entry off its member: the build fails and names the register
            moved = text11.replace('{ 0xD006u, 1u, EVRE_GUARD_U8,', '{ 0xD007u, 1u, EVRE_GUARD_U8,')
            moved_path = os.path.join(folder, 'moved_table.h')
            with open(moved_path, 'w', encoding='utf-8') as f:
                f.write(moved)
            with open(os.path.join(folder, 'moved.cpp'), 'w') as f:
                f.write('#include "moved_table.h"\n')
            r = subprocess.run([cc, '-std=c++11', '-I', folder, '-I', lib, '-I', os.path.join(lib, 'guard'), '-c',
                                os.path.join(folder, 'moved.cpp'), '-o', os.devnull], capture_output=True, text=True)
            check(moved != text11 and r.returncode != 0 and 'LEVEL: its entry is its member' in r.stderr,
                  '[1.1] an entry moved off its member (LEVEL at 0xD007): the build fails and names LEVEL')
            # the header on the build matrix
            with open(os.path.join(folder, 'uses11.cpp'), 'w') as f:
                f.write('#include "guard_test_table.h"\n')
            builds = bad = 0
            for name, compiler, flags, stds in matrix:
                if name in skip or not shutil.which(compiler):
                    continue
                for std in stds:
                    for opt in ('-O0', '-Os'):
                        builds += 1
                        r = subprocess.run([compiler, '-std=' + std, opt] + flags + ['-Wall', '-Wextra', '-Wpedantic', '-Werror',
                                           '-I', folder, '-I', lib, '-I', os.path.join(lib, 'guard'), '-c',
                                           os.path.join(folder, 'uses11.cpp'), '-o', os.devnull], capture_output=True, text=True)
                        if r.returncode:
                            bad += 1
                            print('     %s %s %s: %s' % (compiler, std, opt, r.stderr.strip()[:300]))
            check(builds and not bad, '[1.1] the table on the build matrix: %d builds, -Wall -Wextra -Wpedantic -Werror, '
                  '%d failed' % (builds, bad))

        # the export errors, each on its map
        def bad_map(**keys):
            reg = dict({'addr': '0xD010', 'name': 'X', 'type': 'u8', 'access': 'rw'}, **keys)
            return {'format': 'evre-map/1', 'device': 'Bad', 'registers': [reg]}
        errors = [
            ('a special that is not a whole raw value', bad_map(special={'0.5': 'half'}), 'not a whole raw value'),
            ('a special outside the type', bad_map(special={'300': 'big'}), 'not a whole raw value'),
            ('no raw value left between min and max', bad_map(min=1.2, max=1.8), 'no raw value is left'),
            ('an f32 special past the largest float', bad_map(type='f32', special={'1e39': 'huge'}), 'past the largest f32'),
            ('more than 255 listed values', bad_map(type='u16', special=dict((str(i), 's%d' % i) for i in range(256))),
             'more than 255'),
            ('a writable register outside the device bank', bad_map(addr='0xC000'), 'outside 0xD000'),
            ('two writable registers that overlap', {'format': 'evre-map/1', 'device': 'Bad', 'registers': [
                {'addr': '0xD010', 'name': 'A', 'type': 'u16', 'access': 'rw'},
                {'addr': '0xD011', 'name': 'B', 'type': 'u8', 'access': 'rw'}]}, 'overlaps'),
            ('no register a host writes', {'format': 'evre-map/1', 'device': 'Bad', 'registers': [
                {'addr': '0xD010', 'name': 'A', 'type': 'u8'}]}, 'no register a host writes'),
        ]
        for what, map_obj, text in errors:
            r, _ = export(cli, folder, map_obj, 'bad')
            check(r.returncode == 1 and text in r.stderr, 'export error: %s (%s)' % (what, r.stderr.strip().splitlines()[-1:]))

        # --check
        r, base = export(cli, folder, MAP, 'fresh')
        json_path = os.path.join(folder, 'fresh.json')
        def again(*extra, to='guard', out=None):
            return subprocess.run([cli, 'export', json_path, '--to', to, '-o', out or base] + list(extra),
                                  capture_output=True, text=True).returncode
        fresh = again('--check')
        with open(base + '.cpp', 'a') as f:
            f.write('/* changed */\n')
        stale = again('--check')
        os.remove(base + '.h')
        missing = again('--check')
        h_out = os.path.join(folder, 'fresh.h')
        subprocess.run([cli, 'export', json_path, '--to', 'h', '-o', h_out], capture_output=True)
        h_fresh = again('--check', to='h', out=h_out)
        with open(json_path, 'w', encoding='utf-8') as f:
            json.dump(dict(MAP, device='Guard test 2'), f)
        h_stale = again('--check', to='h', out=h_out)
        no_file = subprocess.run([cli, 'export', json_path, '--to', 'h', '--check'], capture_output=True).returncode
        check((fresh, stale, missing, h_fresh, h_stale, no_file) == (0, 1, 1, 0, 1, 2),
              '--check: 0 when fresh, 1 when a file differs or is missing (--to guard and --to h), 2 without -o (%s)'
              % [fresh, stale, missing, h_fresh, h_stale, no_file])

        # --to table, byte for byte
        golden = os.path.join(HERE, 'golden', 'example_device_table.h')
        out = os.path.join(folder, 'example_device_table.h')
        r = subprocess.run([cli, 'export', os.path.join(HERE, '..', 'maps', 'example_device.json'), '--to', 'table', '-o', out],
                           capture_output=True, text=True)
        same = r.returncode == 0 and os.path.exists(golden) and open(out, 'rb').read() == open(golden, 'rb').read()
        check(same, '--to table of maps/example_device.json equals tests/golden/example_device_table.h byte for byte')
        golden11 = os.path.join(HERE, 'golden', 'example_device_table_11.h')
        example = os.path.join(HERE, '..', 'maps', 'example_device.json')
        r = subprocess.run([cli, 'export', example, '--to', 'table', '--lib', '1.1', '-o', out], capture_output=True, text=True)
        same = r.returncode == 0 and os.path.exists(golden11) and open(out, 'rb').read() == open(golden11, 'rb').read()
        check(same, '--to table --lib 1.1 of maps/example_device.json equals tests/golden/example_device_table_11.h byte '
              'for byte')
        fresh = subprocess.run([cli, 'export', example, '--to', 'table', '--lib', '1.1', '-o', out, '--check'],
                               capture_output=True).returncode
        older = subprocess.run([cli, 'export', example, '--to', 'table', '-o', out, '--check'], capture_output=True).returncode
        misuse = [subprocess.run([cli, 'export', example] + extra, capture_output=True).returncode
                  for extra in (['--to', 'table', '--lib', '1.2'], ['--to', 'h', '--lib', '1.1'])]
        check((fresh, older, misuse) == (0, 1, [2, 2]),
              '--lib 1.1 with --check: 0 when fresh, 1 against the 1.0 table; --lib 1.2, or --lib with --to h: 2 (%s)'
              % [fresh, older, misuse])
    print('\n%d passed, %d failed' % (passed, failed))
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

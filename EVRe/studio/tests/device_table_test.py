#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The device table export, compiled with the EVRe device library and run.

    python device_table_test.py <build folder> [--lib DIR]

Writes a map into a temporary folder, exports it with <build>/evre as a device
table, compiles it with the library (lib/EVRe.cpp: --lib, else $EVRE_LIB, else
../../lib from here, as in the EVRe repository) and a test program with g++
(-Wall -Wextra -Werror for the generated header), and runs it: the images at
their addresses, the defaults, a read and a write through decodePacketInto(),
the read-only boundary, keep_limits(). Then maps the library cannot serve are
refused with the reason. Without the library or g++ the compile part says SKIP.
Exit code: 0 all passed, 1 a check failed, 2 a program is missing."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
passed = failed = 0

MAP = {
    "format": "evre-map/1", "device": "Table test", "device_id": "0x1234",
    "registers": [
        {"addr": "0xA000", "name": "DEVICE_ID", "type": "u16", "group": "Protocol"},
        {"addr": "0xD000", "name": "UPTIME", "type": "u32", "unit": "ms"},
        {"addr": "0xD004", "name": "TEMP", "type": "i16", "scale": 0.1, "offset": -40, "unit": "C"},
        {"addr": "0xD008", "name": "SERIAL", "type": "bytes", "size": 6},
        {"addr": "0xD00E", "name": "Default", "type": "u16", "default": 7},
        {"addr": "0xD010", "name": "SETPOINT", "type": "f32", "access": "rw", "min": -20, "max": 120, "default": 25,
         "special": {"-1": "off"}},
        {"addr": "0xD014", "name": "SPEED", "type": "u16", "access": "rw", "scale": 0.5, "min": 1, "max": 100,
         "default": 10},
        {"addr": "0xD016", "name": "GO", "type": "u8", "access": "rw", "write": "action"},
        {"addr": "0xD017", "name": "KEY", "type": "u8", "access": "wo"},
        {"addr": "0xD018", "name": "CTRL", "type": "u8", "access": "rw",
         "fields": [{"name": "MODE", "bits": "1:0"}, {"name": "BUSY", "bits": "4", "access": "ro"}]},
        {"addr": "0xD019", "name": "FLAGS", "type": "u8", "persist": True,
         "fields": [{"name": "OVER", "bits": "0", "access": "w1c"}]},
    ]}

PROGRAM = r'''
#include <cstdio>
#include <cstring>

#include "EVRe.h"
#include "table_test_table.h"

static table_test_ro_t ro;
static table_test_rw_t rw;
static base_t dev;
static int failed = 0;

static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) failed++;
}

/* a request as a host sends it, answered by the library */
static uint16_t ask(uint8_t fn, uint16_t off, uint16_t cnt, const void *data, uint16_t len, uint8_t *answer) {
	uint8_t req[300];
	uint16_t n = 0, got = 0;
	req[n++] = 0x7B;
	req[n++] = TABLE_TEST_SLAVE;
	req[n++] = fn;
	req[n++] = uint8_t(off);
	req[n++] = uint8_t(off >> 8);
	req[n++] = uint8_t(cnt);
	req[n++] = uint8_t(cnt >> 8);
	std::memcpy(req + n, data, len);
	n = uint16_t(n + len);
	const uint16_t crc = GetCrc16(req, n);
	req[n++] = uint8_t(crc);
	req[n++] = uint8_t(crc >> 8);
	req[n++] = 0x7D;
	decodePacketInto(&dev, req, n, answer, 300, &got);
	return got;
}

int main() {
	check(protocolInit(&dev) == NO_ERROR, "protocolInit");
	check(table_test_bind(&dev, &ro, &rw) == NO_ERROR, "bind");
	check(dev.DEVICE_ID == 0x1234 && dev.DEVICE_REG_WRITE_MIN == 0xD010 && dev.DEVICE_REG_READ_MAX == 0xD019,
			"DEVICE_ID, WRITE_MIN at the first writable register, READ_MAX at the last byte");
	check(rw.setpoint == 25.0f && rw.speed == 20 && ro.default_ == 7, "the defaults as start values, raw (SPEED 10 / 0.5)");
	check(sizeof ro == TABLE_TEST_RO_SIZE && TABLE_TEST_RO_SIZE == 16 && TABLE_TEST_RW_SIZE == 10, "the image sizes");

	ro.uptime = 0x11223344u;
	ro.temp = 655; /* 25.5 C */
	std::memcpy(ro.serial, "ABCDEF", 6);
	uint8_t a[300];
	uint16_t n = ask(READ, 0xD000, 16, nullptr, 0, a);
	const uint8_t want[16] = { 0x44, 0x33, 0x22, 0x11, 0x8F, 0x02, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 7, 0 };
	check(n == 26 && a[2] == READ_RESP && std::memcmp(a + 7, want, 16) == 0,
			"a read of the read-only image: every register at its address, little endian, the gap 0");

	table_test_rw_t seen = rw;
	uint8_t w[6];
	const float big = 500.0f;
	std::memcpy(w, &big, 4);
	w[4] = 100; /* SPEED 50 */
	w[5] = 0;
	n = ask(WRITE_ACK, 0xD010, 6, w, 6, a);
	check(n && a[2] == WRITE_ACK_RESP && rw.setpoint == 500.0f && rw.speed == 100, "a host write lands in rw");
	check(table_test_keep_limits(&rw, &seen) == 1 && rw.setpoint == 25.0f && rw.speed == 100,
			"keep_limits puts SETPOINT (past its max) back and keeps SPEED");
	rw.setpoint = -1.0f;
	rw.speed = 1; /* 0.5: below its min 1 */
	check(table_test_keep_limits(&rw, &seen) == 1 && rw.setpoint == -1.0f && rw.speed == 20,
			"a special value passes; a scaled limit is checked in raw (SPEED back to seen)");

	n = ask(WRITE_ACK, 0xD000, 4, w, 4, a);
	check(n && a[2] == ERROR_RESP && a[7] == PERMISSION_DENIED && ro.uptime == 0x11223344u,
			"a write below WRITE_MIN refused (PERMISSION_DENIED), the image unchanged");
	n = ask(READ, 0xD01A, 1, nullptr, 0, a);
	check(n && a[2] == ERROR_RESP && a[7] == REG_OFFSET_OUT_OF_RANGE, "a read past READ_MAX refused");
	n = ask(READ, 0xA000, 2, nullptr, 0, a);
	check(n == 12 && a[7] == 0x34 && a[8] == 0x12, "the protocol bank still the library's: DEVICE_ID");
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


def export(cli, folder, map_obj, name):
    path = os.path.join(folder, name + '.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(map_obj, f)
    out = os.path.join(folder, name + '_table.h')
    r = subprocess.run([cli, 'export', path, '--to', 'table', '-o', out], capture_output=True, text=True)
    return r, out


def library(opt):
    for folder in filter(None, [opt, os.environ.get('EVRE_LIB'), os.path.join(HERE, '..', '..', 'lib')]):
        if os.path.exists(os.path.join(folder, 'EVRe.h')) and os.path.exists(os.path.join(folder, 'EVRe.cpp')):
            return os.path.abspath(folder)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('--lib', help='the folder with EVRe.h and EVRe.cpp')
    opts = ap.parse_args()
    cli = os.path.join(opts.build, 'evre' + ('.exe' if os.name == 'nt' else ''))
    if not os.path.exists(cli):
        print('missing: %s' % cli)
        return 2
    with tempfile.TemporaryDirectory() as folder:
        r, header = export(cli, folder, MAP, 'table_test')
        check(r.returncode == 0 and os.path.exists(header), 'exported (%s)' % r.stderr.strip()[:200])
        text = open(header, encoding='utf-8').read() if os.path.exists(header) else ''
        check('default_ = 7' in text and 'uint8_t serial[6]' in text and '_gap_d006[2]' in text,
              'a keyword name, a bytes register, a gap')
        check('device_id' not in text and '0xA000' not in text, 'the protocol bank is left to the library')
        for word in ('GO', 'KEY', 'CTRL.BUSY', 'FLAGS.OVER', 'FLAGS'):
            check(word in text.split('#ifndef')[0], 'the comment names what the library leaves to the device: ' + word)

        lib = library(opts.lib)
        gpp = shutil.which('g++')
        if not lib or not gpp:
            print('SKIP compile and run: %s' % ('no g++' if lib else 'no EVRe library (--lib or EVRE_LIB)'))
        else:
            with open(os.path.join(folder, 'main.cpp'), 'w', encoding='utf-8') as f:
                f.write(PROGRAM)
            exe = os.path.join(folder, 'table_test' + ('.exe' if os.name == 'nt' else ''))
            steps = [[gpp, '-std=c++17', '-O1', '-c', os.path.join(lib, 'EVRe.cpp'), '-I', lib, '-o',
                      os.path.join(folder, 'evre.o')],
                     [gpp, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-c', os.path.join(folder, 'main.cpp'),
                      '-I', lib, '-I', folder, '-o', os.path.join(folder, 'main.o')],
                     [gpp, os.path.join(folder, 'main.o'), os.path.join(folder, 'evre.o'), '-o', exe]]
            for step in steps:
                c = subprocess.run(step, capture_output=True, text=True)
                if c.returncode:
                    break
            check(c.returncode == 0, 'compiled with the library, no warning (%s)' % (c.stderr.strip()[:600]))
            if c.returncode == 0:
                run = subprocess.run([exe], capture_output=True, text=True)
                for line in run.stdout.splitlines():
                    check(line.startswith('PASS'), 'device: ' + line.split(' ', 1)[-1])
                check(run.returncode == 0, 'the device program passed')

        bad = json.loads(json.dumps(MAP))
        bad['registers'].append({"addr": "0xD020", "name": "LATE", "type": "u8"})
        r, _ = export(cli, folder, bad, 'late')
        check(r.returncode == 1 and 'LATE (0xD020) is read-only but comes after SETPOINT (0xD010)' in r.stderr,
              'a read-only register above a writable one: refused, with both named')
        bad = json.loads(json.dumps(MAP))
        bad['registers'].append({"addr": "0xB000", "name": "ELSEWHERE", "type": "u8"})
        r, _ = export(cli, folder, bad, 'elsewhere')
        check(r.returncode == 1 and 'ELSEWHERE (0xB000): outside 0xD000..0xDFFF' in r.stderr,
              'a register outside the device bank: refused')
    print('%d passed, %d failed' % (passed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

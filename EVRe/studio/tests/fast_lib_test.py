#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The device's helper for Fast EVRe (lib/fast), compiled with the EVRe device library and run.

    python fast_lib_test.py <build folder> [--lib DIR]

Compiles lib/fast/evre_fast.cpp with the library (lib/EVRe.cpp: --lib, else $EVRE_LIB, else ../../lib from here)
and a test program, as C++11, 14, 17 and 20 at -O0, -O1, -O2, -O3 and -Os, the helper with
-Wall -Wextra -Wpedantic -Werror, and runs each: a window refused outside the device bank or too small, the frames
it builds, the numbers after evre_fast_lost(), the largest block, a block that does not fit (nothing written), the
wrap at 2^32. The helper takes no heap (nm) and little stack (-fstack-usage). Its frames then go through the
Studio's parser and the block's rules (<build>/evre_fast_test, helperFrames), and the example block of
PROTOCOL.md's "Fast EVRe" section is built and compared byte for byte with the one the document shows.
Without g++ or the library it says SKIP. Exit code: 0 all passed, 1 a check failed, 2 a program is missing."""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
PROTOCOL = os.path.join(HERE, '..', '..', 'docs', 'PROTOCOL.md')
passed = failed = 0

PROGRAM = r'''
#include <cstdio>
#include <cstring>

#include "EVRe.h"
#include "fast/evre_fast.h"

static int failed = 0;
static FILE *frames = nullptr; /* the frames, as hex on one line, then "first count flags" for each */
static char wanted[64][32];
static int built = 0;

static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
	if (!ok) failed++;
}

static uint32_t le32(const uint8_t *b) { return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }

/* n records at EVRE_FAST_BEFORE: record k is (first + k) & 0x7FFF, then 0x1234 */
static uint16_t block(evre_fast_t *s, uint8_t *buf, uint16_t n) {
	for (uint16_t k = 0; k < n; k++) {
		const uint16_t v = uint16_t((s->next + k) & 0x7FFFU);
		uint8_t *r = buf + EVRE_FAST_BEFORE + 4U * k;
		r[0] = uint8_t(v);
		r[1] = uint8_t(v >> 8);
		r[2] = 0x34;
		r[3] = 0x12;
	}
	const uint32_t first = s->next;
	const uint8_t flags = s->pending;
	const uint16_t len = evre_fast_frame(s, 1, buf, n);
	if (len) {
		for (uint16_t i = 0; i < len; i++) std::fprintf(frames, "%02X", buf[i]);
		std::snprintf(wanted[built++], sizeof wanted[0], "%u %u %u", unsigned(first), unsigned(n), unsigned(flags));
	}
	return len;
}

/* the frame is whole: start, slave, READ_RESP, the window, its count, the CRC over all before it, the end */
static bool whole(const uint8_t *buf, uint16_t len, uint16_t addr, uint16_t records) {
	const uint16_t count = uint16_t(8U + 4U * records);
	const uint16_t crc = GetCrc16(buf, len - 3);
	return len == 7 + count + 3 && buf[0] == 0x7B && buf[1] == 1 && buf[2] == READ_RESP && buf[3] == uint8_t(addr)
			&& buf[4] == uint8_t(addr >> 8) && buf[5] == uint8_t(count) && buf[6] == uint8_t(count >> 8)
			&& buf[len - 3] == uint8_t(crc) && buf[len - 2] == uint8_t(crc >> 8) && buf[len - 1] == 0x7D;
}

int main(int argc, char **argv) {
	frames = std::fopen(argc > 1 ? argv[1] : "frames.txt", "w");
	static uint8_t buf[1100];
	evre_fast_t s;
	std::memset(&s, 0xA5, sizeof s);

	/* refusals: no frame is built after one */
	const evre_fast_config_t below = { 0xCFFF, 64, 4 }, past = { 0xDF00, 0x101, 4 }, tiny = { 0xDC00, 11, 4 },
			none = { 0xDC00, 64, 0 }, edge = { 0xDF00, 0x100, 4 };
	check(evre_fast_init(&s, &below) == PERMISSION_DENIED, "a window below the device bank is refused");
	check(evre_fast_init(&s, &past) == PERMISSION_DENIED, "a window past 0xDFFF is refused");
	check(evre_fast_init(&s, &tiny) == PERMISSION_DENIED, "a window too small for the header and one record is refused");
	check(evre_fast_init(&s, &none) == PERMISSION_DENIED, "a record of 0 bytes is refused");
	check(evre_fast_init(&s, nullptr) == INSTANCE_IS_NULL && evre_fast_init(nullptr, &edge) == INSTANCE_IS_NULL,
			"a null pointer is refused");
	check(evre_fast_room(&s) == 0 && evre_fast_frame(&s, 1, buf, 1) == 0, "after a refusal no frame is built");
	check(evre_fast_init(&s, &edge) == NO_ERROR && evre_fast_room(&s) == 62, "a window up to 0xDFFF is taken");

	static const evre_fast_config_t cfg = { 0xDC00, 1024, 4 };
	check(evre_fast_init(&s, &cfg) == NO_ERROR, "the example window is taken");
	check(evre_fast_room(&s) == 254, "254 records of 4 bytes in 1024");
	evre_fast_start(&s);
	uint16_t len = block(&s, buf, 10);
	check(len == 58 && whole(buf, len, 0xDC00, 10), "10 records: a whole READ_RESP of 58 bytes");
	check(le32(buf + 7) == 0 && buf[11] == 10 && buf[12] == 0 && buf[13] == EVRE_FAST_FLAG_START && buf[14] == 0,
			"the first block: record 0, START");
	check(s.next == 10 && s.pending == 0, "the next record is 10, no flag pending");
	len = block(&s, buf, 254);
	check(len == 1034 && whole(buf, len, 0xDC00, 254) && buf[13] == 0, "the largest block: 254 records, 1034 bytes");
	std::memset(buf, 0xEE, sizeof buf);
	check(evre_fast_frame(&s, 1, buf, 255) == 0 && buf[0] == 0xEE && buf[14] == 0xEE && s.next == 264,
			"255 records do not fit: 0, nothing written, the numbers kept");
	check(evre_fast_frame(&s, 0, buf, 1) == 0, "no frame from slave 0");
	evre_fast_lost(&s, 100);
	check(s.next == 364 && (s.pending & EVRE_FAST_FLAG_LOST), "100 records dropped: they keep their numbers");
	len = block(&s, buf, 5);
	check(len && le32(buf + 7) == 364 && buf[13] == EVRE_FAST_FLAG_LOST, "the next block starts at 364 and says LOST");
	len = block(&s, buf, 0);
	check(len == 18 && whole(buf, len, 0xDC00, 0), "a block of no records: its header alone");
	s.next = 0xFFFFFFF0UL; /* just below the wrap */
	len = block(&s, buf, 32);
	check(len && le32(buf + 7) == 0xFFFFFFF0UL && s.next == 16, "the number wraps at 2^32");
	block(&s, buf, 3);
	evre_fast_start(&s);
	len = block(&s, buf, 3);
	check(len && le32(buf + 7) == 0 && buf[13] == EVRE_FAST_FLAG_START, "a start again: record 0, START");

	/* PROTOCOL.md's example: slave 1, window 0xDC00, the stream's first block, 3 records of I_LOAD and V_BUS */
	static uint8_t example[EVRE_FAST_BEFORE + 3 * 4 + EVRE_FAST_AFTER];
	const int16_t values[3][2] = { { 2400, 12010 }, { 2410, 12005 }, { 2398, 11998 } };
	for (int k = 0; k < 3; k++)
		for (int c = 0; c < 2; c++) {
			example[EVRE_FAST_BEFORE + 4 * k + 2 * c] = uint8_t(uint16_t(values[k][c]));
			example[EVRE_FAST_BEFORE + 4 * k + 2 * c + 1] = uint8_t(uint16_t(values[k][c]) >> 8);
		}
	evre_fast_t doc;
	evre_fast_init(&doc, &cfg);
	evre_fast_start(&doc);
	len = evre_fast_frame(&doc, 1, example, 3);
	std::printf("EXAMPLE");
	for (uint16_t i = 0; i < len; i++) std::printf(" %02X", example[i]);
	std::printf("\n");

	std::fprintf(frames, "\n");
	for (int i = 0; i < built; i++) std::fprintf(frames, "%s\n", wanted[i]);
	std::fclose(frames);
	std::printf("frames %d\n", built);
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
        if os.path.exists(os.path.join(folder, 'EVRe.h')) and os.path.exists(os.path.join(folder, 'fast', 'evre_fast.h')):
            return os.path.abspath(folder)
    return None


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def doc_example():
    """the bytes of the example block in PROTOCOL.md: the code block after "<!-- fast-example -->" """
    text = open(PROTOCOL, encoding='utf-8').read()
    m = re.search(r'<!-- fast-example -->\s*```[^\n]*\n(.*?)```', text, re.S)
    if not m:
        return None
    return ' '.join(re.findall(r'\b[0-9A-F]{2}\b', m.group(1).split('\n')[0]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('build')
    ap.add_argument('--lib', help='the folder with EVRe.h, EVRe.cpp and fast/')
    opts = ap.parse_args()
    exe_suffix = '.exe' if os.name == 'nt' else ''
    fast_test = os.path.join(opts.build, 'evre_fast_test' + exe_suffix)
    if not os.path.exists(fast_test):
        print('missing: %s' % fast_test)
        return 2
    lib = library(opts.lib)
    gpp = shutil.which('g++')
    if not lib or not gpp:
        print('SKIP: %s' % ('no g++' if lib else 'no EVRe library with fast/ (--lib or EVRE_LIB)'))
        return 0
    helper = os.path.join(lib, 'fast', 'evre_fast.cpp')
    strict = ['-Wall', '-Wextra', '-Wpedantic', '-Werror']
    with tempfile.TemporaryDirectory() as folder:
        main_cpp = os.path.join(folder, 'main.cpp')
        with open(main_cpp, 'w', encoding='utf-8') as f:
            f.write(PROGRAM)
        example = None
        frames = os.path.join(folder, 'frames.txt')
        for std in ('c++11', 'c++14', 'c++17', 'c++20'):
            for opt in ('-O0', '-O1', '-O2', '-O3', '-Os'):
                tag = '%s %s' % (std, opt)
                objs = [os.path.join(folder, n) for n in ('evre.o', 'fast.o', 'main.o')]
                exe = os.path.join(folder, 'fast_lib' + exe_suffix)
                steps = [[gpp, '-std=' + std, opt, '-c', os.path.join(lib, 'EVRe.cpp'), '-I', lib, '-o', objs[0]],
                         [gpp, '-std=' + std, opt] + strict + ['-fstack-usage', '-c', helper, '-I', lib, '-o', objs[1]],
                         [gpp, '-std=' + std, opt, '-Wall', '-Wextra', '-c', main_cpp, '-I', lib, '-o', objs[2]],
                         [gpp] + objs + ['-o', exe]]
                for step in steps:
                    c = run(step)
                    if c.returncode:
                        break
                check(c.returncode == 0, '%s: compiled, the helper with no warning %s' % (tag, c.stderr.strip()[:400]))
                if c.returncode:
                    continue
                r = run([exe, frames])
                bad = [line for line in r.stdout.splitlines() if line.startswith('FAIL')]
                check(r.returncode == 0 and not bad and 'frames 7' in r.stdout,
                      '%s: the device program passed %s' % (tag, ' / '.join(bad)))
                if std == 'c++17' and opt == '-O2':
                    for line in r.stdout.splitlines():
                        if line.startswith(('PASS', 'FAIL')):
                            print('     ' + line)
                        if line.startswith('EXAMPLE'):
                            example = line.split(' ', 1)[1]
                    # no heap: the helper calls nothing but the library's CRC
                    nm = shutil.which('nm')
                    if nm:
                        undefined = run([nm, '-u', '-C', objs[1]]).stdout
                        heap = [w for w in ('malloc', 'calloc', 'realloc', 'free', 'operator new', 'operator delete')
                                if w in undefined]
                        check(not heap, 'no heap: the helper calls %s' % ', '.join(
                            line.strip()[2:] for line in undefined.splitlines()) or 'nothing')
                    su = os.path.splitext(objs[1])[0] + '.su'
                    if not os.path.exists(su):
                        su = os.path.join(os.getcwd(), 'evre_fast.su')
                    if os.path.exists(su):
                        sizes = [int(line.split('\t')[1]) for line in open(su) if '\t' in line]
                        check(sizes and max(sizes) <= 64, 'its stack: at most %d bytes a call' % max(sizes or [0]))
        # the frames through the Studio's parser and the block's rules
        env = dict(os.environ, EVRE_FAST_FRAMES=frames)
        r = subprocess.run([fast_test, 'helperFrames'], capture_output=True, text=True, env=env)
        check(r.returncode == 0 and 'PASS   : FastTest::helperFrames()' in r.stdout,
              "the helper's frames through the Studio's parser and the block's rules %s" %
              (r.stdout[-600:] if r.returncode else ''))
        # PROTOCOL.md's example, built by the helper
        shown = doc_example()
        check(example is not None and shown == example,
              "PROTOCOL.md's example block is the helper's\n     doc:    %s\n     helper: %s" % (shown, example))
    print('%d passed, %d failed' % (passed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

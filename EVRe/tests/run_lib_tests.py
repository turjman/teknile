#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""The EVRe library 1.1 (B1 ranges, B2 handlers) and EVRe Guard, on a PC.

    python run_lib_tests.py [--old DIR] [--cc g++] [--fuzz DIR [--fuzz-cases N]]

1. The same 12000+ frames through eight builds of lib_test.cpp -DTRANSCRIPT:
   the 1.0 library (--old, default lib_1.0 beside this file) with its pointer table, 1.1
   (../lib) with the pointer table and with ranges, each also as a host's
   mirror and as a device that takes broadcasts into its bank
   (ACCEPT_BROADCAST_D000 1), and a mirror that takes them (its STATUS goes
   out as it holds it, D-25). The transcripts must be identical: every answer
   byte, every return code, the memory after every frame. That is the proof
   that existing devices see no change, bar the classes 1.1 changes on purpose
   (TRANSCRIPT_CLASSES: F1, F2, D-9, D-10, D-13, D-21, D-24), each checked for
   its new answer and counted.
2. lib_test.cpp -DFEATURES on 1.1: ranges, the handlers, EVRe Guard, the
   frames that tell apart two orders of the same steps, and the decisions
   (D-1 to D-25). Twice: as it is, and with the CRC table in RAM and the lock
   hooks (lock_hooks.h). Both see decodePacket's calloc (-Wl,--wrap=calloc).
3. configure_test.cpp: protocolInit with a device's own protocolConfigure
   (D-20). Run where an override of a weak function is reliable; on MinGW only
   compiled, and the suite says so.
4. api_compat.cpp: every public name, used as existing code uses it.
5. The library compiled with -Wall -Wextra at -O0, -O1, -O2, -Os and -O3, as
   C++11 and C++17: 1.1 may not add a warning.
6. EVRe Guard part 2, the register checks (GUARD_PLAN.md section 9): guard_desc_test.cpp, the decisions D-26 on and
   the wire table's rows, as it is, in the lock build, under the sanitizers (Linux) and against a device without part
   2; the transcript's frames through the check alone with a neutral table (GUARD_CLASSES); guard_fuzz.cpp, the
   oracle fuzz, 2 seeds x --guard-cases at -O1 and -O2; guard_mutants.py, every mutant caught; the library fuzz never
   returns 15; R3 and R4 (no heap, recursion, goto or cast of the data pointer; ASCII, LF); the build matrix (g++,
   -m32, arm-none-eabi-g++ for a Cortex-M7 and a Cortex-M0, avr-g++; C++11 to C++20; -O0 to -O3 and -Os; -Wall
   -Wextra -Wpedantic -Werror; --skip names a compiler not to use, a missing one fails the run); the stack and size
   on the Cortex-M7 at -Os, and the firmware's -Oz -flto; the docs checks (section 9, item 9): PROTOCOL.md's two
   wirings of the Guard compile as written, its error table is EVRe.h's codes, its "Cost" is what the Cortex-M7
   build measured.
7. With --fuzz DIR: fuzz_test.cpp built against the library in DIR and against
   ../lib; the two outputs must be identical, line for line, but for the
   operations of FUZZ_CLASSES, each checked; and in the ../lib run every frame
   decoded in place has the outcome of the same frame with two buffers, every
   handler sees the frame's RX_SLAVE_ID, a device's STATUS goes out with bit
   14 as ACCEPT_BROADCAST_D000 says and a mirror's as it holds it, and a
   broadcast into the device bank and a WRITE_ACK_RESP get the codes 1.1 gives
   them. DIR is the library before a change (the frozen pre-refactor copy for
   stage 2).
Exit code: 0 all passed, 1 a check failed, 2 no compiler."""
import argparse
import hashlib
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
NEW = os.path.normpath(os.path.join(HERE, '..', 'lib'))
GUARD = os.path.join(NEW, 'guard')
GUARD_SOURCES = [os.path.join(GUARD, 'evre_guard.cpp'), os.path.join(GUARD, 'evre_guard_desc.cpp')]
LIMIT_S = 600  # a program that runs longer hangs: a loop that cannot end is a failure too
passed = failed = 0


def check(ok, what):
    global passed, failed
    print('%s %s' % ('PASS' if ok else 'FAIL', what))
    sys.stdout.flush()
    if ok:
        passed += 1
    else:
        failed += 1


def build(cc, folder, name, lib, defines, extra=(), source='lib_test.cpp', flags=('-std=c++17', '-O1')):
    exe = os.path.join(folder, name + ('.exe' if os.name == 'nt' else ''))
    cmd = [cc] + list(flags) + ['-I', lib] + ['-D' + d for d in defines] + list(extra) + \
        [os.path.join(HERE, source), os.path.join(lib, 'EVRe.cpp'), '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:3000])
        return None
    return exe


def execute(exe, args=()):
    """(exit code, stdout); a program that does not finish within LIMIT_S fails with -1"""
    try:
        r = subprocess.run([exe] + list(args), capture_output=True, timeout=LIMIT_S)
        return r.returncode, r.stdout.decode('ascii', 'replace').replace('\r\n', '\n')
    except subprocess.TimeoutExpired:
        print('     %s: no end after %d s' % (os.path.basename(exe), LIMIT_S))
        return -1, ''


def parse(line):
    """'D EB E000 4 1 -> 0 10:7B01... mHASH cCONFIG qCNT:QUEUE rHASH sSTATUS' -> its fields; None for the other lines.
    m hashes everything a frame can change but STATUS (s); r all of that but CONFIG, MSG_CNT and the first 8 queue
    bytes (c, q). 'mem' and 'rest' are m and r with STATUS, so a check that compares them compares STATUS too."""
    parts = line.split()
    if len(parts) != 13 or parts[5] != '->':
        return None
    length, _, answer = parts[7].partition(':')
    count, _, queue = parts[10][1:].partition(':')
    return {'kind': parts[0], 'fn': int(parts[1], 16), 'off': int(parts[2], 16), 'cnt': int(parts[3]),
            'slave': int(parts[4]), 'ret': int(parts[6]), 'len': int(length), 'answer': answer,
            'mem': parts[8] + parts[12], 'mem_hash': parts[8], 'status': int(parts[12][1:], 16),
            'config': int(parts[9][1:], 16), 'msg_cnt': int(count),
            'queue': [int(queue[i:i + 2], 16) for i in range(0, len(queue), 2)], 'rest': parts[11] + parts[12],
            'rest_hash': parts[11]}


def crc16_x25(data):
    """the frame CRC (GetCrc16): CRC-16/X-25"""
    fcs = 0xFFFF
    for b in data:
        fcs ^= b
        for _ in range(8):
            fcs = (fcs >> 1) ^ 0x8408 if fcs & 1 else fcs >> 1
    return fcs ^ 0xFFFF


def refused(code, answered):
    """a refusal with `code` that changes nothing: an ERROR_RESP with it, or silence"""
    def ok(old, new, untouched):
        good = new['ret'] == code and (new['mem'] == untouched or new['kind'] == 'E')
        if answered:
            return good and new['len'] == 11 and new['answer'][4:6] == 'EE' and new['answer'][14:16] == '%02X' % code
        return good and new['len'] == 0
    return ok


def same_answer(old, new):
    return (old['ret'], old['len'], old['answer']) == (new['ret'], new['len'], new['answer'])


def heartbeat_left_alone(old, new, untouched):
    """D-10: a mirror's CONFIG is what the READ_RESP stored (bit 0 too), not forced to 1; the rest as 1.0"""
    if old['ret'] != 0:
        return new == old  # refused: as before
    if not same_answer(old, new) or new['rest'] != old['rest'] or new['queue'] != old['queue'] \
            or new['msg_cnt'] != old['msg_cnt']:
        return False
    at = 0xA004 - new['off']  # the frame's data byte i is (7 i + 3) & 0xFF, see lib_test.cpp
    bit0 = (((at * 7 + 3) & 0xFF) & 1) if 0 <= at < new['cnt'] else 0
    return new['config'] == (old['config'] & ~1) | bit0


def queue_zeroed(old, new, untouched):
    """D-13: after a clear or an ack the queue past MSG_CNT is 0; everything else as 1.0"""
    if old['ret'] != 0:
        return new == old  # refused: as before
    if not same_answer(old, new) or new['rest'] != old['rest'] or new['config'] != old['config']:
        return False
    cnt = new['msg_cnt']
    return cnt == old['msg_cnt'] and new['queue'] == [q if i < cnt else 0 for i, q in enumerate(old['queue'])]


def status_bit_sent(old, new, untouched):
    """D-24: STATUS went out with CAP_BROADCAST_D000 (bit 14) set, and STATUS holds it after: the answer as 1.0's
    but for that bit (and so the CRC), everything else as 1.0"""
    if old['ret'] != 0:
        return new == old  # refused: nothing sent, as before
    same = ('ret', 'len', 'mem_hash', 'rest_hash', 'config', 'msg_cnt', 'queue')
    if not all(new[k] == old[k] for k in same) or new['status'] != old['status'] | 0x4000:
        return False
    if old['kind'] == 'L':
        return new['answer'] == old['answer']  # lib_test.cpp prints an 'L' line's length from before the call: no bytes
    frame = bytearray.fromhex(old['answer'])
    frame[7 + 0xA003 - old['off']] |= 0x40  # STATUS's high byte, in the data of the READ_RESP
    crc = crc16_x25(frame[:-3])
    frame[-3], frame[-2] = crc & 0xFF, crc >> 8
    return new['answer'] == frame.hex().upper()


TRANSCRIPT_CLASSES = ('F1', 'F2', 'D-9', 'D-10', 'D-13', 'D-21', 'D-24')


def expected_change(f, mirror, broadcast_d000):
    """The frames 1.1 answers differently from 1.0 on purpose: (class, check), None for every other frame.
    D-19 (a write that clears the queue does the clear alone) needs an ack handler to show: the transcript has
    none, so its frames are D-13's and look the same; FEATURES and the fuzz check it."""
    bank = f['off'] & 0xF000
    unknown_bank = bank not in (0xA000, 0xD000)
    ours = f['slave'] == 1
    # D-24: STATUS goes out with CAP_BROADCAST_D000 exactly when the device takes broadcasts into its bank: a READ
    # answered, or the device's own READ_RESP from its registers, that holds STATUS's high byte. An 'L' READ
    # carries no data, so it is a valid READ of one byte more. With the setting 0 these frames stay as 1.0's
    # (STATUS never has the bit here): that is checked too. D-25: a mirror's STATUS is never refreshed, so on a
    # mirror these frames stay as 1.0's whatever the setting: checked too.
    count = f['cnt'] + 1 if f['kind'] == 'L' else f['cnt']
    if broadcast_d000 and not mirror and 0xA000 <= f['off'] <= 0xA003 < f['off'] + count \
            and ((f['kind'] in ('D', 'L') and f['fn'] == 0xAA and ours) or (f['kind'] == 'E' and f['fn'] == 0xAB)):
        return 'D-24', status_bit_sent
    # an 'L' frame declares one byte more than it carries: a real length error, except for a
    # function without data (WRITE_ACK_RESP), where it is a valid frame with a larger count
    if f['kind'] in ('D', 'M') or (f['kind'] == 'L' and f['fn'] == 0xEC):
        # D-9: a device refuses a WRITE_ACK_RESP; a mirror checks its bank only (a broadcast one was always 2)
        if f['fn'] == 0xEC and ours:
            return 'D-9', refused(3 if unknown_bank else 0, False) if mirror else refused(2, False)
        # F2: a write to an unknown bank is refused (a broadcast only takes WRITE; slave 2 is not ours)
        if f['fn'] in (0xEA, 0xEB) and unknown_bank and (ours or (f['slave'] == 0 and f['fn'] == 0xEA)):
            return 'F2', refused(3, ours)
        # D-21: a broadcast reaches the reserved bank only, unless the device takes one into its bank
        if f['fn'] == 0xEA and f['slave'] == 0 and bank == 0xD000 and not broadcast_d000:
            return 'D-21', refused(3, False)
        # F1: a device takes no READ_RESP (a mirror still does)
        if f['fn'] == 0xAB and ours and not mirror:
            return 'F1', refused(2, False)
        # D-10: a mirror's stored READ_RESP sets no HEARTBEAT
        if f['fn'] == 0xAB and ours and mirror and f['kind'] != 'L':
            return 'D-10', heartbeat_left_alone
        # D-13: a write that clears the queue or acknowledges a message zeroes the queue past MSG_CNT
        if f['fn'] in (0xEA, 0xEB) and bank == 0xA000 and (ours or (f['slave'] == 0 and f['fn'] == 0xEA)) \
                and f['off'] + f['cnt'] > 0xA006 and f['kind'] != 'L':
            return 'D-13', queue_zeroed
    if f['kind'] == 'E' and f['fn'] in (0xEA, 0xEB) and unknown_bank:
        return 'F2', refused(3, False)  # the encoder builds no write to an unknown bank
    return None


def compare(base, other, name, mirror, broadcast_d000):
    """Every line as 1.0's, except the frames of TRANSCRIPT_CLASSES, each checked for its new answer."""
    a, b = base.splitlines(), other.splitlines()
    if len(a) != len(b):
        check(False, '%s: %d lines, 1.0 %d' % (name, len(b), len(a)))
        return
    untouched = next(p['mem'] for p in map(parse, a) if p and p['ret'] == 7)  # a frame for another slave
    covered = dict((c, 0) for c in TRANSCRIPT_CLASSES)
    changed = dict((c, 0) for c in TRANSCRIPT_CLASSES)
    bad = []
    for x, y in zip(a, b):
        fx, fy = parse(x), parse(y)
        why = expected_change(fx, mirror, broadcast_d000) if fx else None
        if why is None:
            if x != y:
                bad.append((x, y))
            continue
        finding, ok = why
        covered[finding] += 1
        if x != y:
            changed[finding] += 1
        if not ok(fx, fy, untouched):
            bad.append((x, y))
    for x, y in bad[:3]:
        print('     1.0: %s\n     1.1: %s' % (x[:180], y[:180]))
    print('     %s: %s' % (name, ', '.join('%s %d frames (%d changed)' % (c, covered[c], changed[c])
                                          for c in TRANSCRIPT_CLASSES if covered[c])))
    check(not bad, '%s: as 1.0 on every frame, except %s, each checked for its new answer'
          % (name, ', '.join('%d %s' % (changed[c], c) for c in TRANSCRIPT_CLASSES if changed[c])))


# EVRe Guard part 2 changes what a device answers, on purpose, in these classes (D-numbers of REVIEW.md). Each is
# worked out from a frame's inputs alone, by guard_verdict() below (the transcript) or by guard_fuzz.cpp's oracle
# (the fuzz), and each changed line is checked for its new answer: the code, answered or silent, the echoed offset
# and count, the memory as it was (nothing stored, no HEARTBEAT).
GUARD_CLASSES = (('G-VALUE', 'D-26', 'a value the register does not take: 15, nothing stored'),
                 ('G-PART', 'D-30', 'only part of a number register: 3, nothing stored'),
                 ('G-UNCOVERED', 'D-31', 'a byte no entry covers: 3, nothing stored'),
                 ('G-SETUP', 'D-38', 'no good table: 3 for the device bank, nothing stored'),
                 ('G-BCAST', 'D-40', 'a broadcast refused: silent, nothing stored'),
                 ('G-MIRROR', 'D-39', 'a mirror: every write refused, 3, nothing stored'))
TRANSCRIPT_CLASSES = TRANSCRIPT_CLASSES + tuple(name for name, _, _ in GUARD_CLASSES)

# lib_test.cpp's neutral table (-DGUARD_CHECK), written again here from its description, not from the engine's code:
# (address, size, type, min, max, listed values); a bytes register has no limits. 0xD0F2..0xD0F3 is a gap.
FLT_MAX = 3.4028234663852886e38
GUARD_TABLE = ((0xD0DC, 1, 'u8', 10, 0xF0, (0,)), (0xD0DD, 1, 'i8', -100, 100, ()), (0xD0DE, 2, 'u16', 0, 0x7FFF, ()),
               (0xD0E0, 2, 'i16', -1000, 10000, ()), (0xD0E2, 4, 'u32', 0, 0xFFFFFFFF, ()),
               (0xD0E6, 4, 'f32', -FLT_MAX, FLT_MAX, ()), (0xD0EA, 8, 'bytes', None, None, ()),
               (0xD0F4, 2, 'u16', 0, 0xFFFF, ()), (0xD0F6, 4, 'i32', -2 ** 31, 2 ** 31 - 1, ()),
               (0xD0FA, 4, 'f32', -1000.0, 1000.0, ()), (0xD0FE, 1, 'u8', 0, 0xFF, ()))


def guard_value_ok(kind, size, low, high, listed, raw):
    """a whole register's bytes, decoded as a host would, compared in plain numbers"""
    if kind == 'f32':
        value = struct.unpack('<f', raw)[0]
        if math.isnan(value) or math.isinf(value):
            return False
    elif kind.startswith('i'):
        value = int.from_bytes(raw, 'little', signed=True)
    else:
        value = int.from_bytes(raw, 'little')
    return value in listed or low <= value <= high


def guard_verdict(off, cnt, data, mirror, bad_table):
    """(code, class) the register checks refuse a write with, None when it passes"""
    if mirror:
        return 3, 'G-MIRROR'
    if cnt == 0 or (off >= 0xA000 and off + cnt <= 0xA106):
        return None
    if bad_table:
        return 3, 'G-SETUP'
    if off < 0xD000 or off + cnt > 0xE000:
        return 3, 'G-UNCOVERED'
    owner = {}
    for reg in GUARD_TABLE:
        for at in range(reg[0], reg[0] + reg[1]):
            owner[at] = reg
    at = off
    while at < off + cnt:
        reg = owner.get(at)
        if reg is None:
            return 3, 'G-UNCOVERED'
        start, size, kind, low, high, listed = reg
        if kind != 'bytes':
            if start < off or start + size > off + cnt:
                return 3, 'G-PART'
            if not guard_value_ok(kind, size, low, high, listed, bytes(data[start - off:start - off + size])):
                return 15, 'G-VALUE'
        at = start + size
    return None


def guard_compare(base, other, name, mirror, bad_table):
    """The build with the register checks against the same build without a handler: every line the same, but for the
    frames the checks refuse (guard_verdict on the frame's own bytes), each refused exactly as its class says."""
    a, b = base.splitlines(), other.splitlines()
    if len(a) != len(b):
        check(False, '%s: %d lines, without the checks %d' % (name, len(b), len(a)))
        return
    untouched = next(p['mem'] for p in map(parse, a) if p and p['ret'] == 7)
    data = [(i * 7 + 3) & 0xFF for i in range(0x6100)]
    counts = dict((c, 0) for c, _, _ in GUARD_CLASSES)
    bad = []
    queue_kept = False  # an ack ('M') refused: the queue's line after it shows the queue as it was
    for x, y in zip(a, b):
        if x.startswith('MSG') and queue_kept:
            if y != 'MSG 3 5 6':
                bad.append((x, y + '  <- the queue changed by a refused ack'))
            continue
        fx, fy = parse(x), parse(y)
        asked = fx is not None and fx['ret'] == 0 and fx['kind'] in ('D', 'M') and fx['slave'] in (0, 1) \
            and (fx['fn'] in (0xEA, 0xEB) or (mirror and fx['fn'] == 0xAB))
        verdict = None
        if asked:
            frame = [0, 1] if fx['kind'] == 'M' else data[:fx['cnt']]
            verdict = guard_verdict(fx['off'], fx['cnt'], frame, mirror, bad_table)
        if verdict is None:
            if x != y:
                bad.append((x, y))
            continue
        code, finding = verdict
        # an 'M' line's length is printed from before the call (lib_test.cpp), so it shows no answer either way
        silent = fx['slave'] == 0 or fx['fn'] == 0xAB or fx['kind'] == 'M'
        if fx['slave'] == 0:
            finding = 'G-BCAST'
        ok = fy['ret'] == code and fy['mem'] == untouched and (
            fy['len'] == 0 if silent else
            fy['len'] == 11 and fy['answer'][4:6] == 'EE' and fy['answer'][6:14] == fx['answer'][6:14] if fx['len'] == 11
            else fy['len'] == 11 and fy['answer'][4:6] == 'EE' and fy['answer'][14:16] == '%02X' % code)
        if not ok:
            bad.append((x, y + '  <- not refused as %s says' % finding))
            continue
        counts[finding] += 1
        queue_kept = fx['kind'] == 'M'
    for x, y in bad[:3]:
        print('     without: %s\n     with:    %s' % (x[:180], y[:200]))
    print('     %s: %s' % (name, ', '.join('%s %d frames' % (c, counts[c]) for c, _, _ in GUARD_CLASSES if counts[c])))
    check(not bad, '%s: as the build without a handler on every frame, except %s, each refused as its class says'
          % (name, ', '.join('%d %s' % (counts[c], c) for c, _, _ in GUARD_CLASSES if counts[c]) or 'none'))
    return counts


def warnings(cc, lib, folder, flags=('-std=c++17', '-O1')):
    r = subprocess.run([cc] + list(flags) + ['-Wall', '-Wextra', '-c', os.path.join(lib, 'EVRe.cpp'), '-I', lib,
                        '-o', os.path.join(folder, 'w.o')], capture_output=True, text=True)
    return [line for line in r.stderr.splitlines() if 'warning:' in line or 'error:' in line]


# GCC finds some warnings only with more optimisation (-Wstringop-overflow at -O2), others only without
WARNING_BUILDS = [('-std=%s' % std, opt) for std in ('c++11', 'c++17') for opt in ('-O0', '-O1', '-O2', '-Os', '-O3')] + \
    [('-std=c++17', '-O2', '-DEVRE_CRC_TABLE_RUNTIME=1')]


def api_compat(cc, folder):
    """api_compat.cpp built, and run, in each way a device or a host includes the header"""
    variants = [('C++11', ['-std=c++11']), ('C++17', ['-std=c++17']),
                ('the CRC table in RAM', ['-std=c++17', '-DEVRE_CRC_TABLE_RUNTIME=1'])]
    if os.name == 'nt':
        variants.append(('<windows.h> first', ['-std=c++17', '-DWITH_WINDOWS_H']))
    for what, flags in variants:
        exe = build(cc, folder, 'api_compat', NEW, [], ['-I', GUARD] + GUARD_SOURCES, source='api_compat.cpp',
                    flags=flags + ['-O2', '-Wall', '-Wextra', '-Werror'])
        check(exe is not None, 'api_compat: every public name compiles, %s, -Wall -Wextra -Werror' % what)
        if exe:
            code, out = execute(exe)
            bad = [line for line in out.splitlines() if line.startswith('FAIL')]
            for line in bad[:5]:
                print('     ' + line)
            check(code == 0 and not bad and out.strip().endswith('0 failed'), 'api_compat: runs, %s' % what)
    r = subprocess.run([cc, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wmissing-declarations', '-DDEFINE_CONFIGURE',
                        '-I', NEW, '-I', GUARD, '-c', os.path.join(HERE, 'api_compat.cpp'), '-o', os.path.join(folder, 'cfg.o')],
                       capture_output=True, text=True)
    check(r.returncode == 0, "api_compat: a device's protocolConfigure() is the header's function %s" % r.stderr.strip()[:300])


def configure_override(cc, folder):
    """configure_test.cpp: protocolInit with the device's own protocolConfigure (D-20). An override of a weak
    function is not reliable on MinGW: there the program is only compiled, and the suite says so."""
    machine = subprocess.run([cc, '-dumpmachine'], capture_output=True, text=True).stdout.strip()
    flags = ['-std=c++11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wmissing-declarations']
    if 'mingw' in machine or os.name == 'nt':
        r = subprocess.run([cc] + flags + ['-I', NEW, '-c', os.path.join(HERE, 'configure_test.cpp'),
                            '-o', os.path.join(folder, 'configure.o')], capture_output=True, text=True)
        check(r.returncode == 0, "configure_test: compiles (-Wall -Wextra -Werror) %s" % r.stderr.strip()[:300])
        print("     configure_test: not run on %s, where an override of the weak protocolConfigure is not reliable; "
              "the Linux run checks D-20" % (machine or os.name))
        return
    exe = build(cc, folder, 'configure_test', NEW, [], source='configure_test.cpp', flags=flags)
    check(exe is not None, "built: configure_test (a device's own protocolConfigure, -Wall -Wextra -Werror)")
    if exe:
        code, out = execute(exe)
        for line in out.splitlines():
            if line.startswith(('PASS', 'FAIL')):
                check(line.startswith('PASS'), '[configure] ' + line.split(' ', 1)[1])
        check(code == 0, 'the configure program passed')


FNV_START = '811C9DC5'  # the handlers' hash when none was asked
# The changes 1.1 makes on purpose to what the library did before (the decisions D-n that the FEATURES checks of
# lib_test.cpp are named after), in the order a line is attributed to them. fuzz_test.cpp tags
# each operation from its inputs; each class here checks the new line. o, n: the old and the new line, split:
# a/d/e/f: [case.op, kind, ret, len, hash, memory, handlers, bMEMORY_BEFORE, lLOOSE, iIN_PLACE, sAPART, xCLEAR,
# kSETTING, eERROR, hHANDLERS, tTAGS]; p: [.., p, ret, STATUS, A000 null, slots, rRANGES, tTAGS]. t: the tags. LOOSE
# is the memory but for HEARTBEAT and the queue past MSG_CNT; IN_PLACE and APART the outcome of a frame decoded in
# place and of the same frame with an answer buffer of its own; CLEAR (x=, x!, x-) whether a write that clears the
# queue and runs on into the slots did what the frame cut after MSG_CNT does; SETTING (D24 and D25 only, else k-): on
# a device (D24) the outcome of the same call on a STATUS whose bit 14 is the case's ACCEPT_BROADCAST_D000 already,
# ret/len/hash/memory/handlers/loose; on a mirror (D25) k= when the call left STATUS as it was and any READ_RESP it
# built carries STATUS as it was, k! when not; ERROR (e1, e0, e-) whether an answer of 11 bytes is the request's ERROR_RESP
# with the return code in it; HANDLERS the handlers' hash without the queue bytes the ack handlers see
# (fuzz_test.cpp).


def _untouched(n):
    return n[5] == n[7][1:] and n[6] == FNV_START  # memory as before the call, no handler asked


def _same_answer(o, n):
    return o[2:5] == n[2:5]  # return code, length, answer


def _error_len(t):
    return '11' if 'room' in t else '0'  # an ERROR_RESP, when it fits


def _refused_as(n, t):
    """refused with n's return code: an ERROR_RESP that carries it and echoes the request, when one fits"""
    return n[3] == _error_len(t) and (n[3] != '11' or n[13] == 'e1')


def _as_apart(o, n):
    """in place exactly as with two buffers in 1.1, and with two buffers as before"""
    return n[9] != 'i-' and n[9][1:] == n[10][1:] and o[10] == n[10]


def _outcome(n):
    """what the k field of a line records, in its order: ret, len, hash, memory, handlers, loose"""
    return [n[2], n[3], n[4], n[5], n[6], n[8][1:]]


def _setting(line):
    """the k field of a line, as _outcome gives it; None for k-"""
    k = line[12][1:].split('/')
    return k if len(k) == 6 else None


def _with_setting(o):
    """D-24: the old line as the same call on a STATUS whose bit 14 was the setting already would have made it"""
    k = _setting(o)
    if k is None:
        return o
    r = list(o)
    r[2:7], r[8] = k[0:5], 'l' + k[5]
    return r


FUZZ_CLASSES = [
    ('D3', 'outLen 0 on an early refusal',
     lambda o, n, m, t: o[2] == n[2] and o[3] == '48879' and n[3] == '0' and o[5:7] == n[5:7]),
    ('D11', 'a wrong start or end byte: silent, nothing done',
     lambda o, n, m, t: n[2] == '1' and n[3] == '0' and _untouched(n)),
    ('D12', 'an unknown code, any length: code 2, answered',
     lambda o, n, m, t: n[2] == '2' and _refused_as(n, t) and _untouched(n)),
    ('D12s', 'an ERROR_RESP, any length: code 2, silent', lambda o, n, m, t: n[2] == '2' and n[3] == '0' and _untouched(n)),
    ('D14', 'the encoder builds no broadcast but a WRITE', lambda o, n, m, t: n[2] == '2' and n[3] == '0' and _untouched(n)),
    ('D9', 'a device refuses a WRITE_ACK_RESP: code 2, silent', lambda o, n, m, t: n[2] == '2' and n[3] == '0' and _untouched(n)),
    ('D9m', "a mirror takes a WRITE_ACK_RESP for 0xA000 or 0xD000, whatever its own limits",
     lambda o, n, m, t: n[2] == '0' and n[3] == '0' and _untouched(n)),
    ('D9u', 'a mirror refuses a WRITE_ACK_RESP for another bank: code 3, silent',
     lambda o, n, m, t: n[2] == '3' and n[3] == '0' and _untouched(n)),
    ('D2room', 'the room before the handler: BUFFER_TOO_SMALL, answered if it fits, not asked',
     lambda o, n, m, t: n[2] == '11' and _refused_as(n, t) and _untouched(n)),
    ('D8', "a handler's 1 or 7 answered with an ERROR_RESP; EVRE_HANDLED from a read handler answers",
     lambda o, n, m, t: (o[2] == n[2] and o[5:7] == n[5:7] and _refused_as(n, t))
     or (o[2] == '255' and n[2] == '0' and o[6] == n[6] and o[8] == n[8] and int(n[3]) >= 10)),
    ('D2place', 'a WRITE_ACK in place: the same answer, and the outcome of two buffers', lambda o, n, m, t: _same_answer(o, n)
     and _as_apart(o, n)),
    ('D10', 'no HEARTBEAT for a response: the same answer, the handlers and the rest of the memory',
     lambda o, n, m, t: _same_answer(o, n) and o[6] == n[6] and o[8] == n[8]),
    ('D13', 'the queue zeroed past MSG_CNT: the same answer, the rest of the memory, the same handlers in the same order',
     lambda o, n, m, t: _same_answer(o, n) and o[8] == n[8] and o[14] == n[14]),
    ('D19', 'a clear and slot bytes in one write: what the frame cut after MSG_CNT does (x=), the same answer',
     lambda o, n, m, t: _same_answer(o, n) and n[11] == 'x='),
    ('D21', 'a broadcast into the device bank of a device that takes none: code 3, silent, nothing done',
     lambda o, n, m, t: n[2] == '3' and n[3] == '0' and _untouched(n)),
    ('D24', "STATUS sent with bit 14 as the setting: the old library's outcome on a STATUS whose bit 14 is the setting "
     "already (its k), the two libraries agree on that (the same k), all else the same",
     lambda o, n, m, t: n[2] == '0' and _setting(o) is not None and _outcome(n) == _setting(o) and n[12] == o[12]
     and o[:2] == n[:2] and o[7] == n[7] and o[11] == n[11] and (o[9] == 'i-') == (n[9] == 'i-')),
    ('D17', 'a refused range table: code 14, not 13, and emptied (D7)',
     lambda o, n, m, t: o[1] == 'p' and o[2] == '13' and n[2] == '14' and n[6] == 'r0' and o[3:6] == n[3:6]),
]


FUZZ_CHECKS = dict((name, ok) for name, _, ok in FUZZ_CLASSES)
# Tags fuzz_test.cpp gives exactly: every line of the new run that carries one must pass its check, the same as
# before or not (D24, D25 and D19 have checks of their own in fuzz_compare). D25 is no class: a mirror's STATUS is
# left alone, as the library before the refactor left it, so a D25 line differs only through another class.
EXACT = ('D21', 'D9', 'D9m', 'D9u')


def fuzz_compare(base, other):
    """The lines of two fuzz runs: equal, or the first difference in a case is an operation that carries a class
    whose check the new line passes. After a difference that changed the device's memory the rest of that case
    follows from it (counted as 'then'); otherwise the next lines must be equal again. And on every line of the new
    run, a frame decoded in place has the outcome of the same frame with two buffers (D-2), a write that clears the
    queue and runs on into the slots does what the clear alone does (D-19: never x!), a broadcast into the bank
    of a device that takes none is refused (D-21), a WRITE_ACK_RESP gets its code (D-9: 2 from a device, 0 or 3 from
    a mirror), an operation of a device that sent STATUS has the outcome of the same call
    on a STATUS whose bit 14 is the setting already (D-24: its own k), and one of a mirror left STATUS alone (D-25:
    k=): a line the same as before cannot hide the old behaviour. A D24 operation that succeeded in 1.1 may carry
    another class too (D8: EVRE_HANDLED from a read handler): that class is checked against the old line as its k
    says it would have been.
    (counts, bad lines)"""
    counts = dict((name, 0) for name, _, _ in FUZZ_CLASSES)
    counts['then'] = 0
    bad = []
    if len(base) != len(other):
        return counts, [('%d lines' % len(base), '%d lines' % len(other))]
    case, diverged, differed, mirror = None, False, False, False
    for x, y in zip(base, other):
        o, n = x.split(), y.split()
        here = o[0].split('.')[0]
        if here != case:
            case, diverged, differed = here, False, False
        if o[1] == 'layout':
            mirror = o[-1] == '1'
        if '.' in n[0] and len(n) > 2 and n[2] == '15':
            bad.append((x, y + '  <- 15 from the library: only a handler may return it (D-27)'))
            continue
        if n[1] == 'd' and n[9] != 'i-' and n[9][1:] != n[10][1:]:
            bad.append((x, y + '  <- in place, not as with two buffers'))
            continue
        if n[1] in ('a', 'd') and n[11] == 'x!':
            bad.append((x, y + '  <- more than the clear alone (D-19)'))
            continue
        exact = [name for name in EXACT if name in n[-1][1:].split(',')] if n[1] in ('a', 'd') else []
        wrong = [name for name in exact if not FUZZ_CHECKS[name](o, n, mirror, [])]
        if wrong:
            bad.append((x, y + '  <- not what %s says, on a line it tags exactly' % ', '.join(wrong)))
            continue
        if n[1] in ('a', 'd', 'e', 'f') and 'D24' in n[-1][1:].split(',') and n[2] == '0' \
                and _outcome(n) != _setting(n):
            bad.append((x, y + '  <- STATUS sent with bit 14 not the setting (D-24)'))
            continue
        if n[1] in ('a', 'd', 'e', 'f') and 'D25' in n[-1][1:].split(',') and n[12] != 'k=':
            bad.append((x, y + "  <- a mirror's STATUS refreshed (D-25)"))
            continue
        if x == y:
            continue
        if diverged or (o[1] == 'buffers' and differed):
            counts['then'] += 1
            continue
        tags = o[-1][1:].split(',') if o[-1].startswith('t') and o[1] not in ('layout', 'buffers') else []
        if n[-1] != o[-1]:
            tags = []  # the same inputs must give the same tags
        # STATUS went out (D24, succeeded): the other classes judge the old line as it would have been with it
        base = _with_setting(o) if 'D24' in tags and n[2] == '0' else o
        passed = [name for name, _, ok in FUZZ_CLASSES if name in tags and ok(o if name == 'D24' else base, n, mirror, tags)]
        if not passed:
            bad.append((x, y))
            continue
        counts[passed[0]] += 1
        differed = True
        diverged = o[1] != 'p' and o[5] != n[5]
    return counts, bad


def fuzz(cc, folder, before, cases):
    """fuzz_test.cpp through the library in `before` and through ../lib: the same lines, but for the classes of
    FUZZ_CLASSES, each checked"""
    outputs = {}
    for name, lib, opt in (('before', before, '-O1'), ('after', NEW, '-O1'), ('after -O2', NEW, '-O2')):
        exe = build(cc, folder, 'fuzz_' + name.replace(' ', '').replace('-', ''), lib, [], source='fuzz_test.cpp',
                    flags=['-std=c++17', opt])
        check(exe is not None, 'fuzz: built %s (%s)' % (name, lib))
        if not exe:
            return
        for seed in ('1', '2'):
            code, out = execute(exe, [str(cases), seed])
            check(code == 0, 'fuzz: ran %s, seed %s%s' % (name, seed, ' (exit 3: a handler saw a wrong RX_SLAVE_ID)'
                                                           if code == 3 else ''))
            outputs[(name, seed)] = out
    for seed in ('1', '2'):
        base = outputs[('before', seed)].splitlines()
        ops = sum(1 for line in base if '.' in line.split(' ')[0])
        for name in ('after', 'after -O2'):
            other = outputs[(name, seed)].splitlines()
            counts, bad = fuzz_compare(base, other)
            for x, y in bad[:3]:
                print('     before: %s\n     %s: %s' % (x, name, y))
            changed = sum(counts.values())
            mirrored = sum(1 for line in other if 'D25' in (line.split() or [''])[-1][1:].split(','))
            print('     %s, seed %s: %d lines differ: %s; %d D25 lines (a mirror sent STATUS), each checked' % (
                name, seed, changed, ', '.join('%s %d' % (k, v) for k, v in counts.items() if v), mirrored))
            check(not bad, 'fuzz: %s cases, %d operations, seed %s: %s is the same as before, line for line, but for '
                  'the decided classes, each checked (%d lines outside them)' % (cases, ops, seed, name, len(bad)))


def guard_transcripts(cc, folder, transcripts):
    """The 1.0 transcript's frames through the check alone (the firmware's wiring) with a neutral table, against the
    same 1.1 build without a handler: as a device, taking broadcasts into its bank, as a mirror, and with a table
    init refuses. Every line that differs falls in a class of GUARD_CLASSES; every class shows up."""
    seen = dict((c, 0) for c, _, _ in GUARD_CLASSES)
    for name, twin, defines, mirror, bad_table in (
            ('1.1 ranges, register checks', '1.1 ranges', [], False, False),
            ('1.1 ranges, broadcast into 0xD000, register checks', '1.1 ranges, broadcast into 0xD000', ['BROADCAST_D000'],
             False, False),
            ('1.1 ranges, mirror, register checks', '1.1 ranges, mirror', ['MIRROR'], True, False),
            ('1.1 ranges, register checks, a bad table', '1.1 ranges', ['GUARD_BAD_TABLE'], False, True)):
        exe = build(cc, folder, name.replace(' ', '_').replace('.', '').replace(',', ''), NEW,
                    ['TRANSCRIPT', 'USE_RANGES', 'GUARD_CHECK'] + defines, ['-I', GUARD] + GUARD_SOURCES)
        check(exe is not None, 'built: %s' % name)
        if not exe or twin not in transcripts:
            continue
        code, out = execute(exe)
        check(code == 0, 'ran: %s' % name)
        counts = guard_compare(transcripts[twin], out, name, mirror, bad_table)
        for c in counts or {}:
            seen[c] += counts[c]
    missing = [c for c, _, _ in GUARD_CLASSES if not seen[c]]
    check(not missing, 'the transcript with the register checks shows every class of GUARD_CLASSES%s'
          % (': not ' + ', '.join(missing) if missing else ''))


def register_checks(cc, folder):
    """EVRe Guard part 2 (guard_desc_test.cpp): the decisions D-26 on and the wire table's rows, as it is and in the
    lock build; then the same checks against a device without part 2 (-DNO_CHECK), where the checks of each decision
    that changes an answer must fail; and part 2 compiled with -Wall -Wextra -Wpedantic -Werror, C++11 and C++17."""
    guard = ['-I', GUARD, '-Wall', '-Wextra'] + GUARD_SOURCES
    for exe_name, label, extra in (('guard_desc', '', []),
                                   ('guard_desc_lock', 'lock hooks', ['-include', os.path.join(HERE, 'lock_hooks.h')])):
        what = 'register checks' + (', %s' % label if label else '')
        exe = build(cc, folder, exe_name, NEW, [], guard + extra, source='guard_desc_test.cpp')
        check(exe is not None, 'built: %s (1.1 + EVRe Guard parts 1 and 2, -Wall -Wextra)' % what)
        if exe:
            code, out = execute(exe)
            for line in out.splitlines():
                if line.startswith(('PASS', 'FAIL')):
                    check(line.startswith('PASS'), ('[%s] ' % label if label else '') + line.split(' ', 1)[1])
            check(code == 0, 'the %s program passed' % what)
    exe = build(cc, folder, 'guard_desc_none', NEW, ['NO_CHECK'], ['-I', GUARD, os.path.join(GUARD, 'evre_guard.cpp')],
                source='guard_desc_test.cpp')
    check(exe is not None, 'built: the register checks against a device without part 2 (-DNO_CHECK)')
    if exe:
        _, out = execute(exe)
        fails = {}
        for line in out.splitlines():
            if line.startswith('FAIL'):
                tag = line.split(' ', 2)[1].rstrip(':')
                fails[tag] = fails.get(tag, 0) + 1
        print('     without part 2 these checks fail, as they must: %s' % ', '.join(
            '%s %d' % (tag, fails[tag]) for tag in sorted(fails, key=lambda t: (t[0] != 'D', t))))
        # every decision that changes what a device answers or stores fails without part 2; the others (activity,
        # a clamp register, a broadcast of CONFIG, the entry's size, the device's own frames) hold either way and are
        # proven by their mutants or are part 1's
        must = ('D-26', 'D-27', 'D-28', 'D-30', 'D-31', 'D-32', 'D-33', 'D-34', 'D-35', 'D-38', 'D-39', 'D-41', 'D-43',
                'D-49', 'wire')
        missing = [tag for tag in must if not fails.get(tag)]
        check(not missing, 'without part 2 the checks of %s fail (each a behaviour part 2 adds)%s'
              % (', '.join(must), ': not ' + ', '.join(missing) if missing else ''))
    for std in ('c++11', 'c++17'):
        r = subprocess.run([cc, '-std=' + std, '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-c',
                            os.path.join(GUARD, 'evre_guard_desc.cpp'), '-I', NEW, '-I', GUARD,
                            '-o', os.path.join(folder, 'gd.o')], capture_output=True, text=True)
        check(r.returncode == 0, 'EVRe Guard part 2 compiles with -Wall -Wextra -Wpedantic -Werror (%s) %s'
              % (std, r.stderr.strip()[:300]))


# ------------------------------------------------------------------ EVRe Guard part 2: the proof (G.2)

def guard_fuzz(cc, folder, cases, sanitize):
    """guard_fuzz.cpp: builds A, B, C and N in step, B and C against the oracle; 2 seeds at -O1 and -O2 (and, on Linux,
    a shorter run under the address and undefined-behaviour sanitizers). Every class shows up, and no failure."""
    runs = [('-O1', [], cases), ('-O2', [], cases)]
    if sanitize:
        runs.append(('-O1 sanitizers', ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'], max(cases // 6, 1000)))
    for label, extra, n in runs:
        exe = build(cc, folder, 'guard_fuzz' + label.replace(' ', '_').replace('-', ''), NEW, [],
                    ['-I', GUARD, '-Wall', '-Wextra'] + extra + GUARD_SOURCES, source='guard_fuzz.cpp',
                    flags=('-std=c++17', label.split()[0]))
        check(exe is not None, 'guard fuzz: built (%s)' % label)
        if not exe:
            continue
        for seed in ('1', '2'):
            code, out = execute(exe, [str(n), seed])
            lines = out.strip().splitlines()
            for line in lines[:-1][:5]:
                print('     ' + line)
            summary = lines[-1] if lines else ''
            fields = summary.split()
            counts = dict(zip(fields[0::2], fields[1::2]))
            missing = [c for c, _, _ in GUARD_CLASSES if counts.get(c, '0') == '0']
            print('     ' + summary)
            check(code == 0 and summary.endswith('failed 0') and not missing,
                  'guard fuzz (%s, seed %s): %s cases; B against A and C against N, every refusal the oracle\'s, in its class, '
                  'nothing stored; every class seen%s' % (label, seed, counts.get('cases', '?'),
                                                          ': not ' + ', '.join(missing) if missing else ''))


def guard_mutants(cc, cases):
    """guard_mutants.py: every mutant of part 2 fails a feature check of its decision and the fuzz"""
    r = subprocess.run([sys.executable, os.path.join(HERE, 'guard_mutants.py'), '--cc', cc, '--cases', str(cases)],
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if line.startswith(('PASS', 'FAIL')):
            check(line.startswith('PASS'), '[mutant] ' + line.split(' ', 1)[1])
    check(r.returncode == 0, 'guard mutants: %s' % (r.stdout.strip().splitlines() or ['no output'])[-1])


def library_never_15(cc, folder):
    """the library's own fuzz (fuzz_test.cpp) against ../lib: no operation returns 15, which only a handler may"""
    exe = build(cc, folder, 'fuzz_lib', NEW, [], source='fuzz_test.cpp', flags=['-std=c++17', '-O1'])
    check(exe is not None, 'built: the library fuzz against ../lib')
    if not exe:
        return
    code, out = execute(exe, ['20000', '1'])
    fifteen = [line for line in out.splitlines() if len(line.split()) > 2 and line.split()[2] == '15']
    check(code == 0 and not fifteen, 'D-27: the library fuzz, 20000 cases: the library never returns 15 (%d lines do)'
          % len(fifteen))


# (name on the skip flag, compiler, flags, standards); the AVR stand-in headers in tests/avr are seen by that row only
MATRIX = (('g++', 'g++', [], ('c++11', 'c++14', 'c++17', 'c++20')),
          ('m32', 'g++', ['-m32'], ('c++11', 'c++14', 'c++17', 'c++20')),
          ('arm-none-eabi-g++', 'arm-none-eabi-g++',
           ['-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti'],
           ('c++11', 'c++14', 'c++17', 'c++20')),
          ('arm-none-eabi-g++', 'arm-none-eabi-g++', ['-mcpu=cortex-m0', '-mthumb', '-mfloat-abi=soft', '-fno-exceptions', '-fno-rtti'],
           ('c++11', 'c++14', 'c++17', 'c++20')),
          ('avr-g++', 'avr-g++', ['-mmcu=atmega2560', '-I', os.path.join(HERE, 'avr')], ('c++11', 'c++14', 'c++17')))
MATRIX_FILES = (os.path.join(NEW, 'EVRe.cpp'), os.path.join(GUARD, 'evre_guard.cpp'), os.path.join(GUARD, 'evre_guard_desc.cpp'),
                os.path.join(HERE, 'guard_device.cpp'))
STACK_LIMIT = 96  # bytes, evre_guard_check_write on the Cortex-M7 at -Os (GUARD_PLAN.md section 8)


def usable(name, compiler, skip):
    """a compiler of the matrix: there, or named on --skip; a missing one that is not skipped fails the run"""
    if name in skip or compiler in skip:
        print('     skipped (--skip): %s' % name)
        return False
    if name == 'm32':
        # into a file: os.devnull is "nul" on Windows, which MinGW's assembler and linker cannot create
        with tempfile.TemporaryDirectory() as probe:
            r = subprocess.run(['g++', '-m32', '-x', 'c++', '-', '-o', os.path.join(probe, 'm32')], input='int main(){}',
                               capture_output=True, text=True)
        ok = r.returncode == 0
    else:
        ok = shutil.which(compiler) is not None
    check(ok, 'the build matrix: %s is there (or name it on --skip)' % name)
    return ok


def matrix(folder, skip):
    """the library, part 1, part 2 and a device with a generated-style table, compiled on every target, standard and
    optimisation with -Wall -Wextra -Wpedantic -Werror; nothing is run but on the host"""
    import concurrent.futures
    jobs = []
    for name, compiler, flags, stds in MATRIX:
        if not usable(name, compiler, skip):
            continue
        for std in stds:
            for opt in ('-O0', '-O1', '-O2', '-O3', '-Os'):
                for source in MATRIX_FILES:
                    # each build into a file of its own (they run side by side), not os.devnull: that is "nul" on
                    # Windows, which MinGW's assembler cannot create
                    cmd = [compiler, '-std=' + std, opt] + flags + ['-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', NEW,
                                                                    '-I', GUARD, '-c', source, '-o',
                                                                    os.path.join(folder, 'matrix_%d.o' % len(jobs))]
                    jobs.append(('%s %s %s %s' % (' '.join([compiler] + flags[:1]), std, opt, os.path.basename(source)), cmd))

    def one(job):
        r = subprocess.run(job[1], capture_output=True, text=True)
        return job[0], r.returncode, r.stderr

    bad = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        for what, code, err in pool.map(one, jobs):
            if code or 'warning' in err:
                bad.append((what, err.strip()[:300]))
    for what, err in bad[:5]:
        print('     %s: %s' % (what, err))
    check(jobs and not bad, 'the build matrix: %d builds (targets x C++11..20 x -O0..-O3, -Os x the library, parts 1 and 2 '
          'and a device), 0 warnings with -Wall -Wextra -Wpedantic -Werror (%d failed)' % (len(jobs), len(bad)))


def arm_stack_and_size(folder, skip):
    """On the Cortex-M7 at -Os: the code size of part 2, every new function's stack static and the check's under
    STACK_LIMIT; then the firmware's own release flags (-Oz -flto), linked, with the guarded write path's stack. Returns
    what docs_checks compares with the docs (None without the compiler)."""
    if 'arm-none-eabi-g++' in skip or not shutil.which('arm-none-eabi-g++'):
        return
    m7 = ['arm-none-eabi-g++', '-mcpu=cortex-m7', '-mthumb', '-mfpu=fpv5-d16', '-mfloat-abi=hard', '-fno-exceptions', '-fno-rtti',
          '-std=c++11', '-I', NEW, '-I', GUARD]
    obj = os.path.join(folder, 'desc_m7.o')
    r = subprocess.run(m7 + ['-Os', '-ffunction-sections', '-fstack-usage', '-c', os.path.join(GUARD, 'evre_guard_desc.cpp'), '-o', obj],
                       capture_output=True, text=True, cwd=folder)
    check(r.returncode == 0, 'Cortex-M7 -Os: part 2 compiles with -fstack-usage')
    if r.returncode != 0:
        return
    su = os.path.join(folder, 'desc_m7.su')
    usage = {}
    if os.path.exists(su):
        for line in open(su):
            parts = line.rsplit('\t', 2)
            if len(parts) == 3:
                usage[parts[0].split(':')[-1].split('(')[0].split()[-1]] = (int(parts[1]), parts[2].strip())
    size = subprocess.run(['arm-none-eabi-size', '-A', obj], capture_output=True, text=True).stdout
    text = sum(int(line.split()[1]) for line in size.splitlines() if line.startswith('.text'))
    # each function's code, from its own section (-ffunction-sections): what PROTOCOL.md's "Cost" names
    functions = {}
    for line in size.splitlines():
        for name in ('evre_guard_check_write', 'evre_guard_check_init'):
            if line.startswith('.text._Z%d%s' % (len(name), name)):  # its C++ name, mangled
                functions[name] = int(line.split()[1])
    public = dict((k, v) for k, v in usage.items() if k.startswith('evre_guard_'))
    print('     Cortex-M7 -Os: part 2 %d B of code; stack %s' % (text, ', '.join('%s %d B' % (k, v[0]) for k, v in sorted(public.items()))))
    check(usage and all(kind == 'static' for _, kind in usage.values()), 'R3: every function of part 2 has a static stack (no alloca, '
          'no variable-length array)')
    check(public.get('evre_guard_check_write', (999,))[0] <= STACK_LIMIT, 'the check\'s stack on the Cortex-M7 at -Os: %s B, at most %d B'
          % (public.get('evre_guard_check_write', ('?',))[0], STACK_LIMIT))
    lto = os.path.join(folder, 'lto')
    os.makedirs(lto, exist_ok=True)
    r = subprocess.run(m7 + ['-Oz', '-flto', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '--specs=nosys.specs',
                             '-fstack-usage', '-Wall', '-Wextra', '-Wpedantic', '-Werror'] + list(MATRIX_FILES) +
                       ['-o', os.path.join(lto, 'device.elf')], capture_output=True, text=True, cwd=lto)
    frames = {}
    for name in os.listdir(lto):
        if name.endswith('.su'):
            for line in open(os.path.join(lto, name)):
                parts = line.rsplit('\t', 2)
                if len(parts) == 3:
                    frames[parts[0].split(':')[-1].split('.')[0]] = int(parts[1])
    path = sum(frames.get(f, 0) for f in ('main', 'decodePacketInto', 'onWrite', 'look'))
    print('     Cortex-M7 -Oz -flto, a device with parts 1 and 2 linked: frames %s; the guarded write path about %d B' % (
        ', '.join('%s %d' % (k, v) for k, v in sorted(frames.items()) if v), path))
    check(r.returncode == 0 and frames, 'Cortex-M7 -Oz -flto (the firmware\'s release flags): the device with parts 1 and 2 links, '
          'its stack measured')
    # the sizes of the entry, the table and the check on this target
    probe = os.path.join(folder, 'sizes_m7.cpp')
    with open(probe, 'w') as f:
        f.write('#include "evre_guard_desc.h"\n'
                'static_assert(sizeof(evre_guard_desc_t) == 24, "entry");\n'
                'static_assert(sizeof(evre_guard_table_t) == 12, "table");\n'
                'static_assert(sizeof(evre_guard_check_t) == 12, "check");\n')
    sizes = subprocess.run(m7 + ['-c', probe, '-o', os.devnull], capture_output=True, text=True)
    return {'write': functions.get('evre_guard_check_write', 0), 'init': functions.get('evre_guard_check_init', 0),
            'stack': public.get('evre_guard_check_write', (0,))[0], 'sizes': sizes.returncode == 0}


def docs_checks(cc, folder, measured):
    """GUARD_PLAN.md section 9, item 9: the docs say what the code does. PROTOCOL.md's two wirings of EVRe Guard
    compile as written; its error table names every code of EVRe.h with its value; its "Cost" equals what the
    Cortex-M7 build measured (when it ran)."""
    import re
    protocol = open(os.path.join(HERE, '..', 'docs', 'PROTOCOL.md'), encoding='utf-8').read()
    blocks = re.findall(r'```c\n(.*?)```', protocol, re.S)
    part1 = [b for b in blocks if 'evre_guard_config_t guard_config' in b]
    part2 = [b for b in blocks if 'evre_guard_write_checked' in b]
    check(len(part1) == 1 and len(part2) == 1, 'docs: PROTOCOL.md has one wiring for part 1 and one for parts 1 and 2')

    def program(block, before):
        # the lines before "at start-up" are the file's, the rest the body of a function, as a device writes them
        head, _, body = block.partition('/* at start-up')
        body = '/* at start-up' + body
        sending = '\t(void) sending;\n' if 'sending' in body else ''
        return ('#include <stdint.h>\n#include "EVRe.h"\n#include "evre_guard.h"\n' + before + head +
                'void setup(void);\nvoid setup(void) {\n' + body + sending + '}\n')
    stand_in = os.path.join(folder, 'example_guard.h')
    with open(stand_in, 'w') as f:
        f.write('/* a stand-in for evre export example.json --to guard */\n#include "evre_guard_desc.h"\n'
                'extern const evre_guard_table_t example_table;\n')
    scaffold1 = 'static evre_base_t dev;\nstatic uint8_t saved_failures;\n'
    scaffold2 = ('static evre_base_t dev;\nstatic uint64_t now64(void) { return 0; }\n'
                 'static uint8_t token[16];\nstatic const evre_guard_config_t guard_config = { 0xD010, 16, token, nullptr, 0, 3, '
                 '1000, 60000, 300000, now64 };\n')
    for what, block, before in (('part 1', part1, scaffold1), ('parts 1 and 2', part2, scaffold2)):
        if not block:
            continue
        source = os.path.join(folder, 'wiring_%d.cpp' % len(before))
        with open(source, 'w', encoding='utf-8') as f:
            f.write(program(block[0], before))
        bad = []
        for std in ('c++11', 'c++17'):
            r = subprocess.run([cc, '-std=' + std, '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', folder, '-I', NEW, '-I', GUARD,
                                '-c', source, '-o', os.devnull], capture_output=True, text=True)
            if r.returncode:
                bad.append('%s: %s' % (std, r.stderr.strip()[:300]))
        check(not bad, 'docs: PROTOCOL.md\'s wiring of %s compiles as written (C++11, C++17, -Wpedantic -Werror)%s'
              % (what, ' %s' % bad if bad else ''))

    # the error table against the enum
    header = open(os.path.join(NEW, 'EVRe.h'), encoding='utf-8').read()
    enum = re.search(r'enum ERR_CODE_ENUM \{(.*?)\};', header, re.S).group(1)
    codes = dict((m.group(1), int(m.group(2))) for m in re.finditer(r'^\s*(\w+) = (\d+)U,', enum, re.M))
    errors = protocol.split('| Code | Name | Meaning | On the wire |', 1)[-1].split('\n\n', 1)[0]
    table = dict((m.group(2), int(m.group(1))) for m in re.finditer(r'^\| (\d+) \| `(\w+)` \|', errors, re.M))
    check(codes and table == codes, 'docs: PROTOCOL.md\'s error table names every code of EVRe.h with its value (%d codes)%s'
          % (len(codes), '' if table == codes else ' %s' % sorted(set(table.items()) ^ set(codes.items()))))

    # the Cost paragraph against the Cortex-M7 build
    if not measured:
        print('     the Cost numbers are checked with arm-none-eabi-g++ only: skipped')
        return
    cost = re.search(r'\*\*Cost\.\*\* About ([\d.]+) KB of code for the write check and ([\d.]+) KB for init.*?'
                     r'(\d+) B of flash per writable register.*?(\d+) B per table; (\d+) B of RAM; (\d+) B of the decoder\'s stack',
                     protocol, re.S)
    if not cost:
        check(False, 'docs: PROTOCOL.md\'s "Cost" paragraph is where the check reads it')
        return
    write_kb, init_kb, entry, table_b, ram, stack = cost.groups()
    kb = lambda n: '%.1f' % (n / 1024.0)
    check(kb(measured['write']) == write_kb and kb(measured['init']) == init_kb,
          'docs: the Cost\'s code sizes are the measured ones (the write check %d B = %s KB, init %d B = %s KB)'
          % (measured['write'], write_kb, measured['init'], init_kb))
    check(int(stack) == measured['stack'], 'docs: the Cost\'s stack is the measured one (%s B, measured %d B)' % (stack, measured['stack']))
    check(measured['sizes'] and (entry, table_b, ram) == ('24', '12', '12'),
          'docs: the Cost\'s 24 B per register, 12 B per table and 12 B of RAM are the sizes on the Cortex-M7')


def r3_checks(cc, folder):
    """R3: no heap, no recursion, no goto, no cast of the data pointer in part 2"""
    source = open(os.path.join(GUARD, 'evre_guard_desc.cpp'), encoding='utf-8').read()
    code = '\n'.join(line.split('/*')[0] for line in source.splitlines())
    check('goto' not in code.split(), 'R3: no goto in part 2')
    import re
    casts = re.findall(r'\(\s*(?:const\s+)?(?:u?int(?:16|32)_t|float|double)\s*\*\s*\)', code)
    check(not casts, 'R3: no cast of a byte pointer to a wider type in part 2 (the frame\'s bytes are read one at a time) %s' % casts)
    obj = os.path.join(folder, 'desc_host.o')
    ci = os.path.join(folder, 'desc_host.ci')
    r = subprocess.run([cc, '-std=c++11', '-O2', '-fcallgraph-info', '-I', NEW, '-I', GUARD, '-c',
                        os.path.join(GUARD, 'evre_guard_desc.cpp'), '-o', obj], capture_output=True, text=True, cwd=folder)
    if r.returncode != 0:
        print('     -fcallgraph-info not taken by %s: the recursion check is skipped' % cc)
    else:
        nm = subprocess.run(['nm', obj], capture_output=True, text=True).stdout
        heap = [s for s in ('malloc', 'calloc', 'free', '_Znw', '_Zna', '_Zdl', '_Zda') if s in nm]
        check(not heap, 'R3: part 2 calls no malloc, calloc, free, new or delete (nm) %s' % heap)
        edges = {}
        if os.path.exists(ci):
            for m in re.finditer(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"', open(ci).read()):
                edges.setdefault(m.group(1), set()).add(m.group(2))

        def reaches(start, target, seen):
            for nxt in edges.get(start, ()):
                if nxt == target or (nxt not in seen and (seen.add(nxt) or reaches(nxt, target, seen))):
                    return True
            return False
        loops = [f for f in edges if reaches(f, f, set())]
        check(edges and not loops, 'R3: no function of part 2 reaches itself (-fcallgraph-info, %d callers) %s' % (len(edges), loops))


def r4_scan():
    """R4: the Guard's files and their tests in UTF-8 with LF, ASCII only (comments too), no section sign"""
    files = [os.path.join(GUARD, f) for f in sorted(os.listdir(GUARD))] + \
        [os.path.join(HERE, f) for f in ('guard_desc_test.cpp', 'guard_fuzz.cpp', 'guard_device.cpp', 'guard_mutants.py')]
    bad = []
    for path in files:
        data = open(path, 'rb').read()
        if b'\r' in data or any(b > 0x7F for b in data):
            bad.append(os.path.basename(path))
    check(not bad, 'R4: EVRe Guard and its tests: ASCII only, LF line endings (%d files) %s' % (len(files), bad))


def sanitized_features(cc, folder):
    """on Linux: the register checks under the address and undefined-behaviour sanitizers (UBSan's alignment check
    sees a cast of the data pointer on a PC too; ASan a read past the frame)"""
    exe = build(cc, folder, 'guard_desc_san', NEW, [], ['-I', GUARD, '-g', '-fsanitize=address,undefined',
                                                         '-fno-sanitize-recover=all'] + GUARD_SOURCES, source='guard_desc_test.cpp')
    check(exe is not None, 'built: the register checks under -fsanitize=address,undefined')
    if exe:
        code, out = execute(exe)
        check(code == 0 and out.strip().endswith('0 failed'), 'the register checks under the sanitizers: every check passes, '
              'no report')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(HERE, 'lib_1.0'))
    ap.add_argument('--cc', default='g++')
    ap.add_argument('--fuzz', metavar='DIR', help='the library before a refactor: compare it with ../lib (fuzz_test.cpp)')
    ap.add_argument('--fuzz-cases', type=int, default=300000)
    ap.add_argument('--guard-cases', type=int, default=300000, help='cases of the guard fuzz, per seed and build')
    ap.add_argument('--skip', default='', help='compilers of the build matrix not to use, comma-separated: m32, '
                    'arm-none-eabi-g++, avr-g++ (a missing one not named here fails the run)')
    opts = ap.parse_args()
    skip = set(s.strip() for s in opts.skip.split(',') if s.strip())
    if not shutil.which(opts.cc):
        print('missing: %s' % opts.cc)
        return 2
    with tempfile.TemporaryDirectory() as folder:
        transcripts = {}
        builds = (('1.0 pointers', opts.old, ['TRANSCRIPT']),
                  ('1.1 pointers', NEW, ['TRANSCRIPT']),
                  ('1.1 ranges', NEW, ['TRANSCRIPT', 'USE_RANGES']),
                  ('1.1 pointers, mirror', NEW, ['TRANSCRIPT', 'MIRROR']),
                  ('1.1 ranges, mirror', NEW, ['TRANSCRIPT', 'USE_RANGES', 'MIRROR']),
                  ('1.1 pointers, broadcast into 0xD000', NEW, ['TRANSCRIPT', 'BROADCAST_D000']),
                  ('1.1 ranges, broadcast into 0xD000', NEW, ['TRANSCRIPT', 'USE_RANGES', 'BROADCAST_D000']),
                  ('1.1 pointers, mirror, broadcast into 0xD000', NEW, ['TRANSCRIPT', 'MIRROR', 'BROADCAST_D000']))
        for name, lib, defines in builds:
            exe = build(opts.cc, folder, name.replace(' ', '_').replace('.', '').replace(',', ''), lib, defines)
            check(exe is not None, 'built: %s' % name)
            if exe:
                code, out = execute(exe)
                check(code == 0, 'ran: %s' % name)
                transcripts[name] = out
        if len(transcripts) == len(builds):
            base = transcripts['1.0 pointers']
            frames = base.strip().splitlines()[-1]
            print('     1.0: %s, %d lines, sha256 %s' % (frames, len(base.splitlines()), hashlib.sha256(base.encode()).hexdigest()[:16]))
            for name, _, defines in builds[1:]:
                compare(base, transcripts[name], name, 'MIRROR' in defines, 'BROADCAST_D000' in defines)
        guard_transcripts(opts.cc, folder, transcripts)

        guard = ['-I', os.path.join(NEW, 'guard'), os.path.join(NEW, 'guard', 'evre_guard.cpp'), '-Wall', '-Wextra',
                 '-Wl,--wrap=calloc']
        for exe_name, label, defines, extra in (
                ('features', '', ['FEATURES', 'WRAP_CALLOC'], []),
                ('features_ram_lock', 'RAM table, lock hooks', ['FEATURES', 'WRAP_CALLOC', 'EVRE_CRC_TABLE_RUNTIME=1'],
                 ['-include', os.path.join(HERE, 'lock_hooks.h')])):
            what = 'features' + (', %s' % label if label else '')
            exe = build(opts.cc, folder, exe_name, NEW, defines, guard + extra)
            check(exe is not None, 'built: %s (1.1 + EVRe Guard, -Wall -Wextra)' % what)
            if exe:
                code, out = execute(exe)
                for line in out.splitlines():
                    if line.startswith(('PASS', 'FAIL')):
                        check(line.startswith('PASS'), ('[%s] ' % label if label else '') + line.split(' ', 1)[1])
                check(code == 0, 'the %s program passed' % what)

        configure_override(opts.cc, folder)
        api_compat(opts.cc, folder)

        old_w = warnings(opts.cc, opts.old, folder)
        new_w = []
        for flags in WARNING_BUILDS:
            new_w += ['%s: %s' % (' '.join(flags), w) for w in warnings(opts.cc, NEW, folder, flags)]
        print('     warnings with -Wall -Wextra: 1.0 %d (-std=c++17 -O1), 1.1 %d in %d builds' % (len(old_w), len(new_w), len(WARNING_BUILDS)))
        check(not new_w, 'the 1.1 library compiles without a warning (-Wall -Wextra; C++11 and C++17; -O0 -O1 -O2 -Os -O3)')
        for w in new_w[:5]:
            print('     ' + w)
        g = subprocess.run([opts.cc, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-c', os.path.join(NEW, 'guard', 'evre_guard.cpp'),
                            '-I', NEW, '-o', os.path.join(folder, 'g.o')], capture_output=True, text=True)
        check(g.returncode == 0, 'EVRe Guard compiles with -Wall -Wextra -Werror %s' % g.stderr.strip()[:300])
        register_checks(opts.cc, folder)
        linux = sys.platform.startswith('linux')
        guard_fuzz(opts.cc, folder, opts.guard_cases, linux)
        guard_mutants(opts.cc, max(opts.guard_cases // 15, 2000))
        library_never_15(opts.cc, folder)
        if linux:
            sanitized_features(opts.cc, folder)
        else:
            print('     the sanitizer builds run on Linux only: not on %s' % sys.platform)
        r3_checks(opts.cc, folder)
        r4_scan()
        matrix(folder, skip)
        measured = arm_stack_and_size(folder, skip)
        docs_checks(opts.cc, folder, measured)

        if opts.fuzz:
            fuzz(opts.cc, folder, os.path.abspath(opts.fuzz), opts.fuzz_cases)
    print('%d passed, %d failed' % (passed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

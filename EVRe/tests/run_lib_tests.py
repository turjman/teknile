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
6. With --fuzz DIR: fuzz_test.cpp built against the library in DIR and against
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
import os
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
NEW = os.path.normpath(os.path.join(HERE, '..', 'lib'))
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
        exe = build(cc, folder, 'api_compat', NEW, [], source='api_compat.cpp',
                    flags=flags + ['-O2', '-Wall', '-Wextra', '-Werror'])
        check(exe is not None, 'api_compat: every public name compiles, %s, -Wall -Wextra -Werror' % what)
        if exe:
            code, out = execute(exe)
            bad = [line for line in out.splitlines() if line.startswith('FAIL')]
            for line in bad[:5]:
                print('     ' + line)
            check(code == 0 and not bad and out.strip().endswith('0 failed'), 'api_compat: runs, %s' % what)
    r = subprocess.run([cc, '-std=c++11', '-Wall', '-Wextra', '-Werror', '-Wmissing-declarations', '-DDEFINE_CONFIGURE',
                        '-I', NEW, '-c', os.path.join(HERE, 'api_compat.cpp'), '-o', os.path.join(folder, 'cfg.o')],
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--old', default=os.path.join(HERE, 'lib_1.0'))
    ap.add_argument('--cc', default='g++')
    ap.add_argument('--fuzz', metavar='DIR', help='the library before a refactor: compare it with ../lib (fuzz_test.cpp)')
    ap.add_argument('--fuzz-cases', type=int, default=300000)
    opts = ap.parse_args()
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

        if opts.fuzz:
            fuzz(opts.cc, folder, os.path.abspath(opts.fuzz), opts.fuzz_cases)
    print('%d passed, %d failed' % (passed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

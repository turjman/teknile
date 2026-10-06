#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Mutants of EVRe Guard part 2 (GUARD_PLAN.md section 9, item 5): one per decision and per rule.

    python guard_mutants.py [--cc g++] [--cases 20000] [--only NAME]

Each mutant is one change of lib/guard/evre_guard_desc.cpp, made in a copy in a temporary folder. The copy is built
with guard_desc_test.cpp (the feature checks) and with guard_fuzz.cpp (the oracle fuzz), and each mutant must fail
both: a check that names its decision, and the fuzz. A mutant whose text is no longer in the source fails the run,
so the list cannot drift from the code unseen. run_lib_tests.py runs this script.
Exit code: 0 every mutant caught by both, 1 one was not (or its text is gone), 2 no compiler."""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.normpath(os.path.join(HERE, '..', 'lib'))
SOURCE = os.path.join(LIB, 'guard', 'evre_guard_desc.cpp')

# (name, the decision or rule it breaks, the text, its replacement)
MUTANTS = [
    ('the NaN test removed', 'D-32',
     '\t\tif (!isFinite(value)) {\n\t\t\treturn EVRE_GUARD_WHY_NOT_FINITE;',
     '\t\tif (false) {\n\t\t\treturn EVRE_GUARD_WHY_NOT_FINITE;'),
    ('the NaN test catches the infinities only', 'D-32',
     'return (f32 & F32_EXPONENT) != F32_EXPONENT;',
     'return (f32 & 0x7FFFFFFFUL) != F32_EXPONENT;'),
    ('<= turned into < on max', 'D-26',
     'return at >= key(reg->type, reg->min) && at <= key(reg->type, reg->max);',
     'return at >= key(reg->type, reg->min) && at < key(reg->type, reg->max);'),
    ('>= turned into > on min', 'D-26',
     'return at >= key(reg->type, reg->min) && at <= key(reg->type, reg->max);',
     'return at > key(reg->type, reg->min) && at <= key(reg->type, reg->max);'),
    ('a signed register compared unsigned', 'D-26',
     'return isSigned(type) ? (value ^ SIGN) : value;',
     'return value;'),
    ('no sign extension', 'D-26',
     '\t\tvalue = toSigned(value, reg->size);\n\t}\n\tif (listed',
     '\t\tvalue = value;\n\t}\n\tif (listed'),
    ('values read big endian', 'D-30',
     'raw = (raw << 8) | bytes[ind - 1U];',
     'raw = (raw << 8) | bytes[size - ind];'),
    ('the part-of-a-number rule removed', 'D-30',
     '\t\t\tif (reg->addr < pos || endOf(reg) > end) {\n\t\t\t\treturn refuse(check, PERMISSION_DENIED, reg->addr, EVRE_GUARD_WHY_PART);',
     '\t\t\tif (false) {\n\t\t\t\treturn refuse(check, PERMISSION_DENIED, reg->addr, EVRE_GUARD_WHY_PART);'),
    ('the uncovered-byte rule removed', 'D-31',
     'if (ind >= table->n_regs || table->regs[ind].addr > pos) {',
     'if (ind >= table->n_regs) {'),
    ('the list ignored (specials refused)', 'D-33',
     '\tif (listed(table, reg, value)) {\n\t\treturn EVRE_GUARD_WHY_NONE;',
     '\tif (false) {\n\t\treturn EVRE_GUARD_WHY_NONE;'),
    ('the reserved bank checked (CONFIG refused)', 'D-38',
     'if (offset >= RESERVED_START && end <= RESERVED_END) {',
     'if (false) {'),
    ('a bad table let through', 'D-38',
     'check->table = good ? table : nullptr;',
     'check->table = table;'),
    ('a bad table refuses the reserved bank too', 'D-38',
     '\tif (offset >= RESERVED_START && end <= RESERVED_END) {\n\t\treturn NO_ERROR;\n\t}\n\tif (check->table == nullptr) {',
     '\tif (check->table == nullptr) {\n\t\treturn refuse(check, PERMISSION_DENIED, offset, EVRE_GUARD_WHY_SETUP);\n\t}\n'
     '\tif (offset >= RESERVED_START && end <= RESERVED_END) {\n\t\treturn NO_ERROR;\n\t}\n\tif (check->table == nullptr) {'),
    ('init skips the order of the entries', 'D-38',
     'if (ind > 0 && reg->addr < endOf(&table->regs[ind - 1U])) {',
     'if (false) {'),
    ('init skips the size of the type', 'D-38',
     '} else if (!isNumber(reg->type) || reg->size != sizeOf(reg->type)) {',
     '} else if (!isNumber(reg->type)) {'),
    ('init skips the flags', 'D-42',
     'if (reg->spare1 != 0 || reg->spare2 != 0 || reg->flags != 0 || reg->zero_bits != 0) {',
     'if (reg->spare1 != 0 || reg->spare2 != 0 || reg->zero_bits != 0) {'),
    ('init skips the spare members', 'D-42',
     'if (reg->spare1 != 0 || reg->spare2 != 0 || reg->flags != 0 || reg->zero_bits != 0) {',
     'if (reg->flags != 0 || reg->zero_bits != 0) {'),
    ('init skips the limits', 'D-38',
     '\t\tif (!entryOk(reg) || !limitsOk(reg) || !listOk(table, reg)) {',
     '\t\tif (!entryOk(reg) || !listOk(table, reg)) {'),
    ('init skips the order of a list', 'D-38',
     '\t\tif (ind > first && value <= table->values[ind - 1U]) {',
     '\t\tif (false) {'),
    ('the check before the login', 'D-28',
     '\tconst uint8_t login = evre_guard_write(guard, device, offset, data, count);\n\tif (login != NO_ERROR) {',
     '\tconst uint8_t first = evre_guard_check_write(check, device, offset, data, count);\n\tif (first != NO_ERROR) {\n'
     '\t\treturn first;\n\t}\n\tconst uint8_t login = evre_guard_write(guard, device, offset, data, count);\n\tif (login != NO_ERROR) {'),
    ('EVRE_HANDLED returned in place of NO_ERROR', 'D-26',
     '\t\tpos = endOf(reg);\n\t\t++ind;\n\t}\n\treturn NO_ERROR;',
     '\t\tpos = endOf(reg);\n\t\t++ind;\n\t}\n\treturn EVRE_HANDLED;'),
    ('only the first register checked', 'D-26',
     '\t\tpos = endOf(reg);\n\t\t++ind;\n\t}\n\treturn NO_ERROR;',
     '\t\tpos = endOf(reg);\n\t\t++ind;\n\t\tbreak;\n\t}\n\treturn NO_ERROR;'),
    ('the search off by one', 'D-31',
     '\t\tif (endOf(&table->regs[mid]) <= offset) {',
     '\t\tif (endOf(&table->regs[mid]) < offset) {'),
    ('a mirror accepted', 'D-39',
     'if (device == nullptr || device->ACCEPT_READ_RESP != 0) {',
     'if (device == nullptr) {'),
    ('the mirror flag read at init only', 'D-39',
     'if (device == nullptr || device->ACCEPT_READ_RESP != 0) {',
     'if (device == nullptr) {\n\t\treturn refuse(check, PERMISSION_DENIED, offset, EVRE_GUARD_WHY_SETUP);\n\t}\n\tif (false) {'),
    ('-0.0 not read as +0.0', 'D-33',
     '\t\tif (value == F32_MINUS_ZERO) {\n\t\t\tvalue = 0; /* so a listed 0 takes -0.0 too */',
     '\t\tif (false) {\n\t\t\tvalue = 0; /* so a listed 0 takes -0.0 too */'),
    ('f32 compared as unsigned bits', 'D-49',
     '\t\treturn (value & SIGN) != 0 ? ~value : (value | SIGN);',
     '\t\treturn value;'),
    ('init trusting a range table out of order', 'D-41',
     '\t\treturn rangesInOrder(device) && insideRanges(table, device);',
     '\t\treturn insideRanges(table, device);'),
    ('init not checking the entries against the ranges', 'D-41',
     '\t\treturn rangesInOrder(device) && insideRanges(table, device);',
     '\t\treturn rangesInOrder(device);'),
    ('no refusal counted', 'D-43',
     '\t\t++check->refused;',
     '\t\tcheck->refused += 0;'),
]


def build(cc, folder, name, source, lib_guard, flags):
    exe = os.path.join(folder, name + ('.exe' if os.name == 'nt' else ''))
    cmd = [cc, '-std=c++17', '-O1'] + flags + ['-I', LIB, '-I', lib_guard, os.path.join(HERE, source), os.path.join(LIB, 'EVRe.cpp'),
                                               os.path.join(LIB, 'guard', 'evre_guard.cpp'),
                                               os.path.join(lib_guard, 'evre_guard_desc.cpp'), '-o', exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return exe if r.returncode == 0 else None


def run(exe, args=()):
    try:
        r = subprocess.run([exe] + list(args), capture_output=True, timeout=600)
        return r.returncode, r.stdout.decode('ascii', 'replace')
    except subprocess.TimeoutExpired:
        return -1, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', default='g++')
    ap.add_argument('--cases', type=int, default=20000)
    ap.add_argument('--only')
    opts = ap.parse_args()
    if not shutil.which(opts.cc):
        print('missing: %s' % opts.cc)
        return 2
    original = open(SOURCE, encoding='utf-8').read()
    caught = missed = 0
    with tempfile.TemporaryDirectory() as folder:
        guard = os.path.join(folder, 'guard')
        os.makedirs(guard)
        shutil.copy(os.path.join(LIB, 'guard', 'evre_guard.h'), guard)
        shutil.copy(os.path.join(LIB, 'guard', 'evre_guard_desc.h'), guard)
        for name, decision, old, new in MUTANTS:
            if opts.only and opts.only not in name:
                continue
            if original.count(old) != 1:
                print('FAIL %s: %s: its text is not in evre_guard_desc.cpp once (%d)' % (decision, name, original.count(old)))
                missed += 1
                continue
            with open(os.path.join(guard, 'evre_guard_desc.cpp'), 'w', encoding='utf-8', newline='\n') as f:
                f.write(original.replace(old, new))
            features = build(opts.cc, folder, 'features', 'guard_desc_test.cpp', guard, ['-Wno-unused-function'])
            fuzz = build(opts.cc, folder, 'fuzz', 'guard_fuzz.cpp', guard, ['-Wno-unused-function'])
            if not features or not fuzz:
                print('FAIL %s: %s: does not build' % (decision, name))
                missed += 1
                continue
            code, out = run(features)
            failing = [line.split(' ', 1)[1] for line in out.splitlines() if line.startswith('FAIL')]
            named = [line for line in failing if line.startswith(decision + ':') or line.startswith(decision + ' ')
                     or line.startswith('wire:')]
            fcode, fout = run(fuzz, [str(opts.cases), '1'])
            crashed = code < 0 or code > 1  # a signal: the mutant read past the frame, and the run says so
            ok = code != 0 and (named or crashed) and fcode != 0
            print('%s %s: %s: %s, the fuzz %s' % (
                'PASS' if ok else 'FAIL', decision, name,
                'the feature checks crash (exit %d)' % code if crashed else
                '%d feature checks fail (%d of %s or the wire table)' % (len(failing), len(named), decision),
                'fails' if fcode != 0 else 'passes'))
            if ok:
                caught += 1
            else:
                missed += 1
    print('%d mutants caught, %d missed' % (caught, missed))
    return 0 if missed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())

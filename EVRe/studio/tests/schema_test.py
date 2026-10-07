#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks map files against the format's JSON Schema (docs/evre-map-1.schema.json).

    python schema_test.py [map.json ...]     (default: every map in ../maps; bus files are not maps: left out)

Needs the jsonschema package; without it the test says SKIP and passes.
Exit code: 0 all valid (or skipped), 1 a map is not valid."""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(HERE, '..', 'docs', 'evre-map-1.schema.json')
MAP_FORMAT = os.path.join(HERE, '..', 'docs', 'MAP_FORMAT.md')

STREAM = {'name': 'ADC', 'addr': '0xDC00', 'size': 1024, 'rate': 10000, 'enable': 'ADC_STREAM',
          'channels': [{'name': 'I_LOAD', 'type': 'i16', 'unit': 'A', 'scale': 0.0005}]}


def doc_keys(section):
    """the keys a table of MAP_FORMAT.md's section lists in its first column ("A stream:", "A channel:")"""
    text = open(MAP_FORMAT, encoding='utf-8').read()
    part = text.split(section, 1)[1].split('\n\n', 2)[1]
    keys = set()
    for line in part.splitlines():
        if line.startswith('| `'):
            keys.update(re.findall(r'`([a-z_]+)`', line.split('|')[1]))
    return keys


def main():
    try:
        import jsonschema
    except ImportError:
        print('SKIP jsonschema is not installed (pip install jsonschema)')
        return 0
    with open(SCHEMA, encoding='utf-8') as f:
        schema = json.load(f)
    jsonschema.Draft202012Validator.check_schema(schema)
    print('PASS the schema itself is valid (draft 2020-12)')
    validator = jsonschema.Draft202012Validator(schema)
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(HERE, '..', 'maps', '*.json')))
    failed = 0
    for path in files:
        with open(path, encoding='utf-8') as f:
            doc = json.load(f)
        if str(doc.get('format', '')).startswith('evre-bus/'):
            print('SKIP %s: a bus file, not a map' % os.path.basename(path))
            continue
        errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.path))
        if errors:
            failed += 1
            print('FAIL %s' % os.path.basename(path))
            for e in errors[:10]:
                print('     %s: %s' % ('/'.join(str(p) for p in e.path), e.message))
        else:
            print('PASS %s: valid' % os.path.basename(path))
    # a map that is not one must fail
    bad = {'format': 'evre-map/1', 'registers': [{'addr': '0x10000', 'type': 'u12', 'access': 'maybe'}]}
    caught = len(list(validator.iter_errors(bad))) >= 3
    print('%s a bad map is refused (address, type, access)' % ('PASS' if caught else 'FAIL'))
    failed += 0 if caught else 1

    # Fast EVRe's streams: a good one passes, and what the schema can say of the checker's refusals is refused
    def stream_map(change):
        stream = json.loads(json.dumps(STREAM))
        change(stream)
        return {'format': 'evre-map/1', 'registers': [], 'streams': [stream]}
    def check(ok, what):
        nonlocal failed
        print('%s %s' % ('PASS' if ok else 'FAIL', what))
        failed += 0 if ok else 1
    check(not list(validator.iter_errors(stream_map(lambda s: None))), 'a stream is valid')
    refusals = [
        ('a window below the device bank', lambda s: s.update(addr='0xCC00')),
        ('a window past it', lambda s: s.update(addr=0xE000)),
        ('a window too small for the header and a record', lambda s: s.update(size=8)),
        ('a rate of 0', lambda s: s.update(rate=0)),
        ('no channel', lambda s: s.update(channels=[])),
        ('a bytes channel', lambda s: s['channels'][0].update(type='bytes')),
        ('a stream without a name', lambda s: s.pop('name')),
        ('a stream without a size', lambda s: s.pop('size')),
        ('a channel without a name', lambda s: s['channels'][0].pop('name')),
    ]
    for what, change in refusals:
        check(bool(list(validator.iter_errors(stream_map(change)))), 'refused: ' + what)
    # the keys MAP_FORMAT.md lists are the schema's
    defs = schema['$defs']
    for section, name in (('A stream:', 'stream'), ('A channel:', 'channel')):
        documented, schemed = doc_keys(section), set(defs[name]['properties'])
        check(documented == schemed, 'MAP_FORMAT.md lists the %s keys of the schema (%s)' %
              (name, ', '.join(sorted(documented ^ schemed)) or 'the same'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks map files against the format's JSON Schema (docs/evre-map-1.schema.json).

    python schema_test.py [map.json ...]     (default: every map in ../maps; bus files are not maps: left out)

Also: the register keys MAP_FORMAT.md and STUDIO.md document are the schema's.
Needs the jsonschema package; without it the test says SKIP and passes.
Exit code: 0 all valid (or skipped), 1 a map is not valid."""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(HERE, '..', 'docs', 'evre-map-1.schema.json')


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
    # EVRe Guard's key: "past_limits" is "refuse" or "clamp", nothing else
    def reg(**keys):
        return {'format': 'evre-map/1', 'registers': [dict({'addr': '0xD000', 'type': 'u8', 'access': 'rw'}, **keys)]}
    good = all(not list(validator.iter_errors(reg(past_limits=v, min=0, max=9))) for v in ('refuse', 'clamp'))
    wrong = len(list(validator.iter_errors(reg(past_limits='wrap')))) == 1
    print('%s "past_limits": "refuse" and "clamp" are valid, "wrap" is not' % ('PASS' if good and wrong else 'FAIL'))
    failed += 0 if good and wrong else 1
    flags = not list(validator.iter_errors(reg(closed=True, enum={'0': 'off'}, reserved_zero=True,
                                                fields=[{'name': 'A', 'bits': '0'}])))
    not_bool = len(list(validator.iter_errors(reg(closed='yes', reserved_zero=1)))) == 2
    print('%s "closed" and "reserved_zero" are booleans' % ('PASS' if flags and not_bool else 'FAIL'))
    failed += 0 if flags and not_bool else 1
    # the docs and the schema name the same register keys: MAP_FORMAT.md's table of section 4 (an overlay's
    # "remove" is in section 7) and STUDIO.md 16.3
    def keys_of(path, start, stop):
        text = open(os.path.join(HERE, '..', 'docs', path), encoding='utf-8').read()
        part = text.split(start, 1)[-1].split(stop, 1)[0]
        return set(re.findall(r'`(\w+)`', ''.join(line.split(' | ')[0] for line in part.splitlines() if line.startswith('| `'))))
    in_schema = set(schema['$defs']['register']['properties'])
    in_format = keys_of('MAP_FORMAT.md', '## 4. A register', '## 5. Values') | {'remove'}
    in_studio = keys_of('STUDIO.md', '### 16.3 Register keys', '### 16.4')
    same = in_format == in_schema and in_studio <= in_schema and {'past_limits', 'closed', 'reserved_zero'} <= in_studio
    print('%s the register keys of MAP_FORMAT.md section 4 are the schema\'s, and STUDIO.md 16.3 names them%s'
          % ('PASS' if same else 'FAIL', '' if same else ' %s %s' % (sorted(in_format ^ in_schema), sorted(in_studio - in_schema))))
    failed += 0 if same else 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

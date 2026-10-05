#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks map files against the format's JSON Schema (docs/evre-map-1.schema.json).

    python schema_test.py [map.json ...]     (default: every map in ../maps; bus files are not maps: left out)

Needs the jsonschema package; without it the test says SKIP and passes.
Exit code: 0 all valid (or skipped), 1 a map is not valid."""
import glob
import json
import os
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
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())

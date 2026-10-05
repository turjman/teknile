#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EVRe Studio's JSON API from Python: read, write and stream registers by name.

EVRe Studio must be connected to the device with "Serve API" ticked. Writes
also need "Allow API writes" (and "including ⚠ registers" for those).

    python evre_studio_client.py                        # info, a few values, a 2 s stream
    python evre_studio_client.py SUPPLY_V TEMPERATURE   # stream these instead

The default names are registers of maps/example_device.json.
Standard library only.
"""
import json
import socket
import sys
import time


class EvreStudio:
    """One JSON object per line each way. Requests may carry "id"."""

    def __init__(self, host='127.0.0.1', port=1220, timeout=3.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.file = self.sock.makefile('rw', encoding='utf-8', newline='\n')

    def request(self, **q):
        self.file.write(json.dumps(q) + '\n')
        self.file.flush()
        answer = json.loads(self.file.readline())
        if not answer.get('ok', True):
            raise RuntimeError(answer.get('error', 'refused'))
        return answer

    def info(self):
        return self.request(cmd='info')

    def registers(self):
        return self.request(cmd='list')['registers']

    def get(self, *names):
        """fresh values from the device, scaled as the map says"""
        return self.request(cmd='get', names=list(names))['values']

    def set(self, **values):
        """write, then read back: returns what the device holds now"""
        return self.request(cmd='set', values=values)['values']

    def stream(self, names, ms=50):
        """yields (time, values) every ms until the caller stops iterating"""
        self.request(cmd='stream', names=list(names), ms=ms)
        try:
            while True:
                m = json.loads(self.file.readline())
                if 'values' in m and 't' in m:
                    yield m['t'], m['values']
        finally:
            self.file.write(json.dumps({'cmd': 'stop'}) + '\n')
            self.file.flush()

    def close(self):
        self.sock.close()


def main():
    s = EvreStudio()
    info = s.info()
    print('%s via %s, %d registers, writes %s' % (info['device'], info['link'], info['registers'],
                                                 'allowed' if info['writes'] else 'off'))
    names = sys.argv[1:] or ['SUPPLY_V', 'SUPPLY_I']
    print(s.get(*names))
    t_end = time.time() + 2.0
    for t, v in s.stream(names, ms=50):
        print('%.3f %s' % (t, '  '.join('%s=%s' % kv for kv in v.items())))
        if time.time() > t_end:
            break
    s.close()


if __name__ == '__main__':
    main()

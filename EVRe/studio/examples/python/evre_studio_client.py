#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""EVRe Studio's JSON API from Python: read, write and stream registers by name,
and a fast stream's channels as each period's min, max and mean.

EVRe Studio must be connected to the device with "Serve API" ticked. Writes
also need "Allow API writes" (and "including ⚠ registers" for those). A fast
stream must be on in the Studio (Start stream, or --fast ADC).

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

    def streams(self):
        """the map's fast streams: name, rate, on, and channels (each named STREAM.CHANNEL)"""
        return self.request(cmd='list').get('streams', [])

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

    def fast_stream(self, channels, period_ms=100):
        """yields (time, {channel: {"n", "min", "max", "mean", "first"}}) every period_ms: each fast
        channel's records of that period, until the caller stops iterating"""
        self.request(cmd='stream', names=list(channels), period_ms=period_ms)
        try:
            while True:
                m = json.loads(self.file.readline())
                if 'fast' in m and 't' in m:
                    yield m['t'], m['fast']
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
    # a fast stream that is on: its first channel's min, max and mean every 100 ms for 1 s
    s = EvreStudio()
    for stream in s.streams():
        state = 'on' if stream['on'] else 'off'
        print('fast stream %s: %s, %g records a second' % (stream['name'], state, stream['rate']))
        if not stream['on']:
            continue
        channel = stream['channels'][0]['name']
        t_end = time.time() + 1.0
        for t, fast in s.fast_stream([channel], period_ms=100):
            one = fast[channel]
            if one['n']:
                print('%.3f %s: %d records, min %g, max %g, mean %g' % (t, channel, one['n'], one['min'], one['max'],
                                                                       one['mean']))
            if time.time() > t_end:
                break
        break
    s.close()


if __name__ == '__main__':
    main()

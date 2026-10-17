# SPDX-License-Identifier: Apache-2.0
"""EVRe Studio's JSON API (port 1220): the device the Studio is connected to, by register name, and its fast
streams' channels (STREAM.CHANNEL) as each period's min, max and mean. The Studio's STUDIO.md, chapter 17.

    with evre.connect_studio() as studio:            # 127.0.0.1:1220, "Serve API" ticked in the Studio
        print(studio.get('SUPPLY_V', 'ADC.I_LOAD'))   # {'ADC.I_LOAD': 0.4185, 'SUPPLY_V': 12.03}
        for t, summary in studio.fast_stream(['ADC.I_LOAD'], period_ms=100):
            print(t, summary['ADC.I_LOAD'].mean)

A fast stream's raw records come by port 1219 (the EVRe pass-through), with Device.stream as from the device:
evre.connect_tcp('127.0.0.1', 1219, 'maps/example_fast.json').stream('ADC').
Standard library only.
"""
import collections
import json
import socket

from .link import EvreError

# one fast channel's records of a period: n records (numbers first ... first + n - 1), their min, max and mean; with
# n 0 the others are None
Summary = collections.namedtuple('Summary', 'n min max mean first')
# one line of a stream: t (seconds since 1970), values ({register: value}: a sample) or fast ({channel: Summary})
Line = collections.namedtuple('Line', 't values fast')


def _summary(numbers):
    return Summary(numbers.get('n', 0), numbers.get('min'), numbers.get('max'), numbers.get('mean'),
                   numbers.get('first'))


class Studio:
    """One connection to the JSON port: one object per line each way. A refusal raises EvreError with the Studio's
    reason."""

    def __init__(self, host='127.0.0.1', port=1220, timeout=3.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.file = self.sock.makefile('rw', encoding='utf-8', newline='\n')
        self._waiting = []  # stream lines read while an answer was awaited

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _send(self, request):
        self.file.write(json.dumps(request) + '\n')
        self.file.flush()

    def _line(self):
        text = self.file.readline()
        if not text:
            raise EvreError('EVRe Studio closed the connection')
        return json.loads(text)

    def request(self, **request):
        """the answer to one request; a stream's lines that come first are kept for stream()"""
        self._send(request)
        while True:
            answer = self._line()
            if 't' in answer:  # a stream's line: answers have no time
                self._waiting.append(answer)
                continue
            if not answer.get('ok', False):
                raise EvreError(answer.get('error', 'refused'))
            return answer

    def info(self):
        return self.request(cmd='info')

    def registers(self):
        """every register of the map, as "list" gives them"""
        return self.request(cmd='list')['registers']

    def streams(self):
        """the map's fast streams: name, rate, on, and channels (each its name STREAM.CHANNEL, type, unit)"""
        return self.request(cmd='list').get('streams', [])

    def get(self, *names):
        """fresh values of registers, and the newest record's value of fast channels: {name: value}"""
        return self.request(cmd='get', names=list(names))['values']

    def fast_value(self, name):
        """a fast channel's newest record: (value, its time in seconds since 1970)"""
        answer = self.request(cmd='get', names=[name])
        key = next(iter(answer['times']))
        return answer['values'][key], answer['times'][key]

    def set(self, **values):
        """write (needs Allow API writes), then read back: what the device holds now"""
        return self.request(cmd='set', values=values)['values']

    def stream(self, names, ms=100, period_ms=None):
        """yields a Line for each line the Studio sends: a sample of the registers every ms, and the fast channels'
        min, max and mean every period_ms (ms when None), until the caller stops iterating ({"cmd":"stop"} then)"""
        request = {'cmd': 'stream', 'names': list(names), 'ms': ms}
        if period_ms is not None:
            request['period_ms'] = period_ms
        self._waiting = []
        self.request(**request)
        try:
            while True:
                line = self._waiting.pop(0) if self._waiting else self._line()
                if 'fast' in line:
                    yield Line(line['t'], None, {k: _summary(v) for k, v in line['fast'].items()})
                elif 'values' in line:
                    yield Line(line['t'], line['values'], None)
        finally:
            self._send({'cmd': 'stop'})
            self._drain_stop()

    def fast_stream(self, channels, period_ms=100):
        """yields (t, {channel: Summary}) every period_ms: each fast channel's records of that period"""
        for line in self.stream(channels, period_ms=period_ms):
            if line.fast is not None:
                yield line.t, line.fast

    def _drain_stop(self):
        """the lines up to the stop's answer, read and dropped"""
        try:
            while True:
                answer = self._line()
                if answer.get('ok') is True:
                    return
        except (OSError, ValueError, EvreError):
            pass


def connect_studio(host='127.0.0.1', port=1220, timeout=3.0):
    """a Studio: EVRe Studio's JSON API, "Serve API" ticked (or started with --api)"""
    return Studio(host, port, timeout)

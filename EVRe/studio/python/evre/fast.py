# SPDX-License-Identifier: Apache-2.0
"""A fast stream's recording (Fast EVRe), a .evrs file as EVRe Studio and `evre record` write it.

    rec = evre.read_recording('run.ADC.evrs')
    print(rec.stream['name'], len(rec.times), 'samples,', rec.lost, 'lost')
    current = rec.values['I_LOAD']        # in the map's units (raw x scale + offset)
    t = rec.times                         # seconds on the writer's clock (EVRe Studio: the CSV's time_s)

The file is a row of pieces, each a name of 4 ASCII bytes and a length (u32, little endian) before its body:
EVRS (the head, JSON: "format", "device", "stream", "start", "start_s"), TIME (a record number u64 and its time
f64), and BLK (one block as it came: first u32, count u16, flags u8, spare u8, then the records). A piece whose name
is not known is skipped; a file cut off is read up to its last whole piece (`cut` says so).

Records keep their numbers in 64 bits across the device's 32-bit wrap; a block whose numbers do not follow the one
before begins a gap (`gaps`: where, and how many records were lost), START or numbers that go back a new start.
A record's time is its start's TIME mark before it plus the records since, at the period between that mark and
the next of the same start. Standard library only.
"""
import json
import struct

FORMAT = 'evre-fast-rec/1'
FLAG_START, FLAG_LOST = 0x01, 0x02
_TYPES = {'u8': 'B', 'i8': 'b', 'u16': 'H', 'i16': 'h', 'u32': 'I', 'i32': 'i', 'f32': 'f'}


class Recording:
    """What read_recording() gives: the head and the samples.

    head      the EVRS piece (a dict); stream: the stream's object from the map, device: the map's device
    start     the wall-clock time the recording began (ISO 8601); start_s: the writer's clock then
    numbers   each sample's record number (64 bits, counted from its start)
    times     each sample's time on the writer's clock, seconds
    values    {channel name: [value, ...]} in the map's units
    gaps      [(index of the sample after the gap, records lost)]; lost: their sum
    starts    the index of each start's first sample
    blocks, bad_blocks, cut
    """

    def __init__(self):
        self.head, self.stream, self.device, self.start, self.start_s = {}, {}, '', '', 0.0
        self.numbers, self.times, self.values = [], [], {}
        self.gaps, self.starts, self.lost = [], [], 0
        self.blocks, self.bad_blocks, self.cut = 0, 0, False


def _pieces(data):
    at, out = 0, []
    while at < len(data):
        if at + 8 > len(data):
            return out, True
        name, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
        if at + 8 + size > len(data):
            return out, True
        out.append((name, at + 8, size))
        at += 8 + size
    return out, False


def read_recording(path):
    """a .evrs file -> Recording; ValueError when it is not one"""
    with open(path, 'rb') as f:
        data = f.read()
    pieces, cut = _pieces(data)
    if not pieces or pieces[0][0] != b'EVRS':
        raise ValueError('%s: not a fast stream\'s recording (it does not begin with EVRS)' % path)
    rec = Recording()
    rec.cut = cut
    rec.head = json.loads(data[pieces[0][1]:pieces[0][1] + pieces[0][2]].decode('utf-8'))
    if rec.head.get('format') != FORMAT:
        raise ValueError('%s: not a recording of the format %s' % (path, FORMAT))
    rec.stream, rec.device = rec.head.get('stream', {}), rec.head.get('device', '')
    rec.start, rec.start_s = rec.head.get('start', ''), float(rec.head.get('start_s', 0))
    channels = rec.stream.get('channels', [])
    layout = '<' + ''.join(_TYPES[c.get('type', 'i16')] for c in channels)
    size = struct.calcsize(layout)
    scales = [(float(c.get('scale', 1)), float(c.get('offset', 0))) for c in channels]
    columns = [[] for _ in channels]
    rate = float(rec.stream.get('rate', 0) or 0)

    # the blocks in order, each start's marks laid after the block that follows them (a start's TIME comes before
    # its first block), as the Studio reads it
    starts_marks, pending = [], []
    end32 = end64 = None
    for name, at, length in pieces[1:]:
        if name == b'TIME' and length >= 16:
            pending.append(struct.unpack_from('<Qd', data, at))
        elif name == b'BLK ':
            if length < 8:
                rec.bad_blocks += 1
                continue
            first, count, flags, spare = struct.unpack_from('<IHBB', data, at)
            if length != 8 + count * size or flags & ~(FLAG_START | FLAG_LOST) or spare:
                rec.bad_blocks += 1
                continue
            new_start = end32 is None or bool(flags & FLAG_START)
            if not new_start:
                ahead = (first - end32) & 0xFFFFFFFF
                if ahead >= 0x80000000:      # went back without START: the device started again
                    new_start = True
            if new_start:
                number = first
                starts_marks.append([])
                rec.starts.append(len(rec.numbers))
            else:
                number = end64 + ahead
                if ahead:
                    rec.gaps.append((len(rec.numbers), ahead))
                    rec.lost += ahead
            if count:
                for k, values in enumerate(struct.iter_unpack(layout, data[at + 8:at + 8 + count * size])):
                    rec.numbers.append(number + k)
                    for c, v in enumerate(values):
                        columns[c].append(v * scales[c][0] + scales[c][1])
            starts_marks[-1].extend(pending)
            pending = []
            end32, end64 = (first + count) & 0xFFFFFFFF, number + count
            rec.blocks += 1
    if pending and starts_marks:
        starts_marks[-1].extend(pending)
    rec.values = {c.get('name', str(i)): columns[i] for i, c in enumerate(channels)}

    # the times: per start, from its marks
    bounds = rec.starts + [len(rec.numbers)]
    for s, marks in enumerate(starts_marks):
        marks = sorted(marks)
        periods = []
        for k, (record, time) in enumerate(marks):
            if k + 1 < len(marks) and marks[k + 1][0] > record:
                periods.append((marks[k + 1][1] - time) / (marks[k + 1][0] - record))
            else:
                periods.append(periods[-1] if periods else (1.0 / rate if rate > 0 else 0.0))
        m = 0
        for i in range(bounds[s], bounds[s + 1]):
            number = rec.numbers[i]
            if not marks:
                rec.times.append(float('nan'))
                continue
            while m + 1 < len(marks) and marks[m + 1][0] <= number:
                m += 1
            rec.times.append(marks[m][1] + (number - marks[m][0]) * periods[m])
    return rec

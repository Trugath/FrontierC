"""Compile the native intro scene timeline into scene-selection includes and C data.

Each scene row windows the selector's running time value: scene 00 the raw
resident time, scenes 01..21 the time left after scene 00 and the orbit
interval. Inside a window, phase = ((time-start)<<4 mod 2^32) DIV divisor
+ phase_bias as 16-bit words. The original 16-bit DIV faults if a quotient
exceeds FFFFh; the compiler rejects windows that could reach that path.
"""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCENES = 22
FIELDS = ['scene', 'start', 'end', 'divisor', 'phase_bias']


def document(config, source, asset):
    entry = config['timeline']
    if entry['module'] != 'intro':
        raise ValueError('Timeline belongs to intro')
    orbit_length = int(entry['orbit_length'], 0)
    with asset(source, entry['scenes']).open(encoding='ascii', newline='') as stream:
        rows = list(csv.reader(stream))
    if not rows or [value.strip() for value in rows[0]] != FIELDS:
        raise ValueError('Expected scene,start,end,divisor,phase_bias header')
    scenes = []
    for row in rows[1:]:
        if len(row) != len(FIELDS):
            raise ValueError('Scene timeline row needs five fields')
        index, start, end, divisor, bias = (int(value.strip(), 0) for value in row)
        if index != len(scenes):
            raise ValueError('Scenes must be ordered and consecutive from 00')
        if not 0 <= start < end <= 0xffffffff:
            raise ValueError(f'Scene {index:02d} window must satisfy 0 <= start < end < 2^32')
        if not 0 < divisor <= 0xffff or not 0 <= bias <= 0xffff:
            raise ValueError(f'Scene {index:02d} divisor/bias must be 16-bit, divisor nonzero')
        if end - start > 1 << 28 or ((end - start - 1) << 4) // divisor > 0xffff:
            raise ValueError(f'Scene {index:02d} window can overflow the 16-bit division')
        scenes.append({'scene': index, 'start': start, 'end': end, 'divisor': divisor, 'phase_bias': bias})
    if len(scenes) != SCENES:
        raise ValueError('Timeline must define scenes 00..21')
    if not 0 < orbit_length <= 0xffffffff or scenes[0]['end'] > 0xffffffff - orbit_length:
        raise ValueError('Invalid orbit interval')
    return {'format': 'fe2-intro-timeline-v1', 'orbit_start': scenes[0]['end'],
            'orbit_length': orbit_length, 'scenes': scenes}


def load(source):
    data = json.loads(Path(source).read_text(encoding='utf-8'))
    if data['format'] != 'fe2-intro-timeline-v1':
        raise ValueError('Unsupported intro timeline intermediate')
    return data

"""Compile systems handcrafted system keys into the keyed template table.

Each row maps a system's sector coordinates and system number to a template
section of the systems tables.ini (named without its `system_template_` prefix). The
table is six-byte records (key dword, template word) with a zero-key
terminator; the original reads the terminator's template word from the
following code bytes, so no fallback word is stored.
"""
import configparser
import csv
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def document(config, source, asset):
    entry = config['handcrafted_systems']
    if entry['module'] != 'systems':
        raise ValueError('Handcrafted systems belong to systems')
    templates = configparser.ConfigParser()
    templates.read(asset(source, entry['templates']), encoding='ascii')
    offsets = {name[len('system_template_'):]: int(templates[name]['start'], 0)
               for name in templates.sections() if name.startswith('system_template_')}
    from system_names import pack_system
    with asset(source, entry['entries']).open(encoding='ascii', newline='') as stream:
        rows = list(csv.reader(stream))
    if not rows or [value.strip() for value in rows[0]] != ['sector_x', 'sector_y', 'number', 'template']:
        raise ValueError('Expected sector_x,sector_y,number,template header')
    systems = []
    for row in rows[1:]:
        if len(row) != 4:
            raise ValueError('Handcrafted system rows need sector coordinates, a system number and a template')
        sector_x, sector_y, number, template = (value.strip() for value in row)
        if any(not re.fullmatch(r'-?[0-9]+', value) for value in (sector_x, sector_y, number)):
            raise ValueError('Sector coordinates and the system number must be decimal')
        key = pack_system(int(sector_x, 10), int(sector_y, 10), int(number, 10))
        if not 0 < key <= 0xffffffff or any(key == s['key'] for s in systems):
            raise ValueError('Keys must be unique nonzero 32-bit values')
        if template not in offsets or template == 'fallback':
            raise ValueError(f'Unknown or reserved template: {template}')
        systems.append({'key': key, 'template': template, 'offset': offsets[template]})
    start, end = int(entry['start'], 0), int(entry['end'], 0)
    if start + 6 * len(systems) + 4 != end:
        raise ValueError('Handcrafted system table size changed')
    return {'format': 'fe2-handcrafted-systems-v1', 'start': start, 'end': end,
            'fallback': offsets['fallback'], 'systems': systems}


def load(source):
    data = json.loads(Path(source).read_text(encoding='utf-8'))
    if data['format'] != 'fe2-handcrafted-systems-v1':
        raise ValueError('Unsupported handcrafted system intermediate')
    return data

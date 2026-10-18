"""Lay out the native systems profile/system/text resources at their image offsets.

Covers CS:00A2h..3F20h: the FF marker, every profile and system table, the
text directory and strings, the near profile-callback table (resolved to
handler offsets), the static object and the keyed handcrafted-system table.
The two bytes after the zero-key terminator are the RETF and MOV opcodes
that begin the code at 3F1Fh; the key scan reads them as the terminator's
template word, so they are included as data. tools/systems_tables_c.py slices
typed C tables from this layout.
"""
import configparser
import json
from pathlib import Path
import re
import struct
import csv

from module_tables import document
from resource_sources import asset, compile_resource
from handcrafted_system_keys import load as load_systems

ROOT=Path(__file__).resolve().parents[1]
START,END=0xa2,0x3f21
TERMINATOR_TEMPLATE_WORD=bytes((0xcb,0xa3))   # RETF; MOV moffs16,AX
CALLBACKS={'clear_profile_scalar_and_maybe_generate_name':0x4bda,'settle_warm_body':0x4bad,
    'settle_moderate_temperature_body':0x4b69,'warm_and_settle_body':0x4abb,'reclassify_temperature_and_settlement':0x4990,
    'promote_profile_to_descriptor_ae':0x4bff,'promote_profile_after_two_negative_samples':0x4c27,
    'promote_profile_after_negative_sample':0x4c2c}


def tables(path):
    config=configparser.ConfigParser(); config.read(path,encoding='ascii')
    return [(t['start'],b''.join(v.to_bytes(t['width'],'little') for v in t['values']))
            for t in document(config,path,asset,None)['tables']]


PROFILE_FIELDS = [('kind', 0xa3, 2), ('descriptor', 0xd1, 2),
                  ('size_threshold', 0xff, 4), ('subtree_position', 0x15b, 2),
                  ('temperature', 0x189, 2), ('profile_reference', 0x1b7, 4)]


def profile_records(path=None):
    path = path or ROOT / 'data/profiles/profiles.csv'
    rows = []
    columns = ['profile', 'size_threshold', 'kind', 'descriptor', 'temperature',
               'subtree_position', 'profile_reference', 'rule']
    handlers = {'none': 0, **CALLBACKS}
    with Path(path).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != columns:
            raise ValueError('Unexpected profile columns')
        for index, row in enumerate(reader):
            if None in row or any(value is None for value in row.values()):
                raise ValueError('Invalid profile row')
            if int(row['profile'], 0) != index or row['rule'] not in handlers:
                raise ValueError('Profile indices or rule names are invalid')
            record = {'callback': handlers[row['rule']]}
            for field, _, width in PROFILE_FIELDS:
                if field == 'descriptor':
                    from model_names import parse_model
                    value = parse_model(row[field])
                else:
                    value = int(row[field], 0)
                if not 0 <= value < 1 << (8 * width):
                    raise ValueError('Profile value exceeds its field width')
                record[field] = value
            rows.append(record)
    if len(rows) != 23:
        raise ValueError('Expected 23 ordered profile records')
    return rows


def profile_callbacks():
    return 0x213, b''.join(row['callback'].to_bytes(2, 'little') for row in profile_records())


def profile_tables():
    rows = profile_records()
    return [(start, b''.join(row[field].to_bytes(width, 'little') for row in rows))
            for field, start, width in PROFILE_FIELDS]


def strings():
    catalog = ROOT / 'data/text/systems/catalog.ini'
    if not (catalog.parent / 'strings.tsv').is_file():
        config = configparser.ConfigParser()
        config.read(catalog, encoding='ascii')
        start, end = int(config['strings']['directory_offset'], 0), int(config['strings']['end_offset'], 0)
        return start, bytes(end - start)
    text=json.loads(Path(compile_resource('strings',catalog)).read_text())
    entries=[bytes(e['bytes'])+b'\0' for e in text['entries']]
    relative=2*len(entries); offsets=[]
    for entry in entries: offsets.append(relative); relative+=len(entry)
    blob=struct.pack(f'<{len(offsets)}H',*offsets)+b''.join(entries)
    if text['directory_offset']+len(blob)!=text['end_offset']: raise ValueError('systems text layout changed')
    return text['directory_offset'],blob


def handcrafted_systems():
    data=load_systems(compile_resource('handcrafted_systems',ROOT/'data/systems/handcrafted/handcrafted_systems.ini'))
    blob=b''.join(struct.pack('<IH',s['key'],s['offset']) for s in data['systems'])+b'\0'*4
    return data['start'],blob+TERMINATOR_TEMPLATE_WORD


def blocks():
    result=[(0xa2,b'\xff'),profile_callbacks(),strings(),handcrafted_systems()]
    result+=profile_tables()+tables(ROOT/'data/profiles/tables.ini')
    result+=tables(ROOT/'data/systems/handcrafted/tables.ini')
    result.sort()
    if result[0][0]!=START or result[-1][0]+len(result[-1][1])!=END or any(
            a[0]+len(a[1])!=b[0] for a,b in zip(result,result[1:])):
        raise ValueError('systems resources must cover 00A2h..3F20h exactly')
    return result

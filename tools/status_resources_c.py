"""Lay out the native status resources at their original image offsets.

Covers CS:00A2h..1202h: the FF marker, every status CSV table, the text
directory and strings, the reserved zero byte at 1102h and the contract
creation table with handler names resolved to their entry offsets.
tools/status_tables_c.py slices typed C tables from this layout.
"""
import configparser
import json
from pathlib import Path
import struct

from module_tables import document
from resource_sources import asset, compile_resource
from contract_callback_table import load as load_callbacks

ROOT=Path(__file__).resolve().parents[1]
START,END=0xa2,0x1203
HANDLERS={'create_contract_related_object':0x18c5,'create_two_contract_objects':0x18e4,
          'create_three_contract_objects':0x18ff,'create_contract_mode_four_object':0x192d}


def blocks():
    result=[(0xa2,b'\xff'),(0x1102,b'\0')]
    source=ROOT/'data/status/tables.ini'
    config=configparser.ConfigParser(); config.read(source,encoding='ascii')
    for t in document(config,source,asset,None)['tables']:
        result.append((t['start'],b''.join(v.to_bytes(t['width'],'little') for v in t['values'])))
    catalog = ROOT/'data/text/status/catalog.ini'
    if not (catalog.parent / 'strings.tsv').is_file():
        parsed = configparser.ConfigParser(); parsed.read(catalog, encoding='ascii')
        start, end = int(parsed['strings']['directory_offset'], 0), int(parsed['strings']['end_offset'], 0)
        result.append((start, bytes(end - start)))
    else:
        text=json.loads(Path(compile_resource('strings',catalog)).read_text())
        entries=[bytes(e['bytes'])+b'\0' for e in text['entries']]
        relative=2*len(entries); offsets=[]
        for entry in entries: offsets.append(relative); relative+=len(entry)
        blob=struct.pack(f'<{len(offsets)}H',*offsets)+b''.join(entries)
        if text['directory_offset']+len(blob)!=text['end_offset']: raise ValueError('status text layout changed')
        result.append((text['directory_offset'],blob))
    callbacks=load_callbacks(compile_resource('contract_callbacks',ROOT/'data/status/contract_callbacks.ini'))
    result.append((callbacks['start'],b''.join(struct.pack('<HH',row['threshold'],
        HANDLERS[row['callback']] if row['callback'] else 0) for row in callbacks['subtypes'])))
    result.sort()
    if result[0][0]!=START or result[-1][0]+len(result[-1][1])!=END or any(
            a[0]+len(a[1])!=b[0] for a,b in zip(result,result[1:])):
        raise ValueError('status resources must cover 00A2h..1202h exactly')
    return result

"""Compile chart name/text/descriptor/query-offset resources from TSV/CSV."""
import configparser
import csv
from pathlib import Path
import struct
from resource_sources import asset
from string_resources import document
ROOT=Path(__file__).resolve().parents[1]
def resources():
    source=ROOT/'data/text/chart/catalog.ini'; config=configparser.ConfigParser(); config.read(source,encoding='ascii'); text=document(config,source,asset)
    if (text['directory_offset'],text['end_offset'],len(text['entries']))!=(0x504,0x1b59,261): raise ValueError('chart text layout changed')
    relative=522; offsets=[]; strings=bytearray()
    for entry in text['entries']:
        value=bytes(entry['bytes'])+b'\0'; offsets.append(relative); strings.extend(value); relative+=len(value)
    if 0x504+relative!=0x1b59: raise ValueError('chart text end changed')
    rows=[(0x504,struct.pack('<261H',*offsets)+strings)]
    root=ROOT/'data/maps'; config=configparser.ConfigParser(); config.read(root/'tables.ini',encoding='ascii')
    for name in ('system_name_syllables','text_catalog_padding','system_descriptor_values','map_projection_records'):
        entry=config[name]; start,end=int(entry['start'],0),int(entry['end'],0)
        if entry['type']=='fixed_text':
            slots=(root/entry['file']).read_text(encoding='ascii').splitlines()
            if len(slots)!=32 or any(len(s.encode('ascii'))>=4 for s in slots): raise ValueError('chart syllable shape changed')
            data=b''.join(s.encode('ascii').ljust(4,b'\0') for s in slots)
        elif entry['type']=='candidate_offsets':
            from map_authoring import candidate_offsets
            numbers=candidate_offsets(root/entry['file'])
            data=b''.join(value.to_bytes(2,'little') for value in numbers)
        else:
            from resource_sources import csv_integers
            width={'u8':1,'u16':2,'u32':4}[entry['type']]
            data=b''.join(value.to_bytes(width,'little') for value in csv_integers(root/entry['file']))
        if len(data)!=end-start: raise ValueError(f'chart query resource size changed: {name}')
        rows.append((start,bytes(data)))
    return rows
def write_header(path):
    rows=resources(); lines=['/* Generated from native chart query/name TSV/CSV; do not edit. */']
    for i,(start,data) in enumerate(rows): lines.append('static const uint8_t query_table_'+str(i)+'[]={'+','.join(map(str,data))+'};')
    lines.append('static const struct { uint16_t offset,length; const uint8_t *bytes; } query_tables[]={')
    for i,(start,data) in enumerate(rows): lines.append('{'+str(start)+','+str(len(data))+',query_table_'+str(i)+'},')
    lines.append('};'); path.parent.mkdir(parents=True,exist_ok=True); path.write_text('\n'.join(lines)+'\n',encoding='ascii')

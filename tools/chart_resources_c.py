"""Compile the remaining chart view tables directly from native CSV/GPL."""
import configparser
import csv
from pathlib import Path
from system_query_resources_c import resources as queries
from sector_resources_c import resources as sectors
from resource_sources import read_palette
from map_authoring import WIDTHS, values as map_values
ROOT=Path(__file__).resolve().parents[1]

def resources():
    root=ROOT/'data/maps'
    config=configparser.ConfigParser(); config.read(root/'tables.ini',encoding='ascii')
    rows=queries()+sectors()
    starts={start for start,_ in rows}
    for name in config.sections():
        entry=config[name]; start,end=int(entry['start'],0),int(entry['end'],0)
        if start in starts: continue
        path=root/entry['file']
        if entry['type']=='rgb6':
            data=bytes(v for rgb in read_palette(path) for v in rgb)
        elif entry['type']=='fixed_text':
            slot=entry.getint('slot_bytes')
            chunks=[]
            for text in path.read_text(encoding='ascii').splitlines():
                raw=text.encode('ascii')
                if not raw or len(raw)>=slot or any(not 32<=b<127 for b in raw):
                    raise ValueError('fixed text must be printable ASCII with room for NUL')
                chunks.append(raw+bytes(slot-len(raw)))
            data=b''.join(chunks)
        elif entry['type'] in WIDTHS:
            data=b''.join(v.to_bytes(WIDTHS[entry['type']],'little') for v in map_values(entry['type'], path))
        elif entry['type']=='sprite_id':
            from sprite_names import parse_sprite
            values=[parse_sprite(v.strip()) for row in csv.reader(path.read_text().splitlines()) for v in row if v.strip()]
            data=b''.join(v.to_bytes(2,'little') for v in values)
        else:
            width={'u8':1,'u16':2,'u32':4}[entry['type']]
            from resource_sources import csv_integers
            values=csv_integers(path)
            data=b''.join(v.to_bytes(width,'little') for v in values)
        if len(data)!=end-start: raise ValueError(f'chart resource layout changed: {name}')
        rows.append((start,data))
    return sorted(rows)

def write_header(path):
    rows=resources(); lines=['/* Generated from native chart CSV/GPL/TSV; do not edit. */']
    for i,(start,data) in enumerate(rows): lines.append('static const uint8_t map_table_'+str(i)+'[]={'+','.join(map(str,data))+'};')
    lines.append('static const struct { uint16_t offset,length; const uint8_t *bytes; } map_tables[]={')
    for i,(start,data) in enumerate(rows): lines.append('{'+str(start)+','+str(len(data))+',map_table_'+str(i)+'},')
    lines.extend(['};','']); path.parent.mkdir(parents=True,exist_ok=True); path.write_text('\n'.join(lines),encoding='ascii')

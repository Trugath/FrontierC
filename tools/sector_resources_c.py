"""Compile the native chart catalog/generator/density tables directly from CSV."""
import configparser
import csv
from pathlib import Path
from map_authoring import WIDTHS, values as map_values
ROOT=Path(__file__).resolve().parents[1]
NAMES=('catalog_sector_coordinates','catalog_coordinate_tail','catalog_system_ranges',
       'catalog_system_records','catalog_system_identifiers','sector_sample_weights',
       'sector_count_weights','generated_identifier_low','generated_identifier_high','catalog_extra_draw_kinds')
def resources():
    root=ROOT/'data/maps'; config=configparser.ConfigParser(); config.read(root/'tables.ini',encoding='ascii'); result=[]
    for name in NAMES:
        row=config[name]; start,end=int(row['start'],0),int(row['end'],0)
        if row['type'] in WIDTHS:
            width=WIDTHS[row['type']]
            values=map_values(row['type'], root/row['file'])
        else:
            width={'u8':1,'u16':2}[row['type']]
            from resource_sources import csv_integers
            values=csv_integers(root/row['file'])
        data=b''.join(v.to_bytes(width,'little') for v in values)
        if len(data)!=end-start: raise ValueError(f'chart table size changed: {name}')
        result.append((start,data))
    return result
def write_header(path):
    rows=resources(); lines=['/* Generated from native chart CSV resources; do not edit. */']
    for i,(start,data) in enumerate(rows): lines.append('static const uint8_t sector_table_'+str(i)+'[]={'+','.join(map(str,data))+'};')
    lines.append('static const struct { uint16_t offset,length; const uint8_t *bytes; } sector_tables[]={')
    for i,(start,data) in enumerate(rows): lines.append('{'+str(start)+','+str(len(data))+',sector_table_'+str(i)+'},')
    lines.append('};'); path.parent.mkdir(parents=True,exist_ok=True); path.write_text('\n'.join(lines)+'\n',encoding='ascii')

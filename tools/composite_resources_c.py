"""Compile named composite placements, selectors and palette views for C."""
import configparser
from pathlib import Path

from composite_commands import tables
from module_tables import document
from resource_sources import asset,read_palette

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'data/composites/tables.ini'
PALETTE=ROOT/'data/palettes/composites.gpl'


def resources():
    config=configparser.ConfigParser()
    config.read(SOURCE,encoding='ascii')
    validated=document(config,SOURCE,asset,read_palette)
    data=tables()
    if set(data)!=set(config.sections()): raise ValueError('composites table directory differs from INI')
    bank_len=1488
    counts={'overlay_part_coordinates':3,'overlay_part_offsets':3,
        'base_palette_pointers':8,'feature_palette_pointers':16,
        'detail_palette_pointers':33,'secondary_palette_pointers':8,'override_palette_pointers':8}
    for style in ('style_a','style_b'):
        counts[style+'_coordinates']=44
        for group in range(6): counts[f'{style}_part_group_{group}']=4
        counts[style+'_detail_parts']=16
        counts[style+'_optional_parts']=8
    if set(counts)!=set(data): raise ValueError('composites expected table set changed')
    for name,rows in data.items():
        coordinate=name.endswith('_coordinates')
        if len(rows)!=counts[name] or any(len(row)!=(2 if coordinate else 1) for row in rows):
            raise ValueError('composites table shape changed: '+name)
        if coordinate:
            if any(not -128<=v<=127 for row in rows for v in row): raise ValueError(name)
        elif name.endswith('_palette_pointers'):
            colors=88 if name=='base_palette_pointers' else 8
            if any(v<0xb0 or (v-0xb0)%3 or v-0xb0+colors*3>bank_len for (v,) in rows):
                raise ValueError('Palette pointer outside bank: '+name)
        else:
            limit=3 if name=='overlay_part_offsets' else 44
            if any(v%4 or not 0<=v<limit*4 for (v,) in rows): raise ValueError(name)
    return data,validated


def write_header(path):
    data,validated=resources()
    lines=['/* Generated from native composites CSV resources; the palette is loaded from frontier.dat. */',
           '#include <stddef.h>',
           'const uint8_t *composite_rgb6;',
           'size_t composite_rgb6_bytes;']
    for name,rows in data.items():
        if name.endswith('_coordinates'):
            lines.append(f'static const int16_t {name}[][2] = {{')
            lines.extend('    {'+', '.join(map(str,row))+'},' for row in rows)
        else:
            lines.append(f'static const uint16_t {name}[] = {{')
            lines.append('    '+', '.join(str(row[0]) for row in rows)+',')
        lines.append('};')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/resources/composite_data.h')
    write_header(parser.parse_args().output)

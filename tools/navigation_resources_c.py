"""Compile native navigation navigation text/tables (TXT/CSV/INI) into the C resource image."""
import configparser
from pathlib import Path
import struct

from module_tables import document
from resource_sources import asset

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'data/navigation/tables.ini'
START,END=0xa2,0x10c
DIRECTORY=0xa3
# Byte at 010Bh is the RETF following the marker shape; radius index 18 reads it.
RADIUS_ALIAS=b'\xcb'


def resources():
    config=configparser.ConfigParser(); config.read(SOURCE,encoding='ascii')
    tables={t['label']:t for t in document(config,SOURCE,asset,None)['tables']}
    def raw(label):
        table=tables[label]
        return b''.join(v.to_bytes(table['width'],'little') for v in table['values'])
    image=(b'\xff'+struct.pack('<2H',tables['time_point_format']['start']-DIRECTORY,
                               tables['target_prompt_text']['start']-DIRECTORY)+
           raw('time_point_format')+raw('target_prompt_stream')+b'\0'+raw('target_prompt_text')+
           raw('drawing_parameters')+raw('marker_shape')+RADIUS_ALIAS)
    layout=[(label,tables[label]['start']) for label in ('time_point_format','target_prompt_stream',
        'target_prompt_text','drawing_parameters','marker_shape')]
    if layout!=[('time_point_format',0xa7),('target_prompt_stream',0xb6),('target_prompt_text',0xbb),
            ('drawing_parameters',0xe9),('marker_shape',0xf7)] or START+len(image)!=END:
        raise ValueError('navigation resource layout changed')
    return image

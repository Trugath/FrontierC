"""Compile complete commander native text/preferences/filename resources for C."""
import configparser
from pathlib import Path
import struct
from string_resources import document
from resource_sources import asset
ROOT=Path(__file__).resolve().parents[1]

def resources():
    source=ROOT/'data/text/commander/catalog.ini'
    if not (source.parent / 'strings.tsv').is_file():
        blob=bytearray(b'\xff'+bytes(0x917-0xa3))
        offsets, lengths = [], []
    else:
        config=configparser.ConfigParser(); config.read(source,encoding='ascii')
        data=document(config,source,asset)
        if (data['module'],data['directory_offset'],data['end_offset'],len(data['entries']))!=('commander',0xa3,0x917,27): raise ValueError('Commander text layout changed')
        relative=54; offsets=[]; lengths=[]; strings=bytearray()
        for entry in data['entries']:
            value=bytes(entry['bytes'])+bytes(1); offsets.append(relative); lengths.append(len(value)); strings.extend(value); relative+=len(value)
        blob=bytearray(b'\xff'+struct.pack('<27H',*offsets)+strings)
    from preference_authoring import preference_bytes, widget_words, detail_presets, filename_words
    directory = ROOT / 'data/preferences'
    def table(values, width):
        return b''.join(v.to_bytes(width, 'little') for v in values)
    def add(start,value):
        if 0xa2+len(blob)!=start: raise ValueError(f'commander resource gap at {start:x}')
        blob.extend(value)
    add(0x917, table(preference_bytes(directory / 'default_preferences.csv'), 1))
    add(0x938, table(widget_words(directory / 'preference_widgets.csv'), 2)); add(0xa6e, b'\xff\xff')
    add(0xa70, table(detail_presets(directory / 'shape_detail_presets.csv'), 4)); add(0xa80, b'COMMANDR\0' + bytes(2 + 4 + 128))
    add(0xb0f, table(filename_words(directory / 'filename_edit_widgets.csv'), 2))
    add(0xb23,struct.pack('<5H',65535,0x893e,0x8936,0x8937,0x8938)+b'Jameson\0._01\0'+bytes([92])+b'(Parent Dir)\0'+bytes(6))
    if 0xa2+len(blob)!=0xb4e: raise ValueError('commander resource end changed')
    return bytes(blob),offsets,lengths

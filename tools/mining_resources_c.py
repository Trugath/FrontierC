"""Compile complete mining native text/parameters directly from TSV/CSV."""
import configparser
from pathlib import Path
import struct
from mining_parameters_c import resources as parameters
from string_resources import document
from resource_sources import asset
ROOT=Path(__file__).resolve().parents[1]
def resources():
    source=ROOT/'data/text/mining/catalog.ini'
    prose = source.parent / 'strings.tsv'
    if not prose.is_file():
        return b'\xff'+parameters()+bytes(0x53e-0x14e)
    config=configparser.ConfigParser(); config.read(source,encoding='ascii'); data=document(config,source,asset)
    if (data['module'],data['directory_offset'],data['end_offset'],len(data['entries']))!=('mining',0x14e,0x53e,30): raise ValueError('Mining text layout changed')
    relative=60; offsets=[]; strings=bytearray()
    for entry in data['entries']:
        value=bytes(entry['bytes'])+b'\0'; offsets.append(relative); strings.extend(value); relative+=len(value)
    if 0x14e+relative!=0x53e: raise ValueError('mining resource size changed')
    return b'\xff'+parameters()+struct.pack('<30H',*offsets)+strings

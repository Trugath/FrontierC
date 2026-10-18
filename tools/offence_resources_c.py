"""Compile offences native offence text, ownership challenges and parameter rows."""
import configparser
from pathlib import Path
import struct
from string_resources import document
from resource_sources import asset
from ownership_challenges import document as challenges
ROOT=Path(__file__).resolve().parents[1]
def resources():
    source=ROOT/'data/text/offences/catalog.ini'
    if not (source.parent / 'strings.tsv').is_file():
        blob=bytearray(b'\xff'+bytes(0x210-0xa3)+b'\0')
    else:
        config=configparser.ConfigParser(); config.read(source,encoding='ascii')
        data=document(config,source,asset)
        if (data['module'],data['directory_offset'],data['end_offset'],len(data['entries']))!=('offences',0xa3,0x210,13): raise ValueError('Offence text layout changed')
        offsets=[]; relative=26; strings=bytearray()
        for entry in data['entries']:
            value=bytes(entry['bytes'])+b'\0'; offsets.append(relative); strings.extend(value); relative+=len(value)
        if 0xa3+relative!=0x210: raise ValueError('offences text end changed')
        blob=bytearray(b'\xff'+struct.pack('<13H',*offsets)+strings+b'\0')
    source=ROOT/'data/ownership/catalog.ini'; config=configparser.ConfigParser(); config.read(source,encoding='ascii')
    table=challenges(config,source,asset)['tables'][0]
    if (table['start'],table['end'],len(table['values']))!=(0x211,0x6c5,301*4): raise ValueError('offences ownership layout changed')
    blob.extend(bytes(table['values']))
    if 0xa2+len(blob)!=0x6c5: raise ValueError('offences ownership layout changed')
    from offence_parameters import words
    blob.extend(b''.join(v.to_bytes(2, 'little') for v in words(ROOT / 'data/offences/parameters.csv')))
    if 0xa2+len(blob)!=0x71d: raise ValueError('offences resource end changed')
    return bytes(blob)

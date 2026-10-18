"""Compile native intro intro resources (text, models, objects, tables, timeline) for C.

Produces CS image data blocks at their original offsets plus the scene
timeline. No assembly or original binary is read.
"""
import json
from pathlib import Path
import struct

from resource_sources import compile_resource

ROOT=Path(__file__).resolve().parents[1]
INTRO=ROOT/'data/intro'
IMAGE_END=0x8f8f
# Scene-selection code ends 18h bytes after the DIV continuation label.
CONTINUATION_TO_OBJECT=0x18
VECTORS=(0x62,[(0,0),(0xa2,1),(0,0),*[(0x1d7,1)]*4,(0x8ee7,1),(0x1d7,1),(0x1d7,1),(0x6ede,1),
              *[(0x1d7,1)]*4,(0,0)])


def _load(kind,path):
    return json.loads(Path(compile_resource(kind,path)).read_text(encoding='utf-8'))


def strings():
    data=_load('strings',ROOT/'data/text/intro/catalog.ini')
    entries=[bytes(e['bytes'])+b'\0' for e in data['entries']]
    relative=2*len(entries); directory=[]
    for entry in entries: directory.append(relative); relative+=len(entry)
    blob=struct.pack(f'<{len(directory)}H',*directory)+b''.join(entries)
    if data['directory_offset']+len(blob)!=data['end_offset']: raise ValueError('intro text layout changed')
    return data['directory_offset'],blob


def models(intro=None):
    from resource_sources import document
    intro = Path(intro) if intro else INTRO
    data=document('intro_models', intro/'models.ini')
    starts={m['name']:m['start'] for m in data['models']}
    blob=bytearray(struct.pack('<H',data['end']-data['start']))
    for name in data['directory']: blob+=struct.pack('<H',starts[name]-data['directory_offset'] if name else 0)
    for m in data['models']:
        vertices=16+len(m['extension']); classification=vertices+len(m['vertices'])
        program=classification+len(m['classification'])
        if data['start']+len(blob)!=m['start']: raise ValueError(f'{m["name"]} moved')
        blob+=struct.pack('<8H',program,vertices,len(m['vertices'])*16,classification,
                          len(m['classification'])+4,*m['header'])
        blob+=bytes(m['extension']+m['vertices']+m['classification']+m['program'])
    if data['start']+len(blob)!=data['end']: raise ValueError('intro model bank size changed')
    return data['start'],bytes(blob)


def objects(intro=None):
    from resource_sources import document
    intro = Path(intro) if intro else INTRO
    data=document('intro_objects', intro/'objects.ini')
    return {o['name']:(o['start'],struct.pack('<143H',*o['words'])) for o in data['objects']}


def tables():
    data=_load('module_tables',INTRO/'tables.ini')
    return [(t['start'],b''.join(v.to_bytes(t['width'],'little') for v in t['values'])) for t in data['tables']]


def timeline():
    return _load('intro_timeline',INTRO/'timeline.ini')


def blocks():
    """(offset, bytes) data blocks; segment words of relocated vectors are CS (patched by C)."""
    result=[(0xa2,b'\xff')]
    result+=tables()
    result.append(strings())
    result.append(models())
    result+=list(objects().values())
    result.sort()
    if any(a[0]+len(a[1])>b[0] for a,b in zip(result,result[1:])) or result[-1][0]+len(result[-1][1])>IMAGE_END:
        raise ValueError('intro data blocks overlap')
    return result


def resources():
    """Concatenated block bytes, for the build manifest hash."""
    return b''.join(offset.to_bytes(2,'little')+blob for offset,blob in blocks())

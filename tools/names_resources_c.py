"""Compile and validate names native font-byte strings/templates for C."""
from pathlib import Path

from names_generator import catalog, templates, maximum_output_bytes

ROOT=Path(__file__).resolve().parents[1]


def resource_data():
    entries=catalog()
    bases,ranges=templates()
    if len(bases)!=9 or len(ranges)!=9: raise ValueError('Expected nine template tables')
    for base,count in zip(bases,ranges):
        if not count or not 0<=base<base+count<=len(entries):
            raise ValueError('Template index outside catalogue')
    offsets,blob=[],bytearray()
    for index,entry in enumerate(entries):
        if any(v<32 or v>=253 for v in entry.replace(b'\xff\x30\x16',b'')):
            raise ValueError('Unrecovered name text grammar')
        if index<284 and b'\xff' in entry:
            raise ValueError('Component pools cannot contain template expansion controls')
        offsets.append(len(blob))
        blob.extend(entry+b'\0')
    bounds,component=maximum_output_bytes()
    if max(bounds.values())!=29 or component!=11:
        raise ValueError('Name capacity constants need updating')
    return entries,offsets,bytes(blob),bases,ranges


def write_header(path):
    bases,ranges=templates()
    if len(bases)!=9 or len(ranges)!=9: raise ValueError('Expected nine template tables')
    lines=['/* Generated from names template tables; the catalogue is loaded from frontier.dat. */',
           '#include <stddef.h>',
           'const uint8_t *fe2_names_directory;',
           'size_t fe2_names_directory_bytes;']
    for kind,name,values in (('uint16_t','name_bases',bases),('uint16_t','name_ranges',ranges)):
        lines.append(f'static const {kind} {name}[] = {{')
        for start in range(0,len(values),16):
            lines.append('    '+', '.join(str(v) for v in values[start:start+16])+',')
        lines.append('};')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'build/resources/names_data.h')
    write_header(parser.parse_args().output)

#!/usr/bin/env python3
"""Unpack an original Frontier install and pack the game's asset dat.

unpack-game reads your Frontier binaries into the editable data tree.
pack reads that tree and writes frontier.dat. Edit the files in between.
repack writes frontier.dat directly from a zip or folder of those binaries.
unpack reads a dat back into the record folder frontier-data.exe also packs.
"""
import argparse
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))

from build_native_viewer import compile_assets
from extract_assets import InstallError, unpack_game

MAGIC = b'FE2DATA1'


def records_from_assets(compiled=None, data_root=None):
    compiled = compile_assets(data_root) if compiled is None else compiled
    records = {
        'font': compiled['font'],
        'palette': compiled['palette'],
        'widths': compiled['widths'],
        'text/builtin': compiled['builtin'],
    }
    info = bytearray()
    info += _text_entry('builtin', 2, 0, 0, 0)
    for text in compiled['texts']:
        records[f'text/{text["name"]}'] = text['data']
        info += _text_entry(text['name'], text['entries'], text['kind'], text['slot'], text['mask'])
    records['text-info'] = bytes(info)
    for bank, image in enumerate(compiled['banks']):
        records[f'bank/{bank}'] = image
        offsets = [0] * 240
        for row in compiled['models']:
            if row['bank'] == bank:
                offsets[row['slot']] = row['offset']
                records[f'model/{bank}/{row["slot"]}'] = row['data']
        records[f'model-offsets/{bank}'] = struct.pack('<' + 'H' * 240, *offsets)
    for ident, data in compiled['graphics']:
        records[f'graphic/{ident}'] = data
    root = Path(data_root) if data_root else ROOT / 'data'
    for name in ('hallking', 'elitet', 'babayaga', 'barem', 'jupalt', 'valkries', 'bluedan', 'ggofkiev', 'intro'):
        path = root / 'music' / f'{name}.mid'
        data = path.read_bytes()
        if not data.startswith(b'MThd'):
            raise ValueError(f'{path} is not a MIDI file')
        records[f'music/{name}'] = data
    from adlib_tables_c import image as adlib_image
    from galaxy_density import read_pgm
    from intro_resources_c import models, objects
    from resource_sources import read_palette
    records['adlib/image'] = adlib_image(root / 'adlib')
    records['galaxy/density'] = read_pgm(root / 'galaxy' / 'density.pgm') + b'\0\0'
    colors = read_palette(root / 'palettes' / 'composites.gpl')
    palette = bytes(channel for rgb in colors for channel in rgb)
    if len(palette) != 1488:
        raise ValueError('Composite palette must be 496 RGB6 triplets')
    records['composites'] = palette
    intro = root / 'intro'
    records['intro/models'] = models(intro)[1]
    compiled_objects = objects(intro)
    records['intro/orbit'] = compiled_objects['initial_scene_object'][1]
    records['intro/scenes'] = b''.join(compiled_objects[f'intro_scene_object_{index:02d}'][1] for index in range(22))
    from market_resources_c import resources as market_resources
    from systems_resources_c import blocks as systems_blocks
    from catalogue_tables_c import records as catalogue_records
    import struct as catalogue_struct
    records['table/market'] = market_resources(root)
    records['table/systems'] = b''.join(blob for _start, blob in systems_blocks())
    catalogue = bytearray()
    for row in catalogue_records():
        catalogue += catalogue_struct.pack('<I', row['key'])
        catalogue += bytes(row['metadata'])
        catalogue += bytes(row['payload'])
        catalogue += bytes((row['packed'],))
        catalogue += catalogue_struct.pack('<HH', *row['responses'])
    records['table/catalogue'] = bytes(catalogue)
    from input_tables_c import translation as input_translation
    from pointer_tables_c import image as pointer_image
    from pool_tables_c import table as pool_table
    from motion_tables_c import table as motion_table
    from contact_tables_c import table as contact_table
    from position_tables_c import table as position_table
    from objects_tables_c import preamble as objects_preamble
    from scene_probe_tables_c import table as probe_table
    records['table/input'] = input_translation()
    records['table/pointer'] = pointer_image()
    records['table/pool'] = pool_table()
    records['table/motion'] = motion_table()
    records['table/contact'] = contact_table()
    records['table/position'] = position_table()
    records['table/objects'] = objects_preamble()
    records['table/probe'] = probe_table()
    from people_tables_c import table_blob as people_blob
    from panels_tables_c import image as panels_image
    from intro_tables_c import record as intro_record
    records['table/people'] = people_blob()
    records['table/panels'] = panels_image()
    records['table/intro'] = intro_record()
    from navigation_resources_c import resources as navigation_resources
    records['table/navigation'] = navigation_resources()
    from render_tables_c import record as render_record
    records['table/render'] = render_record()
    from offences_tables_c import record as offences_record
    from mining_tables_c import record as mining_record
    records['table/offences'] = offences_record()
    records['table/mining'] = mining_record()
    from commander_tables_c import record as commander_record
    records['table/commander'] = commander_record()
    return records


def _text_entry(name, entries, kind, slot, mask):
    encoded = name.encode('ascii')
    if not encoded or len(encoded) > 255:
        raise ValueError(f'Bad text record name {name}')
    return bytes((len(encoded),)) + encoded + struct.pack('<HHHH', entries, kind, slot, mask)


def write_archive(records, path):
    path = Path(path)
    payload = bytearray(MAGIC)
    names = sorted(records)
    payload += struct.pack('<I', len(names))
    for name in names:
        encoded = name.encode('ascii')
        data = records[name]
        if not encoded or len(encoded) > 255 or any(c in name for c in '\\:') or name.startswith('/') or '..' in name.split('/'):
            raise ValueError(f'Bad asset name {name}')
        payload += bytes((len(encoded),)) + encoded + struct.pack('<I', len(data)) + data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return path


def read_archive(path):
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:8] != MAGIC:
        raise ValueError('Not a Frontier asset dat')
    count = struct.unpack_from('<I', data, 8)[0]
    records, at = {}, 12
    for _ in range(count):
        if at >= len(data):
            raise ValueError('Truncated asset dat')
        name_len = data[at]
        at += 1
        if name_len == 0 or at + name_len + 4 > len(data):
            raise ValueError('Truncated asset name')
        name = data[at:at + name_len].decode('ascii')
        at += name_len
        size = struct.unpack_from('<I', data, at)[0]
        at += 4
        if at + size > len(data):
            raise ValueError(f'Truncated asset {name}')
        records[name] = data[at:at + size]
        at += size
    if at != len(data):
        raise ValueError('Unexpected bytes after the asset dat')
    if len(records) != count:
        raise ValueError('Duplicate asset name')
    return records


def write_tree(records, directory):
    directory = Path(directory)
    lines = []
    for name in sorted(records):
        target = directory / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(records[name])
        lines.append(name)
    (directory / 'assets.txt').write_bytes(('\n'.join(lines) + '\n').encode('ascii'))
    return directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    unpack_original = commands.add_parser('unpack-game', help='extract a Frontier zip or folder into the editable data tree')
    unpack_original.add_argument('source', type=Path, help='zip of Frontier binaries, or a folder of those files')
    unpack_original.add_argument('--data', type=Path, default=ROOT / 'data')
    pack = commands.add_parser('pack', help='pack the editable data tree into frontier.dat')
    pack.add_argument('--data', type=Path, default=ROOT / 'data')
    pack.add_argument('--output', type=Path, default=ROOT / 'build' / 'game' / 'frontier.dat')
    unpack = commands.add_parser('unpack', help='unpack a dat into the record folder the data executable packs')
    unpack.add_argument('dat', type=Path)
    unpack.add_argument('directory', type=Path)
    repack = commands.add_parser('repack', help='write frontier.dat directly from a Frontier zip or folder')
    repack.add_argument('source', type=Path, help='zip of Frontier binaries, or a folder of those files')
    repack.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'unpack-game':
        try:
            result = unpack_game(args.source, args.data)
        except (InstallError, ValueError) as error:
            print(error if isinstance(error, InstallError) else f'Unable to extract this Frontier install: {error}',
                  file=sys.stderr)
            return 2
        print(f'Unpacked {result["sprites"]} sprites, {result["main_models"] + result["auxiliary_models"] + result["intro_models"]} models, '
              f'{result["glyphs"]} glyphs and {len(result["music"])} scores into {args.data}')
    elif args.command == 'pack':
        print(write_archive(records_from_assets(data_root=args.data), args.output))
    elif args.command == 'repack':
        with tempfile.TemporaryDirectory() as temporary:
            try:
                unpack_game(args.source, Path(temporary))
            except (InstallError, ValueError) as error:
                print(error if isinstance(error, InstallError) else f'Unable to extract this Frontier install: {error}',
                      file=sys.stderr)
                return 2
            print(write_archive(records_from_assets(data_root=Path(temporary)), args.output))
    else:
        print(write_tree(read_archive(args.dat), args.directory))


if __name__ == '__main__':
    main()

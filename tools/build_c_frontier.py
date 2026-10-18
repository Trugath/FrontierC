"""Build a native frontier (main executable) subsystem with the math
library: build_library(directory, compiler, subsystem)."""
from compiler import zig, compile_object
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

import contact_tables_c
import input_tables_c
import math3_tables_c
import motion_tables_c
import objects_tables_c
import pointer_tables_c
import pool_tables_c
import scene_probe_tables_c
import position_tables_c
import render_tables_c

ROOT = Path(__file__).resolve().parents[1]
# FE2_FRONTIER_SOURCE builds from another copy of the sources (mutation runs).
FRONTIER = Path(os.environ.get('FE2_FRONTIER_SOURCE', ROOT / 'src/engine'))
TABLES = {'pool': (pool_tables_c, 'pool_tables.h'), 'motion': (motion_tables_c, 'motion_tables.h'),
          'position': (position_tables_c, 'position_tables.h'), 'contact': (contact_tables_c, 'contact_tables.h'),
          'scene_probe': (scene_probe_tables_c, 'scene_probe_tables.h'), 'render': (render_tables_c, 'render_tables.h'),
          'objects': (objects_tables_c, 'objects_tables.h'), 'pointer': (pointer_tables_c, 'pointer_tables.h'),
          'input': (input_tables_c, 'input_tables.h')}
# The subsystems each one calls into.
DEPENDS = {'motion': ['pool'], 'position': ['motion', 'pool'], 'contact': ['motion', 'pool'],
           'scene_probe': ['motion', 'pool'], 'session': ['pool', 'objects', 'pointer', 'input'], 'input': ['pointer']}


# Gameplay areas under src/game. Several former modules share a directory, so
# their generated tables share one include directory and use distinct names.
AREAS = [
    ('world', [
        ('systems_tables_c', 'write_tables', 'system_tables_data.h'),
        ('catalogue_tables_c', 'write_tables', 'catalogue_tables.h'),
        ('names_resources_c', 'write_header', 'names_data.h'),
        ('hyperspace_tables_c', 'write_tables', 'hyperspace_tables.h'),
        ('galaxy_density_c', 'write_header', 'galaxy_density_data.h'),
    ]),
    ('flight', [
        ('flight_tables_c', 'write_tables', 'flight_tables_data.h'),
        ('panels_tables_c', 'write_tables', 'panel_tables_data.h'),
        ('navigation_tables_c', 'write_tables', 'navigation_tables_data.h'),
    ]),
    ('chart', [
        ('chart_tables_c', 'write_tables', 'galactic_map_tables_data.h'),
        ('galaxy_density_c', 'write_header', 'galaxy_density_data.h'),
    ]),
    ('station', [
        ('market_tables_c', 'write_tables', 'market_tables_data.h'),
        ('station_tables_c', 'write_tables', 'station_tables_data.h'),
        ('interaction_text_c', 'write_tables', 'interaction_tables.h'),
    ]),
    ('people', [
        ('people_tables_c', 'write_tables', 'people_tables_data.h'),
        ('offences_tables_c', 'write_tables', 'offence_tables.h'),
    ]),
    ('mining', [('mining_tables_c', 'write_tables', 'mining_tables.h')]),
    ('commander', [
        ('commander_tables_c', 'write_tables', 'commander_tables.h'),
        ('status_tables_c', 'write_tables', 'status_tables_data.h'),
    ]),
    ('intro', [
        ('intro_tables_c', 'write_tables', 'intro_tables_data.h'),
        ('composite_resources_c', 'write_header', 'composite_data.h'),
    ]),
]


def overlay_objects(directory, compiler, flags, done):
    """Gameplay objects. Generated tables share one include directory."""
    import importlib
    tables = directory / 'generated'
    tables.mkdir(parents=True, exist_ok=True)
    objects = []
    for area, writers in AREAS:
        for name, function, file in writers:
            getattr(importlib.import_module(name), function)(tables / file)
        folder = ROOT / 'src/game' / area
        for source in sorted(folder.glob('*.c')):
            if source.resolve() in done:
                continue
            done.add(source.resolve())
            obj = directory / (area + '_' + source.stem + '.o')
            compile_object(compiler, flags,
                           [tables, folder, FRONTIER, FRONTIER / 'game', ROOT / 'src/game'], source, obj)
            objects.append(obj)
    return objects


def build_library(directory, compiler, subsystem, target=None):
    """`target` is a zig target triple to cross-compile for (default: the host)."""
    directory = directory.resolve()
    compiler = compiler.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    math3_tables_c.write_tables(directory / 'math3_tables.c')
    parts = [subsystem] + DEPENDS.get(subsystem, [])
    for part in parts:
        generator, name = TABLES.get(part, (None, None))
        if generator: generator.write_tables(directory / name)
    generator = TABLES.get(subsystem, (None, None))[0]
    flags = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic'] + (['-target', target] if target else [])
    sources = [s for part in parts for s in sorted((FRONTIER / part).glob('*.c'))] + [FRONTIER / 'game/call_args.c'] + \
        sorted((FRONTIER / 'math').glob('*.c')) + \
        [directory / 'math3_tables.c']
    includes = [directory, FRONTIER, *(FRONTIER / part for part in parts), ROOT / 'src/game', FRONTIER / 'math',
                FRONTIER / 'game']
    objects = []
    for source in sources:
        obj = directory / (source.parent.name + '_' + source.stem + '.o')
        compile_object(compiler, flags, includes, source, obj)
        objects.append(obj)
    if subsystem == 'session':
        objects += overlay_objects(directory / 'overlays', compiler, flags, {s.resolve() for s in sources})
        sources += [source for area, _ in AREAS for source in sorted((ROOT / 'src/game' / area).glob('*.c'))]
    library = directory / f'libfe2{subsystem}.a'
    library.unlink(missing_ok=True)
    subprocess.run([str(compiler), 'ar', 'rcs', str(library), *map(str, objects)], check=True)
    manifest = {'module': f'frontier/{subsystem}', 'target': 'host-native C port', 'compiler': str(compiler),
                'flags': flags, 'sources': [str(s.relative_to(ROOT)) if s.is_relative_to(ROOT) else s.name for s in sources],
                'library': str(library),
                'tables_sha256': hashlib.sha256(generator.source().encode()).hexdigest() if generator else None,
                'boundary': 'typed state and services; no guest memory or registers'}
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return library


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('subsystem')
    parser.add_argument('--zig', type=Path, default=zig())
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(build_library(args.output or ROOT / 'build/c/frontier' / args.subsystem, args.zig, args.subsystem))

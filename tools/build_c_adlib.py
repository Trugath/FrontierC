"""Build the adlib driver C port."""
from compiler import zig, compile_object
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil

from adlib_tables_c import ROOT, write_tables

SOURCES = sorted((ROOT / 'src/engine/audio/adlib').glob('*.c'))


def build_library(directory, compiler):
    directory = directory.resolve()
    compiler = compiler.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'LICENSE', directory / 'LICENSE.txt')
    write_tables(directory / 'adlib_image.h')
    flags = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic']
    objects = []
    for source in SOURCES:
        obj = directory / (source.stem + '.o')
        compile_object(compiler, flags, [directory, ROOT / 'src/engine/audio/adlib'], source, obj)
        objects.append(obj)
    library = directory / 'libfe2adlib.a'
    library.unlink(missing_ok=True)
    subprocess.run([str(compiler), 'ar', 'rcs', str(library), *map(str, objects)], check=True)
    manifest = {'module': 'adlib', 'target': 'host-native C port',
                'compiler': str(compiler), 'compiler_version': subprocess.check_output(
                    [str(compiler), 'version'], text=True).strip(), 'flags': flags,
                'sources': [str(s.relative_to(ROOT)) for s in SOURCES], 'library': str(library),
                'tables': 'ambient offsets from data/adlib/bank.ini; the image is a frontier.dat record',
                'tables_sha256': hashlib.sha256((directory / 'adlib_image.h').read_bytes()).hexdigest(),
                'boundary': 'fe2_adlib_game callbacks; OPL writes are the hardware side'}
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return library


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', type=Path, default=zig())
    parser.add_argument('--output', type=Path, default=ROOT / 'build/c/adlib')
    args = parser.parse_args()
    print(build_library(args.output, args.zig))

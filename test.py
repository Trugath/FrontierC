#!/usr/bin/env python3
"""Run independent Python resource tests and native C behavior tests."""
import argparse
from pathlib import Path
import subprocess
import sys
import unittest
import json

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python-only', action='store_true')
    parser.add_argument('--integration', action='store_true', help='also build and exercise the complete game')
    args = parser.parse_args()
    if args.python_only and args.integration:
        parser.error("--python-only and --integration are mutually exclusive")
    suite = unittest.defaultTestLoader.discover(str(ROOT / 'tests'))
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
        return 1
    if args.python_only:
        return 0
    from compiler import windows_executable_inputs, zig
    import catalogue_tables_c
    import math3_tables_c
    import input_tables_c
    import pointer_tables_c
    import mining_tables_c
    directory = ROOT / 'build/tests'
    directory.mkdir(parents=True, exist_ok=True)
    catalogue_tables_c.write_tables(directory / 'catalogue_tables.h')
    import struct
    catalogue_blob = bytearray()
    for row in catalogue_tables_c.records():
        catalogue_blob += struct.pack('<I', row['key'])
        catalogue_blob += bytes(row['metadata'])
        catalogue_blob += bytes(row['payload'])
        catalogue_blob += bytes((row['packed'],))
        catalogue_blob += struct.pack('<HH', *row['responses'])
    folded = ',\n'.join('    ' + ', '.join(f'0x{byte:02x}' for byte in catalogue_blob[index:index + 16])
                        for index in range(0, len(catalogue_blob), 16))
    (directory / 'catalogue_blob.h').write_text(
        '/* Catalogue bytes for the native tests. The game reads these from frontier.dat. */\n'
        f'static const uint8_t fe2_test_catalogue[] = {{\n{folded}\n}};\n', encoding='ascii')
    mining_tables_c.write_tables(directory / 'mining_tables.h')
    math3_tables_c.write_tables(directory / 'math3_tables.c')
    input_tables_c.write_tables(directory / 'input_tables.h')
    pointer_tables_c.write_tables(directory / 'pointer_tables.h')

    def write_blob(path, symbol, data):
        folded_blob = ',\n'.join('    ' + ', '.join(f'0x{byte:02x}' for byte in data[index:index + 16])
                                 for index in range(0, len(data), 16))
        path.write_text(
            f'static const uint8_t {symbol}[] = {{\n{folded_blob}\n}};\n', encoding='ascii')

    write_blob(directory / 'input_blob.h', 'fe2_test_input', input_tables_c.translation())
    write_blob(directory / 'pointer_blob.h', 'fe2_test_pointer', pointer_tables_c.image())
    write_blob(directory / 'mining_blob.h', 'fe2_test_mining', mining_tables_c.record())
    sources = [ROOT / 'tests/native_tests.c', ROOT / 'src/engine/game/call_args.c',
               ROOT / 'src/game/world/system_lookup.c', ROOT / 'src/game/world/system_record.c', ROOT / 'src/game/world/catalogue_tables.c',
               directory / 'math3_tables.c']
    sources += sorted((ROOT / 'src/game/mining').glob('*.c'))
    sources += sorted((ROOT / 'src/engine/math').glob('*.c'))
    sources += sorted((ROOT / 'src/engine/input').glob('*.c'))
    sources += sorted((ROOT / 'src/engine/pointer').glob('*.c'))
    exe = directory / ('native-tests.exe' if sys.platform == 'win32' else 'native-tests')
    subprocess.run([str(zig()), 'cc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra',
                    '-Werror', '-pedantic', '-UNDEBUG', '-I' + str(ROOT / 'src/engine'),
                    '-I' + str(ROOT / 'src/game'),                     '-I' + str(ROOT / 'src/engine/math'), '-I' + str(directory),
                    *map(str, sources), *windows_executable_inputs(zig(), directory),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    import optional_tables_c
    optional_tables_c.speaker(directory / 'speaker_tables.h')
    optional_tables_c.setup(directory / 'setup_tables.h')
    embed_midi = all(path.is_file() for path in optional_tables_c.roland_scores())
    if embed_midi:
        optional_tables_c.roland(directory / 'roland_tables.h')
    else:
        print('Optional MIDI checks were not compiled. Unpack the Roland effect scores and music first.', flush=True)
    optional_sources = [ROOT / 'tests/native_optional_tests.c']
    for folder in ('speaker', 'roland'):
        optional_sources += sorted((ROOT / 'src/engine/audio' / folder).glob('*.c'))
    optional_sources += sorted((ROOT / 'src/game/setup').glob('*.c'))
    optional_exe = directory / ('optional-tests.exe' if sys.platform == 'win32' else 'optional-tests')
    windows_inputs = windows_executable_inputs(zig(), directory)
    subprocess.run([str(zig()), 'cc', '-std=c99', '-O1', '-g', '-UNDEBUG',
                    *(['-DFE2_ROLAND_EMBEDDED'] if embed_midi else []),
                    '-Wall', '-Wextra', '-Werror', '-pedantic', '-I' + str(ROOT / 'src/game'),
                    '-I' + str(ROOT / 'src/engine'), '-I' + str(directory),
                    *map(str, optional_sources), *windows_inputs,
                    '-o', str(optional_exe)], check=True)
    subprocess.run([str(optional_exe)], check=True)
    recorded = list(sources + optional_sources)
    recorded.extend(Path(path) for path in windows_inputs if str(path).endswith('.c'))
    (directory / 'native-sources.json').write_text(json.dumps({
        'sources': [str(path.relative_to(ROOT)) for path in recorded]
    }, indent=2) + '\n', encoding='utf-8')
    if args.integration:
        integration = unittest.TestLoader().discover(str(ROOT / 'tests/integration'))
        if not unittest.TextTestRunner(verbosity=2).run(integration).wasSuccessful():
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

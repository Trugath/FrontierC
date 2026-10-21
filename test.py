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
    def compile_native(name, sources, includes, defines=()):
        output = directory / (name + ('.exe' if sys.platform == 'win32' else ''))
        subprocess.run([str(zig()), 'cc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-UNDEBUG', *[f'-D{item}' for item in defines], *[f'-I{path}' for path in includes],
                        *map(str, sources), *windows_executable_inputs(zig(), directory),
                        '-o', str(output)], check=True)
        subprocess.run([str(output)], check=True)
        return sources

    sources = [ROOT / 'tests/native_tests.c', ROOT / 'src/engine/game/call_args.c',
               ROOT / 'src/game/world/system_lookup.c', ROOT / 'src/game/world/system_record.c',
               ROOT / 'src/game/world/catalogue_tables.c', directory / 'math3_tables.c']
    sources += sorted((ROOT / 'src/game/mining').glob('*.c'))
    sources += sorted((ROOT / 'src/engine/math').glob('*.c'))
    sources += sorted((ROOT / 'src/engine/input').glob('*.c'))
    sources += sorted((ROOT / 'src/engine/pointer').glob('*.c'))
    behavior_includes = [ROOT / 'src/engine', ROOT / 'src/game', ROOT / 'src/engine/math', directory]
    sources = compile_native('native-tests', sources, behavior_includes)
    import status_tables_c
    import market_tables_c
    from market_resources_c import resources as market_resources
    status_tables_c.write_tables(directory / 'status_tables_data.h')
    market_tables_c.write_tables(directory / 'market_tables_data.h')
    write_blob(directory / 'market_blob.h', 'fe2_test_market', market_resources())
    reported_sources = [
        ROOT / 'tests/reported_bugs.c', ROOT / 'src/engine/game/call_args.c',
        ROOT / 'src/game/commander/missions.c', ROOT / 'src/game/commander/military_rank.c',
        ROOT / 'src/game/commander/contracts.c', ROOT / 'src/game/commander/page_layout.c',
        ROOT / 'src/game/station/commodity_prices.c', ROOT / 'src/game/station/market_space.c',
        ROOT / 'src/game/station/market_tables.c', ROOT / 'src/game/world/drive_query.c']
    reported_sources = compile_native('reported-bugs', reported_sources,
                                      [ROOT / 'src/engine', ROOT / 'src/game', directory])
    import interaction_text_c
    interaction_text_c.write_tables(directory / 'interaction_tables.h')
    contract_sources = [
        ROOT / 'tests/native_contracts.c', ROOT / 'tests/native_contracts_equipment.c',
        ROOT / 'tests/native_contracts_conduct.c',
        ROOT / 'src/engine/game/call_args.c',
        ROOT / 'src/engine/math/contexts.c',
        ROOT / 'src/engine/pool/equipment_effects.c', ROOT / 'src/engine/pool/pool_visitors.c',
        ROOT / 'src/engine/render/texture.c', ROOT / 'src/engine/render/geometry.c',
        ROOT / 'src/engine/render/projection.c', ROOT / 'src/engine/render/scanlines.c',
        ROOT / 'src/engine/render/spans.c', ROOT / 'src/engine/render/clip.c',
        ROOT / 'src/game/people/delta_updates.c', ROOT / 'src/game/people/services.c',
        ROOT / 'src/game/station/interaction_text.c', ROOT / 'src/game/station/commodity_list.c',
        ROOT / 'src/game/station/charges.c', ROOT / 'src/game/station/conversation.c',
        ROOT / 'src/game/station/common.c',
        ROOT / 'src/engine/objects/propulsion.c', ROOT / 'src/engine/objects/random.c',
        ROOT / 'src/game/station/market_space.c', ROOT / 'src/game/station/market_update.c',
        ROOT / 'src/game/commander/mining_page.c', ROOT / 'src/game/commander/passenger_page.c',
        ROOT / 'src/game/commander/crew_page.c', ROOT / 'src/game/commander/mission_page.c',
        ROOT / 'src/game/commander/page_layout.c',
        ROOT / 'src/game/world/stations.c', ROOT / 'src/game/world/generation_random.c',
        ROOT / 'src/game/world/body_names.c', ROOT / 'src/game/world/profiles.c',
        ROOT / 'src/game/world/system_record.c']
    contract_includes = [ROOT / 'src/engine', ROOT / 'src/game', ROOT / 'src/engine/math',
                         ROOT / 'src/engine/render', ROOT / 'src/engine/pool', ROOT / 'src/game/people',
                         ROOT / 'src/game/station', ROOT / 'src/game/world',
                         ROOT / 'src/game/commander', ROOT / 'src/engine/objects', directory]
    contract_sources = compile_native('native-contracts', contract_sources, contract_includes)
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
    optional_sources = compile_native(
        'optional-tests', optional_sources, [ROOT / 'src/game', ROOT / 'src/engine', directory],
        ('FE2_ROLAND_EMBEDDED',) if embed_midi else ())
    recorded = list(sources + reported_sources + contract_sources + optional_sources)
    recorded.extend(Path(path) for path in windows_executable_inputs(zig(), directory) if str(path).endswith('.c'))
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

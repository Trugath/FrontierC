"""Authoring contracts and lossless migration checks for runtime data."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from resource_sources import ROOT, asset, document, settings
from ship_parameters import records
from system_records import trade_code


class Authoring(unittest.TestCase):
    def test_profile_rules_and_widths_reject_invalid_edits(self):
        from systems_resources_c import profile_records
        text = (ROOT / 'data/profiles/profiles.csv').read_text()
        self.assertEqual(len(profile_records()), 23)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profiles.csv'
            header, first, *rest = text.splitlines()
            for invalid in (text.replace('size_threshold', 'unknown', 1),
                            '\n'.join([header, *rest]) + '\n',
                            text.replace('none', 'unrecognized_rule', 1)):
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    profile_records(path)
            fields = first.split(',')
            fields[2] = '65536'
            path.write_text('\n'.join([header, ','.join(fields), *rest]) + '\n')
            with self.assertRaises(ValueError):
                profile_records(path)

    def test_named_text_and_models_reject_raw_selectors(self):
        from model_names import parse_model
        from text_references import parse_selector
        self.assertEqual(parse_selector('interaction:text_scanner'), 0x987c)
        self.assertEqual(parse_selector('chart:text_sol'), 0x8400)
        self.assertEqual(parse_model('main_model_052'), 0x34)
        self.assertEqual(parse_model('0x58'), 0x58)
        for invalid in ('0x987c', '0x8400', 'missing:text', 'status:not_a_real_entry'):
            with self.assertRaises(ValueError):
                parse_selector(invalid)
        for invalid in ('0x34', 'main_model_999', '0x10000'):
            with self.assertRaises(ValueError):
                parse_model(invalid)

    def test_reference_table_migrations_are_lossless(self):
        import adlib_tables_c
        import market_resources_c
        import systems_resources_c
        import systems_tables_c
        expected = json.loads((ROOT / 'tests/authoring/reference_table_hashes.json').read_text())
        blobs = {'profile_callbacks': systems_resources_c.profile_callbacks()[1]}
        if (ROOT / 'data/adlib/pitch_curves.csv').is_file():
            blobs['pitch'] = adlib_tables_c.pitch_image()[0]
        else:
            expected.pop('pitch')
        if (ROOT / 'data/text/systems/strings.tsv').is_file():
            blobs['systems_image'] = systems_tables_c.image()
        else:
            expected.pop('systems_image')
        blobs.update({name: market_resources_c.dispatch_table(ROOT / 'data', name, count)
                      for _, name, count in market_resources_c.DISPATCH})
        for name, blob in blobs.items():
            with self.subTest(table=name):
                self.assertEqual(hashlib.sha256(blob).hexdigest(), expected[name])

    def test_model_banks_preserve_pre_refactor_data(self):
        if not any((ROOT / 'data/models/frontier/models').glob('*.model')):
            self.skipTest('unpacked model sources are not present')
        expected = json.loads((ROOT / 'tests/authoring/model_bank_hashes.json').read_text())
        for name, checksum in expected.items():
            with self.subTest(bank=name):
                bank = document('main_models', ROOT / 'data/models' / name / 'models.ini')
                digest = hashlib.sha256(json.dumps(bank, sort_keys=True).encode()).hexdigest()
                self.assertEqual(digest, checksum)

    def test_ship_parameter_rows_and_invalid_edits(self):
        text = (ROOT / 'data/models/frontier/ship_parameters.csv').read_text()
        self.assertEqual(len(records(text)), 41)
        for invalid in (text + text.splitlines()[1] + '\n',
                        text.replace('main_thrust', 'unknown', 1),
                        text.replace(',560,', ',65536,', 1)):
            if invalid == text:
                continue
            with self.assertRaises(ValueError):
                records(invalid)
        header, row, *_ = text.splitlines()
        fields = row.split(',')
        for value in ('-1', '65536', 'oops'):
            fields[1] = value
            with self.assertRaises(ValueError):
                records(header + '\n' + ','.join(fields) + '\n')

    def test_all_typed_table_manifests_compile(self):
        count = 0
        for path in (ROOT / 'data').rglob('*.ini'):
            config = settings(path)
            if config.sections() and all({'type', 'start', 'end', 'file'} <= set(config[s])
                                         for s in config.sections()):
                with self.subTest(path=str(path.relative_to(ROOT))):
                    document('module_tables', path)
                count += 1
        self.assertGreater(count, 15)

    def test_every_native_table_generator(self):
        import importlib
        from build_c_frontier import AREAS, TABLES
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            for area, writers in AREAS:
                self.assertTrue(list((ROOT / 'src/game' / area).glob('*.c')))
                for name, function, filename in writers:
                    with self.subTest(generator=name):
                        output = directory / filename
                        getattr(importlib.import_module(name), function)(output)
                        self.assertGreater(output.stat().st_size, 0)
            for generator, filename in TABLES.values():
                with self.subTest(generator=generator.__name__):
                    generator.write_tables(directory / filename)
                    self.assertGreater((directory / filename).stat().st_size, 0)

    def test_system_overrides_compile(self):
        from catalogue_tables_c import source
        from system_names import allegiance_labels, government_labels, population_labels
        catalogue = document('system_records', ROOT / 'data/systems/catalogue/catalog.ini')
        self.assertEqual(len(catalogue['records']), 331)
        sol = catalogue['records'][7]
        self.assertEqual(sol['key'], 0xa925c600)
        self.assertEqual(sol['metadata'][0], 9)
        self.assertEqual(sol['metadata'][6], 0x85)
        self.assertEqual(government_labels()[5], 'Federal Democracy')
        self.assertEqual(population_labels()[9], 'More than 10 Billion')
        self.assertEqual(allegiance_labels()[2], 'federation')
        self.assertIn('fe2_catalogue_fallback_records', source())
        packed = bytearray()
        for row in catalogue['records']:
            packed += row['key'].to_bytes(4, 'little')
            packed += bytes(row['metadata'])
            packed += bytes(row['payload'])
            packed += bytes((row['packed'],))
            packed += row['responses'][0].to_bytes(2, 'little')
            packed += row['responses'][1].to_bytes(2, 'little')
        self.assertEqual(hashlib.sha256(packed).hexdigest(),
                         'c972e7390e0b2b100b1d51f8d79d7536683d7562f946f8ee908ca07887ac5175')
        from system_templates import encode
        sol = encode(ROOT / 'data/systems/handcrafted/sol.csv')
        earth = sol[4 * 64:5 * 64]
        self.assertEqual(int.from_bytes(earth[8:10], 'little'), 1)
        self.assertEqual(int.from_bytes(earth[0x20:0x22], 'little'), 0x127)
        self.assertEqual(earth[0x3a], 1)
        self.assertEqual(earth[0x3b], 1)
        systems = document('handcrafted_systems', ROOT / 'data/systems/handcrafted/handcrafted_systems.ini')
        self.assertEqual(systems['systems'][0]['key'], 0xa925c600)
        keys = [row['key'] for row in systems['systems']]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertNotIn(0, keys)
        for illegal in ('', 'i8', 'e-1', '0x100', 'import'):
            with self.assertRaises(ValueError):
                trade_code(illegal)
        self.assertEqual(trade_code('!e7'), 0x8f)
        self.assertEqual(trade_code('i0'), 0)

    def test_asset_paths_cannot_leave_catalogue(self):
        source = ROOT / 'data/intro/models.ini'
        for name in ('../outside', '../../README.md', str(ROOT / 'README.md')):
            with self.assertRaises(ValueError):
                asset(source, name)
        self.assertEqual(asset(source, 'models/example.model'), source.parent / 'models/example.model')

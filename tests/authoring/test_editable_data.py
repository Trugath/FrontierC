"""Behavior and malformed-input checks for named gameplay and audio data."""
import copy
import csv
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from resource_sources import ROOT
import gameplay_data
import speaker_scripts


class EditableData(unittest.TestCase):
    def test_native_data_migrations_preserve_bytes(self):
        import market_resources_c
        import mining_resources_c
        import mining_parameters_c
        import optional_tables_c
        expected = json.loads((ROOT / 'tests/authoring/editable_data_hashes.json').read_text())
        pairs = [
            ('market_image', market_resources_c.resources()),
            ('mining_parameters', mining_parameters_c.resources()),
        ]
        if (ROOT / 'data/text/mining/strings.tsv').is_file():
            pairs.append(('mining_image', mining_resources_c.resources()))
        else:
            expected.pop('mining_image')
        actual = {name: hashlib.sha256(blob).hexdigest() for name, blob in pairs}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'speaker.h'
            optional_tables_c.speaker(path)
            actual['speaker_c_tables'] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertEqual(actual, expected)

    def temporary_csv(self, text, function):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'table.csv'
            path.write_text(text, encoding='utf-8')
            return function(path)

    def test_allegiance_candidates_use_map_names(self):
        from system_names import allegiance_candidates, allegiance_labels, map_name, pack_system
        path = ROOT / 'data/economy/allegiance_candidate_systems.csv'
        self.assertEqual(allegiance_labels(), ['independent', 'imperial', 'federation'])
        self.assertEqual(allegiance_candidates(path), [0xa925c604, 0xa925c604, 0xa905c680])
        self.assertEqual((map_name(0, 0, 0), pack_system(0, 0, 0)), ('sol', 0xa925c600))
        self.assertEqual((map_name(0, 0, 1), pack_system(0, 0, 1)), ('alpha_centauri', 0xa925c601))
        self.assertEqual((map_name(-3, -6, 0), pack_system(-3, -6, 0)), ('lave', 0xa8f5c540))
        text = path.read_text()
        for variant in (text.replace('eneda', 'uv_ceti'),
                        text.replace('independent', 'federation'),
                        text.replace(',4', ',5', 1),
                        '\n'.join(text.splitlines()[:-1]) + '\n'):
            with self.subTest(variant=variant.splitlines()[1][:40]), self.assertRaises(ValueError):
                self.temporary_csv(variant, allegiance_candidates)

    def test_commodity_edit_changes_only_the_named_word(self):
        text = (ROOT / 'data/economy/commodity_market_parameters.csv').read_text()
        original = gameplay_data.commodity_parameters(ROOT / 'data/economy/commodity_market_parameters.csv')
        self.assertEqual(original[-8], (-40) & 65535)  # radioactives disposal price
        rows = list(csv.reader(io.StringIO(text)))
        index = next(i for i, row in enumerate(rows) if row[0] == 'robots')
        rows[index][1] = '1234'
        output = io.StringIO(); csv.writer(output).writerows(rows)
        edited = self.temporary_csv(output.getvalue(), gameplay_data.commodity_parameters)
        target = (index - 1) * 4
        self.assertEqual(edited[target], 1234)
        self.assertEqual(edited[:target] + edited[target + 1:], original[:target] + original[target + 1:])

    def test_spreadsheet_utf8_bom_is_accepted(self):
        for relative, reader in (
                ('economy/commodity_market_parameters.csv', gameplay_data.commodity_parameters),
                ('mining/yields.csv', gameplay_data.mining_yields),
                ('mining/categories.csv', gameplay_data.mining_categories)):
            path = ROOT / 'data' / relative
            self.assertEqual(self.temporary_csv('\ufeff' + path.read_text(), reader), reader(path))

    def test_q15_coefficients_are_named_decimal_rows(self):
        from economy_authoring import q15_coefficients
        from system_names import population_labels
        path = ROOT / 'data/economy/economic_factor_value_factors.csv'
        bands = [str(i) for i in range(8)]
        self.assertEqual(q15_coefficients(path, 'economic_factor', bands),
                         [16384, 17201, 18019, 19654, 22924, 26194, 29464, 32767])
        text = path.read_text()
        for variant in (text.replace('16384', '0x4000', 1),
                        text.replace('16384', '32768', 1),
                        text.replace('0,16384', '1,16384', 1)):
            with self.subTest(variant=variant.splitlines()[1]), self.assertRaises(ValueError):
                self.temporary_csv(variant, lambda table: q15_coefficients(table, 'economic_factor', bands))
        population = ROOT / 'data/economy/population_value_factors.csv'
        self.assertEqual(q15_coefficients(population, 'population', population_labels())[0], 32767)

    def test_commodity_schema_order_and_signed_bounds(self):
        text = (ROOT / 'data/economy/commodity_market_parameters.csv').read_text()
        lines = text.splitlines()
        invalid = [text.replace('base_price', 'unknown', 1),
                   '\n'.join(lines[:-1]) + '\n',
                   '\n'.join([lines[0], lines[2], lines[1], *lines[3:]]) + '\n',
                   text.replace('water,10,', 'water,32768,', 1),
                   text.replace('water,10,', 'water,-32769,', 1),
                   text.replace('water,10,', 'unrecognized,10,', 1),
                   text.replace('water,10,11,8000,6000', 'water,10,11,8000', 1)]
        for variant in invalid:
            with self.subTest(variant=variant[:100]), self.assertRaises(ValueError):
                self.temporary_csv(variant, gameplay_data.commodity_parameters)

    def test_mining_symbol_resolution_and_reserve_edit(self):
        path = ROOT / 'data/mining/yields.csv'
        original = gameplay_data.mining_yields(path)
        self.assertEqual(original[:6], [1, 0xa217, 0, 3, 0xa218, 3])
        text = path.read_text().replace('1,low_in_minerals,mine_low_minerals,3',
                                       '1,low_in_minerals,mine_low_minerals,123')
        edited = self.temporary_csv(text, gameplay_data.mining_yields)
        self.assertEqual(edited[5], 123)
        self.assertEqual(edited[:5] + edited[6:], original[:5] + original[6:])
        self.assertEqual(gameplay_data.mining_categories(ROOT / 'data/mining/categories.csv'),
                         [0, 0, 0, 18, 18, 18, 15, 15, 16])

    def test_invalid_mining_references_and_indices(self):
        text = (ROOT / 'data/mining/yields.csv').read_text()
        for variant in (text.replace('0,mined_out', '1,mined_out', 1),
                        text.replace('mined_out', 'unrecognized_category', 1),
                        text.replace('no_mineable_substances', 'unrecognized_text', 1),
                        text.replace('no_mineable_substances,0', 'no_mineable_substances,65536', 1),
                        '\n'.join(text.splitlines()[:-1]) + '\n'):
            with self.assertRaises(ValueError):
                self.temporary_csv(variant, gameplay_data.mining_yields)
        text = (ROOT / 'data/mining/categories.csv').read_text()
        for variant in (text.replace('unsuitable,water', 'unsuitable,unrecognized', 1),
                        text + text.splitlines()[1] + '\n'):
            with self.assertRaises(ValueError):
                self.temporary_csv(variant, gameplay_data.mining_categories)

    def test_pitch_curves_generate_pointer_table(self):
        import adlib_tables_c
        if not (ROOT / 'data/adlib/pitch_curves.csv').is_file():
            self.skipTest('unpacked AdLib pitch curves are not present')
        text = (ROOT / 'data/adlib/pitch_curves.csv').read_text()
        before, labels = adlib_tables_c.pitch_image()
        rows = list(csv.reader(io.StringIO(text)))
        rows[1][1] = '99'
        buffer = io.StringIO(); csv.writer(buffer).writerows(rows)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'curves.csv'; path.write_text(buffer.getvalue())
            with patch.object(adlib_tables_c, 'PITCH', path):
                after, edited_labels = adlib_tables_c.pitch_image()
        self.assertEqual(labels, edited_labels)
        at = labels['pitch_bend_curve_00']
        self.assertEqual(after[at], 99)
        self.assertEqual(after[:at] + after[at + 1:], before[:at] + before[at + 1:])
        for index in range(14):
            pointer = int.from_bytes(after[0x382 + 2 * index:0x384 + 2 * index], 'little')
            self.assertEqual(pointer, labels[f'pitch_bend_curve_{index:02d}'])

    def test_invalid_pitch_edits(self):
        import adlib_tables_c
        if not (ROOT / 'data/adlib/pitch_curves.csv').is_file():
            self.skipTest('unpacked AdLib pitch curves are not present')
        text = (ROOT / 'data/adlib/pitch_curves.csv').read_text()
        lines = text.splitlines()
        for variant in (text.replace('bend_00', 'unknown', 1),
                        '\n'.join(lines[:-1]) + '\n',
                        text.replace('\n0,', '\n1,', 1),
                        text.replace('\n0,0,', '\n0,256,', 1)):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'curves.csv'; path.write_text(variant)
                with patch.object(adlib_tables_c, 'PITCH', path), self.assertRaises(ValueError):
                    adlib_tables_c.pitch_image()


class SpeakerScripts(unittest.TestCase):
    def setUp(self):
        self.document = json.loads((ROOT / 'data/speaker/scripts.json').read_text())

    def test_nested_loops_and_signed_deltas(self):
        commands = [{'op':'loop', 'count':256, 'commands':[
            {'op':'loop', 'count':4, 'commands':[{'op':'pitch_delta', 'steps':-3}]}]},
            {'op':'finish_note'}]
        self.assertEqual(speaker_scripts.pattern_commands(commands), bytes.fromhex('81 00 81 04 fd 82 82 83'))

    def test_effect_pattern_names_resolve_to_native_ids(self):
        compiled = speaker_scripts.compile_document(self.document)
        effect = dict(compiled['effects'])['effect_falling_tones']
        self.assertEqual(effect, bytes([0, 200, 5, 0, 188, 2, 255]))
        self.document['effects'][1]['commands'][0]['pattern'] = 'pattern_noise'
        self.document['effects'][1]['commands'][0]['note'] = 'keep'
        updated = dict(speaker_scripts.compile_document(self.document)['effects'])['effect_falling_tones']
        self.assertEqual(updated[:3], bytes([1, 255, 5]))
        self.assertEqual(updated[3:], effect[3:])

    def test_invalid_schema_names_references_and_commands(self):
        variants = []
        def variant():
            result = copy.deepcopy(self.document); variants.append(result); return result
        variant()['format'] = 'unknown'
        variant()['patterns'][1]['name'] = self.document['patterns'][0]['name']
        variant()['effects'][1]['commands'][0]['pattern'] = 'missing_pattern'
        variant()['effects'][1]['commands'][0]['note'] = 255
        variant()['effects'][1]['commands'][0]['ticks'] = True
        variant()['patterns'][0]['commands'][0]['ticks'] = 256
        variant()['patterns'][0]['commands'][0]['extra'] = 1
        variant()['patterns'][0]['commands'][0]['op'] = 'unknown'
        variant()['effects'][0]['commands'].append({'op':'end'})
        variant()['effects'][0]['commands'] = []
        variant()['patterns'][0]['commands'].pop()
        variant()['patterns'].pop()
        for data in variants:
            with self.subTest(document=data), self.assertRaises(ValueError):
                speaker_scripts.compile_document(data)

    def test_invalid_loop_and_reserved_pitch_values(self):
        invalid = [[{'op':'pitch_delta','steps':value}, {'op':'finish_note'}]
                   for value in (-129, -128, -125, 126, 127, 128)]
        invalid += [[{'op':'loop','count':value,'commands':[{'op':'wait','ticks':1}]},
                     {'op':'finish_note'}] for value in (0, 257, True)]
        invalid.append([{'op':'loop','count':2,'commands':[]}, {'op':'finish_note'}])
        invalid.append([{'op':'loop','count':256,'commands':[{'op':'noise_on'}]},
                        {'op':'finish_note'}])
        nested = [{'op':'wait','ticks':1}]
        for _ in range(5): nested = [{'op':'loop','count':2,'commands':nested}]
        invalid.append(nested + [{'op':'finish_note'}])
        for commands in invalid:
            with self.subTest(commands=commands), self.assertRaises(ValueError):
                speaker_scripts.pattern_commands(commands)

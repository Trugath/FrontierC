"""Semantic cockpit authoring, reference resolution, and migration checks."""
import copy
import hashlib
import json
import unittest
from unittest.mock import patch

import cockpit_layout as layout
import flight_tables_c


class CockpitLayout(unittest.TestCase):
    def setUp(self):
        self.doc = json.loads(layout.SOURCE.read_text())

    def test_all_recovered_flight_blocks_preserved(self):
        if not (layout.ROOT / 'data/text/flight/strings.tsv').is_file():
            self.skipTest('unpacked flight text is not present')
        expected = json.loads((layout.ROOT / 'tests/authoring/cockpit_data_hashes.json').read_text())
        actual = {hex(offset): hashlib.sha256(data).hexdigest() for offset, data in flight_tables_c.blocks()}
        self.assertEqual(actual, expected)

    def test_native_generation_does_not_reconstruct_overlay_image(self):
        with patch.object(flight_tables_c, 'blocks', side_effect=AssertionError('Guest image read')):
            self.assertIn('fe2_flight_equipment_slot_centres[8][2]', flight_tables_c.source())

    def test_slot_edit_changes_named_coordinates(self):
        original = layout.compile_document(self.doc)
        self.doc['equipment_slots'][3] = [100, 180]
        edited = layout.compile_document(self.doc)
        self.assertEqual(edited['slots'][3], [100, 180])
        self.assertEqual(edited['sprites'], original['sprites'])
        for key in ('commands', 'regions', 'markers', 'labels', 'drive_labels'):
            self.assertEqual(edited[key], original[key])

    def test_sprite_names_resolve_through_actual_atlas(self):
        self.doc['weapons']['homing_missile']['even_slot_sprite'] = 'dummy_mine'
        result = layout.compile_document(self.doc)
        self.assertEqual(result['sprites'][2], [54, 56])
        self.assertEqual(result['sprites'][0], [54, 54])

    def test_text_references_resolve_by_catalogue(self):
        self.doc['target_labels']['upper_left']['text'] = 'flight:target_behind_km'
        self.doc['drive_labels']['class_1'] = 'flight:mode_unknown'
        result = layout.compile_document(self.doc)
        self.assertEqual(result['labels'][3], 0x8606)
        self.assertEqual(result['drive_labels'][1], 0x8612)

    def test_command_edit_updates_hitbox_key_and_cleanup(self):
        self.doc['commands']['ship_command'] = {'key': 'z', 'bounds': [200, 180, 220, 200]}
        result = layout.compile_document(self.doc)
        self.assertEqual(result['commands'][-5:], [200, 180, 220, 200, ord('z')])
        self.assertEqual(result['regions'][-1], ord('z'))
        source = flight_tables_c.source()
        declaration = next(line for line in source.splitlines() if 'fe2_flight_command_rectangles[' in line)
        self.assertIn('[36]', declaration)
        self.assertTrue(declaration.endswith('0xffff};'))
        self.assertNotIn('fe2_flight_layout[', source)

    def test_signed_marker_endpoints(self):
        self.doc['target_marker'][0]['from'] = [-32768, 32767]
        self.assertEqual(layout.compile_document(self.doc)['markers'][:2], [32768, 32767])

    def test_malformed_layout_rejected(self):
        changes = [
            lambda d: d.update(format='unknown'),
            lambda d: d.update(unknown=[]),
            lambda d: d['weapons'].pop('dummy_mine'),
            lambda d: d['weapons']['dummy_mine'].update(even_slot_sprite='missing'),
            lambda d: d['weapons']['dummy_mine'].update(even_slot_sprite=54),
            lambda d: d['equipment_slots'].pop(),
            lambda d: d['equipment_slots'].__setitem__(0, [320, 1]),
            lambda d: d['equipment_slots'].__setitem__(0, [True, 1]),
            lambda d: d['commands']['ship_command'].update(key='mm'),
            lambda d: d['commands']['ship_command'].update(key='m'),
            lambda d: d['commands']['ship_command'].update(bounds=[20, 10, 10, 20]),
            lambda d: d['commands']['ship_command'].update(bounds=[10, 10, 20, 201]),
            lambda d: d['target_labels']['upper_left'].update(text='flight:missing'),
            lambda d: d['drive_labels'].update(class_1='interaction:missing'),
            lambda d: d['target_marker'][0].update(to=[32768, 0]),
            lambda d: d['target_marker'][0].update(to=[1]),
        ]
        for change in changes:
            doc = copy.deepcopy(self.doc)
            change(doc)
            with self.subTest(document=doc), self.assertRaises(ValueError):
                layout.compile_document(doc)

    def test_schema_matches_top_level_authoring_fields(self):
        schema = json.loads((layout.SOURCE.parent / 'layout.schema.json').read_text())
        self.assertEqual(set(schema['properties']), set(self.doc))
        self.assertEqual(set(schema['properties']['commands']['properties']), set(layout.COMMANDS))

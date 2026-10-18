"""Station authoring uses named actions and text; encoded layouts are generated."""
import copy
import json
import tempfile
from pathlib import Path
import unittest

import station_layout as layout
from text_references import resolve


class StationLayout(unittest.TestCase):
    def setUp(self):
        self.doc = json.loads(layout.SOURCE.read_text())

    def test_action_names_resolve_continue_targets(self):
        data = layout.compile_document(self.doc)
        for index, name in enumerate(layout.ACTIONS):
            expected = self.doc['actions'][name]['continue_to']
            self.assertEqual(data['displays'][index][4], resolve(expected, 'interaction', optional=True))
        self.assertIsNone(self.doc['actions']['conversation']['continue_to'])
        self.assertEqual(data['displays'][layout.ACTIONS.index('fitting')][4], 0x980a)

    def test_edit_named_target_changes_only_that_action(self):
        original = layout.compile_document(self.doc)
        self.doc['actions']['fitting']['continue_to'] = self.doc['actions']['ship']['continue_to']
        edited = layout.compile_document(self.doc)
        self.assertEqual(edited['displays'][4][4], 0x980f)
        for index in range(len(layout.ACTIONS)):
            if index != 4:
                self.assertEqual(edited['displays'][index], original['displays'][index])
        for key in ('widgets', 'drives', 'repairs', 'pagination_text_x'):
            self.assertEqual(edited[key], original[key])

    def test_message_and_page_settings_are_independent(self):
        self.doc['actions']['menu']['continue_to'] = self.doc['actions']['ship']['continue_to']
        self.doc['actions']['repairs']['page_size'] = 7
        data = layout.compile_document(self.doc)
        self.assertEqual(data['displays'][0][4], 0x980f)
        self.assertEqual(data['displays'][8][3], 7)

    def test_shared_widget_groups_update_both_sets(self):
        original = layout.compile_document(self.doc)
        self.doc['widget_groups']['footer'][0]['bounds'] = [120, 155, 130, 165]
        edited = layout.compile_document(self.doc)
        self.assertEqual(edited['widgets']['board'][-15:-11], [120, 155, 130, 165])
        self.assertEqual(edited['widgets']['list'][-15:-11], [120, 155, 130, 165])
        self.assertEqual(edited['widgets']['flight'], original['widgets']['flight'])
        self.assertEqual(layout.widget_action({'secondary_row': 13}), 0x9d)
        self.assertEqual(layout.widget_action({'primary_row': 13}), 0x8d)

    def test_drive_and_repair_text_references(self):
        self.doc['drives']['class_1']['name_text'] = self.doc['drives']['class_2']['name_text']
        self.doc['repairs'][0]['charge'] = -12
        data = layout.compile_document(self.doc)
        self.assertEqual(data['drives'][1][1], 0x988a)
        self.assertEqual(data['repairs'][0], (0x987b, 65524))

    def test_invalid_edits_rejected(self):
        changes = [
            lambda d: d.update(format='unknown'),
            lambda d: d.update(extra=1),
            lambda d: d['actions'].pop('trade'),
            lambda d: d['actions']['fitting'].update(continue_to=0x980a),
            lambda d: d['actions']['fitting'].update(continue_to='interaction:missing'),
            lambda d: d['actions']['fitting'].update(continue_to='flight:weapon_none'),
            lambda d: d['actions']['fitting'].update(continue_to='interaction:text_cargo_bay_life_support'),
            lambda d: d['actions']['fitting'].update(visible_rows=0),
            lambda d: d['actions']['fitting'].update(row_spacing=True),
            lambda d: d['widget_groups']['footer'][0].update(action='unknown'),
            lambda d: d['widget_groups']['footer'][0].update(action={'primary_row': 14}),
            lambda d: d['widget_groups']['footer'][0].update(action={'menu_key': '6'}),
            lambda d: d['widget_groups']['footer'][0].update(bounds=[20, 20, 10, 10]),
            lambda d: d['widget_sets'].update(board=['missing']),
            lambda d: d['widget_sets'].update(board=['primary_rows', 'primary_rows']),
            lambda d: d['widget_sets'].update(board=['footer']),
            lambda d: d['drives']['class_1'].update(name_text='interaction:missing'),
            lambda d: d['repairs'][0].update(damage_flag=1),
            lambda d: d['repairs'][0].update(charge=32768),
        ]
        for change in changes:
            doc = copy.deepcopy(self.doc)
            change(doc)
            with self.subTest(document=doc), self.assertRaises(ValueError):
                layout.compile_document(doc)

    def test_schema_and_bom(self):
        schema = json.loads((layout.SOURCE.parent / 'layout.schema.json').read_text())
        self.assertEqual(set(schema['properties']), set(self.doc))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'layout.json'
            path.write_text(json.dumps(self.doc), encoding='utf-8-sig')
            self.assertEqual(layout.load(path), layout.load())

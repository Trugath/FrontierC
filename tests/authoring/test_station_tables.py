"""Native station tables are typed, independent, and preserve recovered content."""
import hashlib
import json
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

import station_tables_c as tables
from data_tables import dispatch_words


class StationTables(unittest.TestCase):
    def test_recovered_blocks_unchanged(self):
        expected = json.loads((tables.ROOT / 'tests/authoring/station_data_hashes.json').read_text())
        self.assertEqual({hex(offset): hashlib.sha256(data).hexdigest() for offset, data in tables.blocks()}, expected)

    def test_native_generation_does_not_read_guest_image(self):
        with patch.object(tables, 'sections', side_effect=AssertionError('Recovery data read')), \
             patch.object(tables, 'image_span', side_effect=AssertionError('Guest image read')):
            source = tables.source()
        self.assertNotIn('fe2_station_layout[', source)
        self.assertNotIn('fe2_station_words[', source)
        self.assertIn('fe2_station_display fe2_station_displays[9]', source)

    def test_independent_widget_groups_have_local_terminators(self):
        native = tables.native_tables()
        self.assertEqual({name: len(values) // 5 for name, values in native['widgets'].items()},
                         {'flight': 5, 'station': 6, 'list': 31, 'board': 17})
        self.assertEqual(native['widgets']['flight'][:5], [30, 20, 38, 28, 49])
        self.assertEqual(native['widgets']['board'][-5:], [200, 157, 208, 166, 3])
        source = tables.source()
        for name in native['widgets']:
            declaration = source.split('fe2_station_widgets_' + name + '[', 1)[1].split('};', 1)[0]
            self.assertTrue(declaration.rstrip().endswith('65535,'))

    def test_display_drive_and_item_fields_match_consumers(self):
        native = tables.native_tables()
        self.assertEqual(native['displays'][4], (13, 10, 152, 14, 0x980a))
        self.assertEqual(native['pagination_text_x'], 152)
        self.assertEqual(native['drives'][1], (5, 0x9887, 4))
        self.assertEqual(len(native['items']), 45)
        self.assertEqual(native['items'][0], (1, 1, 1, 5, 0xc0, 0x13))
        self.assertEqual(native['repairs'][-1], (0x9878, 25))

    def test_named_dispatch_has_no_authorable_code_addresses(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'rules.csv'
            path.write_text('slot,handler\n0,first\n1,second\n')
            self.assertEqual(dispatch_words(path, 'slot', 2, {'first': 4, 'second': 10}), b'\x04\x00\x0a\x00')
            for invalid in ('slot,handler,offset\n0,first,4\n',
                            'slot,handler\n0,missing\n1,second\n',
                            'slot,handler\n1,first\n0,second\n'):
                path.write_text(invalid)
                with self.assertRaises(ValueError):
                    dispatch_words(path, 'slot', 2, {'first': 4, 'second': 10})

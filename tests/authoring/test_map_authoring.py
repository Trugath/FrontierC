"""Named galactic-map catalogue, labels and opening positions."""
import unittest

from resource_sources import ROOT
from map_authoring import values
import chart_resources_c


class MapAuthoring(unittest.TestCase):
    def test_named_tables_keep_the_recovered_words(self):
        maps = ROOT / 'data/maps'
        systems = maps / 'catalog_systems.csv'
        self.assertEqual(values('catalog_coordinates', systems)[:4], [0x1718, 0x1524, 0x1719, 0x1524])
        self.assertEqual(values('catalog_coordinate_tail', systems), [0x1714, 0x151e])
        ranges = values('catalog_ranges', systems)
        self.assertEqual(ranges[:2], [0, 0x16])
        self.assertEqual(ranges[-3:], [0xea, 0xea, 0xea])
        self.assertEqual(values('catalog_records', systems)[:4], [9, 202, 16, 0])
        self.assertEqual(values('catalog_identifiers', systems)[0], 0x88)
        annotations = values('map_annotations', maps / 'map_annotation_records.csv')
        self.assertEqual(annotations[:4], [0x28, 0x0d00, 0x1c00, 0x150])
        self.assertEqual(annotations[-9:], [1, 0x1753, 0x1532, 0xb0, 1, 0x1714, 0x1513, 0xaa, 0])
        self.assertEqual(values('candidate_offsets', maps / 'candidate_sector_offsets.csv')[:4],
                         [8192, 1, 0xe000, 0xffff])
        starts = values('map_starts', maps / 'map_start_positions.csv')
        self.assertEqual(starts[:10], [0x6a00, 0x1717, 0x3200, 0x1524, 0xcc00, 0xffff, 1, 0, 0x88, 0])
        self.assertEqual(starts[20:30], [0xe000, 0x1715, 0xe000, 0x151e, 0, 0, 0, 0, 0x88, 0])
        image = dict(chart_resources_c.resources())
        self.assertEqual(image[0x2052], b':  \x00')

    def test_catalog_and_start_names_are_checked(self):
        systems = (ROOT / 'data/maps/catalog_systems.csv').read_text()
        starts = (ROOT / 'data/maps/map_start_positions.csv').read_text()
        for kind, text, changed in (
                ('catalog_coordinates', systems, systems.replace(',sol,', ',lave,', 1)),
                ('map_annotations', (ROOT / 'data/maps/map_annotation_records.csv').read_text(),
                 (ROOT / 'data/maps/map_annotation_records.csv').read_text().replace('major_arm', 'missing_label', 1)),
                ('map_starts', starts, starts.replace('ross_154', 'sol', 1))):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self._compile(kind, changed)

    def _compile(self, kind, text):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'table.csv'
            path.write_text(text, encoding='utf-8')
            values(kind, path)

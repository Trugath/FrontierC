"""Named portrait sprites and palette views compile to the recovered image."""
import hashlib
import tempfile
import unittest
from pathlib import Path

import composite_authoring as authoring
import composite_resources_c


HASH = 'a9b2ec7d35d7ac95ab9ada1e7cad647d6453218c8c13549f17b254ffb2f0dd0c'


class CompositeAuthoring(unittest.TestCase):
    def test_named_sources_preserve_recovered_image(self):
        from resource_sources import document
        compiled = authoring.image_bytes()
        self.assertEqual(len(compiled), 526)
        self.assertEqual(hashlib.sha256(compiled).hexdigest(), HASH)
        recovered = document('module_tables', authoring.DIRECTORY / 'tables.ini')
        blob = bytearray()
        for table in recovered['tables']:
            if table['width'] == 1:
                blob.extend(table['values'])
            else:
                for value in table['values']:
                    blob.extend(value.to_bytes(table['width'], 'little'))
        self.assertEqual(bytes(blob), compiled)

    def test_detail_selector_names_the_placed_sprite(self):
        slots = authoring.load_placements('style_a')
        self.assertEqual(slots[7], ('male_eyes_1', -31, -72))
        self.assertEqual(authoring.resolve_selectors('style_a', ['male_eyes_1', 'male_eyes_2'], slots), [0x1c, 0x20])
        self.assertEqual(authoring.tables()['style_a_detail_parts'][9], [64])
        self.assertEqual(authoring.tables()['base_palette_pointers'][:2], [[0xb0], [0x230]])

    def test_placement_row_order_is_not_a_reference(self):
        original = authoring.load_placements('style_a')
        header, *body = (authoring.DIRECTORY / 'male_parts.csv').read_text(encoding='utf-8').splitlines()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'male_parts.csv'
            path.write_text('\n'.join([header, *reversed(body)]) + '\n', encoding='utf-8')
            self.assertEqual(authoring.load_placements('style_a', Path(tmp)), original)

    def test_coordinate_edit_leaves_sprite_selection_unchanged(self):
        slots = authoring.load_placements('style_a')
        before = authoring.resolve_selectors('style_a', ['male_eyes_1', 'male_jaw_left_1'], slots)
        edited = authoring.arrange('style_a', [
            (stem, x, y + 3 if stem == 'male_eyes_1' else y) for stem, x, y in slots])
        self.assertEqual(edited[7], ('male_eyes_1', -31, -69))
        self.assertEqual(authoring.resolve_selectors('style_a', ['male_eyes_1', 'male_jaw_left_1'], edited), before)
        self.assertEqual(edited[20][1:], slots[20][1:])

    def test_palette_views_resolve_without_encoding_addresses(self):
        self.assertEqual(authoring.palette_words(['view_00', 'view_07'], 44), [0xb0, 0x230])
        self.assertEqual(authoring.palette_words(['view_02'], 8), [0x1b8])
        for names, colors in ((['view_02'], 44), (['view_22'], 44), (['missing'], 8)):
            with self.subTest(names=names), self.assertRaises(ValueError):
                authoring.palette_words(names, colors)

    def test_invalid_placement_is_rejected(self):
        slots = authoring.load_placements('style_a')
        records = [(stem, x, y) for stem, x, y in slots]
        variants = [
            [('female_hair_1', records[0][1], records[0][2]), *records[1:]],
            [('missing', records[0][1], records[0][2]), *records[1:]],
            [(records[1][0], records[1][1], records[1][2]), *records[1:]],
            records[:-1],
            [(records[0][0], 128, records[0][2]), *records[1:]],
        ]
        for variant in variants:
            with self.subTest(first=variant[0]), self.assertRaises(ValueError):
                authoring.arrange('style_a', variant)
        with self.assertRaises(ValueError):
            authoring.resolve_selectors('style_a', ['female_hair_1'], slots)

    def test_portrait_pngs_preview_the_zero_identity_palette(self):
        import sprite_previews
        if not Path('data/graphics/sprites/male_nose_2.png').is_file():
            self.skipTest('unpacked sprites are not present')
        nose = Path('data/graphics/sprites/male_nose_2.png').read_bytes()
        ordinary = Path('data/graphics/sprites/210.png').read_bytes()

        def palette(data):
            position = 8
            while position < len(data):
                size = int.from_bytes(data[position:position + 4], 'big')
                tag = data[position + 4:position + 8]
                payload = data[position + 8:position + 8 + size]
                if tag == b'PLTE':
                    return [tuple(payload[index:index + 3]) for index in range(0, size, 3)]
                position += 12 + size
            raise AssertionError('missing PLTE')

        preview = sprite_previews.portrait_palette(palette(nose))
        self.assertEqual(palette(nose), preview)
        self.assertEqual(palette(nose)[:128], palette(ordinary)[:128])
        self.assertNotEqual(palette(ordinary)[128:], preview[128:])
        self.assertNotEqual(palette(nose)[0x90], (0, 0, 0))

    def test_map_pngs_preview_the_system_palette(self):
        import sprite_previews
        if not Path('data/graphics/sprites/012.png').is_file():
            self.skipTest('unpacked sprites are not present')
        body = Path('data/graphics/sprites/012.png').read_bytes()
        ordinary = Path('data/graphics/sprites/210.png').read_bytes()

        def palette(data):
            position = 8
            while position < len(data):
                size = int.from_bytes(data[position:position + 4], 'big')
                tag = data[position + 4:position + 8]
                payload = data[position + 8:position + 8 + size]
                if tag == b'PLTE':
                    return [tuple(payload[index:index + 3]) for index in range(0, size, 3)]
                position += 12 + size
            raise AssertionError('missing PLTE')

        shown = palette(body)
        self.assertEqual(shown, sprite_previews.map_palette(shown))
        self.assertEqual(shown[:128], palette(ordinary)[:128])
        self.assertNotEqual(shown[128:], palette(ordinary)[128:])
        self.assertNotEqual(shown[160], (0, 0, 0))

    def test_native_header_uses_resolved_selectors(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'composite_data.h'
            composite_resources_c.write_header(path)
            text = path.read_text(encoding='ascii')
        detail = next(line for line in text.splitlines() if line.startswith('    28, 32, 36,'))
        self.assertIn('28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 28, 32, 36,', detail)
        self.assertIn('static const int16_t style_a_coordinates[][2] = {', text)

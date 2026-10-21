"""Copyrighted Frontier assets extract from binaries back into the data formats."""
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path

import extract_assets
import graphics_resources
import resource_sources

ROOT = Path(__file__).resolve().parents[2]


def bank_image(kind, ini):
    compiled = resource_sources.document(kind, ini)
    end = compiled['file_end'] if 'file_end' in compiled else compiled['end']
    image = bytearray(end)
    for model in compiled['models']:
        image[model['start']:model['end']] = extract_assets.model_record_bytes(model)
    return bytes(image)


class ExtractAssets(unittest.TestCase):
    def test_sprites_round_trip_to_the_graphics_file(self):
        if not any((ROOT / 'data/graphics/sprites').glob('*.png')):
            self.skipTest('unpacked sprites are not present')
        source = ROOT / 'data/graphics/el2g.ini'
        original = graphics_resources.encode(resource_sources.document('graphics', source))
        with tempfile.TemporaryDirectory() as tmp:
            graphics = Path(tmp) / 'graphics'
            graphics.mkdir()
            shutil.copy(source, graphics / 'el2g.ini')
            extract_assets.extract_sprites(original, graphics, ROOT / 'data/video')
            recovered = graphics_resources.encode(resource_sources.document('graphics', graphics / 'el2g.ini'))
        self.assertEqual(recovered, original)

    def test_model_banks_round_trip(self):
        if not any((ROOT / 'data/models/frontier/models').glob('*.model')):
            self.skipTest('unpacked model sources are not present')
        banks = (
            ('main_models', ROOT / 'data/models/frontier'),
            ('main_models', ROOT / 'data/models/frontier_auxiliary'),
            ('intro_models', ROOT / 'data/intro'),
        )
        for kind, directory in banks:
            with self.subTest(bank=directory.name):
                image = bank_image(kind, directory / 'models.ini')
                with tempfile.TemporaryDirectory() as tmp:
                    bank = Path(tmp) / directory.name
                    bank.mkdir()
                    for name in ('models.ini', 'model_directory.tsv', 'ship_parameters.csv'):
                        source = directory / name
                        if source.is_file():
                            shutil.copy(source, bank / name)
                    extract_assets.extract_model_bank(image, bank / 'models.ini')

    def test_font_and_portrait_palette_round_trip(self):
        if not (ROOT / 'data/video/font.bdf').is_file():
            self.skipTest('unpacked font and palettes are not present')
        colors = resource_sources.read_palette(ROOT / 'data/video/default.gpl')
        glyphs = resource_sources.read_font(ROOT / 'data/video/font.bdf')
        image = bytearray(extract_assets.MCGA_FONT + 115 * 10)
        image[extract_assets.MCGA_START:extract_assets.MCGA_START + 8] = b'EL2G.GRF'
        for index, color in enumerate(colors):
            image[extract_assets.MCGA_COLORS + 3 * index:extract_assets.MCGA_COLORS + 3 * index + 3] = bytes(color)
        for glyph in glyphs:
            offset = extract_assets.MCGA_FONT + 10 * glyph['index']
            image[offset:offset + 9] = bytes(int(row, 2) for row in glyph['bitmap_rows'])
            image[offset + 9] = glyph['advance']
        header = bytearray(512)
        header[:2] = b'MZ'
        header[8:10] = (32).to_bytes(2, 'little')
        portrait = bytearray(extract_assets.PALETTE_END)
        composite = resource_sources.read_palette(ROOT / 'data/palettes/composites.gpl')
        for index, color in enumerate(composite):
            portrait[extract_assets.PALETTE_START + 3 * index:extract_assets.PALETTE_START + 3 * index + 3] = bytes(color)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            extract_assets.extract_mcga(bytes(header) + bytes(image), root / 'video')
            extract_assets.extract_composite_palette(bytes(header) + bytes(portrait), root / 'palettes')
            self.assertEqual(resource_sources.read_palette(root / 'video/default.gpl'), colors)
            self.assertEqual(resource_sources.read_font(root / 'video/font.bdf'), glyphs)
            self.assertEqual(resource_sources.read_palette(root / 'palettes/composites.gpl'), composite)

    def test_music_scores_round_trip(self):
        if not (ROOT / 'data/music/elitet.mid').is_file():
            self.skipTest('unpacked MIDI scores are not present')
        original = (ROOT / 'data/music/elitet.mid').read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            extract_assets.extract_music(original, 'elitet.rol', Path(tmp))
            self.assertEqual((Path(tmp) / 'elitet.mid').read_bytes(), original)

    def test_zip_ignores_uncopyrighted_files_and_names_gaps(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / 'frontier.zip'
            with zipfile.ZipFile(archive, 'w') as zip_file:
                zip_file.writestr('install/EL2G.GRF', b'sprites')
                zip_file.writestr('install/readme.txt', b'not an asset')
            files = extract_assets.load_binaries(archive)
        self.assertEqual(files['el2g.grf'], b'sprites')
        self.assertNotIn('readme.txt', files)
        self.assertIn('frontier.exe', extract_assets.missing_assets(files))
        self.assertNotIn('el2g.grf', extract_assets.missing_assets(files))

    def test_folder_and_zip_resolve_the_same_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'install'
            folder.mkdir()
            (folder / 'EL2G.GRF').write_bytes(b'sprites')
            (folder / 'notes.txt').write_bytes(b'ignore')
            archive = Path(tmp) / 'frontier.zip'
            with zipfile.ZipFile(archive, 'w') as zip_file:
                zip_file.write(folder / 'EL2G.GRF', 'disk/EL2G.GRF')
                zip_file.writestr('disk/notes.txt', b'ignore')
            self.assertEqual(extract_assets.load_binaries(folder), extract_assets.load_binaries(archive))

    def test_install_reports_missing_files_and_wrong_versions(self):
        files = {name: b'not-the-game' for name in extract_assets.REQUIRED}
        del files['frontier.exe']
        with self.assertRaises(extract_assets.InstallError) as missing:
            extract_assets.check_install(files)
        self.assertIn('missing: frontier.exe', str(missing.exception))
        files['frontier.exe'] = b'not-the-game'
        with self.assertRaises(extract_assets.InstallError) as wrong:
            extract_assets.check_install(files)
        self.assertIn('Unable to extract', str(wrong.exception))
        self.assertIn('frontier.exe', str(wrong.exception))

    def test_seed_creates_authored_files_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            extract_assets.seed_authored(tmp)
            root = Path(tmp)
            self.assertTrue((root / 'models/frontier/models.ini').is_file())
            self.assertTrue((root / 'main/frontier/greeting_one.txt').is_file())
            self.assertTrue((root / 'main/frontier/glyph_widths.csv').is_file())
            self.assertTrue((root / 'text/frontier/catalog.ini').is_file())
            self.assertTrue((root / 'text/interaction/identifiers.txt').is_file())
            self.assertTrue((root / 'text/frontier/identifiers.txt').is_file())
            self.assertTrue((root / 'adlib/bank.ini').is_file())
            self.assertTrue((root / 'galaxy/tables.ini').is_file())
            self.assertTrue((root / 'economy/tables.ini').is_file())
            self.assertTrue((root / 'systems/catalogue/records.tsv').is_file())
            self.assertTrue((root / 'text/market/strings.tsv').is_file())
            self.assertFalse((root / 'text/interaction/strings.tsv').exists())
            self.assertFalse((root / 'text/frontier/strings.tsv').exists())
            self.assertFalse((root / 'adlib/records.tsv').exists())
            self.assertFalse((root / 'galaxy/density.pgm').exists())
            self.assertFalse((root / 'economy/README.md').exists())
            self.assertTrue((root / 'graphics/el2g.ini').is_file())
            self.assertTrue((root / 'intro/objects.ini').is_file())

    def test_text_catalog_round_trips_from_its_directory(self):
        image = bytearray(b'\x04\x00\x07\x00Hi\x00A\x00')
        with tempfile.TemporaryDirectory() as tmp:
            catalog = Path(tmp)
            (catalog / 'catalog.ini').write_text(
                '[strings]\nmodule=sample\nentries=strings.tsv\ncount=2\n'
                'directory_offset=0\nend_offset=9\n', encoding='ascii')
            (catalog / 'identifiers.txt').write_text('greeting\nletter\n', encoding='ascii')
            extract_assets.extract_text_catalog(bytes(image), catalog)
            self.assertEqual((catalog / 'strings.tsv').read_text(encoding='ascii'), 'greeting\tHi\nletter\tA\n')

    def test_setup_messages_round_trip_from_the_overlay(self):
        body = bytearray(extract_assets.SETUP_TEXT)
        for text in ('\r\nOne\r\n', '\r\nTwo\r\n', '\r\nThree\r\n', '\r\nFour\r\n', '\r\nFive\r\n', '\r\nSix\r\n'):
            body += text.encode('ascii') + b'$'
        body += b'EMMXXXX0\x00'
        header = bytearray(64)
        header[:2] = b'MZ'
        header[8:10] = (4).to_bytes(2, 'little')
        with tempfile.TemporaryDirectory() as tmp:
            extract_assets.extract_setup_messages(bytes(header) + bytes(body), tmp)
            import json
            messages = json.loads((Path(tmp) / 'messages.json').read_text(encoding='ascii'))
        self.assertEqual(messages['dos_version_message'], '\r\nOne\r\n')
        self.assertEqual(messages['sound_menu_message'], '\r\nSix\r\n')
        self.assertEqual(messages['ems_device_name'], 'EMMXXXX0')

    def test_text_catalog_can_omit_executable_addresses(self):
        import string_resources
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'strings.tsv').write_text('hello\tHello\n', encoding='ascii')
            (root / 'catalog.ini').write_text('[strings]\nmodule=sample\nentries=strings.tsv\n', encoding='ascii')
            document = string_resources.document(resource_sources.settings(root / 'catalog.ini'), root / 'catalog.ini',
                                                 resource_sources.asset)
        self.assertEqual(document['module'], 'sample')
        self.assertEqual(len(document['entries']), 1)
        self.assertNotIn('directory_offset', document)

    def test_model_bank_can_omit_executable_addresses(self):
        import main_model_resources
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            models = root / 'models'
            models.mkdir()
            (models / 'main_model_000.model').write_text(
                '[header]\nscale_shift = 0\nreserved_0c = 0\nculling_radius = 1\n\n'
                '[extension]\nbytes =\n\n[vertices]\nrecords =\n    0x00, 0x00, 0x00, 0x00\n\n'
                '[classification]\nrecords =\n\n[program]\nbytes = 0x00 0x00\n', encoding='ascii')
            directory = '\n'.join(f'{slot}\t{"main_model_000" if slot == 0 else "-"}' for slot in range(240)) + '\n'
            (root / 'model_directory.tsv').write_text(directory, encoding='ascii')
            (root / 'models.ini').write_text(
                '[bank]\nmodule = frontier\nprefix = main_model\ndirectory = model_directory.tsv\n'
                'directory_count = 240\n\n[main_model_000]\nfile = models/main_model_000.model\n', encoding='ascii')
            document = main_model_resources.document(resource_sources.settings(root / 'models.ini'),
                                                     root / 'models.ini', resource_sources.asset)
        self.assertEqual(document['start'], 0)
        self.assertEqual(document['models'][0]['start'], 2 + 480)
        self.assertGreater(document['file_end'], document['models'][0]['start'])


if __name__ == '__main__':
    unittest.main()

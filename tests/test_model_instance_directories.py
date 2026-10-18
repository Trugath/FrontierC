"""Tests of model-relative character directory relocation and alternate entry decoding."""
import configparser
import unittest
from model_instance_directories import compile_extension, directory_offsets, program_entry_offsets
from model_render_programs import compile_source, decode_source


class InstanceDirectories(unittest.TestCase):
    def config(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read_dict({'extension': {'bytes': ' '.join(['0'] * 16),
                                      'instance_programs': '0x20,0\n0x21,2\n0x22,2'},
                          'vertices': {'records': '1 2 3 4'},
                          'classification': {'records': ''}})
        return config

    def test_directory_relocates_targets_and_keeps_character_aliases(self):
        config = self.config()
        extension = compile_extension(config['extension'], 4, 0)
        self.assertEqual(extension[16:], [42, 0, 44, 0, 44, 0])
        self.assertEqual(directory_offsets(config), [0, 2, 2])
        self.assertEqual(program_entry_offsets(config), [0, 2])
        # A larger preceding vertex table relocates all pointers, not just
        # their low bytes. The source program-relative addresses stay stable.
        self.assertEqual(compile_extension(config['extension'], 260, 0)[16:],
                         [42, 1, 44, 1, 44, 1])

    def test_alternate_entry_recovers_after_default_return(self):
        data = bytes.fromhex('00 00 19 80 00 00 00 00')
        source, count, covered = decode_source(data, follow_branches=True, entry_offsets=[2])
        self.assertEqual((count, covered), (3, 6))
        self.assertIn('update_basis_axis token=0x8019', source)
        self.assertIn('raw 0x00 0x00', source)
        self.assertEqual(compile_source(source), data)
        for entries in ([-1], [len(data)], [0.5]):
            with self.assertRaises(ValueError):
                decode_source(data, follow_branches=True, entry_offsets=entries)
        with self.assertRaises(ValueError):
            decode_source(data, entry_offsets=[2])

    def test_directory_rejects_holes_duplicate_selectors_and_word_overflow(self):
        for entries in ('0x21,0', '0x20,0\n0x20,2', '0x20,-1', '0x20,0x10000', ''):
            config = self.config()
            config['extension']['instance_programs'] = entries
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                compile_extension(config['extension'], 4, 0)
        config = self.config()
        config['extension']['instance_programs'] = '0x20,0xffff'
        with self.assertRaises(ValueError):
            compile_extension(config['extension'], 4, 0)


if __name__ == '__main__':
    unittest.main()

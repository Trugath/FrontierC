"""Contract tests for editable model contact programs."""
import unittest

from model_contact_resources import compile_source, decode_source


class ContactPrograms(unittest.TestCase):
    def test_plane_payloads_preserve_tag_bits_and_terminators(self):
        source = '''planes_all_le tag=0x03 result=0x82 refs=1,7
planes_all_gt tag=0x45 result=0x20 refs=-
result_planes_all_le tag=0xc7 result=0x80 refs=2
end
'''
        self.assertEqual(compile_source(source), bytes.fromhex('03 82 01 07 00 45 20 00 c7 80 02 00 00'))

    def test_nested_model_number_is_big_endian_and_shift_is_raw(self):
        source = 'child tag=0xa3 result=0x80 field=255 shift=0xe1 model=33059 transform=0x3f vertex=0\nend\n'
        self.assertEqual(compile_source(source), bytes.fromhex('a3 80 ff e1 81 23 3f 00 00'))

    def test_decoder_stops_at_program_end_and_preserves_surrounding_data(self):
        data = bytes.fromhex('ff 22 01 08 83 04 00 00 77 99')
        source, end = decode_source(data, 1)
        self.assertEqual(end, 8)
        self.assertEqual(compile_source(source), data[1:8])
        self.assertIn('field=8 shift=0x83', source)
        self.assertEqual(data[end:], bytes.fromhex('77 99'))

    def test_rejects_wrong_operation_missing_conditions_and_unknown_operands(self):
        for source in ('child tag=0x03 result=0 refs=1\nend',
                       'planes_all_le tag=0x20 result=0 refs=1\nend',
                       'planes_all_le tag=1 result=0 refs=1 extra=2\nend'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                compile_source(source)

    def test_rejects_embedded_terminator_and_duplicate_fields(self):
        for source in ('planes_all_le tag=1 result=0 refs=1,0\nend',
                       'planes_all_le tag=1 tag=2 result=0 refs=1\nend'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                compile_source(source)

    def test_requires_single_final_terminator(self):
        for source in ('', 'end\nend', 'end\nplanes_all_le tag=1 result=0 refs=1'):
            with self.subTest(source=source), self.assertRaises(ValueError):
                compile_source(source)

    def test_rejects_truncated_binary_payloads(self):
        for data in (b'', bytes.fromhex('80 01 00 01 00'), bytes.fromhex('01 02 01'),
                     bytes.fromhex('20 01 00')):
            with self.subTest(data=data), self.assertRaises(ValueError):
                decode_source(data)


if __name__ == '__main__':
    unittest.main()

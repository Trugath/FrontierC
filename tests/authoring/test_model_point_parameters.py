import unittest

from model_point_parameters import (decode_size_operand, encode_size_operand,
                                    resolved_size_mode, material_accumulator,
                                    describe_material)


class PointParametersTests(unittest.TestCase):
    def test_all_operand_words_preserve_their_meaning(self):
        for word in range(65536):
            operand = decode_size_operand(word)
            canonical = encode_size_operand(operand.kind, operand.value)
            decoded = decode_size_operand(canonical)
            self.assertEqual((decoded.kind, decoded.value),
                             (operand.kind, operand.value))
            self.assertEqual(operand.raw, word)

    def test_literal_rotation_and_fixed_size(self):
        self.assertEqual(decode_size_operand(0x4000).value, 0x80)
        self.assertEqual(decode_size_operand(0x40).value, 0x8000)
        self.assertEqual(resolved_size_mode(0x800a), ('fixed', 10))
        self.assertEqual(resolved_size_mode(10), ('depth_scaled', 10))

    def test_indirect_addresses_and_aliases(self):
        self.assertEqual(decode_size_operand(0xc0).value, -64)
        self.assertEqual(decode_size_operand(0xffff).value, 62)
        self.assertEqual(decode_size_operand(0x80).value, 0x72)
        self.assertEqual(decode_size_operand(0x8180).value, 0x272)

    def test_direct_and_table_selectors(self):
        self.assertEqual(material_accumulator(0xabc8), 0xaac)
        self.assertEqual(material_accumulator(0xe028), 0x10000)
        self.assertEqual(describe_material(0xe028)['table_byte_offset'], 16)
        self.assertEqual(material_accumulator(0xe128, 0x1234), 0x11234)
        self.assertEqual(material_accumulator(0x1318, 0xffffffff), 0x1f)

    def test_unencodable_values_rejected(self):
        for kind, value in [('literal', 1), ('frame', -63), ('object', 0x172)]:
            with self.assertRaises(ValueError):
                encode_size_operand(kind, value)

    def test_material_arithmetic_against_register_steps(self):
        # Register-width reference keeps MOV/AND/BTR steps separate, especially
        # the 16-bit write to AX preserving the synthesized upper half of EAX.
        for selector in range(4096):
            for inherited in (0, 0x111, 0x12345678, 0xffffffff):
                eax = selector
                si = eax & 0xf00
                if si == 0xe00:
                    di = (eax & 0xe) << 15
                    si = (eax & 0xe0) << 14
                    eax |= di | si
                    eax = (eax & 0xffff0000) | (eax & 0x10)
                eax = (eax & 0xffff0000) | ((eax & 0xffff) & 0xefe)
                carry = bool(eax & 0x10)
                eax &= ~0x10
                if carry:
                    eax = (eax + inherited) & 0xffffffff
                self.assertEqual(material_accumulator(selector << 4, inherited), eax)

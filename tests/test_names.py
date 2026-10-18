"""Prevent regression to opaque recovery identifiers in native C code."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeNames(unittest.TestCase):
    def test_native_identifiers_describe_their_consumers(self):
        opaque = re.compile(r'\b(?:fe2_m\d{2}_\w+|m\d{2}_\w+|M\d{2}_\w+|g_[0-9a-f]{4}|field_[0-9a-f]+|people_(?:update|motion|walk|callback)_[0-9a-f]+|flight_guidance_[0-9a-f]+)\b')
        violations = []
        for path in (ROOT / 'src').rglob('*'):
            if path.suffix not in {'.c', '.h'}:
                continue
            code = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
            for match in opaque.finditer(code):
                violations.append(f'{path.relative_to(ROOT)}: {match[0]}')
        self.assertEqual(violations, [])

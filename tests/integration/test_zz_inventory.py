"""Every retained C translation unit must belong to a successful build."""
import json
import sys
import unittest
from build_c_frontier import ROOT


class CompilationInventory(unittest.TestCase):
    def test_no_orphaned_c_sources(self):
        compiled = set()
        manifests = list((ROOT / 'build/game').rglob('*manifest.json'))
        manifests.append(ROOT / 'build/tests/native-sources.json')
        for manifest in manifests:
            data = json.loads(manifest.read_text())
            compiled.update(path.replace('\\', '/') for path in data['sources'])
        sources = {path.relative_to(ROOT).as_posix() for path in (ROOT / 'src').rglob('*.c')}
        if sys.platform != 'win32':
            sources.discard('src/engine/host/windows_crt.c')
        self.assertEqual(sorted(sources - compiled), [], 'C files are missing from the build/test targets')

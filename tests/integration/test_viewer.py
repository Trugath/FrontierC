"""Build from editable assets and render real models through the native host."""
import re
import subprocess
import unittest
from build_native_viewer import build, TARGET
from compiler import zig
from build_c_frontier import ROOT
ZIG = zig()


class NativeViewer(unittest.TestCase):
    def test_host(self):
        exe = build(ROOT / 'build/game', ZIG)
        if TARGET:
            self.skipTest(f'built the Win32 host for {TARGET}; it runs only on Windows')
        def render(model, frames, textured=False):
            command = [str(exe), '--model', str(model), '--frames', str(frames)]
            if textured: command.append('--textured')
            result = subprocess.run(command,
                text=True, capture_output=True, check=True)
            match = re.fullmatch(r'frames=(\d+) errors=0 hash=(\d+)\s*', result.stdout)
            self.assertIsNotNone(match, result.stdout + result.stderr)
            self.assertEqual(int(match[1]), frames)
            self.assertNotEqual(int(match[2]), 952198597, 'blank frame')
            return match[2]
        a = render(2, 12)
        self.assertEqual(a, render(2, 12))
        self.assertNotEqual(a, render(2, 24))
        render(10, 12)
        render(2, 12, textured=True)
        print('Native Win32 host: editable assets, real models, repeated frame reset, deterministic rotating frames')


if __name__ == '__main__': unittest.main()

"""Build cache correctness: edits and failed compiles must not reuse stale objects."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from compiler import compile_object, windows_executable_inputs, zig


class ObjectCache(unittest.TestCase):
    def test_reuse_and_invalidation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            compiler = root / 'zig'; compiler.write_text('compiler')
            source = root / 'main.c'; source.write_text('int main(void) { return 0; }')
            header = root / 'values.h'; header.write_text('#define VALUE 1')
            output = root / 'main.o'
            def emit(*args, **kwargs):
                output.write_bytes(b'object')
            with patch('compiler.subprocess.run', side_effect=emit) as run:
                compile_object(compiler, ['-O2'], [root], source, output)
                compile_object(compiler, ['-O2'], [root], source, output)
                self.assertEqual(run.call_count, 1)
                source.write_text('int main(void) { return 1; }')
                compile_object(compiler, ['-O2'], [root], source, output)
                self.assertEqual(run.call_count, 2)
                header.write_text('#define VALUE 200')
                compile_object(compiler, ['-O2'], [root], source, output)
                self.assertEqual(run.call_count, 3)
                compile_object(compiler, ['-O0'], [root], source, output)
                self.assertEqual(run.call_count, 4)
                compiler.write_text('updated compiler')
                compile_object(compiler, ['-O0'], [root], source, output)
                self.assertEqual(run.call_count, 5)
                output.unlink()
                compile_object(compiler, ['-O0'], [root], source, output)
                self.assertEqual(run.call_count, 6)

    def test_failed_compile_cannot_be_cached(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            compiler = root / 'zig'; compiler.write_text('compiler')
            source = root / 'main.c'; source.write_text('broken source')
            output = root / 'main.o'
            with patch('compiler.subprocess.run', side_effect=subprocess.CalledProcessError(1, 'cc')):
                with self.assertRaises(subprocess.CalledProcessError):
                    compile_object(compiler, [], [root], source, output)
            self.assertFalse(output.with_suffix('.o.sha256').exists())


class WindowsExecutable(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'win32', 'Windows loader requirements')
    def test_executable_uses_the_public_runtime_and_declares_windows_10(self):
        try:
            compiler = zig()
        except SystemExit:
            self.skipTest('zig cc was not found')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'main.c'
            source.write_text(
                'void *memchr(const void *, int, unsigned long long);\n'
                'char *strchr(const char *, int);\n'
                'char *strrchr(const char *, int);\n'
                'char *strstr(const char *, const char *);\n'
                'int main(void) {\n'
                '    const char *text = "frontier";\n'
                '    return !(strchr(text, \'f\') && strrchr(text, \'r\') &&\n'
                '             strstr(text, "tier") && memchr(text, \'o\', 8));\n'
                '}\n', encoding='ascii')
            exe = root / 'check.exe'
            subprocess.run([str(compiler), 'cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic',
                            str(source), *windows_executable_inputs(compiler, root), '-o', str(exe)],
                           check=True)
            data = exe.read_bytes()
            self.assertIn(b'8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a', data)
            self.assertNotIn(b'api-ms-win-crt-private', data)
            pe = struct.unpack_from('<I', data, 0x3c)[0]
            major, minor = struct.unpack_from('<HH', data, pe + 24 + 40)
            self.assertLessEqual((major, minor), (10, 0))
            subprocess.run([str(exe)], check=True)

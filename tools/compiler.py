"""Locate the zig cc used to build the game."""
import os
import shutil
import hashlib
import json
import subprocess
import sys
from pathlib import Path

_HEADER_DIGESTS = {}


def compile_object(compiler, flags, includes, source, output):
    """Reuse an object only when its source, headers, compiler and flags match.

    Header sets deliberately include all headers under the include roots, so
    computed includes and generated tables are covered without parsing C.
    Cache stamps are published only after a successful compilation.
    """
    compiler, source, output = map(Path, (compiler, source, output))
    includes = tuple(Path(p).resolve() for p in includes)
    headers = sorted({p for directory in includes for p in directory.rglob('*')
                      if p.is_file() and p.suffix == '.h'})
    key = tuple((str(p), p.stat().st_size, p.stat().st_mtime_ns) for p in headers)
    if key not in _HEADER_DIGESTS:
        digest = hashlib.sha256()
        for header in headers:
            digest.update(str(header).encode())
            digest.update(header.read_bytes())
        _HEADER_DIGESTS[key] = digest.hexdigest()
    command = [str(compiler), 'cc', *flags, *(f'-I{p}' for p in includes),
               '-c', str(source), '-o', str(output)]
    identity = (compiler.stat().st_size, compiler.stat().st_mtime_ns)
    digest = hashlib.sha256(json.dumps([command, identity, _HEADER_DIGESTS[key]]).encode()
                            + source.read_bytes()).hexdigest()
    stamp = output.with_suffix(output.suffix + '.sha256')
    if output.is_file() and stamp.is_file() and stamp.read_text() == digest:
        return
    # A failed compile must never leave a usable stamp for a partial object.
    stamp.unlink(missing_ok=True)
    subprocess.run(command, check=True)
    stamp.write_text(digest, encoding='ascii')

def windows_executable_inputs(compiler, directory, target=None):
    """Files that let a Windows executable load on Windows 10 and Windows 11.

    A non-Windows build gets an empty list unless it is cross-compiling to
    Windows. The resource is the supported-OS manifest. windows_crt.c supplies
    the exception handler and string functions the private runtime DLL exports,
    which Windows 10 does not provide.
    """
    windows = (target is not None and 'windows' in target) or (target is None and sys.platform == 'win32')
    if not windows:
        return []
    host = Path(__file__).resolve().parents[1] / 'src' / 'engine' / 'host'
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    resource = directory / 'windows.res'
    # Absolute inputs start with '/', which zig rc also accepts as an option prefix.
    subprocess.run([str(compiler), 'rc', '--', str(host / 'windows.rc'), str(resource)],
                   check=True, cwd=host)
    return [str(host / 'windows_crt.c'), str(resource)]


def zig():
    env = os.environ.get('ZIG')
    if env:
        return Path(env)
    found = shutil.which('zig')
    if found:
        return Path(found)
    raise SystemExit('zig cc was not found. Install zig, or set ZIG to zig.exe.')

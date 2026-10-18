#!/usr/bin/env python3
"""Embed the authored asset templates frontier-data.exe copies on unpack."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from extract_assets import authored_files, DATA


def main():
    if len(sys.argv) != 2:
        print('usage: embed_authored.py <output.c>', file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    files = []
    for path in authored_files():
        if not path.is_file():
            print(f'This project is missing {path.relative_to(DATA).as_posix()}', file=sys.stderr)
            return 2
        files.append((path.relative_to(DATA).as_posix(), path.read_bytes()))
    lines = [
        '/* Generated authored templates. Do not edit. */',
        '#include "asset_seed.h"',
        '',
    ]
    for index, (name, blob) in enumerate(files):
        lines.append(f'static const uint8_t authored_{index}[] = {{')
        for at in range(0, len(blob), 24):
            lines.append(','.join(str(byte) for byte in blob[at:at + 24]) + ',')
        if not blob:
            lines.append('0')
        lines.append('};')
    lines.append('const fe2_authored fe2_authored_files[] = {')
    for index, (name, blob) in enumerate(files):
        lines.append(f'{{"{name}", authored_{index}, {len(blob)}}},')
    lines.append('};')
    lines.append(f'const size_t fe2_authored_count = {len(files)};')
    lines.append('')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines), encoding='ascii')
    return 0


if __name__ == '__main__':
    sys.exit(main())

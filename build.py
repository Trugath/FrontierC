#!/usr/bin/env python3
"""Build the Frontier game and print the executable path."""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from compiler import zig
from build_native_intro import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build' / 'game')
    parser.add_argument('--zig', type=Path, default=None)
    parser.add_argument('--intro', action='store_true', help='build the intro host instead of the game')
    args = parser.parse_args()
    print(build(args.output, args.zig or zig(), game=not args.intro))


if __name__ == '__main__':
    main()

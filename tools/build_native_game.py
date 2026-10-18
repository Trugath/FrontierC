"""Build the native game host, starting with the original intro."""
from compiler import zig
import argparse
from pathlib import Path
from build_native_intro import ROOT, build

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-game')
    parser.add_argument('--zig', type=Path, default=zig())
    args = parser.parse_args()
    print(build(args.output, args.zig, game=True))

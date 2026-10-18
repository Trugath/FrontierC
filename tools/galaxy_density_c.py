"""Compile the native galaxy ASCII PGM into C samples and original zero tail."""
from pathlib import Path

from galaxy_density import read_pgm

ROOT=Path(__file__).resolve().parents[1]
RESOURCE=ROOT/'data/galaxy/density.pgm'


def write_header(path):
    lines=['/* The density grid is loaded from frontier.dat. */',
           '#include <stddef.h>',
           'const uint8_t *galaxy_density_data;',
           'size_t galaxy_density_bytes;']
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,
                        default=ROOT/'build/resources/galaxy_density_data.h')
    write_header(parser.parse_args().output)

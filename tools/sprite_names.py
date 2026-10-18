"""Resolve sprite image stems in data/graphics/el2g.ini to sprite numbers."""
import configparser
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GRAPHICS = ROOT / 'data/graphics/el2g.ini'


def sprite_stems():
    config = configparser.ConfigParser()
    config.read(GRAPHICS, encoding='ascii')
    stems = {}
    for section in config.sections():
        if not section.startswith('sprite '):
            continue
        stem = Path(config[section]['image']).stem
        if stem in stems:
            raise ValueError(f'Duplicate sprite image stem {stem}')
        stems[stem] = int(section.split()[1])
    return stems


def parse_sprite(token):
    """An image stem, or a number whose sprite has no image stem."""
    token = token.strip()
    stems = sprite_stems()
    if token in stems:
        return stems[token]
    if not re.fullmatch(r'0x[0-9A-Fa-f]+|[0-9]+', token):
        raise ValueError(f'Unknown sprite {token!r}')
    value = int(token, 0)
    if not 0 <= value <= 0xffff:
        raise ValueError(f'Sprite number exceeds a word: {token}')
    owner = next((stem for stem, number in stems.items() if number == value), None)
    if owner:
        raise ValueError(f'{token} is sprite {owner}; write that name')
    return value

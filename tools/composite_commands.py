"""Portrait draw requests compiled from named composite resources.

Sprite choices and palette views are resolved before the service requests are
built. The requests themselves still follow the recovered drawing order,
including the exchanged selector words and override rules.
"""
from pathlib import Path
from resource_sources import read_palette

from composite_authoring import tables

ROOT = Path(__file__).resolve().parents[1] / 'data'


def commands(low, high, override=0, resources=None):
    """Return ordered rectangle, palette and sprite service requests.

    Palette payloads are native RGB6 bytes. Sprite requests carry resource
    index and absolute signed-coordinate position. Inputs are unsigned words.
    """
    data = tables() if resources is None else resources
    low, high, override = low & 65535, high & 65535, override & 65535
    packed = (high << 16) | low
    bank = bytes(value for rgb in read_palette(ROOT / 'palettes/composites.gpl') for value in rgb)
    result = [('rectangle', 1, 118, 22, 136, 0)]

    def palette(name, index, start, count, displacement=0):
        address = data[name + '_palette_pointers'][index][0] + displacement
        offset = address - 0xb0
        if offset < 0 or offset + count * 3 > len(bank) or offset % 3:
            raise ValueError('Palette pointer exceeds native RGB6 bank')
        result.append(('palette', start, bank[offset:offset + count * 3]))

    base = (packed >> 1) & 7
    palette('base', base, 0x80, 44)
    palette('base', base, 0xac, 44, 132)
    for name, shift, mask, start in (('feature', 4, 15, 0xd8),
            ('detail', 8, 31, 0xe0), ('secondary', 13, 7, 0xe8),
            ('feature', 16, 15, 0xf0), ('feature', 20, 15, 0xf8)):
        if override and start in (0xd8, 0xf8):
            palette('override', (override >> 3) & 7, start, 8)
        else:
            palette(name, (packed >> shift) & mask, start, 8)

    # PUSH AX/PUSH DX followed by POP AX/POP DX exchanges the words.
    parts = (low << 16) | high
    style = 'style_b' if low & 1 else 'style_a'
    resource_base = 0xa4 if low & 1 else 0x78

    def sprite(prefix, index, base):
        x, y = data[prefix + '_coordinates'][index]
        result.append(('sprite', base + index, 59 + x, 136 + y))

    for group, shift, mask in (('part_group_5', 1, 3), ('part_group_0', 3, 3),
            ('part_group_1', 3, 3), ('part_group_3', 5, 3),
            ('part_group_2', 7, 3), ('detail_parts', 9, 15),
            ('part_group_4', 13, 3)):
        encoded = data[style + '_' + group][(parts >> shift) & mask][0]
        sprite(style, encoded // 4, resource_base)
    overlay = (override >> 1) & 3
    if overlay:
        sprite('overlay_part', data['overlay_part_offsets'][overlay - 1][0] // 4, 0x6d)
    else:
        encoded = data[style + '_optional_parts'][(parts >> 15) & 7][0]
        sprite(style, encoded // 4, resource_base)
    return result

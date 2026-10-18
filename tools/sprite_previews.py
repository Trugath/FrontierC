"""Refresh PNG preview colors from the palettes the game uploads.

Pixel indices are not modified. Indices 0..127 keep their existing preview.
Portrait indices 128..255 show the composite upload for an identity whose
selectors are all zero. Map body indices 128..255 show the system chart palette.
"""
import struct
import zlib
from pathlib import Path

from resource_sources import settings

ROOT = Path(__file__).resolve().parents[1]
GRAPHICS = ROOT / 'data/graphics/el2g.ini'
PALETTE = ROOT / 'data/palettes/composites.gpl'
VIEWS = ROOT / 'data/palettes/composites.ini'
MAP_PALETTE = ROOT / 'data/maps/system_view_palette.gpl'
MAP_SPRITES = ROOT / 'data/maps/hierarchy_sprite_selectors.csv'
PORTRAITS = list(range(109, 112)) + list(range(120, 208))


def display(component):
    return (component * 255 + 31) // 63


def gpl_colors(path=PALETTE):
    colors = []
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0].isdigit() and parts[1].isdigit():
            values = tuple(int(part) for part in parts[:3])
            if any(not 0 <= value <= 255 for value in values):
                raise ValueError('Palette components must be RGB8 values')
            colors.append(tuple(display((value * 63 + 127) // 255) for value in values))
    return colors


def views(path=VIEWS):
    config = settings(path)
    found = {}
    for section in config.sections():
        if not section.startswith('view '):
            continue
        found['view_' + section.split()[1]] = (
            config[section].getint('first_color'), config[section].getint('color_count'))
    return found


def first_view(directory, name):
    column = (directory / name).read_text(encoding='ascii').splitlines()
    if column[0] != 'view' or len(column) < 2:
        raise ValueError(f'{name} must name palette views')
    return column[1]


def portrait_palette(existing, directory=ROOT / 'data/composites', palette=PALETTE, view_path=VIEWS):
    """RGB8 preview. `existing` is the current 256-color PLTE."""
    if len(existing) != 256 or any(len(color) != 3 for color in existing):
        raise ValueError('Portrait preview requires a 256-color PLTE')
    bank = gpl_colors(palette)
    ranges = views(view_path)
    colors = list(existing)
    base_name = first_view(directory, 'base_palette_pointers.csv')
    first, count = ranges[base_name]
    if count != 44 or first < 0 or first + 88 > len(bank):
        raise ValueError('Base preview needs 88 composite colors')
    colors[0x80:0xd8] = bank[first:first + 88]
    slots = (
        (0xd8, 'feature_palette_pointers.csv'),
        (0xe0, 'detail_palette_pointers.csv'),
        (0xe8, 'secondary_palette_pointers.csv'),
        (0xf0, 'feature_palette_pointers.csv'),
        (0xf8, 'feature_palette_pointers.csv'),
    )
    for start, filename in slots:
        name = first_view(directory, filename)
        view_first, view_count = ranges[name]
        if view_count != 8 or view_first < 0 or view_first + 8 > len(bank):
            raise ValueError(f'{name} is not an 8-color composite view')
        colors[start:start + 8] = bank[view_first:view_first + 8]
    return colors


def chunk(tag, payload):
    return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag + payload) & 0xffffffff)


def replace_preview(path, directory=ROOT / 'data/composites', palette=PALETTE, view_path=VIEWS):
    data = Path(path).read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path} is not a PNG')
    output = bytearray(data[:8])
    position, found = 8, False
    while position < len(data):
        size = int.from_bytes(data[position:position + 4], 'big')
        tag = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + size]
        nxt = position + 12 + size
        if tag == b'PLTE':
            if size != 768:
                raise ValueError(f'{path} preview is not 256 colors')
            existing = [tuple(payload[index:index + 3]) for index in range(0, 768, 3)]
            payload = b''.join(bytes(color) for color in portrait_palette(existing, directory, palette, view_path))
            output += chunk(b'PLTE', payload)
            found = True
        else:
            output += data[position:nxt]
        position = nxt
    if not found or position != len(data):
        raise ValueError(f'{path} has no palette preview')
    return bytes(output)


def map_sprite_ids(path=MAP_SPRITES):
    found = []
    for line in Path(path).read_text(encoding='ascii').splitlines():
        line = line.strip()
        if not line:
            continue
        number = int(line)
        if number < 0 or number > 224:
            raise ValueError(f'{path} names sprite {number}')
        found.append(number)
    if not found:
        raise ValueError(f'{path} names no map sprites')
    return sorted(set(found))


def map_palette(existing, palette=MAP_PALETTE):
    """RGB8 preview. Indices 128..255 are the system chart upload."""
    if len(existing) != 256 or any(len(color) != 3 for color in existing):
        raise ValueError('Map preview requires a 256-color PLTE')
    bank = gpl_colors(palette)
    if len(bank) != 128:
        raise ValueError('System chart palette is not 128 colors')
    colors = list(existing)
    colors[128:256] = bank
    return colors


def map_paths(graphics=GRAPHICS, selectors=MAP_SPRITES):
    config = settings(graphics)
    paths = []
    for number in map_sprite_ids(selectors):
        section = f'sprite {number:03d}'
        if section not in config:
            raise ValueError(f'Missing {section}')
        paths.append(graphics.parent / config[section]['image'])
    return paths


def refresh_maps(paths=None, graphics=GRAPHICS, selectors=MAP_SPRITES, palette=MAP_PALETTE):
    changed = 0
    targets = map_paths(graphics, selectors) if paths is None else paths
    for path in targets:
        data = Path(path).read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError(f'{path} is not a PNG')
        output = bytearray(data[:8])
        position, found = 8, False
        while position < len(data):
            size = int.from_bytes(data[position:position + 4], 'big')
            tag = data[position + 4:position + 8]
            payload = data[position + 8:position + 8 + size]
            nxt = position + 12 + size
            if tag == b'PLTE':
                if size != 768:
                    raise ValueError(f'{path} preview is not 256 colors')
                existing = [tuple(payload[index:index + 3]) for index in range(0, 768, 3)]
                payload = b''.join(bytes(color) for color in map_palette(existing, palette))
                output += chunk(b'PLTE', payload)
                found = True
            else:
                output += data[position:nxt]
            position = nxt
        if not found or position != len(data):
            raise ValueError(f'{path} has no palette preview')
        updated = bytes(output)
        if updated != data:
            path.write_bytes(updated)
            changed += 1
    return changed


def portrait_paths(graphics=GRAPHICS):
    config = settings(graphics)
    paths = []
    for number in PORTRAITS:
        section = f'sprite {number:03d}'
        if section not in config:
            raise ValueError(f'Missing {section}')
        paths.append(graphics.parent / config[section]['image'])
    return paths


def refresh(paths=None, graphics=GRAPHICS, composites=ROOT / 'data/composites',
            palette=PALETTE, view_path=VIEWS):
    changed = 0
    for path in portrait_paths(graphics) if paths is None else paths:
        updated = replace_preview(path, composites, palette, view_path)
        if updated != path.read_bytes():
            path.write_bytes(updated)
            changed += 1
    return changed


if __name__ == '__main__':
    print(f'Updated {refresh()} portrait previews and {refresh_maps()} map previews')

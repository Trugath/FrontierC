"""Resolve portrait placements, sprite choices and palette views.

Placement files own a sprite image and its pixel offset. Selector files name
those sprites. Palette files name views in the composite palette. Bank slots,
scaled selectors and image addresses are derived from those names.
"""
import csv
import re
from pathlib import Path

from resource_sources import read_palette, settings

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'data/composites'
PALETTE = ROOT / 'data/palettes/composites.ini'
GRAPHICS = ROOT / 'data/graphics/el2g.ini'
BANKS = {
    'style_a': (120, 44, 'male_parts.csv'),
    'style_b': (164, 44, 'female_parts.csv'),
    'overlay': (109, 3, 'headgear_parts.csv'),
}


def sprite_catalogue():
    catalogue = {}
    config = settings(GRAPHICS)
    for section in config.sections():
        if not section.startswith('sprite '):
            continue
        stem = Path(config[section]['image']).stem
        if stem in catalogue:
            raise ValueError('Sprite image stems must be unique')
        catalogue[stem] = int(section.split()[1])
    return catalogue


def read_rows(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        return [row for row in csv.reader(stream) if any(cell.strip() for cell in row)]


def signed_byte(text):
    if not re.fullmatch(r'-?\d+', text.strip()):
        raise ValueError('Portrait offset must be a signed byte')
    value = int(text)
    if not -128 <= value <= 127:
        raise ValueError('Portrait offset must be a signed byte')
    return value


def arrange(bank, records, catalogue=None):
    """Place records by sprite identity. Row order is not significant."""
    if bank not in BANKS:
        raise ValueError('Unknown portrait bank')
    base, count, _ = BANKS[bank]
    catalogue = sprite_catalogue() if catalogue is None else catalogue
    by_id = {ident: stem for stem, ident in catalogue.items()}
    slots = [None] * count
    for stem, x, y in records:
        if type(x) is not int or type(y) is not int or not -128 <= x <= 127 or not -128 <= y <= 127:
            raise ValueError('Portrait offset must be a signed byte')
        if stem not in catalogue:
            raise ValueError(f'Unknown sprite image: {stem}')
        ident = catalogue[stem]
        if not base <= ident < base + count:
            raise ValueError(f'Sprite {stem} is not in the {bank} portrait bank')
        index = ident - base
        if slots[index] is not None:
            raise ValueError(f'Sprite {stem} is placed more than once')
        slots[index] = (stem, x, y)
    missing = [by_id.get(base + index, str(base + index))
               for index, slot in enumerate(slots) if slot is None]
    if missing:
        raise ValueError(f'{bank} placement is missing sprite {", ".join(missing)}')
    return slots


def load_placements(bank, directory=DIRECTORY):
    _, _, filename = BANKS[bank]
    path = Path(directory) / filename
    rows = read_rows(path)
    if not rows or [cell.strip() for cell in rows[0]] != ['sprite', 'x', 'y']:
        raise ValueError(f'{path.name} must have columns sprite,x,y')
    records = []
    for row in rows[1:]:
        if len(row) != 3:
            raise ValueError(f'{path.name} must have columns sprite,x,y')
        records.append((row[0].strip(), signed_byte(row[1]), signed_byte(row[2])))
    return arrange(bank, records)


def column(path, name):
    path = Path(path)
    rows = read_rows(path)
    if not rows or [cell.strip() for cell in rows[0]] != [name]:
        raise ValueError(f'{path.name} must have a {name} column')
    values = []
    for row in rows[1:]:
        if len(row) != 1 or not row[0].strip():
            raise ValueError(f'{path.name} must contain one {name} per row')
        values.append(row[0].strip())
    if not values:
        raise ValueError(f'{path.name} has no entries')
    return values


def resolve_selectors(bank, names, slots):
    index_of = {stem: index for index, (stem, _, _) in enumerate(slots)}
    words = []
    for name in names:
        if name not in index_of:
            raise ValueError(f'{name} has no placement in {bank}')
        words.append(index_of[name] * 4)
    return words


def selector_words(bank, names, directory=DIRECTORY):
    return resolve_selectors(bank, names, load_placements(bank, directory))


def palette_views():
    views = {}
    config = settings(PALETTE)
    for section in config.sections():
        if not section.startswith('view '):
            continue
        if not re.fullmatch(r'view \d\d', section):
            raise ValueError(f'Palette view section must be named like "view 02": {section}')
        ident = 'view_' + section.split()[1]
        entry = config[section]
        views[ident] = (entry.getint('first_color'), entry.getint('color_count'))
    if not views:
        raise ValueError('Composite palette has no views')
    return views


def palette_words(names, colors):
    if colors not in (8, 44):
        raise ValueError('Palette selectors use 8-color or 44-color views')
    views = palette_views()
    bank = len(read_palette(PALETTE.parent / 'composites.gpl'))
    # A base view is uploaded as two consecutive 44-color ranges.
    span = 88 if colors == 44 else colors
    words = []
    for name in names:
        if name not in views:
            raise ValueError(f'Unknown palette view: {name}')
        first, count = views[name]
        if count != colors:
            raise ValueError(f'Palette view {name} is {count} colors, expected {colors}')
        if first < 0 or first + span > bank:
            raise ValueError(f'Palette view {name} does not fit the color bank')
        words.append(0xb0 + first * 3)
    return words


def tables(directory=DIRECTORY):
    directory = Path(directory)
    config = settings(directory / 'tables.ini')
    compiled = {}
    for section in config.sections():
        entry = config[section]
        kind = entry['type']
        path = directory / entry['file']
        if kind == 'composite_coordinates':
            bank = entry['bank']
            if bank not in BANKS or entry['file'] != BANKS[bank][2]:
                raise ValueError(f'{section} placement file does not match its bank')
            compiled[section] = [[x, y] for _, x, y in load_placements(bank, directory)]
        elif kind == 'composite_sprite':
            compiled[section] = [[word] for word in selector_words(entry['bank'], column(path, 'sprite'), directory)]
        elif kind == 'composite_palette':
            compiled[section] = [[word] for word in palette_words(column(path, 'view'), entry.getint('colors'))]
        else:
            raise ValueError(f'Unsupported composites table type: {kind}')
    return compiled


def recovery_values(directory, entry, kind):
    """Flat integers for the recovered image. Coordinates are unsigned bytes."""
    directory = Path(directory)
    if kind == 'composite_coordinates':
        values = []
        for _, x, y in load_placements(entry['bank'], directory):
            values.extend((x & 255, y & 255))
        return values
    if kind == 'composite_sprite':
        return selector_words(entry['bank'], column(directory / entry['file'], 'sprite'), directory)
    if kind == 'composite_palette':
        return palette_words(column(directory / entry['file'], 'view'), entry.getint('colors'))
    raise ValueError(f'Unsupported composites table type: {kind}')


def image_bytes(directory=DIRECTORY):
    directory = Path(directory)
    config = settings(directory / 'tables.ini')
    compiled = tables(directory)
    blob = bytearray()
    for section in config.sections():
        kind = config[section]['type']
        if kind == 'composite_coordinates':
            for x, y in compiled[section]:
                blob.extend((x & 255, y & 255))
        elif kind in ('composite_sprite', 'composite_palette'):
            for (word,) in compiled[section]:
                blob.extend(word.to_bytes(2, 'little'))
        else:
            raise ValueError(f'Unsupported composites table type: {kind}')
    return bytes(blob)

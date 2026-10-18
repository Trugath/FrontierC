"""Resolve galactic-map system names to packed system keys.

Key bits follow src/game/world/system_lookup.c: bits 0..5 are the system
number (bits 0..2 of that field are the distance bias), bits 6..18 are the
map x coordinate, and bits 19..31 are the map y coordinate. The map's System
Sector Code prints those coordinates relative to 0x1718 and 0x1524, in that
order. Names follow src/game/chart/galactic_queries.c.
"""
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CENTRE_X = 0x1718
CENTRE_Y = 0x1524
ALLEGIANCE_SELECTOR = 0x84b7


def _chart_names():
    path = ROOT / 'data/text/chart/identifiers.txt'
    names = [line for line in path.read_text(encoding='ascii').splitlines() if line]
    if len(names) != 261 or len(set(names)) != 261:
        raise ValueError('chart text catalogue changed')
    return names


def _sectors():
    from map_authoring import catalog_coordinates, _range_words
    coordinates = catalog_coordinates(ROOT / 'data/maps/catalog_systems.csv')
    ranges = _range_words(ROOT / 'data/maps/catalog_systems.csv')
    if len(coordinates) != 94 or len(ranges) < 48:
        raise ValueError('catalog sector layout changed')
    return [(coordinates[2 * i], coordinates[2 * i + 1]) for i in range(47)], ranges


def _syllables():
    slots = (ROOT / 'data/maps/system_name_syllables.txt').read_text(encoding='ascii').splitlines()
    if len(slots) != 32:
        raise ValueError('chart syllable shape changed')
    return slots


def _rol16(value, count):
    count &= 15
    value &= 0xffff
    return ((value << count) | (value >> (16 - count))) & 0xffff if count else value


def _absolute(sector_x, sector_y, number):
    """Return map x (key bits 6..18) and map y (key bits 19..31)."""
    map_x = sector_x + CENTRE_X
    map_y = sector_y + CENTRE_Y
    if not 0 <= map_x < 8192 or not 0 <= map_y < 8192 or not 0 <= number < 64:
        raise ValueError('system coordinates are outside the map')
    return map_x, map_y


# Chart lines the catalogue and economy tables already name, in selector order.
GOVERNMENT_LABELS = (
    'None', 'None (Anarchy)', 'Rule by force of local Barons',
    'Policed by the local corporations', 'Corporate State', 'Federal Democracy',
    'Federal Colonial Rule', 'Dictatorship', 'Communist', 'Religious Dictatorship',
    'Independent Democracy', 'Imperial Rule', 'Imperial Colonial Rule',
    'No Stable Government', 'Martial Law.',
)
POPULATION_LABELS = (
    'None registered', 'Less than 1,000', '1,000-10,000', '10,000-100,000',
    '100,000-1 Million', '1-10 Million', '10-100 Million', '100 Million-1 Billion',
    '1-10 Billion', 'More than 10 Billion',
)


def allegiance_labels():
    """Chart labels for government bits 6..7, in allegiance order."""
    names = _chart_names()
    labels = []
    for allegiance in range(3):
        doubled = ((ALLEGIANCE_SELECTOR + allegiance) << 1) & 0xffff
        ident = names[(doubled & 0x3fe) // 2 - 1]
        if not ident.startswith('text_'):
            raise ValueError('chart allegiance label is not a text identifier')
        labels.append(ident.removeprefix('text_'))
    return labels


def government_labels():
    """Government-type lines printed for bits 0..5. Fifteen entries sit ahead of population."""
    return list(GOVERNMENT_LABELS)


def population_labels():
    """Population lines printed for the population band, from none registered through 10 billion."""
    return list(POPULATION_LABELS)


def map_name(sector_x, sector_y, number):
    """Lowercase map name at a sector code and system number."""
    map_x, map_y = _absolute(sector_x, sector_y, number)
    sectors, ranges = _sectors()
    names = _chart_names()
    for index in range(len(sectors) - 1, -1, -1):
        # Catalog pairs are stored as (key bits 6..18, key bits 19..31).
        if sectors[index] != (map_x, map_y):
            continue
        text = ranges[index] // 2 + number
        if text >= len(names):
            raise ValueError('system number is outside the chart text catalogue')
        ident = names[text]
        if not ident.startswith('text_'):
            raise ValueError('catalog system is not a chart text identifier')
        return ident.removeprefix('text_')
    # Syllables use the chart's parameter order: its y is key bits 6..18.
    a = (map_x + number) & 0xffff
    b = (map_y + a) & 0xffff
    a = (_rol16(a, 3) + b) & 0xffff
    b = (_rol16(b, 5) + a) & 0xffff
    mix = (_rol16(a, number & 31) + _rol16(b, 4)) & 0xffff
    parts = []
    syllables = _syllables()
    for _ in range(3):
        parts.append(syllables[(mix & 124) >> 2])
        mix = _rol16(mix, 11)
    shown = ''.join(parts)
    return chr(ord(shown[0]) - 32).lower() + shown[1:]


def pack_system(sector_x, sector_y, number):
    map_x, map_y = _absolute(sector_x, sector_y, number)
    return (map_y << 19) | (map_x << 6) | number


def _signed(token):
    try:
        return int(token, 0)
    except ValueError:
        raise ValueError(f'expected an integer coordinate, got {token!r}') from None


def allegiance_candidates(path):
    labels = allegiance_labels()
    columns = ['allegiance', 'system', 'sector_x', 'sector_y', 'number']
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != columns:
            raise ValueError(f'{path}: expected columns {columns}')
        records = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path}: incomplete or extra fields')
            records.append({key: value.strip() for key, value in row.items()})
    if len(records) != len(labels):
        raise ValueError('expected one candidate system for each allegiance')
    keys = []
    for label, row in zip(labels, records):
        if row['allegiance'] != label:
            raise ValueError(f'allegiance {row["allegiance"]!r} is not the chart label {label!r}')
        sector_x = _signed(row['sector_x'])
        sector_y = _signed(row['sector_y'])
        number = _signed(row['number'])
        expected = map_name(sector_x, sector_y, number)
        if row['system'] != expected:
            raise ValueError(f'{row["system"]!r} is not the map name {expected!r} at that sector')
        keys.append(pack_system(sector_x, sector_y, number))
    return keys

"""Named galactic-map catalogue, annotation labels and opening positions.

Sector coordinates are the signed pair in the map's System Sector Code.
Catalogue order is the chart text order. Record bytes and the identifier word
are the values the chart reads; the identifier is not split because both of its
bytes select a value window as well as a star type.
"""
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CENTRE_X = 0x1718
CENTRE_Y = 0x1524
CATALOG_COLUMNS = ['role', 'sector_x', 'sector_y', 'draw_kind', 'system', 'style', 'depth', 'place_x', 'place_y', 'identifier']
ANNOTATION_COLUMNS = ['threshold', 'sector_x', 'sector_y', 'label']
START_COLUMNS = ['system', 'sector_x', 'sector_y', 'number', 'y_low', 'x_low', 'z', 'identifier']


def _chart_names():
    path = ROOT / 'data/text/chart/identifiers.txt'
    names = [line.split('\t', 1)[0] for line in path.read_text(encoding='ascii').splitlines()]
    if len(names) != 261 or len(set(names)) != 261:
        raise ValueError('chart text catalogue changed')
    return names


def _rows(path, columns):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != columns:
            raise ValueError(f'{path}: expected columns {columns}')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path}: incomplete or extra fields')
            rows.append({key: value.strip() for key, value in row.items()})
    if not rows:
        raise ValueError(f'{path}: empty table')
    return rows


def _integer(token, low, high, label):
    try:
        value = int(token, 0)
    except ValueError:
        raise ValueError(f'{label} must be an integer') from None
    if not low <= value <= high:
        raise ValueError(f'{label} is outside {low}..{high}')
    return value


def _coordinate(sector_x, sector_y):
    x = sector_x + CENTRE_X
    y = sector_y + CENTRE_Y
    if not 0 <= x <= 65535 or not 0 <= y <= 65535:
        raise ValueError('sector coordinate does not fit in a map word')
    return x, y


def _blank(row, *fields):
    if any(row[field] for field in fields):
        raise ValueError(f'{row["role"]} rows only use their own fields')


def catalog(path):
    names = _chart_names()
    expected = [name.removeprefix('text_') for name in names[:117]]
    sectors, systems = [], []
    coordinate_tail = draw_kind_tail = None
    for row in _rows(path, CATALOG_COLUMNS):
        role = row['role']
        if role == 'system':
            sector_x = _integer(row['sector_x'], -65535, 65535, 'sector_x')
            sector_y = _integer(row['sector_y'], -65535, 65535, 'sector_y')
            draw_kind = _integer(row['draw_kind'], 0, 65535, 'draw_kind')
            if not sectors or sectors[-1]['at'] != (sector_x, sector_y) or sectors[-1]['draw_kind'] != draw_kind or not sectors[-1]['open']:
                sectors.append({'at': (sector_x, sector_y), 'draw_kind': draw_kind, 'open': True})
            systems.append({
                'system': row['system'],
                'style': _integer(row['style'], 0, 255, 'style'),
                'depth': _integer(row['depth'], 0, 255, 'depth'),
                'place_x': _integer(row['place_x'], 0, 255, 'place_x'),
                'place_y': _integer(row['place_y'], 0, 255, 'place_y'),
                'identifier': _integer(row['identifier'], 0, 65535, 'identifier'),
            })
        elif role == 'empty':
            _blank(row, 'system', 'style', 'depth', 'place_x', 'place_y', 'identifier')
            if sectors:
                sectors[-1]['open'] = False
            sectors.append({
                'at': (_integer(row['sector_x'], -65535, 65535, 'sector_x'),
                       _integer(row['sector_y'], -65535, 65535, 'sector_y')),
                'draw_kind': _integer(row['draw_kind'], 0, 65535, 'draw_kind'),
                'open': False,
            })
        elif role == 'coordinate_tail':
            _blank(row, 'draw_kind', 'system', 'style', 'depth', 'place_x', 'place_y', 'identifier')
            if coordinate_tail is not None:
                raise ValueError('only one coordinate tail is stored')
            coordinate_tail = _coordinate(_integer(row['sector_x'], -65535, 65535, 'sector_x'),
                                          _integer(row['sector_y'], -65535, 65535, 'sector_y'))
        elif role == 'draw_kind_tail':
            _blank(row, 'sector_x', 'sector_y', 'system', 'style', 'depth', 'place_x', 'place_y', 'identifier')
            if draw_kind_tail is not None:
                raise ValueError('only one draw-kind tail is stored')
            draw_kind_tail = _integer(row['draw_kind'], 0, 65535, 'draw_kind')
        else:
            raise ValueError(f'unknown catalogue role {role!r}')
    if [row['system'] for row in systems] != expected:
        raise ValueError('catalogue systems must follow the chart text order')
    if len(sectors) != 47 or coordinate_tail is None or draw_kind_tail is None:
        raise ValueError('expected 47 sectors, a coordinate tail and a draw-kind tail')
    return {'sectors': sectors, 'systems': systems, 'coordinate_tail': coordinate_tail, 'draw_kind_tail': draw_kind_tail}


def catalog_coordinates(path):
    data = catalog(path)
    values = []
    for sector in data['sectors']:
        x, y = _coordinate(*sector['at'])
        values.extend((x, y))
    return values


def catalog_coordinate_tail(path):
    x, y = catalog(path)['coordinate_tail']
    return [x, y]


def _range_words(path):
    catalog(path)
    counts = []
    current = None
    for row in _rows(path, CATALOG_COLUMNS):
        if row['role'] == 'system':
            key = (row['sector_x'], row['sector_y'], row['draw_kind'])
            if current != key:
                counts.append(0)
                current = key
            counts[-1] += 1
        elif row['role'] == 'empty':
            counts.append(0)
            current = None
    if len(counts) != 47 or sum(counts) != 117:
        raise ValueError('catalogue sector counts changed')
    boundaries = [0]
    for count in counts:
        boundaries.append(boundaries[-1] + 2 * count)
    end = boundaries[-1]
    return boundaries + [end, end]


def catalog_records(path):
    values = []
    for system in catalog(path)['systems']:
        values.extend((system['style'], system['depth'], system['place_x'], system['place_y']))
    return values


def catalog_identifiers(path):
    return [system['identifier'] for system in catalog(path)['systems']]


def catalog_draw_kinds(path):
    data = catalog(path)
    return [sector['draw_kind'] for sector in data['sectors']] + [data['draw_kind_tail']]


def annotations(path):
    names = {name: index for index, name in enumerate(_chart_names())}
    values = []
    for row in _rows(path, ANNOTATION_COLUMNS):
        label = 'text_' + row['label']
        if label not in names:
            raise ValueError(f'unknown map label {row["label"]!r}')
        x, y = _coordinate(_integer(row['sector_x'], -65535, 65535, 'sector_x'),
                           _integer(row['sector_y'], -65535, 65535, 'sector_y'))
        values.extend((_integer(row['threshold'], 0, 65535, 'threshold'), x, y, (names[label] + 1) * 2))
    if len(values) != 56:
        raise ValueError('expected 14 map annotations')
    # A zero threshold ends the original scan. It sits between the labels and
    # the candidate-offset tables.
    values.append(0)
    return values


STEP = 8192
OFFSET_TABLES = ('one_sector', 'two_sectors')


def candidate_offsets(path):
    """Twelve sector steps for each candidate ring.

    The query adds `(sector_x * 8192 + sector_y) * 64` to the system
    reference. 8192 steps one sector in X and 1 steps one sector in Y.
    Orthogonal neighbours are listed twice, so a random draw among the
    twelve one-sector steps prefers them. The two-sector ring is the twelve
    steps of chebyshev radius 2 that are not the four corners.
    """
    grouped = {name: [] for name in OFFSET_TABLES}
    for row in _rows(path, ['table', 'sector_x', 'sector_y']):
        if row['table'] not in grouped:
            raise ValueError(f'unknown offset table {row["table"]!r}')
        dx = _integer(row['sector_x'], -4, 4, 'sector_x')
        dy = _integer(row['sector_y'], -4, 4, 'sector_y')
        grouped[row['table']].append((dx * STEP + dy) & 0xffff)
    words = []
    for name in OFFSET_TABLES:
        if len(grouped[name]) != 12:
            raise ValueError(f'{name} needs 12 sector steps')
        words.extend(grouped[name])
    return words


def starts(path):
    from system_names import map_name
    values = []
    rows = _rows(path, START_COLUMNS)
    if len(rows) != 6:
        raise ValueError('expected six map opening positions')
    for row in rows:
        sector_x = _integer(row['sector_x'], -65535, 65535, 'sector_x')
        sector_y = _integer(row['sector_y'], -65535, 65535, 'sector_y')
        number = _integer(row['number'], 0, 63, 'number')
        if map_name(sector_x, sector_y, number) != row['system']:
            raise ValueError(f'{row["system"]!r} is not system {number} of sector {sector_x},{sector_y}')
        x, y = _coordinate(sector_x, sector_y)
        z = _integer(row['z'], -2147483648, 4294967295, 'z') & 0xffffffff
        values.extend((_integer(row['y_low'], 0, 65535, 'y_low'), x,
                       _integer(row['x_low'], 0, 65535, 'x_low'), y,
                       z & 65535, z >> 16, number, 0,
                       _integer(row['identifier'], 0, 65535, 'identifier'), 0))
    return values


WIDTHS = {
    'catalog_coordinates': 2, 'catalog_coordinate_tail': 2, 'catalog_ranges': 2,
    'catalog_records': 1, 'catalog_identifiers': 2, 'catalog_draw_kinds': 2,
    'map_annotations': 2, 'map_starts': 2, 'candidate_offsets': 2,
}


def values(kind, path):
    readers = {
        'catalog_coordinates': catalog_coordinates,
        'catalog_coordinate_tail': catalog_coordinate_tail,
        'catalog_ranges': _range_words,
        'catalog_records': catalog_records,
        'catalog_identifiers': catalog_identifiers,
        'catalog_draw_kinds': catalog_draw_kinds,
        'map_annotations': annotations,
        'map_starts': starts,
        'candidate_offsets': candidate_offsets,
    }
    if kind not in readers:
        raise ValueError(f'unknown map table {kind}')
    return readers[kind](path)

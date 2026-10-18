"""Named authoring tables for commodity markets and mining machines."""
import csv
from pathlib import Path
from system_records import commodity_columns

ROOT = Path(__file__).resolve().parents[1]


def rows(path, columns):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != columns:
            raise ValueError(f'{path}: expected columns {columns}')
        result = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path}: incomplete or extra fields')
            result.append({key: value.strip() for key, value in row.items()})
    return result


def word(token, signed=False):
    value = int(token, 0)
    low, high = (-32768, 32767) if signed else (0, 65535)
    if not low <= value <= high:
        raise ValueError('Table value exceeds its word type')
    return value & 65535


def mining_text_names():
    return [line for line in
            (ROOT / 'data/text/mining/identifiers.txt').read_text(encoding='ascii').splitlines() if line]


def mining_category_names():
    # The commander mining page displays prospect + selector A202h.
    names = mining_text_names()[2:11]
    if len(names) != 9 or len(set(names)) != 9:
        raise ValueError('Expected nine unique mining prospect identifiers')
    return names


def commodity_parameters(path):
    columns = ['commodity', 'base_price', 'price_variation', 'base_supply', 'supply_variation']
    records = rows(path, columns)
    if [row['commodity'] for row in records] != commodity_columns():
        raise ValueError('Commodity rows must match the complete market catalogue order')
    return [word(row[column], signed=True) for row in records for column in columns[1:]]


def mining_categories(path):
    records = rows(path, ['category', 'commodity'])
    if [row['category'] for row in records] != mining_category_names():
        raise ValueError('Mining categories must match the nine ordered prospect identifiers')
    commodities = {name: index for index, name in enumerate(commodity_columns())}
    if any(row['commodity'] not in commodities for row in records):
        raise ValueError('Unknown mining commodity identifier')
    return [commodities[row['commodity']] for row in records]


def mining_yields(path):
    records = rows(path, ['sample_index', 'category', 'assessment', 'reserve'])
    if len(records) != 27:
        raise ValueError('Expected 27 sampled mining yields')
    categories = {name: index for index, name in enumerate(mining_category_names())}
    assessments = {name: 0xa200 + index for index, name in enumerate(mining_text_names())}
    result = []
    for index, row in enumerate(records):
        if int(row['sample_index'], 0) != index:
            raise ValueError('Mining sample indices must be ordered and contiguous')
        if row['category'] not in categories or row['assessment'] not in assessments:
            raise ValueError('Unknown mining category or assessment text')
        result.extend((categories[row['category']], assessments[row['assessment']], word(row['reserve'])))
    return result

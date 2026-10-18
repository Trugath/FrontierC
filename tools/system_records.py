"""Compile catalogue's native fixed records through a JSON intermediate."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECIMAL_COLUMNS = ['resource_richness', 'category_0_rate', 'market_profile', 'category_1_rate',
                   'market_counter_seed', 'economic_factor_index']
COMMODITY_SOURCE = ROOT / 'data/text/market/strings.tsv'


def commodity_columns():
    names = [line.split('\t', 1)[0] for line in COMMODITY_SOURCE.read_text(encoding='ascii').splitlines()]
    if (len(names) != 31 or len(set(names)) != 31 or
            any(not re.fullmatch(r'[a-z][a-z0-9_]*', name) for name in names)):
        raise ValueError('Expected 31 unique commodity identifiers from market')
    return names


def text_selector(value, catalogue):
    """catalogue:identifier, or a word that is not an entry in that catalogue."""
    from text_references import parse_selector, resolve
    value = value.strip()
    if ':' in value:
        if not value.startswith(catalogue + ':'):
            raise ValueError(f'Expected a {catalogue} text reference')
        return resolve(value)
    return parse_selector(value)


def unsigned(value, width):
    result = int(value, 0)
    if not 0 <= result < 1 << (width * 8):
        raise ValueError(f'Record value exceeds {width}-byte field')
    return result


def decimal_byte(value, label):
    if not re.fullmatch(r'[0-9]+', value):
        raise ValueError(f'{label} must be a decimal byte')
    result = int(value, 10)
    if result > 255:
        raise ValueError(f'{label} exceeds one byte')
    return result


def _choice(value, labels, label):
    try:
        return labels.index(value)
    except ValueError:
        raise ValueError(f'unknown {label} {value!r}') from None


def catalogue_key(row):
    """Map name plus System Sector Code and system number."""
    from system_names import map_name, pack_system
    if any(not re.fullmatch(r'-?[0-9]+', row[name]) for name in ('sector_x', 'sector_y', 'number')):
        raise ValueError('sector coordinates and system number must be decimal')
    sector_x = int(row['sector_x'], 10)
    sector_y = int(row['sector_y'], 10)
    number = int(row['number'], 10)
    expected = map_name(sector_x, sector_y, number)
    if row['system'] != expected:
        raise ValueError(f'{row["system"]!r} is not system {number} of sector {sector_x},{sector_y}')
    return pack_system(sector_x, sector_y, number)


def government_byte(government, allegiance):
    from system_names import allegiance_labels, government_labels
    low = _choice(government, government_labels(), 'government')
    high = _choice(allegiance, allegiance_labels(), 'allegiance')
    return low | (high << 6)


def population_band(value):
    from system_names import population_labels
    return _choice(value, population_labels(), 'population')


def strength_nibbles(low, high):
    low = decimal_byte(low, 'low_strength')
    high = decimal_byte(high, 'high_strength')
    if low > 15 or high > 15:
        raise ValueError('strengths are nibbles, 0..15')
    return low | (high << 4)


def trade_code(token):
    """i/e selects import/export, 0..7 strength, ! illegal; raw bytes preserve unknown bits."""
    match = re.fullmatch(r'(!?)([ie])([0-7])', token)
    if match:
        illegal, direction, strength = match.groups()
        return (128 if illegal else 0) | (8 if direction == 'e' else 0) | int(strength)
    if re.fullmatch(r'0x[0-9a-fA-F]{2}', token):
        return unsigned(token, 1)
    raise ValueError(f'Invalid trade code: {token}')


def document(config, source, asset):
    entry = config['records']
    start, end = int(entry['start'], 0), int(entry['end'], 0)
    fallback, keyed = entry.getint('fallback_count'), entry.getint('keyed_count')
    if (start, end, fallback, keyed) != (0xa3, 0x3eb7, 7, 324):
        raise ValueError('catalogue fixed record layout changed')
    commodities = commodity_columns()
    columns = ['record', 'system', 'sector_x', 'sector_y', 'number', 'population'] + DECIMAL_COLUMNS[:5] + [
        'government', 'allegiance', 'economic_factor_index'] + commodities + [
        'low_strength', 'high_strength', 'map_description_text', 'system_view_text']
    records = []
    with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if reader.fieldnames != columns:
            raise ValueError('Unexpected catalogue record columns')
        for index, row in enumerate(reader):
            label = (f'fallback_record_{index}' if index < fallback else
                     f'keyed_record_{index-fallback:03d}')
            if row['record'] != label or None in row or any(v is None for v in row.values()):
                raise ValueError(f'Invalid record row {index}')
            payload = bytes(trade_code(row[name]) for name in commodities)
            if len(payload) != 31:
                raise ValueError(f'Expected 31 payload bytes: {label}')
            key = catalogue_key(row)
            if key == 0:
                raise ValueError('Zero keys are reserved for the generated terminator')
            metadata = [population_band(row['population'])]
            metadata.extend(decimal_byte(row[name], name) for name in DECIMAL_COLUMNS[:5])
            metadata.append(government_byte(row['government'], row['allegiance']))
            metadata.append(decimal_byte(row['economic_factor_index'], 'economic_factor_index'))
            records.append({'label': label, 'key': key, 'metadata': metadata,
                            'payload': list(payload),
                            'packed': strength_nibbles(row['low_strength'], row['high_strength']),
                            'responses': [text_selector(row['map_description_text'], 'chart'),
                                          text_selector(row['system_view_text'], 'systems')]})
    if len(records) != fallback + keyed:
        raise ValueError('catalogue record count changed')
    return {'format': 'fe2-system-records-v1', 'start': start, 'end': end, 'commodities': commodities, 'records': records}

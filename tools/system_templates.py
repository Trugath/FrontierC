"""Handcrafted system templates: one 64-byte record per body.

The CSV names the fields the loader and the chart actually read. Derived orbit
words, the runtime object pointer and the reserved slots stay zero; the loader
fills the first two.
"""
import csv
import re
from pathlib import Path

COLUMNS = ['size', 'seed', 'parent', 'kind', 'descriptor', 'scalar', 'orbit_longitude',
           'orbit_eccentricity', 'orbit_inclination', 'temperature_c', 'name',
           'orbit_shift', 'generation', 'basis_seed']

# Kind 0 is the root class. 0x44 sets shape 7 on top of the large-body flags.
# 0x46 is copied unconverted and placed beside its parent.
KINDS = {'root': 0, 'large_shape_7': 0x44, 'verbatim': 0x46}
TEMPERATURE_OFFSET = 273


def _u32(token, label):
    try:
        value = int(token, 0)
    except ValueError:
        raise ValueError(f'{label} must be an unsigned dword') from None
    if not 0 <= value <= 0xffffffff:
        raise ValueError(f'{label} exceeds a dword')
    return value


def _word(token, label):
    try:
        value = int(token, 0)
    except ValueError:
        raise ValueError(f'{label} must be an unsigned word') from None
    if not 0 <= value <= 0xffff:
        raise ValueError(f'{label} exceeds a word')
    return value


def _decimal_byte(token, label):
    if not re.fullmatch(r'[0-9]+', token):
        raise ValueError(f'{label} must be a decimal byte')
    value = int(token, 10)
    if value > 255:
        raise ValueError(f'{label} exceeds a byte')
    return value


def _kind(token):
    if token in KINDS:
        return KINDS[token]
    if not re.fullmatch(r'[0-9]+', token):
        raise ValueError(f'unknown body kind {token!r}')
    value = int(token, 10)
    owner = next((name for name, kind in KINDS.items() if kind == value), None)
    if owner:
        raise ValueError(f'{token} is {owner}; write that name')
    if value > 0xffff:
        raise ValueError('body kind exceeds a word')
    return value


def _temperature(token):
    if not re.fullmatch(r'-?[0-9]+', token):
        raise ValueError('temperature_c must be decimal degrees Celsius')
    celsius = int(token, 10)
    stored = celsius + TEMPERATURE_OFFSET
    if not -32768 <= celsius <= 32767 or not 0 <= stored <= 0xffff:
        raise ValueError('temperature_c does not fit the temperature word')
    return stored


def _parent(token, names):
    if token == 'frontier':
        return 0
    try:
        return names.index(token) + 1
    except ValueError:
        raise ValueError(f'unknown parent {token!r}') from None


def encode(path):
    """64-byte records for one handcrafted system list."""
    from model_names import parse_model
    path = Path(path)
    with path.open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != COLUMNS:
            raise ValueError(f'{path.name}: unexpected handcrafted template columns')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path.name}: incomplete template row')
            rows.append({key: value.strip() for key, value in row.items()})
    names = [row['name'] for row in rows]
    if len(names) != len(set(names)) or any(name == 'frontier' for name in names):
        raise ValueError(f'{path.name}: body names must be unique and must not be frontier')
    payload = bytearray()
    for row in rows:
        name = row['name'].encode('ascii')
        if not name or len(name) >= 22 or any(not 32 <= byte < 127 for byte in name):
            raise ValueError(f'{path.name}: {row["name"]!r} is not a 22-byte name')
        record = bytearray(64)
        record[0:4] = _u32(row['size'], 'size').to_bytes(4, 'little')
        record[4:8] = _u32(row['seed'], 'seed').to_bytes(4, 'little')
        record[8:10] = _parent(row['parent'], names).to_bytes(2, 'little')
        record[10:12] = _kind(row['kind']).to_bytes(2, 'little')
        record[12:14] = parse_model(row['descriptor']).to_bytes(2, 'little')
        record[14:18] = _u32(row['scalar'], 'scalar').to_bytes(4, 'little')
        record[18:20] = _word(row['orbit_longitude'], 'orbit_longitude').to_bytes(2, 'little')
        record[28:30] = _word(row['orbit_eccentricity'], 'orbit_eccentricity').to_bytes(2, 'little')
        record[30:32] = _word(row['orbit_inclination'], 'orbit_inclination').to_bytes(2, 'little')
        record[32:34] = _temperature(row['temperature_c']).to_bytes(2, 'little')
        record[34:34 + len(name)] = name
        record[58] = _decimal_byte(row['orbit_shift'], 'orbit_shift')
        record[59] = _decimal_byte(row['generation'], 'generation')
        record[62:64] = _word(row['basis_seed'], 'basis_seed').to_bytes(2, 'little')
        payload.extend(record)
    if not payload:
        raise ValueError(f'{path.name}: a system template needs at least one body')
    return list(payload)

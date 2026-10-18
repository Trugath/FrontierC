"""The 45 items a station can fit.

Row order is the interaction catalogue from `text_xb13_dummy_prox_mine`.
The row index is what the fitting screen adds to selector 0x9873. Price and
refund are multiplied by the station's tech band. Mass is the signed word
compared with free cargo space. `tech_level` is the system seed below which
the row is hidden.

`fitting` selects the buy and sell path:

- equipment: toggle the equipment bit `code xor 0x10`
- cabin: one passenger cabin
- weapon_mount: a mount whose code is `code`
- hyperdrive: the drive id `code`, refused when a drive is already fitted
- shield: add 0x40 to shield capacity
- installed: the installed-equipment byte `code`
- cabin_service: the mining-machine service

`stored_word` and `stored_byte` are the two record fields the fitting screen
does not read. Every stored word is 3. The stored byte is 0, 128 or 131.
They stay in the 12-byte record so the compiled image does not change.
"""
import csv
import struct
from pathlib import Path

from text_references import text_catalogues

FITTINGS = {
    'equipment': 0,
    'cabin': 1,
    'weapon_mount': 2,
    'hyperdrive': 3,
    'shield': 4,
    'installed': 5,
    'cabin_service': 6,
}
FIRST_ITEM = 0x73
ITEM_COUNT = 45
COLUMNS = ('item', 'price', 'refund', 'mass', 'fitting', 'code', 'tech_level', 'stored_word', 'stored_byte')


def _word(token, label, signed=False):
    try:
        value = int(token, 10)
    except ValueError:
        raise ValueError(f'{label} must be a decimal integer') from None
    if signed:
        if not -32768 <= value <= 32767:
            raise ValueError(f'{label} does not fit a signed word')
        return value & 0xffff
    if not 0 <= value <= 0xffff:
        raise ValueError(f'{label} does not fit a word')
    return value


def _byte(token, label):
    try:
        value = int(token, 10)
    except ValueError:
        raise ValueError(f'{label} must be a decimal integer') from None
    if not 0 <= value <= 255:
        raise ValueError(f'{label} does not fit a byte')
    return value


def _rows(path):
    with Path(path).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(COLUMNS):
            raise ValueError('equipment catalogue columns changed')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError('equipment catalogue has an incomplete row')
            rows.append({key: value.strip() for key, value in row.items()})
    if len(rows) != ITEM_COUNT:
        raise ValueError(f'expected {ITEM_COUNT} equipment items')
    return rows


def _checked(path):
    names = [name for name, selector in text_catalogues().items()
             if name.startswith('interaction:') and selector >= 0x9800 + FIRST_ITEM]
    names.sort(key=lambda name: text_catalogues()[name])
    expected = names[:ITEM_COUNT]
    if len(expected) != ITEM_COUNT:
        raise ValueError('interaction catalogue has no equipment block')
    rows = _rows(path)
    if [row['item'] for row in rows] != expected:
        raise ValueError('equipment rows must stay in catalogue order from the dummy proximity mine')
    return rows


def records(path):
    """Native rows: price, refund, mass, handler, code, tech_level."""
    native = []
    for row in _checked(path):
        if row['fitting'] not in FITTINGS:
            raise ValueError(f'unknown fitting {row["fitting"]!r}')
        native.append((
            _word(row['price'], 'price'),
            _word(row['refund'], 'refund'),
            _word(row['mass'], 'mass', signed=True),
            FITTINGS[row['fitting']],
            _byte(row['code'], 'code'),
            _byte(row['tech_level'], 'tech_level'),
        ))
    return native


def image_bytes(path):
    data = bytearray()
    for row, native in zip(_checked(path), records(path)):
        price, refund, mass, handler, code, tech = native
        data.extend(struct.pack('<HHHHBBBB', price, refund, mass, _word(row['stored_word'], 'stored_word'),
                                handler * 2, _byte(row['stored_byte'], 'stored_byte'), code, tech))
    return list(data)

"""Docking-slot coordinates and the model lists embedded in that image.

A station's shape word selects a bank and is also the highest slot index.
Each slot is three signed words at `base + slot * 8`. The following two
bytes belong to the next slot, or are zero when nothing follows. The
coordinates are station-frame units. Rotating stations run them through the
orientation matrix and shift the dot product down six bits. A fixed station
uses the control bank the same way. A free station publishes the control
words directly and shifts Z left by two.

Category offers are model slots stored in the unused tail of the attachment
bank. Kind 0 is goods and kind 1 is equipment; the table is
`allegiance * 2 + kind`. Allegiance 0, 1 and 2 are independent, imperial
and federation, the same order as the candidate systems. Kind 9 is the
station's own ships. Kind 12 is taken from the mission detail instead of a
random draw. Kinds 6, 7, 8, 10 and 11 are selected by that kind number; no
caller in the recovered market names them, so the offer keeps the kind number.
"""
import csv
import struct
from pathlib import Path

from model_names import parse_model

ATTACHMENT_BASE = 0x0443
ATTACHMENT_SIZE = 0x0567 - ATTACHMENT_BASE

# bank, byte base, slot count. Counts stop at the next bank or the offer lists.
ATTACHMENT_BANKS = (
    ('shape_1', 0x00, 2),
    ('shape_3', 0x0e, 4),
    ('other', 0x2c, 6),
)
COMMAND_BANKS = (
    ('shape_1', 0x00, 4),
    ('shape_3', 0x1e, 4),
    ('other', 0x3c, 4),
    ('shape_0', 0x5a, 4),
)
COMMAND_SIZE = 0x0633 - 0x05bb
CONTROL_BANKS = (
    ('free', 0x00, 4),
    ('fixed', 0x1e, 4),
)
CONTROL_SIZE = 0x066f - 0x0633

# Table index order. The byte offset is where the model words sit in the
# attachment image, and the pointer stored beside the count is that offset
# plus the attachment image base.
OFFERS = (
    ('independent_goods', 0x98, 7),
    ('independent_equipment', 0xa6, 12),
    ('imperial_goods', 0x7e, 7),
    ('imperial_equipment', 0x8c, 6),
    ('federation_goods', 0x5a, 7),
    ('federation_equipment', 0x68, 11),
    ('kind_6', 0xbe, 8),
    ('kind_7', 0xce, 18),
    ('kind_8', 0xf2, 7),
    ('station_ships', 0x100, 2),
    ('kind_10', 0x104, 2),
    ('kind_11', 0x108, 3),
    ('from_detail', 0x10e, 11),
)


def _rows(path, columns):
    with Path(path).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(columns):
            raise ValueError(f'{Path(path).name}: unexpected columns')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{Path(path).name}: incomplete row')
            rows.append({key: value.strip() for key, value in row.items()})
    if not rows:
        raise ValueError(f'{Path(path).name}: no rows')
    return rows


def _signed(token, label):
    try:
        value = int(token, 10)
    except ValueError:
        raise ValueError(f'{label} must be a decimal integer') from None
    if not -32768 <= value <= 32767:
        raise ValueError(f'{label} does not fit a signed word')
    return value


def _place(image, banks, rows, path_name):
    grouped = {name: [] for name, _, _ in banks}
    for row in rows:
        if row['bank'] not in grouped:
            raise ValueError(f'{path_name}: unknown bank {row["bank"]!r}')
        grouped[row['bank']].append(row)
    for name, base, count in banks:
        found = grouped[name]
        if len(found) != count:
            raise ValueError(f'{path_name}: {name} needs {count} slots')
        for slot, row in enumerate(found):
            if int(row['slot'], 10) != slot:
                raise ValueError(f'{path_name}: {name} slots must be 0..{count - 1} in order')
            at = base + slot * 8
            for part, label in enumerate(('x', 'y', 'z')):
                value = _signed(row[label], label)
                image[at + 2 * part:at + 2 * part + 2] = struct.pack('<h', value)
    return image


def attachment_bytes(slots_path, offers_path):
    image = bytearray(ATTACHMENT_SIZE)
    _place(image, ATTACHMENT_BANKS, _rows(slots_path, ('bank', 'slot', 'x', 'y', 'z')), Path(slots_path).name)
    offers = {name: [] for name, _, _ in OFFERS}
    for row in _rows(offers_path, ('offer', 'model')):
        if row['offer'] not in offers:
            raise ValueError(f'unknown category offer {row["offer"]!r}')
        offers[row['offer']].append(parse_model(row['model']))
    for name, offset, count in OFFERS:
        models = offers[name]
        if len(models) != count:
            raise ValueError(f'{name} has {len(models)} models; the image holds {count}')
        for index, model in enumerate(models):
            struct.pack_into('<H', image, offset + 2 * index, model)
    return list(image)


def category_pair_words(offers_path):
    counts = {name: 0 for name, _, _ in OFFERS}
    for row in _rows(offers_path, ('offer', 'model')):
        if row['offer'] not in counts:
            raise ValueError(f'unknown category offer {row["offer"]!r}')
        parse_model(row['model'])
        counts[row['offer']] += 1
    words = []
    for name, offset, count in OFFERS:
        if counts[name] != count:
            raise ValueError(f'{name} has {counts[name]} models; the image holds {count}')
        words.extend((count, ATTACHMENT_BASE + offset))
    return words


def command_bytes(path):
    image = bytearray(COMMAND_SIZE)
    _place(image, COMMAND_BANKS, _rows(path, ('bank', 'slot', 'x', 'y', 'z')), Path(path).name)
    return list(image)


def control_words(path):
    image = bytearray(CONTROL_SIZE)
    _place(image, CONTROL_BANKS, _rows(path, ('bank', 'slot', 'x', 'y', 'z')), Path(path).name)
    return [struct.unpack_from('<H', image, index)[0] for index in range(0, len(image), 2)]

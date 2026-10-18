"""Hit boxes on the status list pages.

Each row is one pointer region: left, top, right, bottom, and the key the
click sends. Keys `row_0` .. `row_25` are 0x80 plus the row. Crew accepts
rows below 0x94, mining rows below 0x8a, and cargo rows below 0x9a. `refuel`
is key 0x66. The list ends at the 0xffff the pointer code stops on. The first
box starts two bytes before the old widget dump, on the word that used to sit
at the end of the quantity-text list.
"""
import csv
from pathlib import Path

COLUMNS = ('left', 'top', 'right', 'bottom', 'key')
KEYS = {f'row_{index}': 0x80 + index for index in range(26)}
KEYS['refuel'] = 0x66


def widget_words(path):
    with Path(path).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(COLUMNS):
            raise ValueError('status widgets need left, top, right, bottom, key')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError('status widget row is incomplete')
            rows.append({key: value.strip() for key, value in row.items()})
    if [row['key'] for row in rows] != [f'row_{index}' for index in range(26)] + ['refuel']:
        raise ValueError('status widgets must be row_0 through row_25 and then refuel')
    words = []
    for row in rows:
        for name in ('left', 'top', 'right', 'bottom'):
            try:
                value = int(row[name], 10)
            except ValueError:
                raise ValueError(f'{name} must be a decimal coordinate') from None
            if not 0 <= value <= 65535:
                raise ValueError(f'{name} exceeds a word')
            words.append(value)
        words.append(KEYS[row['key']])
    words.append(0xffff)
    return words

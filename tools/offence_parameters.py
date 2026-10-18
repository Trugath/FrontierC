"""The eleven offence rows: score, fine, flags and police-search threshold.

Row order is the offence text catalog after the two fine messages. `alert`,
`message` and `find_police` are the flag bits the commit path tests. The fine
column is the word the notice prints; the local fine increases by ten times
that word.
"""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLAGS = (('alert', 1), ('message', 2), ('find_police', 4))


def offence_names():
    names = [line for line in
             (ROOT / 'data/text/offences/identifiers.txt').read_text(encoding='ascii').splitlines() if line]
    if names[:2] != ['first_fine', 'additional_fine'] or len(names) != 13:
        raise ValueError('Offence names must follow the two fine messages')
    return names[2:]


def _word(token, label):
    if not re.fullmatch(r'[0-9]+', token):
        raise ValueError(f'{label} must be a decimal word')
    value = int(token, 10)
    if value > 0xffff:
        raise ValueError(f'{label} exceeds a word')
    return value


def _flag(token, name):
    if token == 'yes':
        return True
    if token == 'no':
        return False
    raise ValueError(f'{name} must be yes or no')


def words(path):
    names = offence_names()
    columns = ('offence', 'score', 'fine', 'alert', 'message', 'find_police', 'threshold')
    with Path(path).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(columns):
            raise ValueError('offence parameters: unexpected columns')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError('offence parameters: incomplete row')
            rows.append({key: value.strip() for key, value in row.items()})
    if [row['offence'] for row in rows] != names:
        raise ValueError('offence parameters must follow the offence text, in order')
    values = []
    for row in rows:
        flags = 0
        for name, bit in FLAGS:
            if _flag(row[name], name):
                flags |= bit
        values.extend((_word(row['score'], 'score'), _word(row['fine'], 'fine'), flags,
                       _word(row['threshold'], 'threshold')))
    return values

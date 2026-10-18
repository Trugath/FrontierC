"""Compile status contract-creation chances into the subtype callback table.

Sixteen rows (subtypes 0..15) give a word threshold and an optional handler.
A zero threshold must have no handler; a nonzero threshold requires one,
because the appender calls it when the resident random value is at most the
threshold.
"""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HANDLERS = ('create_contract_related_object', 'create_two_contract_objects',
            'create_three_contract_objects', 'create_contract_mode_four_object')


def document(config, source, asset):
    entry = config['contract_creation_callbacks']
    if entry['module'] != 'status':
        raise ValueError('Contract callbacks belong to status')
    with asset(source, entry['entries']).open(encoding='ascii', newline='') as stream:
        rows = list(csv.reader(stream))
    if not rows or [v.strip() for v in rows[0]] != ['subtype', 'threshold', 'callback']:
        raise ValueError('Expected subtype,threshold,callback header')
    subtypes = []
    for row in rows[1:]:
        if len(row) != 3:
            raise ValueError('Contract callback rows need three fields')
        subtype, threshold, handler = int(row[0], 0), int(row[1], 0), row[2].strip()
        if subtype != len(subtypes) or not 0 <= threshold <= 0xffff:
            raise ValueError('Subtypes must be consecutive with 16-bit thresholds')
        if (threshold == 0) != (handler == '') or (handler and handler not in HANDLERS):
            raise ValueError(f'Subtype {subtype}: zero threshold iff no handler; known handlers only')
        subtypes.append({'subtype': subtype, 'threshold': threshold, 'callback': handler or None})
    start, end = int(entry['start'], 0), int(entry['end'], 0)
    if len(subtypes) != 16 or end - start != 64:
        raise ValueError('Expected 16 contract subtypes in 64 bytes')
    return {'format': 'fe2-contract-callbacks-v1', 'start': start, 'end': end, 'subtypes': subtypes}


def load(source):
    data = json.loads(Path(source).read_text(encoding='utf-8'))
    if data['format'] != 'fe2-contract-callbacks-v1':
        raise ValueError('Unsupported contract callback intermediate')
    return data

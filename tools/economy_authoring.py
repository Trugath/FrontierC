"""Passenger, remote-subject and mission-reward rows.

A passage name is the interaction text the bulletin expands. A remote subject
is stored as its distance from the plain-name entry, which the advert adds
back before display. Reward amounts are the credit words the notice prints.
"""
import csv
import re
from pathlib import Path

from text_references import resolve

SUBJECT_BASE = 'interaction:text_empty_413'


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


def _credits(token):
    if not re.fullmatch(r'[0-9]+', token):
        raise ValueError(f'reward must be a decimal credit amount, not {token!r}')
    value = int(token, 10)
    if value > 0xffff:
        raise ValueError('reward exceeds a word')
    return value


def passage_pairs(path):
    words = []
    for row in _rows(path, ('passage', 'reward_multiplier')):
        if not row['passage'].startswith('interaction:'):
            raise ValueError(f'{row["passage"]} is not an interaction text')
        multiplier = row['reward_multiplier']
        if not re.fullmatch(r'[0-9]+', multiplier):
            raise ValueError('reward multiplier must be a decimal word')
        factor = int(multiplier, 10)
        if factor > 0xffff:
            raise ValueError('reward multiplier exceeds a word')
        words.extend((resolve(row['passage']), factor))
    return words


def remote_subjects(path):
    base = resolve(SUBJECT_BASE)
    words = []
    for row in _rows(path, ('reward', 'subject')):
        if not row['subject'].startswith('interaction:'):
            raise ValueError(f'{row["subject"]} is not an interaction text')
        offset = resolve(row['subject']) - base
        if offset < 0 or offset > 0xffff:
            raise ValueError(f'{row["subject"]} is not a remote-subject text')
        words.extend((_credits(row['reward']), offset))
    return words


def reward_words(path):
    return [_credits(row['reward']) for row in _rows(path, ('reward',))]


def q15_coefficients(path, index_name, labels):
    """One Q15 word per named index. 32768 would be one; 32767 is the saturated word."""
    rows = _rows(path, (index_name, 'coefficient'))
    found = [row[index_name] for row in rows]
    if found != list(labels):
        raise ValueError(f'{Path(path).name}: expected {index_name} rows {list(labels)}')
    words = []
    for row in rows:
        token = row['coefficient']
        if not re.fullmatch(r'[0-9]+', token):
            raise ValueError(f'{row[index_name]}: coefficient must be a decimal Q15 word')
        value = int(token, 10)
        if value > 32767:
            raise ValueError(f'{row[index_name]}: coefficient exceeds the saturated Q15 value 32767')
        words.append(value)
    return words

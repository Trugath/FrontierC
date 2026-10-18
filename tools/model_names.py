"""Resolve main-bank model directory names to slot numbers."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'data/models/frontier/model_directory.tsv'


def model_slots():
    slots = {}
    for line in DIRECTORY.read_text(encoding='ascii').splitlines():
        slot, name = line.split('\t')
        if name == '-':
            continue
        if name in slots:
            raise ValueError(f'Duplicate model name {name}')
        slots[name] = int(slot)
    return slots


def parse_model(token):
    """A directory name, or a number whose slot has no directory name."""
    token = token.strip()
    slots = model_slots()
    if token in slots:
        return slots[token]
    if not re.fullmatch(r'0x[0-9A-Fa-f]+|[0-9]+', token):
        raise ValueError(f'Unknown model {token!r}')
    value = int(token, 0)
    if not 0 <= value <= 0xffff:
        raise ValueError(f'Model slot exceeds a word: {token}')
    owner = next((name for name, slot in slots.items() if slot == value), None)
    if owner:
        raise ValueError(f'{token} is {owner}; write that name')
    return value

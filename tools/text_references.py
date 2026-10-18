"""Resolve authored catalogue:identifier references to encoded text selectors."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Selector of each catalogue's first entry. The high bits choose the text bank.
CATALOGUES = {'systems': 0x8000, 'chart': 0x8400, 'flight': 0x8600,
              'interaction': 0x9800, 'status': 0x9c00}


def text_catalogues():
    result = {}
    for catalogue, base in CATALOGUES.items():
        names = [line for line in
                 (ROOT / f'data/text/{catalogue}/identifiers.txt').read_text(encoding='ascii').splitlines() if line]
        if len(names) != len(set(names)):
            raise ValueError(f'Duplicate text identifiers in {catalogue}')
        result.update({f'{catalogue}:{name}': base + index for index, name in enumerate(names)})
    return result


def resolve(reference, catalogue=None, optional=False):
    if reference is None and optional:
        return 0
    texts = text_catalogues()
    if not isinstance(reference, str) or reference not in texts:
        raise ValueError(f'Unknown text reference: {reference!r}')
    if catalogue and not reference.startswith(catalogue + ':'):
        raise ValueError(f'Expected a {catalogue} text reference')
    return texts[reference]


def parse_selector(token):
    """A catalogue:identifier, or a number that is not itself a catalogue entry."""
    import re
    token = token.strip()
    texts = text_catalogues()
    if ':' in token:
        return resolve(token)
    if not re.fullmatch(r'0x[0-9A-Fa-f]+|[0-9]+', token):
        raise ValueError(f'Unknown text reference: {token!r}')
    value = int(token, 0)
    if not 0 <= value <= 0xffff:
        raise ValueError(f'Text selector exceeds a word: {token}')
    owner = next((name for name, selector in texts.items() if selector == value), None)
    if owner:
        raise ValueError(f'{token} is {owner}; write that name')
    return value

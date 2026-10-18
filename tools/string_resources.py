"""Compile escaped text catalogs and their relative-word directories."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def decode(text):
    data = bytearray()
    position = 0
    while position < len(text):
        char = text[position]
        if char == '\\':
            if text[position:position+2] == '\\\\':
                data.append(92)
                position += 2
                continue
            match = re.match(r'\\x([0-9A-Fa-f]{2})', text[position:])
            if not match:
                raise ValueError('Expected \\\\ or \\xHH escape')
            data.append(int(match[1], 16))
            position += 4
        else:
            if not 32 <= ord(char) < 127:
                raise ValueError('Escape non-ASCII or control bytes as \\xHH')
            data.append(ord(char))
            position += 1
    if 0 in data:
        raise ValueError('Text entries cannot contain embedded NUL bytes')
    return list(data)


def document(config, source, asset):
    entry = config['strings']
    records = []
    names = set()
    for line in asset(source, entry['entries']).read_text(encoding='ascii').splitlines():
        name, value = line.split('\t', 1)
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) or name in names:
            raise ValueError('Expected unique assembly identifiers')
        names.add(name)
        records.append({'name':name, 'bytes':decode(value)})
    if 'count' in entry and len(records) != entry.getint('count'):
        raise ValueError('Text entry count differs from catalog')
    document = {'format':'fe2-string-directory-v1', 'module':entry['module'], 'entries':records}
    if 'directory_offset' in entry:
        document['directory_offset'] = int(entry['directory_offset'], 0)
        document['end_offset'] = int(entry['end_offset'], 0)
    return document

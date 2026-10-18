"""Backward-walk contact offsets, stored as selector/length word pairs.

model_contact_extension_walk.inc reads length first while walking backward.
Selector & 6 chooses a basis word offset; bit0 negates that basis vector.
Other selector bits feed the generated bound and remain encoded.
"""
import shlex
import struct


def compile_source(source):
    output = bytearray()
    for number, line in enumerate(source.splitlines(), 1):
        tokens = shlex.split(line, comments=True)
        if not tokens:
            continue
        operation = tokens.pop(0)
        if operation not in ('basis_offset', 'stop'):
            raise ValueError(f'Unknown contact offset operation on line {number}')
        fields = {}
        for token in tokens:
            key, value = token.split('=', 1)
            if key in fields:
                raise ValueError('Duplicate contact offset field')
            fields[key] = value
        if operation == 'stop':
            if output or set(fields) != {'unused_word'}:
                raise ValueError('Backward-walk stop must be first in physical storage')
            unused = int(fields['unused_word'], 0)
            if not 0 <= unused <= 65535:
                raise ValueError('Unused preceding word exceeds 16 bits')
            output.extend(struct.pack('<HH', unused, 0))
            continue
        if set(fields) != {'selector', 'basis_word_offset', 'negate', 'length'}:
            raise ValueError('Incorrect contact offset fields')
        selector, basis, length = (int(fields[k], 0) for k in
                                   ('selector', 'basis_word_offset', 'length'))
        if (not 0 <= selector <= 65535 or basis != selector & 6 or
                fields['negate'] != str(bool(selector & 1)).lower() or
                not -32768 <= length <= 32767 or length == 0):
            raise ValueError('Contact offset fields disagree with encoded selector/length')
        output.extend(struct.pack('<Hh', selector, length))
    if not output:
        raise ValueError('Contact offsets require at least one pair')
    return bytes(output)


def decode_source(data):
    if not data or len(data) % 4:
        raise ValueError('Contact offset span must contain complete word pairs')
    lines = ['# Physical storage order; contact traversal visits these pairs backward.',
             '# Zero length stops without consuming the preceding word.']
    for selector, length in struct.iter_unpack('<Hh', data):
        if length == 0:
            lines.append(f'stop unused_word=0x{selector:04x}')
            continue
        lines.append(f'basis_offset selector=0x{selector:04x} '
                     f'basis_word_offset={selector & 6} '
                     f'negate={str(bool(selector & 1)).lower()} length={length}')
    result = '\n'.join(lines) + '\n'
    if compile_source(result) != data:
        raise AssertionError('Contact offset roundtrip changed bytes')
    return result

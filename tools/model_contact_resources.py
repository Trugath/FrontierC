"""Native contact-program source for the recovered resident model interpreter.

Tag/result bytes retain unknown low bits. Operation names describe the four
payload/plane-predicate paths at image12DC7, not inferred object identities.
"""
import shlex


OPERATIONS = {'planes_all_le': 0, 'planes_all_gt': 0x40,
              'child': 0x80, 'result_planes_all_le': 0xc0}


def number(value, maximum=255):
    result = int(value, 0)
    if not 0 <= result <= maximum:
        raise ValueError('Contact operand out of range')
    return result


def compile_source(text):
    result = bytearray()
    ended = False
    for line_number, line in enumerate(text.splitlines(), 1):
        tokens = shlex.split(line, comments=True)
        if not tokens:
            continue
        try:
            if ended:
                raise ValueError('Record after contact terminator')
            if tokens == ['end']:
                result.append(0)
                ended = True
                continue
            operation = tokens[0]
            if operation not in OPERATIONS:
                raise ValueError('Unknown contact operation')
            fields = {}
            for token in tokens[1:]:
                key, value = token.split('=', 1)
                if key in fields:
                    raise ValueError('Duplicate contact operand')
                fields[key] = value
            tag = number(fields['tag'])
            if not tag or tag & 0xc0 != OPERATIONS[operation]:
                raise ValueError('Tag does not match contact operation')
            required = {'tag', 'result'}
            if tag & 0x20:
                required |= {'field', 'shift'}
            if operation == 'child':
                required |= {'model', 'transform', 'vertex'}
            else:
                required |= {'refs'}
            if set(fields) != required:
                raise ValueError('Incorrect contact operands')
            result.extend((tag, number(fields['result'])))
            if tag & 0x20:
                result.extend((number(fields['field']), number(fields['shift'])))
            if operation == 'child':
                model = number(fields['model'], 65535)
                result.extend((model >> 8, model & 255,
                               number(fields['transform']), number(fields['vertex'])))
            else:
                refs = [] if fields['refs'] == '-' else [number(v) for v in fields['refs'].split(',')]
                if 0 in refs:
                    raise ValueError('Zero plane reference is a list terminator')
                result.extend(refs)
                result.append(0)
        except (KeyError, ValueError) as error:
            raise ValueError(f'Contact source line {line_number}: {error}') from error
    if not ended:
        raise ValueError('Missing contact terminator')
    return bytes(result)


def decode_source(data, start=0):
    """Decode through first program terminator; return source and next byte index."""
    position = start
    lines = ['# Recovered model contact program; tag/result low bits retain original values.']

    def take():
        nonlocal position
        if not 0 <= position < len(data):
            raise ValueError('Truncated contact program')
        value = data[position]
        position += 1
        return value

    while True:
        tag = take()
        if not tag:
            lines.append('end')
            break
        operation = next(name for name, mask in OPERATIONS.items() if tag & 0xc0 == mask)
        values = [operation, f'tag=0x{tag:02x}', f'result=0x{take():02x}']
        if tag & 0x20:
            values += [f'field={take()}', f'shift=0x{take():02x}']
        if operation == 'child':
            model = take() * 256 + take()
            values += [f'model={model}', f'transform=0x{take():02x}', f'vertex={take()}']
        else:
            refs = []
            while (reference := take()):
                refs.append(reference)
            values.append('refs=' + (','.join(map(str, refs)) if refs else '-'))
        lines.append(' '.join(values))
    source = '\n'.join(lines) + '\n'
    if compile_source(source) != bytes(data[start:position]):
        raise ValueError('Contact decoder roundtrip failed')
    return source, position

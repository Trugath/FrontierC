"""Compile structured PC-speaker JSON to native note and pattern bytes."""
import json
import re
from pathlib import Path

FORMAT = 'frontier-speaker-scripts-v1'


def fields(record, expected):
    if not isinstance(record, dict) or set(record) != set(expected):
        raise ValueError('Unexpected speaker command fields')


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'Speaker integer must be in {low}..{high}')
    return value


def named_records(records, count):
    if not isinstance(records, list) or len(records) != count:
        raise ValueError(f'Expected {count} speaker scripts')
    names = set()
    for record in records:
        fields(record, ('name', 'commands'))
        name = record['name']
        if not isinstance(name, str) or not re.fullmatch(r'[a-z][a-z0-9_]*', name) or name in names:
            raise ValueError('Speaker script names must be valid and unique')
        if not isinstance(record['commands'], list) or not record['commands']:
            raise ValueError('Speaker script commands cannot be empty')
        names.add(name)
    return records


def pattern_commands(commands, depth=0):
    if not isinstance(commands, list) or not commands:
        raise ValueError('Empty speaker loop or pattern')
    output = bytearray()
    for index, command in enumerate(commands):
        if not isinstance(command, dict):
            raise ValueError('Expected a speaker command object')
        op = command.get('op')
        if op == 'loop':
            fields(command, ('op', 'count', 'commands'))
            if depth >= 4:
                raise ValueError('Speaker loop nesting exceeds four stack entries')
            count = integer(command['count'], 1, 256)
            body = pattern_commands(command['commands'], depth + 1)
            if all(item['op'] in ('noise_on', 'noise_off') for item in command['commands']):
                raise ValueError('Speaker loops must contain a wait or pitch step')
            output.extend((0x81, count & 255))
            output.extend(body)
            output.append(0x82)
        elif op == 'wait':
            fields(command, ('op', 'ticks'))
            output.extend((0x80, integer(command['ticks'], 0, 255)))
        elif op == 'pitch_delta':
            fields(command, ('op', 'steps'))
            value = integer(command['steps'], -128, 127) & 255
            if 0x7e <= value <= 0x83:
                raise ValueError('Pitch delta overlaps a speaker opcode')
            output.append(value)
        elif op in ('noise_on', 'noise_off', 'finish_note'):
            fields(command, ('op',))
            if op == 'finish_note' and (depth or index != len(commands) - 1):
                raise ValueError('finish_note must terminate the top-level pattern')
            output.append({'noise_on':0x7f, 'noise_off':0x7e, 'finish_note':0x83}[op])
        else:
            raise ValueError('Unknown speaker pattern operation')
    if not depth and commands[-1].get('op') != 'finish_note':
        raise ValueError('Speaker patterns must finish their note')
    return bytes(output)


def compile_document(document):
    if isinstance(document, dict) and '$schema' in document:
        fields(document, ('$schema', 'format', 'patterns', 'effects'))
        if not isinstance(document['$schema'], str):
            raise ValueError('Speaker schema reference must be a string')
    else:
        fields(document, ('format', 'patterns', 'effects'))
    if document['format'] != FORMAT:
        raise ValueError('Unsupported speaker script format')
    patterns = named_records(document['patterns'], 10)
    pattern_ids = {record['name']: index for index, record in enumerate(patterns)}
    output = {'patterns': [(record['name'], pattern_commands(record['commands'])) for record in patterns],
              'effects': []}
    for record in named_records(document['effects'], 9):
        compiled = bytearray()
        for index, command in enumerate(record['commands']):
            if not isinstance(command, dict):
                raise ValueError('Expected a speaker effect command object')
            op = command.get('op')
            if op == 'note':
                fields(command, ('op', 'pattern', 'note', 'ticks'))
                pattern = command['pattern']
                if not isinstance(pattern, str) or pattern not in pattern_ids:
                    raise ValueError('Unknown speaker note pattern')
                note = 255 if command['note'] == 'keep' else integer(command['note'], 0, 254)
                compiled.extend((pattern_ids[pattern], note, integer(command['ticks'], 0, 255)))
            elif op == 'pause':
                fields(command, ('op', 'ticks'))
                compiled.extend((254, integer(command['ticks'], 0, 255)))
            elif op == 'end':
                fields(command, ('op',))
                if index != len(record['commands']) - 1:
                    raise ValueError('Commands cannot follow an effect end')
                compiled.append(255)
            else:
                raise ValueError('Unknown speaker effect operation')
        if record['commands'][-1].get('op') != 'end':
            raise ValueError('Speaker effects must end explicitly')
        output['effects'].append((record['name'], bytes(compiled)))
    return output


def load(path):
    return compile_document(json.loads(Path(path).read_text(encoding='utf-8-sig')))

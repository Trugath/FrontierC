"""Compile native AdLib patches and sequencer records to a checked JSON bank.

Unknown byte runs remain explicitly raw. This compiler does not guess stream
boundaries or reclassify undecoded bytes as commands.
"""
from __future__ import annotations

import configparser
import csv
import json
import re
from pathlib import Path
from adlib_commands import COMMANDS

ROOT = Path(__file__).resolve().parents[1]
PATCH_FIELDS = ('modulator_characteristics_20', 'carrier_characteristics_23',
    'feedback_connection_c0', 'modulator_waveform_e0', 'carrier_waveform_e3',
    'modulator_level_40', 'carrier_level_43', 'modulator_attack_decay_60',
    'carrier_attack_decay_63', 'modulator_sustain_release_80',
    'carrier_sustain_release_83')
KINDS = {'label', 'pointer', 'word', 'raw', 'byte', 'patch', 'event', 'command', 'note'}


def rows(path):
    with Path(path).open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream, delimiter='\t' if str(path).endswith('.tsv') else ','))


def document(source):
    source = Path(source)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(source, encoding='utf-8')
    bank = config['bank']
    directory = source.parent
    patches = {}
    for row in rows(directory / bank['patches']):
        if row['name'] in patches:
            raise ValueError('Duplicate patch')
        patches[row['name']] = [int(row[field], 0) for field in PATCH_FIELDS]
    result = {'schema': 'fe2-adlib-bank-v1', 'origin': int(bank['origin'], 0),
              'end': int(bank['end'], 0), 'symbols': dict(config['symbols']),
              'patch_fields': list(PATCH_FIELDS), 'patches': patches,
              'records': rows(directory / bank['records'])}
    encode(result)
    return result


def encode(document):
    labels = {}
    for row in document['records']:
        if row['kind'] == 'label':
            if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', row['operands']) or row['operands'] in labels:
                raise ValueError('Invalid or duplicate label')
            labels[row['operands']] = int(row['offset'], 0)
    symbols = {key: int(value, 0) for key, value in document['symbols'].items()}
    def value(token):
        token = token.strip()
        if token in labels:
            return labels[token]
        if token in symbols:
            return symbols[token]
        return int(token, 0)
    output = bytearray()
    for row in document['records']:
        kind = row['kind']
        if kind not in KINDS or int(row['offset'], 0) != document['origin'] + len(output):
            raise ValueError(f'Unknown record or noncontiguous bank layout: {row} expected {document["origin"] + len(output):04x}')
        if kind == 'label':
            continue
        tokens = [token.strip() for token in row['operands'].split(',')]
        values = document['patches'][tokens[0]] if kind == 'patch' else [value(token) for token in tokens]
        if kind == 'patch' and len(values) != 11 or kind == 'event' and len(values) != 2 or kind == 'note' and len(values) not in (2, 3):
            raise ValueError('Invalid typed record width')
        if kind == 'command' and not 0x80 <= values[0] <= 0xc9:
            raise ValueError('Invalid command opcode')
        if kind == 'command' and len(values) != COMMANDS[values[0]].width:
            raise ValueError('Command operands disagree with recovered handler')
        if kind == 'pointer' and any(token not in labels for token in tokens):
            raise ValueError('Pointer must reference a bank label')
        width = 2 if kind in ('pointer', 'word') else 1
        for number in values:
            if not 0 <= number < 1 << (8 * width):
                raise ValueError('Resource operand out of range')
            output.extend(number.to_bytes(width, 'little'))
    if document['origin'] + len(output) != document['end']:
        raise ValueError('Bank extent differs from manifest')
    return bytes(output)


SHORT = {
    'ADLIB_LOOP_COUNT_80': 'loop_count', 'ADLIB_LOOP_END_81': 'loop_end',
    'ADLIB_START_EVENT_82': 'start_event', 'ADLIB_RELEASE_THRESHOLD_83': 'channel_clock_divider',
    'ADLIB_JUMP_84': 'jump', 'ADLIB_CALL_85': 'call', 'ADLIB_RETURN_86': 'return',
    'ADLIB_OCTAVE_BIAS_87': 'octave_bias', 'ADLIB_STOP_88': 'stop', 'ADLIB_REST_89': 'rest',
    'ADLIB_INSTRUMENT_90': 'instrument', 'ADLIB_PITCH_SLIDE_91': 'pitch_slide',
    'ADLIB_DISABLE_SLIDE_92': 'disable_slide', 'ADLIB_FREQUENCY_BIAS_93': 'frequency_bias',
    'ADLIB_PITCH_MODULATION_95': 'pitch_modulation', 'ADLIB_LEVEL_9E': 'level',
    'ADLIB_DELAY_A0': 'delay', 'ADLIB_CHANNEL_TEMPO_A9': 'channel_tempo',
    'ADLIB_LEVEL_BIAS_AB': 'level_bias', 'ADLIB_SET_CHANNEL_LEVEL_AC': 'set_channel_level',
    'ADLIB_ADD_CHANNEL_LEVEL_AD': 'add_channel_level', 'ADLIB_CLEAR_CHANNEL_B3': 'clear_channel',
    'ADLIB_RANDOM_FREQUENCY_OFFSET_B5': 'random_frequency_offset', 'ADLIB_NOP_BB': 'nop',
}
BRANCH = {0x81, 0x84, 0x85}
NO_FALL = {0x84, 0x86, 0x88}


def _u16(image, addr):
    return image[addr] | (image[addr + 1] << 8)


def _hex_byte(value):
    return f'0x{value:02x}'


def _hex_word(value):
    return f'0x{value:04x}'


def decode_bank(image, origin, end, symbols, ambient_pitch, ambient_level):
    """Recover the editable bank from an AdLib overlay image.

    Instrument and event directories are word tables. Patches are the 11-byte
    targets of the instrument directory. Streams are walked with the recovered
    command widths; call, jump, and loop-end targets become pointers. Bytes no
    event reaches stay raw.
    """
    image = bytes(image)
    if not origin < end <= len(image):
        raise ValueError('AdLib bank leaves the overlay')
    by_opcode = {}
    for name, value in symbols.items():
        opcode = int(value, 0)
        if opcode in by_opcode:
            raise ValueError(f'Duplicate AdLib opcode {name}')
        by_opcode[opcode] = name
    event_at = _u16(image, origin)
    if not origin < event_at < end:
        raise ValueError('AdLib event directory is outside the bank')
    instrument_at = origin + 2
    pointers = []
    addr = instrument_at
    while addr < end:
        value = _u16(image, addr)
        if origin <= value < end:
            pointers.append(value)
        addr += 2
        if pointers and addr >= min(pointers):
            break
    if not pointers or (addr - instrument_at) % 2:
        raise ValueError('AdLib instrument directory is not a word table')
    instrument_end = addr
    instrument_count = (instrument_end - instrument_at) // 2
    patch_at = sorted(set(pointers))
    for start, nxt in zip(patch_at, patch_at[1:]):
        if nxt - start < 11:
            raise ValueError('AdLib patches overlap')
    if patch_at[-1] + 11 > event_at:
        raise ValueError('AdLib patches run into the event directory')
    event_words = []
    addr = event_at
    while addr + 2 <= end:
        value = _u16(image, addr)
        event_words.append(value)
        addr += 2
        targets = [word for word in event_words if origin <= word < end]
        if targets and addr >= min(targets):
            break
    if not event_words or addr != min(word for word in event_words if origin <= word < end):
        raise ValueError('AdLib event directory is not contiguous')
    event_end = addr
    headers = {word for word in event_words if origin <= word < end}
    occupied = set(range(origin, instrument_end))
    occupied.update(range(event_at, event_end))
    for start in patch_at:
        occupied.update(range(start, start + 11))
    for header in headers:
        occupied.update(range(header, header + 2))
    ambient = {ambient_pitch, ambient_level}
    records = []
    covered = set()
    starts = set()

    def add(offset, kind, operands, description):
        records.append({'offset': f'0x{offset:04x}', 'kind': kind, 'operands': operands, 'description': description})

    def target_name(target):
        if target == event_at:
            return 'event_record_pointers'
        if target in headers:
            return f'event_record_{target:04x}'
        if target == ambient_pitch:
            return 'ambient_pitch_parameter'
        if target == ambient_level:
            return 'ambient_level_parameter'
        return f'stream_{target:04x}'

    pending = [header + 2 for header in sorted(headers)]
    branches = set()
    while pending:
        addr = pending.pop(0)
        while origin <= addr < end and addr not in starts:
            if addr in occupied or addr in covered:
                if addr in covered and addr not in starts:
                    raise ValueError(f'AdLib stream enters an instruction at 0x{addr:04x}')
                break
            op = image[addr]
            if op < 0x80:
                if addr + 2 > end or (addr + 1) in occupied:
                    raise ValueError(f'AdLib note leaves its stream at 0x{addr:04x}')
                add(addr, 'note', f'{_hex_byte(op)}, {_hex_byte(image[addr + 1])}', f'note, image+0x{addr:04x}')
                starts.add(addr)
                covered.update((addr, addr + 1))
                addr += 2
                continue
            if op > 0xc9 or op not in COMMANDS:
                raise ValueError(f'AdLib opcode 0x{op:02x} at 0x{addr:04x} is outside the recovered grammar')
            if addr + 1 in ambient:
                if op not in (0xab, 0x93):
                    raise ValueError(f'AdLib ambient operand is not a level or frequency command at 0x{addr:04x}')
                add(addr, 'byte', _hex_byte(op), 'Ambient stream command opcode')
                add(addr + 1, 'byte', _hex_byte(image[addr + 1]), 'Mutable ambient command operand')
                starts.add(addr)
                covered.update((addr, addr + 1))
                addr += 2
                continue
            if op in BRANCH:
                if addr + 3 > end:
                    raise ValueError(f'AdLib branch leaves the bank at 0x{addr:04x}')
                target = _u16(image, addr + 1)
                symbol = by_opcode[op]
                if not origin <= target < end:
                    add(addr, 'byte', symbol, f'{SHORT[symbol]}, image+0x{addr:04x}')
                    add(addr + 1, 'word', _hex_word(target), 'Unclassified bank data')
                    starts.add(addr)
                    covered.update((addr, addr + 1, addr + 2))
                    break
                add(addr, 'byte', symbol, f'{SHORT[symbol]}, image+0x{addr:04x}')
                add(addr + 1, 'pointer', target_name(target), '')
                starts.add(addr)
                covered.update((addr, addr + 1, addr + 2))
                branches.add(target)
                if target not in headers and target != event_at:
                    pending.append(target)
                if op == 0x81 and addr + 3 < end:
                    branches.add(addr + 3)
                if op in NO_FALL:
                    break
                addr += 3
                continue
            width = COMMANDS[op].width
            if addr + width > end or any((addr + i) in occupied for i in range(width)):
                raise ValueError(f'AdLib command leaves its stream at 0x{addr:04x}')
            symbol = by_opcode.get(op)
            if not symbol or symbol not in SHORT:
                raise ValueError(f'AdLib opcode 0x{op:02x} has no recovered name')
            operands = ', '.join([symbol, *(_hex_byte(image[addr + 1 + i]) for i in range(width - 1))])
            add(addr, 'command', operands, f'{SHORT[symbol]}, image+0x{addr:04x}')
            starts.add(addr)
            covered.update(range(addr, addr + width))
            if op in NO_FALL:
                break
            addr += width
    labels = {
        origin: ('resource_bank', f'image+0x{origin:04x}'),
        instrument_at: ('instrument_pointers', f'image+0x{instrument_at:04x}'),
        event_at: ('event_record_pointers', f'image+0x{event_at:04x}'),
        ambient_pitch: ('ambient_pitch_parameter', 'Mutable ambient command operand'),
        ambient_level: ('ambient_level_parameter', 'Mutable ambient command operand'),
    }
    for start in patch_at:
        labels[start] = (f'opl_patch_{start:04x}', f'image+0x{start:04x}')
    for header in headers:
        labels[header] = (f'event_record_{header:04x}', f'image+0x{header:04x}')
        labels.setdefault(header + 2, (f'stream_{header + 2:04x}', f'image+0x{header + 2:04x}'))
    for target in branches:
        if target in headers or target == event_at or target in ambient:
            continue
        description = 'Ambient stream command opcode' if target + 1 in ambient else f'image+0x{target:04x}'
        labels[target] = (f'stream_{target:04x}', description)
    content = {
        origin: ('pointer', 'event_record_pointers', 'image+1871, sequencer bank address'),
    }
    for index in range(instrument_count):
        at = instrument_at + 2 * index
        value = _u16(image, at)
        if origin <= value < end:
            content[at] = ('pointer', f'opl_patch_{value:04x}', f'instrument {index}')
        else:
            content[at] = ('word', _hex_word(value), f'instrument {index} (unclassified address)')
    for start in patch_at:
        content[start] = ('patch', f'opl_patch_{start:04x}', '')
    for index, value in enumerate(event_words):
        at = event_at + 2 * index
        if origin <= value < end:
            content[at] = ('pointer', target_name(value), f'event {index}')
        else:
            content[at] = ('word', _hex_word(value), f'event {index}')
    for header in headers:
        content[header] = ('event', f'{image[header]}, {image[header + 1] if image[header + 1] < 128 else image[header + 1] - 256}',
                           'channel, signed priority byte')
    for row in records:
        content[int(row['offset'], 16)] = (row['kind'], row['operands'], row['description'])
    claimed = set()
    for at, (kind, _operands, _description) in content.items():
        if kind == 'patch':
            width = 11
        elif kind in ('pointer', 'word', 'event', 'note'):
            width = 2
        elif kind == 'command':
            width = 1 + _operands.count(',')
        elif kind == 'byte':
            width = 1
        else:
            raise ValueError(f'Unsized AdLib record {kind}')
        span = range(at, at + width)
        if any(byte in claimed for byte in span):
            raise ValueError(f'AdLib record at 0x{at:04x} overlaps another record')
        claimed.update(span)
    raw_start = None
    for addr in range(origin, end):
        if addr in claimed:
            raw_start = None
            continue
        if raw_start is None:
            raw_start = addr
        if addr + 1 == end or (addr + 1) in claimed:
            for chunk in range(raw_start, addr + 1, 12):
                stop = min(chunk + 12, addr + 1)
                operands = ', '.join(_hex_byte(image[i]) for i in range(chunk, stop))
                content[chunk] = ('raw', operands, 'Unclassified bank data')
    ordered = []
    for addr in range(origin, end):
        if addr in labels:
            name, description = labels[addr]
            ordered.append({'offset': f'0x{addr:04x}', 'kind': 'label', 'operands': name, 'description': description})
        if addr in content:
            kind, operands, description = content[addr]
            ordered.append({'offset': f'0x{addr:04x}', 'kind': kind, 'operands': operands, 'description': description})
    decoded = {'schema': 'fe2-adlib-bank-v1', 'origin': origin, 'end': end, 'symbols': symbols,
               'patch_fields': list(PATCH_FIELDS), 'patches': {}, 'records': ordered}
    for start in patch_at:
        decoded['patches'][f'opl_patch_{start:04x}'] = list(image[start:start + 11])
    if encode(decoded) != image[origin:end]:
        raise ValueError('Recovered AdLib bank does not match the overlay')
    return decoded


def pitch_rows(image, table, curves, steps):
    """Fourteen bend curves and the pointer table that addresses them."""
    image = bytes(image)
    rows_out = []
    for index in range(curves):
        pointer = _u16(image, table + 2 * index)
        expected = table + curves * 2 + index * steps
        if pointer != expected or expected + steps > len(image):
            raise ValueError('AdLib pitch curve pointer does not match its curve')
        rows_out.append([index, *image[expected:expected + steps]])
    return rows_out


def write_bank(image, directory):
    """Write patches.csv, records.tsv, and pitch_curves.csv from an overlay image."""
    directory = Path(directory)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(directory / 'bank.ini', encoding='utf-8')
    bank = config['bank']
    origin, end = int(bank['origin'], 0), int(bank['end'], 0)
    decoded = decode_bank(image, origin, end, dict(config['symbols']),
                          int(config['ambient']['pitch'], 0), int(config['ambient']['level'], 0))
    pitch = config['pitch']
    curves = pitch_rows(image, int(pitch['table'], 0), int(pitch['curves']), int(pitch['steps']))
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / 'patches.csv').open('w', encoding='utf-8', newline='\n') as stream:
        writer = csv.writer(stream, lineterminator='\n')
        writer.writerow(['name', *PATCH_FIELDS])
        for name in sorted(decoded['patches'], key=lambda item: int(item.rsplit('_', 1)[-1], 16)):
            writer.writerow([name, *(_hex_byte(value) for value in decoded['patches'][name])])
    with (directory / 'records.tsv').open('w', encoding='utf-8', newline='\n') as stream:
        writer = csv.DictWriter(stream, ['offset', 'kind', 'operands', 'description'], delimiter='\t', lineterminator='\n')
        writer.writeheader()
        writer.writerows(decoded['records'])
    with (directory / 'pitch_curves.csv').open('w', encoding='utf-8', newline='\n') as stream:
        writer = csv.writer(stream, lineterminator='\n')
        writer.writerow(['curve', *(f'bend_{i:02d}' for i in range(int(pitch['steps'])))])
        writer.writerows(curves)
    return decoded


def compile_bank(source):
    result = document(source)
    output = ROOT / 'build/resources/adlib/bank.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return output


def extract(assembly, destination):
    """One-time extraction of already classified NASM resource records."""
    text = Path(assembly).read_text(encoding='utf-8')
    start = text.index('resource_bank:')
    end = text.index('%unmacro adlib_patch', start)
    symbols, records, patches = {}, [], []
    position = 0x1871
    macro = False
    label = None
    for line in text[start:end].splitlines():
        code, _, comment = line.partition(';')
        code = code.strip()
        if code.startswith('%macro'):
            macro = True
        if macro:
            if code.startswith('%endmacro'):
                macro = False
            continue
        match = re.fullmatch(r'%define (\w+) (\S+)', code)
        if match:
            symbols[match[1]] = match[2]
            continue
        if not code:
            continue
        match = re.fullmatch(r'(\w+):', code)
        if match:
            label = match[1]
            annotated = re.search(r'image\+0x([0-9a-f]+)', comment)
            if annotated and int(annotated[1], 16) != position:
                raise ValueError(f'Label extent mismatch {label}')
            records.append({'offset': f'0x{position:04x}', 'kind': 'label', 'operands': label, 'description': comment.strip()})
            continue
        match = re.fullmatch(r'(db|dw|adlib_patch|adlib_event_header|adlib_command|adlib_note)\s+(.+)', code)
        if not match:
            raise ValueError(f'Unparsed bank source {code}')
        op, operands = match.groups()
        tokens = [token.strip() for token in operands.split(',')]
        kind = {'db': 'raw', 'dw': 'word', 'adlib_patch': 'patch', 'adlib_event_header': 'event', 'adlib_command': 'command', 'adlib_note': 'note'}[op]
        if op == 'dw' and all(re.fullmatch(r'[A-Za-z_]\w*', token) for token in tokens):
            kind = 'pointer'
        if op == 'db' and any(token.startswith('ADLIB_') for token in tokens):
            kind = 'byte'
        if kind == 'patch':
            patches.append(dict(name=label, **dict(zip(PATCH_FIELDS, tokens))))
            operands = label
        records.append({'offset': f'0x{position:04x}', 'kind': kind, 'operands': operands, 'description': comment.strip()})
        position += len(tokens) * (2 if op == 'dw' else 1)
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for name, values, fields, delimiter in [('patches.csv', patches, ['name', *PATCH_FIELDS], ','),
            ('records.tsv', records, ['offset', 'kind', 'operands', 'description'], '\t')]:
        with (destination / name).open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fields, delimiter=delimiter)
            writer.writeheader()
            writer.writerows(values)
    lines = ['[bank]', 'origin = 0x1871', f'end = 0x{position:04x}', 'patches = patches.csv', 'records = records.tsv', '', '[symbols]']
    lines.extend(f'{key} = {value}' for key, value in symbols.items())
    (destination / 'bank.ini').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return document(destination / 'bank.ini')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', metavar='ASSEMBLY',
                        help='One-time import of a legacy module with the inline typed bank')
    args = parser.parse_args()
    source = ROOT / 'data/adlib/bank.ini'
    if args.extract:
        extract(Path(args.extract), source.parent)
    compile_bank(source)

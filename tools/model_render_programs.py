"""Lossless source codec for recovered model renderer operations.

Dispatch uses word & 31 (B207h). Unsupported/dynamic grammars terminate decoding;
their remaining bytes are explicit raw data, never guessed instruction boundaries.
"""
import struct
from model_point_parameters import decode_size_operand, describe_material, resolved_size_mode

# Additional ES:SI words consumed unconditionally by these handlers.
OPERATIONS = {
    0: ('return_stream', 0),
    1: ('inline_curve', 2),
    2: ('line_segment', 1),
    3: ('bitmap_surface', 2),
    4: ('textured_quad', 3),
    5: ('polygon_value_sequence', None),
    7: ('adjusted_bitmap_surface', 2),
    8: ('adjusted_textured_quad', 3),
    9: ('vertex_pair_surface', 2),
    10: ('model_instance', 3),
    11: ('scaled_threshold', 1),
    12: ('inverse_scaled_threshold', 1),
    13: ('binary_expression', 1),
    14: ('nested_model_context', None),
    15: ('position_duration_sound', 0),
    16: ('geometry_four', 3),
    17: ('geometry_six', 5),
    18: ('projected_vertex_text', 1),
    19: ('context_mask', 1),
    20: ('excluded_context_mask', 1),
    21: ('curve_batch', 0),
    22: ('four_vertex_lines', 3),
    23: ('vertex_distance_filter', 2),
    24: ('point_list', None),
    25: ('update_basis_axis', 0),
    26: ('update_parameter', None),
    27: ('conditional_nested_model', None),
    28: ('load_basis_columns', 0),
    29: ('binary_expression_alternate', 1),
    30: ('configure_context', 0),
    31: ('geometry_pipeline', None),
}
BY_NAME = {name: slot for slot, (name, _) in OPERATIONS.items()}

# Slot 5 is followed by a compact complex-polygon substream. Preserve the
# encoded words in source while naming the command selected by bits 9..11.
POLYGON_COMMANDS = {
    'end': (0, 1),
    'bezier': (1, 3),
    'line': (2, 2),
    'line_cont': (3, 1),
    'bezier_cont': (4, 2),
    'line_join': (5, 1),
    'circle': (6, 2),
    'adjacent_operator': (7, 1),
}
POLYGON_COMMAND_NAMES = {slot: name for name, (slot, _) in POLYGON_COMMANDS.items()}
POLYGON_COMMAND_ALIASES = {'join': 'line_join'}


def _format_point_pairs(words):
    pairs = []
    for index, word in enumerate(words):
        values = []
        for position, value in enumerate((word >> 8, word & 255)):
            is_marker = value == 0x7f and (position == 1 or index > 0)
            values.append('end' if is_marker else str(value if value < 128 else value - 256))
        pairs.append(','.join(values))
    return '|'.join(pairs)


def _parse_point_pairs(source):
    words = []
    for index, pair in enumerate(source.split('|')):
        fields = pair.split(',')
        if len(fields) != 2:
            raise ValueError('Point pair requires high-byte and low-byte vertices')
        values = []
        for position, field in enumerate(fields):
            if field == 'end':
                if index == 0 and position == 0:
                    raise ValueError('First high-byte vertex cannot be a terminator')
                value = 127
            else:
                value = int(field, 0)
                if not -128 <= value <= 127:
                    raise ValueError('Point vertex exceeds signed byte range')
                if value == 127 and (position == 1 or index > 0):
                    raise ValueError('Point sentinel must be written as end')
            values.append(value & 255)
        words.append((values[0] << 8) | values[1])
    return words


def _format_point_size(word):
    operand = decode_size_operand(word)
    kind, value = (resolved_size_mode(operand.value) if operand.kind == 'literal'
                   else (operand.kind, operand.value))
    return f'{kind}:{value}@0x{word:04x}'


def _parse_point_size(source):
    semantic, separator, encoded = source.partition('@')
    if not separator:
        raise ValueError('Point size must retain its encoded word after @')
    word = int(encoded, 0)
    operand = decode_size_operand(word)
    kind, value = (resolved_size_mode(operand.value) if operand.kind == 'literal'
                   else (operand.kind, operand.value))
    supplied_kind, colon, supplied_value = semantic.partition(':')
    if not colon or supplied_kind != kind or int(supplied_value, 0) != value:
        raise ValueError('Point size fields disagree with its encoded operand')
    return word


def _format_point_material(token):
    material = describe_material(token)
    value = material['rgb12'] if material['kind'] == 'rgb12' else material['table_byte_offset']
    return f"{material['kind']}:0x{value:03x}", str(material['inherit']).lower()


def _polygon_command_values(slot, encoded):
    if slot == 0 or slot == 5:
        return []
    if slot == 1:
        return [value for word in encoded[1:] for value in (word >> 8, word & 255)]
    if slot == 2:
        return [encoded[0] & 255, encoded[1] & 255]
    if slot == 3:
        return [encoded[0] & 255]
    if slot == 4:
        return [encoded[0] & 255, encoded[1] >> 8, encoded[1] & 255]
    if slot == 6:
        return [encoded[0] & 255, encoded[1] >> 8, encoded[1] & 127]
    return None


def _parse_polygon_commands(source):
    words = []
    commands = source.split('|')
    if not commands or not commands[-1]:
        raise ValueError('Polygon command list is empty or has a trailing separator')
    for index, command in enumerate(commands):
        name, separator, fields = command.partition(':')
        semantic, at, encoded = fields.partition('@')
        name = POLYGON_COMMAND_ALIASES.get(name, name)
        if not separator or name not in POLYGON_COMMANDS:
            raise ValueError('Unknown polygon subcommand')
        if not at:
            # Read the previous lossless form during migration; the decoder
            # immediately rewrites it with explicit semantic fields.
            encoded, semantic = semantic, None
        slot, width = POLYGON_COMMANDS[name]
        values = [int(value, 0) for value in encoded.split(',')]
        if len(values) != width or any(not 0 <= value <= 65535 for value in values):
            raise ValueError('Polygon subcommand has an invalid encoded word span')
        if (values[0] >> 9) & 7 != slot:
            raise ValueError('Polygon subcommand name does not match encoded dispatch')
        decoded = (None if semantic is None else
                   ([] if not semantic else [int(value, 0) for value in semantic.split(',')]))
        expected = _polygon_command_values(slot, values)
        if decoded is not None and expected is not None and decoded != expected:
            raise ValueError('Polygon subcommand fields disagree with encoded words')
        if decoded and expected is None:
            raise ValueError('Adjacent operator alias has no established fields')
        if slot == 0 and index != len(commands) - 1:
            raise ValueError('Polygon end command must terminate its substream')
        words.extend(values)
    if (words[-1] >> 9) & 7 != 0:
        raise ValueError('Polygon subcommand list has no end command')
    return words


def _format_polygon_commands(words):
    commands, offset = [], 0
    while offset < len(words):
        first = words[offset]
        slot = (first >> 9) & 7
        name = POLYGON_COMMAND_NAMES[slot]
        width = POLYGON_COMMANDS[name][1]
        if offset + width > len(words):
            return None
        encoded = words[offset:offset + width]
        values = _polygon_command_values(slot, encoded)
        semantic = '' if values is None else ','.join(str(value) for value in values)
        commands.append(name + ':' + semantic + '@' + ','.join(
            f'0x{word:04x}' for word in encoded))
        offset += width
        if slot == 0:
            return '|'.join(commands) if offset == len(words) else None
    return None


def operand_count(token, words):
    slot = token & 31
    if slot == 6:
        parameter = token >> 5
        if parameter in (0, 0x7fe, 0x7ff):
            return 0
        # Stored parameter AH bit0 gates all extra-word paths (4484/45D5/4753).
        # The 7FDh sentinel instead directly consumes a depth word at4691h.
        return 1 if parameter == 0x7fd or parameter & 0x100 else 0
    if slot not in OPERATIONS:
        return None
    if slot in (14, 27):
        if not words:
            return None
        return (1 if slot == 14 else 2) + (2 if words[0] & 0x8000 else 0)
    if slot == 28:
        # Both entries consume a phase word. Negative tokens enter 7778h
        # directly; nonnegative tokens load columns and then ES:LODSW at 7771h.
        return 1
    if slot == 31:
        # The declared extent is applied by the early-release path; normal
        # completion retains its parser cursor. Native body length follows the
        # declared early-path span in recovered complete bodies.
        return 2 + (((token >> 4) & 0xfffe) // 2)
    if slot == 5:
        if not words:
            return None
        index = 1
        while index < len(words):
            value_slot = (words[index] >> 9) & 7
            # Slot7 deliberately falls through the adjacent value-token table
            # into operator-dispatch slot0 (render_operator_add). It consumes
            # no inline stream words. The two dispatch tables share a boundary.
            index += 1 + (0, 2, 1, 0, 1, 0, 1, 0)[value_slot]
            if index > len(words):
                return None
            if value_slot == 0:
                # Visible-normal parsing ends at slot zero. The rejected-normal
                # path independently skips unsigned DH bytes from after the
                # header; it need not rejoin this contour terminator.
                return index
        return None
    if slot == 24:
        for index, word in enumerate(words[1:], 1):
            # The discard loop tests AL after every LODSW. The render loop
            # tests AL for each point word too, but can terminate earlier on
            # AH==7Fh between paired points. A low-byte marker is therefore
            # the shared boundary. The first word enters projection directly,
            # so its high byte is a vertex even when it is 7Fh; only later
            # words pass through the AH sentinel check.
            if word & 255 == 0x7f:
                return index + 1
            if index > 1 and word >> 8 == 0x7f:
                return None
        return None
    if slot == 26:
        if not words:
            return None
        # The handler consumes the mode word, then skips seven table words when AH != 0.
        return 8 if words[0] & 0xff00 else 1
    return OPERATIONS[slot][1]


def compile_source(source, read_geometry=None, names_path=None):
    output = bytearray()
    for number, line in enumerate(source.splitlines(), 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        parts = line.replace(',', ' ').split()
        name = parts.pop(0)
        if name == 'ship_parameters':
            from ship_parameters import records
            fields = dict(part.split('=', 1) for part in parts)
            if len(parts) != 2 or set(fields) != {'file', 'model'} or read_geometry is None:
                raise ValueError('Ship parameters require file, model and an asset reader')
            rows = records(read_geometry(fields['file']), names_path)
            if fields['model'] not in rows:
                raise ValueError('Unknown ship parameter model')
            output.extend(rows[fields['model']])
            continue
        if name == 'raw':
            values = [int(value, 0) for value in parts]
            if not values or any(not 0 <= value <= 255 for value in values):
                raise ValueError(f'Invalid raw bytes on line {number}')
            output.extend(values)
            continue
        geometry_pipeline = name == 'geometry_pipeline'
        expected = ({'token', 'words', 'payload', 'file', 'truncated'} if geometry_pipeline
                    else {'token', 'words', 'truncated', 'commands', 'normal_index',
                          'skip_bytes', 'skip_if_z_clipped', 'header', 'parameter', 'points',
                          'size', 'material', 'inherit_material'})
        fields = {}
        # Words are separated by commas in the source, parse before normalizing.
        for field in line.split()[1:]:
            key, value = field.split('=', 1)
            if key not in expected or key in fields:
                raise ValueError(f'Invalid renderer fields on line {number}')
            fields[key] = value
        truncated = fields.get('truncated', 'false')
        if truncated not in ('true', 'false'):
            raise ValueError('Renderer truncated marker must be true or false')
        fields.pop('truncated', None)
        slot = 6 if name == 'parameter_control' else BY_NAME.get(name)
        allowed = ({'token', 'words', 'payload'}, {'token', 'words', 'file'}) if geometry_pipeline else (
            ({'token', 'words'}, {'token', 'words', 'commands'},
             {'token', 'normal_index', 'skip_bytes', 'header', 'commands'},
             {'token', 'normal_index', 'skip_bytes', 'skip_if_z_clipped',
              'header', 'commands'}) if slot == 5
            else (({'token', 'words'}, {'token', 'parameter', 'points'},
                   {'token', 'size', 'material', 'inherit_material', 'points'}) if slot == 24
                  else ({'token', 'words'},)))
        if set(fields) not in allowed:
            raise ValueError(f'Missing renderer fields on line {number}')
        token = int(fields['token'], 0)
        if 'points' in fields:
            parameter = (_parse_point_size(fields['size']) if 'size' in fields
                         else int(fields['parameter'], 0))
            words = [parameter] + _parse_point_pairs(fields['points'])
            if 'material' in fields:
                material, inherit = _format_point_material(token)
                kind, colon, value = fields['material'].partition(':')
                expected_kind, _, expected_value = material.partition(':')
                if (not colon or kind != expected_kind or
                        int(value, 0) != int(expected_value, 0) or
                        fields['inherit_material'] != inherit):
                    raise ValueError('Point material fields disagree with its dispatch token')
        elif 'header' in fields:
            header = int(fields['header'], 0)
            normal_index = int(fields['normal_index'], 0)
            skip_bytes = int(fields['skip_bytes'], 0)
            skip_if_z_clipped = fields.get('skip_if_z_clipped')
            if skip_if_z_clipped is not None and skip_if_z_clipped not in ('true', 'false'):
                raise ValueError('Polygon clipping flag must be true or false')
            if (not 0 <= header <= 65535 or normal_index != (header & 0x7f) or
                    skip_bytes != (header >> 8) or
                    (skip_if_z_clipped is not None and
                     (skip_if_z_clipped == 'true') != bool(header & 0x80))):
                raise ValueError('Polygon header fields disagree with its encoded word')
            words = [header]
        else:
            words = [] if fields['words'] == '-' else [int(v, 0) for v in fields['words'].split(',')]
        if any(not 0 <= value <= 65535 for value in [token] + words):
            raise ValueError('Renderer word exceeds 16 bits')
        if slot is None or token & 31 != slot:
            raise ValueError('Operation does not match dispatch token')
        if 'commands' in fields:
            if slot != 5 or len(words) != 1:
                raise ValueError('Named polygon commands require one initial parameter word')
            words.extend(_parse_polygon_commands(fields['commands']))
        if geometry_pipeline:
            if 'file' in fields:
                if read_geometry is None:
                    raise ValueError('Geometry source resolver required')
                from model_geometry_resources import compile_source as compile_geometry
                payload = compile_geometry(read_geometry(fields['file']))
            else:
                payload = bytes.fromhex(fields['payload']) if fields['payload'] != '-' else b''
            declared = (token >> 4) & 0xfffe
            if (len(words) != 2 or
                    (len(payload) != declared if truncated == 'false'
                     else not 0 <= len(payload) < declared)):
                raise ValueError('Geometry pipeline header or declared payload length changed')
            output.extend(struct.pack('<HHH', token, *words))
            output.extend(payload)
            continue
        count = operand_count(token, words)
        if count is None or (len(words) != count and
                             not (truncated == 'true' and len(words) < count)):
            raise ValueError('Unsupported token or incorrect renderer operand count')
        output.extend(struct.pack('<' + 'H' * (1 + len(words)), token, *words))
    return bytes(output)


def _format_operation(token, words, truncated=False):
    slot = token & 31
    name = 'parameter_control' if slot == 6 else OPERATIONS[slot][0]
    if slot == 24 and not truncated:
        material, inherit = _format_point_material(token)
        return (f'point_list token=0x{token:04x} size={_format_point_size(words[0])} '
                f'material={material} inherit_material={inherit} '
                f'points={_format_point_pairs(words[1:])}')
    if slot == 31:
        payload = b''.join(struct.pack('<H', word) for word in words[2:])
        return (f'geometry_pipeline token=0x{token:04x} '
                f'words=0x{words[0]:04x},0x{words[1]:04x} payload={payload.hex() or "-"}'
                + (' truncated=true' if truncated else ''))
    if slot == 5 and not truncated and len(words) > 1:
        commands = _format_polygon_commands(words[1:])
        if commands is not None:
            header = words[0]
            return (f'polygon_value_sequence token=0x{token:04x} '
                    f'normal_index=0x{header & 0x7f:02x} '
                    f'skip_bytes=0x{header >> 8:02x} '
                    f'skip_if_z_clipped={str(bool(header & 0x80)).lower()} '
                    f'header=0x{header:04x} '
                    f'commands={commands}')
    operands = ','.join(f'0x{word:04x}' for word in words) or '-'
    return (f'{name} token=0x{token:04x} words={operands}'
            + (' truncated=true' if truncated else ''))


def decode_source(data, follow_branches=False, entry_offsets=()):
    data = bytes(data)
    if follow_branches:
        return _decode_control_flow(data, entry_offsets)
    if entry_offsets:
        raise ValueError('Additional entries require control-flow decoding')
    lines, offset, decoded = [], 0, 0
    while offset + 2 <= len(data):
        token = int.from_bytes(data[offset:offset + 2], 'little')
        words = [int.from_bytes(data[i:i + 2], 'little')
                 for i in range(offset + 2, len(data) - 1, 2)]
        count = operand_count(token, words)
        if count is None:
            break
        if offset + 2 + count * 2 > len(data):
            available_words = (len(data) - offset - 2) // 2
            slot = token & 31
            if slot == 31 and available_words >= 2:
                lines.append(_format_operation(token, words[:available_words], truncated=True))
                offset += 2 + available_words * 2
            elif slot != 31 and available_words >= 1:
                lines.append(_format_operation(token, words[:available_words], truncated=True))
                offset += 2 + available_words * 2
            break
        slot = token & 31
        lines.append(_format_operation(token, words[:count]))
        offset += 2 + count * 2
        decoded += 1
        # The return handler discards the dispatcher's saved return address.
        if slot == 0:
            break
    for start in range(offset, len(data), 16):
        lines.append('raw ' + ' '.join(f'0x{v:02x}' for v in data[start:start + 16]))
    result = '\n'.join(lines) + ('\n' if lines else '')
    if compile_source(result) != data:
        raise AssertionError('Renderer source roundtrip failed')
    return result, decoded, offset


def _decode_control_flow(data, entry_offsets=()):
    """Decode only offsets reachable through proven dispatcher/skip transitions.

    An overlap means incompatible interpretations. Fall back to the conservative
    entry prefix rather than select one interpretation or invent an alias.
    """
    if any(not isinstance(offset, int) or not 0 <= offset < len(data)
           for offset in entry_offsets):
        raise ValueError('Renderer entry offset leaves program')
    pending, records, partials = [0, *entry_offsets], {}, {}
    while pending:
        offset = pending.pop()
        while offset + 2 <= len(data):
            if offset in records:
                break
            token = int.from_bytes(data[offset:offset + 2], 'little')
            words = [int.from_bytes(data[i:i + 2], 'little')
                     for i in range(offset + 2, len(data) - 1, 2)]
            count = operand_count(token, words)
            if count is None:
                break
            if offset + 2 + 2 * count > len(data):
                available_words = (len(data) - offset - 2) // 2
                slot = token & 31
                if (slot == 31 and available_words >= 2) or (slot != 31 and available_words >= 1):
                    raw_tail = len(data) - (offset + 2 + available_words * 2)
                    if raw_tail:
                        break
                    partials[offset] = _format_operation(
                        token, words[:available_words], truncated=True)
                break
            end = offset + 2 + 2 * count
            if any(offset < other_end and other_start < end
                   for other_start, (other_end, _, _) in records.items()):
                return decode_source(data)
            slot = token & 31
            records[offset] = (end, _format_operation(token, words[:count]), True)
            if slot in (11, 12, 19, 20, 23):
                skip = (token >> 4) & 0xfffe
                # Zero skip discards a dispatcher return address, not a jump.
                if skip and end + skip < len(data):
                    pending.append(end + skip)
            elif slot == 5 and words and words[0] >> 8:
                # A rejected polygon normal skips unsigned DH bytes from the
                # cursor immediately after the initial header word. Its
                # visible path independently ends at the nested slot-zero
                # command, so this target can be a separate stream entry.
                skip = words[0] >> 8
                target = offset + 4 + skip
                if skip and target < len(data):
                    pending.append(target)
            offset = end
            if slot == 0:
                break
    entries = dict(records)
    for start, operation in sorted(partials.items()):
        if any(start < end and other_start < len(data)
               for other_start, (end, _, _) in records.items()):
            continue
        if any(other_start != start for other_start in partials):
            continue
        entries[start] = (len(data), operation, False)
    lines, cursor, count, covered = [], 0, 0, 0
    for start, (end, operation, complete) in sorted(entries.items()):
        for raw_start in range(cursor, start, 16):
            lines.append('raw ' + ' '.join(f'0x{v:02x}' for v in data[raw_start:min(raw_start + 16, start)]))
        lines.append(operation + f' # program +0x{start:04x}')
        cursor = end
        if complete:
            count += 1
            covered += end - start
    for raw_start in range(cursor, len(data), 16):
        lines.append('raw ' + ' '.join(f'0x{v:02x}' for v in data[raw_start:raw_start + 16]))
    result = '\n'.join(lines) + ('\n' if lines else '')
    if compile_source(result) != data:
        raise AssertionError('Renderer control-flow source roundtrip failed')
    return result, count, covered

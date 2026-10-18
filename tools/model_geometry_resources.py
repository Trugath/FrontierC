"""Native sources for declared renderer geometry-pipeline bodies.

The word/layer blocks are established by988Fh..996Eh. Signed-tag vector, curve
and terminator records are compiled from native fields; unsupported fragments
remain explicit raw bytes.
"""
import struct


def compile_source(source):
    output = bytearray()
    curve = None
    previous = None
    for line in source.splitlines():
        parts = line.split('#', 1)[0].split()
        if not parts:
            continue
        operation, *tokens = parts
        if operation in ('vector_phase', 'begin_curve', 'isolated_point', 'curve_start', 'curve_point'):
            fields = {}
            for token in tokens:
                key, value = token.split('=', 1)
                if key in fields:
                    raise ValueError('Duplicate geometry field')
                fields[key] = value
            expected = {'tag', 'xyz', 'phase'} if operation == 'vector_phase' else (
                {'tag', 'selector'} if operation == 'begin_curve' else (
                    {'xyz', 'parameter'} if operation == 'isolated_point' else {'xyz'}))
            if set(fields) != expected:
                raise ValueError('Invalid geometry byte-record fields')
            xyz = [int(v, 0) for v in fields.get('xyz', '').split(',')] if 'xyz' in fields else []
            if xyz and (len(xyz) != 3 or any(not -128 <= v <= 127 for v in xyz)):
                raise ValueError('Geometry vector needs three signed8-bit components')
            if operation == 'vector_phase':
                tag, phase = int(fields['tag'], 0), int(fields['phase'], 0)
                if curve is not None or not 128 <= tag <= 255 or not 0 <= phase <= 255:
                    raise ValueError('Invalid negative-tag phase record')
                output.extend(bytes([tag] + [v & 255 for v in xyz] + [phase]))
            elif operation == 'begin_curve':
                tag, selector = int(fields['tag'], 0), int(fields['selector'], 0)
                if curve is not None or not 1 <= tag <= 127 or not -128 <= selector <= 127:
                    raise ValueError('Invalid positive-tag curve header')
                output.extend(bytes([tag, selector & 255]))
                curve = 'points'
            elif operation == 'isolated_point':
                parameter = int(fields['parameter'], 0)
                if curve != 'points' or not 0 <= parameter <= 255:
                    raise ValueError('Invalid isolated geometry point')
                output.extend(bytes([v & 255 for v in xyz] + [0, parameter]))
            elif operation == 'curve_start':
                if curve != 'points':
                    raise ValueError('Curve start must follow its header/isolated points')
                output.extend(bytes(v & 255 for v in xyz))
                curve = 'needs_point'
            elif operation == 'curve_point':
                if curve not in ('needs_point', 'curve') or xyz[0] == 0:
                    raise ValueError('Additional curve point needs nonzero X')
                output.extend(bytes(v & 255 for v in xyz))
                curve = 'curve'
        elif operation == 'end_curve':
            if curve != 'curve' or tokens:
                raise ValueError('Curve needs an additional point before its terminator')
            output.append(0)
            curve = None
        elif operation == 'end_records':
            if curve is not None or tokens:
                raise ValueError('Cannot terminate incomplete geometry curve')
            output.append(0)
        elif operation == 'padding':
            values = [int(token, 0) for token in tokens]
            if previous != 'end_records' or len(output) % 2 != 1 or len(values) != 1 or not 0 <= values[0] <= 255:
                raise ValueError('Geometry padding must align an odd records-end cursor')
            output.append(values[0])
        elif operation == 'raw':
            if curve is not None:
                raise ValueError('Raw fragment cannot split a named curve')
            values = [int(token, 0) for token in tokens]
            if not values or any(not 0 <= value <= 255 for value in values):
                raise ValueError('Geometry raw byte outside8-bit range')
            output.extend(values)
        elif operation in ('primary_words', 'selection_words', 'secondary_words'):
            values = [int(token, 0) for token in tokens]
            expected = 29 if operation == 'selection_words' else 4
            if len(values) != expected or any(not 0 <= value <= 65535 for value in values):
                raise ValueError('Geometry word block count or width changed')
            output.extend(struct.pack('<' + 'H' * expected, *values))
        elif operation == 'layer':
            values = [int(token, 0) for token in tokens]
            if len(values) != 2 or not 1 <= values[0] <= 65535 or not 0 <= values[1] <= 65535:
                raise ValueError('Geometry layer needs nonzero scale and palette words')
            output.extend(struct.pack('<HH', *values))
        elif operation == 'layer_base':
            values = [int(token, 0) for token in tokens]
            if len(values) != 1 or not 0 <= values[0] <= 65535:
                raise ValueError('Geometry layer base needs one word')
            output.extend(struct.pack('<H', values[0]))
        elif operation == 'end_layers':
            if tokens:
                raise ValueError('Layer terminator has no operands')
            output.extend(b'\0\0')
        else:
            raise ValueError('Unknown geometry source record')
        previous = operation
    if curve is not None:
        raise ValueError('Unterminated geometry curve')
    return bytes(output)


def _vector(data):
    return ','.join(str(value if value < 128 else value - 256) for value in data)


def _decode_records(data, start):
    lines, offset = [], start
    while offset < len(data):
        tag, begin = data[offset], offset
        offset += 1
        if tag == 0:
            lines.append('end_records')
            return lines, offset
        if tag >= 128:
            if offset + 4 > len(data):
                return lines, begin
            lines.append(f'vector_phase tag=0x{tag:02x} xyz={_vector(data[offset:offset + 3])} phase={data[offset + 3]}')
            offset += 4
            continue
        if offset >= len(data):
            return lines, begin
        selector = data[offset] if data[offset] < 128 else data[offset] - 256
        offset += 1
        group = [f'begin_curve tag={tag} selector={selector}']
        while True:
            if offset + 4 > len(data):
                return lines, begin
            xyz = _vector(data[offset:offset + 3])
            offset += 3
            if data[offset] != 0:
                group.append('curve_start xyz=' + xyz)
                break
            if offset + 2 > len(data):
                return lines, begin
            group.append(f'isolated_point xyz={xyz} parameter={data[offset + 1]}')
            offset += 2
        while offset < len(data) and data[offset] != 0:
            if offset + 3 > len(data):
                return lines, begin
            group.append('curve_point xyz=' + _vector(data[offset:offset + 3]))
            offset += 3
        if offset >= len(data):
            return lines, begin
        offset += 1
        group.append('end_curve')
        lines.extend(group)
    return lines, offset


def decode_source(data):
    data = bytes(data)
    lines, offset = [], 0
    if len(data) >= 8:
        flags = int.from_bytes(data[:2], 'little')
        extra = 58 if flags & 0x1000 else (0 if flags & 0x4000 else 8)
        if len(data) >= 8 + extra:
            def words(name, start, count):
                values = struct.unpack('<' + 'H' * count, data[start:start + 2 * count])
                return name + ' ' + ' '.join(f'0x{value:04x}' for value in values)
            lines.append(words('primary_words', 0, 4))
            if flags & 0x1000:
                lines.append(words('selection_words', 8, 29))
            elif not flags & 0x4000:
                lines.append(words('secondary_words', 8, 4))
            offset = 8 + extra
            layer_offset, layer_lines = offset, []
            base = int.from_bytes(data[offset:offset + 2], 'little') if offset + 2 <= len(data) else None
            if base is not None:
                layer_lines.append(f'layer_base 0x{base:04x}')
                layer_offset += 2
            if base == 0:
                lines.extend(layer_lines)
                records, offset = _decode_records(data, layer_offset)
                lines.extend(records)
            while base and layer_offset + 2 <= len(data):
                scale = int.from_bytes(data[layer_offset:layer_offset + 2], 'little')
                layer_offset += 2
                if scale == 0:
                    layer_lines.append('end_layers')
                    lines.extend(layer_lines)
                    offset = layer_offset
                    records, offset = _decode_records(data, offset)
                    lines.extend(records)
                    break
                if layer_offset + 2 > len(data):
                    break
                palette = int.from_bytes(data[layer_offset:layer_offset + 2], 'little')
                layer_offset += 2
                layer_lines.append(f'layer 0x{scale:04x} 0x{palette:04x}')
    if lines and lines[-1] == 'end_records' and offset % 2 and offset < len(data):
        lines.append(f'padding 0x{data[offset]:02x}')
        offset += 1
    for start in range(offset, len(data), 16):
        lines.append('raw ' + ' '.join(f'0x{value:02x}' for value in data[start:start + 16]))
    result = '\n'.join(lines) + ('\n' if lines else '')
    if compile_source(result) != data:
        raise AssertionError('Geometry native source roundtrip failed')
    return result

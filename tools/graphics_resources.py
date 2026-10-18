#!/usr/bin/env python3
"""EL2G.GRF compiler, recovered from main CS:E349/E378 and MCGA consumers.

Pixel rows contain palette indices, not RGB. Compression plans preserve original
token choices; incompatible pixel edits trigger deterministic LZ recompression.
"""
from pathlib import Path
import argparse
import hashlib
import json


def _expand(plan, source=None):
    out = bytearray()
    encoded = bytearray()
    for token in plan:
        if set(token) == {'literal'}:
            count = token['literal']
            if not isinstance(count, int) or not 0 <= count <= 127:
                raise ValueError('Literal count must be 0..127')
            if source is None:
                raise ValueError('Literal tokens require source pixels')
            chunk = source[len(out):len(out)+count]
            if len(chunk) != count:
                raise ValueError('Literal exceeds decoded length')
            encoded.append(count)
            encoded.extend(chunk)
            out.extend(chunk)
        elif set(token) == {'copy', 'distance'}:
            count, distance = token['copy'], token['distance']
            if not isinstance(count, int) or not 1 <= count <= 16:
                raise ValueError('Copy count must be 1..16')
            if not isinstance(distance, int) or not 1 <= distance <= min(2047, len(out)):
                raise ValueError('Copy distance exceeds decoded history')
            encoded.extend((((-count << 3) & 255) | (distance >> 8), distance & 255))
            for _ in range(count):
                out.append(out[-distance])
        else:
            raise ValueError('Unknown compression token')
    return bytes(out), bytes(encoded)


def compression_plan(data, start=0):
    """Stable greedy plan for edited pixels; original compressor is unknown."""
    plan, literals = ([{'literal': start}] if start else []), 0
    position = start
    while position < len(data):
        best_count, best_distance = 0, 0
        for distance in range(1, min(position, 2047) + 1):
            count = 0
            while count < min(16, len(data) - position) and data[position + count] == data[position + count - distance]:
                count += 1
            if count > best_count:
                best_count, best_distance = count, distance
                if count == 16:
                    break
        if best_count >= 3:
            if literals:
                plan.append({'literal': literals})
                literals = 0
            plan.append({'copy': best_count, 'distance': best_distance})
            position += best_count
        else:
            literals += 1
            position += 1
            if literals == 127:
                plan.append({'literal': literals})
                literals = 0
    if literals:
        plan.append({'literal': literals})
    return plan


def compress(data, start=0):
    """Stable greedy encoder for edited pixels; original compressor is unknown."""
    plan = compression_plan(data, start)
    encoded = _expand(plan, data)[1]
    return encoded[start + 1:] if start else encoded


def _decode_tokens(take, count, history=b''):
    prefix = len(history)
    decoded, plan = bytearray(history), []
    while len(decoded)-prefix < count:
        command = take(1)[0]
        if command < 128:
            plan.append({'literal': command})
            decoded.extend(take(command))
        else:
            copies = -((command-256) >> 3)
            distance = ((command & 7) << 8) | take(1)[0]
            if not 1 <= distance <= len(decoded):
                raise ValueError('Back-reference exceeds decoded history')
            plan.append({'copy': copies, 'distance': distance})
            for _ in range(copies):
                decoded.append(decoded[-distance])
        if len(decoded)-prefix > count:
            raise ValueError('Command exceeds decoded record size')
    return bytes(decoded[prefix:]), plan


def decode(blob):
    position, records = 0, []
    def take(count):
        nonlocal position
        if position + count > len(blob):
            raise ValueError('Truncated GRF stream')
        value = blob[position:position+count]
        position += count
        return value
    while True:
        start = position
        size = int.from_bytes(take(2), 'little')
        if size == 0:
            if position != len(blob):
                raise ValueError('Unexpected bytes after terminator')
            break
        flags = take(1)[0]
        if flags == 255:
            raise ValueError('Opaque FF record: pixel semantics not established')
        header = bytes([flags]) + take(7)
        height, width = header[1], int.from_bytes(header[2:4], 'little')
        if size < 8:
            raise ValueError('Unsupported record dimensions/layout')
        if flags & 2:
            packed = take(size-8)
            inner_position = 0
            def inner_take(count):
                nonlocal inner_position
                if inner_position+count > len(packed):
                    raise ValueError('Truncated inner sprite stream')
                value = packed[inner_position:inner_position+count]
                inner_position += count
                return value
            pixels, plan = _decode_tokens(inner_take, width*height)
            if inner_position != len(packed):
                raise ValueError('Unexpected bytes after inner sprite stream')
        else:
            if size != 8+width*height:
                raise ValueError('Raster dimensions do not match decoded length')
            pixels, plan = _decode_tokens(take, size-8, history=header)
        records.append({'id': len(records), 'original_offset': start,
                        'flags': flags, 'width': width, 'height': height,
                        'offset_x': int.from_bytes(header[4:6], 'little', signed=True),
                        'offset_y': int.from_bytes(header[6:8], 'little', signed=True),
                        'layout': 'palette-index-raster',
                        'pixel_rows': [pixels[y*width:(y+1)*width].hex() for y in range(height)],
                        'compression_plan': plan})
    return {'format': 'fe2-grf-pixels-v2', 'records': records}


def encode(document):
    if document.get('format') != 'fe2-grf-pixels-v2':
        raise ValueError('Unsupported graphics source format')
    output = bytearray()
    for record in document['records']:
        flags, width, height = (record[key] for key in ('flags', 'width', 'height'))
        if not (isinstance(flags, int) and 0 <= flags < 255 and
                isinstance(width, int) and 0 <= width <= 65535 and
                isinstance(height, int) and 0 <= height <= 255):
            raise ValueError('Invalid header dimensions/flags')
        offsets = [record[key] for key in ('offset_x', 'offset_y')]
        if not all(isinstance(v, int) and -32768 <= v <= 32767 for v in offsets):
            raise ValueError('Invalid signed sprite offsets')
        header = (bytes((flags, height)) + width.to_bytes(2, 'little') +
                  b''.join(value.to_bytes(2, 'little', signed=True) for value in offsets))
        if record.get('layout') != 'palette-index-raster':
            raise ValueError('Record requires palette index raster')
        rows = [bytes.fromhex(row) for row in record['pixel_rows']]
        if len(rows) != height or any(len(row) != width for row in rows):
            raise ValueError('Pixel rows must match width and height')
        pixels = b''.join(rows)
        if flags & 2:
            data, plan, skip = pixels, record['compression_plan'], 0
        else:
            data = header+pixels
            plan = [{'literal': 8}]+record['compression_plan']
            skip = 9
        try:
            # The MCGA inner decoder uses a DEC AH loop: a zero literal
            # command copies 256 bytes rather than being a harmless empty run.
            # Invalid preservation hints must trigger normal recompression.
            if flags & 2 and any(token.get('literal') == 0 for token in plan):
                raise ValueError('Inner sprite literal runs must be nonzero')
            expanded, encoded = _expand(plan, data)
        except ValueError:
            expanded, encoded = b'', b''
        encoded = encoded[skip:] if expanded == data else compress(data, start=8 if skip else 0)
        size = 8+len(encoded) if flags & 2 else 8+len(pixels)
        if size > 65535 or width*height > 65535:
            raise ValueError('Record exceeds word-sized decoded length')
        # grf_allocate_record places each size-prefixed record wholly within
        # one 16 KiB EMS page; the renderer later maps only that one page.
        if size + 2 > 0x4000:
            raise ValueError('Size-prefixed graphics record exceeds one 16 KiB EMS page')
        output.extend(size.to_bytes(2, 'little'))
        output.extend(header)
        output.extend(encoded)
    output.extend(b'\x00\x00')
    return bytes(output)


def build(source_path):
    return encode(json.loads(Path(source_path).read_text(encoding='utf-8')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='operation', required=True)
    recover = sub.add_parser('recover')
    recover.add_argument('original', type=Path)
    recover.add_argument('source', type=Path)
    compile_parser = sub.add_parser('build')
    compile_parser.add_argument('source', type=Path)
    compile_parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.operation == 'recover':
        original = args.original.read_bytes()
        document = decode(original)
        assert encode(document) == original, 'Recovered source must reproduce artifact exactly'
        document['original_sha256'] = hashlib.sha256(original).hexdigest()
        args.source.parent.mkdir(parents=True, exist_ok=True)
        args.source.write_text(json.dumps(document, indent=2)+'\n', encoding='utf-8')
        print(f'Recovered {len(document["records"])} records')
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(build(args.source))


if __name__ == '__main__':
    main()

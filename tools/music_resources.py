"""Editable event sources for the MIDI chunks shipped under FE2's .ROL names."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / 'build/resources/music'
CHANNEL_KINDS = {0x8: 'note_off', 0x9: 'note_on', 0xa: 'poly_pressure',
                 0xb: 'controller', 0xc: 'program_change', 0xd: 'channel_pressure',
                 0xe: 'pitch_bend'}


def read_vlq(data: bytes, pos: int) -> tuple[int, int, str]:
    start, value = pos, 0
    for _ in range(4):
        if pos >= len(data):
            raise ValueError('Truncated variable-length quantity')
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 127)
        if not byte & 128:
            return value, pos, data[start:pos].hex()
    raise ValueError('Variable-length quantity exceeds four bytes')


def write_vlq(value: int, spelling: str | None = None) -> bytes:
    if not isinstance(value, int) or not 0 <= value <= 0xfffffff:
        raise ValueError('Variable-length quantity outside 28-bit range')
    if spelling:
        raw = bytes.fromhex(spelling)
        old, end, _ = read_vlq(raw, 0)
        if end != len(raw):
            raise ValueError('Extra bytes in variable-length spelling')
        if old == value:
            return raw
    result = [value & 127]
    while value >> 7:
        value >>= 7
        result.append((value & 127) | 128)
    return bytes(reversed(result))


def parse_track(data: bytes) -> list[dict]:
    events, pos, running = [], 0, None
    while pos < len(data):
        delta, pos, spelling = read_vlq(data, pos)
        if pos >= len(data):
            raise ValueError('Missing event status')
        omitted = data[pos] < 128
        if omitted:
            if running is None:
                raise ValueError('Running status without preceding channel status')
            status = running
        else:
            status = data[pos]
            pos += 1
        event = {'delta': delta, 'delta_encoding': spelling, 'status': status,
                 'running_status': omitted}
        if 0x80 <= status <= 0xef:
            running = status
            size = 1 if status >> 4 in (0xc, 0xd) else 2
            payload = data[pos:pos + size]
            if len(payload) != size or any(x >= 128 for x in payload):
                raise ValueError('Truncated or invalid channel event')
            pos += size
            event.update(kind=CHANNEL_KINDS[status >> 4], channel=status & 15,
                         data=list(payload))
        elif status in (0xff, 0xf0, 0xf7):
            running = None
            if status == 0xff:
                if pos >= len(data):
                    raise ValueError('Missing meta-event type')
                event['meta_type'] = data[pos]
                pos += 1
            size, pos, raw_size = read_vlq(data, pos)
            payload = data[pos:pos + size]
            if len(payload) != size:
                raise ValueError('Truncated meta/system payload')
            pos += size
            event.update(kind='meta' if status == 0xff else 'sysex',
                         payload_hex=payload.hex(), length_encoding=raw_size)
            if status == 0xff and event['meta_type'] in (1, 2, 3, 4, 5, 6, 7):
                event['text_preview'] = payload.decode('latin1')
        else:
            raise ValueError(f'Unsupported track status {status:#x}')
        events.append(event)
    return events


def encode_track(events: list[dict]) -> bytes:
    out, running = bytearray(), None
    for event in events:
        out += write_vlq(event['delta'], event.get('delta_encoding'))
        status = event['status']
        omitted = event.get('running_status', False)
        if omitted and (running != status or not 0x80 <= status <= 0xef):
            raise ValueError('Invalid running-status reference')
        if not omitted:
            out.append(status)
        if 0x80 <= status <= 0xef:
            running = status
            payload = bytes(event['data'])
            size = 1 if status >> 4 in (0xc, 0xd) else 2
            if len(payload) != size or any(x >= 128 for x in payload):
                raise ValueError('Invalid channel event data')
            out += payload
        elif status in (0xff, 0xf0, 0xf7):
            running = None
            if status == 0xff:
                out.append(event['meta_type'])
            payload = bytes.fromhex(event['payload_hex'])
            out += write_vlq(len(payload), event.get('length_encoding'))
            out += payload
        else:
            raise ValueError(f'Unsupported track status {status:#x}')
    return bytes(out)


def export(data: bytes, name: str) -> dict:
    if not data.startswith(b'MThd'):
        raise ValueError('Expected MThd header')
    chunks, pos = [], 0
    while pos < len(data):
        if len(data) - pos < 8:
            chunks.append({'kind': 'trailing', 'payload_hex': data[pos:].hex()})
            break
        tag, size = data[pos:pos + 4], struct.unpack('>I', data[pos + 4:pos + 8])[0]
        pos += 8
        if size > len(data) - pos:
            raise ValueError('Chunk length exceeds file')
        payload = data[pos:pos + size]
        pos += size
        chunk = {'tag_hex': tag.hex()}
        if tag == b'MThd':
            if len(payload) < 6:
                raise ValueError('Truncated MIDI header')
            fmt, count, division = struct.unpack('>HHH', payload[:6])
            chunk.update(kind='header', format=fmt, track_count=count,
                         division=division, extra_hex=payload[6:].hex())
        elif tag == b'MTrk':
            chunk.update(kind='track', events=parse_track(payload))
        else:
            chunk.update(kind='opaque', payload_hex=payload.hex())
        chunks.append(chunk)
    return {'schema': 'fe2-midi-source-v1', 'original_name': name,
            'original_sha256': hashlib.sha256(data).hexdigest(), 'chunks': chunks}


def encode(source: dict) -> bytes:
    if source['schema'] != 'fe2-midi-source-v1':
        raise ValueError('Unknown music source schema')
    out = bytearray()
    for chunk in source['chunks']:
        kind = chunk['kind']
        if kind == 'trailing':
            out += bytes.fromhex(chunk['payload_hex'])
            continue
        tag = bytes.fromhex(chunk['tag_hex'])
        if len(tag) != 4:
            raise ValueError('Chunk tag must contain four bytes')
        if kind == 'header':
            payload = struct.pack('>HHH', chunk['format'], chunk['track_count'],
                                  chunk['division']) + bytes.fromhex(chunk['extra_hex'])
        elif kind == 'track':
            payload = encode_track(chunk['events'])
        elif kind == 'opaque':
            payload = bytes.fromhex(chunk['payload_hex'])
        else:
            raise ValueError('Unknown chunk kind')
        out += tag + struct.pack('>I', len(payload)) + payload
    return bytes(out)


def build(source_path: str | Path) -> bytes:
    return encode(json.loads(Path(source_path).read_text(encoding='utf-8')))


def build_music(original_name: str, output_path: str | Path | None = None) -> bytes:
    if Path(original_name).name != original_name or not original_name.endswith('.rol'):
        raise ValueError('Expected original .rol basename')
    result = build(SOURCE_DIR / (original_name.removesuffix('.rol') + '.json'))
    if output_path is not None:
        Path(output_path).write_bytes(result)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    ex = sub.add_parser('export')
    ex.add_argument('original', type=Path)
    ex.add_argument('source', type=Path)
    bu = sub.add_parser('build')
    bu.add_argument('source', type=Path)
    bu.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'export':
        original = args.original.read_bytes()
        source = export(original, args.original.name)
        if encode(source) != original:
            raise ValueError('Export failed exact-byte round trip')
        args.source.parent.mkdir(parents=True, exist_ok=True)
        args.source.write_text(json.dumps(source, indent=2) + '\n', encoding='utf-8')
    else:
        args.output.write_bytes(build(args.source))


if __name__ == '__main__':
    main()

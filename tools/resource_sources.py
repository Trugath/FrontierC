"""Compile editable assets to the JSON consumed by the recovered binary codecs."""
from pathlib import Path
import configparser
import csv
import json
import os
import re
import struct
import tempfile
import time
import zlib

from music_resources import export as midi_document

ROOT = Path(__file__).resolve().parents[1]
INTERMEDIATE = ROOT / 'build/resources'


def settings(path):
    config = configparser.ConfigParser(interpolation=None)
    with Path(path).open(encoding='utf-8') as stream:
        config.read_file(stream)
    return config


def csv_integers(path):
    """Every integer in a CSV. A first row that is not numeric is a header."""
    with Path(path).open(encoding='ascii', newline='') as stream:
        rows = list(csv.reader(stream))
    if rows and any(cell.strip() and _integer_token(cell) is None for cell in rows[0]):
        rows = rows[1:]
    values = []
    for row in rows:
        for cell in row:
            cell = cell.strip()
            if not cell:
                continue
            value = _integer_token(cell)
            if value is None:
                raise ValueError(f'{Path(path).name}: {cell!r} is not an integer')
            values.append(value)
    return values


def _integer_token(token):
    try:
        return int(token.strip(), 0)
    except ValueError:
        return None


def asset(source, name):
    directory = Path(source).resolve().parent
    result = (directory / name).resolve()
    if not result.is_relative_to(directory):
        raise ValueError('Asset path leaves its source directory')
    return result


def read_palette(path):
    lines = Path(path).read_text(encoding='utf-8').splitlines()
    if not lines or lines[0] != 'GIMP Palette':
        raise ValueError('Expected a GIMP palette')
    colors = []
    for line in lines[1:]:
        line = line.strip()
        if not line or line.startswith(('#', 'Name:', 'Columns:')):
            continue
        values = [int(v) for v in line.split()[:3]]
        if len(values) != 3 or any(not 0 <= v <= 255 for v in values):
            raise ValueError('Palette components must be RGB8 values')
        colors.append([(v * 63 + 127) // 255 for v in values])
    return colors


def write_palette(path, colors, name):
    lines = ['GIMP Palette', 'Name: ' + name, 'Columns: 16',
             '# RGB8 display values; the build quantizes to VGA RGB6.']
    for index, color in enumerate(colors):
        rgb = [(v * 255 + 31) // 63 for v in color]
        lines.append(' '.join(f'{v:3d}' for v in rgb) + f'  color_{index:03d}')
    Path(path).write_text('\n'.join(lines) + '\n', encoding='utf-8')


def png_chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))


def write_png(path, width, height, rows, transparent_zero=False, colors_rgb6=None):
    """Store authoritative pixel indices with an optional VGA palette preview."""
    raw = b''.join(b'\x00' + bytes.fromhex(row) for row in rows)
    header = struct.pack('>IIBBBBB', width, height, 8, 3, 0, 0, 0)
    if colors_rgb6 is None:
        palette = bytes(v for index in range(256) for v in (index, index, index))
    else:
        if len(colors_rgb6) != 256 or any(len(color) != 3 or any(type(v) is not int or not 0 <= v <= 63 for v in color) for color in colors_rgb6):
            raise ValueError('PNG preview requires 256 RGB6 palette entries')
        palette = bytes((v * 255 + 31) // 63 for color in colors_rgb6 for v in color)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', header) +
                          png_chunk(b'PLTE', palette) + (png_chunk(b'tRNS', b'\x00') if transparent_zero else b'') +
                          png_chunk(b'IDAT', zlib.compress(raw)) +
                          png_chunk(b'IEND', b''))


def read_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Expected PNG image')
    position, chunks = 8, []
    while position < len(data):
        if position + 12 > len(data):
            raise ValueError('Truncated PNG chunk')
        size = int.from_bytes(data[position:position+4], 'big')
        tag = data[position+4:position+8]
        end = position+8+size
        payload = data[position+8:end]
        if end+4 > len(data) or int.from_bytes(data[end:end+4], 'big') != zlib.crc32(tag+payload):
            raise ValueError('PNG checksum mismatch')
        chunks.append((tag, payload))
        position = end+4
        if tag == b'IEND':
            break
    if position != len(data) or not chunks or chunks[0][0] != b'IHDR' or chunks[-1] != (b'IEND', b''):
        raise ValueError('Invalid PNG structure')
    width, height, depth, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', chunks[0][1])
    if (not 0 < width <= 65535 or not 0 < height <= 255 or width*height > 65535 or
            compression or filtering or interlace or
            not ((color == 3 and depth in (1, 2, 4, 8)) or (color == 0 and depth == 8))):
        raise ValueError('Use a non-interlaced indexed PNG (1/2/4/8-bit) or 8-bit grayscale PNG')
    palettes = [payload for tag, payload in chunks if tag == b'PLTE']
    if color == 3 and (len(palettes) != 1 or not palettes[0] or len(palettes[0]) % 3 or len(palettes[0]) > 768):
        raise ValueError('Indexed PNG requires a valid palette')
    packed_width = (width*depth+7)//8
    expected = (packed_width+1)*height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(b''.join(payload for tag, payload in chunks if tag == b'IDAT'), expected+1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('PNG raster length differs from dimensions')
    previous, rows = bytes(packed_width), []
    for y in range(height):
        start = y*(packed_width+1)
        filter_type, row = raw[start], bytearray(raw[start+1:start+1+packed_width])
        if filter_type > 4:
            raise ValueError('Unknown PNG row filter')
        for x in range(packed_width):
            left, above, corner = row[x-1] if x else 0, previous[x], previous[x-1] if x else 0
            if filter_type == 1:
                row[x] = (row[x]+left) & 255
            elif filter_type == 2:
                row[x] = (row[x]+above) & 255
            elif filter_type == 3:
                row[x] = (row[x]+((left+above)//2)) & 255
            elif filter_type == 4:
                p = left+above-corner
                distances = (abs(p-left), abs(p-above), abs(p-corner))
                prediction = (left, above, corner)[distances.index(min(distances))]
                row[x] = (row[x]+prediction) & 255
        pixels = bytes((row[x*depth//8] >> (8-depth-(x*depth % 8))) & ((1 << depth)-1)
                       for x in range(width))
        if color == 3 and any(index >= len(palettes[0])//3 for index in pixels):
            raise ValueError('PNG pixel exceeds its palette')
        rows.append(pixels.hex())
        previous = row
    return width, height, rows


def read_font(path):
    lines = Path(path).read_text(encoding='ascii').splitlines()
    if not lines or lines[0] != 'STARTFONT 2.1' or lines[-1] != 'ENDFONT':
        raise ValueError('Expected a BDF 2.1 font')
    glyphs, block = [], None
    for line in lines:
        if line.startswith('STARTCHAR '):
            if block is not None:
                raise ValueError('Nested BDF glyph')
            block = []
        elif line == 'ENDCHAR':
            if block is None:
                raise ValueError('Unexpected BDF ENDCHAR')
            def field(key):
                matches = [v.split()[1:] for v in block if v.startswith(key+' ')]
                if len(matches) != 1:
                    raise ValueError('Missing/duplicate BDF '+key)
                return [int(v) for v in matches[0]]
            encoding, advance, bounds = field('ENCODING'), field('DWIDTH'), field('BBX')
            if encoding != [len(glyphs)] or len(advance) != 2 or advance[1] or not 0 <= advance[0] <= 255 or bounds != [8, 9, 0, 0]:
                raise ValueError('Font requires contiguous glyph IDs, 8x9 cells and byte advances')
            bitmap = block[block.index('BITMAP')+1:]
            if len(bitmap) != 9 or any(not re.fullmatch('[0-9a-fA-F]{2}', row) for row in bitmap):
                raise ValueError('Glyph bitmap requires nine byte rows')
            glyphs.append({'index':len(glyphs), 'bitmap_rows':[f'{int(row,16):08b}' for row in bitmap],
                           'advance':advance[0]})
            block = None
        elif block is not None:
            block.append(line)
    counts = [line for line in lines if line.startswith('CHARS ')]
    if block is not None or counts != ['CHARS '+str(len(glyphs))] or len(glyphs) != 115:
        raise ValueError('Font requires 115 complete glyphs')
    return glyphs


def write_font(path, glyphs):
    lines = ['STARTFONT 2.1', 'FONT fe2-recovered', 'SIZE 9 75 75', 'FONTBOUNDINGBOX 8 9 0 0',
             'COMMENT ENCODING values are game glyph IDs, not Unicode characters',
             'STARTPROPERTIES 2', 'FONT_ASCENT 9', 'FONT_DESCENT 0', 'ENDPROPERTIES', 'CHARS 115']
    for glyph in glyphs:
        advance = glyph['advance']
        lines += [f'STARTCHAR glyph_{glyph["index"]:03d}', f'ENCODING {glyph["index"]}',
                  f'SWIDTH {advance*8000//9} 0', f'DWIDTH {advance} 0', 'BBX 8 9 0 0', 'BITMAP']
        lines += [f'{int(row,2):02X}' for row in glyph['bitmap_rows']]
        lines += ['ENDCHAR']
    lines += ['ENDFONT']
    Path(path).write_text('\n'.join(lines)+'\n', encoding='ascii')


def read_plan(path):
    plan = []
    for line in Path(path).read_text(encoding='ascii').splitlines():
        words = line.split('#',1)[0].split()
        if not words:
            continue
        if words[0] == 'literal' and len(words) == 2:
            plan.append({'literal':int(words[1])})
        elif words[0] == 'copy' and len(words) == 3:
            plan.append({'copy':int(words[1]), 'distance':int(words[2])})
        else:
            raise ValueError('Expected literal COUNT or copy COUNT DISTANCE')
    return plan


def integers(text):
    return [int(value, 0) for value in text.split()]


def document(kind, source):
    source = Path(source)
    if kind == 'midi':
        return midi_document(source.read_bytes(), source.stem+'.rol')
    config = settings(source)
    if kind == 'main_models':
        from main_model_resources import document as model_document
        return model_document(config, source, asset)
    if kind == 'intro_models':
        from intro_model_resources import document as model_document
        return model_document(config, source, asset)
    if kind == 'intro_objects':
        from intro_model_resources import object_document
        return object_document(config, source, asset)
    if kind == 'contract_callbacks':
        from contract_callback_table import document as callbacks_document
        return callbacks_document(config, source, asset)
    if kind == 'handcrafted_systems':
        from handcrafted_system_keys import document as systems_document
        return systems_document(config, source, asset)
    if kind == 'intro_timeline':
        from intro_timeline_resources import document as timeline_document
        return timeline_document(config, source, asset)
    if kind == 'system_records':
        from system_records import document as record_document
        return record_document(config, source, asset)
    if kind == 'ownership_challenges':
        from ownership_challenges import document as challenge_document
        return challenge_document(config, source, asset)
    if kind == 'module_tables':
        from module_tables import document as table_document
        return table_document(config, source, asset, read_palette)
    if kind == 'strings':
        from string_resources import document as string_document
        return string_document(config, source, asset)
    if kind == 'graphics':
        records = []
        for section in config.sections():
            if section != f'sprite {len(records):03d}':
                raise ValueError('Sprite sections must be ordered and contiguous')
            entry = config[section]
            width, height, rows = read_png(asset(source, entry['image']))
            if entry.get('compression'):
                plan = read_plan(asset(source, entry['compression']))
            else:
                from graphics_resources import compression_plan
                plan = compression_plan(bytes.fromhex(''.join(rows)))
            records.append({'id':len(records), 'flags':entry.getint('flags'), 'width':width, 'height':height,
                            'offset_x':entry.getint('offset_x'), 'offset_y':entry.getint('offset_y'),
                            'layout':'palette-index-raster', 'pixel_rows':rows,
                            'compression_plan':plan})
        if len(records) != 225:
            raise ValueError('Expected 225 sprites')
        return {'format':'fe2-grf-pixels-v2', 'records':records}
    if kind == 'roland':
        effects = []
        for section in config.sections():
            if section != f'effect {len(effects):02d}':
                raise ValueError('Effect sections must be ordered and contiguous')
            entry = config[section]
            midi = midi_document(asset(source, entry['music']).read_bytes(), entry['music'])
            headers = [c for c in midi['chunks'] if c['kind'] == 'header']
            tracks = [c for c in midi['chunks'] if c['kind'] == 'track']
            if len(midi['chunks']) != 2 or len(tracks) != 1 or len(headers) != 1 or headers[0]['format'] != 0:
                raise ValueError('Effects require a single-track format-0 MIDI file')
            effects.append({'index':len(effects), 'image_offset':int(entry['image_offset'],0),
                            'encoded_bytes':entry.getint('encoded_bytes'), 'events':tracks[0]['events']})
        return {'schema':'fe2-roland-effects-v1', 'effects':effects}
    if kind == 'palette':
        entry = config['palette']
        views = []
        for section in config.sections():
            if section.startswith('view '):
                first, count = config[section].getint('first_color'), config[section].getint('color_count')
                views.append({'first_color':first, 'color_count':count, 'image_offset':0xb0+first*3})
        return {'format':'fe2-composite_context-dac-palettes-v1', 'colors_rgb6':read_palette(asset(source,entry['colors'])),
                'pointer_labels':integers(entry['pointer_labels']), 'palette_views':views}
    if kind == 'video':
        entry = config['video']
        message = entry['memory_error_message']
        message = re.sub(r'\\([rn\\])', lambda m:{'r':'\r','n':'\n','\\':'\\'}[m[1]], message)
        result = {'format':'fe2-mcga-assets-v1', 'overlay_filename':entry['overlay_filename'],
                  'graphics_filename':entry['graphics_filename'], 'memory_error_message':message,
                  'palette_state_bytes':integers(entry['palette_state_bytes']),
                  'colors_rgb6':read_palette(asset(source,entry['palette'])),
                  'glyphs':read_font(asset(source,entry['font']))}
        if 'palette_cache_seeds' in entry:
            with asset(source, entry['palette_cache_seeds']).open(encoding='ascii', newline='') as stream:
                result['palette_cache_seeds'] = [[int(value.strip(), 0) for value in row] for row in csv.reader(stream)]
        for key in ('leading_byte','overlay_filename_prefix','graphics_filename_prefix','zero_padding_bytes','default_palette_offset'):
            result[key] = int(entry[key],0)
        return result
    raise ValueError('Unknown native resource type')


def read_published(path):
    """Open a published document. A concurrent replace can deny the open briefly."""
    path = Path(path)
    last = None
    for _ in range(50):
        try:
            return path.read_text(encoding='utf-8')
        except PermissionError as error:
            last = error
            time.sleep(0.02)
    raise last


def compile_resource(kind, source):
    source = Path(source).resolve()
    # Keep generated JSON paths deterministic and independent of --output.
    relative = source.relative_to(ROOT/'data')
    output = INTERMEDIATE / relative.with_suffix('.json')
    result = document(kind, source)
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(result, indent=2) + '\n'
    # Parallel builds publish the same document. Leave an identical file in
    # place so readers are not caught by a replace they do not need.
    try:
        if read_published(output) == payload:
            return output
    except (FileNotFoundError, PermissionError):
        pass
    # Multiple native builds consume this shared cache concurrently. Publish
    # a complete document so a reader never sees another writer's truncate.
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=output.parent,
                                     suffix='.tmp', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(payload)
    try:
        for attempt in range(50):
            try:
                os.replace(temporary, output)
                break
            except PermissionError:
                # Windows readers may briefly hold a non-delete-sharing handle.
                if attempt == 49:
                    raise
                time.sleep(0.02)
    finally:
        temporary.unlink(missing_ok=True)
    return output

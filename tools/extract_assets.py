#!/usr/bin/env python3
"""Extract copyrighted Frontier: Elite II assets from a user-supplied zip.

The zip is a DOS install, or a folder of the same files. Numeric gameplay
tables, ship and system names, and other short labels stay in this tree.
This reads the original artwork, 3D models, music, and the longer original
prose:

- EL2G.GRF: sprites
- EL2MCGA3.OVL: bitmap font and boot palette
- EL2M13.OVL: portrait palette
- FRONTIER.EXE: main and auxiliary model banks
- EL2M08.OVL: intro models
- EL2M00.OVL, EL2M02.OVL, EL2M03.OVL, EL2M08.OVL, EL2M09.OVL, EL2M10.OVL,
  EL2M14.OVL, EL2M15.OVL, EL2M17.OVL, EL2M18.OVL, FRONTIER.EXE: system
  descriptions, chart text, flight text, intro and title text, personal names,
  commander credits, status text, offence notices, mining text, station dialogue,
  and the ship-name catalogue
- EL2M19.OVL: the galaxy density grid
- EL2ADLIB.OVL: the AdLib soundtrack, its instruments, and its pitch curves
- EL2SETUP.OVL: the setup-check messages
- the nine .ROL scores, written as MIDI

Decoders recovered in this project cover GRF pixels,
MCGA font/palette layout, model headers, string directories, and MIDI.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

import graphics_resources
import model_contact_offsets
import model_contact_resources
import model_geometry_resources
import model_render_programs
import music_resources
import resource_sources
import sprite_previews

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'data'

MUSIC = ('babayaga', 'barem', 'bluedan', 'elitet', 'ggofkiev', 'hallking', 'intro', 'jupalt', 'valkries')
REQUIRED = ('el2g.grf', 'el2mcga3.ovl', 'el2m13.ovl', 'frontier.exe', 'el2m08.ovl',
            *(f'{name}.rol' for name in MUSIC),
            'el2m00.ovl', 'el2m02.ovl', 'el2m03.ovl', 'el2m09.ovl', 'el2m10.ovl',
            'el2m14.ovl', 'el2m15.ovl', 'el2m17.ovl', 'el2m18.ovl', 'el2m19.ovl',
            'el2adlib.ovl', 'el2setup.ovl')
# Original prose. The committed identifiers.txt is the name list; unpack writes strings.tsv.
PROSE = (
    ('systems', 'el2m00.ovl'),
    ('chart', 'el2m02.ovl'),
    ('flight', 'el2m03.ovl'),
    ('intro', 'el2m08.ovl'),
    ('names', 'el2m09.ovl'),
    ('commander', 'el2m10.ovl'),
    ('frontier', 'frontier.exe'),
    ('status', 'el2m14.ovl'),
    ('offences', 'el2m15.ovl'),
    ('mining', 'el2m17.ovl'),
    ('interaction', 'el2m18.ovl'),
)
SETUP_TEXT = 0x6a
SETUP_MESSAGES = (
    'dos_version_message', 'cpu_required_message', 'base_memory_message',
    'ems_missing_message', 'ems_memory_message', 'sound_menu_message',
)
# DOS Frontier: Elite II file hashes. A different release is rejected before decoding.
VERSIONS = {
    'babayaga.rol': 'a0e575d9c9adb9f38aa415a705e98daeb91a0722035d1800e89d35cc8d20cc5b',
    'barem.rol': '1cf990d45823ca2545a696a23487a06382a2c627d6c132e9dbead561c27edcb2',
    'bluedan.rol': '6218208f5b52735b281a9f7c2234703a7bea32ab32de14620b6b4cfb17adec7b',
    'el2g.grf': '5ee7a6afcf2be3456cad77a471ebb68f5eb3fa527e961657a00cf02533272257',
    'el2adlib.ovl': '82a388beadd1b7038bf39586762b604d289870a78b362269af6e160bdd170bfb',
    'el2m00.ovl': '498ec72901e1e976deb40ccf239689721aa2d065e7e7230a4105175d01520644',
    'el2m02.ovl': '5983e32f81f3d87718fb9f755470a9139c79b2fe96e35faf1be1dd51e40e95d4',
    'el2m03.ovl': '9c7291fb7dd325df438a210ca5251024064cb0e759f49f2282e94959f30263b7',
    'el2m08.ovl': '9a54088bf20fe6a0d11860a84392772a83914c4fffc75e089ca2ac96aa7fad45',
    'el2m09.ovl': '9c78faa549fbd60fe6d5252ac8e86d90147db3cf625d20e711fb3290269531dc',
    'el2m10.ovl': '47d6c8ddf5864a1ee57677b55f215d67d5f004bee5bcdc71439d2a8fee7ba7fa',
    'el2m13.ovl': '0e74df1d4125353946560b2fb8c55dde139a311cfe7b1d1a6f727d8a8634229d',
    'el2m14.ovl': '3e300b9c0ca16b8c76a79a53273ef3d330bbf863fdcd24a3280c7649c97c426d',
    'el2m15.ovl': '265741fa94ecadbd73296f66e85f4908f6de23abec26863c66f6bfc6e226a6ab',
    'el2m17.ovl': '0278fb19894dfee22f78bcf341cf58762a2f8947d4cff24ca420f1f77eb1ab81',
    'el2m18.ovl': 'b7af80f723a553f499f51483764a1f3b161231ebb90948db1480dac8a9b4ef7c',
    'el2m19.ovl': 'a33c7573c493fb7cad8ad4cc0cde478ea116681b5166884177a36080a55c8c3c',
    'el2mcga3.ovl': '34bdc63da0dfee171f1dc86c92d0ebd5dd3b2fa697a6d725851abfdea7bff291',
    'el2setup.ovl': '2ee4e914f1c464a338889caae3bc65e27640654072bf4add1d278b9f833852a0',
    'elitet.rol': '68b8814f045d14902854ce0144f2ef9797e98866ac9a3f15b31954cfd520a646',
    'frontier.exe': '0b7c477eb68f43785194f1d489277c7bb10bdeb743a3b9a0a4092b1692456982',
    'ggofkiev.rol': '93c40e207ffdfcb6c8e4e062dbb50b08ecdb8dc0dbe698f2ea77f6d818c342ad',
    'hallking.rol': '074f7b9e09f816a989adbd39185515f5d94414f8b9294cc710cc71c0701b848f',
    'intro.rol': '61c8ab1022c2b18173278051d0d3ce158f76169c5d43cd3e1b323ebc6ad84419',
    'jupalt.rol': '244567d4dae0747b3137fada92b9149152a3332604e67cdb4a4ccfa1d4eec62a',
    'valkries.rol': '99a93721a0e2d9798d91444d625b9cb4138c1d7c5a83c2ec3809bb5242dad44f',
}
# Decoded from the install. These must not be copied from the repository seed.
_EXTRACTED = (
    'graphics/sprites/', 'graphics/compression/',
    'models/frontier/models/', 'models/frontier/geometry/',
    'models/frontier/contacts/', 'models/frontier/contact_offsets/',
    'models/frontier_auxiliary/models/',
    'intro/models/', 'intro/geometry/', 'intro/objects/',
    'music/', 'roland/effects/',
)

MCGA_START = 0x5d4b
MCGA_COLORS = 0x5de7
MCGA_FONT = 0x5f64
PALETTE_START, PALETTE_END = 0xb0, 0x680
GEOMETRY_LINE = re.compile(
    r'^(geometry_pipeline token=0x[0-9a-f]+ words=0x[0-9a-f]+,0x[0-9a-f]+) payload=([0-9a-f]+|-)( # program \+0x[0-9a-f]+)?$')


def program_image(blob):
    """Drop the MZ header. Model and palette offsets are image-relative."""
    if len(blob) < 64 or blob[:2] != b'MZ':
        raise ValueError('Expected an MZ executable')
    header = int.from_bytes(blob[8:10], 'little') * 16
    if header < 64 or header >= len(blob):
        raise ValueError('Invalid MZ header')
    return blob[header:]


def load_binaries(path):
    """Map lowercase file names to bytes. A zip and a directory are equivalent."""
    path = Path(path)
    if path.is_dir():
        found = {}
        for child in path.rglob('*'):
            if child.is_file() and child.name.lower() in REQUIRED:
                found.setdefault(child.name.lower(), child.read_bytes())
        return found
    if not zipfile.is_zipfile(path):
        raise ValueError('Expected a zip of Frontier binaries or a directory of those files')
    found = {}
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            name = Path(info.filename).name.lower()
            if name in REQUIRED and name not in found:
                found[name] = archive.read(info)
    return found


def missing_assets(files):
    return [name for name in REQUIRED if name not in files]


class InstallError(Exception):
    """The supplied install is missing a file or is a different game version."""


def check_install(files):
    """Reject a missing file or a hash that is not DOS Frontier: Elite II."""
    absent = missing_assets(files)
    if absent:
        raise InstallError('This Frontier install is missing: ' + ', '.join(absent))
    wrong = [name for name, digest in VERSIONS.items()
             if hashlib.sha256(files[name]).hexdigest() != digest]
    if wrong:
        raise InstallError('Unable to extract ' + ', '.join(wrong)
                           + ': not the Frontier: Elite II version this tool supports.')


def authored_files():
    """Project files the packer reads and the original binaries do not contain.

    Gameplay tables, indexes, and catalogs live in this repository. A repack
    into an empty directory has to carry them, or the dat cannot be built.
    Strings, sprites, models, music, and the other decoded outputs stay out
    of the seed because unpack writes those from the install.
    """
    extracted = {DATA / 'text' / module / 'strings.tsv' for module, _filename in PROSE}
    extracted.update(DATA / name for name in (
        'adlib/patches.csv', 'adlib/pitch_curves.csv', 'adlib/records.tsv',
        'galaxy/density.pgm', 'setup/messages.json', 'video/font.bdf',
        'video/default.gpl', 'palettes/composites.gpl'))
    paths = []
    for path in sorted(DATA.rglob('*')):
        if not path.is_file() or path.suffix == '.md' or path in extracted:
            continue
        relative = path.relative_to(DATA).as_posix()
        if any(relative.startswith(prefix) for prefix in _EXTRACTED):
            continue
        paths.append(path)
    return paths


def seed_authored(data_root):
    """Copy authored files that are not already in the destination."""
    data_root = Path(data_root)
    for source in authored_files():
        if not source.is_file():
            raise InstallError(f'This project is missing {source.relative_to(DATA).as_posix()}')
        relative = source.relative_to(DATA)
        target = data_root / relative
        if target.resolve() == source.resolve() or target.is_file():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())


def _words(values):
    lines = []
    for offset in range(0, len(values), 16):
        row = values[offset:offset + 16]
        lines.append('    ' + ', '.join(f'0x{value:02x}' for value in row))
    return '\n'.join(lines)


def model_record_bytes(model):
    """Pack one compiled model the way the executable stores it."""
    extension = bytes(model['extension'])
    vertices = bytes(model['vertices'])
    classification = bytes(model['classification'])
    program = bytes(model['program'])
    absent = model.get('absent_classification', False)
    vertex_at = 16 + len(extension)
    if absent:
        class_at, class_work = 0, 0
        program_at = vertex_at + len(vertices)
    else:
        class_at = vertex_at + len(vertices)
        class_work = len(classification) + 4
        program_at = class_at + len(classification)
    header = model['header']
    if len(header) != 3:
        raise ValueError('Model header requires three words')
    blob = struct.pack('<8H', program_at, vertex_at, len(vertices) * 16, class_at, class_work, *header)
    blob += extension + vertices
    if not absent:
        blob += classification
    return blob + program


def _parse_model(blob, name):
    if len(blob) < 16:
        raise ValueError(f'{name} is shorter than a model header')
    program_at, vertex_at, vertex_work, class_at, class_work, *header = struct.unpack_from('<8H', blob)
    if vertex_at < 16 or vertex_at > len(blob) or vertex_work % 16:
        raise ValueError(f'{name} has an invalid vertex header')
    vertex_len = vertex_work // 16
    extension = blob[16:vertex_at]
    vertices = blob[vertex_at:vertex_at + vertex_len]
    if len(vertices) != vertex_len:
        raise ValueError(f'{name} vertices leave the record')
    if class_at == 0 and class_work == 0:
        absent = True
        classification = b''
        expected = vertex_at + vertex_len
    else:
        absent = False
        if class_work < 4:
            raise ValueError(f'{name} has an invalid classification header')
        class_len = class_work - 4
        classification = blob[class_at:class_at + class_len]
        if len(classification) != class_len:
            raise ValueError(f'{name} classification leaves the record')
        expected = class_at + class_len
    if program_at != expected or program_at > len(blob):
        raise ValueError(f'{name} program pointer disagrees with its sections')
    return {'name': name, 'header': header, 'extension': extension, 'vertices': vertices,
            'classification': classification, 'program': blob[program_at:], 'absent': absent,
            'program_at': program_at}


def _split_contact(program, program_at, extension, name):
    """Separate the contact stream, and a preceding basis-offset walk when present."""
    if len(extension) < 12:
        return program, None
    pointer = int.from_bytes(extension[10:12], 'little')
    if pointer == 0:
        return program, None
    contact_at = pointer - program_at
    if not 0 <= contact_at < len(program):
        raise ValueError(f'{name} contact pointer leaves its program')
    try:
        contact, contact_end = model_contact_resources.decode_source(program, contact_at)
    except (ValueError, AssertionError) as error:
        raise ValueError(f'{name} contact program: {error}') from error
    pos = contact_at
    while pos >= 2:
        length = int.from_bytes(program[pos - 2:pos], 'little', signed=True)
        if length == 0:
            break
        if pos < 4:
            pos = contact_at
            break
        pos -= 4
    else:
        pos = contact_at
    offsets = None
    if pos != contact_at:
        try:
            offsets = model_contact_offsets.decode_source(program[pos:contact_at])
        except (ValueError, AssertionError):
            pos = contact_at
    render = program[:pos] + program[contact_end:]
    return render, {'source': contact, 'offsets': offsets, 'at': pos}


def _instance_entries(extension, vertex_len, class_len):
    if len(extension) not in (118, 198):
        return None
    base = 16 + len(extension) + vertex_len + class_len
    entries = []
    for index in range(16, len(extension), 2):
        address = extension[index] + 256 * extension[index + 1]
        offset = address - base
        if offset < 0:
            return None
        entries.append((0x20 + len(entries), offset))
    return entries


def _entry_offsets(extension, vertex_len, class_len):
    entries = _instance_entries(extension, vertex_len, class_len)
    if not entries:
        return []
    return [offset for _, offset in entries]


def _decode_program(program, entries):
    try:
        source, _, _ = model_render_programs.decode_source(
            program, follow_branches=True, entry_offsets=entries)
    except (ValueError, AssertionError):
        source, _, _ = model_render_programs.decode_source(program)
    if model_render_programs.compile_source(source) != program:
        raise ValueError('Decoded renderer program does not round-trip')
    return source


def _lift_ship_parameters(source, program, model_name, table):
    if model_name not in table:
        return source
    needle = table[model_name]
    start = program.find(needle)
    if start < 0 or program.find(needle, start + 1) != -1:
        return source
    lines = [line for line in source.splitlines() if line.split('#', 1)[0].strip()]
    rewritten, cursor, skipping = [], 0, False
    for line in lines:
        chunk = model_render_programs.compile_source(line + '\n')
        if not skipping and cursor == start:
            rewritten.append(f'ship_parameters file=ship_parameters.csv model={model_name}')
            skipping = True
        if skipping:
            cursor += len(chunk)
            if cursor == start + len(needle):
                skipping = False
            elif cursor > start + len(needle):
                return source
            continue
        rewritten.append(line)
        cursor += len(chunk)
    if skipping or cursor != len(program):
        return source
    lifted = '\n'.join(rewritten) + '\n'

    def reader(name):
        if name != 'ship_parameters.csv':
            raise ValueError(name)
        return table['_csv']

    if model_render_programs.compile_source(lifted, reader) != program:
        return source
    return lifted


def _externalize_geometry(source, model_name, bank):
    lines, files, used = [], {}, set()
    for line in source.splitlines():
        body, separator, comment = line.partition(' # ')
        match = GEOMETRY_LINE.match(body)
        if not match or match[2] == '-':
            lines.append(line)
            continue
        payload = bytes.fromhex(match[2])
        try:
            geometry = model_geometry_resources.decode_source(payload)
        except (ValueError, AssertionError):
            lines.append(line)
            continue
        offset = comment.split('+0x', 1)[1] if comment.startswith('program +0x') else f'{len(used):04x}'
        while offset in used:
            offset = f'{int(offset, 16) + 1:04x}'
        used.add(offset)
        relative = f'geometry/{model_name}_{offset}.geometry'
        files[relative] = geometry
        suffix = f' # {comment}' if separator else ''
        lines.append(f'{match[1]} file={relative}{suffix}')
    for relative, geometry in files.items():
        target = bank / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(geometry, encoding='ascii')
    text = '\n'.join(lines) + ('\n' if lines else '')
    return text, files


def _write_model(model, render, contact, bank, table):
    name = model['name']
    entries = _entry_offsets(model['extension'], len(model['vertices']), len(model['classification']))
    source = _decode_program(render, [offset for offset in entries if offset < len(render)])
    source = _lift_ship_parameters(source, render, name, table)
    source, _files = _externalize_geometry(source, name, bank)
    if contact:
        contact_path = bank / 'contacts' / f'{name}.contact'
        contact_path.parent.mkdir(parents=True, exist_ok=True)
        contact_path.write_text(contact['source'], encoding='ascii')
        if contact['offsets']:
            offset_path = bank / 'contact_offsets' / f'{name}.basis_offsets'
            offset_path.parent.mkdir(parents=True, exist_ok=True)
            offset_path.write_text(contact['offsets'], encoding='ascii')
    instance = _instance_entries(model['extension'], len(model['vertices']), len(model['classification']))
    extension = model['extension'][:16] if instance else model['extension']
    instructions = '\n'.join('    ' + line.strip() for line in source.splitlines() if line.strip())
    lines = ['[header]']
    for key, value in zip(('scale_shift', 'reserved_0c', 'culling_radius'), model['header']):
        lines.append(f'{key} = 0x{value:04x}')
    lines += ['', '[extension]', 'bytes =']
    if extension:
        lines.append(_words(extension))
    if instance:
        lines.append('instance_programs =')
        lines.extend(f'    0x{selector:02x}, 0x{offset:04x}' for selector, offset in instance)
    lines += ['', '[vertices]', 'records =']
    for index in range(0, len(model['vertices']), 4):
        lines.append(_words(model['vertices'][index:index + 4]).rstrip())
    lines += ['', '[classification]', 'records =']
    for index in range(0, len(model['classification']), 4):
        lines.append(_words(model['classification'][index:index + 4]).rstrip())
    lines += ['', '[program]', 'instructions =', instructions]
    if contact:
        lines += ['', '[contact]', f'file = contacts/{name}.contact', f'program_offset = {contact["at"]}']
        if contact['offsets']:
            lines += ['', '[contact_offsets]', f'file = contact_offsets/{name}.basis_offsets',
                      f'program_offset = {contact["at"]}']
    target = bank / 'models' / f'{name}.model'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text('\n'.join(lines) + '\n', encoding='ascii')


def extract_model_bank(image, ini_path):
    """Write model sources for every span named by an existing bank ini."""
    ini_path = Path(ini_path)
    config = resource_sources.settings(ini_path)
    bank = ini_path.parent
    csv_path = bank / 'ship_parameters.csv'
    table = {}
    if csv_path.is_file():
        from ship_parameters import records
        text = csv_path.read_text(encoding='ascii')
        table = records(text)
        table['_csv'] = text
    allow_contact = config['bank'].get('module') != 'intro'
    count = 0
    for section in config.sections():
        if section in {'bank', 'trailer'}:
            continue
        entry = config[section]
        start, end = int(entry['start'], 0), int(entry['end'], 0)
        if not 0 <= start < end <= len(image):
            raise ValueError(f'{section} leaves the executable image')
        parsed = _parse_model(image[start:end], section)
        if parsed['absent'] != entry.getboolean('absent_classification', fallback=False):
            raise ValueError(f'{section} classification flag disagrees with its header')
        if allow_contact:
            render, contact = _split_contact(parsed['program'], parsed['program_at'], parsed['extension'], section)
        else:
            render, contact = parsed['program'], None
        _write_model(parsed, render, contact, bank, table)
        count += 1
    kind = 'intro_models' if not allow_contact else 'main_models'
    compiled = resource_sources.document(kind, ini_path)
    for model in compiled['models']:
        packed = model_record_bytes(model)
        if packed != image[model['start']:model['end']]:
            raise ValueError(f'{model["name"]} did not round-trip to the executable bytes')
    return count


def _plan_text(plan):
    lines = ['# Original encoding hints. Changed pixels may invalidate these and trigger recompression.']
    for token in plan:
        if 'literal' in token:
            lines.append(f'literal {token["literal"]}')
        else:
            lines.append(f'copy {token["copy"]} {token["distance"]}')
    return '\n'.join(lines) + '\n'


def _sprite_paths(ini_path, count):
    images, plans = {}, {}
    if ini_path.is_file():
        config = resource_sources.settings(ini_path)
        for index in range(count):
            section = f'sprite {index:03d}'
            if section not in config:
                break
            images[index] = config[section]['image']
            plans[index] = config[section]['compression']
    for index in range(count):
        images.setdefault(index, f'sprites/{index:03d}.png')
        plans.setdefault(index, f'compression/{index:03d}.lz')
    return images, plans


def _preview_colors(video_dir):
    palette = video_dir / 'default.gpl'
    font = video_dir / 'font.bdf'
    if not palette.is_file() or not font.is_file():
        return None
    colors = resource_sources.read_palette(palette)
    glyphs = resource_sources.read_font(font)
    payload = bytearray()
    for color in colors:
        payload.extend(color)
    for glyph in glyphs:
        payload.extend(int(row, 2) for row in glyph['bitmap_rows'])
        payload.append(glyph['advance'])
    payload = payload[:768].ljust(768, b'\x00')
    return [[payload[index] & 63, payload[index + 1] & 63, payload[index + 2] & 63]
            for index in range(0, 768, 3)]


def extract_sprites(blob, graphics_dir, video_dir):
    decoded = graphics_resources.decode(blob)
    records = decoded['records']
    if len(records) != 225:
        raise ValueError(f'EL2G.GRF contains {len(records)} sprites, expected 225')
    graphics_dir = Path(graphics_dir)
    images, plans = _sprite_paths(graphics_dir / 'el2g.ini', len(records))
    preview = _preview_colors(video_dir)
    for record in records:
        index = record['id']
        image = graphics_dir / images[index]
        plan = graphics_dir / plans[index]
        image.parent.mkdir(parents=True, exist_ok=True)
        plan.parent.mkdir(parents=True, exist_ok=True)
        resource_sources.write_png(image, record['width'], record['height'], record['pixel_rows'],
                                   transparent_zero=bool(record['flags'] & 1), colors_rgb6=preview)
        plan.write_text(_plan_text(record['compression_plan']), encoding='ascii')
    lines = ['# PNG pixel indices are authoritative. Do not reorder palette entries.',
             '# Indices 0..127 preview the boot palette. Portrait indices 128..255 preview',
             '# the identity-zero composite upload. Map body indices 128..255 preview the',
             '# system chart palette. Refresh: python tools/sprite_previews.py',
             '# Compression files preserve original token choices.', '']
    for record in records:
        index = record['id']
        lines += [f'[sprite {index:03d}]', f'image = {images[index]}', f'compression = {plans[index]}',
                  f'flags = {record["flags"]}', f'offset_x = {record["offset_x"]}',
                  f'offset_y = {record["offset_y"]}', '']
    (graphics_dir / 'el2g.ini').write_text('\n'.join(lines), encoding='ascii')
    return len(records)


def extract_mcga(blob, video_dir):
    image = program_image(blob)
    if b'EL2G.GRF' not in image[MCGA_START:MCGA_COLORS] or len(image) < MCGA_FONT + 115 * 10:
        raise ValueError('EL2MCGA3.OVL does not contain the font and boot palette')
    colors = [list(image[offset:offset + 3]) for offset in range(MCGA_COLORS, MCGA_FONT, 3)]
    if len(colors) != 127 or any(component > 63 for color in colors for component in color):
        raise ValueError('Boot palette is not 127 VGA colors')
    glyphs = []
    for index in range(115):
        offset = MCGA_FONT + 10 * index
        glyphs.append({'index': index, 'bitmap_rows': [f'{value:08b}' for value in image[offset:offset + 9]],
                       'advance': image[offset + 9]})
    video_dir = Path(video_dir)
    video_dir.mkdir(parents=True, exist_ok=True)
    resource_sources.write_palette(video_dir / 'default.gpl', colors, 'FE2 MCGA canonical palette')
    resource_sources.write_font(video_dir / 'font.bdf', glyphs)
    return len(glyphs)


def extract_composite_palette(blob, palette_dir):
    image = program_image(blob)
    if len(image) < PALETTE_END:
        raise ValueError('EL2M13.OVL does not contain the portrait palette')
    colors = [list(image[offset:offset + 3]) for offset in range(PALETTE_START, PALETTE_END, 3)]
    if len(colors) != 496 or any(component > 63 for color in colors for component in color):
        raise ValueError('Portrait palette is not 496 VGA colors')
    palette_dir = Path(palette_dir)
    palette_dir.mkdir(parents=True, exist_ok=True)
    resource_sources.write_palette(palette_dir / 'composites.gpl', colors, 'FE2 Composite palette')
    return len(colors)


def _intro_object_source(raw):
    """The editable .object text for one 286-byte intro record."""
    if len(raw) != 286:
        raise ValueError('Intro object record is not 286 bytes')
    words = struct.unpack('<143H', raw)
    rows = [','.join(f'0x{words[row * 3 + column]:04x}' for column in range(3)) for row in range(3)]
    selector = words[0x5a // 2]
    if selector % 2 or selector // 2 >= 240:
        raise ValueError('Intro object model selector is not a directory slot')
    def dword(offset):
        return words[offset // 2] | (words[offset // 2 + 1] << 16)
    state = [f'0x{offset:04x},0x{words[offset // 2]:04x}'
             for offset in range(0x20, 0x11e, 2)
             if offset not in (0x5a, 0x72) and words[offset // 2]]
    lines = ['[orientation]', 'rows = ' + rows[0], '    ' + rows[1], '    ' + rows[2], '',
             '[render]', f'word_12 = 0x{words[0x12 // 2]:04x}',
             f'offset_x = 0x{dword(0x14):08x}', f'offset_y = 0x{dword(0x18):08x}',
             f'offset_z = 0x{dword(0x1c):08x}', f'model_slot = {selector // 2}',
             f'phase = 0x{words[0x72 // 2]:04x}', '', '[state]']
    if state:
        lines.append('words = ' + state[0])
        lines.extend('    ' + row for row in state[1:])
    else:
        lines.append('words =')
    return '\n'.join(lines) + '\n'


def extract_intro_objects(blob, intro_dir):
    """Write the 23 intro object sources and check they compile back to the overlay bytes."""
    from intro_model_resources import object_document
    image = program_image(blob)
    intro_dir = Path(intro_dir)
    config = resource_sources.settings(intro_dir / 'objects.ini')
    for name in config.sections():
        entry = config[name]
        start, end = int(entry['start'], 0), int(entry['end'], 0)
        if end - start != 286 or end > len(image):
            raise ValueError(f'{name} leaves the intro overlay')
        path = intro_dir / entry['file']
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_intro_object_source(image[start:end]), encoding='ascii')
    catalog = intro_dir / 'objects.ini'
    decoded = object_document(resource_sources.settings(catalog), catalog, resource_sources.asset)
    for obj in decoded['objects']:
        packed = struct.pack('<143H', *obj['words'])
        if packed != image[obj['start']:obj['end']]:
            raise ValueError(f'{obj["name"]} does not round-trip')
    return len(decoded['objects'])


def escape_text(data):
    """Encode string-directory bytes the way strings.tsv stores them."""
    pieces = []
    for byte in data:
        if byte == 92:
            pieces.append('\\\\')
        elif 32 <= byte < 127:
            pieces.append(chr(byte))
        else:
            pieces.append(f'\\x{byte:02X}')
    return ''.join(pieces)


def extract_text_catalog(image, catalog_dir):
    """Pair the committed identifiers with the overlay's NUL-terminated strings."""
    catalog_dir = Path(catalog_dir)
    config = resource_sources.settings(catalog_dir / 'catalog.ini')['strings']
    start, end = int(config['directory_offset'], 0), int(config['end_offset'], 0)
    count = int(config['count'])
    names = (catalog_dir / 'identifiers.txt').read_text(encoding='ascii').splitlines()
    module = catalog_dir.name
    if len(names) != count or not start < end <= len(image):
        raise ValueError(f'{module} text does not match its catalog')
    block = image[start:end]
    if len(block) < 2 * count:
        raise ValueError(f'{module} text directory leaves the overlay')
    strings = []
    cursor = 2 * count
    for index in range(count):
        relative = struct.unpack_from('<H', block, 2 * index)[0]
        if relative != cursor or relative >= len(block) or 0 not in block[relative:]:
            raise ValueError(f'{module} text directory is not contiguous')
        stop = block.index(0, relative)
        strings.append(block[relative:stop])
        cursor = stop + 1
    if cursor != len(block):
        raise ValueError(f'{module} text does not fill its catalog span')
    lines = [f'{name}\t{escape_text(blob)}' for name, blob in zip(names, strings)]
    (catalog_dir / 'strings.tsv').write_text('\n'.join(lines) + '\n', encoding='ascii', newline='\n')
    return count


def extract_setup_messages(blob, setup_dir):
    """Write the seven setup-check strings. Each message ends at a DOS '$'."""
    image = program_image(blob)
    cursor = SETUP_TEXT
    messages = {}
    for name in SETUP_MESSAGES:
        if cursor >= len(image) or 36 not in image[cursor:]:
            raise ValueError('EL2SETUP.OVL does not contain the setup messages')
        end = image.index(36, cursor)
        messages[name] = image[cursor:end].decode('ascii')
        cursor = end + 1
    if cursor >= len(image) or 0 not in image[cursor:]:
        raise ValueError('EL2SETUP.OVL does not contain the setup messages')
    device = image[cursor:image.index(0, cursor)]
    messages['ems_device_name'] = device.decode('ascii')
    target = Path(setup_dir) / 'messages.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(messages, indent=2) + '\n', encoding='ascii', newline='\n')
    return len(messages)


def extract_density(blob, galaxy_dir):
    """Write the 128-by-129 galaxy grid from its overlay span."""
    import galaxy_density
    galaxy_dir = Path(galaxy_dir)
    config = resource_sources.settings(galaxy_dir / 'tables.ini')['galaxy_density_data']
    start, end = int(config['start'], 0), int(config['end'], 0)
    width, height = int(config['width']), int(config['height'])
    image = program_image(blob)
    if end - start != width * height or end > len(image):
        raise ValueError('EL2M19.OVL does not contain the galaxy density grid')
    galaxy_density.write_pgm(galaxy_dir / 'density.pgm', image[start:end], width, height)
    if galaxy_density.read_pgm(galaxy_dir / 'density.pgm', width, height) != image[start:end]:
        raise ValueError('EL2M19.OVL density grid did not round-trip')
    return width * height


def extract_adlib(blob, adlib_dir):
    """Write the editable AdLib bank and pitch curves from EL2ADLIB.OVL."""
    from adlib_resource_sources import write_bank
    write_bank(program_image(blob), adlib_dir)
    return 1


def extract_music(blob, name, music_dir):
    document = music_resources.export(blob, name)
    encoded = music_resources.encode(document)
    if encoded != blob:
        raise ValueError(f'{name} is not a Frontier MIDI score')
    target = Path(music_dir) / (Path(name).stem + '.mid')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(encoded)
    return target.name


def extract(files, data_root=DATA):
    """Write every present copyrighted asset. Raise if the install is incomplete."""
    absent = missing_assets(files)
    if absent:
        raise ValueError('Frontier install is missing ' + ', '.join(absent))
    data_root = Path(data_root)
    glyphs = extract_mcga(files['el2mcga3.ovl'], data_root / 'video')
    colors = extract_composite_palette(files['el2m13.ovl'], data_root / 'palettes')
    sprites = extract_sprites(files['el2g.grf'], data_root / 'graphics', data_root / 'video')
    portraits = 0
    if (data_root / 'composites').is_dir() and (data_root / 'palettes' / 'composites.ini').is_file():
        portraits = sprite_previews.refresh(
            graphics=data_root / 'graphics' / 'el2g.ini',
            composites=data_root / 'composites',
            palette=data_root / 'palettes' / 'composites.gpl',
            view_path=data_root / 'palettes' / 'composites.ini')
    if (data_root / 'maps' / 'hierarchy_sprite_selectors.csv').is_file() and \
            (data_root / 'maps' / 'system_view_palette.gpl').is_file():
        sprite_previews.refresh_maps(
            graphics=data_root / 'graphics' / 'el2g.ini',
            selectors=data_root / 'maps' / 'hierarchy_sprite_selectors.csv',
            palette=data_root / 'maps' / 'system_view_palette.gpl')
    image = program_image(files['frontier.exe'])
    main = extract_model_bank(image, data_root / 'models' / 'frontier' / 'models.ini')
    auxiliary = extract_model_bank(image, data_root / 'models' / 'frontier_auxiliary' / 'models.ini')
    intro_image = program_image(files['el2m08.ovl'])
    intro = extract_model_bank(intro_image, data_root / 'intro' / 'models.ini')
    intro_objects = extract_intro_objects(files['el2m08.ovl'], data_root / 'intro')
    music = [extract_music(files[f'{name}.rol'], f'{name}.rol', data_root / 'music') for name in MUSIC]
    prose = [extract_text_catalog(program_image(files[filename]), data_root / 'text' / module)
             for module, filename in PROSE]
    density = extract_density(files['el2m19.ovl'], data_root / 'galaxy')
    extract_adlib(files['el2adlib.ovl'], data_root / 'adlib')
    setup = extract_setup_messages(files['el2setup.ovl'], data_root / 'setup')
    return {'sprites': sprites, 'glyphs': glyphs, 'palette_colors': colors, 'portrait_previews': portraits,
            'main_models': main, 'auxiliary_models': auxiliary, 'intro_models': intro,
            'intro_objects': intro_objects, 'music': music, 'text_entries': sum(prose),
            'density_samples': density, 'setup_messages': setup}


def unpack_game(source, data_root=DATA):
    """Check the install, create authored files, then decode the binaries."""
    files = load_binaries(source)
    check_install(files)
    seed_authored(data_root)
    return extract(files, data_root)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='zip of Frontier binaries, or a folder of those files')
    parser.add_argument('--data', type=Path, default=DATA, help='data directory to write (default: data/)')
    args = parser.parse_args(argv)
    try:
        result = unpack_game(args.source, args.data)
    except (InstallError, ValueError) as error:
        print(error if isinstance(error, InstallError) else f'Unable to extract this Frontier install: {error}',
              file=sys.stderr)
        return 2
    print(f'Extracted {result["sprites"]} sprites, {result["glyphs"]} glyphs, '
          f'{result["palette_colors"]} portrait colors, '
          f'{result["main_models"] + result["auxiliary_models"] + result["intro_models"]} models, '
          f'{result["text_entries"]} text entries, {result["setup_messages"]} setup messages, '
          f'and {len(result["music"])} scores into {args.data}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

"""Build the Win32 native model viewer from editable resources, without assembly."""
from compiler import windows_executable_inputs, zig
import argparse
import sys
import struct
import json
import shutil
import subprocess
from pathlib import Path
from build_c_frontier import ROOT, build_library
from resource_sources import document, read_font, read_png, read_palette, settings
from graphics_resources import compress


# Text banks keep the overlay slots the session selects.
TEXT_SLOTS = {'systems': 0, 'chart': 2, 'flight': 3, 'market': 7, 'intro': 8,
              'names': 9, 'commander': 10, 'status': 14, 'offences': 15,
              'mining': 17, 'interaction': 18}


def compile_assets(data_root=None):
    """Bytes the game used to compile in. The dat packer stores these records."""
    data_root = ROOT / 'data' if data_root is None else Path(data_root)
    banks = [document('main_models', data_root / 'models' / name / 'models.ini')
             for name in ('frontier', 'frontier_auxiliary')]
    compiled = {'banks': [], 'models': [], 'texts': [], 'graphics': []}
    for b, bank in enumerate(banks):
        image = bytearray(bank['file_end'] - bank['start'])
        struct.pack_into('<H', image, 0, bank['end'] - bank['start'])
        offsets = {m['name']: m['start'] - bank['start'] for m in bank['models']}
        for i, name in enumerate(bank['directory']):
            struct.pack_into('<H', image, bank['directory_offset'] - bank['start'] + 2 * i,
                             offsets[name] - (bank['directory_offset'] - bank['start']) if name else 0)
        packed = {}
        if len(bank['directory']) > 240:
            raise ValueError(f'model bank {b} has more than 240 directory slots')
        for m in bank['models']:
            v, c, e, p = m['vertices'], m['classification'], m['extension'], m['program']
            vo, co = 16 + len(e), 16 + len(e) + len(v)
            po = co + len(c)
            data = struct.pack('<8H', po, vo, len(v) * 16, 0 if m['absent_classification'] else co,
                               0 if m['absent_classification'] else len(c) + 4, *m['header']) + bytes(e + v + c + p)
            offset = m['start'] - bank['start']
            if offset == 0 or offset > 0xFFFF:
                raise ValueError(f'{m["name"]} has no address in the model bank')
            image[offset:offset + len(data)] = data
            packed[m['name']] = (offset, data)
        for slot, name in enumerate(bank['directory']):
            if not name:
                continue
            offset, data = packed[name]
            compiled['models'].append({'bank': b, 'slot': slot, 'name': name, 'offset': offset, 'data': data})
        image[bank['end'] - bank['start']:] = bytes(bank['trailer_bytes'])
        compiled['banks'].append(bytes(image))
    font = read_font(data_root / 'video/font.bdf')
    compiled['font'] = bytes(x for g in font for x in [*(int(r, 2) for r in g['bitmap_rows']), g['advance']])
    for catalog in sorted((data_root / 'text').glob('*/catalog.ini')):
        text = document('strings', catalog)
        payloads = [bytes(e['bytes']) + b'\0' for e in text['entries']]
        at, offsets = len(payloads) * 2, []
        for payload in payloads:
            offsets.append(at)
            at += len(payload)
        name = text['module']
        if name == 'frontier':
            kind, slot, mask = 1, 0, 0
        elif name in TEXT_SLOTS:
            kind, slot, mask = 2, TEXT_SLOTS[name], 511
        else:
            raise ValueError(f'No text slot for {name}')
        compiled['texts'].append({'name': name, 'entries': len(payloads), 'kind': kind, 'slot': slot, 'mask': mask,
                                  'data': struct.pack('<' + 'H' * len(offsets), *offsets) + b''.join(payloads)})
    greetings = [(data_root / 'main/frontier' / f'greeting_{name}.txt').read_text().strip().encode('ascii') + b'\0'
                 for name in ('one', 'two')]
    compiled['builtin'] = struct.pack('<HH', 4, 4 + len(greetings[0])) + b''.join(greetings)
    widths = [int(x) for x in (data_root / 'main/frontier/glyph_widths.csv').read_text().replace('\n', ',').split(',') if x.strip()]
    compiled['widths'] = bytes(widths + [0] * (256 - len(widths)))
    compiled['palette'] = bytes(c for rgb in read_palette(data_root / 'video/default.gpl') for c in rgb)
    graphics = settings(data_root / 'graphics/el2g.ini')
    for section in graphics.sections():
        entry = graphics[section]
        ident = int(section.split()[1])
        width, height, rows = read_png(data_root / 'graphics' / entry['image'])
        pixels = bytes.fromhex(''.join(rows))
        packed = compress(pixels)
        compressed = len(packed) < len(pixels)
        data = struct.pack('<BBHhh', (entry.getint('flags') & ~2) | (2 if compressed else 0), height, width,
                           entry.getint('offset_x'), entry.getint('offset_y')) + (packed if compressed else pixels)
        if len(data) > 61438:
            raise ValueError(f'Graphic {ident} exceeds the native cache arena')
        compiled['graphics'].append((ident, data))
    return compiled


def assets(directory):
    compiled = compile_assets()
    lines = []

    def array(name, data):
        lines.append(f'static const uint8_t {name}[] = {{')
        for at in range(0, len(data), 24):
            lines.append(','.join(map(str, data[at:at + 24])) + ',')
        lines.append('};')

    seen = set()
    for row in compiled['models']:
        if row['name'] in seen:
            continue
        seen.add(row['name'])
        array(row['name'], row['data'])
    for b, image in enumerate(compiled['banks']):
        array(f'native_model_bank_{b}', image)
    array('native_font', compiled['font'])
    for text in compiled['texts']:
        array('text_' + text['name'], text['data'])
    array('text_builtin', compiled['builtin'])
    array('text_widths', compiled['widths'])
    array('native_palette', compiled['palette'])
    for ident, data in compiled['graphics']:
        array(f'graphic_{ident}', data)
    lines.append('static void native_resources(fe2_render_runtime_resources *r, fe2_graphics_cache *g) {')
    lines.append('r->font = native_font; r->glyphs = sizeof(native_font)/10;')
    for b in range(len(compiled['banks'])):
        lines.append(f'r->model_banks[{b}].data=native_model_bank_{b}; r->model_banks[{b}].count=sizeof(native_model_bank_{b});')
        for row in compiled['models']:
            if row['bank'] == b:
                lines.append(f'r->models[{b}][{row["slot"]}].data={row["name"]}; r->models[{b}][{row["slot"]}].count=sizeof({row["name"]}); r->model_offsets[{b}][{row["slot"]}]={row["offset"]};')
    lines.append('r->text.builtin.bytes.data=text_builtin; r->text.builtin.bytes.count=sizeof(text_builtin); r->text.builtin.entries=2;')
    lines.append('r->text.widths=text_widths;')
    for text in compiled['texts']:
        if text['kind'] == 1:
            target = 'external'
        else:
            target = f'overlay[{text["slot"]}]'
        lines.append(f'r->text.{target}.bytes.data=text_{text["name"]}; r->text.{target}.bytes.count=sizeof(text_{text["name"]}); r->text.{target}.entries={text["entries"]};')
        if text['kind'] == 2:
            lines.append(f'r->text.overlay_mask[{text["slot"]}]={text["mask"]};')
    for ident, _data in compiled['graphics']:
        lines.append(f'{{ fe2_render_bytes b={{graphic_{ident},sizeof(graphic_{ident})}}; if(g) (void)fe2_graphics_cache_bind(g,{ident},b); }}')
    lines.append('}')
    (directory / 'native_assets.h').write_text('\n'.join(lines) + '\n', encoding='ascii')
    return compiled


# The host is Win32: other platforms cross-compile it.
TARGET = None if sys.platform == 'win32' else 'x86_64-windows-gnu'


def build(directory, compiler, session=False):
    directory.mkdir(parents=True, exist_ok=True); assets(directory)
    shutil.copyfile(ROOT / 'LICENSE', directory / 'LICENSE.txt')
    library = build_library(directory / 'render', compiler, 'render', TARGET)
    session_library = build_library(directory / 'session', compiler, 'session', TARGET)
    exe = directory / ('fe2-native-session.exe' if session else 'fe2-native-viewer.exe')
    subprocess.run([str(compiler), 'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        *(['-target', TARGET] if TARGET else []), '-I', str(directory), '-I', str(ROOT / 'src/game'), '-I', str(ROOT / 'src/engine'),
        '-DFE2_NATIVE_SESSION=' + ('1' if session else '0'),
        str(ROOT / 'src/engine/host/viewer.c'), str(library), str(session_library),
        *windows_executable_inputs(compiler, directory, TARGET),
        '-lgdi32', '-luser32', '-o', str(exe)], check=True)
    (directory / 'viewer-manifest.json').write_text(json.dumps({
        'sources': ['src/engine/host/viewer.c'], 'executable': str(exe)
    }, indent=2) + '\n', encoding='utf-8')
    return exe


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=ROOT / 'build/native-viewer')
    p.add_argument('--zig', type=Path, default=zig())
    a = p.parse_args(); print(build(a.output.resolve(), a.zig.resolve()))

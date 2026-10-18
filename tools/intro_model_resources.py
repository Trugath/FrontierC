"""Compile intro model headers, encoded vertices and renderer programs via JSON.

Model names describe directory slots, not inferred object identities. Relative
header pointers, buffer sizes and directory offsets come from native arrays.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _values(text, width):
    values = [int(token, 0) for token in text.replace(',', ' ').split()]
    if any(not 0 <= value < 1 << width for value in values):
        raise ValueError('Model element exceeds its declared width')
    return values


def document(config, source, asset):
    from resource_sources import settings
    bank = config['bank']
    start = int(bank['start'], 0)
    directory_offset = int(bank['directory_offset'], 0)
    count = bank.getint('directory_count')
    end = int(bank['end'], 0)
    if (bank['module'] != 'intro' or count != 240 or
            directory_offset != start + 2 or not start < end <= 65536):
        raise ValueError('Invalid intro model bank layout')
    directory = []
    for line in asset(source, bank['directory']).read_text(encoding='ascii').splitlines():
        slot, name = line.split('\t')
        if int(slot) != len(directory):
            raise ValueError('Model slots must be consecutive and ordered')
        directory.append(None if name == '-' else name)
    if len(directory) != count:
        raise ValueError('Model directory count changed')
    models = []
    position = directory_offset + 2 * count
    for name in config.sections():
        if name == 'bank':
            continue
        if not re.fullmatch(r'intro_model_[0-9]{3}', name):
            raise ValueError('Invalid model name')
        entry = config[name]
        model = settings(asset(source, entry['file']))
        if set(model.sections()) != {'header', 'extension', 'vertices', 'classification', 'program'}:
            raise ValueError('Invalid model sections')
        for section, key in [('extension', 'bytes'), ('vertices', 'records'),
                             ('classification', 'records')]:
            if set(model[section]) != {key}:
                raise ValueError('Invalid model payload fields')
        keys = ['scale_shift', 'reserved_0c', 'culling_radius']
        if set(model['header']) != set(keys):
            raise ValueError('Invalid model header fields')
        header = [int(model['header'][key], 0) for key in keys]
        if any(not 0 <= value <= 65535 for value in header):
            raise ValueError('Model header word out of range')
        extension = _values(model['extension']['bytes'], 8)
        def records(section):
            result = []
            for row in model[section]['records'].splitlines():
                if not row.strip():
                    continue
                values = _values(row, 8)
                if len(values) != 4:
                    raise ValueError('Expected a four-byte encoded vector record')
                result.extend(values)
            return result
        vertices = records('vertices')
        classification = records('classification')
        from model_render_programs import compile_source
        if set(model['program']) == {'instructions'}:
            program = list(compile_source(model['program']['instructions'],
                           lambda name: asset(source, name).read_text(encoding='ascii')))
        elif set(model['program']) == {'bytes'}:
            program = _values(model['program']['bytes'], 8)
        else:
            raise ValueError('Invalid model renderer source fields')
        vertex_offset = 16 + len(extension)
        classification_offset = vertex_offset + len(vertices)
        program_offset = classification_offset + len(classification)
        size = program_offset + len(program)
        if len(vertices) * 16 > 65535 or len(classification) + 4 > 65535:
            raise ValueError('Model working buffer size exceeds a word')
        model_start, model_end = int(entry['start'], 0), int(entry['end'], 0)
        if model_start != position or model_end != position + size or model_end > end:
            raise ValueError(f'Model layout changed: {name}')
        models.append({'name': name, 'start': model_start, 'end': model_end,
                       'header': header, 'extension': extension,
                       'vertices': vertices, 'classification': classification, 'program': program})
        position = model_end
    if position != end or set(filter(None, directory)) != {m['name'] for m in models}:
        raise ValueError('Model bank has missing or unreferenced models')
    return {'format': 'fe2-intro-models-v1', 'start': start, 'end': end,
            'directory_offset': directory_offset, 'directory': directory, 'models': models}


def object_document(config, source, asset):
    """Compile explicit render fields and sparse unknown state words."""
    from resource_sources import settings
    objects = []
    for name in config.sections():
        if not re.fullmatch(r'initial_scene_object|intro_scene_object_[0-9]{2}', name):
            raise ValueError('Invalid intro object name')
        entry = config[name]
        start, end = int(entry['start'], 0), int(entry['end'], 0)
        if not 0 <= start < end <= 65536 or end - start != 286:
            raise ValueError('Intro render objects must occupy 286 bytes')
        model = settings(asset(source, entry['file']))
        if set(model.sections()) != {'orientation', 'render', 'state'}:
            raise ValueError('Invalid intro object sections')
        if set(model['orientation']) != {'rows'} or set(model['state']) != {'words'}:
            raise ValueError('Invalid intro object payload fields')
        rows = [row for row in model['orientation']['rows'].splitlines() if row.strip()]
        orientation = [_values(row, 16) for row in rows]
        if len(orientation) != 3 or any(len(row) != 3 for row in orientation):
            raise ValueError('Expected three rows of three orientation words')
        render = model['render']
        if set(render) != {'word_12', 'offset_x', 'offset_y', 'offset_z', 'model_slot', 'phase'}:
            raise ValueError('Invalid intro render fields')
        words = [0] * 143
        words[:9] = [value for row in orientation for value in row]
        for key, offset in [('word_12', 0x12), ('phase', 0x72)]:
            value = int(render[key], 0)
            if not 0 <= value <= 65535:
                raise ValueError('Intro object word exceeds its width')
            words[offset // 2] = value
        slot = int(render['model_slot'], 0)
        if not 0 <= slot < 240:
            raise ValueError('Intro model slot outside the directory')
        words[0x5a // 2] = slot * 2
        for key, offset in [('offset_x', 0x14), ('offset_y', 0x18), ('offset_z', 0x1c)]:
            value = int(render[key], 0)
            if not 0 <= value <= 0xffffffff:
                raise ValueError('Intro render offset exceeds its width')
            words[offset // 2:offset // 2 + 2] = [value & 65535, value >> 16]
        seen = set()
        for row in model['state']['words'].splitlines():
            if not row.strip():
                continue
            values = _values(row, 16)
            if len(values) != 2:
                raise ValueError('Expected state word offset and value')
            offset, value = values
            if offset % 2 or not 0x20 <= offset < 0x11e or offset in (0x5a, 0x72) or offset in seen:
                raise ValueError('Invalid, duplicated or reserved object state offset')
            seen.add(offset)
            words[offset // 2] = value
        objects.append({'name': name, 'start': start, 'end': end, 'words': words})
    expected = {'initial_scene_object'} | {f'intro_scene_object_{i:02d}' for i in range(22)}
    if {obj['name'] for obj in objects} != expected:
        raise ValueError('Intro object catalog must define all 23 objects')
    ranges = sorted((obj['start'], obj['end']) for obj in objects)
    if any(left[1] > right[0] for left, right in zip(ranges, ranges[1:])):
        raise ValueError('Intro object ranges overlap')
    return {'format': 'fe2-intro-objects-v1', 'objects': objects}

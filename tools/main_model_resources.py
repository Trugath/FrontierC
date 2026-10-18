"""Compile Main model headers, encoded vertices and renderer programs via JSON.

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
    count = bank.getint('directory_count', fallback=240)
    prefix = bank.get('prefix', 'main_model')
    if prefix not in {'main_model', 'auxiliary_model'}:
        raise ValueError('Invalid model bank prefix')
    if bank['module'] != 'frontier' or count != 240:
        raise ValueError('Invalid main model bank layout')
    # Addresses are the original executable image. A mod omits them and the
    # packer lays the bank out from the directory order.
    explicit = 'start' in bank
    if explicit:
        start = int(bank['start'], 0)
        directory_offset = int(bank['directory_offset'], 0)
        end = int(bank['end'], 0)
        if directory_offset != start + 2 or not start < end <= 0x100000:
            raise ValueError('Invalid main model bank layout')
    else:
        start, directory_offset, end = 0, 2, None
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
        if name in {'bank', 'trailer'}:
            continue
        if not re.fullmatch(prefix + r'_[0-9]{3}', name):
            raise ValueError('Invalid model name')
        entry = config[name]
        model = settings(asset(source, entry['file']))
        required_sections = {'header', 'extension', 'vertices', 'classification', 'program'}
        if set(model.sections()) not in (required_sections, required_sections | {'contact'},
                                        required_sections | {'contact', 'contact_offsets'}):
            raise ValueError('Invalid model sections')
        for section, key in [('vertices', 'records'),
                             ('classification', 'records')]:
            if set(model[section]) != {key}:
                raise ValueError('Invalid model payload fields')
        keys = ['scale_shift', 'reserved_0c', 'culling_radius']
        if set(model['header']) != set(keys):
            raise ValueError('Invalid model header fields')
        header = [int(model['header'][key], 0) for key in keys]
        if any(not 0 <= value <= 65535 for value in header):
            raise ValueError('Model header word out of range')
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
        from model_instance_directories import compile_extension, program_entry_offsets
        extension = compile_extension(model['extension'], len(vertices), len(classification))
        if len(extension) not in ({0, 14} if prefix == 'main_model' else {0, 118, 198}):
            raise ValueError('Unexpected model header extension size')
        from model_render_programs import compile_source
        names = Path(source).resolve().parents[2] / 'text' / 'frontier' / 'identifiers.txt'
        if set(model['program']) == {'instructions'}:
            program = list(compile_source(model['program']['instructions'],
                           lambda name: asset(source, name).read_text(encoding='ascii'),
                           names if names.is_file() else None))
        elif set(model['program']) == {'bytes'}:
            program = _values(model['program']['bytes'], 8)
        else:
            raise ValueError('Invalid model renderer source fields')
        if any(not 0 <= offset < len(program) for offset in program_entry_offsets(model)):
            raise ValueError('Instance directory entry leaves renderer program')
        contact_position = None
        if 'contact' in model:
            from model_contact_resources import compile_source
            if set(model['contact']) != {'file', 'program_offset'}:
                raise ValueError('Invalid contact source fields')
            contact_position = int(model['contact']['program_offset'], 0)
            if not 0 <= contact_position <= len(program):
                raise ValueError('Contact insertion leaves renderer program')
            if 'contact_offsets' in model:
                from model_contact_offsets import compile_source as compile_offsets
                offsets = model['contact_offsets']
                if set(offsets) != {'file', 'program_offset'}:
                    raise ValueError('Invalid contact offset source fields')
                if int(offsets['program_offset'], 0) != contact_position:
                    raise ValueError('Contact offsets must immediately precede the contact program')
                if contact_position < 2 or program[contact_position - 2:contact_position] != [0, 0]:
                    raise ValueError('Contact offset walk requires the shared preceding zero word')
                pairs = compile_offsets(asset(source, offsets['file']).read_text(encoding='ascii'))
                program[contact_position:contact_position] = pairs
                contact_position += len(pairs)
            contact = compile_source(asset(source, model['contact']['file']).read_text(encoding='ascii'))
            program[contact_position:contact_position] = contact
        vertex_offset = 16 + len(extension)
        classification_offset = vertex_offset + len(vertices)
        program_offset = classification_offset + len(classification)
        if contact_position is not None:
            if len(extension) < 12 or extension[10] + 256 * extension[11] != program_offset + contact_position:
                raise ValueError('Contact source does not match model header pointer')
        size = program_offset + len(program)
        if len(vertices) * 16 > 65535 or len(classification) + 4 > 65535:
            raise ValueError('Model working buffer size exceeds a word')
        if 'start' in entry:
            model_start, model_end = int(entry['start'], 0), int(entry['end'], 0)
            if model_start != position or model_end != position + size or (end is not None and model_end > end):
                raise ValueError(f'Model layout changed: {name}')
        else:
            model_start, model_end = position, position + size
            if end is not None and model_end > end:
                raise ValueError(f'Model layout changed: {name}')
        absent_classification = entry.getboolean('absent_classification', fallback=False)
        if absent_classification and classification:
            raise ValueError('Absent classification has records')
        models.append({'absent_classification': absent_classification, 'name': name, 'start': model_start, 'end': model_end,
                       'header': header, 'extension': extension,
                       'vertices': vertices, 'classification': classification, 'program': program})
        position = model_end
    named = sum(name is not None for name in directory)
    if bank.get('model_count'):
        population = bank.getint('model_count')
    else:
        population = 230 if explicit and prefix == 'main_model' else named
    if len(models) != population or directory.count(None) != count - population:
        raise ValueError('Main model directory population changed')
    if end is None:
        end = position
    if not 0 <= end - start <= 65535:
        raise ValueError('Main model bank size exceeds a word')
    if position != end or set(filter(None, directory)) != {m['name'] for m in models}:
        raise ValueError('Model bank has missing or unreferenced models')
    if config.has_section('trailer'):
        trailer = _values(config['trailer']['bytes'], 8) if 'bytes' in config['trailer'] else list(config['trailer']['text'].encode('ascii'))
        file_end = int(config['trailer']['end'], 0) if 'end' in config['trailer'] else end + len(trailer)
    else:
        trailer, file_end = [], end
    return {'format': 'fe2-main-models-v1', 'start': start, 'end': end,
            'directory_offset': directory_offset, 'directory': directory, 'models': models,
            'prefix': prefix, 'trailer_bytes': trailer, 'file_end': file_end}


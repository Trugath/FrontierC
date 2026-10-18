"""Compile typed CSV/GPL/PGM module tables through intermediate JSON."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def document(config, source, asset, read_palette):
    tables = []
    for section in config.sections():
        entry = config[section]
        kind = entry['type']
        if kind == 'escaped_text':
            from string_resources import decode
            text = asset(source, entry['file']).read_text(encoding='ascii')
            if text.endswith('\n'):
                text = text[:-1]
            values = decode(text)
        elif kind == 'fixed_text':
            slot_bytes = entry.getint('slot_bytes')
            if slot_bytes < 1:
                raise ValueError('Fixed text slot size must be positive')
            values = []
            for text in asset(source, entry['file']).read_text(encoding='ascii').splitlines():
                if not text or any(not 32 <= ord(char) < 127 for char in text):
                    raise ValueError('Fixed text entries must be printable ASCII')
                raw = text.encode('ascii')
                if len(raw) >= slot_bytes:
                    raise ValueError('Fixed text must leave room for NUL terminator')
                values.extend(raw + bytes(slot_bytes - len(raw)))
        elif kind == 'record':
            fields = [field.strip() for field in entry['fields'].split(',')]
            sizes = {'u8': 1, 'u16': 2, 'u32': 4, 'text': 2, 'model': 2}
            if not fields or any(field not in sizes and not re.fullmatch(r'ascii[1-9][0-9]*', field) for field in fields):
                raise ValueError('Unsupported record field type')
            values = []
            with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
                reader = csv.reader(stream)
                if 'columns' in entry:
                    columns = [name.strip() for name in entry['columns'].split(',')]
                    if len(columns) != len(fields) or len(set(columns)) != len(columns):
                        raise ValueError('Record columns must be unique and match field types')
                    if next(reader, None) != columns:
                        raise ValueError('Unexpected record column names')
                for row in reader:
                    if len(row) != len(fields):
                        raise ValueError('Record field count changed')
                    for raw, field in zip(row, fields):
                        if field == 'text':
                            from text_references import parse_selector
                            value = parse_selector(raw)
                            values.extend(value.to_bytes(2, 'little'))
                            continue
                        if field == 'model':
                            from model_names import parse_model
                            value = parse_model(raw)
                            values.extend(value.to_bytes(2, 'little'))
                            continue
                        if field.startswith('ascii'):
                            size = int(field[5:])
                            encoded = raw.encode('ascii')
                            if not encoded or len(encoded) >= size or any(not 32 <= v < 127 for v in encoded):
                                raise ValueError('Record text must be printable ASCII with room for NUL')
                            values.extend(encoded + bytes(size - len(encoded)))
                            continue
                        value = int(raw.strip(), 0)
                        size = sizes[field]
                        if not 0 <= value < 1 << (size * 8):
                            raise ValueError('Record field exceeds its type')
                        values.extend(value.to_bytes(size, 'little'))
        elif kind == 'station_layout_words':
            from station_layout import recovery_table
            values = recovery_table(asset(source, entry['file']), section)
        elif kind in ('cockpit_words', 'cockpit_bytes'):
            from cockpit_layout import table
            values = table(asset(source, entry['file']), section)
        elif kind in ('composite_coordinates', 'composite_sprite', 'composite_palette'):
            from composite_authoring import recovery_values
            values = recovery_values(source.parent, entry, kind)
        elif kind in ('commodity_parameters', 'mining_yields', 'mining_categories'):
            import gameplay_data
            values = getattr(gameplay_data, kind)(asset(source, entry['file']))
        elif kind == 'allegiance_systems':
            from system_names import allegiance_candidates
            values = allegiance_candidates(asset(source, entry['file']))
        elif kind in ('catalog_coordinates', 'catalog_coordinate_tail', 'catalog_ranges',
                      'catalog_records', 'catalog_identifiers', 'catalog_draw_kinds',
                      'map_annotations', 'map_starts', 'candidate_offsets'):
            from map_authoring import values as map_values
            values = map_values(kind, asset(source, entry['file']))
        elif kind == 'render_material':
            from render_material_resources import read_materials
            values = read_materials(asset(source, entry['file']))
        elif kind == 'object_record':
            from object_record import read_object
            values = list(read_object(asset(source, entry['file'])))
        elif kind == 'text_selector':
            from text_references import resolve
            reference = asset(source, entry['file']).read_text(encoding='ascii').strip()
            values = [resolve(reference, entry.get('catalogue'))]
        elif kind == 'text_list':
            from text_references import parse_selector
            with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
                values = [parse_selector(value) for row in csv.reader(stream) for value in row if value.strip()]
        elif kind == 'model_id':
            from model_names import parse_model
            with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
                values = [parse_model(value) for row in csv.reader(stream) for value in row if value.strip()]
        elif kind == 'sprite_id':
            from sprite_names import parse_sprite
            with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
                values = [parse_sprite(value) for row in csv.reader(stream) for value in row if value.strip()]
        elif kind == 'model_word_pairs':
            from model_names import parse_model
            with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
                reader = csv.reader(stream)
                if next(reader, None) != ['model', 'parameter']:
                    raise ValueError('Expected model,parameter columns')
                values = []
                for row in reader:
                    if len(row) != 2:
                        raise ValueError('Expected a model and a parameter')
                    values.extend((parse_model(row[0]), int(row[1], 0)))
        elif kind == 'variant_block':
            base, stride = int(entry['base'], 0), int(entry['stride'], 0)
            with asset(source, entry['file']).open(encoding='ascii', newline='') as stream:
                values = []
                for token in (value for row in csv.reader(stream) for value in row if value.strip()):
                    index = int(token, 0)
                    address = base + index * stride
                    if index < 0 or address > 0xffff:
                        raise ValueError(f'Variant block {token} is outside the table')
                    values.append(address)
        elif kind == 'system_template':
            from system_templates import encode
            values = encode(asset(source, entry['file']))
        elif kind == 'preference_bytes':
            from preference_authoring import preference_bytes
            values = preference_bytes(asset(source, entry['file']))
        elif kind == 'preference_widgets':
            from preference_authoring import widget_words
            values = widget_words(asset(source, entry['file']))
        elif kind == 'detail_presets':
            from preference_authoring import detail_presets
            values = detail_presets(asset(source, entry['file']))
        elif kind == 'filename_widgets':
            from preference_authoring import filename_words
            values = filename_words(asset(source, entry['file']))
        elif kind == 'offence_parameters':
            from offence_parameters import words
            values = words(asset(source, entry['file']))
        elif kind == 'passage_pairs':
            from economy_authoring import passage_pairs
            values = passage_pairs(asset(source, entry['file']))
        elif kind == 'remote_subjects':
            from economy_authoring import remote_subjects
            values = remote_subjects(asset(source, entry['file']))
        elif kind == 'reward_words':
            from economy_authoring import reward_words
            values = reward_words(asset(source, entry['file']))
        elif kind == 'sine_half_cycle':
            from math_samples import sine_half_cycle
            values = sine_half_cycle()
        elif kind == 'square_root_lookup':
            from math_samples import square_root_lookup
            values = square_root_lookup()
        elif kind == 'reciprocal_lookup':
            from math_samples import reciprocal_lookup
            values = reciprocal_lookup()
        elif kind == 'acos_q6':
            from math_samples import acos_signed_q6
            values = acos_signed_q6()
        elif kind == 'calendar_days':
            from calendar_authoring import calendar_days
            values = calendar_days(asset(source, entry['file']))
        elif kind == 'equipment_items':
            from equipment_authoring import image_bytes
            values = image_bytes(asset(source, entry['file']))
        elif kind == 'docking_slots':
            from docking_authoring import attachment_bytes
            values = attachment_bytes(asset(source, entry['file']), asset(source, entry['offers']))
        elif kind == 'category_offers':
            from docking_authoring import category_pair_words
            values = category_pair_words(asset(source, entry['file']))
        elif kind == 'command_slots':
            from docking_authoring import command_bytes
            values = command_bytes(asset(source, entry['file']))
        elif kind == 'control_slots':
            from docking_authoring import control_words
            values = control_words(asset(source, entry['file']))
        elif kind == 'status_widgets':
            from status_authoring import widget_words
            values = widget_words(asset(source, entry['file']))
        elif kind == 'q15_coefficients':
            from economy_authoring import q15_coefficients
            from system_names import population_labels
            index = entry['index']
            if index == 'strength':
                labels = [str(i) for i in range(8)]
            elif index == 'population':
                labels = population_labels()
            elif index == 'economic_factor':
                labels = [str(i) for i in range(8)]
            else:
                raise ValueError(f'Unknown Q15 index {index}')
            values = q15_coefficients(asset(source, entry['file']), index, labels)
        elif kind == 'pgm8':
            from galaxy_density import read_pgm
            values = list(read_pgm(asset(source, entry['file']),
                                   entry.getint('width'), entry.getint('height')))
        elif kind == 'rgb6':
            values = [v for rgb in read_palette(asset(source, entry['file'])) for v in rgb]
        else:
            if kind not in ('s8', 'u8', 'u16', 'u32'):
                raise ValueError('Unsupported table type')
            from resource_sources import csv_integers
            values = csv_integers(asset(source, entry['file']))
        if kind == 's8':
            if any(not -128 <= v <= 127 for v in values):
                raise ValueError('Signed byte table element exceeds its type')
            values = [v & 255 for v in values]
        width = {'s8':1, 'u8':1, 'pgm8':1, 'rgb6':1, 'record':1, 'fixed_text':1, 'escaped_text':1,
                 'u16':2, 'u32':4, 'allegiance_systems':4, 'render_material':2, 'commodity_parameters':2,
                 'catalog_coordinates':2, 'catalog_coordinate_tail':2, 'catalog_ranges':2,
                 'catalog_records':1, 'catalog_identifiers':2, 'catalog_draw_kinds':2,
                 'map_annotations':2, 'map_starts':2, 'candidate_offsets':2,
                 'mining_yields':2, 'mining_categories':1, 'cockpit_words':2, 'cockpit_bytes':1,
                 'station_layout_words':2, 'object_record':1, 'text_selector':2, 'text_list':2,
                 'model_id':2, 'sprite_id':2, 'model_word_pairs':2, 'variant_block':2,
                 'system_template':1, 'preference_bytes':1, 'preference_widgets':2,
                 'detail_presets':4, 'filename_widgets':2, 'offence_parameters':2,
                 'passage_pairs':2, 'remote_subjects':2, 'reward_words':2, 'q15_coefficients':2,
                 'sine_half_cycle':2, 'square_root_lookup':2, 'reciprocal_lookup':2, 'acos_q6':2,
                 'calendar_days':2, 'equipment_items':1, 'docking_slots':1, 'category_offers':2,
                 'command_slots':1, 'control_slots':2, 'status_widgets':2,
                 'composite_coordinates':1,
                 'composite_sprite':2, 'composite_palette':2}[kind]
        if any(type(v) is not int or not 0 <= v < 1 << (width * 8) for v in values):
            raise ValueError('Table element exceeds its type')
        label = section
        if not re.fullmatch(r'[a-z][a-z0-9_]*', label):
            raise ValueError('Invalid table label')
        start, end = int(entry['start'], 0), int(entry['end'], 0)
        if len(values) * width != end - start:
            raise ValueError(f'Table size changed: {label}')
        tables.append({'label':label, 'start':start, 'end':end, 'width':width, 'values':values})
    return {'format':'fe2-module-tables-v1', 'tables':tables}

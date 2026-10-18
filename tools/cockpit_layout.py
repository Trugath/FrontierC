"""Compile semantic cockpit UI JSON into native layout data and recovery bytes."""
import configparser
import json
from pathlib import Path
from text_references import text_catalogues

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'data/cockpit/layout.json'
WEAPONS = ('dummy_mine', 'proximity_mine', 'homing_missile', 'smart_missile',
           'naval_missile', 'nuclear_missile')
LABELS = ('unprojected', 'upper_left', 'upper_right', 'lower_left', 'lower_right', 'unprojected_other')
COMMANDS = ('activate_selected', 'activate_ecm', 'escape_capsule', 'energy_bomb',
            'toggle_cloud_analysis', 'toggle_target_stats', 'ship_command')
DRIVES = ('none', 'class_1', 'class_2', 'class_3', 'class_4', 'class_5', 'class_6',
          'class_7', 'class_8', 'class_9', 'military_class_1', 'military_class_2',
          'military_class_3', 'military_class_4', 'interplanetary')


def fields(value, names):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f'Expected fields: {names}')
    return value


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'Expected integer in {low}..{high}')
    return value


def point(value):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError('Expected [x, y] point')
    return [integer(value[0], 0, 319), integer(value[1], 0, 199)]


def named(value, names):
    fields(value, names)
    return value


def sprite_catalogue():
    config = configparser.ConfigParser()
    config.read(ROOT / 'data/graphics/el2g.ini', encoding='ascii')
    result = {}
    for section in config.sections():
        if section.startswith('sprite '):
            name = Path(config[section]['image']).stem
            if name in result:
                raise ValueError('Sprite image stems must be unique')
            result[name] = int(section.split()[1])
    return result


def compile_document(doc):
    if isinstance(doc, dict) and '$schema' in doc:
        if not isinstance(doc['$schema'], str):
            raise ValueError('Schema reference must be a string')
        doc = {key: value for key, value in doc.items() if key != '$schema'}
    fields(doc, ('format', 'weapons', 'equipment_slots', 'commands', 'target_marker', 'target_labels', 'drive_labels'))
    if doc['format'] != 'frontier-cockpit-layout-v1':
        raise ValueError('Unknown cockpit layout format')
    sprites, texts = sprite_catalogue(), text_catalogues()
    named(doc['weapons'], WEAPONS)
    weapon_sprites = []
    for name in WEAPONS:
        record = fields(doc['weapons'][name], ('even_slot_sprite', 'odd_slot_sprite'))
        pair = []
        for key in ('even_slot_sprite', 'odd_slot_sprite'):
            if not isinstance(record[key], str) or record[key] not in sprites:
                raise ValueError('Unknown sprite image name')
            pair.append(sprites[record[key]])
        weapon_sprites.append(pair)
    if not isinstance(doc['equipment_slots'], list) or len(doc['equipment_slots']) != 8:
        raise ValueError('Expected eight equipment slot centres')
    slots = [point(value) for value in doc['equipment_slots']]
    named(doc['commands'], COMMANDS)
    commands = []
    for name in COMMANDS:
        record = fields(doc['commands'][name], ('key', 'bounds'))
        key = record['key']
        if not isinstance(key, str) or len(key) != 1 or not 32 <= ord(key) <= 126:
            raise ValueError('Command key must be one printable ASCII character')
        bounds = record['bounds']
        if not isinstance(bounds, list) or len(bounds) != 4:
            raise ValueError('Command bounds must be [left, top, right, bottom]')
        left, top = point(bounds[:2])
        right, bottom = integer(bounds[2], 1, 320), integer(bounds[3], 1, 200)
        if left >= right or top >= bottom:
            raise ValueError('Command bounds must have positive area')
        commands.extend((left, top, right, bottom, ord(key)))
    if len({commands[index] for index in range(4, len(commands), 5)}) != len(COMMANDS):
        raise ValueError('Command keys must be unique')
    marker = doc['target_marker']
    if not isinstance(marker, list) or len(marker) != 4:
        raise ValueError('Expected four target marker lines')
    marker_words = []
    for line in marker:
        fields(line, ('from', 'to'))
        for key in ('from', 'to'):
            pair = line[key]
            if not isinstance(pair, list) or len(pair) != 2:
                raise ValueError('Marker endpoints must be [x, y]')
            marker_words.extend(integer(value, -32768, 32767) & 65535 for value in pair)
    named(doc['target_labels'], LABELS)
    labels = []
    for name in LABELS:
        record = fields(doc['target_labels'][name], ('text', 'position'))
        if not isinstance(record['text'], str) or record['text'] not in texts:
            raise ValueError('Unknown target label text')
        labels.extend((texts[record['text']], *point(record['position'])))
    named(doc['drive_labels'], DRIVES)
    drive_labels = []
    for name in DRIVES:
        reference = doc['drive_labels'][name]
        if not isinstance(reference, str) or reference not in texts:
            raise ValueError('Unknown drive label text')
        drive_labels.append(texts[reference])
    # Preserve the recovered image's overlapping reads for kinds 6/7. These
    # words are coordinates, not real icon references. Native tables separate
    # the two fields so layout editing never requires editing packed offsets.
    legacy_layout = [65535] + [value for pair in weapon_sprites for value in pair] + [value for pair in slots for value in pair]
    return {'sprites': weapon_sprites + [slots[0], slots[1]], 'slots': slots,
            'commands': commands, 'regions': list(range(0x10, 0x18)) + commands[4::5],
            'markers': marker_words, 'labels': labels, 'drive_labels': drive_labels,
            'legacy_layout': legacy_layout}


def load(path=SOURCE):
    return compile_document(json.loads(Path(path).read_text(encoding='utf-8-sig')))


def table(path, name):
    data = load(path)
    return data[{'view_marker_coordinates': 'markers', 'cockpit_text_positions': 'labels',
                 'cockpit_resource_selectors': 'regions', 'view_command_rectangles': 'commands',
                 'cockpit_layout_words': 'legacy_layout', 'status_panel_words': 'drive_labels'}[name]]

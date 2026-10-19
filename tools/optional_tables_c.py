"""Compile the optional PC speaker, MIDI and historical setup resources."""
import csv
import json
from pathlib import Path
from music_resources import encode_track
from resource_sources import ROOT, document


def array(name, values, ctype='uint8_t'):
    return f'static const {ctype} {name}[] = {{' + ','.join(map(str, values)) + '};\n'


def _speaker_column(path, columns, field, count):
    with path.open(newline='', encoding='ascii') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(columns):
            raise ValueError(f'{path.name}: unexpected columns')
        values = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{path.name}: incomplete row')
            values.append(row[field].strip())
    if len(values) != count:
        raise ValueError(f'{path.name}: expected {count} rows')
    return values


def speaker(path):
    folder = ROOT / 'data/speaker'
    output = '/* Generated from data/speaker; do not edit. */\n'
    from speaker_scripts import load
    scripts = load(folder / 'scripts.json')
    effect_index = {name: index for index, (name, _) in enumerate(scripts['effects'])}
    effect_index['none'] = 255
    slots = _speaker_column(folder / 'game_effects.csv', ('game_effect', 'script'), 'game_effect', 50)
    named = _speaker_column(folder / 'game_effects.csv', ('game_effect', 'script'), 'script', 50)
    if slots != [str(index) for index in range(50)]:
        raise ValueError('speaker game effects must stay in call order')
    effects = []
    for script in named:
        if script not in effect_index:
            raise ValueError(f'unknown speaker script {script!r}')
        effects.append(effect_index[script])
    levels = [int(value, 10) for value in _speaker_column(
        folder / 'level_durations.csv', ('level', 'updates'), 'updates', 15)]
    notes = _speaker_column(folder / 'pitch_divisors.csv', ('note', 'pit_divisor'), 'note', 256)
    divisors = [int(value, 10) for value in _speaker_column(
        folder / 'pitch_divisors.csv', ('note', 'pit_divisor'), 'pit_divisor', 256)]
    if notes != [str(index) for index in range(256)]:
        raise ValueError('pitch notes must be 0..255 in order')
    if any(not 0 <= value <= 65535 for value in divisors) or any(not 0 <= value <= 255 for value in levels):
        raise ValueError('Invalid speaker table')
    output += array('game_effects', effects)
    output += array('level_durations', levels)
    output += array('divisors', divisors, 'uint16_t')
    for kind in ('effects', 'patterns'):
        names = []
        for index, (_, values) in enumerate(scripts[kind]):
            name = f'{kind}_{index}'
            output += array(name, values)
            names.append(name)
        output += f'static const uint8_t *const {kind}[] = {{' + ','.join(names) + '};\n'
    output += 'const fe2_speaker_sounds fe2_speaker_default_sounds = {game_effects, level_durations, divisors, effects, patterns};\n'
    # The migration fixture pins these bytes, including CRLF from the original Windows write.
    path.write_text(output, encoding='ascii', newline='\r\n')


def setup(path):
    names = {'dos_version_message': 'dos_message', 'cpu_required_message': 'cpu_message',
             'base_memory_message': 'base_memory_message', 'ems_missing_message': 'ems_missing_message',
             'ems_memory_message': 'ems_memory_message', 'sound_menu_message': 'sound_menu',
             'ems_device_name': 'ems_device'}
    messages_path = ROOT / 'data/setup/messages.json'
    if messages_path.is_file():
        messages = json.loads(messages_path.read_text())
        if set(messages) != set(names):
            raise ValueError('Setup message names changed')
    else:
        messages = {key: 'EMMXXXX0' if key == 'ems_device_name' else '.' for key in names}
    output = '/* Generated from data/setup/messages.json; do not edit. */\n'
    for key, suffix in names.items():
        text = messages[key]
        text.encode('ascii')
        output += f'const char fe2_setup_{suffix}[] = {json.dumps(text)};\n'
    path.write_text(output, encoding='ascii')


def roland_scores():
    """MIDI files the optional Roland checks embed. Copyrighted scores stay unpacked."""
    import configparser
    folder = ROOT / 'data/roland'
    config = configparser.ConfigParser()
    config.read(folder / 'effects.ini', encoding='ascii')
    paths = [(folder / config[section]['music']).resolve() for section in config.sections()]
    paths.extend(ROOT / 'data/music' / f'{name}.mid' for name in
                 ('hallking', 'elitet', 'babayaga', 'barem', 'jupalt', 'valkries', 'bluedan', 'ggofkiev', 'intro'))
    return paths


def roland(path):
    missing = [path for path in roland_scores() if not path.is_file()]
    if missing:
        raise FileNotFoundError(missing[0])
    effects = document('roland', ROOT / 'data/roland/effects.ini')['effects']
    encoded = []
    for effect in effects:
        data = encode_track(effect['events'])
        if not data.startswith(b'\x00') or effect['events'][0]['delta'] != 0:
            raise ValueError('Embedded effect requires a synthetic zero delta')
        encoded.append(data[1:])
    music = [(ROOT / 'data/music' / (name + '.mid')).read_bytes() for name in
             ('hallking', 'elitet', 'babayaga', 'barem', 'jupalt', 'valkries', 'bluedan', 'ggofkiev', 'intro')]
    output = '/* Generated from editable MIDI resources; do not edit. */\n'
    for kind, streams in [('effect', encoded), ('music', music)]:
        for index, data in enumerate(streams):
            output += array(f'{kind}_{index}', data)
        output += f'static const uint8_t *const {kind}_data[] = {{' + ','.join(f'{kind}_{i}' for i in range(len(streams))) + '};\n'
        output += array(kind + '_size', [len(data) for data in streams], 'size_t')
    path.write_text(output, encoding='ascii')

"""Commander preference bytes, checkbox widgets and the filename editor.

Preference order is the byte order copied into the commander. A checkbox's
row is that same index: the icon reads the byte, and the key is what the
click sends. `on` and `off` are the bytes 0xff and 0x00 that the toggles
invert.
"""
import csv
import re
from pathlib import Path

PREFERENCES = (
    'detail_low', 'detail_medium', 'detail_high', 'detail_very_high',
    'space_dust', 'background_stars', 'external_view_lock',
    'reverse_left_right', 'reverse_up_down', 'elite_controls',
    'targeting_tunnels', 'ship_targeting_tunnels', 'filename_extensions',
    'sound_effects', 'engine_sound', 'station_sound', 'wind_sound', 'icon_beeps',
    'music', 'hyperspace_music', 'docking_music', 'battle_music', 'continuous_music',
    'track_hall_of_the_mountain_king', 'track_frontier_main_theme', 'track_baba_yaga',
    'track_night_on_the_bare_mountain', 'track_frontier_second_theme',
    'track_ride_of_the_valkyries', 'track_blue_danube', 'track_great_gates_of_kiev',
    'reserved', 'reserved',
)
KEYS = {
    'detail_low': 0x81, 'detail_medium': 0x82, 'detail_high': 0x83, 'detail_very_high': 0x84,
    'space_dust': 0x85, 'background_stars': 0x86, 'external_view_lock': 0x87,
    'reverse_left_right': 0x88, 'reverse_up_down': 0x89, 'elite_controls': 0xa9,
    'targeting_tunnels': 0x9a, 'ship_targeting_tunnels': 0x9b, 'filename_extensions': 0x9c,
    'sound_effects': 0x8a, 'engine_sound': 0x8d, 'station_sound': 0xa8, 'wind_sound': 0xa4,
    'icon_beeps': 0x8e, 'music': 0x8b, 'hyperspace_music': 0xa5, 'docking_music': 0xa6,
    'battle_music': 0xa7, 'continuous_music': 0x8c,
    'track_hall_of_the_mountain_king': 0x91, 'track_frontier_main_theme': 0x92,
    'track_baba_yaga': 0x93, 'track_night_on_the_bare_mountain': 0x94,
    'track_frontier_second_theme': 0x95, 'track_ride_of_the_valkyries': 0x96,
    'track_blue_danube': 0x97, 'track_great_gates_of_kiev': 0x98,
}
FILENAME_KEYS = {'save_file': 0x0d, 'edit_name': 0xe0}
DETAIL = PREFERENCES[:4]


def _rows(path, columns):
    with Path(path).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(columns):
            raise ValueError(f'{Path(path).name}: unexpected columns')
        rows = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f'{Path(path).name}: incomplete row')
            rows.append({key: value.strip() for key, value in row.items()})
    return rows


def _coordinate(token, label):
    if not re.fullmatch(r'[0-9]+', token):
        raise ValueError(f'{label} must be a decimal coordinate')
    value = int(token, 10)
    if value > 65535:
        raise ValueError(f'{label} exceeds a word')
    return value


def _enabled(token):
    if token == 'on':
        return 0xff
    if token == 'off':
        return 0x00
    raise ValueError(f'preference state must be on or off, not {token!r}')


def preference_bytes(path):
    rows = _rows(path, ('preference', 'enabled'))
    if [row['preference'] for row in rows] != list(PREFERENCES):
        raise ValueError('default preferences must stay in preference order, including the two reserved bytes')
    return [_enabled(row['enabled']) for row in rows]


def widget_words(path):
    rows = _rows(path, ('left', 'top', 'right', 'bottom', 'key'))
    if [row['key'] for row in rows] != list(PREFERENCES[:31]):
        raise ValueError('preference widgets must follow preference order')
    words = []
    for row in rows:
        words.extend(_coordinate(row[name], name) for name in ('left', 'top', 'right', 'bottom'))
        words.append(KEYS[row['key']])
    return words


def detail_presets(path):
    rows = _rows(path, ('preset',))
    if [row['preset'] for row in rows] != list(DETAIL):
        raise ValueError('shape detail presets must be low, medium, high, very high')
    return [0xff << (8 * index) for index in range(4)]


def filename_words(path):
    rows = _rows(path, ('left', 'top', 'right', 'bottom', 'key'))
    if [row['key'] for row in rows] != ['save_file', 'edit_name']:
        raise ValueError('filename widgets are the save button and the name box')
    words = []
    for row in rows:
        words.extend(_coordinate(row[name], name) for name in ('left', 'top', 'right', 'bottom'))
        words.append(FILENAME_KEYS[row['key']])
    return words

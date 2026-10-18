"""Named flight-parameter words shared by model sources and the native game."""
import csv
import io
import struct
from pathlib import Path

COLUMNS = ['model', 'main_thrust', 'retro_thrust', 'gun_mount_flags', 'hull_mass', 'internal_capacity',
           'price', 'preview_size', 'name_text', 'required_crew', 'missile_pylons', 'drive_options',
           'combat_points', 'front_eye_forward', 'front_eye_up', 'rear_eye_forward', 'rear_eye_up',
           'front_gun_forward', 'front_gun_up', 'rear_gun_forward', 'rear_gun_up',
           'top_gun_forward', 'top_gun_up', 'bottom_gun_forward', 'bottom_gun_up']


def records(text, names_path=None):
    catalogue = Path(names_path) if names_path else Path(__file__).resolve().parents[1] / 'data/text/frontier/identifiers.txt'
    text_selectors = {line.split('\t', 1)[0]: 0x4000 + index
                      for index, line in enumerate(catalogue.read_text(encoding='ascii').splitlines()) if line}
    reader = csv.DictReader(io.StringIO(text))
    if reader.fieldnames != COLUMNS:
        raise ValueError('Unexpected ship parameter columns')
    result = {}
    for row in reader:
        if None in row or any(value is None for value in row.values()):
            raise ValueError('Invalid ship parameter row')
        name = row['model']
        if not name or name in result:
            raise ValueError('Ship model must be unique and nonempty')
        words = [text_selectors[row[column]] if column == 'name_text' and row[column] in text_selectors
                 else int(row[column], 0) for column in COLUMNS[1:13]]
        if any(not 0 <= word <= 65535 for word in words):
            raise ValueError('Ship parameter exceeds a word')
        weights = []
        absent = False
        for column in COLUMNS[13:]:
            if not row[column]:
                absent = True
                continue
            if absent:
                raise ValueError('Omitted mount coordinates must be a trailing suffix')
            value = int(row[column], 0)
            if not -32768 <= value <= 32767:
                raise ValueError('Mount coordinate exceeds a signed word')
            weights.append(value)
        if len(weights) not in (4, 6, 8, 10, 12):
            raise ValueError('Expected paired eye/mount coordinates, including both eye positions')
        result[name] = struct.pack('<12H' + 'h' * len(weights), *words, *weights)
    return result

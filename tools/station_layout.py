"""Semantic station display, widget, drive, and repair authoring."""
import json
from pathlib import Path

from text_references import resolve
from station_rules import reply_routes

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'data/interactions/layout.json'
ACTIONS = ('menu', 'conversation', 'bulletin', 'trade', 'fitting', 'shipyard', 'ship', 'missions', 'repairs')
DRIVES = ('none', 'class_1', 'class_2', 'class_3', 'class_4', 'class_5', 'class_6',
          'class_7', 'class_8', 'class_9', 'military_class_1', 'military_class_2',
          'military_class_3', 'military_class_4', 'interplanetary')
GROUPS = ('flight_menu', 'station_menu', 'secondary_rows', 'primary_rows', 'footer')
WIDGET_SETS = ('flight', 'station', 'list', 'board')
DISPLAY_FIELDS = ('visible_rows', 'row_spacing', 'text_x', 'page_size', 'continue_to')


def fields(value, names):
    if not isinstance(value, dict) or set(value) != set(names):
        raise ValueError(f'Expected fields: {names}')


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'Expected integer in {low}..{high}')
    return value


def widget_action(action):
    if action in ('next_page', 'previous_page', 'continue'):
        return {'next_page': 6, 'previous_page': 5, 'continue': 3}[action]
    if not isinstance(action, dict) or len(action) != 1:
        raise ValueError('Unknown station widget action')
    kind, index = next(iter(action.items()))
    if kind == 'menu_key':
        if not isinstance(index, str) or index not in ('1', '2', '3', '4', '5'):
            raise ValueError('Menu keys are 1..5')
        return ord(index)
    if kind not in ('primary_row', 'secondary_row'):
        raise ValueError('Unknown row action')
    return (0x80 if kind == 'primary_row' else 0x90) + integer(index, 0, 13)


def compile_document(doc):
    if isinstance(doc, dict) and '$schema' in doc:
        if not isinstance(doc['$schema'], str):
            raise ValueError('Schema reference must be a string')
        doc = {key: value for key, value in doc.items() if key != '$schema'}
    fields(doc, ('format', 'actions', 'pagination_text_x', 'widget_groups', 'widget_sets', 'drives', 'repairs'))
    if doc['format'] != 'frontier-station-layout-v1':
        raise ValueError('Unknown station layout format')
    fields(doc['actions'], ACTIONS)
    reply_limit = 0x9800 + len(reply_routes())
    displays = []
    for name in ACTIONS:
        record = doc['actions'][name]
        fields(record, DISPLAY_FIELDS)
        continue_text = resolve(record['continue_to'], 'interaction', optional=True)
        if continue_text and not 0x9800 <= continue_text < reply_limit:
            raise ValueError('Continue target has no station reply handler')
        displays.append((integer(record['visible_rows'], 1, 45) - 1,
                         integer(record['row_spacing'], 1, 199), integer(record['text_x'], 0, 319),
                         integer(record['page_size'], 0, 45),
                         continue_text))
    fields(doc['widget_groups'], GROUPS)
    groups = {}
    for name, records in doc['widget_groups'].items():
        if not isinstance(records, list) or not records:
            raise ValueError('Widget groups must be nonempty lists')
        words = []
        for record in records:
            fields(record, ('bounds', 'action'))
            bounds = record['bounds']
            if not isinstance(bounds, list) or len(bounds) != 4:
                raise ValueError('Expected [left, top, right, bottom] widget bounds')
            left, top, right, bottom = [integer(v, 0, limit) for v, limit in zip(bounds, (319, 199, 320, 200))]
            if left >= right or top >= bottom:
                raise ValueError('Widget bounds must have positive area')
            words.extend((left, top, right, bottom, widget_action(record['action'])))
        groups[name] = words
    fields(doc['widget_sets'], WIDGET_SETS)
    widgets = {}
    for name in WIDGET_SETS:
        references = doc['widget_sets'][name]
        if (not isinstance(references, list) or not references or
                any(not isinstance(reference, str) or reference not in groups for reference in references)):
            raise ValueError('Unknown widget group reference')
        if len(set(references)) != len(references):
            raise ValueError('Widget set repeats a group')
        widgets[name] = [word for reference in references for word in groups[reference]]
    if [len(widgets[name]) // 5 for name in WIDGET_SETS] != [5, 6, 31, 17]:
        raise ValueError('Native station widget counts changed')
    fields(doc['drives'], DRIVES)
    drives = []
    for name in DRIVES:
        record = doc['drives'][name]
        fields(record, ('trade_in_value', 'name_text', 'mass'))
        drives.append((integer(record['trade_in_value'], 0, 65535), resolve(record['name_text']),
                       integer(record['mass'], 0, 65535)))
    repairs = doc['repairs']
    if not isinstance(repairs, list) or len(repairs) != 18:
        raise ValueError('Expected eighteen equipment damage flags')
    compiled_repairs = []
    for index, record in enumerate(repairs):
        fields(record, ('damage_flag', 'name_text', 'charge'))
        if integer(record['damage_flag'], 0, 17) != index:
            raise ValueError('Repair damage flags must be ordered and contiguous')
        compiled_repairs.append((resolve(record['name_text'], 'interaction'), integer(record['charge'], -32768, 32767) & 65535))
    return {'displays': displays, 'pagination_text_x': integer(doc['pagination_text_x'], 0, 319),
            'widgets': widgets, 'drives': drives, 'repairs': compiled_repairs}


def load(path=SOURCE):
    return compile_document(json.loads(Path(path).read_text(encoding='utf-8-sig')))


def recovery_table(path, name):
    """Encode legacy aliases only for the historical-resource regression path."""
    data = load(path)
    displays, widgets = data['displays'], data['widgets']
    if name == 'interaction_row_limits': return [row[0] for row in displays]
    if name == 'interaction_row_spacing': return [row[1] for row in displays] + [data['pagination_text_x']]
    if name == 'interaction_text_columns': return [row[2] for row in displays] + [widgets['flight'][0]]
    if name == 'interaction_page_sizes': return [row[3] for row in displays[:8]]
    if name == 'interaction_action_messages': return [row[4] for row in displays]
    if name == 'interaction_widgets':
        return (widgets['flight'] + [65535] + widgets['station'] + [65535] + widgets['list'] + [65535])[1:]
    columns = {'shipyard_text_selectors': 0, 'interaction_record_text_selectors': 1, 'interaction_reply_text_selectors': 2}
    if name in columns: return [row[columns[name]] for row in data['drives']]
    if name == 'interaction_reply_parameters': return [value for row in data['repairs'] for value in row]
    raise ValueError(f'Unknown station recovery table: {name}')

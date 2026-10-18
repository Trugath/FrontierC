"""Compile editable station reply routing and mission destination rules."""
import csv
from pathlib import Path

from gameplay_data import ROOT, rows

HANDLERS = ('service', 'fitting', 'yard', 'trade', 'charge', 'talk', 'mission', 'board')


def reply_routes(path=None):
    names = [line.split('\t', 1)[0] for line in
             (ROOT / 'data/text/interaction/identifiers.txt').read_text(encoding='ascii').splitlines() if line]
    records = rows(path or ROOT / 'data/interactions/reply_routes.csv', ['reply', 'handler'])
    if [row['reply'] for row in records] != names[:115]:
        raise ValueError('Reply routes must cover the first 115 interaction texts in catalogue order')
    if any(row['handler'] not in HANDLERS for row in records):
        raise ValueError('Unknown station reply handler')
    return [HANDLERS.index(row['handler']) for row in records]


def mission_destinations(path=None):
    from system_records import catalogue_key
    with (ROOT / 'data/systems/catalogue/records.tsv').open(encoding='ascii', newline='') as stream:
        catalogue = {row['record']: catalogue_key(row) for row in csv.DictReader(stream, delimiter='\t')
                     if row['record'].startswith('keyed_record_')}
    records = rows(path or ROOT / 'data/interactions/mission_destinations.csv',
                   ['system_record', 'mission_flag_bit'])
    if not records:
        raise ValueError('Mission destination rules must not be empty')
    result = []
    for row in records:
        if row['system_record'] not in catalogue:
            raise ValueError('Unknown keyed system catalogue record')
        bit = int(row['mission_flag_bit'], 0)
        if not 0 <= bit < 32:
            raise ValueError('Mission flag must fit the 32-bit mission state')
        result.append((catalogue[row['system_record']], bit))
    if len({key for key, _ in result}) != len(result) or len({bit for _, bit in result}) != len(result):
        raise ValueError('Mission destination systems and flag bits must be unique')
    return result


def source():
    routes = reply_routes()
    destinations = mission_destinations()
    lines = ['const uint8_t fe2_station_reply_routes[115] = {']
    for start in range(0, len(routes), 6):
        lines.append('    ' + ', '.join('STATION_REPLY_' + HANDLERS[value].upper()
                                       for value in routes[start:start + 6]) + ',')
    lines.extend(['};', 'const fe2_station_mission_destination fe2_station_mission_destinations[] = {'])
    lines.extend(f'    {{0x{key:08x}u, {bit}}},' for key, bit in destinations)
    lines.extend(['};', f'const unsigned fe2_station_mission_destination_count = {len(destinations)};'])
    return '\n'.join(lines)

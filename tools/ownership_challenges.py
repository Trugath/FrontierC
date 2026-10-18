"""Compile manual word/line/page/first-letter challenges to module tables."""
import csv


def document(config, source, asset):
    entry = config['challenges']
    with asset(source, entry['entries']).open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if reader.fieldnames != ['word', 'line', 'page', 'answer']:
            raise ValueError('Expected word, line, page, answer TSV columns')
        rows = list(reader)
    if len(rows) != entry.getint('count'):
        raise ValueError('Ownership challenge count changed')
    values = []
    for row in rows:
        if None in row or any(value is None for value in row.values()):
            raise ValueError('Ownership challenge column count changed')
        coordinates = [int(row[field], 10) for field in ('word', 'line', 'page')]
        if any(not 1 <= value <= 255 for value in coordinates):
            raise ValueError('Challenge word/line/page must fit a nonzero byte')
        answer = row['answer']
        if len(answer) != 1 or not 'A' <= answer <= 'Z':
            raise ValueError('Challenge answer must be one uppercase letter')
        values.extend(coordinates + [ord(answer) - ord('A')])
    start, end = int(entry['start'], 0), int(entry['end'], 0)
    if len(values) != end - start:
        raise ValueError('Ownership challenge byte length changed')
    return {'format': 'fe2-module-tables-v1', 'tables': [
        {'label': 'ownership_challenge_table', 'start': start, 'end': end,
         'width': 1, 'values': values}]}

"""Resolve named dispatch rules; address encoding is confined to recovery output."""
import csv
import struct


def dispatch_words(path, index_column, count, handlers):
    values = []
    with path.open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != [index_column, 'handler']:
            raise ValueError('Unexpected dispatch columns')
        for index, row in enumerate(reader):
            if None in row or any(value is None for value in row.values()):
                raise ValueError('Invalid dispatch row')
            if int(row[index_column], 0) != index:
                raise ValueError('Dispatch slots must be ordered and contiguous')
            if row['handler'] not in handlers:
                raise ValueError('Unknown dispatch handler')
            values.append(handlers[row['handler']])
    if len(values) != count:
        raise ValueError('Dispatch entry count changed')
    return struct.pack(f'<{count}H', *values)

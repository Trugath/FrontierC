"""Read one-based texture selectors and seven logical RGB12 palette slots."""
import csv


def read_materials(path):
    values = []
    fields = ['index', 'graphics_selector'] + [f'color{i}' for i in range(7)]
    with path.open(encoding='ascii', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if reader.fieldnames != fields:
            raise ValueError('Material columns must identify the selector and seven colors')
        for index, row in enumerate(reader, 1):
            if None in row or any(value is None for value in row.values()):
                raise ValueError('Material record requires all nine columns')
            if int(row['index'], 0) != index:
                raise ValueError('Material indices must be consecutive and one-based')
            selector = int(row['graphics_selector'], 0)
            if not 0 <= selector <= 0xffff:
                raise ValueError('Graphics selector must fit a word')
            colors = [int(row[f'color{i}'], 0) for i in range(7)]
            if any(not (0 <= color <= 0xfff or color == 0xffff) for color in colors):
                raise ValueError('Palette slots require RGB12 colors or FFFF sentinel')
            values.extend([selector, *colors])
    if not values:
        raise ValueError('Material table cannot be empty')
    return values

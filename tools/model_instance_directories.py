"""Character renderer entry tables consumed at model header +20h.

The input byte is indexed as 2*(byte-20h). Source offsets are relative to the
renderer program; compiled words are relative to the complete model record.
Only the two auxiliary records have this recovered extension layout.
"""


def byte_values(source):
    values = [int(v, 0) for v in source.replace(',', ' ').split()]
    if any(not 0 <= v <= 255 for v in values):
        raise ValueError('Model extension byte out of range')
    return values


def instance_entries(section):
    entries = []
    for row in section['instance_programs'].splitlines():
        if not row.strip():
            continue
        fields = row.replace(',', ' ').split()
        if len(fields) != 2:
            raise ValueError('Instance entry requires a byte selector and program offset')
        selector, offset = (int(v, 0) for v in fields)
        if selector != 0x20 + len(entries) or selector > 255:
            raise ValueError('Instance directory must have contiguous byte selectors from 20h')
        if not 0 <= offset <= 65535:
            raise ValueError('Instance program offset exceeds a word')
        entries.append(offset)
    if not entries:
        raise ValueError('Instance directory cannot be empty')
    return entries


def compile_extension(section, vertex_bytes, classification_bytes):
    if set(section) not in ({'bytes'}, {'bytes', 'instance_programs'}):
        raise ValueError('Invalid model extension fields')
    prefix = byte_values(section['bytes'])
    if 'instance_programs' not in section:
        return prefix
    if len(prefix) != 16:
        raise ValueError('Instance directory prefix must end at model header +20h')
    entries = instance_entries(section)
    base = 16 + len(prefix) + 2 * len(entries) + vertex_bytes + classification_bytes
    output = list(prefix)
    for offset in entries:
        address = base + offset
        if address > 65535:
            raise ValueError('Instance entry leaves model-relative word addressing')
        output.extend((address & 255, address >> 8))
    return output


def directory_offsets(config):
    """Recover ordered entries, retaining character aliases during migration."""
    section = config['extension']
    if 'instance_programs' in section:
        return instance_entries(section)
    extension = byte_values(section['bytes'])
    if len(extension) not in (118, 198):
        return []
    vertices = byte_values(config['vertices']['records'])
    classification = byte_values(config['classification']['records'])
    base = 16 + len(extension) + len(vertices) + len(classification)
    return [extension[i] + 256 * extension[i + 1] - base
            for i in range(16, len(extension), 2)]


def program_entry_offsets(config):
    return sorted(set(directory_offsets(config)))

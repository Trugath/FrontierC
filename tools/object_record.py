"""Encode a named 286-byte object record.

The layout is the one fe2_object_read_template reads. Omitted fields are zero.
Values keep the original unsigned bit patterns. `near_unit` is the axis word
0x7f00. `model` and `stored_model` name a slot in the main model directory.
The name occupies the 20 bytes
at 0xf6 and must leave a NUL; the word at 0x10a is model_scale_a, which the
original's 22-byte name slot also covers when that word is zero.
"""
from pathlib import Path

RECORD_BYTES = 0x11e
FIELDS = (
    (0x00, 'orientation', 2, 9),
    (0x12, 'rebuild_budget', 1, 1),
    (0x13, 'render_state', 1, 1),
    (0x14, 'camera_offset', 4, 3),
    (0x20, 'camera_target', 4, 3),
    (0x2c, 'position_low', 4, 3),
    (0x38, 'position_high', 4, 3),
    (0x44, 'parent_handle', 1, 1),
    (0x45, 'station_slot', 1, 1),
    (0x46, 'alternate_orientation', 2, 9),
    (0x58, 'model_parameter', 2, 1),
    (0x5a, 'model', 2, 1),
    (0x5c, 'basis_generation', 1, 1),
    (0x5d, 'handle', 1, 1),
    (0x5e, 'role', 1, 1),
    (0x5f, 'level', 1, 1),
    (0x60, 'update_period', 1, 1),
    (0x61, 'update_countdown', 1, 1),
    (0x62, 'velocity', 4, 3),
    (0x6e, 'screen_x', 2, 1),
    (0x70, 'screen_y', 2, 1),
    (0x72, 'clock', 2, 1),
    (0x74, 'cargo_type', 2, 1),
    (0x76, 'target_seed', 4, 1),
    (0x7a, 'size_mantissa', 2, 1),
    (0x7c, 'size_exponent', 2, 1),
    (0x7e, 'orbit', 4, 2),
    (0x86, 'scalar', 4, 1),
    (0x8a, 'vector_delta', 2, 3),
    (0x90, 'orbit_product', 2, 4),
    (0x98, 'orbit_radius', 4, 1),
    (0x9c, 'orbit_inclination', 2, 1),
    (0x9e, 'time_shift', 1, 1),
    (0x9f, 'docking_state', 1, 1),
    (0xa0, 'docking_claim', 1, 1),
    (0xa1, 'generation', 1, 1),
    (0xa2, 'basis_seed', 2, 1),
    (0xa4, 'shape', 2, 1),
    (0xa6, 'docked', 1, 8),
    (0xae, 'docking_timer', 2, 1),
    (0xb0, 'scale_exponent', 2, 1),
    (0xb2, 'radius_class', 2, 1),
    (0xb4, 'progress', 2, 1),
    (0xb6, 'temperature', 2, 1),
    (0xb8, 'hull', 2, 1),
    (0xba, 'stored_model', 2, 1),
    (0xbc, 'cooldown', 2, 1),
    (0xbe, 'busy_remaining', 2, 1),
    (0xc0, 'velocity_fraction', 2, 3),
    (0xc6, 'feedback', 4, 3),
    (0xd2, 'behaviour_parent', 1, 1),
    (0xd3, 'behaviour', 1, 1),
    (0xd4, 'behaviour_target', 1, 1),
    (0xd5, 'control_class', 1, 1),
    (0xd6, 'source_offset', 2, 3),
    (0xdc, 'steering', 2, 3),
    (0xe2, 'steering_shift', 2, 1),
    (0xe4, 'rotation_rates', 2, 3),
    (0xea, 'capacity', 2, 1),
    (0xec, 'map_marker', 1, 1),
    (0xed, 'drive_fuel', 1, 1),
    (0xee, 'reward', 4, 1),
    (0xf2, 'fuel', 4, 1),
    (0xf6, 'name', 1, 20),
    (0x10a, 'model_scale_a', 2, 3),
    (0x110, 'model_scale_b', 2, 3),
    (0x116, 'transition_countdown', 2, 1),
    (0x118, 'flags', 2, 1),
    (0x11a, 'trail', 1, 1),
    (0x11b, 'effect', 1, 1),
    (0x11c, 'probe_countdown', 1, 1),
    (0x11d, 'frame_state', 1, 1),
)
_BY_NAME = {name: (offset, width, count) for offset, name, width, count in FIELDS}


def _covered():
    seen = bytearray(RECORD_BYTES)
    for offset, name, width, count in FIELDS:
        for index in range(offset, offset + width * count):
            if seen[index]:
                raise ValueError(f'Object field {name} overlaps another field')
            seen[index] = 1
    if any(not byte for byte in seen):
        raise ValueError('Object record fields do not cover the 286-byte record')


_covered()


def read_object(path):
    """Return the 286 record bytes named by an object-record file."""
    record = bytearray(RECORD_BYTES)
    seen = set()
    for line_number, raw in enumerate(Path(path).read_text(encoding='ascii').splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if '=' not in line:
            raise ValueError(f'{path}:{line_number}: expected name = value')
        name, value = (part.strip() for part in line.split('=', 1))
        if name not in _BY_NAME:
            raise ValueError(f'{path}:{line_number}: unknown object field {name}')
        if name in seen:
            raise ValueError(f'{path}:{line_number}: repeated object field {name}')
        seen.add(name)
        offset, width, count = _BY_NAME[name]
        if name == 'name':
            encoded = value.encode('ascii')
            if not encoded or len(encoded) >= count or any(not 32 <= byte < 127 for byte in encoded):
                raise ValueError(f'{path}:{line_number}: name must be printable and fit before model_scale_a')
            record[offset:offset + len(encoded)] = encoded
            continue
        parts = [part.strip() for part in value.split(',')]
        if len(parts) != count:
            raise ValueError(f'{path}:{line_number}: {name} needs {count} values')
        for index, part in enumerate(parts):
            if part == 'near_unit' and name in ('orientation', 'alternate_orientation'):
                number = 0x7f00
            elif name in ('model', 'stored_model'):
                from model_names import parse_model
                number = parse_model(part)
            else:
                number = int(part, 0)
            if not 0 <= number < 1 << (8 * width):
                raise ValueError(f'{path}:{line_number}: {name} exceeds its field')
            record[offset + index * width:offset + (index + 1) * width] = number.to_bytes(width, 'little')
    return bytes(record)

"""DOS point-list size operands and RGB12 selectors, before runtime evaluation.

Arithmetic mirrors render_variable_point_command.inc. These descriptions retain
the original word: local and object operands have ignored bits, so canonical
encoders are deliberately separate from decoding.
"""
from dataclasses import dataclass


def _word(value: int) -> int:
    if not 0 <= value <= 0xffff:
        raise ValueError('expected an unsigned 16-bit word')
    return value


@dataclass(frozen=True)
class PointSizeOperand:
    raw: int
    kind: str
    value: int


def decode_size_operand(word: int) -> PointSizeOperand:
    """value is a literal word, frame-relative offset, or object field offset."""
    word = _word(word)
    if not word & 0x80:
        value = ((word >> 7) | (word << 9)) & 0xffff
        return PointSizeOperand(word, 'literal', value)
    shifted = (word << 1) & 0xffff
    if shifted & 0x80:
        return PointSizeOperand(word, 'frame', (shifted & 0x7f) - 0x40)
    return PointSizeOperand(word, 'object', (shifted & 0xfe7f) + 0x72)


def encode_size_operand(kind: str, value: int) -> int:
    """Produce a canonical spelling; aliases retain their raw word on decode."""
    if kind == 'literal':
        value = _word(value)
        if value & 1:
            raise ValueError('literal size must be even (operand bit 7 is mode)')
        return ((value << 7) | (value >> 9)) & 0xffff
    if kind == 'frame':
        offset = value + 0x40
        if not 0 <= offset <= 0x7e or offset & 1:
            raise ValueError('frame offset must be even in [-64, 62]')
        return 0xc0 | (offset >> 1)
    if kind == 'object':
        offset = value - 0x72
        if offset < 0 or offset > 0xffff or offset & ~0xfe7e:
            raise ValueError('object field offset is not encodable')
        return 0x80 | (offset >> 1)
    raise ValueError('unknown size operand kind')


def resolved_size_mode(word: int) -> tuple[str, int]:
    """A loaded/literal negative word is a fixed screen-size magnitude."""
    word = _word(word)
    return ('fixed', word & 0x7fff) if word & 0x8000 else ('depth_scaled', word)


def material_accumulator(token: int, inherited: int = 0) -> int:
    """Return EAX before optional material-table lookup; keep 32-bit wrap.

    Nonzero upper 16 bits select the table path. Table byte offset is the low
    16 bits of EAX >> 12; low 12 bits are added to its RGB12 word at offset -14.
    """
    selector = _word(token) >> 4
    value = selector
    if selector & 0xf00 == 0xe00:
        value |= ((selector & 0xe) << 15) | ((selector & 0xe0) << 14)
        value = (value & 0xffff0000) | (selector & 0x10)
    value = (value & 0xffff0000) | ((value & 0xffff) & 0xefe)
    inherit = bool(value & 0x10)
    value &= ~0x10
    return (value + (inherited if inherit else 0)) & 0xffffffff


def describe_material(token: int) -> dict:
    selector = _word(token) >> 4
    if selector & 0xf00 == 0xe00:
        return {'kind': 'table', 'inherit': bool(selector & 0x10),
                'table_byte_offset': ((selector & 0xe) << 3) |
                                     ((selector & 0xe0) << 2)}
    return {'kind': 'rgb12', 'inherit': bool(selector & 0x10),
            'rgb12': selector & 0xeee}

"""Readable reference for the complete names generator and its local text grammar.

The assembly remains the game build input. This reference is differentially
checked against it, using the original resident sampler and formatter too.
"""
from dataclasses import dataclass
from pathlib import Path
from string_resources import decode

ROOT = Path(__file__).resolve().parents[1] / 'data'


def rotate_left(value, count):
    return ((value << count) | (value >> (32 - count))) & 0xffffffff


def swap_words(value):
    return ((value << 16) | (value >> 16)) & 0xffffffff


@dataclass
class Seed:
    accumulator: int
    state: int

    def sample(self, bound):
        total = (self.state + self.accumulator) & 0xffffffff
        self.accumulator = rotate_left(self.accumulator, 5)
        self.state = swap_words((total + self.accumulator) & 0xffffffff)
        return ((bound & 0xffff) * (self.state & 0xffff)) >> 16


def catalog():
    entries = [bytes(decode(line.split('\t', 1)[1])) for line in
               (ROOT / 'text/names/strings.tsv').read_text(encoding='ascii').splitlines()]
    if len(entries) != 394:
        raise ValueError('Name directory must contain 394 entries')
    return entries


def templates():
    root = ROOT / 'names'
    def words(name):
        return [int(v, 0) for v in (root / name).read_text().strip().split(',')]
    return words('name_template_bases.csv'), words('name_template_ranges.csv')


def suffix(seed, entries, family_only=False):
    if family_only or seed.state & 255 >= 25:
        return entries[89 + seed.sample(138)]
    result = entries[79 + seed.sample(10)] + b's'
    return result if seed.state & 0x800 else result + b'on'


def expand_template(entry, component):
    # All control occurrences in this module are FF + big-endian selector
    # 3016h: resident pointer0 text expansion from DS:7E4/7E6.
    result = entry.replace(b'\xff\x30\x16', component)
    if any(value >= 0xfd for value in result):
        raise ValueError('Unrecovered names template control')
    return result


def generate(mode, low_seed, high_seed, entries=None, tables=None):
    """Return output including NUL and final seed registers (SI:DI, CX:BX).

    Actual x86 wraps destination offsets; this pure reference returns bytes
    and delegates destination capacity/segment policy to its caller.
    """
    entries = catalog() if entries is None else entries
    bases, ranges = templates() if tables is None else tables
    mode &= 0xffff
    initial = ((high_seed & 0xffff) << 16) | (low_seed & 0xffff)
    swapped = swap_words(initial)
    if mode < 6:
        seed = Seed(rotate_left((initial + swapped) & 0xffffffff, 7), initial)
        if mode == 4:
            seed.sample(4)  # The original discards this sample before surname selection.
            output = suffix(seed, entries, family_only=True)
        else:
            alternate = mode == 2 or (mode != 1 and initial & 1)
            first = (224 + seed.sample(60)) if alternate else seed.sample(105)
            output = entries[first] + b' ' + suffix(seed, entries)
    else:
        seed = Seed(rotate_left(swapped, 7), (initial + swapped) & 0xffffffff)
        seed.state = (seed.state + seed.accumulator) & 0xffffffff
        seed.accumulator = rotate_left(seed.accumulator, 4)
        seed.state = (seed.state + seed.accumulator) & 0xffffffff
        if mode < 15:
            component = suffix(seed, entries)
            index = bases[mode - 6] + seed.sample(ranges[mode - 6])
            output = expand_template(entries[index], component)
        else:
            def letter():
                return bytes([65 + seed.sample(26)])
            def digit():
                return bytes([48 + seed.sample(10)])
            if mode == 15:
                output = letter() + letter() + b'-' + digit() + digit() + digit()
            elif mode == 16:
                # The original writes '+ ' without advancing BP; digits overwrite it.
                output = digit() + digit() + digit() + letter()
            else:
                output = letter() + letter() + b' * ' + digit()
    return output + b'\0', seed


def maximum_output_bytes():
    """Conservative per-mode storage bounds from all possible resource choices."""
    entries = catalog()
    component = max(max(map(len, entries[89:227])),
                    max(map(len, entries[79:89])) + 3)
    first = max(map(len, entries[:105] + entries[224:284]))
    bounds = {mode: first + 1 + component + 1 for mode in range(6)}
    bounds[4] = max(map(len, entries[89:227])) + 1
    bases, ranges = templates()
    for mode, base, count in zip(range(6, 15), bases, ranges):
        bounds[mode] = max(len(expand_template(entry, b'x' * component)) + 1
                           for entry in entries[base:base + count])
    bounds.update({15: 7, 16: 5, 17: 7})  # 17 represents every unsigned mode >=17.
    return bounds, component + 1

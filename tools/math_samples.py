"""Integer samples for the resident sine, root, reciprocal and arccosine tables.

The series below are evaluated with decimal arithmetic so the words do not
depend on the host math library. Each one was checked against the original
table before that table was removed.
"""
from decimal import Decimal, localcontext

_PI = Decimal('3.14159265358979323846264338327950288419716939937510')


def _series_sin(turns, denominator):
    """sin(pi * turns / denominator), as a Decimal."""
    with localcontext() as ctx:
        ctx.prec = 50
        x = _PI * turns / denominator
        term = x
        total = Decimal(0)
        n = 0
        while True:
            total += term
            n += 1
            term *= -x * x / Decimal((2 * n) * (2 * n + 1))
            if abs(term) < Decimal('1e-30'):
                return +total


def sine_half_cycle():
    """Half a turn. Index i is angle i/2048 of a half turn, amplitude 32767.

    The quarter-turn sample is 32766. A signed word cannot hold 32767 and
    still negate, and that is the sample the original table stores.
    """
    samples = []
    for index in range(2048):
        value = int(_series_sin(index, 2048) * 32767)
        samples.append(32766 if value == 32767 else value)
    return samples


def square_root_lookup():
    """floor(sqrt(index * 2^21)), the value fe2_math_square_root32 shifts."""
    import math
    return [math.isqrt(index << 21) for index in range(2048)]


def reciprocal_lookup():
    """min(32767, 2^23 / index). Index 0 is the saturated word, not a division."""
    return [32767 if index == 0 else min(32767, (1 << 23) // index) for index in range(1024)]


def _series_asin(value):
    with localcontext() as ctx:
        ctx.prec = 50
        x = Decimal(value)
        term = x
        total = x
        n = 1
        while True:
            term *= x * x * Decimal(2 * n - 1) / Decimal(2 * n)
            step = term / Decimal(2 * n + 1)
            total += step
            n += 1
            if abs(step) < Decimal('1e-30'):
                return +total


def acos_signed_q6():
    """Indices 0..63 are floor(acos(q/64) * 32767 / pi).

    Index 64 is 32766. Each later negative bin q is 32766 minus the positive
    sample for -q.
    """
    with localcontext() as ctx:
        ctx.prec = 50
        half = _PI / 2
        positive = []
        for q in range(64):
            angle = half - _series_asin(Decimal(q) / 64)
            positive.append(int(angle * 32767 / _PI))
    negative = [32766]
    negative.extend(32766 - positive[-q] for q in range(-63, 0))
    return positive + negative

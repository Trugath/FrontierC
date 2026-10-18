"""Native density resource, bounded grid lookup and explicit backend memory model."""
from pathlib import Path


def write_pgm(path: Path, samples: bytes, width: int = 128, height: int = 129) -> None:
    """Write the ASCII P2 graymap `read_pgm` accepts, one row per line."""
    if len(samples) != width * height or any(value > 255 for value in samples):
        raise ValueError('Density graymap sample count or byte range changed')
    lines = ['P2', '# Frontier galaxy density samples; preserve raw byte intensities.',
             f'{width} {height}', '255']
    for y in range(height):
        row = samples[y * width:(y + 1) * width]
        lines.append(' '.join(str(value) for value in row))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text('\n'.join(lines) + '\n', encoding='ascii', newline='\n')


def read_pgm(path: Path, width: int = 128, height: int = 129) -> bytes:
    """Read an ASCII P2 graymap with byte samples, without scaling values.

    The resource is grayscale intensity, not indexed color. Require maximum
    value 255 and exact dimensions so image editors cannot silently rescale it.
    """
    if not isinstance(width, int) or not isinstance(height, int) or width <= 0 or height <= 0:
        raise ValueError('Graymap dimensions must be positive integers')
    words = ' '.join(line.partition('#')[0] for line in
                     path.read_text(encoding='ascii').splitlines()).split()
    if len(words) < 4 or words[0] != 'P2':
        raise ValueError('Density source must be an ASCII P2 graymap')
    if tuple(map(int, words[1:4])) != (width, height, 255):
        raise ValueError('Density graymap dimensions or maximum value changed')
    samples = [int(word) for word in words[4:]]
    if len(samples) != width * height or any(not 0 <= v <= 255 for v in samples):
        raise ValueError('Density graymap sample count or byte range changed')
    return bytes(samples)


def lookup_segment(segment: bytes, bx: int) -> tuple[int, int]:
    """Model galaxy's word loads in Unicorn for explicit resident memory.

    Effective addresses wrap to 16 bits before the word load. The second byte
    of a word at FFFFh is at linear byte 10000h, not offset zero. Supply that
    additional byte; contents beyond the loaded overlay are caller-owned.
    Out-of-domain inputs characterize this backend only: original 386 segment
    limit/fault behavior is not established by this function.
    """
    if len(segment) != 0x10001:
        raise ValueError('Lookup needs one 64KiB segment plus the following byte')
    first = (0xa3 + bx) & 0xffff
    second = (0x123 + bx) & 0xffff
    return (segment[first] | (segment[first + 1] << 8),
            segment[second] | (segment[second + 1] << 8))


def neighbors(grid: bytes, x: int, y: int) -> tuple[int, int, int, int]:
    """chart's bounded input contract: flat row addressing, original zero tail.

    Boundary column reads cross into the next row. The bottom-right final
    sample reads the first trailing zero, rather than a clamped grid value.
    """
    if len(grid) != 128 * 129:
        raise ValueError('Expected 128 by 129 density samples')
    if not (0 <= x < 8192 and 0 <= y < 8192):
        raise ValueError('Coordinates outside the chart lookup domain')
    data = grid + b'\0\0'
    offset = (y // 64) * 128 + x // 64
    return tuple(data[offset + delta] for delta in (0, 1, 128, 129))

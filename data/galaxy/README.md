# galaxy byte grid

`frontier-data unpack` writes `density.pgm` from `EL2M19.OVL`. The file is the original density grid and is not part of the source tree. `tables.ini` stays here and names the span.

`density.pgm` retains 16512 bytes as a 128-by-129 ASCII P2 grayscale image. This
layout is established by chart sector sampling: rows are 128 bytes apart and
cells span 64 coordinate units. Values are density samples, not palette indices;
chart interpolates them and applies a separate color mapping. Physical units
remain unresolved.
The maximum intensity must remain 255. Samples compile directly to bytes without
scaling, filtering, palette conversion or resampling. The compiler rejects wrong
dimensions, changed maximum intensity, invalid sample values and missing/extra
samples. Save edited images as ASCII P2 PGM; binary P5 is not accepted.

Two trailing zero bytes remain separate from the image. PGM compiles through
intermediate JSON to generated C under `build/`. Row and column order
are unchanged. This image replaces the former CSV as the authoritative source.

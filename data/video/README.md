# MCGA video resources

`font.bdf` and `default.gpl` are written by `frontier-data unpack` and are
gitignored. `el2mcga3.ini` describes the palette, bitmap font and initialization strings. `el2mcga3_tables.ini` describes the scanline shape bank. Both compile to intermediate JSON during the build; generated assembly is disposable.

`scanline_shape_profiles.csv` contains 42 records in original bank order. Each row has fifteen unsigned half-widths (`w0`..`w14`) followed by 255 (`stop`). Zero advances without drawing. Adjacent rows form the two profiles used by the main renderer with separately mapped colors. Preserve the row order and sixteen-field layout.

The runtime selects from bank offset 224 plus a byte selector; the main renderer multiplies its selector by 32 and adds 16 for the second pass. The earlier fourteen records are retained without guessing which caller selectors reach them. The consumer's original anchor bias, row origin and clipping behavior are documented beside its source.


`palette_cache_seeds.csv` contains sixteen RGB12-key, logical-slot, DAC-index triples with no header. These initialize reserved grayscale palette slots. `el2mcga3.ini` references the file; the compiler uses intermediate JSON to generate the exact seed initialization instructions. Keys must fit twelve bits, slots must be below 255, DAC indices must fit a byte, and keys/slots must be unique.

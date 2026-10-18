# Drive parameters

`parameters.csv` contains fifteen ordered records: unsigned capacity numerator,
threshold word, and `name_text`. `name_text` is a flight, interaction, or status
catalogue entry. The build validates field widths and
row shape, emits little-endian records through intermediate JSON, and checks
original table boundaries.

Object field A6 indexes these records: 0 is the none entry, 1 the interplanetary
drive, 2..9 standard hyperdrive classes 1..8, and 10..13 military drive classes
1..4. Index 14 is retained without a drive identity claim. Physical units are still being investigated.

`outcomes.csv` preserves sixteen selectors indexed by a sampled low nibble.
Do not reorder either table.

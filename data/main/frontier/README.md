# Resident main data

`tables.ini` compiles editable tables through JSON into assembly includes. Values retain their shipped order and integer rounding; table names are inferred.

## Video surfaces

`video_surfaces.csv` has three seven-word records, in order: general drawing surface (320?200), viewport drawing surface (320?168), and pointer surface (320?200, initially VGA segment A000h). The first two framebuffer segments are filled at runtime by EL2MCGA3. They live at SS:FD59, SS:FD67 and SS:FD75 with SS=3154h.

| Word | Field | Evidence |
|---|---|---|
| 0 | Pixel-buffer offset | `LES DI,[BP]` in sprite drawing |
| 1 | Pixel-buffer segment | Far-pointer load and framebuffer segment updates |
| 2 | Clipping origin X | Subtracted from requested sprite X |
| 3 | Clipping origin Y | Subtracted from requested sprite Y |
| 4 | Width | Right clipping and row-address multiplication |
| 5 | Height | Bottom clipping |
| 6 | Extra bytes per row | Added to width for row stride |

The sprite path computes destination stride as width plus row padding. Negative local coordinates trim the source, and right/bottom clipping reduce the copied dimensions. The pointer redraw temporarily targets VGA through the third descriptor; the shared ABI names these fields in `source/common/video_surface_abi.inc`. The records are runtime descriptors rather than pixel assets. Signed word comparisons, wrapping additions and early exits remain assembly behavior; do not substitute host integer arithmetic without checking edge cases.

Other resources include the keyboard translation maps, mathematical lookup words, formatting text and dates, indexed equipment constants, and five-word UI rectangles. Unresolved numeric table contracts are documented in the main module recovery notes.

## Signed arccosine lookup

Indices 0..63 represent q=0..63 and indices 64..127 represent q=-64..-1.
The positive half is `floor(acos(q/64) * 32767/pi)`. Entry 64 is 32766.
Each later negative entry is `32766 - table[-q]`. `tools/math_samples.py`
generates the words. The sine half-cycle, square-root table and saturated
reciprocal are generated there too: sine is `floor(32767 * sin(pi * index / 2048))`
with the quarter-turn sample held at 32766, square root is `floor(sqrt(index * 2^21))`,
and reciprocal is `min(32767, 2^23 / index)` with index 0 saturated.

Renderer entry 8612h selects the even byte index `(unsigned_word(AX)>>8)&FFFEh`,
equivalently the signed Q6 bin `signed16(AX)>>9`, doubles the selected word,
and stores it at BP-8Ah. Every group of 512 quotients shares a table entry;
negative inputs floor toward negative infinity at bin boundaries. There is no
interpolation or post-division clamp. The build evaluates the series in decimal
arithmetic, not with the host math library. The isolated instruction
test checks every quotient word and preservation of the extended register high
halves. The earlier signed division, its exception-resume behavior and the later
geometry queue are outside that numerical test.


The paired solid-disc size maps are separate editable byte tables: `paired_disc_inner_sizes.csv` (image 28D2..28DB) and `paired_disc_outer_sizes.csv` (28DB..28E4). Main rendering draws the outer size first, then the inner size with a second mapped color. Original caller bases are one byte before each table; selectors 1..9 address these entries. The selector domain remains under analysis. `coordinate_pair_rejection_masks.csv` follows at 28E4..2902; `main_render_parameter_tail.csv` now begins at 2902, preserving the remaining parameters. `tables.ini` compiles these resources through intermediate JSON into position-checked assembly.


`coordinate_pair_rejection_masks.csv` contains fifteen unsigned words consumed by the main clipping path at 53A7. Helpers at 64BD (X) and 64D3 (Y) OR region bits into BL: negative coordinate=2, in-range=4, positive coordinate outside the bound=8, and projection sentinel 8002h=16. The original lookup base is two bytes before the extracted bank, so combined codes 2..30 address its fifteen entries. Entries for codes 2, 8 and 16 are FFFFh; the other entries are zero. Callers branch on the returned word. These are coordinate-pair rejection masks, not a general polygon clipping table, and sentinel handling beyond this lookup remains under analysis.

`object_variant_parameter_sets.csv` holds six 16-byte records at CS:121Ch, each four packed dwords copied to DS:1FF5h..2004h. The selector is the low three bits of object byte 118h, multiplied by sixteen. Physical/component meanings remain unresolved. There is no selector range check: values 6/7 read the following executable bytes. Extraction preserves adjacency and the service entry at 127Ch rather than inventing two extra records. The former source had incorrectly disassembled this table and crossed the following instruction boundary.

The six former resident inline-parameter arrays are copies of a shared basis
rotation rebuild-cost source, `basis_rotation_rebuild_costs.csv`. Rotation
step selectors2..9 index the first eight bytes through a -2 bias; the two
remaining bytes are retained without assigning an unsupported purpose. The
selected byte reduces record12h's rebuild budget; borrow calls service1B8h
and resets the budget to60. Per-copy labels keep layout/CS origins explicit.


`contact_axis_permutations.csv` holds six eight-byte rows at DS:5781h.
Their first three words are all six permutations of query offsets 0, 2 and 4.
The fourth words are retained adjacency data (last value 2), not normalized
padding. Contact child queries, scene probes and later vector transforms use
the same first-three-word view. They mask selectors to three bits; selectors
6/7 consequently read adjacent state rather than a validated permutation.
That unchecked behavior remains in assembly; exclusion of 6/7 is unproved.

# Galactic map native tables

`tables.ini` orders the source tables with types, fixed image offsets and expected lengths. GPL colors are standard RGB8 display values quantized back to VGA RGB6 during compilation. Weight, descriptor and hierarchy tables that have not been decoded stay numeric.

`sector_sample_weights.csv` holds the weights a density sample multiplies by. The sector hash picks an even index into the 127 words.

The build writes intermediate JSON under `build/resources/maps/`. Those files are disposable. The map tables in this directory are the inputs; the original overlay is not read by the compiler.

`catalog_systems.csv` is the 47-sector catalogue in chart-text order. `sector_x` and `sector_y` are the map's System Sector Code. Each `system` row is one named star: its chart identifier (`sol` is `text_sol` in `../text/chart/strings.tsv`), the record bytes, and the identifier word. `style` is the marker byte, 9 for every catalogued star, the same value procedural stars use. `depth`, `place_x` and `place_y` are the record bytes. The chart negates `depth` as a signed byte, and turns each placement byte into a position with `(byte << 9) + 0x8000`. The identifier word is kept whole. After the chart swaps its bytes, some bits select the star-type text and the multiple-star prefix, and both bytes also select a value window. `draw_kind` is the extra scene kind drawn for that sector; 0 draws none. An `empty` row is a sector with a draw kind and no stars. The coordinate tail and draw-kind tail sit after the 47 sectors and are not part of the sector search. Text-directory offsets are calculated from the number of stars in each sector.

`map_annotation_records.csv` names the density-map labels. `threshold` is the largest sample count at which the label is drawn. The sector columns use the same System Sector Code, and `label` is the chart text (`major_arm` is `text_major_arm`). Betelgeuse and Spica are the two labels drawn only at sample count 1. A zero threshold after them ends the original scan.

`map_start_positions.csv` names the six positions used to open the map: Ross 154, Sol, Lave, then Ross 154 three more times. `number` is the System Number inside that sector. `y_low` and `x_low` are the position words below the sector, and `z` is the signed 32-bit height. `identifier` is the descriptor word stored with that opening position. Two padding words in each record stay zero.

`body_name_separator.txt` is the ASCII separator between a body name and the text that follows it: colon, space, space.

`candidate_sector_offsets.csv` is the two rings used when a candidate system is chosen outside its own sector. `sector_x` and `sector_y` are sector steps. The query adds `(sector_x * 8192 + sector_y) * 64` to the system reference. The one-sector ring lists the four orthogonal steps twice. The two-sector ring is chebyshev radius 2 without the four corners.

Two 128-entry palette banks supply system/density views. `density_sample_colors.csv` contains both the sampled color lookup and the nearby physical bytes aliased by its readers; it is not a declaration that every byte is a distinct semantic color. The hierarchy's four 35-word tables drive spacing, sprite offsets and sprite selection.
Sprite selection names the image stem in `graphics/el2g.ini`.

Editing element values retains layout. Changes to table lengths or offsets require recovering and relocating every reference; guards intentionally reject them. Table identities are based on DOS readers, while units, descriptor bits and several edge entries remain unresolved.

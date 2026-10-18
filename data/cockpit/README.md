# Cockpit authoring

Edit `layout.json` for cockpit UI data. `layout.schema.json` provides editor
completion and structural validation. The build additionally checks references,
screen bounds, unique command keys, and the fixed number of native slots.

- `weapons`: six weapon types, each referencing named sprite images for even
  and odd equipment slots. Names resolve through `data/graphics/el2g.ini`;
  the corresponding PNGs have matching descriptive filenames.
- `equipment_slots`: eight `[x, y]` slot centres in screen pixels, in installed
  equipment order. Their pointer IDs are generated; do not maintain a second
  list of numeric resource selectors.
- `commands`: named actions, printable character keys and
  `[left, top, right, bottom]` hitboxes. The native hit test includes both edges;
  historical bounds can extend to 320/200, just beyond the last screen pixel.
  Keys drive the hitboxes, flight keyboard dispatch and pointer
  cleanup from the same definition.
- `target_marker`: four line segments with signed pixel coordinates relative
  to the marker centre. Each segment has `from` and `to` endpoints.
- `target_labels`: the six projection cases, with a catalogue text reference
  and an absolute pixel position.
- `drive_labels`: drive classes mapped to catalogue text references.

Text references are `catalogue:identifier`, such as
`flight:target_behind_km` or `interaction:text_class_1_hyperdrive`.
Edit the matching `data/text/<catalogue>/strings.tsv` to change wording.
There are no authored packed text selectors or sprite directory indices.

The compiler still reconstructs historical image blocks for recovery checks.
The leading FFFF word in the old layout was the previous hitbox table's end
marker. Native hitboxes now have their own terminator, and sprite lookup and
slot centres are separate C arrays. The original also read the first two slot
centres as icon pairs for equipment kind codes 6/7; that unusual fallback is
retained by the compiler for compatibility, not presented as editable icons.
Only the six proven icon pairs have named weapon definitions.

Run `python test.py --integration` after edits. Migration hashes pin all original
flight resource blocks; deliberate content changes require reviewing those
fixtures. Normal editing changes JSON/PNG/TSV sources, never generated C or
`build/resources/` outputs. `tables.ini` describes the recovery image layout,
not the authoring interface.

## Not yet semantic authoring sources

`player_ship_template.txt` is the Frontier body, model `main_model_052`, copied before the
player's own ship is installed.
`emitted_object_variants.csv` names each effect model. `parameter` is the model parameter copied onto the object that effect spawns.
`emitted_object_variant_offsets.csv` is the block index of those pairs.
The prefix and overlay-event mask also retain native encodings. These still
need further recovery; changing their file extension would not make them
meaningful authoring formats. See `data/AUTHORING.md` for the wider audit.

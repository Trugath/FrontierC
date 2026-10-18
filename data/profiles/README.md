# Procedural system profiles

`profiles.csv` is the authoritative table of 23 procedural body profiles.
Each row holds the size threshold, body kind, descriptor, temperature, subtree
position, initial profile/reference value and named generation rule. The build
validates field widths, profile order and rule names, then emits the parallel
arrays required by the original layout. Callback offsets are resolved by
`tools/systems_resources_c.py`; they are not editable numeric guesses.

`static_object.txt` is the root body named Frontier, model `main_model_160`. Top-level
system records hang from it. Profile `descriptor` and the generated-station
model list name slots in `models/frontier/model_directory.tsv`. `saturated_profile_curve.csv` is the 256-sample curve. A position below 0x4000
selects sample `position >> 6`; a position at or above 0x4000 returns 0xffff.
`tables.ini` also describes the generated-station model and orbit tables. Handcrafted systems are maintained separately under
`../systems/handcrafted/`. Threshold and scalar values retain the recovered
integer encodings; the compiler does not assign unverified physical units.

Generation is implemented in `src/game/world/profiles.c` and
`src/game/world/profile_rules.c`. The profile table's size lookup selects the
first threshold strictly greater than the input. Equality advances to the next
profile. The migration regression fixture pins the resulting systems image.

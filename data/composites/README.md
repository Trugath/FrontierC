# Station portrait composites

Placements, sprite choices and palette views are edited here. `tables.ini`
records the recovered image offsets used to rebuild historical bytes. Those
offsets are compiler metadata. The section names are the generated C symbols;
the CSV filenames are the features.

A portrait is one masculine or feminine head, drawn back to front. Bit 0 of
the identity selects the feminine head when set and the masculine head when
clear. Each sprite name is the image stem in `data/graphics/el2g.ini`, so
`male_eyes_1` is `data/graphics/sprites/male_eyes_1.png`. The graphics section
number still selects the bank slot. `x` and `y` in the placement files are
signed pixel offsets from the portrait origin (59, 136), each in -128..127.
Row order in a placement file does not matter.

| Draw order | File | Feature | Selector bits |
|---|---|---|---|
| 1 | `male_collar.csv`, `female_shoulders.csv` | Collar or shoulders | 1..2 |
| 2 | `male_jaw_left.csv`, `female_jaw_left.csv` | Left jaw | 3..4 |
| 3 | `male_jaw_right.csv`, `female_jaw_right.csv` | Right jaw, same bits as the left | 3..4 |
| 4 | `male_mouth.csv`, `female_mouth.csv` | Mouth | 5..6 |
| 5 | `male_nose.csv`, `female_nose.csv` | Nose | 7..8 |
| 6 | `male_eyes.csv`, `female_eyes.csv` | Eyes, 16 choices | 9..12 |
| 7 | `male_chin.csv`, `female_chin.csv` | Chin | 13..14 |
| 8 | `male_hair.csv`, `female_hair.csv` | Hair | 15..17 |

Positions live once, in `male_parts.csv` or `female_parts.csv`. Changing a
placement does not retarget a selector that names that sprite. Glasses,
sunglasses, dreadlocks, lipstick and earrings are named where the sprite
shows them. The remaining variants of a feature are numbered in bank order.

The public entry can select masculine hair rows 0, 1, 4 and 5, and feminine
hair rows 2, 3, 6 and 7. The style flag occupies the middle hair bit. The
other rows stay in the table. Direct style entry can still address every row.

`headgear.csv` names `portrait_cap`, `portrait_helmet` or `portrait_hood`.
One of those replaces the hair when override bits 1..2 are 1, 2 or 3.
Their positions are in `headgear_parts.csv`.

Palette tables name views from `data/palettes/composites.ini`. `view_02` is
the `[view 02]` section: `first_color`, `color_count`, and the colors in
`composites.gpl`. Base tables reference 44-color views, and the following
44 colors are uploaded with them. The other tables reference 8-color views.
`detail_palette_pointers.csv` has 33 rows. The selector uses the first 32,
and the last row is kept.

The sprite PNGs keep palette indices. Their preview colors for indices
128..255 are the composite upload used when every selector is zero: the first
base view and the 44 colors after it, then the first feature, detail,
secondary and upper feature views. `python tools/sprite_previews.py` rewrites
that preview after a palette-view edit. Pixel indices stay put.

The compiler calculates bank slots, scaled selectors and palette addresses.
Run `python test.py` after edits. The migration check pins the recovered
526-byte image; intentional portrait changes require reviewing that hash.

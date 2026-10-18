# Catalogue system overrides

Edit `records.tsv` and run `python build.py`. The first seven rows are distance
fallbacks; the following 324 rows are searched in order by system key.
Preserve the complete header and row identifiers. A zero key is reserved for
the generated terminator; these coordinates do not produce one.

`system` is the galactic map name. `sector_x` and `sector_y` are the System
Sector Code, and `number` is the system number in that sector. The name has to
be the map name of those coordinates. The seven fallbacks are all stored as
Sol (`0, 0, 0`); distance chooses among them, and lookup does not search them
by that name. Two keyed rows can share one system. Search returns the first.

`population` is the line the map prints for that band, from `None registered`
through `More than 10 Billion`. `government` is the government-type line, and
`allegiance` is `independent`, `imperial`, or `federation`. The same government
index also selects the economy-type line, so economy is not a separate column.
`low_strength` and `high_strength` are the two strength nibbles, each `0`..`15`.

`resource_richness` feeds mining yield and map status. `category_0_rate` and
`category_1_rate` feed market request generation. `market_profile` is the market
profile word, `market_counter_seed` is the remaining profile-query counter
input, and `economic_factor_index` selects price and supply factors. These are
decimal magnitudes. The names describe observed uses, not physical units.

`map_description_text` names a chart catalogue entry. `system_view_text` names a
systems catalogue entry. `0x8505` is one past the last chart entry and stays a raw word.
The 31 commodity columns use identifiers from `../../text/market/strings.tsv`.
Use `i0`..`i7` for imports, `e0`..`e7` for exports, optionally prefixed by `!`
for illegal goods. Raw `0xNN` tokens retain other bits.

`catalog.ini` keeps the fixed image span of this table. Physical system contents
live in `../handcrafted/`; the catalogue describes economy and information
rather than a duplicate body hierarchy. See
`../../../src/game/world/system_lookup.c` for search and fallback behavior and
`../../../src/game/world/system_answers.c` for query projections.

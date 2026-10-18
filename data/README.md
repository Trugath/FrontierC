# Editable game data

Original bitmap artwork, 3D models, and music under `data/` are not covered by
this project's source license. See [ATTRIBUTION.md](../ATTRIBUTION.md).
`frontier-data repack frontier.zip frontier.dat` writes the dat directly from a zip.
`frontier-data unpack frontier.zip data`, or
`python tools/frontier_data.py unpack-game`, fills the copyrighted files from
a zip or folder of your own Frontier: Elite II install. It refuses a missing
file or a different version before it writes anything. Model indexes, text identifier lists, greeting text, and the portrait layout are
already in this tree. Unpack keeps those files and writes the original catalogues beside their identifier lists. Numeric gameplay tables and commodity names stay in this tree.

The decoded sprites, model sources, intro objects, font, extracted palettes,
MIDI scores, AdLib soundtrack, galaxy density grid, original catalogues, and
setup messages are listed in `.gitignore`. Unpack writes them beside the
indexes, identifier lists, and gameplay tables, which are the files this
repository keeps.

Files under `data/` are the source of truth. `frontier-data pack data frontier.dat`
packs the sprites, both ship model banks, the intro model bank and scene
objects, the font, the boot palette, the composite palette, glyph widths, text,
the galaxy density grid, the AdLib soundtrack, and the nine `music/*.mid`
scores into `frontier.dat`. That is the file `frontier` loads.
`.ROL` scores become `music/*.mid`. Pack writes those nine scores into the
dat. `--sound roland` plays them on the system MIDI device. `--sound adlib`
plays the packed AdLib soundtrack. `--sound speaker`
plays effects, and `--sound none` stays silent. The default is AdLib.
Market, catalogue, systems, navigation, panels, people, the intro timeline,
offences, mining yields, commander preferences, and the engine tables (input,
pointer, pool, motion, contact, position, objects, scene probe, and render
materials) are packed into `frontier.dat`. Edit those sources and pack a new
dat; `frontier.exe` stays as it is. Sine, square root, reciprocal, and
arccosine stay in the executable. Cockpit, chart, station, status, and speaker
tables do too, so a change to those still needs `python build.py`.
The Python tools validate these files, write intermediate JSON under
`build/resources/`, and generate C tables under the selected build directory.
These outputs are disposable. The native build reads `data/` only.

Model banks and text catalogs may omit the original DOS addresses. The packer
then lays the bank out itself and checks that the result still fits the
runtime limits. Sprite files under `graphics/compression/` are optional notes;
the packer rebuilds each sprite from its PNG. `frontier-data` also packs a
record folder when that folder has `assets.txt` and no editable model or sprite
index.

Sprites, decoded model sources, intro objects, `video/font.bdf`,
`video/default.gpl`, `palettes/composites.gpl`, the MIDI scores, the original
text catalogs, the AdLib soundtrack, `galaxy/density.pgm`, and
`setup/messages.json` are the local unpack output.
The indexes and identifier lists next to them are in this repository.
`python build.py` compiles the executable. It packs `frontier.dat` when the
unpacked files are present. Pack those files with `frontier-data` before
playing. A later edit to a packed table is another pack. An edit to the
cockpit, chart, station, status, or speaker tables is another build.

| Change | Source |
|---|---|
| Ship flight parameter records | `models/frontier/ship_parameters.csv` |
| Model geometry, render programs and collision programs | `models/frontier/`, `models/frontier_auxiliary/` |
| System population, economy, allegiance and commodity overrides | `systems/catalogue/records.tsv` |
| Handcrafted system selection | `systems/handcrafted/handcrafted_systems.csv` |
| Bodies, names, orbits and stations in handcrafted systems | `systems/handcrafted/<system>.csv` and matching trailer |
| Procedural body profiles and their rule selection | `profiles/profiles.csv`, `profiles/tables.ini` |
| Named commodity price and supply parameters | `economy/commodity_market_parameters.csv` |
| Economy coefficients and bulletin dispatch | `economy/` |
| Named mining yields and commodity mapping | `mining/yields.csv`, `mining/categories.csv` |
| Station reply routing and mission destination flags | `interactions/reply_routes.csv`, `interactions/mission_destinations.csv` |
| Station screens, Continue actions, widgets, drive names/values and repairs | `interactions/layout.json` |
| Station portraits: sprite placement, part choice and palette views | `composites/`, `palettes/composites.ini` |
| Drive classes and jump outcomes | `drives/` |
| Text and its identifiers | `text/<area>/strings.tsv` and `catalog.ini`. Description and dialogue catalogs are unpacked; `identifiers.txt` is the committed name list |
| Starting ship template and cockpit data | `cockpit/` |
| Cockpit icons, slots, command hitboxes/keys, target labels and drive names | `cockpit/layout.json` |
| Font and boot palette | `video/font.bdf`, `video/default.gpl` |
| Sprites and sprite placement | `graphics/sprites/*.png`, `graphics/el2g.ini` |
| Music and Roland effects | `music/*.mid`, `roland/effects/*.mid` |
| AdLib instruments, pitch curves, and soundtrack | `adlib/patches.csv`, `adlib/pitch_curves.csv`, `adlib/records.tsv`, unpacked from `EL2ADLIB.OVL` |
| Optional PC speaker commands and pitch tables | `speaker/scripts.json`, `speaker/*.csv` |
| Historical setup-check messages | `setup/messages.json`, unpacked from `EL2SETUP.OVL` |

A system's catalogue row describes its economy and information pages. Its
handcrafted body table describes its physical contents. These have distinct
consumers and are linked by the system key. In the catalogue that key is the
map name, System Sector Code, and system number. Change the relevant source;
do not add a second set of overrides in C. The system key table chooses body
templates, including fallback behavior for systems without a template.

Ship parameter rows are keyed by model name. A `ship_parameters` directive in
the corresponding `.model` inserts the row's complete flight-parameter record at the
original model-relative position. The CSV holds each record once. The row includes thrust, mass, capacity, drive flags, crew, missile pylons, combat
points, and signed eye/gun coordinates. Empty trailing coordinates mean that
the model has no corresponding turret record. `name_text` accepts the symbolic
identifier in `text/frontier/strings.tsv`. Unpack writes that catalogue from `FRONTIER.EXE`. Column names follow the verified consumers; values retain native integer encodings,
including packed flags and text selectors. Do not assume SI units or that every
word is a physical measurement. Model directory slots retain stable numeric IDs
because rendering and simulation refer to them by number.

Most resources retain fixed record counts and layout boundaries used by the C
port. The compiler rejects incompatible edits. The migration regression tests
pin the recovered model banks and formerly assembly-backed tables by hash.
Intentional content changes require reviewing and updating those fixtures.

Some older numeric CSVs are still recovery inputs rather than useful authoring
formats. See [AUTHORING.md](AUTHORING.md) for the format policy and remaining
conversion audit; the cockpit README distinguishes its semantic UI layout from
its still-raw emission tables.

Run `python test.py` for authoring and native unit checks, or
`python test.py --integration` for the complete native gameplay suite as well.
The leaf READMEs retain the recovered byte layouts and the evidence for them.

PNG pixels are palette indices. Keep indexed PNG or 8-bit grayscale input;
palette reordering changes game pixels. GPL palettes use RGB8 values, quantized
to VGA RGB6. BDF glyph IDs are game glyph IDs rather than Unicode code points.
MIDI editors can normalize byte encodings even when notes sound unchanged.

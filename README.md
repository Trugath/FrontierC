# Frontier

This is a native port of Frontier to C. Windows uses GDI and waveOut, and the
executables run on Windows 10 and Windows 11. Linux uses X11 and ALSA. The
intro leads into a ship at Ross 154, Sol, or Lave.

The game loads `frontier.dat` from its own directory. That file holds the
sprites, models, font, palettes, text, music, and the packed gameplay tables.
You make it by repacking your own copy of the original Frontier binaries.

## Play

Requires Python 3.10+ and [zig](https://ziglang.org/) (`zig cc`). If `zig` is
not on `PATH`, set `ZIG` to the executable.

Build the game and the data tool:

```powershell
python build.py
```

This writes `build\game\frontier.exe` and `build\game\frontier-data.exe`. On
Linux the names are `frontier` and `frontier-data`. `make` does the same
thing. Before a dat exists, the build prints `frontier.dat was not packed. Unpack a Frontier install, then pack it with frontier-data.`
and still writes both programs.

Repack a zip or a folder of the original DOS install into `frontier.dat`
beside the game, then start it:

```powershell
build\game\frontier-data.exe repack C:\path\to\frontier.zip build\game\frontier.dat
build\game\frontier.exe
```

`repack` checks the twenty-six original files, then writes `frontier.dat`.
A missing file or a different version stops with an explanation and exit code
2. The decoded files stay in a temporary directory that `repack` removes.

`--sound` chooses `none`, `adlib`, `roland`, or `speaker`. The default is
`adlib`. Roland plays the packed MIDI scores. AdLib plays the packed
soundtrack. The PC speaker plays effects. `--assets path` loads a dat from
somewhere other than the game's directory.

The first menu choice starts in Ross 154.

- F7: take off. Later presses change the flight control mode.
- Enter / right Shift: increase or decrease the requested speed.
- A / Z and comma / period: flight axes.
- F1: cycle views. From a map, F1 returns to flight.
- F2: galactic chart, then the current system.
- F6 on the chart: the selected system's information page.
- F7 / F8: zoom a map. F9: map display options. F10: galactic view, or arm an autopilot target.
- Escape: pause. Press again for preferences and commander files.
- Alt+F4 or closing the window: quit.

`--frames N` runs without a window and exits after that many frames.
`--dump-frame path.ppm` writes the last frame.

## Changing the data

To edit the decoded files, unpack the install into `data/` and pack that
directory:

```powershell
build\game\frontier-data.exe unpack C:\path\to\frontier.zip data
build\game\frontier-data.exe pack data build\game\frontier.dat
```

`unpack` keeps the indexes, identifier lists, greeting text, and the portrait
layout already in `data/`, and decodes:

- `EL2G.GRF` into `graphics/sprites/*.png`, keeping the image paths in `graphics/el2g.ini`
- `EL2MCGA3.OVL` into `video/font.bdf` and `video/default.gpl`
- `EL2M13.OVL` into `palettes/composites.gpl`
- `FRONTIER.EXE` into `models/frontier` and `models/frontier_auxiliary`
- `EL2M08.OVL` into `intro/`
- `FRONTIER.EXE` text into `text/frontier/strings.tsv`
- `EL2M00.OVL` into `text/systems/strings.tsv`
- `EL2M02.OVL` into `text/chart/strings.tsv`
- `EL2M03.OVL` into `text/flight/strings.tsv`
- `EL2M08.OVL` text into `text/intro/strings.tsv`
- `EL2M09.OVL` into `text/names/strings.tsv`
- `EL2M10.OVL` into `text/commander/strings.tsv`
- `EL2M14.OVL` into `text/status/strings.tsv`
- `EL2M15.OVL` into `text/offences/strings.tsv`
- `EL2M17.OVL` into `text/mining/strings.tsv`
- `EL2M18.OVL` into `text/interaction/strings.tsv`
- `EL2M19.OVL` into `galaxy/density.pgm`
- `EL2ADLIB.OVL` into `adlib/patches.csv`, `adlib/records.tsv`, and `adlib/pitch_curves.csv`
- `EL2SETUP.OVL` into `setup/messages.json`
- the nine `.ROL` scores into `music/*.mid`

Those decoded files are gitignored. Edit the PNGs, `.model` sources, text
catalogs, palette, and font, then pack again. Commodity names and the numeric
gameplay tables are already in this tree. Market, systems, the catalogue,
navigation, panels, people, the intro timeline, offences, mining, commander
preferences, and the engine tables listed in [the data guide](data/README.md)
are packed into `frontier.dat`, so an edit to those sources is another
`frontier-data pack`. Sine, square root, reciprocal, and arccosine stay in the
executable. Cockpit, chart, station, status, and speaker tables do too, so a
change to those, or to any C source, is another `python build.py`.

When the unpacked files are already in `data/`, `python build.py` also packs
`frontier.dat`. `python build.py --intro` builds the intro host only.

`python tools/frontier_data.py unpack-game` writes an install into `data/`.
`python tools/frontier_data.py pack` packs this repository's `data/` tree.
`frontier-data unpack` of an existing dat, and `frontier-data pack` of a
directory with no `models/frontier/models.ini` or `graphics/el2g.ini`, use the
one-file-per-record form listed in `assets.txt`:

```powershell
build\game\frontier-data.exe unpack build\game\frontier.dat records
build\game\frontier-data.exe pack records build\game\frontier.dat
```

`python tools/frontier_data.py` and `python tools/extract_assets.py` are
developer entry points and use the same messages.

The dat contains the font, the boot palette, the composite palette, glyph
widths, text, the two ship model banks, the intro model bank and scene
objects, the sprites, the galaxy density grid, the AdLib soundtrack, the nine
MIDI scores, and the packed gameplay tables. Model banks and text catalogs may
omit the original DOS addresses. The packer lays those banks out itself.
Sprite compression notes under `graphics/compression/` are optional. The
packer rebuilds each sprite from its PNG.

## Layout

```
build.py              one build command
src/engine/           audio, math, renderer, simulation, session, and the window host
src/game/             world, flight, chart, station, people, mining, commander, intro
data/                 gameplay tables and indexes; artwork is unpacked locally
tools/                table and asset generators the build runs
```

`src/engine/host` is the window, the frame loop, and the bindings between the
two trees. The OPL chip synthesizer under `src/engine/audio/adlib` is Elliot Stirling's C port of the assembly.

## Validation

```powershell
python test.py                  # resource/tooling tests and native C behavior tests
python test.py --integration    # also build and exercise the complete native game
python test.py --python-only    # resource/tooling tests without a compiler
```

Integration checks cover starting configurations, flight, scanner and HUD,
station trade and fitting, ship purchases, missions, hyperspace, maps,
preferences, and commander save/load. The visible Windows input test is opt-in:
set `FE2_WINDOW_TESTS=1` before running integration tests.

C builds use strict warnings and reuse objects only when their source, include
headers, compiler identity and flags match. Build artifacts stay under `build/`.
See [the data guide](data/README.md) for authoritative editable sources.

## License

The C source, headers, and C include fragments authored for this port are
Copyright (c) 2026 Elliot Stirling and licensed under the [MIT License](LICENSE).
That license covers only this source code.

Original *Frontier: Elite II* artwork, models, and other game assets remain the
property of their copyright holders. See [ATTRIBUTION.md](ATTRIBUTION.md).

# Galactic map and system text

`strings.tsv` is the editable source for the 261 entries in the chart text directory: star names, system descriptions, classifications and map messages. Each row contains a stable identifier, one TAB, and text. Entry order defines the numeric IDs used by the game.

Leading and trailing spaces are significant. Nonprintable bytes use `\xHH`; a literal backslash uses `\\`. Formatting control bytes retain their original values; their full meaning has not yet been recovered. NUL terminators are added by the compiler.

`catalog.ini` describes the resource layout and `identifiers.txt` is the committed name list. `frontier-data unpack` writes `strings.tsv` from `EL2M02.OVL`. The build compiles these files into `build/resources/text/chart/catalog.json`, then into the C text tables. JSON is a build product.

The directory contains relative 16-bit offsets and is rebuilt from labels. Its image offset is 0x0504, the first string is at 0x070e, and the resource ends at 0x1b59. Surrounding code still uses fixed addresses, so the build rejects total size changes until those references are recovered. Equal-size edits can be built with `python build.py`; the original project's assembly verification requires the original bytes.

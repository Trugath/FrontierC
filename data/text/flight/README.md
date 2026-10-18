# Flight and cockpit text

`identifiers.txt` is the committed name list. `frontier-data unpack` writes `strings.tsv` from `EL2M03.OVL`. `strings.tsv` holds 57 ordered rows: identifier, TAB, escaped text. Order determines numeric lookup identity; identifiers give readable source names. Preserve whitespace. Nonprintable bytes use `\xHH`, literal backslashes use `\\`, and the compiler adds each NUL terminator.

Formatting commands and marker glyph bytes retain their original values; not every glyph/control meaning is resolved. `catalog.ini` defines the module, directory count and original image bounds. Intermediate JSON and generated assembly are regenerated during the module build. Total size changes require relocating the surrounding code and are guarded.

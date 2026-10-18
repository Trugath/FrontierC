# Offence text

`identifiers.txt` is the committed name list. `frontier-data unpack` writes
`strings.tsv` from `EL2M15.OVL`. Thirteen ordered TSV strings generate the
relative directory at 00A3 and text ending at 0210. Controls use `\xHH`;
literal backslash uses `\\`. The reserved byte at 0210 belongs to the module
layout and is not part of the last string.

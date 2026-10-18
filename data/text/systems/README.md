# systems system descriptions

The 38 ordered entries in `strings.tsv` recreate the relative-word directory at image 0x0241 and its NUL-terminated text through 0x1963. `identifiers.txt` is the committed name list. `frontier-data unpack` writes the strings from `EL2M00.OVL`. Empty-looking descriptions contain one space; they remain separate entries with their original positions. Carriage returns and formatter controls use explicit byte escapes. The build compiles this catalog through JSON into the C text tables.

The display entry at 0x4e19 masks AX with 0x03fe, sets ES to zero, supplies the directory in SI, and calls resident vector 0x5c. The mask admits more indices than the 38 declared entries; callers must supply valid selectors. No bounds check is introduced.

catalogue stores system-view selectors such as 0x801a for Sol, 0x8012 for Alpha Centauri, and 0x8008 for Sirius. chart's selected-system view sends that selector through resident text vector 0x120. Their low selector values agree with catalog entries 26, 18, and 8 and the corresponding description content. The complete resident dispatch path and formatter grammar have not been established here.

Entry names retain their numeric catalog index and a text-derived slug. The catalog order, duplicate space entries, text spelling, control bytes, and directory offsets are preserved exactly.

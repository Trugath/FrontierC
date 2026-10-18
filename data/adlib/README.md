# AdLib native resource sources

`bank.ini` gives the image-relative extent (1871h..67DDh), the pitch-curve
placement, the two mutable ambient operands, and command symbols. It stays in
this repository. `frontier-data unpack` reads `EL2ADLIB.OVL` and writes
`patches.csv`, `records.tsv`, and `pitch_curves.csv`. Those three files are
the original soundtrack and are not part of the source tree.

`patches.csv` gives 150 named, 11-byte OPL patches with individual register
fields. `records.tsv` gives their placement, the instrument/event directories,
event headers, notes, commands, symbolic absolute stream targets and explicitly
unclassified bytes. Offsets are image offsets, excluding the 512-byte MZ header.

The build compiles these editable files to
`build/resources/adlib/bank.json` and validates widths, values, references
and every record offset. JSON is an intermediate file. No original overlay is
read to build the bank. Labels preserve aliases, including the mutable ambient command operands.

The instrument directory has 197 slots. Slot 65 is the original literal value
2, not a patch pointer. The other 196 entries refer to the named patches.
The event directory begins at 2229h and contains 114 words, including duplicate
references and zero entries. There are 76 distinct event headers; their two bytes
are logical channel and signed priority. Note records contain pitch and duration;
an optional third byte is supported by the source schema for per-note modulation.

`tools/adlib_resource_audit.py` follows all recovered event headers with concrete
call stacks, both conditional-loop edges, and the note-width state installed by
command C9h. This is a static audit of possible local paths, not a recording of
game playback. It finds 8,210 instruction starts covering 16,893 bytes. All local
stream opcodes lie within the physical 80h..C9h dispatch range. The original call
at 4CB4h targets 0087h, outside this resource bank; this edge is explicitly
reported and its bytes are preserved. Its use during actual playback remains
unverified.

All event-reachable resource bytes have typed source records. The remaining
1,019 raw bytes consist of 444 bytes in the patch region with no recovered
instrument reference and 575 bytes in the stream region not reached by this
event-rooted graph. Some look like unused patches or stream fragments, but they
remain raw until a consumer or independent evidence establishes their meaning.
The build does not guess patch boundaries, repair the external call, or discard
unreferenced data.

## Pitch bend curves

`pitch_curves.csv` contains fourteen curves as spreadsheet rows. `curve` is the
ordered index 0..13; `bend_00` through `bend_31` are the byte-sized frequency
deltas read by the driver for each bend step. Values are not normalized floats
or MIDI controller values. Edit a numeric cell without calculating addresses.
The compiler constructs the pointer table and preserves the intervening zero
storage. It does not smooth the recovered non-monotonic entry in curve 12.

The old packed hexadecimal `pitch_tables.csv` has been replaced. The compiled
pitch image is pinned by a migration hash and remains byte-identical.

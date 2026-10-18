# Editable model renderer programs

All 353 resident, auxiliary and intro `.model` files now supply
`[program].instructions`. The model compiler creates the original byte arrays
as JSON intermediates and then emits the original module includes. Program
lengths, relative pointers, model boundaries and contact insertion offsets are
still checked by the existing bank compilers.

```ini
[program]
instructions =
    parameter_control token=0xffe6 words=-
    update_parameter token=0x761a words=0x8200,0x0760,0x0760,0x0760,0x0760,0x0760,0x0760,0x0760
    adjusted_bitmap_surface token=0xe327 words=0x0002,0x0402
    bitmap_surface token=0xe323 words=0x0002,0x0304
    point_list token=0x9898 size=fixed:0@0x0040 material=rgb12:0x888 inherit_material=false points=0,2|4,6|1,3|5,7|end,end
    raw 0x03 0x00 0x03 0x02
```

`token` preserves all sixteen bits of the command word. Its low five bits
select the operation, and the upper eleven bits retain packed selectors and
flags whose complete domains are still under investigation. `words` gives
the following inline little-endian word operands; `-` means none. `raw` is an
explicit sequence of byte values. Lines allow `#` comments. The compiler
rejects unknown operations, a token selecting another operation, duplicate or
unknown fields, out-of-range values and incorrect operand counts. `truncated=true`
is reserved for a command whose bounded model fragment ends before its declared
operands or geometry extent; it preserves available words/bytes without
claiming a complete command boundary.

The source names describe handler behavior rather than inferred object names.
All 32 command dispatch entries have a source operation. Conservative decoding
recovered 10,732 complete command occurrences covering 60,748 of 62,426 renderer
bytes; this measures bytes with proven boundaries, not complete model semantics.
There are no truncated commands or stopped reachable paths in the current
audit. A further 1,350 bytes are past independently extracted contact-program
insertion points. The 1,678 bytes still raw are outside complete command spans.
Another 236 bytes formerly stored in those arrays are now independent native
contact-offset resources: they are data consumed backward by contact handling,
not renderer instructions. The original combined storage remains 62,662 bytes.
Correcting slot28 to consume its phase word on both signed-token paths cleared
the previously reported point-list, polygon and truncated-geometry failures:
those were downstream decoder misalignment, not malformed model records.
The auxiliary character-instance directories establish 94 distinct directory
targets across two models and recover another 3,276 formerly raw bytes.

Their `[extension]` sections keep the 16-byte prefix in `bytes` and expose
`instance_programs` as rows of `byte_selector, program_relative_offset`.
Selectors start at 20h and are contiguous; duplicate offsets preserve character
aliases. The compiler adds the model-relative program base to each offset to
recreate the original header table. The decoder traverses these entries as
well as the default entry and conditional targets; it does not infer new
entries merely because a terminal suffix resembles opcodes.

`[contact_offsets]` references `contact_offsets/*.basis_offsets` resources.
These selector/length pairs immediately precede the contact program in the
compiled model, but are removed from `[program].instructions`. Its compressed
`program_offset` must equal `[contact].program_offset`; the build inserts the
pairs before contact code and checks the resulting model-relative pointer.
Each pair is a selector and a length, and the list ends at a shared zero terminator.

Slot24's render path can end on a word whose high byte is7Fh between paired
points; its discard path scans until the low byte is7Fh. A low-byte7Fh word
is therefore a common boundary and is decoded even when its high byte differs.
The first packed point word enters projection directly: its high byte is a
signed vertex index, including 7Fh (vertex127), rather than a terminator.
Only subsequent words pass through the high-byte sentinel check. A high-byte-
only7Fh marker in a subsequent word gives different continuation cursors, so
that stream and its suffix remain raw rather than inventing a common boundary.
The coverage and stopped-path counts are reproducible with
`python tools/audit_model_render_sources.py`.

| Operation | Slot | Additional words | Evidence |
| --- | ---: | ---: | --- |
| return_stream | 0 | 0 | 7E62h discards dispatcher return address |
| inline_curve | 1 | 2 | 492Dh, two ES:LODSW instructions |
| line_segment | 2 | 1 | 51BDh |
| bitmap_surface / adjusted_bitmap_surface | 3 / 7 | 2 | 54C4h / 549Ch; adjusted entry rereads same operands |
| textured_quad / adjusted_textured_quad | 4 / 8 | 3 | 5C6Fh / 5C42h; adjusted entry rereads same operands |
| polygon_value_sequence | 5 | variable | 67E7h; visible-normal path ends at nested slot0, rejected-normal path can skip unsigned DH bytes |
| parameter_control | 6 | 0 or 1 | 4265h; parameter0/7FEh/7FFh consume0, 7FDh consumes1, otherwise bit100h determines consumption |
| vertex_pair_surface | 9 | 2 | 71EDh, two ES:LODSW before saving the caller stream |
| model_instance | 10 | 3 | 78D8h, rejection skips last two words; accepted path reads them and restores caller stream |
| scaled_threshold / inverse_scaled_threshold | 11 / 12 | 1 | 7DFFh / 7E85h |
| binary_expression / binary_expression_alternate | 13 / 29 | 1 | binary operand selectors are packed in one word |
| nested_model_context | 14 | 1 or 3 | 7C0Ch; first operand sign gates four inline vertex-index bytes |
| position_duration_sound | 15 | 0 | 80D0h invokes SOUND_PLAY_DURATION_DS with token>>5 and a position-derived duration |
| geometry_four / geometry_six | 16 / 17 | 3 / 5 | 81ABh / 812Ah; first geometry word is in token |
| projected_vertex_text | 18 | 1 | `render_projected_vertex_command.inc` reads one word and advances SI by two |
| context_mask / excluded_context_mask | 19 / 20 | 1 | 7EC4h / 7F23h |
| curve_batch | 21 | 0 | 4840h; both positive and signed entries retain stream cursor |
| four_vertex_lines | 22 | 3 | `render_four_vertex_command.inc` |
| vertex_distance_filter | 23 | 2 | threshold word and two vertex-index bytes |
| point_list | 24 | variable | 8F60h; parameter then point words; render and discard paths test different 7Fh bytes |
| update_basis_axis | 25 | 0 | `render_basis_axis_update.inc` retains stream pointer |
| update_parameter | 26 | 1 or 8 | mode word AH!=0 skips seven lookup words |
| conditional_nested_model | 27 | 2 or 4 | 7B6Bh; first operand sign gates four inline vertex-index bytes |
| load_basis_columns | 28 | 1 | signed tokens read phase at777Ah; nonnegative tokens load columns then read phase at7771h |
| configure_context | 30 | 0 | signed context iteration passes explicit registers to helper entries rather than consuming inline operands |
| geometry_pipeline | 31 | two header words plus declared span | 93C6h early release adds `(token>>4)&FFFEh`; normal cursor follows parser consumption |

The dispatcher at B207h masks words with 1Fh. The seven value-token handlers
are reached through a separate caller and are **not** ordinary command slots.
Conditional filters can skip commands using `(token>>4)&FFFEh`; these bytes
are retained as source commands only where sequential boundaries are proven.
This source format does not claim to reconstruct the runtime control-flow
graph or establish the meaning/units of every packed operand.

The nested polygon value grammar selects slot `(word>>9)&7`. Slots0..6 consume
0,2,1,0,1,0,1 additional words, respectively. The normal-visible path executes
these handlers through the nested slot0 terminator. Normal dispatch through
28A4h and rejection dispatch through2902h have identical widths: the latter
skips operands that the former evaluates. Separately, when the runtime normal
lookup rejects the polygon, the DOS handler can advance by unsigned `DH` bytes
from the cursor immediately after the polygon header word. This alternate
target is independent of the visible path's slot0 location; it need not rejoin
the same boundary. The control-flow decoder follows both entries and retains
them when their instruction spans do not overlap. An overlap falls back to the
conservative sequential entry prefix rather than inventing labels inside an
operand. Point lists stop at a shared
low-byte7Fh marker. A high-byte-only7Fh marker after the first packed word can end the normal render path
before the discard scan, so such streams retain raw source from that command
onward.

Complete point lists separate the decoded `size` operand from `points`.
Each `high,low` pair records two signed byte vertex indices in projection
order (the high byte is visited first), separated from the next pair by `|`.
`end` encodes a 7Fh sentinel; it is required wherever the handler recognizes
that byte as a marker. The first high byte can instead be vertex `127` because
it bypasses the high-byte sentinel check. A final `vertex,end` projects that
high-byte vertex before stopping; a subsequent `end,end` stops before either
vertex is projected. Negative indices retain the handler's signed indexing
relative to the runtime vertex-table base. `size=fixed:0@0x0040` names the
literal screen-size mode and magnitude; `depth_scaled:128@0x4000` names the
nonnegative literal mode. `frame:-64@0x00c0` and `object:114@0x0080` name word
address offsets relative to BP and the saved object base. The original word
after `@` preserves ignored-bit aliases; the compiler validates its decoded
meaning. Indirect operands choose fixed/depth scaling from the loaded word,
not the encoded selector's sign. `material=rgb12:0x888` or `table:0x190` names
the token's RGB12 value or table byte offset; `inherit_material` names addition
of the parent material accumulator. These fields are checked against `token`.
The compiler accepts the older `words` and `parameter` forms during migration.
Addressing wraps, and the encoded selector is distinct from the runtime-resolved material.

Complete slot5 bodies expose the header's low-seven-bit `normal_index`, bit-7
`skip_if_z_clipped`, and high-byte `skip_bytes`, while retaining the whole
header word. The alternate skip is selected by the runtime normal-visibility
result; it is not selected by a sign bit in this header. FE2's renderer
reference uses the bit-7 flag to abandon the
remaining polygon spans when an edge endpoint crosses the near Z clip plane;
otherwise it clips that edge. DOS's alternate path advances by the unsigned
high byte from the cursor after the header. See [`RenderComplex` in
fe2-intro](https://github.com/watsonmw/fe2-intro/blob/master/src/render.c).
The `commands=` field names nested subcommands and shows decoded vertex indices
or circle parameters. Each descriptor also retains every original word after
`@`; the compiler checks that displayed fields decode from those words. The
names follow the matching seven-entry complex-polygon table in
[`fe2-intro`'s model compiler](https://github.com/watsonmw/fe2-intro/blob/master/src/modelcode.c):
`end`, `bezier`, `line`, `line_cont`, `bezier_cont`, `line_join` and `circle`. The
operand widths match the DOS dispatch table. This remains a cross-version
label inference; command words remain authoritative, including unused or
version-specific bits. The adjacent slot7 table fall-through is labeled
`adjacent_operator` and likewise retains its word without inferred fields.

The [archived Galaxy VII format notes](https://web.archive.org/web/20220117065108/http://www.jongware.com/galaxy7.html)
describe **FFE**, not FE2, and explicitly warn that its model commands only
derive from FE2 with version differences. Their polygon header describes a
byte extent followed by a normal index, and their nested path opcodes use
byte-valued command IDs. In particular, FFE opcode `0x0A` is documented as
`EndSegment`, which terminates one contour so a new line or curve can begin;
the example uses this to create the hole in the vector-font letter O. FE2's
corresponding subcommand-table slot is called `LineJoin` (`cjoin`) in
fe2-intro. Its FE2 renderer closes the current contour to its starting vertex
when the contour state requests closure. That is a distinct operation from
FFE's `EndSegment`, despite sharing the same table slot and having no trailing
operand. The FFE page is useful evidence for the family of contour operations,
but its name must not be applied to the FE2 command. The recovered DOS words
and FE2 table remain the authority for FE2 source.

`main_model_036` demonstrates why a conflicting slot5 control-flow region
cannot be recovered as one linear block. At program offset0234h, tokenD245h and
parameter833Ch lead the visible-normal parser to its slot0 terminator at0238h,
whose normal continuation is023Ah. If the runtime normal lookup rejects the
polygon, the handler instead advances from the post-header cursor0238h by83h
bytes to02BBh. That alternate entry decodes four complete commands through
02C9h before reaching bytes that do not have a proven command boundary. The
visible and rejected-normal paths overlap later command interpretations, so the
canonical linear source keeps this conflicting region raw rather than choosing
one path.

Parameter control is no longer classified as state-dependent consumption:
`token>>5` is written to1F3Dh before its high byte at1F3Eh is tested. Bit100h
therefore selects the extra-word branches at4484h/45D5h/4753h directly from
the token. Special7FDh jumps to4691h and consumes one signed depth word;
0/7FEh/7FFh consume none. Meanings and numeric units of these variants remain
open even though their storage grammar is recovered.

Each recovered block stops at an unsupported or conflicting grammar,
truncated operand, or return-stream command. The migration also follows proven
conditional skip targets from slots11/12/19/20/23. Those targets are relative
to the cursor after each handler's inline operands, by `(token>>4)&FFFEh`
bytes. A zero skip discards the dispatcher return address rather than adding
a new branch. Thus a block beyond a return is recovered only when a known
conditional branch or instance directory entry reaches it. Comments identify the byte offset of every
recovered command. Overlapping interpretations fall back to the entry prefix
instead of inventing labels inside another instruction's operands. Branches
are bounded by independently extracted contact insertion points and by the
model boundary.

No arbitrary word in a raw suffix is relabeled as a command. A stop can mean
runtime-dependent cursor consumption, conflicting reachable interpretations,
or an operand extent beyond the bounded model body. The audit lists each
stop by dispatch slot and cause.

## Geometry payload sources

Slot31 references29 editable `geometry/*.geometry` files. The first two inline
words are retained beside the token. Its token declares the byte distance used
by an early release path; on normal completion the parser's actual cursor
remains in use. The compiler checks the extracted early-path span against that
declared extent while keeping native geometry syntax authoritative.

A geometry body begins with four primary words. Flag1000h adds a29-word
selection block. If that flag is absent and4000h is clear, four secondary words
follow. An independent `layer_base` word controls whether scale/color layer
pairs follow; a zero base skips that list, otherwise a zero scale terminates it.
The body then contains signed-tag records. High-bit tags identify four-byte
vector/phase records. Positive tags describe curves with a selector, signed
three-component start and point records, optional isolated points, and a zero
terminator. Zero X is the isolated-point form; nonzero X begins or continues a
curve. `end_records` terminates the outer record collection and any following
byte is explicitly represented as alignment `padding` when its placement is
proven.

The29 files contain28 fully decoded bodies:33 vector/phase records,69 curves,
334 curve points,3 isolated points,55 layer pairs and20 alignment bytes. Their
word blocks include28 primary blocks and19 secondary blocks;4 complete bodies
also contain selection blocks. The remaining two declared bytes are an
explicitly raw intro body whose normal-path grammar has not been established.
The three former truncated files came from misaligned command decoding and
have been removed. `tools/model_geometry_resources.py`
validates tags, signed vector ranges, curve ordering, terminators, layer rules,
padding placement and total declared lengths.


`tools/model_render_programs.py` compiles and conservatively decodes this
format. `tools/extract_model_render_sources.py` is an idempotent migration tool
that uses the editable sources themselves, requires no original binary, and
asserts byte preservation per model. Codec tests validate operand lengths,
dispatch slots, special grammars, odd raw suffixes, conditional branch targets,
overlapping operands, and all 353 source models.

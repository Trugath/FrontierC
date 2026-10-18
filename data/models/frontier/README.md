# Resident main model bank

`models.ini`, `model_directory.tsv`, and `ship_parameters.csv` are in this
repository. The `.model`, geometry, contact, and basis-offset files are
written by `frontier-data unpack` and are gitignored.

Edit flight statistics in `ship_parameters.csv`. Its 41 named rows include
ship-name text identifiers, crew, missile pylons, drive options and view/mount
coordinates. A `ship_parameters` directive in each applicable `.model` inserts
that row. The full statistics tails are stored once; C and model-bank bytes are
unchanged by the migration. See `../../README.md` for the authoring workflow.

The editable bank covers image offsets `412d2..512c0`: a word bank size, 240 relative directory words, and 230 distinct models. Ten directory entries are zero. The preceding zero word at `412d0` belongs outside this bank. The twelve-byte ASCII trailer at `512c0..512cc` is preserved separately; its purpose remains unknown.

Evidence: initial resident DS:07fa contains segment 412d. Service 18de sets the model directory offset to 4. Service 18b5 reads a relative directory word and adds that directory base. Every model independently satisfies the layout already recovered in intro: fixed 16-byte header, optional 14-byte extension, four-byte encoded vector records, four-byte classification records, and renderer bytecode. All 230 models satisfy vertex storage bytes = working-buffer size /16 and classification storage bytes = header count -4.

`models.ini` and `model_directory.tsv` establish order and layout. Each `.model` contains editable header words, the extension, encoded vectors, classification records, and renderer program instructions. Pointer and size fields are reconstructed. JSON is intermediate only. Slots are named by directory index; object identities and complete renderer operand semantics remain to be recovered. Scale/culling labels follow the established intro resource vocabulary and are not new runtime observations.

A direct compile and roundtrip check reproduced all 65,530 bytes through the file trailer exactly. The renderer source now names proven instruction prefixes and preserves undecoded suffixes explicitly. This does not constitute recovered C or complete model semantics. See [the renderer source format](../RENDER_PROGRAM_FORMAT.md) for supported operations, evidence and coverage.


## Contact program source

The74 models with nonzero header1Ah contact pointers now use editable
`contacts/*.contact` files (194 records). `[contact]` identifies a bank-relative
file and an insertion offset into the model program. `[program].instructions` holds
only the remaining prefix/suffix; the compiler inserts the compiled contact
stream before rebuilding the existing JSON/model include. Header-pointer and
original model-boundary checks still apply. No game binary is a build input.

Each line names one interpreter payload, retains its original tag/result byte,
and supplies explicit operands. For example:

```text
planes_all_le tag=0x10 result=0x80 refs=2,3,4,6
child tag=0x81 result=0x02 model=16 transform=0x00 vertex=2
end
```

`planes_all_le` and `planes_all_gt` describe the plane predicates tested by the
ordinary/inverted list paths; `result_planes_all_le` applies a result when its
plane list passes. `child` references a model directory slot with a transform
byte and vertex reference. Conditional tags20h require `field` and raw `shift`
operands. Field words come from target72h+2*index; shift80h inverts the test.
Unknown tag/result meanings remain numeric rather than invented gameplay names.
Lists and whole programs have separate zero terminators, emitted by the codec.
The contact stream is reached both by the resident model-contact interpreter
and by the scene-probe geometry walker through each model's relative pointer at
header extension offset1Ah. These callers consume the common tags and nested
records for different queries; the interval walk's endpoint/state semantics
remain a separate resident-code contract.

The shipped programs contain114 child records,29 result-plane lists,27 ordinary
lists and24 inverted lists. Child slots range16..210; observed permutation
selectors are0,2,3,4,5. Conditional field indices are1,2,23. Plane references
start at2. These are resident-resource observations, not universal emulator
bounds or proof about dynamically loaded model banks. Vertex/normal X mirroring,
truncated dot products and recursive query behavior are implemented in
`src/engine/contact/model.c`.

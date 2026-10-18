# Native intro intro resources

`models.ini` and `model_directory.tsv` are in this repository. The 109 `.model`
sources, their geometry files, and the 23 `.object` sources are written by
`frontier-data unpack` and are gitignored. `models.ini` orders those models
in the original bank layout. `model_directory.tsv` defines 240 ordered slots; `-` emits the original zero entry. Models use directory-slot names until their visual identities are established.

Each model source has five sections:

- `header`: scale shift, neutral word +0Ch and culling radius.
- `extension`: any bytes between the base header and packed vertices.
- `vertices`: four-byte packed-source records, one per row.
- `classification`: four-byte packed coefficient/reference records, one per row.
- `program`: the renderer bytecode stream, explicitly retained until its complete grammar is recovered.

The build derives the remaining header words:

| Offset | Generated field | DOS evidence |
| --- | --- | --- |
| +00h | Relative program offset | FRONTIER.EXE B2DBh..B2E1h adds it to the model pointer before interpretation |
| +02h | Relative packed-vertex offset | B27Ah..B282h prepares the far packed-source pointer |
| +04h | Vertex working bytes: packed vertex bytes times sixteen | B28Eh..B294h allocates this buffer; EL2MATH3 generates paired 32-byte records from four-byte sources |
| +06h | Relative classification-source offset | B2ABh..B2B1h stores it at renderer frame BP-E0h |
| +08h | Classification working bytes: packed classification bytes plus four | B298h allocates it; EL2MATH3 DS:030Ch seeds and fills the buffer |
| +0Ah | Source scale shift | 3E4Fh..3E53h supplies renderer frame BP-D4h |
| +0Ch | Neutral preserved word | Broader consumers remain untraced |
| +0Eh | Source culling radius | 4186h..4193h shifts it by the model scale for rejection tests |

These relationships agree across all 109 models. Five models have fourteen extension bytes; all others start packed vertices directly after the sixteen-byte header. Relative directory pointers, model-internal offsets and the bank-span word are generated symbolically, with guards retaining every original model boundary.

`objects.ini` defines 23 `.object` sources: one initial orbit object and 22 scene objects. Each source has a three-row orientation matrix, renderer offset dwords, model slot, mutable phase and sparse state-word assignments. Unspecified state words become zero. The compiler generates exactly 286 bytes, rejects duplicate/reserved/unaligned state offsets and derives the word +5Ah selector as twice the directory slot. Hexadecimal fields represent original bit patterns; signed interpretation belongs to their consumers.

`timeline.ini` and `scene_timeline.csv` define the intro timeline. Scene 00 windows the raw resident time (DS:1FC1h); the orbit interval of `orbit_length` follows it; scenes 01..21 window the time left after both are subtracted, first match wins and no match restarts at time 0400h. Inside a window the scene object's phase word +72h becomes `(((time-start)<<4) mod 2^32) DIV divisor + phase_bias` (16-bit). The compiler rejects windows whose quotient could exceed FFFFh. Time units remain unresolved.

`tables.ini` supplies the initial three-word render vector and eight classification values through typed CSV. Intro/title/sign strings live separately in `data/text/intro/`.

Native inputs compile through intermediate JSON under `build/resources/intro/`. No original binary is read by the normal build. The intro models themselves are the local unpack, and they are not packed into `frontier.dat`.

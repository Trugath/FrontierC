# Handcrafted systems system templates

These editable CSV files recover 133 object records across eleven keyed systems and one fallback list. They replace 8,632 literal data bytes at image 0x1963..0x3b1b. The original source format and authoring tools are unknown.

Each object row names the fields the loader and the chart read. `size`, `seed` and `scalar` are the dwords the loader normalizes, written whole. `parent` is the body this record hangs from, or `frontier` for the static Frontier object. `kind` is `root` for the root class, `large_shape_7` for a large body drawn with shape 7, and `verbatim` for a city or port copied unconverted and placed beside its parent. Every other kind is the decimal body class, the same integer as a row of `profiles.csv`. `descriptor` is a model in `models/frontier/model_directory.tsv`, or a bare number when that slot has no name. `temperature_c` is the Celsius value the chart prints; the stored word is that value plus 273. `orbit_shift` and `generation` are the two bytes at the end of the record. Bit 7 of `generation` marks a generated name; these templates leave it clear. `orbit_longitude`, `orbit_inclination` and `basis_seed` stay words on the circle the orbit code uses. The name is printable ASCII in a 22-byte slot. Derived orbit words, the runtime object pointer and the reserved slots are zero in every template, so they are not columns; the loader fills the derived words and the pointer.

Each list ends with a separate ten-byte trailer CSV. `terminator` is zero; the loader writes that zero to the next destination object. The next four bytes are the name-generator counters `named_bodies`, `root_letter`, `moon_digit` and `moon_letter`. The last two bytes are `station_count` and `sol_station_count`. `root_letter` and `moon_letter` are counters the name generator increments, not the letters it appends.

`handcrafted_systems.csv` maps eleven systems to these lists, in search order. A row is the System Sector Code, the system number, and the template name. An unmatched nonzero key selects the fallback. The sentinel occupies only four bytes: the code reads a pointer at sentinel +4 before testing the zero key, overlapping the following RETF and MOV opcodes. Input key zero therefore selects the overlapping word 0xa3cb. This behavior is preserved rather than repaired; normal caller exclusion of zero has not been proved.

The loader at image 0x40f9 advances source and destination by 64 bytes per object. Records with word +0x0a equal to 0x46 are copied verbatim. Other records undergo conversion, including calls through resident vectors 0x2c4, 0x2b4, 0x288 and 0x2c8, selective field copying, derived words at +0x14/+0x16, and a NUL-terminated name copy. The external math services now use the recovered shared ABI; the complete conversion chain and physical units still need analysis. This is not a complete memcpy-based reconstruction of the loader.

`tables.ini` declares the fixed image spans. The compiler passes these native files through generated JSON and C tables; no original executable is consulted by the build. A name must be printable ASCII and leave room for the NUL terminator; the slot is padded with zeros.

Descriptions at image 0x0241..0x1963 are editable text resources under `data/text/systems`. The object at 0x3b1b is the Frontier body in `data/profiles/static_object.txt`.

Generation callbacks reuse four Sol records as templates: Daedalus (index 2), Abraham Lincoln (index 12), Apollonius City (index 16), and Galileo (index 17). Their code references now use symbolic aliases relative to the Sol list. Preserve the ordering of these rows when editing; these are code-visible template positions.

## Object instantiation pass

The recovered code is split into `handcrafted_system_loader.inc` (image 0x40f9..0x4275), `system_object_instantiation.inc` (0x4275..0x468a), and `system_record_fields.inc`.

The record parent word at +8 is one-based: a nonzero value selects the list base plus `parent * 64 - 64`. During instantiation the code reads that parent's runtime far pointer from compact record +0x18. Zero selects the static Frontier object. Each successfully allocated runtime object publishes its far pointer back into the compact source record at +0x18/+0x1a.

The pass stops on a zero leading source dword or when resident vector 0xac signals allocation failure. The allocation request uses AX=source descriptor (+0x0c) via DS:0x7c0 and writes 0x1d to DS:0x7bc. Success obtains the runtime object from DS:0x7e0. The eight saved stack bytes are discarded on either stop path.

Confirmed transfers include source dword +4, rotated right 16 bits, to runtime +0x76; source bytes +0x3a/+0x3b to runtime +0x9e/+0xa1; source words +0x20/+0x3e to runtime +0xb6/+0xa2; and source dword +0x0e to runtime +0x86. Source kind +0x0a controls additional initialization. These are data-flow contracts, not complete physical field definitions.

Source kind 0x46 takes the special initialization path after parent setup. That path sets runtime flag byte +0x118 bit 0x20, with further descriptor-dependent flags and shape setup. Other parented records invoke resident math services for time and basis calculations. Both paths eventually copy the compact record's NUL-terminated name at +0x22 to runtime +0xf6, then advance the source by 64 bytes. The copy has no length bound; native resources enforce a 22-byte source name slot.

The descriptor-specific setup and coordinate conversion still require combining the recovered math contracts with caller data. The native C implementation and integration checks live in `src/game/world/` and `tests/`; the remaining discussion records the original recovery evidence.

## Recovered math calls in the loader

systems now uses the shared `math_abi.inc` names for 36 calls into the recovered `math` services. The unsigned-pair normalizer (DS:0x2c4, math image 0x1db6) takes N=DX:AX. For nonzero N, let k=floor(log2(N)); it returns DX=floor(N * 2^(14-k)) and BX=k+1. For zero, DX=0 and BX=-7. AX is shifted scratch, not a retained low mantissa. A standalone check compared this formula with the original word-by-word shift sequence on 10,008 boundary and random inputs.

For ordinary compact records, the leading dword is decremented modulo 2^32 before this normalization; the loader then adds 63 to BX and stores DX/BX at record +0/+2. A leading value of one therefore becomes mantissa zero with exponent 56. The second input dword at +0x0e is normalized without decrement, receives exponent bias +25, and is stored at +0x0e/+0x10. A zero second input retains exponent 18. Physical units for these encoded values remain unresolved.

The later parent-dependent calculation chains signed mantissa multiplication, exponent-adjusted division, and lookup rescaling. Object instantiation uses unsigned-word normalization, unsigned conversion, lookup-basis builders, wide subtraction/multiplication, interpolation, and component rescaling. Those calls now have their recovered service names; their complete higher-level physical interpretation still needs analysis. The service contracts live in `src/engine/math/math3.h`.

## Parent-relative scalar calculation

For a nonzero one-based parent index, the loader resolves the parent within the converted destination list. It increments the child's normalized scalar exponent when parent kind is zero and the signed 16-bit byte distance `parent - child` is at least -128. This includes a parent exactly two records earlier. Pointer subtraction wraps as a word; no independent ordering or bounds check is added.

Let m be the child scalar mantissa, e its original exponent, and e' the possibly incremented exponent. The register sequence starts BX=e and computes h1=high16(2 * signed16(m) * signed16(m)), then h2=high16(2 * signed16(h1) * signed16(m)); each doubled product wraps modulo 2^32. The resulting exponent in BX is e+2e' modulo 2^16. These high-word truncations are part of the algorithm, so replacing it with one full-precision cubic expression would change results.

The next service divides using the parent's leading normalized mantissa/exponent. Lookup rescaling then returns AX plus an adjusted exponent DX. The loader stores that exponent at compact +0x16 and high16(2 * signed16(AX) * 0x5edb) at +0x14. For a record with parent index zero, the two derived words are both zero and this calculation is skipped. The physical meanings of the scalar, lookup, and constant are still unresolved.

`system_generation_dispatch.inc` now separates procedural root generation from profile initialization. A nonzero mode word selects object instantiation. In generation mode, an input leading dword with zero upper word selects handcrafted loading; other values initialize the two random-state dwords and enter the procedural paths. The comparison uses a zero-extended low word after EAX was cleared, rather than testing the full input directly against zero.

## Handcrafted system keys

`handcrafted_systems.ini` and `handcrafted_systems.csv` define the keyed
template table at image 3ED9h. Each row gives the System Sector Code and system
number of a template section of `tables.ini` (named without `system_template_`).
`tools/handcrafted_system_keys.py` emits six-byte records and a zero-key
terminator. Unlisted keys select the fallback template. Key 0 matches the
terminator and reads its template word from the following code bytes
(RETF/MOV at 3F1Fh), not the fallback. Adding a row moves the following code,
which the layout guards reject.

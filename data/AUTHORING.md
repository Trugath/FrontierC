# Authoring formats and recovery status

A standard container is not sufficient: numeric memory dumps in CSV are still
recovery inputs. Authoring data should describe the domain, use named references
to other resources, and let compilers calculate offsets, selectors, directory
indices and terminators. The source format should suit the data:

| Data | Authoring format |
|---|---|
| UI layout and declarative actions | JSON with a schema, named sprites/text/actions, pixel coordinates |
| Sound command programs | Structured command scripts with named patterns and validated control flow |
| Music and MIDI effects | Standard MIDI files |
| Raster art and palettes | Indexed PNG and GPL |
| Fonts | BDF |
| Tabular game parameters | CSV/TSV with named columns, units/encodings documented, symbolic references |
| Model geometry and render/contact programs | Existing model scripts and typed geometry sources |
| Object defaults | Named object fields and symbolic model/equipment/behavior references; still to recover |
| Mathematical lookup samples | Numeric tables are appropriate when their index, scaling and interpolation semantics are defined |

`cockpit/layout.json` replaces six cockpit word/byte dumps. It compiles into
the UI tables used by C and separately into the old image layout used for
byte-preservation checks. `tables.ini` image offsets are compiler metadata;
they should not be exposed as ordinary content-editing decisions.

`interactions/layout.json` replaces ten station display/widget/drive/repair
dumps with named screens, text references and declarative widget actions. It
also removes source-level aliases between action messages and page sizes.
The unused initial selector is `interactions/selected_interaction_text.txt`.
Native station generation does not emit it.

`composites/male_parts.csv`, `female_parts.csv` and `headgear_parts.csv`
place each portrait sprite by the image stem in `data/graphics/el2g.ini`.
Hair, eyes, jaw, nose, mouth, chin, collar, shoulder and headgear tables
name those sprites. Palette tables name views in `data/palettes/composites.ini`.
Bank slots, scaled selectors and palette addresses are calculated while compiling.

## Tables that stay numeric

These are samples or packed words. The index and the scaling are known. They
are not command scripts, and a guessed unit would be wrong.

| Source | What the number is |
|---|---|
| `cockpit/emitted_object_variants.csv` `parameter` | The model parameter copied onto the spawned effect |
| `maps/catalog_systems.csv` `identifier` | One word. After the chart swaps its bytes, some bits select the star-type text and both bytes select a value window. It is not split |
| `video/scanline_shape_profiles.csv` | Fifteen half-widths and a 255 stop. Row order is the shape-bank order |
| `profiles/saturated_profile_curve.csv` | 256 samples. `fe2_systems_curve` reads `position >> 6` while position is below 0x4000, and returns 0xffff above that |
| `maps/sector_sample_weights.csv` | 127 weights. Density hashes the sector into an even index and multiplies by this word |
| `main/frontier/main_atan_octant.csv` | 128 samples for one octant. No closed form matched the words, so the samples stay |
| `speaker/pitch_divisors.csv` | Note 0..255 and the PIT channel-2 divisor. Frequency is 1193182 / divisor |

Sine, square root, reciprocal and signed Q6 arccosine are generated. A calendar
month table generates the 366 day words. See the directory READMEs for the
formulas.

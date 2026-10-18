# people object resources

`tables.ini` compiles editable CSV values through intermediate JSON. The build
validates type ranges and exact lengths, then writes generated C under `build/`.

- `object_template.txt`: the Frontier body at image 01B3..02D0. Its name is
  Frontier, its orientation is the near-unit axes, and its model is `main_model_052`.
- `object_parameter_tables.csv`: numeric byte/word subtable region 02D1..036B.
  It is stored as bytes because the region mixes widths. Consumers use bases
  02D1, 02E1, 02FD, 030C, 0350 and 035F; gameplay meanings are not yet proven.
- `variant_object_values.csv`: 26 little-endian words at 036C..039F. Variant
  setup indexes the first part using `(selector & 0x1E) + bias`, with bias 0/8.

State callback tables remain symbolic assembly in `object_state_tables.inc`.
CSV values are recovery source, not generated or original-overlay dependencies.

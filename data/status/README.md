# Commander-status numeric resources

14 editable CSV tables compile through `tables.ini` and intermediate JSON.
The compiler validates type ranges and exact lengths. Generated assembly and
JSON are disposable; the original overlay is not a build input.

- `status_object_template.txt`: the Frontier body at 00A3..01C0, model `main_model_090`, copied for a mission target.
- Equipment and quantity text selectors name entries in the interaction and status catalogues.
- `status_screen_widgets.csv` is the list-page hit boxes. `row_0` through `row_25` send keys 0x80..0x99. `refuel` sends 0x66. The compiler writes the 0xffff stop.
- `contract_target_models.csv` names the model spawned for each mission target class.
- Criminal and combat thresholds, page overlay sprites named from `graphics/el2g.ini`, and inventory parameters.
- Cargo object subtypes, military-contract catalogue text, and contract rank parameters.
- Thirteen military rank thresholds, stored as little-endian words.

The 16-row chance/callback table at 1103 uses symbolic near pointers in
`contract_creation_callbacks.inc`; empty rows retain zero threshold/pointer.
Its subtype index is masked, and zero thresholds suppress callback execution.
Text is in the separate native TSV catalog under `data/text/status`.
Some byte-table meanings remain provisional; see the module semantic notes.

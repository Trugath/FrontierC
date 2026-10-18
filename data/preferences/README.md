# Commander preferences and widgets

`tables.ini` keeps the original layout of four tables. `on` is the byte 0xff and `off` is 0x00, which is what each toggle inverts.

- `default_preferences.csv`: the 33 preference bytes, in the order they are copied into the commander. The first 31 match the checkboxes. `detail_very_high` is the fourth shape-detail flag; with the other three clear, that is the highest detail. The last two rows are `reserved` and stay in the saved block.
- `preference_widgets.csv`: one checkbox per preference, in that same order. `left`, `top`, `right` and `bottom` are the icon rectangle. `key` is the click the checkbox sends.
- `shape_detail_presets.csv`: the four shape-detail choices. Each row sets its own flag and clears the other three.
- `filename_edit_widgets.csv`: the save button (`save_file`) and the commander-name box (`edit_name`).

The 27 text entries live in the escaped TSV catalog under `data/text/commander/`.

The compiler writes disposable output under `build/`. Original binaries are not build inputs.

# Station interaction resources

`reply_routes.csv` maps each of the first 115 interaction text identifiers to
its C handler family: service, fitting, yard, trade, charge, talk, mission, or
board. Change a handler here instead of adding numeric ID tests in C. Text
identifiers resolve against `data/text/interaction/strings.tsv`; the compiler
checks complete coverage and catalogue order. Handler-specific behavior still
lives in the corresponding C module.

`mission_destinations.csv` selects the destination systems that trigger special
conversation responses and set mission-state bits. System references resolve
against `data/systems/catalogue/records.tsv`, so the system name and sector
coordinates are stored only in that catalogue. Bits are numbered 0..31. Systems and bits must be unique.
Rows retain the recovered evaluation order; after an unsuccessful search the
last row's bit remains in the scratch parameter, preserving native behavior.
These rules compile directly to native C tables; they add no historical image
bytes. Run `python test.py --integration` after changing them.

## Station layout authoring

Edit `layout.json`; `layout.schema.json` supplies editor completion and
structural validation. The compiler also checks references, routable Continue
targets, widget bounds, group references, and native record counts.

`actions` is keyed by screen name: menu, conversation, bulletin, trade,
fitting, shipyard, ship, missions and repairs. Each has:

- `visible_rows`: the number of visible rows, rather than a last-row index.
- `row_spacing` and `text_x`: pixel spacing and horizontal text position.
- `page_size`: the number of rows required when advancing the list's first
  visible row. Zero retains the recovered repairs-screen behavior.
- `continue_to`: the interaction reply invoked by the Continue button, or
  `null` for no transition. It must resolve to an existing reply handler.

For example, the fitting screen's Continue action uses:

```json
"continue_to": "interaction:text_shipyard_ships_refitted_repaired_bought_and_sold_also_contact_wi"
```

Text references resolve against `data/text/<catalogue>/strings.tsv`. Numeric
text selectors and positional action-message lists are not authored sources.

`widget_groups` defines reusable rectangles with inclusive
`[left, top, right, bottom]` bounds and named actions:

```json
{"bounds": [124, 157, 132, 166], "action": "next_page"}
```

Other actions are `previous_page`, `continue`, `{"menu_key": "1"}` through
`"5"`, and `{"primary_row": 0}` / `{"secondary_row": 0}` through row 13.
The row index is relative to the first visible row. The secondary action
sets the alternate-operation flag used for selling/removing items.
`widget_sets` composes named groups for flight, station, list and board screens;
editing a shared group updates each set that uses it. The compiler generates
pointer IDs and terminators.

`drives` maps drive-class names to `trade_in_value`, `mass` and named text.
Values retain the game's existing integer units.
`repairs` records `damage_flag`, named text and signed `charge`. Damage flag
numbers 0..17 identify bits in the engine's rotated equipment-damage mask;
rows must remain ordered. Charges retain the signed 16-bit encoding, including
the recovered negative value.

`selected_interaction_text.txt` names the unused initial selected-text value.
Only the recovery encoder reads it.

Native station generation reads sections by name and produces independent
display, equipment, drive and repair records. Widget lists each have their own
terminator; C code does not index a concatenated overlay image or rely on an
adjacent table. Item selection uses an item index, not a byte address.

Historical image reconstruction is confined to recovery helpers and regression
fixtures. `tables.ini` boundaries describe that serialization, not native C
memory. Interaction text lives in `data/text/interaction/`.

`interaction_colours.csv` remains numeric palette indices.
`interaction_object_template.txt` is the Frontier body, model `main_model_052`, copied for a
station transaction. `equipment.csv` is the 45 fitting rows in interaction-catalogue
order, starting at the dummy proximity mine. Price and refund are multiplied by
the station's tech band. `fitting` selects the buy and sell path, and `code` is
the equipment bit, mount, drive or installed byte that path uses. `stored_word`
and `stored_byte` are kept because they are in the 12-byte record; the fitting
screen does not read them. Run `python test.py --integration` after edits. Migration
fixtures pin original content and require review for intentional changes.

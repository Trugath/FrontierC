# Market and economy data

`commodity_market_parameters.csv` is a named, spreadsheet-editable table with
one row for each of the 31 commodities in `../text/market/strings.tsv` order:

| Column | Consumer |
|---|---|
| `commodity` | Stable text-catalogue identifier |
| `base_price` | Starting price term |
| `price_variation` | Trade-strength and population adjustment term |
| `base_supply` | Starting supply term |
| `supply_variation` | Trade-strength adjustment to supply |

Numeric columns use signed 16-bit integers. Negative disposal prices are
intentional: radioactives have a base price of -40 and rubbish -5. These values
retain the game's native integer scaling; they are not decimal currency or
physical-unit conversions. The compiler emits the same two's-complement words
used by `src/game/station/commodity_prices.c`.

Price and supply also depend on the coefficient tables in this directory,
import/export and illegality flags, and the runtime random sample. Editing a
base term does not specify the final price shown by every station.

`allegiance_candidate_systems.csv` names the system where each allegiance
starts its candidate search. The three rows are government bits 6–7, labelled
by the map's government panel: Independent, Imperial, then Federation
(`text_independent`, `text_imperial`, `text_federation` in
`../text/chart/strings.tsv`). `sector_x` and `sector_y` are the two signed
numbers in that map's System Sector Code, in that order, and `number` is its
System Number. `system` is the lowercase map name: a chart identifier such as
`uv_ceti` (`text_uv_ceti`), or the generated name for a sector without a
catalogue entry. UV Ceti is system 4 of sector 0,0, the same sector as Sol.
Eneda is system 0 of sector 2,-4. The compiler packs the sector and number
into the system key and rejects a name that does not match that position.

`market_record_dispatch.csv` and `system_record_dispatch_table.csv` name the
bulletin and request generators. The three timed requests are the photograph,
destroy and assassinate adverts.

`record_value_pairs.csv` is the passenger or parcel offer. `passage` is the
interaction text the bulletin inserts, and `reward_multiplier` scales the
fare. `market_local_record_words.csv` is a remote contract: `reward` is the
credit amount added to the offer, and `subject` is who it names. The plain
name is `text_empty_413`; the others are the merchant, company officer, boss,
governor and senator lines. `photograph_rewards.csv`, `destroy_rewards.csv`
and `assassinate_rewards.csv` are the eight credit payments for those missions.

`record_count_factors.csv` is added to a passenger fare. `record_reward_factors.csv`
then multiplies it. The price and supply coefficient tables have a decimal
`coefficient` column of Q15 words: 32768 would be one, and 32767 is the
saturated value the price path uses. The row is trade strength `0`..`7`, the
population line the map prints, or the catalogue `economic_factor_index` `0`..`7`.
`docking_slots.csv`, `command_slots.csv` and `control_slots.csv` are the
station-frame slot coordinates. A shape selects the bank and is the highest
slot index. Each slot is three signed words; the compiler leaves the following
two bytes for the next slot. `category_offers.csv` names the models stored in
the tail of the attachment bank. Kind 0 is goods and kind 1 is equipment, one
list per allegiance (independent, imperial, federation, the same order as
the candidate systems). Kind 9 is the station's ships. Kind 12 comes from the
mission detail. Kinds 6, 7, 8, 10 and 11 keep their kind number.

`tables.ini` keeps the image layout. Code addresses are not authored content;
the recovery encoder supplies them when reconstructing historical bytes.

Run `python test.py` after edits. Migration fixtures check the complete encoded
market image; deliberate economic changes require reviewing that baseline.

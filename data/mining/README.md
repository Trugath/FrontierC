# Mining data

`yields.csv` has 27 sampled-yield rows with these columns:

| Column | Meaning |
|---|---|
| `sample_index` | Ordered row selected by `(n * n) >> 5` in mining assessment |
| `category` | Named prospect from `categories.csv` |
| `assessment` | Text identifier from `../text/mining/strings.tsv` |
| `reserve` | Unsigned 16-bit reserve word; the simulation stores its low byte |

`categories.csv` maps the nine ordered prospects to named commodities from
`../text/market/strings.tsv`. For example, `metal_rich` produces `metal_alloys`.
The category IDs and assessment selectors are generated from their catalogue
order. Do not type hexadecimal selectors or commodity numbers into these files.
Changing displayed wording in a text catalogue does not change its identifier.

The assessment's random bound and body/richness factors determine how often a
sample row is reached. Rows are not equally likely, and `sample_index` is not a
percentage. Empty or exhausted machines retain the original behavior.

`tables.ini` records the native boundaries. Both the C table generator and the
resource compiler use `tools/gameplay_data.py` to validate headers, counts,
ordered indices, symbolic references and integer widths. Run `python test.py`
after editing. Intentional content changes require reviewing migration fixtures.

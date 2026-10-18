# Manual ownership challenges

`challenges.tsv` contains word number, line number, page number and expected
uppercase first letter. Row order is part of the original program. The build
emits four bytes per row, encoding the letter as `ord(answer)-ord('A')`.

There are 301 rows; the last duplicates the previous row. The selector is called
with AX=300, but its range convention is not assumed. No rows are deduplicated.
Coordinates and letters are validated before JSON and assembly generation.

The police question is interaction text selector 986C. offences supplies its word/line/page
values through SS:FBA4/FBA6/FBA8; station copies them to the three format parameters.

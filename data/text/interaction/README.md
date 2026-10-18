# Station and equipment text

462 ordered strings preserve relative directory indices used by callers.
TSV identifiers are descriptive labels derived from displayed text, not original
author symbols. Those names are `identifiers.txt`. `frontier-data unpack` writes
`strings.tsv` from `EL2M18.OVL`. Duplicate text receives an index suffix; entries are not merged.
Escaped bytes preserve the original text-control language. Edit TSV and build;
JSON and assembly includes are generated intermediates.

The directory occupies image 00A3..043E; strings occupy 043F..4CF8. All strings
are contiguous and individually NUL-terminated. The provider masks AX with 03FE
without checking that the resulting index is below 462. Preserve original order
and byte controls; text grammar remains a separate resident-consumer task.

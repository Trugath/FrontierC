# Name templates

Nine base/range pairs retain type indices 6..14. The sampled offset is less
than its range, so base+offset remains inside the 394-entry text directory.
Ranges overlap and must not be partitioned or deduplicated. The assembly lookup
aliases start twelve bytes before these physical tables to retain original
indexing. Edit CSV/INI; JSON and assembly are generated intermediates.

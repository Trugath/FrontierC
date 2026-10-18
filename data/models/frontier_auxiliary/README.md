`models.ini` and `model_directory.tsv` are in this repository. The 14 `.model`
files are written by `frontier-data unpack` and are gitignored.

Auxiliary resident model bank, image 27de4..29438. The directory has 240 slots and 14 unique models. Relative offsets and work-buffer sizes are rebuilt from the arrays. Two models have 198/118-byte header extensions and absent classification arrays; extension semantics are unresolved. Vertex/classification records and renderer programs retain their encoded format. The seven trailing bytes at 29438 are preserved separately; their consumer is unresolved. Model names identify slots, not object identities.

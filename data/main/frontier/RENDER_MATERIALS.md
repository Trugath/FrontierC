# Renderer materials and polygon masks

The image range 25CEh..2864h contains three distinct structures. It is not one
undifferentiated parameter-word array.

`render_material_scratch.csv` initializes three mutable CS words at 25CEh:
the low color offset, high material selector, and cached parameter offset.

`texture_materials.tsv` contains 37 one-based records of eight words. Each
record holds a graphics selector and seven logical RGB12 palette colors.
FFFFh is an unused color sentinel. The selector retains the complete word:
its lower twelve bits identify the graphics resource and its upper bits carry
orientation flags. The renderer shifts the material index left four and uses
a biased base sixteen bytes before the first record. Index zero therefore
aliases preceding storage; the original does not enforce bounds.

`polygon_vertex_acceptance_masks.csv` contains 64 bytes indexed by an OR of
vertex region bits: X-negative=1, X-inside=2, X-beyond=4, Y-negative=8,
Y-inside=16, Y-beyond=32. A mask accepts a polygon when each axis contains an
inside vertex or vertices on both sides. This is a bounding-region test, not
an exact polygon/viewport intersection test. The codec tests check all 64
entries against that predicate.

`tables.ini` records the exact adjacency and offsets. The build compiles these
native sources through module-table JSON and generated assembly includes.
The seven palette words are distinct from the selector and must retain their
order, including repeated entries and FFFFh sentinels.

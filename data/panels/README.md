# Panel colours and drive gauge scales

`palette_mapping.csv` is eleven pairs in their original order, including the repeated index 47. Each pair is a palette index and its mapped value. Drawing uses the index bytes.

`palette_list_end.csv` is the `0xff` that ends that list. `marker_colour_pointers.csv` is the eighteen words that follow it. Each word is the image address of a palette index byte, and the scanner uses the byte at that address as the marker colour for one band of model colour.

`drive_gauge_scales.csv` is fifteen unsigned words, one per drive class. The display quantity they scale still has no named unit.

`overlay_event_mask.csv` is the panel overlay's event mask. `tables.ini` keeps these spans in image order.

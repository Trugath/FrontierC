/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GALACTIC_MAP_TABLES_H
#define FE2_GALACTIC_MAP_TABLES_H
#include "galactic_map.h"
typedef struct map_catalog_entry { uint16_t y,x,first,count,extra; } map_catalog_entry;
typedef struct map_value_window { uint32_t base,next; } map_value_window;
typedef struct map_start { fe2_map_position position; uint16_t index,descriptor; } map_start;
typedef struct map_annotation { uint16_t threshold,y,x,text; } map_annotation;
extern const map_catalog_entry map_catalogs[47];
extern const fe2_map_record map_catalog_records[117];
extern const uint16_t map_catalog_identifiers[117];
extern const uint16_t map_weights[128],map_count_weights[18];
extern const uint8_t map_identifier_low[32],map_identifier_high[32];
extern const char map_syllables[32][4];
extern const char *const map_texts[261];
extern const map_value_window map_selected_values[128],map_query_values[128];
extern const int16_t map_candidate_offsets[2][12];
extern const map_start map_starts[6];
extern const uint8_t map_system_palette[384],map_density_palette[384],map_density_colours[128];
extern const uint8_t map_annotation_colours[3];
extern const uint16_t map_node_spacing[128],map_sprite_x[128],map_sprite_y[128],map_sprites[128];
extern const map_annotation map_annotations[14];
extern const char map_body_separator[4];
#endif

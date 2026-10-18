/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the pointer hit-region list. */
#include "pointer.h"
#include <string.h>

static size_t end(const fe2_pointer_regions *list)
{
    size_t i = 0;
    while (i < FE2_POINTER_REGIONS && list->region[i].left != FE2_POINTER_REGION_END) ++i;
    return i;
}

void fe2_pointer_regions_init(fe2_pointer_regions *list) { memset(list, 0xff, sizeof(*list)); }

size_t fe2_pointer_regions_count(const fe2_pointer_regions *list) { return end(list); }

void fe2_pointer_regions_clear(fe2_pointer_regions *list)
{
    list->region[0].left = FE2_POINTER_REGION_END;
    list->region[FE2_POINTER_REGIONS].left = FE2_POINTER_REGION_END;
}

void fe2_pointer_regions_reset(fe2_pointer_regions *list)
{
    fe2_pointer_regions_clear(list);
    (void)fe2_pointer_regions_append(list, fe2_pointer_function_key_regions, FE2_POINTER_FUNCTION_KEY_REGIONS);
}

int fe2_pointer_regions_append(fe2_pointer_regions *list, const fe2_pointer_region *stream, size_t available)
{
    size_t at = end(list), n = 0, i;
    if (!available) return 0;
    do ++n; while (n < available && stream[n].left != FE2_POINTER_REGION_END);
    if (at + n > FE2_POINTER_REGIONS) return -1;
    for (i = 0; i < n; ++i) {
        /* The next terminator goes first, then the record. */
        list->region[at + i + 1].left = FE2_POINTER_REGION_END;
        list->region[at + i] = stream[i];
    }
    return (int)n;
}

int fe2_pointer_regions_add(fe2_pointer_regions *list, uint16_t x, uint16_t y, uint16_t x_extent,
                            uint16_t y_extent, uint16_t id)
{
    size_t at = end(list);
    fe2_pointer_region *r;
    uint16_t left = (uint16_t)(x - x_extent), top = (uint16_t)(y - y_extent);
    if (at >= FE2_POINTER_REGIONS) return -1;
    list->region[at + 1].left = FE2_POINTER_REGION_END;
    r = &list->region[at];
    r->id = id;
    r->right = (uint16_t)(x + x_extent);
    r->bottom = (uint16_t)(y + y_extent);
    r->left = left & 0x8000u ? 0 : left;
    r->top = top & 0x8000u ? 0 : top;
    return 0;
}

void fe2_pointer_regions_remove(fe2_pointer_regions *list, uint16_t id)
{
    size_t i;
    for (i = 0; i < FE2_POINTER_REGIONS; ++i) {
        if (list->region[i].left & 0x8000u) return;
        if (list->region[i].id == id) break;
    }
    if (i == FE2_POINTER_REGIONS) return;
    /* Each record moves down; the shift stops after a negative left edge. */
    for (++i; i <= FE2_POINTER_REGIONS; ++i) {
        list->region[i - 1] = list->region[i];
        if (list->region[i].left & 0x8000u) return;
    }
}

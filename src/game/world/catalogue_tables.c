/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "system_catalogue.h"

#include "catalogue_tables.h"

const fe2_system_catalogue fe2_default_catalogue = {
    fe2_catalogue_fallback_records, fe2_catalogue_keyed_records, FE2_SYSTEM_KEYED
};

static void read_catalogue(const uint8_t *rec, fe2_catalogue_record *out)
{
    unsigned i;
    out->key = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) | ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);
    out->population_band = rec[4];
    out->resource_richness = rec[5];
    out->category_0_rate = rec[6];
    out->market_profile = rec[7];
    out->category_1_rate = rec[8];
    out->market_counter_seed = rec[9];
    out->government = rec[10];
    out->economic_factor_index = rec[11];
    for (i = 0; i < FE2_SYSTEM_COMMODITIES; ++i) out->trade_codes[i] = rec[12 + i];
    out->strengths = rec[43];
    out->map_text = (uint16_t)(rec[44] | (rec[45] << 8));
    out->view_text = (uint16_t)(rec[46] | (rec[47] << 8));
}

int fe2_catalogue_bind(const uint8_t *image, size_t count)
{
    unsigned i;
    if (!image || count != (7u + 324u) * 48u) return -1;
    for (i = 0; i < FE2_SYSTEM_FALLBACKS; ++i) read_catalogue(image + i * 48u, &fe2_catalogue_fallback_records[i]);
    for (i = 0; i < FE2_SYSTEM_KEYED; ++i)
        read_catalogue(image + (FE2_SYSTEM_FALLBACKS + i) * 48u, &fe2_catalogue_keyed_records[i]);
    return 0;
}

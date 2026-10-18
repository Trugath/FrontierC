/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* status's private structured records in the commander image. The scalar
 * resident generator intentionally excludes structures and pointer-backed
 * arrays. Encode their documented fields, without host struct padding.
 * Shared cells are packed by their session/overlay owners afterwards. */
static void status_image(fe2_commander_status *st, uint8_t *ds, int restore)
{
    unsigned i, j, base;
#define FIELD(address, width, member) do { \
    if (restore) st->member = image_get(ds, (address) - FE2_SAVE_RESIDENT_START, width); \
    else image_put(ds, (address) - FE2_SAVE_RESIDENT_START, width, (uint64_t)st->member); \
} while (0)
    FIELD(0x4fdd, 1, page);
    FIELD(0x4c1b, 1, arrival_countdown);
    FIELD(0x4fde, 1, matched_handle);
    FIELD(0x4bff, 2, contract_count);
    FIELD(0x4fe1, 2, military_count);
    FIELD(0x50df, 2, crew_needed);
    FIELD(0x50e1, 2, crew_count);
    FIELD(0x515b, 4, unpaid_wages);
    FIELD(0x518f, 2, mining_count);
    FIELD(0x20f1, 2, federal_points);
    FIELD(0x20f3, 2, imperial_points);
    for (i = 0; i < 2; ++i) FIELD(0x4bef + 2*i, 2, preview_angle[i]);
    for (i = 0; i < 3; ++i) FIELD(0x4bf3 + 4*i, 4, preview_distance[i]);
    for (i = 0; i < FE2_MAX_CONTRACTS; ++i) {
        base = 0x4c1d + 16*i;
        FIELD(base, 2, contracts[i].fee);
        FIELD(base+2, 1, contracts[i].group);
        FIELD(base+3, 1, contracts[i].kind);
        FIELD(base+4, 4, contracts[i].destination);
        FIELD(base+8, 4, contracts[i].deadline);
        FIELD(base+12, 4, contracts[i].client);
    }
    for (i = 0; i < FE2_MAX_MILITARY_CONTRACTS; ++i) {
        base = 0x4fe3 + 0x2a*i;
        FIELD(base, 1, military[i].type);
        FIELD(base+1, 1, military[i].outcome);
        FIELD(base+2, 4, military[i].target_system);
        FIELD(base+6, 4, military[i].target_seed);
        FIELD(base+10, 4, military[i].deadline);
        FIELD(base+14, 1, military[i].target_model);
        FIELD(base+15, 1, military[i].quality);
        FIELD(base+16, 2, military[i].reward);
        for (j = 0; j < FE2_NAME_BYTES; ++j) FIELD(base+18+j, 1, military[i].target_name[j]);
        FIELD(base+38, 4, military[i].home_system);
    }
    for (i = 0; i < FE2_MAX_CREW; ++i) {
        FIELD(0x50e3 + 6*i, 4, crew[i].name_seed);
        FIELD(0x50e7 + 6*i, 2, crew[i].salary);
    }
    for (i = 0; i < FE2_MAX_MINING_UNITS; ++i) {
        base = 0x5193 + 0x34*i;
        FIELD(base, 4, mining[i].system);
        FIELD(base+30, 1, mining[i].target);
        FIELD(base+31, 1, mining[i].prospect);
        for (j = 0; j < FE2_NAME_BYTES; ++j) FIELD(base+32+j, 1, mining[i].name[j]);
    }
    for (i = 0; i < FE2_PERMIT_WORDS; ++i) FIELD(0x515f + 4*i, 4, permits[i]);
    for (i = 0; i < FE2_AUTHORITIES; ++i) FIELD(0x516f + 2*i, 2, offences[i]);
#undef FIELD
}

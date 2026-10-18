/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MINING_MINING_INTERNAL_H
#define FE2_MINING_MINING_INTERNAL_H

#include "mining.h"

enum {
    MACHINE_MODEL = 0x78, MACHINE_KIND = 0x0d, MACHINE_ROLE = 0x11, MACHINE_MARKER = 0xf9,
    LOADED_MODEL = 0xc2, MACHINE_CARGO = 30, HOPPER_LIMIT = 10,
    RECOVERING = 0xff, UNDOCKED = 0xff,
    MINING_EVENT = 0x17, MACHINE_SELECTED = 0x10,
    KIND_PARAMETER = 0, MODEL_PARAMETER = 1, STYLE_PARAMETER = 7, OBJECT_PARAMETER = 8,
    OBJECTS_PARAMETER = 12,
    NO_RECORD = -1
};

/* mining_records.c */
uint8_t *fe2_mining_record(fe2_mining_state *s, unsigned index);
uint32_t fe2_mining_get32(const uint8_t *at);
void fe2_mining_put32(uint8_t *at, uint32_t value);
int fe2_mining_find_free(fe2_mining_state *s);
int fe2_mining_find_machine(fe2_mining_state *s, uint8_t handle);
void fe2_mining_close_gap(fe2_mining_state *s, unsigned index);
void fe2_mining_message(const fe2_mining_services *game, uint16_t text, uint16_t amount,
                     fe2_call_args *args);

#endif

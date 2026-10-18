/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_NAMES_H
#define FE2_NAMES_H

#include <stddef.h>
#include <stdint.h>

enum { FE2_NAME_MAX_BYTES=29, FE2_NAME_COMPONENT_BYTES=11,
       FE2_NAME_OK=0, FE2_NAME_INVALID=-1, FE2_NAME_CAPACITY=-2 };

typedef struct fe2_name_seed {
    uint32_t accumulator; /* resident sampler SI:DI */
    uint32_t state;       /* resident sampler CX:BX */
} fe2_name_seed;

typedef struct fe2_name_result {
    size_t bytes;         /* includes terminating NUL */
    fe2_name_seed seed;
} fe2_name_result;

/* Resident 0x128 sampler: return AX=high word, optionally publish DX=low word.
 * Seed must be non-null. Zero bound still advances both seed words. */
uint16_t fe2_name_sample(fe2_name_seed *seed, uint16_t bound, uint16_t *product_low);

/* names 0xf92: all unsigned word modes; output retains native font bytes.
 * Buffer and result must be non-null. On error neither is modified.
 * Capacity covers the actual output including NUL; 29 handles every mode. */
int fe2_name_generate(uint16_t mode, uint16_t low_seed, uint16_t high_seed,
                      uint8_t *buffer, size_t capacity, fe2_name_result *result);

/* names 0x11d1: mode=(selector & 0x3fe)>>1. Seeds are caller-provided equivalents
 * of resident 0x7e4/0x7e6. Same capacity/result contract as generate. */
int fe2_name_display(uint16_t selector, uint16_t low_seed, uint16_t high_seed,
                     uint8_t *buffer, size_t capacity, fe2_name_result *result);

/* Pure word cursor result for a successful call, including destination wrap. */
uint16_t fe2_name_end_cursor(uint16_t destination, const fe2_name_result *result);
void fe2_name_noop(void);

#endif

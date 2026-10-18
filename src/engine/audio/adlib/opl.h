/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_OPL_H
#define FE2_OPL_H

/* YM3812 register image and a two-operator renderer. This is the chip the
 * AdLib driver writes; it is not part of the driver. */
#include <stdint.h>

enum { FE2_OPL_RATE = 44100 };

typedef struct fe2_opl {
    uint8_t reg[256];
    uint32_t phase[22];
    int env[22];
    uint8_t stage[22];
    uint8_t key[22];
    uint8_t applied_key[22];
    int32_t feedback[9];
    int32_t feedback_previous[9];
    uint32_t clock;
    uint32_t noise, sample_clock;
    uint16_t am_counter, pm_counter;
    int16_t sample;
} fe2_opl;

void fe2_opl_init(fe2_opl *chip);
void fe2_opl_write(fe2_opl *chip, uint8_t reg, uint8_t value);
int16_t fe2_opl_clock(fe2_opl *chip);
void fe2_opl_render(fe2_opl *chip, int16_t *samples, unsigned frames);

#endif

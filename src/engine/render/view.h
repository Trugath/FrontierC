/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_VIEW_H
#define FE2_RENDER_VIEW_H

#include <stdint.h>

typedef struct fe2_render_view {
    uint16_t shift;
    int16_t position[3]; /* transformed, normalized negative camera position */
    int16_t direction[3]; /* transformed supplied direction */
} fe2_render_view;

/* Model-space view preparation. Basis contains three rows.
 * Normalize all position components under one shift until magnitudes <0x3f00,
 * extend under the original word-exponent rule, negate and transform. Dot
 * sums wrap to 32 bits; twice the sum's high word is retained. */
void fe2_render_prepare_view(const int32_t position[3], const int16_t direction[3],
                              const int16_t basis[9], uint16_t exponent, fe2_render_view *out);

/* Exact wrapped bounds test. Radius is the descriptor word
 * shifted by exponent&31. This preserves wrapped sums and INT32_MIN behavior;
 * it is not a floating-point geometric approximation. */
int fe2_render_model_visible(const int32_t position[3], uint16_t bound, uint16_t exponent);
int fe2_render_center_visible(const int32_t position[3], uint32_t radius);

#endif

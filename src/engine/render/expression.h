/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_EXPRESSION_H
#define FE2_RENDER_EXPRESSION_H

#include <stddef.h>
#include <stdint.h>

typedef struct fe2_render_words {
    const uint16_t *data;
    size_t count;
} fe2_render_words;

typedef struct fe2_render_expression_sources {
    fe2_render_words model, local, scale;
} fe2_render_expression_sources;

typedef enum fe2_render_operator {
    FE2_RENDER_ADD, FE2_RENDER_SUBTRACT, FE2_RENDER_MULTIPLY,
    FE2_RENDER_DIVIDE, FE2_RENDER_SHIFT_RIGHT, FE2_RENDER_SHIFT_LEFT,
    FE2_RENDER_MAXIMUM, FE2_RENDER_MINIMUM_MINUS_ONE,
    FE2_RENDER_MULTIPLY_ALIAS, FE2_RENDER_SHIFT_SIGNED,
    FE2_RENDER_MODEL_WORD, FE2_RENDER_KEEP_IF_NOT_LESS,
    FE2_RENDER_KEEP_IF_NOT_GREATER, FE2_RENDER_SCALE,
    FE2_RENDER_SCALE_BIASED, FE2_RENDER_AND
} fe2_render_operator;

/* Ordinary typed evaluation of the recovered 16 operator slots. Arithmetic
 * wraps at 16 bits; shifts mask count to five bits. Return 0 on success, 1
 * for divide-by-zero (result stays left, matching the original resume), -1
 * for an invalid operator or unavailable table word (output unchanged).
 * Minimum-minus-one keeps left when right > left, otherwise returns right-1.
 * Model-word sums wrap before doubling to a word offset, so index is the
 * low 15 bits of left+right. Scale inputs address up to 2048 table words. */
int fe2_render_expression_apply(fe2_render_operator op, uint16_t left, uint16_t right,
                                 const fe2_render_expression_sources *sources, uint16_t *out);

/* Decode a model-program operand byte into an inline value or bounded
 * model/local word. Inline selectors 0x40..0x7f encode multiples of 1024. */
int fe2_render_expression_operand(uint8_t selector,
                                   const fe2_render_expression_sources *sources, uint16_t *out);

/* Execute one binary-expression token and packed operand word. Token bits
 * 4..7 select the operator, bits 8..10 the destination among eight local
 * result slots. Successful results, including a divide resume, write that
 * slot; failures leave all slots unchanged. Operand source views describe
 * pre-evaluation state and may alias the destination array. */
int fe2_render_expression_evaluate(uint16_t token, uint16_t operands,
                                    const fe2_render_expression_sources *sources, uint16_t slots[8]);

#endif

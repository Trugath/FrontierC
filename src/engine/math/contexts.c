/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* render context initialization (the original reserves the
 * context on its caller's stack). */
#include <string.h>

#include "math3.h"

void fe2_math_init_small_context(fe2_small_context *c)
{
    memset(c->header, 0, sizeof c->header);
    c->limit_low = 0xffff;
    c->limit_high = 0xa8;
    c->limit_end = 0xffff;
}

/* Each list's count and each tail are cleared; the rest is left. */
void fe2_math_init_large_context(fe2_large_context *c)
{
    unsigned block, lane;
    for (block = 0; block < 21; ++block) {
        for (lane = 0; lane < 8; ++lane) c->blocks[block][lane].count = 0;
        memset(c->tails[block], 0, sizeof c->tails[block]);
    }
    c->first = 0xa8;
    c->second = 0xffff;
}

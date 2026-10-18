/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* pointer buttons, motion and the region hit test. */
#include "pointer.h"

enum { MAX_X = 0x13f, MAX_Y = 0xc6, REPEAT = 0x1a, NO_REGION = 0xff, GAMEPORT_REDIRECT = 0x20 };

void fe2_pointer_publish_buttons(fe2_pointer_state *p, uint8_t primary, uint8_t secondary)
{
    p->primary = primary; p->secondary = secondary;
    p->primary_latch &= primary; p->secondary_latch &= secondary;
}

static uint8_t consume(uint8_t held, uint8_t *latch)
{
    uint8_t press = (uint8_t)(~*latch & held);
    *latch |= press;
    return press;
}
int fe2_pointer_consume_primary(fe2_pointer_state *p) { return consume(p->primary, &p->primary_latch) != 0; }
int fe2_pointer_consume_secondary(fe2_pointer_state *p) { return consume(p->secondary, &p->secondary_latch) != 0; }

static uint8_t hit(const fe2_pointer_regions *regions, int16_t x, int16_t y)
{
    uint8_t id = NO_REGION;
    size_t i;
    for (i = 0; i < FE2_POINTER_REGIONS && regions->region[i].left != FE2_POINTER_REGION_END; ++i) {
        const fe2_pointer_region *r = &regions->region[i];
        if (x < (int16_t)r->left || x > (int16_t)r->right || y < (int16_t)r->top || y > (int16_t)r->bottom) continue;
        id = (uint8_t)r->id;
    }
    return id;
}

void fe2_pointer_query(fe2_pointer_state *p, const fe2_pointer_regions *regions, const fe2_pointer_sample *sample,
                       uint16_t *x_out, uint16_t *y_out)
{
    uint16_t x, y;
    uint8_t id, press, previous;
    fe2_pointer_publish_buttons(p, sample->primary, sample->secondary);
    if (p->secondary) {
        p->redirected[0] = (int16_t)(uint16_t)((uint16_t)p->redirected[0] + (uint16_t)sample->motion[0]);
        p->redirected[1] = (int16_t)(uint16_t)((uint16_t)p->redirected[1] + (uint16_t)sample->motion[1]);
        *x_out = p->x; *y_out = p->y; return;
    }
    if ((p->gameport_flags & GAMEPORT_REDIRECT) && (p->gameport[0] || p->gameport[1])) {
        p->redirected[0] = (int16_t)(uint16_t)((uint16_t)p->redirected[0] + (uint16_t)p->gameport[0]);
        p->redirected[1] = (int16_t)(uint16_t)((uint16_t)p->redirected[1] + (uint16_t)p->gameport[1]);
        *x_out = p->x; *y_out = p->y; return;
    }
    x = (uint16_t)((uint16_t)sample->motion[0] + (uint16_t)p->gameport[0] + p->x);
    y = (uint16_t)((uint16_t)sample->motion[1] + (uint16_t)p->gameport[1] + p->y);
    if (x & 0x8000u) x = 0;
    if (x >= MAX_X) x = MAX_X;
    if (y & 0x8000u) y = 0;
    if (y >= MAX_Y) y = MAX_Y;
    *x_out = x; *y_out = y;
    if (!p->primary) { p->hold = 0; return; }
    p->pressed[0] = x; p->pressed[1] = y;
    id = hit(regions, (int16_t)x, (int16_t)y);
    press = consume(p->primary, &p->primary_latch);
    if (press) {
        p->input_code = id;
        previous = p->region[0]; p->region[0] = id; p->region[1] = previous;
        previous = p->age[0]; p->age[0] = 0; p->age[1] = previous;
        p->hold = 1;
    } else if (p->hold >= REPEAT) p->input_code = id;
}

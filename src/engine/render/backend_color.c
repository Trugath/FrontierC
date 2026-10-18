/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "backend.h"

/* Main drawing-color wrappers  and the graphics work pointer
 *. Both DS copies are always written together. */

static void publish(fe2_render_color_state *s, fe2_render_rgb rgb)
{
    s->work = rgb;
    s->current = rgb;
}

void fe2_render_set_color(fe2_render_color_state *s, const fe2_render_color_services *v,
                          int prepare, fe2_render_rgb rgb)
{
    if (prepare && v->prepare) v->prepare(v->context, rgb);
    publish(s, rgb);
    if (v->publish) v->publish(v->context);
    s->restored = 0;
}

void fe2_render_restore_color(fe2_render_color_state *s, const fe2_render_color_services *v, int callback)
{
    if (callback && v->restoring) v->restoring(v->context);
    publish(s, s->saved);
    s->restored = 0xff;
}

void fe2_render_save_and_set_color(fe2_render_color_state *s, const fe2_render_color_services *v,
                                   fe2_render_rgb rgb)
{
    s->saved = rgb;
    publish(s, rgb);
    if (v->publish) v->publish(v->context);
}

void fe2_render_enable_drawing(fe2_render_color_state *s)
{
    s->drawing = 0xff;
}

void fe2_render_disable_drawing(fe2_render_color_state *s, const fe2_render_color_services *v)
{
    s->drawing = 0;
    if (v->clear) v->clear(v->context, 0, 0x13f, 0, 0xc7, 0);
}

int fe2_render_update_palette_127(fe2_render_color_state *s, fe2_palette_upload_fn upload, void *context)
{
    fe2_render_rgb c = s->current;
    if (c.red == s->last_127.red && c.green == s->last_127.green && c.blue == s->last_127.blue) return 0;
    s->last_127 = c;
    if (upload) upload(context, 0x7f, c.red, c.green, c.blue);
    return 1;
}

void fe2_render_set_work_pointer(fe2_render_color_state *s, uint16_t offset, uint16_t segment)
{
    s->work_pointer[0] = offset;
    s->work_pointer[1] = segment;
}

void fe2_render_reset_work_pointer(fe2_render_color_state *s)
{
    fe2_render_set_work_pointer(s, 0, 0x200);
}

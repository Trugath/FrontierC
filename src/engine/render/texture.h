/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_TEXTURE_H
#define FE2_RENDER_TEXTURE_H

/*
 * Textured surface mode of the triangle/quad producers. After
 * face classification and vertex refresh, a surface whose projected points
 * all lie in X -2000..2320, Y -2000..2168, whose material dword has a
 * nonzero high word (a material-table entry), and whose summed X or Y edge
 * lengths reach 10 (triangle) / 14 (quad) becomes a textured command, unless
 * the combined viewport outcodes reject it (then nothing is emitted). Every
 * other surface takes the ordinary solid/clipped path.
 */
#include "commands.h"

/* A bounded, unpacked indexed texture. Pixels use contiguous rows. */
typedef struct fe2_render_texture_image {
    const uint8_t *pixels;
    size_t bytes;
    unsigned width, height;
} fe2_render_texture_image;

typedef struct fe2_render_texture_command {
    uint32_t key;
    unsigned count;              /* 3 or 4 */
    fe2_screen_point points[4];  /* compact producer order */
    uint16_t texture;            /* material entry word 0 */
    uint16_t colors[7];          /* entry words 1..7 shaded per RGB12 nibble, unmapped */
    unsigned color_count;        /* entries before the first 0xffff word */
} fe2_render_texture_command;

/* Caller-owned storage, bound through fe2_render_program_drawing.textures.
 * count is an allocation cursor; reset it with the queue for a new frame. */
typedef struct fe2_render_texture_output {
    fe2_render_command *nodes;
    fe2_render_texture_command *commands;
    size_t capacity, count;
} fe2_render_texture_output;

enum {
    FE2_RENDER_TEXTURE_SOLID = 2, /* not textured: use the solid producer */
    FE2_RENDER_TEXTURE_EMITTED = 1,
    FE2_RENDER_TEXTURE_REJECTED = 0 /* outcodes reject: no command at all */
};

/* Classify and build a textured surface from the projections captured during
 * resolution (compact order) and the anchor record's camera point. face is
 * the nonnegative face answer. Returns one of the values above, or -1 when a
 * required material-table word lies outside the native resource. */
int fe2_render_build_texture(const fe2_screen_point *cached, unsigned count, uint16_t token,
    uint16_t face, uint32_t inherited, const int32_t camera[3], int16_t scale,
    fe2_render_texture_command *out);

/* Queue callback for fe2_render_drawing_target.texture. */
void fe2_render_texture_execute(void *target, const void *command);

#endif

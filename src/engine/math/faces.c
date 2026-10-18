/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* face classification. */
#include "math3.h"

enum { FACE_AWAY = 0x8000, FACE_UNLIT = 0, BUFFER_FILL = 0x8080 };

void fe2_math_init_faces(const fe2_face_frame *f, uint16_t *buffer, uint16_t length)
{
    int16_t count = (int16_t)((length >> 1) - 2), i;
    buffer[0] = (uint16_t)f->header;
    buffer[1] = (uint16_t)(f->header >> 16);
    for (i = 0; i < count; ++i) buffer[2 + i] = BUFFER_FILL;
}

/* x86 16-bit shifts: the count is masked to 5 bits. */
static int16_t shift_word(int16_t v, int16_t count)
{
    unsigned n;
    if (count < 0) {
        n = (uint8_t)-count & 31u;
        return (int16_t)(v >> (n > 15 ? 15 : n));
    }
    n = (uint8_t)count & 31u;
    return n > 15 ? 0 : (int16_t)(uint16_t)((uint16_t)v << n);
}

static int32_t dot(const int16_t a[3], const int16_t b[3])
{
    return (int32_t)((uint32_t)((int32_t)a[0] * b[0]) + (uint32_t)((int32_t)a[1] * b[1]) +
                     (uint32_t)((int32_t)a[2] * b[2]));
}

/*
 * The face's normal is the four bytes before its selector (bit 1 of the
 * selector mirrors it); its first word's high bits pick the vertex whose
 * packed point lies on the face. Seen from the origin side the face is
 * turned away; otherwise the view vector picks a shade from its top bits.
 */
uint16_t fe2_math_classify_face(const fe2_face_frame *f, uint16_t selector)
{
    int mirrored = (selector & 2) != 0;
    const uint8_t *normal = f->normals + (uint16_t)(selector & ~2u) - 4;
    const uint8_t *point;
    uint16_t first = (uint16_t)(normal[0] | normal[1] << 8), vertex;
    int16_t n[3], p[3];
    int32_t facing, light;
    int8_t x;
    unsigned i;
    n[0] = (int16_t)(uint16_t)(normal[0] << 8);
    n[1] = (int16_t)(uint16_t)(normal[3] << 8);
    n[2] = (int16_t)(uint16_t)(normal[2] << 8);
    if (mirrored) n[0] = (int16_t)(uint16_t)-(uint16_t)n[0];
    vertex = (uint16_t)((first >> 7) & 0x1feu);   /* AND AL: bit 8 stays */
    point = f->program + (vertex & 0xfffdu);
    x = (int8_t)point[0];
    if (((vertex & 2) != 0) != mirrored) x = (int8_t)(uint8_t)-(uint8_t)x;
    p[0] = x;
    p[1] = (int8_t)point[3];
    p[2] = (int8_t)point[2];
    for (i = 0; i < 3; ++i)
        p[i] = (int16_t)(shift_word(p[i], (int16_t)(f->scale - f->shift)) - f->origin[i]);
    facing = dot(p, n);
    if (facing >= 0) return FACE_AWAY;
    light = dot(f->view, n);
    if (light >= 0) return FACE_UNLIT;
    return f->shades[((uint32_t)light >> 26 & 0xeu) >> 1];
}

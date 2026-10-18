/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_MATH3_H
#define FE2_MATH3_H

/*
 * The resident math library (math): normalized ("floating") scalars,
 * integer helpers, sine- and square-root-table vectors and bases, vector
 * rescaling, perspective projection and object position updates.
 *
 * Normalized scalars are a mantissa and an exponent (fe2_scalar, from
 * game/scalar.h; fe2_scalar32 for a 32-bit mantissa). A signed mantissa is
 * normalized when its top two bits differ, an unsigned one when bit 14 is
 * its highest set bit. Arithmetic keeps the original's truncation, x86 shift
 * count masking and 16/32-bit wraparound; nothing uses floating point.
 *
 * Angles are 16-bit turns (0x10000 is a full turn). Sine-table values and
 * basis entries are Q15 fractions; H(p, q), the "fraction product", is the
 * high word of 2*p*q.
 */
#include <stdint.h>

#include "game/object.h"
#include "game/scalar.h"

typedef struct fe2_scalar32 {
    uint32_t mantissa;
    uint16_t exponent;
} fe2_scalar32;

typedef struct fe2_point {
    int16_t x, y;
} fe2_point;

/* ---- normalized scalars (normalized.c) ---------------------------------- */
fe2_scalar fe2_math_add(fe2_scalar a, fe2_scalar b);
fe2_scalar fe2_math_subtract(fe2_scalar a, fe2_scalar b);
fe2_scalar32 fe2_math_add32(fe2_scalar32 a, fe2_scalar32 b);
fe2_scalar32 fe2_math_subtract32(fe2_scalar32 a, fe2_scalar32 b);
fe2_scalar fe2_math_add_unsigned(fe2_scalar a, fe2_scalar b);
/* The 32-bit doubled product and the exponent sum. */
fe2_scalar32 fe2_math_multiply(fe2_scalar a, fe2_scalar b);
/* dividend / divisor, for normalized operands (a zero divisor returns the
 * divisor's exponent as the mantissa and the dividend's exponent + 1). */
fe2_scalar fe2_math_divide(fe2_scalar dividend, fe2_scalar divisor);
/* (dividend >> 2) / divisor; the exponent is reduced by the dividend's low
 * word, as the original does. */
fe2_scalar fe2_math_divide32(int32_t dividend, int16_t divisor, uint16_t exponent);
fe2_scalar fe2_math_normalize(fe2_scalar value);
fe2_scalar fe2_math_normalize_unsigned(fe2_scalar value);             /*, mantissa < 0x8000 */
fe2_scalar fe2_math_normalize_positive(fe2_scalar value);             /*, to bit 15 */
fe2_scalar32 fe2_math_normalize32(fe2_scalar32 value);
fe2_scalar fe2_math_from_signed32(int32_t value);
fe2_scalar fe2_math_from_unsigned32(uint32_t value);
/* the normalized 32-bit mantissa and its exponent. */
fe2_scalar32 fe2_math_normalize_signed32(int32_t value);
int32_t fe2_math_to_signed32(fe2_scalar value);                       /*, saturating */
int32_t fe2_math_to_signed32_unchecked(fe2_scalar value);
uint32_t fe2_math_to_unsigned32(fe2_scalar value);                    /*, saturating */
int64_t fe2_math_to_signed64(fe2_scalar value);
int16_t fe2_math_to_signed16(fe2_scalar value);                       /*, saturating */

/* ---- integers (integers.c) ---------------------------------------------- */
typedef struct fe2_division {
    uint32_t quotient, remainder;
} fe2_division;
fe2_division fe2_math_divide_unsigned(uint32_t dividend, uint32_t divisor); /*, divisor != 0 */
/* 2 * (matrix columns. vector), the matrix as nine Q15 words in
 * row order: out[i] = 2 * (m[i]*v0 + m[3+i]*v1 + m[6+i]*v2). */
void fe2_math_matrix_vector(const int16_t matrix[9], const int16_t vector[3], int32_t out[3]);
/* ((b - a) * scale * 8) mod 2^64, a signed difference. */
int64_t fe2_math_scaled_difference(uint64_t a, uint64_t b, uint32_t scale);
/* The wide scalar product used to move objects: component * scalar with
 * the original's reduction of the scalar to 15 bits (see integers.c). */
int64_t fe2_math_wide_scale(int32_t component, int64_t scalar);

/* ---- tables (math3_tables.c, generated) -------------------------------- */
extern const int16_t fe2_math_sine[2048];       /* a half turn of sine, Q15 */
extern const uint16_t fe2_math_square_root[2048];
extern const uint16_t fe2_math_reciprocal[1024];

/* ---- angles and lengths (lookups.c) ------------------------------------- */
int16_t fe2_math_sin(uint16_t angle);
int16_t fe2_math_cos(uint16_t angle);
/* the unit vector of longitude `a` and latitude `b`:
 * (-H(sin b, cos a), H(sin a, sin b), cos b). */
void fe2_math_direction(uint16_t a, uint16_t b, int16_t out[3]);
/* the two basis layouts of angles `a` and `b`. */
void fe2_math_basis_a(uint16_t a, uint16_t b, int16_t basis[9]);
void fe2_math_basis_b(uint16_t a, uint16_t b, int16_t basis[9]);
/* rotate two basis vectors (stride 3 words) by `angle`:
 * u' = H2(cos*u + sin*v), v' = H2(cos*v - sin*u). */
void fe2_math_rotate_pair(int16_t *u, int16_t *v, uint16_t angle);
/* approximately sqrt(value) through the square-root table. */
uint16_t fe2_math_square_root32(uint32_t value);
/* approximately |(x, y, z)|. */
uint16_t fe2_math_length3(int16_t x, int16_t y, int16_t z);
fe2_scalar fe2_math_square_root_scalar(fe2_scalar value);
fe2_scalar fe2_math_square_root_scalar32(fe2_scalar32 value);

/* ---- vector rescaling (rescale.c) --------------------------------------- */
/* refine a divisor from `*accumulator` (its approximate root), then
 * vector[i] = (vector[i] / divisor) * multiplier; the divisor is stored
 * back in `*accumulator`. */
void fe2_math_rescale(int32_t vector[3], uint32_t *accumulator, uint32_t multiplier);
/* the same with the accumulator formed from the vector's own squares
 * (normalizing it to length `multiplier`). */
void fe2_math_rescale_to_length(int32_t vector[3], uint32_t multiplier);
/* The 32-bit variant the renderer uses (image 0x2263, a near helper). */
void fe2_math_rescale32(int32_t vector[3], uint32_t multiplier);

/* ---- projection (projection.c) ------------------------------------------ */
/* (x, y) * 1/depth through the reciprocal table, for depth > 0. */
fe2_point fe2_math_project(int32_t x, int32_t y, int32_t depth);

/* ---- object positions (object_motion.c) --------------------------------- */
void fe2_math_move_object(fe2_object *object, const int32_t delta[3]);
/* position += wide_scale(velocity, scalar). */
void fe2_math_advance_object(fe2_object *object, int64_t scalar);
/* position += (vector_delta * high(factor * scale)) shifted by the
 * object's vector exponent. */
void fe2_math_step_object(fe2_object *object, uint16_t factor);
/* place `object` on the arc of longitude `angle`, at the latitude
 * `phase` (high word, refined by its low 12 bits), with radius `factor`
 * and exponent `exponent`. Sets vector_delta to the arc's step. */
void fe2_math_place_on_arc(fe2_object *object, uint16_t angle, uint32_t phase, int16_t factor,
                           uint16_t exponent);

/* ---- the renderer's contexts and vertices (contexts.c, vertices.c) ----- */

/* The large render context (0x5598 bytes on the renderer's stack): 21 depth
 * blocks of 8 lists (0x80 bytes, a count word first) and 21 16-byte block
 * tails. Only the parts the allocation initializes are named. */
typedef struct fe2_render_list {
    uint16_t count;
    uint8_t items[0x7e];
} fe2_render_list;

typedef struct fe2_large_context {
    uint16_t first, second;            /* : 0xa8, 0xffff */
    uint8_t header[0x42];
    fe2_render_list blocks[21][8];
    uint8_t tails[21][16];
    uint16_t last;
} fe2_large_context;

/* The small context (0xe76 bytes): a cleared header and three words. */
typedef struct fe2_small_context {
    uint8_t header[0x150];             /* cleared */
    uint8_t body[0xe70 - 0x150];
    uint16_t limit_low, limit_high, limit_end; /* : 0xffff, 0xa8, 0xffff */
} fe2_small_context;

void fe2_math_init_large_context(fe2_large_context *context);
void fe2_math_init_small_context(fe2_small_context *context);

/* A model vertex (32 bytes in the renderer's table). Vertices come in
 * mirrored pairs (2k, 2k + 1) built by one four-byte program op. */
enum { FE2_VERTEX_TOO_NEAR = 0x8002, FE2_VERTEX_MODEL_ONLY = 0xffff, FE2_VERTEX_PROJECTED = 1 };
typedef struct fe2_vertex {
    int16_t screen[2];                 /* : projected, or 0x8002 twice when too near */
    int32_t view[3];                   /* : model + the frame offset */
    uint16_t spare;
    uint16_t state;                    /* : 0 unbuilt; 0xffff model only; else its low byte is the
                                        * stamp of the frame that projected it */
    int32_t model[3];
} fe2_vertex;

/* The vertex program's shared state (resident DS). */
typedef struct fe2_vertex_work {
    uint32_t noise_a, noise_b;
    uint16_t references;               /* the last op's reference pair (see vertices.c) */
} fe2_vertex_work;

/* The renderer's frame fields the vertex program reads. */
typedef struct fe2_vertex_frame {
    fe2_vertex *vertices;              /*; negative indices are the parent model's */
    fe2_vertex *const *inherited;      /* BP-0x44 + 4k: for k < 0, the parent's copy of vertex k */
    const uint8_t *program;            /* BP-0xdc: four bytes per vertex pair */
    const uint16_t *object_words;      /* BP-0xd8: the object's words from +0x72 (weights) */
    const uint16_t *frame_words;       /* BP-0x40 (weights) */
    int16_t matrix[9];                 /* BP-0x24 */
    int32_t offset[3];                 /* BP-0x10: the model's origin in view space */
    uint16_t scale;                    /* BP-0xd4: matrix products shift by 0x17 - scale */
    uint16_t object_size;              /* the word at +0x16 of the object at BP-4 */
    uint8_t stamp;                     /* BP-0x9a */
    fe2_vertex_work *work;
} fe2_vertex_frame;

/* build vertex `index` (model and projection) if it is unbuilt,
 * else re-add the frame offset and project it. */
void fe2_math_vertex_refresh(const fe2_vertex_frame *frame, int index);
/* build vertex `index` and its pair (model only where the program
 * does not project). */
void fe2_math_vertex_build(const fe2_vertex_frame *frame, int index);
/* re-add the frame offset to a built vertex, or build it. */
void fe2_math_vertex_place(const fe2_vertex_frame *frame, int index);

/* Face classification. */
typedef struct fe2_face_frame {
    const uint8_t *normals;            /* a face normal occupies the four bytes before its serialized selector */
    const uint8_t *program;            /* BP-0xdc */
    uint32_t header;                   /* BP-0x62 */
    uint16_t scale, shift;             /* BP-0xd4, BP-0x2c */
    int16_t origin[3];                 /* BP-0x32 */
    int16_t view[3];                   /* BP-0x2a */
    uint16_t shades[8];                /* BP-0x68 */
} fe2_face_frame;

/* The buffer: the header dword, then (length / 2 - 2) words of 0x8080. */
void fe2_math_init_faces(const fe2_face_frame *frame, uint16_t *buffer, uint16_t length);
/* for a face turned away; 0 if lit from behind; else a shade. */
uint16_t fe2_math_classify_face(const fe2_face_frame *frame, uint16_t selector);

#endif

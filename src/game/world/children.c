/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* recursive generation of a body's children (planets of a
 * star, moons of a planet...). Children are sampled along the saturating
 * profile curve; each child may in turn get children, with the parent's
 * random states, scratch values and name counters restored afterwards. */
#include "systems_internal.h"

enum {
    CURVE_END = 0xffe0,          /* stop once the curve reaches this */
    MINIMUM_CHILD = 0x20,
    SMALL_CHILD = 0x3a98,        /* below this, sizes are kept 8x larger */
    ROOT_GROUP_POSITION = 0x3000,
    RANDOM_CHILD_SCALE = 0x103,
    SUBTREE_LIMIT_SHIFT = 6, SUBTREE_SCALE_SHIFT = 7
};

/* Values saved around a subtree (the original pushes them). */
typedef struct generation_frame {
    uint16_t position_u, delta;
    uint32_t limit, scale;
    uint16_t step, curve, position;
    unsigned parent;
    uint32_t random_a, random_b;
    uint8_t counters[4];
} generation_frame;

static void save(const fe2_system_generation *g, generation_frame *f)
{
    f->position_u = (uint16_t)g->position_u;
    f->delta = (uint16_t)g->delta;
    f->limit = g->limit;
    f->scale = g->scale;
    f->step = g->step;
    f->curve = g->curve;
    f->position = g->position;
    f->parent = g->parent;
}

static void restore(fe2_system_generation *g, const generation_frame *f)
{
    g->position_u = (g->position_u & 0xffff0000u) | f->position_u;
    g->delta = (g->delta & 0xffff0000u) | f->delta;
    g->limit = f->limit;
    g->scale = f->scale;
    g->step = f->step;
    g->curve = f->curve;
    g->position = f->position;
    g->parent = f->parent;
}

/* children of the body just created (size `size`). */
static void generate_subtree(fe2_system_generation *g, uint32_t size)
{
    generation_frame frame;
    uint32_t limit;

    save(g, &frame);
    if (size < SMALL_CHILD) size <<= 3;
    g->scale = size;
    g->parent = (g->destination + (uint16_t)(g->parent_index - g->record_count) - 1u) %
                g->space.count;
    g->position = g->subtree_position;
    frame.random_a = g->random_a;
    frame.random_b = g->random_b;
    frame.counters[0] = g->moon_digit;
    frame.counters[1] = g->moon_letter;
    frame.counters[2] = g->sub_digit;
    frame.counters[3] = g->depth;
    ++g->depth;
    if (size >= FE2_NAMED_BODY_SIZE)
        g->moon_digit = g->moon_letter = g->sub_digit = g->depth = 0;
    limit = frame.limit >> SUBTREE_LIMIT_SHIFT;
    g->limit = g->scale > limit ? limit : g->scale;
    g->scale >>= SUBTREE_SCALE_SHIFT;
    g->value = (g->value & 0xffff0000u) | (uint16_t)fe2_systems_random(g);
    fe2_systems_generate_children(g);
    g->moon_digit = frame.counters[0];
    g->moon_letter = frame.counters[1];
    g->sub_digit = frame.counters[2];
    g->depth = frame.counters[3];
    g->random_b = frame.random_b;
    g->random_a = frame.random_a;
    restore(g, &frame);
}

/* Size of the next child, or 0 when the sampling ends. Advances the curve. */
static int sample_child(fe2_system_generation *g, uint16_t seed, uint32_t *size)
{
    uint16_t base = (uint16_t)((uint16_t)fe2_systems_random(g) >> 3);
    uint16_t spacing = (uint16_t)(g->position >> 2);
    uint16_t weight = g->position, previous = g->position, r;
    uint32_t sum;

    spacing = (uint16_t)(spacing + (spacing >> 1));
    sum = (uint32_t)base + seed;
    if (sum <= 0xffffu) weight = (uint16_t)(((uint32_t)(uint16_t)sum * g->position) >> 16);
    sum = (uint32_t)spacing + weight;
    if (sum > 0xffffu) return 0;
    g->step = (uint16_t)sum;
    g->delta = (g->delta & 0xffff0000u) |
               (uint16_t)(fe2_systems_curve((uint16_t)(g->step + g->position)) - g->curve);
    r = (uint16_t)fe2_systems_random(g);
    r = (uint16_t)(((uint32_t)r * r) >> 16);
    r = (uint16_t)(((uint32_t)r * r) >> 16);
    r = (uint16_t)(((uint32_t)r * (uint16_t)g->delta) >> 16);
    g->curve = (uint16_t)(g->curve + r);
    *size = (uint32_t)r * (g->scale >> 16) + (((uint32_t)r * (g->scale & 0xffffu)) >> 16);
    sum = (uint32_t)g->position + g->step;
    if (sum > 0xffffu) return 0;
    g->position = (uint16_t)sum;
    g->step >>= 1;
    /* The child's own position: halfway along its step. */
    g->position_u = (g->position_u & 0xffff0000u) | (uint16_t)(previous + g->step);
    return 1;
}

/* A large child of a multi-star system becomes a root group of its own;
 * returns 1 when the subtree should follow with the reduced size. */
static int companion_group(fe2_system_generation *g, uint32_t *size)
{
    uint32_t half = g->root_size >> 1;
    uint16_t position_u, count;

    if (*size >= half) *size = half;
    if (--g->root_budget == 0) return 0;
    fe2_systems_random(g);
    g->delta = (g->delta & 0xffff0000u) | (uint16_t)((uint8_t)(g->random_b >> 8) >> 3);
    position_u = (uint16_t)g->position_u;
    count = g->record_count;
    fe2_systems_generate_root_group(g, *size);
    g->subtree_position = ROOT_GROUP_POSITION;
    g->parent_index = (uint16_t)(count + 1u);
    g->position_u = (g->position_u & 0xffff0000u) | position_u;
    *size >>= 2;
    --g->root_budget;
    return 1;
}

void fe2_systems_generate_children(fe2_system_generation *g)
{
    uint16_t seed;
    uint32_t size;

    g->limit = g->limit >= 0x80000000u ? 0xffffffffu : g->limit << 1;
    seed = (uint16_t)g->value;
    g->curve = fe2_systems_curve(g->position);
    do {
        uint16_t parent_index;
        if (!sample_child(g, seed, &size)) return;
        if (size >= MINIMUM_CHILD) {
            if (size < SMALL_CHILD) size >>= 3;
            parent_index = g->parent_index;
            if (size >= FE2_NAMED_BODY_SIZE && g->root_budget == 0) {
                size = (uint32_t)(uint16_t)fe2_systems_random(g) * RANDOM_CHILD_SCALE;
            } else if (size >= FE2_NAMED_BODY_SIZE && companion_group(g, &size)) {
                generate_subtree(g, size);
                g->parent_index = parent_index;
                continue;
            }
            g->created_index = g->record_count;
            if ((int16_t)g->record_count >= FE2_MAX_GENERATED_RECORDS) {
                g->parent_index = parent_index;
                return;
            }
            g->value = size;
            {
                unsigned record = g->destination;
                int created = fe2_systems_create_profile(g, &record);
                g->destination = record;
                if (created) {
                    size = g->value;
                    g->parent_index = (uint16_t)(g->created_index + 1u);
                    g->destination = (g->destination + 1u) % g->space.count;
                    generate_subtree(g, size);
                }
            }
            g->parent_index = parent_index;
        }
    } while (g->curve < CURVE_END);
}
